// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ModelLoadCmd.h"

#include "Stream.h"
#include "util/ProjectUtils.h"
#include "Engine.h"
#include "subsystem/MeshSystem.h"
#include "io/FileData.h"
#include "Backend.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace doriax;

editor::ModelLoadCmd::ModelLoadCmd(Project* project, uint32_t sceneId, Entity entity, const std::string& modelPath){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = entity;
    this->modelPath = modelPath;

    this->wasModified = project->getScene(sceneId)->isModified;
}

editor::ModelLoadCmd::ModelLoadCmd(Project* project, uint32_t sceneId, Entity entity, const std::string& modelPath, bool mergeStaticMeshes)
    : ModelLoadCmd(project, sceneId, entity, modelPath) {
    hasMergeStaticMeshesOverride = true;
    mergeStaticMeshesOverride = mergeStaticMeshes;
}

editor::ModelLoadCmd::ModelLoadCmd(Project* project, uint32_t sceneId, const std::string& entityName, const Vector3& position, const std::string& modelPath){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = NULL_ENTITY;
    this->modelPath = modelPath;

    this->wasModified = project->getScene(sceneId)->isModified;

    createEntityCmd = new CreateEntityCmd(project, sceneId, entityName, EntityCreationType::MODEL);
    createEntityCmd->addProperty<Vector3>(ComponentType::Transform, "position", position);
}

editor::ModelLoadCmd::ModelLoadCmd(Project* project, uint32_t sceneId, const std::string& entityName, Entity parent, const Vector3& position, const Quaternion& rotation, const Vector3& scale, const std::string& modelPath){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = NULL_ENTITY;
    this->modelPath = modelPath;

    this->wasModified = project->getScene(sceneId)->isModified;

    createEntityCmd = new CreateEntityCmd(project, sceneId, entityName, EntityCreationType::MODEL, parent);
    createEntityCmd->setQuiet(true);
    createEntityCmd->addProperty<Vector3>(ComponentType::Transform, "position", position);
    createEntityCmd->addProperty<Quaternion>(ComponentType::Transform, "rotation", rotation);
    createEntityCmd->addProperty<Vector3>(ComponentType::Transform, "scale", scale);
}

editor::ModelLoadCmd::~ModelLoadCmd(){
    if (cancelFlag) cancelFlag->store(true);
    if (asyncPending){
        Scene* scene = project->getScene(sceneId)->scene;
        scene->getSystem<MeshSystem>()->cancelAsyncModelLoad(entity, modelPath);
    }
    if (oldSubEntitiesDeleteCmd) {
        delete oldSubEntitiesDeleteCmd;
        oldSubEntitiesDeleteCmd = nullptr;
    }
    if (createEntityCmd) {
        delete createEntityCmd;
        createEntityCmd = nullptr;
    }
}

std::vector<Entity> editor::ModelLoadCmd::collectModelDeleteRoots(Scene* scene, const ModelComponent& model) {
    std::vector<Entity> roots;
    roots.insert(roots.end(), model.animations.begin(), model.animations.end());

    if (!model.nodesIdMapping.empty()) {
        // Nodes still under a model node go with it; a part the user moved out is its own root
        std::vector<Entity> nodeRoots;
        for (const auto& node : model.nodesIdMapping) {
            Transform* transform = scene->findComponent<Transform>(node.second);
            if (transform && !ProjectUtils::isModelNode(model, transform->parent)) {
                nodeRoots.push_back(node.second);
            }
        }
        // Undo restores each root at its old transform index, so earlier roots must come first
        std::sort(nodeRoots.begin(), nodeRoots.end(), [scene](Entity a, Entity b) {
            return ProjectUtils::getTransformIndex(scene, a) < ProjectUtils::getTransformIndex(scene, b);
        });
        roots.insert(roots.end(), nodeRoots.begin(), nodeRoots.end());
        return roots;
    }

    if (model.skeleton != NULL_ENTITY) {
        roots.push_back(model.skeleton);
    }
    for (const auto& node : model.meshNodesMapping) {
        roots.push_back(node.second);
    }
    return roots;
}

bool editor::ModelLoadCmd::isMappedMeshNode(const ModelComponent& model, Entity entity) {
    for (const auto& node : model.meshNodesMapping) {
        if (node.second == entity) {
            return true;
        }
    }
    return false;
}

editor::ModelLoadCmd::NodeRef editor::ModelLoadCmd::makeNodeRef(const ModelComponent& model, int nodeIndex, bool bone) {
    // Use the file's name even if the user renamed the entity; unnamed nodes use their index.
    return {nodeIndex, MeshSystem::getModelNodeName(model, nodeIndex), bone};
}

Entity editor::ModelLoadCmd::findModelNode(const ModelComponent& model, const NodeRef& ref) {
    if (ref.index < 0) {
        return NULL_ENTITY;
    }

    // The flat path keeps parts and bones in their own maps, and a re-export can switch paths
    const std::map<int, Entity>* nodes = &model.nodesIdMapping;
    if (nodes->empty()) {
        nodes = ref.bone ? &model.bonesIdMapping : &model.meshNodesMapping;
    }
    // Same index and name first, then the name alone, then the index alone.
    auto sameIndex = nodes->find(ref.index);
    if (sameIndex != nodes->end() && MeshSystem::getModelNodeName(model, ref.index) == ref.name) {
        return sameIndex->second;
    }
    if (!ref.name.empty()) {
        for (const auto& node : *nodes) {
            if (MeshSystem::getModelNodeName(model, node.first) == ref.name) {
                return node.second;
            }
        }
    }
    return sameIndex != nodes->end() ? sameIndex->second : NULL_ENTITY;
}

// The local is kept: an offset from a bone is the user's placement, the file drives the rest
void editor::ModelLoadCmd::attachLocal(Scene* scene, Entity child, Entity parent, const LocalPose& pose) {
    scene->addEntityChild(parent, child, false);
    Transform& transform = scene->getComponent<Transform>(child);
    transform.position = pose.position;
    transform.rotation = pose.rotation;
    transform.scale = pose.scale;
    transform.needUpdate = true;
}

std::unordered_map<Entity, editor::ModelLoadCmd::NodeRef> editor::ModelLoadCmd::mapModelNodes(Scene* scene, const ModelComponent& model) {
    std::unordered_map<Entity, NodeRef> modelNodes;
    for (const auto& node : model.nodesIdMapping) {
        modelNodes.emplace(node.second, makeNodeRef(model, node.first, scene->findComponent<BoneComponent>(node.second) != nullptr));
    }
    for (const auto& node : model.meshNodesMapping) {
        modelNodes.emplace(node.second, makeNodeRef(model, node.first, false));
    }
    for (const auto& bone : model.bonesIdMapping) {
        modelNodes.emplace(bone.second, makeNodeRef(model, bone.first, true));
    }
    return modelNodes;
}

std::vector<editor::ModelLoadCmd::ParkedEntity> editor::ModelLoadCmd::collectAttachments(SceneProject* sceneProject, Entity modelEntity, const ModelComponent& model) {
    Scene* scene = sceneProject->scene;
    std::unordered_map<Entity, NodeRef> modelNodes = mapModelNodes(scene, model);

    std::vector<ParkedEntity> attachments;
    for (Entity candidate : sceneProject->entities) {
        Transform* transform = scene->findComponent<Transform>(candidate);
        if (!transform || candidate == modelEntity || modelNodes.count(candidate)) {
            continue;
        }
        auto parent = modelNodes.find(transform->parent);
        if (parent == modelNodes.end()) {
            continue;
        }
        ParkedEntity parked;
        parked.entity = candidate;
        parked.oldParent = transform->parent;
        parked.parent = parent->second;
        parked.pose = {transform->position, transform->rotation, transform->scale};
        attachments.push_back(parked);
    }
    return attachments;
}

void editor::ModelLoadCmd::recordArrangement(SceneProject* sceneProject, const ModelComponent& model, bool parts) {
    parkedEntities = collectAttachments(sceneProject, entity, model);
    if (!parts) {
        return;
    }

    Scene* scene = sceneProject->scene;
    std::unordered_map<Entity, NodeRef> modelNodes = mapModelNodes(scene, model);
    std::map<int, Entity> defaults = scene->getSystem<MeshSystem>()->getModelNodeDefaultParents(entity, model);
    for (const auto& node : model.meshNodesMapping) {
        Transform* transform = scene->findComponent<Transform>(node.second);
        auto defaultParent = defaults.find(node.first);
        if (!transform || defaultParent == defaults.end() || transform->parent == defaultParent->second) {
            continue;
        }
        MovedPart moved;
        moved.part = makeNodeRef(model, node.first, false);
        auto parent = modelNodes.find(transform->parent);
        if (parent != modelNodes.end()) {
            moved.nodeParent = parent->second;
        } else {
            moved.userParent = transform->parent;
        }
        moved.pose = {transform->position, transform->rotation, transform->scale};
        movedParts.push_back(moved);
    }
}

void editor::ModelLoadCmd::parkEntities(SceneProject* sceneProject, Entity modelEntity, const std::vector<ParkedEntity>& parked) {
    if (parked.empty()) return;
    Scene* scene = sceneProject->scene;
    for (const ParkedEntity& entry : parked) {
        if (scene->isEntityCreated(entry.entity)) {
            scene->addEntityChild(modelEntity, entry.entity, true);
        }
    }
    ProjectUtils::sortEntitiesByTransformOrder(scene, sceneProject->entities);
}

void editor::ModelLoadCmd::unparkEntities(SceneProject* sceneProject, const std::vector<ParkedEntity>& parked) {
    if (parked.empty()) return;
    Scene* scene = sceneProject->scene;
    for (const ParkedEntity& entry : parked) {
        if (scene->isEntityCreated(entry.entity) && scene->isEntityCreated(entry.oldParent)) {
            attachLocal(scene, entry.entity, entry.oldParent, entry.pose);
        }
    }
    ProjectUtils::sortEntitiesByTransformOrder(scene, sceneProject->entities);
}

void editor::ModelLoadCmd::attachToNodes(Scene* scene, const ModelComponent& model, const std::vector<ParkedEntity>& parked) {
    for (const ParkedEntity& entry : parked) {
        if (!scene->isEntityCreated(entry.entity)) {
            continue;
        }
        Entity parent = findModelNode(model, entry.parent);
        if (parent == NULL_ENTITY) {
            Log::warn("Node %d '%s' is no longer in '%s', '%s' was moved to the model", entry.parent.index,
                entry.parent.name.c_str(), model.filename.c_str(), scene->getEntityName(entry.entity).c_str());
            continue;
        }
        attachLocal(scene, entry.entity, parent, entry.pose);
    }
}

void editor::ModelLoadCmd::restoreArrangement(SceneProject* sceneProject, const ModelComponent& model) {
    if (parkedEntities.empty() && movedParts.empty()) return;
    Scene* scene = sceneProject->scene;
    std::shared_ptr<MeshSystem> meshSys = scene->getSystem<MeshSystem>();

    attachToNodes(scene, model, parkedEntities);

    // Validate against the complete new hierarchy before detaching any parts.
    struct Placement {
        Entity part;
        Entity parent;
        Entity defaultParent;
        const MovedPart* moved;
    };
    std::vector<Placement> placements;
    std::unordered_set<Entity> placedParts;
    for (const MovedPart& moved : movedParts) {
        Entity part = findModelNode(model, moved.part);
        Entity parent = moved.userParent != NULL_ENTITY ? moved.userParent : findModelNode(model, moved.nodeParent);
        std::string reason;
        if (part == NULL_ENTITY || !isMappedMeshNode(model, part)) {
            reason = "it is no longer a mesh part";
        } else if (placedParts.count(part)) {
            reason = "another saved part already matched this node";
        } else if (meshSys->canEditModelPart(model, part, &reason) && (parent == NULL_ENTITY || !scene->isEntityCreated(parent))) {
            reason = "its parent is gone";
        }
        if (!reason.empty()) {
            Log::warn("Mesh part %d '%s' of '%s' stays where the file puts it: %s", moved.part.index,
                moved.part.name.c_str(), model.filename.c_str(), reason.c_str());
            continue;
        }
        placedParts.insert(part);
        placements.push_back({part, parent, scene->getComponent<Transform>(part).parent, &moved});
    }
    // Detach all accepted parts so reversing a parent/child arrangement is independent of index order.
    for (const Placement& placement : placements) {
        scene->addEntityChild(entity, placement.part, false);
    }
    for (const Placement& placement : placements) {
        if (placement.part == placement.parent || scene->isParentOf(placement.part, placement.parent)) {
            Log::warn("Cannot restore mesh part %d '%s' of '%s': it would contain its parent",
                placement.moved->part.index, placement.moved->part.name.c_str(), model.filename.c_str());
            // Back to where the loader put it, unless that is now below the part as well
            if (!scene->isParentOf(placement.part, placement.defaultParent)) {
                scene->addEntityChild(placement.defaultParent, placement.part, false);
            }
            continue;
        }
        attachLocal(scene, placement.part, placement.parent, placement.moved->pose);
    }

    ProjectUtils::sortEntitiesByTransformOrder(scene, sceneProject->entities);
}

bool editor::ModelLoadCmd::tryLoad(){
    Scene* scene = project->getScene(sceneId)->scene;
    std::shared_ptr<MeshSystem> meshSys = scene->getSystem<MeshSystem>();
    bool useAsync = Engine::isAsyncLoading();
    std::string ext = FileData::getFilePathExtension(modelPath);
    if (ext == "obj"){
        return meshSys->loadOBJ(entity, modelPath, useAsync);
    }
    return meshSys->loadGLTF(entity, modelPath, useAsync, reuseHierarchy, isNewModel);
}

void editor::ModelLoadCmd::finalizeLoad(){
    SceneProject* sceneProject = project->getScene(sceneId);
    Scene* scene = sceneProject->scene;

    ModelComponent& newModel = scene->getComponent<ModelComponent>(entity);
    newModel.needUpdateModel = false;

    std::vector<Entity> newSubEntities;
    ProjectUtils::collectModelEntities(scene, newModel, newSubEntities);
    for (const auto& e : newSubEntities){
        if (std::find(sceneProject->entities.begin(), sceneProject->entities.end(), e) == sceneProject->entities.end()){
            sceneProject->entities.push_back(e);
        }
    }
    // Nodes come in file order, which often lists children before their parents
    ProjectUtils::sortEntitiesByTransformOrder(scene, sceneProject->entities);

    for (Entity reused : reusedEntities) {
        if (!isMappedMeshNode(newModel, reused)) {
            Log::warn("Mesh part '%s' is no longer in '%s' and became a plain entity",
                scene->getEntityName(reused).c_str(), newModel.filename.c_str());
        }
    }

    if (sameModelFile) {
        restoreArrangement(sceneProject, newModel);
    }

    // Put back on whichever mesh the primitive ended up in, which a merge change moves.
    scene->getSystem<MeshSystem>()->applySubmeshOverrides(savedSubmeshOverrides, entity, newModel);
    if (sameModelFile && !reuseHierarchy) {
        scene->getSystem<MeshSystem>()->applyMeshSettings(savedMeshSettings, entity, newModel);
    }

    sceneProject->isModified = true;

    if (project->isEntityInBundle(sceneId, entity)){
        std::vector<std::string> properties = {"filename"};
        if (mergeStaticMeshesChanged) properties.push_back("mergeStaticMeshes");
        project->bundlePropertyChanged(sceneId, entity, ComponentType::ModelComponent, properties);
    }
}

void editor::ModelLoadCmd::schedulePoll(){
    auto cancel = cancelFlag;
    Backend::getApp().enqueueMainThreadTask([this, cancel]() {
        if (cancel->load()) return;
        if (tryLoad()){
            asyncPending = false;
            finalizeLoad();
            return;
        }
        Scene* scene = project->getScene(sceneId)->scene;
        if (scene->getSystem<MeshSystem>()->isAsyncModelLoadPending(entity, modelPath)){
            schedulePoll();
        } else {
            // Terminal worker failure — drop pending state; rollback happens via the next undo
            asyncPending = false;
        }
    });
}

bool editor::ModelLoadCmd::execute(){
    // An FBX loads from its import, created here the first time
    if (!project->prepareModelFile(modelPath)) {
        return false;
    }

    if (createEntityCmd) {
        if (!createEntityCmd->execute()) {
            return false;
        }
        entity = createEntityCmd->getEntity();
    }

    SceneProject* sceneProject = project->getScene(sceneId);
    Scene* scene = sceneProject->scene;

    Signature signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<Transform>())){
        Log::error("Entity %lu does not have a Transform component", entity);
        return false;
    }
    if (!signature.test(scene->getComponentId<ModelComponent>())){
        Log::error("Entity %lu does not have a ModelComponent", entity);
        return false;
    }

    Transform& transform = scene->getComponent<Transform>(entity);
    MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
    ModelComponent& model = scene->getComponent<ModelComponent>(entity);

    if (hasMergeStaticMeshesOverride && mergeStaticMeshesOverride) {
        std::string reason;
        if (!scene->getSystem<MeshSystem>()->canMergeStaticModel(model, &reason)) {
            Log::warn("Cannot merge static model '%s': %s", model.filename.c_str(), reason.c_str());
            return false;
        }
        if (ProjectUtils::hasCustomMeshParenting(scene, entity)) {
            Log::warn("Cannot merge static model '%s': reset the mesh parenting first", model.filename.c_str());
            return false;
        }
    }

    isNewModel = model.filename.empty();

    // Save old component state
    const bool firstExecution = !oldModel.IsMap();
    oldTransform = Stream::encodeTransform(transform);
    oldMesh = mesh ? Stream::encodeMeshComponent(*mesh, false, false) : YAML::Node();
    oldModel = Stream::encodeModelComponent(model);

    // This is an import option for the current model asset, not an entity-wide preference.
    // A regular file assignment therefore returns to the default hierarchy behavior; the
    // explicit Structure action is the only path that opts a model into static merging.
    const bool requestedMergeStaticMeshes = hasMergeStaticMeshesOverride
        ? mergeStaticMeshesOverride
        : false;
    mergeStaticMeshesChanged = model.mergeStaticMeshes != requestedMergeStaticMeshes;

    sameModelFile = MeshSystem::getModelFilenameKey(model.filename) == MeshSystem::getModelFilenameKey(modelPath);

    // Only a reload that would throw away a local arrangement keeps the mesh children and
    // refreshes their geometry in place. Everything else rebuilds, so node changes still apply.
    reuseHierarchy = sameModelFile && !mergeStaticMeshesChanged
        && model.nodesIdMapping.empty() && ProjectUtils::hasCustomMeshParenting(scene, entity);

    reusedEntities.clear();
    if (reuseHierarchy) {
        for (const auto& node : model.meshNodesMapping) {
            reusedEntities.push_back(node.second);
        }
    }

    // A different asset is a fresh import and its own materials win. Reloading the same file keeps
    // the submesh edits, taken now because the meshes holding them are deleted below. A reuse keeps
    // those meshes, so the loader restores them and these ordinals would be the pre-remap ones.
    if (sameModelFile && !reuseHierarchy) {
        savedSubmeshOverrides = scene->getSystem<MeshSystem>()->collectSubmeshOverrides(entity, model);
    } else if (!sameModelFile && mesh) {
        for (unsigned int i = 0; i < mesh->numSubmeshes; i++) {
            mesh->submeshes[i].overrideFields = 0;
        }
    }
    // The settings go back on a reload of the same file, and on the runtime nodes an undo builds
    // again. Only the first run has the parsed glTF to name their nodes, so a redo keeps them.
    if (firstExecution) {
        savedMeshSettings = scene->getSystem<MeshSystem>()->collectMeshSettings(entity, model, model.filename);
    }

    // Rebuilt nodes get the user's arrangement back by node name. Only the first run has the
    // parsed glTF to name the nodes; a redo reuses what it recorded. Runtime nodes have no delete
    // snapshot, so the entities on them are also kept when the file changes.
    if (firstExecution && !reuseHierarchy && (sameModelFile || !ProjectUtils::hasTrackedModelEntities(scene, model, sceneProject->entities))) {
        recordArrangement(sceneProject, model, sameModelFile);
    }
    parkEntities(sceneProject, entity, parkedEntities);

    std::vector<Entity> oldSubEntityRoots;
    if (!reuseHierarchy) {
        oldSubEntityRoots = collectModelDeleteRoots(scene, model);
    }
    if (!oldSubEntityRoots.empty()) {
        oldSubEntitiesDeleteCmd = new DeleteEntityCmd(project, sceneId, oldSubEntityRoots, true);
        if (!oldSubEntitiesDeleteCmd->execute()) {
            delete oldSubEntitiesDeleteCmd;
            oldSubEntitiesDeleteCmd = nullptr;
            unparkEntities(sceneProject, parkedEntities);
            return false;
        }
    }

    // Clear stale model data before loading new model
    oldRuntimeNodesDestroyed = false;
    if (!reuseHierarchy) {
        oldRuntimeNodesDestroyed = ProjectUtils::destroyModelNodes(scene, model);
    }
    model.mergeStaticMeshes = requestedMergeStaticMeshes;

    if (tryLoad()){
        finalizeLoad();
        return true;
    }

    if (Engine::isAsyncLoading()){
        // Load is in progress on a worker thread — accept the command and finalize when ready
        asyncPending = true;
        cancelFlag = std::make_shared<std::atomic<bool>>(false);
        schedulePoll();
        return true;
    }

    // Synchronous failure — restore old state
    restoreTransform(scene);
    restoreMesh(scene);
    scene->getComponent<ModelComponent>(entity) = Stream::decodeModelComponent(oldModel);
    if (oldSubEntitiesDeleteCmd) {
        oldSubEntitiesDeleteCmd->undo();
        delete oldSubEntitiesDeleteCmd;
        oldSubEntitiesDeleteCmd = nullptr;
    }
    rebuildRuntimeNodes(sceneProject);
    unparkEntities(sceneProject, parkedEntities);
    if (createEntityCmd) {
        createEntityCmd->undo();
    }
    return false;
}

// The parent is not encoded, so it stays
void editor::ModelLoadCmd::restoreTransform(Scene* scene){
    Transform& transform = scene->getComponent<Transform>(entity);
    transform = Stream::decodeTransform(oldTransform, &transform);
}

// The load adds or removes the model's own mesh, so this puts back whichever it had
void editor::ModelLoadCmd::restoreMesh(Scene* scene){
    MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
    if (!oldMesh.IsMap()){
        if (mesh){
            scene->removeComponent<MeshComponent>(entity);
        }
    }else if (mesh){
        *mesh = Stream::decodeMeshComponent(oldMesh);
    }else{
        scene->addComponent<MeshComponent>(entity, Stream::decodeMeshComponent(oldMesh));
    }
}

// Runtime nodes have no snapshot, the restored file builds them again
void editor::ModelLoadCmd::rebuildRuntimeNodes(SceneProject* sceneProject){
    if (!oldRuntimeNodesDestroyed) return;
    oldRuntimeNodesDestroyed = false;

    if (rebuildNodes(sceneProject->scene, entity, savedMeshSettings)) {
        restoreArrangement(sceneProject, sceneProject->scene->getComponent<ModelComponent>(entity));
    }
}

bool editor::ModelLoadCmd::rebuildNodes(Scene* scene, Entity entity, const MeshSystem::MeshSettingsByNode& settings){
    // The mapping points at the destroyed nodes, and the update retries a failed load
    ModelComponent& model = scene->getComponent<ModelComponent>(entity);
    ProjectUtils::clearModelNodes(model);
    model.loadedFilename.clear();
    model.needUpdateModel = true;

    const std::string filename = model.filename;
    std::shared_ptr<MeshSystem> meshSys = scene->getSystem<MeshSystem>();
    const bool loaded = FileData::getFilePathExtension(filename) == "obj"
        ? meshSys->loadOBJ(entity, filename)
        : meshSys->loadGLTF(entity, filename, false, false, false);
    if (loaded) {
        ModelComponent& loadedModel = scene->getComponent<ModelComponent>(entity);
        loadedModel.needUpdateModel = false;
        meshSys->applyMeshSettings(settings, entity, loadedModel);
    }
    return loaded;
}

void editor::ModelLoadCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    Scene* scene = sceneProject->scene;

    if (asyncPending) {
        // Async load still in flight — cancel it; no sub-entities have been created yet
        if (cancelFlag) cancelFlag->store(true);
        scene->getSystem<MeshSystem>()->cancelAsyncModelLoad(entity, modelPath);
        asyncPending = false;
    } else {
        // The attached entities sit on nodes this load created, which are removed below
        parkEntities(sceneProject, entity, parkedEntities);

        ModelComponent& model = scene->getComponent<ModelComponent>(entity);
        std::vector<Entity> newSubEntityRoots = collectModelDeleteRoots(scene, model);
        if (reuseHierarchy) {
            // The kept children stay, so only the nodes this load added are removed
            newSubEntityRoots.erase(std::remove_if(newSubEntityRoots.begin(), newSubEntityRoots.end(),
                [this](Entity root) {
                    return std::find(reusedEntities.begin(), reusedEntities.end(), root) != reusedEntities.end();
                }), newSubEntityRoots.end());
        }
        if (!newSubEntityRoots.empty()) {
            DeleteEntityCmd newSubEntitiesDeleteCmd(project, sceneId, newSubEntityRoots, true);
            newSubEntitiesDeleteCmd.execute();
        }
    }

    // Restore old components
    restoreTransform(scene);
    restoreMesh(scene);
    scene->getComponent<ModelComponent>(entity) = Stream::decodeModelComponent(oldModel);

    if (oldSubEntitiesDeleteCmd) {
        oldSubEntitiesDeleteCmd->undo();
        delete oldSubEntitiesDeleteCmd;
        oldSubEntitiesDeleteCmd = nullptr;
    }
    rebuildRuntimeNodes(sceneProject);
    unparkEntities(sceneProject, parkedEntities);

    sceneProject->isModified = wasModified;

    if (project->isEntityInBundle(sceneId, entity)){
        std::vector<std::string> properties = {"filename"};
        if (mergeStaticMeshesChanged) properties.push_back("mergeStaticMeshes");
        project->bundlePropertyChanged(sceneId, entity, ComponentType::ModelComponent, properties);
    }

    if (createEntityCmd) {
        createEntityCmd->undo();
        entity = NULL_ENTITY;
    }
}

bool editor::ModelLoadCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
