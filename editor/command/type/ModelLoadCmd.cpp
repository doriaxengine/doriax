#include "ModelLoadCmd.h"

#include "Stream.h"
#include "util/ProjectUtils.h"
#include "subsystem/MeshSystem.h"
#include "io/FileData.h"

using namespace doriax;

editor::ModelLoadCmd::ModelLoadCmd(Project* project, uint32_t sceneId, Entity entity, const std::string& modelPath){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = entity;
    this->modelPath = modelPath;

    this->wasModified = project->getScene(sceneId)->isModified;
}

editor::ModelLoadCmd::~ModelLoadCmd(){
    if (oldSubEntitiesDeleteCmd) {
        delete oldSubEntitiesDeleteCmd;
        oldSubEntitiesDeleteCmd = nullptr;
    }
}

std::vector<Entity> editor::ModelLoadCmd::collectModelDeleteRoots(const ModelComponent& model) {
    std::vector<Entity> roots;
    if (model.skeleton != NULL_ENTITY) {
        roots.push_back(model.skeleton);
    }

    roots.insert(roots.end(), model.animations.begin(), model.animations.end());
    return roots;
}

bool editor::ModelLoadCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    Scene* scene = sceneProject->scene;

    Signature signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<Transform>())){
        Log::error("Entity %lu does not have a Transform component", entity);
        return false;
    }
    if (!signature.test(scene->getComponentId<MeshComponent>())){
        Log::error("Entity %lu does not have a MeshComponent", entity);
        return false;
    }
    if (!signature.test(scene->getComponentId<ModelComponent>())){
        Log::error("Entity %lu does not have a ModelComponent", entity);
        return false;
    }

    Transform& transform = scene->getComponent<Transform>(entity);
    MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
    ModelComponent& model = scene->getComponent<ModelComponent>(entity);

    bool isNewModel = model.filename.empty();

    // Save old component state
    oldTransform = Stream::encodeTransform(transform);
    oldMesh = Stream::encodeMeshComponent(mesh, false);
    oldModel = Stream::encodeModelComponent(model);

    std::vector<Entity> oldSubEntityRoots = collectModelDeleteRoots(model);
    if (!oldSubEntityRoots.empty()) {
        oldSubEntitiesDeleteCmd = new DeleteEntityCmd(project, sceneId, oldSubEntityRoots, true);
        if (!oldSubEntitiesDeleteCmd->execute()) {
            delete oldSubEntitiesDeleteCmd;
            oldSubEntitiesDeleteCmd = nullptr;
            return false;
        }
    }

    // Clear stale model data before loading new model
    model.skeleton = NULL_ENTITY;
    model.bonesIdMapping.clear();
    model.bonesNameMapping.clear();
    model.animations.clear();

    // Load the new model
    std::shared_ptr<MeshSystem> meshSys = scene->getSystem<MeshSystem>();
    std::string ext = FileData::getFilePathExtension(modelPath);
    bool ret = false;
    if (ext == "obj"){
        ret = meshSys->loadOBJ(entity, modelPath, true);
    }else{
        ret = meshSys->loadGLTF(entity, modelPath, true, false, isNewModel);
    }

    if (!ret){
        // Restore old state on failure
        scene->getComponent<Transform>(entity) = Stream::decodeTransform(oldTransform);
        scene->getComponent<MeshComponent>(entity) = Stream::decodeMeshComponent(oldMesh);
        scene->getComponent<ModelComponent>(entity) = Stream::decodeModelComponent(oldModel);
        if (oldSubEntitiesDeleteCmd) {
            oldSubEntitiesDeleteCmd->undo();
            delete oldSubEntitiesDeleteCmd;
            oldSubEntitiesDeleteCmd = nullptr;
        }
        return false;
    }

    // Track new model sub-entities in scene
    {
        ModelComponent& newModel = scene->getComponent<ModelComponent>(entity);
        newModel.needUpdateModel = false;
        std::vector<Entity> newSubEntities;
        ProjectUtils::collectModelEntities(scene, newModel, newSubEntities);
        for (const auto& e : newSubEntities){
            sceneProject->entities.push_back(e);
        }
    }

    sceneProject->isModified = true;

    if (project->isEntityInBundle(sceneId, entity)){
        project->bundlePropertyChanged(sceneId, entity, ComponentType::ModelComponent, {"filename"});
    }

    return true;
}

void editor::ModelLoadCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    Scene* scene = sceneProject->scene;

    ModelComponent& model = scene->getComponent<ModelComponent>(entity);

    std::vector<Entity> newSubEntityRoots = collectModelDeleteRoots(model);
    if (!newSubEntityRoots.empty()) {
        DeleteEntityCmd newSubEntitiesDeleteCmd(project, sceneId, newSubEntityRoots, true);
        newSubEntitiesDeleteCmd.execute();
    }

    // Restore old components
    scene->getComponent<Transform>(entity) = Stream::decodeTransform(oldTransform);
    scene->getComponent<MeshComponent>(entity) = Stream::decodeMeshComponent(oldMesh);
    scene->getComponent<ModelComponent>(entity) = Stream::decodeModelComponent(oldModel);

    if (oldSubEntitiesDeleteCmd) {
        oldSubEntitiesDeleteCmd->undo();
        delete oldSubEntitiesDeleteCmd;
        oldSubEntitiesDeleteCmd = nullptr;
    }

    sceneProject->isModified = wasModified;

    if (project->isEntityInBundle(sceneId, entity)){
        project->bundlePropertyChanged(sceneId, entity, ComponentType::ModelComponent, {"filename"});
    }
}

bool editor::ModelLoadCmd::mergeWith(editor::Command* otherCommand){
    return false;
}
