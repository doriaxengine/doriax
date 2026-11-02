// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "DeleteEntityCmd.h"

#include "Stream.h"
#include "Out.h"
#include "util/CameraTextureLink.h"
#include "util/ProjectUtils.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace doriax;

namespace {

void destroyEntityWithoutReconcile(EntityRegistry* registry, Entity entity) {
    uint64_t structuralFlags = editor::UpdateFlags_None;
    if (registry->isEntityCreated(entity)) { // locked children are deleted by systems with their parent
        if (registry->findComponent<LightComponent>(entity)) {
            structuralFlags |= editor::Catalog::getComponentStructuralUpdateFlags(editor::ComponentType::LightComponent);
        }
        if (registry->findComponent<FogComponent>(entity)) {
            structuralFlags |= editor::Catalog::getComponentStructuralUpdateFlags(editor::ComponentType::FogComponent);
        }
        registry->destroyEntity(entity);
    }
    if (structuralFlags != editor::UpdateFlags_None) {
        editor::Catalog::updateEntity(registry, NULL_ENTITY, structuralFlags);
    }
}

}

editor::DeleteEntityCmd::DeleteEntityCmd(Project* project, uint32_t sceneId, Entity entity, bool allowLockedRoots)
    : DeleteEntityCmd(project, sceneId, std::vector<Entity>{entity}, allowLockedRoots){

}

editor::DeleteEntityCmd::DeleteEntityCmd(Project* project, uint32_t sceneId, const std::vector<Entity>& entities, bool allowLockedRoots)
    : DeleteEntityCmd(project, sceneId, entities, allowLockedRoots, {}){

}

editor::DeleteEntityCmd::DeleteEntityCmd(Project* project, uint32_t sceneId, const std::vector<Entity>& entities, bool allowLockedRoots, const std::vector<Entity>& preservedEntities){
    this->project = project;
    this->sceneId = sceneId;
    this->requestedEntities = entities;
    this->preservedEntities = preservedEntities;
    this->allowLockedRoots = allowLockedRoots;

    for (const Entity& entity : entities){
        if (std::find(preservedEntities.begin(), preservedEntities.end(), entity) == preservedEntities.end()) {
            appendEntityData(entity);
        }
    }

    this->wasModified = project->getScene(sceneId)->isModified;
}

bool editor::DeleteEntityCmd::appendEntityData(Entity entity){
    SceneProject* sceneProject = project ? project->getScene(sceneId) : nullptr;
    if (!sceneProject || !sceneProject->scene || !sceneProject->scene->isEntityCreated(entity)) {
        return false;
    }
    if (std::find_if(entities.begin(), entities.end(), [entity](const DeleteEntityData& data) {
            return data.entity == entity;
        }) != entities.end()) {
        return false;
    }

    DeleteEntityData entityData;
    entityData.entity = entity;

    Scene* scene = sceneProject->scene;
    Signature signature = scene->getSignature(entity);
    if (signature.test(scene->getComponentId<Transform>())) {
        entityData.hasTransform = true;
        entityData.parent = scene->getComponent<Transform>(entity).parent;
    }
    entityData.entityIndex = ProjectUtils::getEntityOrderIndex(scene, sceneProject->entities, entity);

    entities.push_back(std::move(entityData));
    return true;
}

std::vector<size_t> editor::DeleteEntityCmd::prepareDeleteSet(){
    SceneProject* sceneProject = project->getScene(sceneId);
    Scene* scene = sceneProject->scene;
    const std::unordered_set<Entity> preserved(preservedEntities.begin(), preservedEntities.end());
    const std::unordered_set<Entity> tracked(sceneProject->entities.begin(), sceneProject->entities.end());

    std::unordered_map<Entity, std::vector<Entity>> transformChildren;
    auto transforms = scene->getComponentArray<Transform>();
    for (size_t i = 0; i < transforms->size(); ++i) {
        transformChildren[transforms->getComponentFromIndex(i).parent].push_back(transforms->getEntity(i));
    }

    struct DeleteCoverage {
        std::unordered_map<Entity, Entity> entityToRoot;
        std::vector<Entity> allEntities;
    };

    auto collectCoverage = [&](const std::vector<Entity>& roots) {
        DeleteCoverage coverage;

        auto addEntity = [&](Entity entity, Entity root) {
            if (entity == NULL_ENTITY || !scene->isEntityCreated(entity) || coverage.entityToRoot.find(entity) != coverage.entityToRoot.end()) {
                return;
            }
            coverage.entityToRoot[entity] = root;
            coverage.allEntities.push_back(entity);
        };

        for (Entity root : roots) {
            addEntity(root, root);
        }

        for (size_t i = 0; i < coverage.allEntities.size(); ++i) {
            Entity entity = coverage.allEntities[i];
            Entity root = coverage.entityToRoot.at(entity);

            // Bundle roots are opaque hierarchy nodes, and nested roots may themselves be
            // members of an already covered bundle instance.
            if (const BundleComponent* component = scene->findComponent<BundleComponent>(entity);
                component && !component->path.empty()) {
                const EntityBundle* bundle = project->getEntityBundle(component->path);
                const EntityBundle::Instance* instance = bundle ? bundle->getInstance(sceneId, entity) : nullptr;
                if (instance && instance->rootEntity == entity) {
                    for (const EntityBundle::EntityMember& member : instance->members) {
                        addEntity(member.localEntity, root);
                    }
                }
            }

            auto children = transformChildren.find(entity);
            if (children != transformChildren.end()) {
                for (Entity child : children->second) {
                    addEntity(child, root);
                }
            }
        }

        return coverage;
    };

    auto rootEntities = [&]() {
        std::vector<Entity> roots;
        roots.reserve(entities.size());
        for (const DeleteEntityData& data : entities) {
            roots.push_back(data.entity);
        }
        return roots;
    };

    auto forEachOwnedDependency = [&](Entity owner, auto&& callback) {
        for (ComponentType componentType : {ComponentType::ActionComponent, ComponentType::AnimationComponent}) {
            for (Entity dependency : ProjectUtils::getOwnedCascadeEntities(scene, owner, componentType)) {
                callback(dependency);
            }
        }
    };

    auto cascadeTouchesPreserved = [&](Entity root) {
        if (preserved.empty()) {
            return false;
        }

        std::unordered_set<Entity> visited;
        std::function<bool(Entity)> visit = [&](Entity entity) {
            if (!visited.insert(entity).second) {
                return false;
            }

            DeleteCoverage coverage = collectCoverage({entity});
            for (Entity covered : coverage.allEntities) {
                if (preserved.count(covered)) {
                    return true;
                }

                bool touchesPreserved = false;
                forEachOwnedDependency(covered, [&](Entity dependency) {
                    if (!touchesPreserved && dependency != NULL_ENTITY &&
                        scene->isEntityCreated(dependency) && visit(dependency)) {
                        touchesPreserved = true;
                    }
                });
                if (touchesPreserved) {
                    return true;
                }
            }
            return false;
        };

        return visit(root);
    };

    // Removing an owning component must keep its owner alive. If an inconsistent ownership
    // graph points back into a preserved branch, retaining that target is safer than letting
    // an engine callback delete the entity that the outer command still needs to update.
    if (!preserved.empty()) {
        entities.erase(std::remove_if(entities.begin(), entities.end(), [&](const DeleteEntityData& data) {
            return cascadeTouchesPreserved(data.entity);
        }), entities.end());
    }

    // Virtual and owned relationships can introduce new structural roots recursively.
    while (true) {
        DeleteCoverage coverage = collectCoverage(rootEntities());
        bool changed = false;

        for (Entity virtualChild : ProjectUtils::getVirtualChildren(scene, coverage.allEntities, preserved)) {
            if (!cascadeTouchesPreserved(virtualChild) && appendEntityData(virtualChild)) {
                changed = true;
            }
        }

        auto includeDependency = [&](Entity dependency) {
            if (dependency == NULL_ENTITY || preserved.count(dependency) || !scene->isEntityCreated(dependency) ||
                coverage.entityToRoot.find(dependency) != coverage.entityToRoot.end() ||
                !tracked.count(dependency) || cascadeTouchesPreserved(dependency)) {
                return;
            }
            if (appendEntityData(dependency)) {
                changed = true;
            }
        };

        for (Entity owner : coverage.allEntities) {
            forEachOwnedDependency(owner, includeDependency);
        }

        if (!changed) {
            break;
        }
    }

    // Keep only roots not already contained by another root's hierarchy or bundle instance.
    std::unordered_set<Entity> coveredRoots;
    std::vector<Entity> roots = rootEntities();
    for (Entity root : roots) {
        DeleteCoverage rootCoverage = collectCoverage({root});
        for (Entity candidate : roots) {
            if (candidate != root && rootCoverage.entityToRoot.find(candidate) != rootCoverage.entityToRoot.end()) {
                coveredRoots.insert(candidate);
            }
        }
    }
    entities.erase(std::remove_if(entities.begin(), entities.end(), [&](const DeleteEntityData& data) {
        return coveredRoots.count(data.entity) != 0;
    }), entities.end());

    // owner root -> dependency roots that must be deleted first.
    DeleteCoverage coverage = collectCoverage(rootEntities());

    bundleInstanceSnapshots.clear();
    auto rememberBundleInstance = [&](const fs::path& path, Entity root) {
        if (path.empty() || root == NULL_ENTITY ||
            std::find_if(bundleInstanceSnapshots.begin(), bundleInstanceSnapshots.end(), [&](const BundleInstanceSnapshot& saved) {
                return saved.path == path && saved.rootEntity == root;
            }) != bundleInstanceSnapshots.end()) {
            return;
        }
        const EntityBundle* bundle = project->getEntityBundle(path);
        const EntityBundle::Instance* instance = bundle ? bundle->getInstance(sceneId, root) : nullptr;
        if (instance && instance->rootEntity == root) {
            bundleInstanceSnapshots.push_back({path, root, instance->members});
        }
    };

    for (Entity entity : coverage.allEntities) {
        if (const BundleComponent* component = scene->findComponent<BundleComponent>(entity)) {
            rememberBundleInstance(component->path, entity);
        }
    }

    std::unordered_map<Entity, std::vector<Entity>> dependencies;
    auto addDependency = [&](Entity ownerRoot, Entity dependency) {
        auto dependencyIt = coverage.entityToRoot.find(dependency);
        if (dependencyIt == coverage.entityToRoot.end() || dependencyIt->second == ownerRoot) {
            return;
        }
        std::vector<Entity>& roots = dependencies[ownerRoot];
        if (std::find(roots.begin(), roots.end(), dependencyIt->second) == roots.end()) {
            roots.push_back(dependencyIt->second);
        }
    };
    for (Entity owner : coverage.allEntities) {
        Entity ownerRoot = coverage.entityToRoot.at(owner);
        forEachOwnedDependency(owner, [&](Entity dependency) {
            addDependency(ownerRoot, dependency);
        });
    }

    std::unordered_map<Entity, size_t> dataIndex;
    for (size_t i = 0; i < entities.size(); ++i) {
        dataIndex[entities[i].entity] = i;
    }

    std::unordered_set<Entity> visiting;
    std::unordered_set<Entity> emitted;
    std::vector<size_t> deleteOrder;
    deleteOrder.reserve(entities.size());

    std::function<void(Entity)> emit = [&](Entity entity) {
        if (emitted.count(entity) || visiting.count(entity)) {
            return;
        }
        visiting.insert(entity);
        auto depIt = dependencies.find(entity);
        if (depIt != dependencies.end()) {
            for (Entity dependency : depIt->second) {
                emit(dependency);
            }
        }
        visiting.erase(entity);
        if (emitted.insert(entity).second) {
            deleteOrder.push_back(dataIndex.at(entity));
        }
    };

    // Preserve the command's original reverse-delete/forward-undo behavior for unrelated
    // roots while ensuring owned dependencies are explicitly removed before their owners.
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        emit(it->entity);
    }

    return deleteOrder;
}

void editor::DeleteEntityCmd::snapshotEntities(){
    SceneProject* sceneProject = project->getScene(sceneId);
    for (DeleteEntityData& entityData : entities) {
        entityData.data = YAML::Node();
        entityData.recoveryBundleData.clear();
        entityData.isBundleRoot = false;
        entityData.bundlePath.clear();

        if (!sceneProject->scene->isEntityCreated(entityData.entity)) {
            continue;
        }

        entityData.bundlePath = project->findEntityBundlePathFor(sceneId, entityData.entity);
        EntityBundle* bundle = project->getEntityBundle(entityData.bundlePath);

        if (bundle) {
            entityData.isBundleRoot = entityData.entity == bundle->getRootEntity(sceneId, entityData.entity);
            if (entityData.isBundleRoot) {
                entityData.data = Stream::encodeEntity(entityData.entity, sceneProject->scene, project, sceneProject);
            }
        } else {
            entityData.data = Stream::encodeEntity(entityData.entity, sceneProject->scene, project, sceneProject);
        }
    }
}

void editor::DeleteEntityCmd::destroyEntity(EntityRegistry* registry, Entity entity, std::vector<Entity>& entities, Project* project, uint32_t sceneId){
    destroyEntityWithoutReconcile(registry, entity);
    ProjectUtils::reconcileTrackedEntities(registry, entities, project, sceneId);
}

void editor::DeleteEntityCmd::destroyEntities(EntityRegistry* registry,
        const std::vector<Entity>& entitiesToDestroy, std::vector<Entity>& entities,
        Project* project, uint32_t sceneId) {
    if (entitiesToDestroy.empty()) {
        return;
    }
    for (Entity entity : entitiesToDestroy) {
        destroyEntityWithoutReconcile(registry, entity);
    }
    ProjectUtils::reconcileTrackedEntities(registry, entities, project, sceneId);
}

bool editor::DeleteEntityCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    std::vector<Entity> entitiesToDelete;
    auto isRequestedEntity = [&](Entity entity) {
        return std::find(requestedEntities.begin(), requestedEntities.end(), entity) != requestedEntities.end();
    };

    auto it = entities.begin();
    while (it != entities.end()) {
        Entity lockedParent = ProjectUtils::getLockedEntityParent(sceneProject->scene, it->entity);
        bool canDelete = true;
        if (lockedParent != NULL_ENTITY){
            canDelete = (allowLockedRoots && isRequestedEntity(it->entity))
                || isRequestedEntity(lockedParent)
                || std::find(entitiesToDelete.begin(), entitiesToDelete.end(), lockedParent) != entitiesToDelete.end();
        }

        // Imported parts and the groups holding them only go away with their own model
        if (canDelete && !allowLockedRoots){
            Entity modelOwner = ProjectUtils::getModelBranchOwner(sceneProject->scene, it->entity);
            canDelete = modelOwner == NULL_ENTITY
                || isRequestedEntity(modelOwner)
                || std::find(entitiesToDelete.begin(), entitiesToDelete.end(), modelOwner) != entitiesToDelete.end();
        }

        if (!canDelete){
             Out::warning("Cannot delete entity '%u'. It is a locked child of another component.", it->entity);
             it = entities.erase(it);
        } else {
             entitiesToDelete.push_back(it->entity);
             ++it;
        }
    }

    if (entities.empty()){
        return false;
    }

    lastSelected = project->getSelectedEntities(sceneId);
    std::vector<size_t> deleteOrder = prepareDeleteSet();
    snapshotEntities();

    for (size_t index : deleteOrder){
        DeleteEntityData& entityData = entities[index];
        // A previously processed structural root (notably a complete bundle instance) may
        // already have removed this member; that root's snapshot is sufficient for undo.
        if (!sceneProject->scene->isEntityCreated(entityData.entity)) {
            continue;
        }

        EntityBundle* bundle = project->getEntityBundle(entityData.bundlePath);

        if (bundle) {
            if (entityData.isBundleRoot) {
                const auto* inst = bundle->getInstance(sceneId, entityData.entity);
                std::vector<Entity> memberEntities;
                if (inst) {
                    for (const auto& member : inst->members) {
                        memberEntities.push_back(member.localEntity);
                    }
                }
                project->unimportEntityBundle(sceneId, entityData.bundlePath, entityData.entity, memberEntities);
            } else {
                if (!entityData.hasTransform) {
                    entityData.parent = bundle->getRootEntity(sceneId, entityData.entity);
                }
                entityData.recoveryBundleData = project->removeEntityFromBundle(sceneId, entityData.entity, true);
            }
        } else {
            std::vector<Entity> allEntities;
            ProjectUtils::collectEntities(entityData.data, allEntities);

            // A bundle nested below a normal hierarchy root must go through the bundle
            // bookkeeping path before its encoded root is destroyed with the hierarchy.
            for (auto it = allEntities.rbegin(); it != allEntities.rend(); ++it) {
                Entity nestedRoot = *it;
                const BundleComponent* component = sceneProject->scene->findComponent<BundleComponent>(nestedRoot);
                if (!component || component->path.empty()) {
                    continue;
                }
                fs::path nestedPath = component->path;
                EntityBundle* nestedBundle = project->getEntityBundle(nestedPath);
                const EntityBundle::Instance* instance = nestedBundle ? nestedBundle->getInstance(sceneId, nestedRoot) : nullptr;
                if (!instance || instance->rootEntity != nestedRoot) {
                    continue;
                }
                std::vector<Entity> members;
                members.reserve(instance->members.size());
                for (const EntityBundle::EntityMember& member : instance->members) {
                    members.push_back(member.localEntity);
                }
                project->unimportEntityBundle(sceneId, nestedPath, nestedRoot, members);
            }

            destroyEntities(sceneProject->scene, allEntities, sceneProject->entities, project, sceneId);
        }

        sceneProject->isModified = true;
    }

    // unbind framebuffers of textures linked to deleted render-to-texture cameras
    CameraTextureLink::resolve(sceneProject->scene);
    survivingEntities = sceneProject->entities;

    return true;
}

void editor::DeleteEntityCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);
    std::unordered_map<Entity, Entity> entityRemap;
    std::vector<Entity> restoredEntities;

    for (DeleteEntityData& entityData : entities){
        Entity restoredParent = entityData.parent;
        if (auto parentRemap = entityRemap.find(restoredParent); parentRemap != entityRemap.end()) {
            restoredParent = parentRemap->second;
        }

        if (entityData.recoveryBundleData.size() == 0){
            if (!entityData.data.IsMap() || !entityData.data["type"]) {
                continue;
            }

            std::vector<Entity> allEntities = Stream::decodeEntity(entityData.data, sceneProject->scene,
                &sceneProject->entities, project, sceneProject, NULL_ENTITY, true, false, &entityRemap);
            if (allEntities.empty()) {
                continue;
            }
            restoredEntities.insert(restoredEntities.end(), allEntities.begin(), allEntities.end());
            entityData.entity = allEntities[0];

            ProjectUtils::moveEntityOrderByIndex(sceneProject->scene, sceneProject->entities,
                entityData.entity, restoredParent, entityData.entityIndex, entityData.hasTransform);

        }else{

            if (!project->addEntityToBundle(sceneId, entityData.recoveryBundleData, restoredParent,
                true, &entityRemap, &restoredEntities)) {
                Out::error("Could not restore deleted bundle entity %u in scene %u; undo stopped before remapping",
                    entityData.entity, sceneId);
                CameraTextureLink::resolve(sceneProject->scene);
                return;
            }
        }
    }

    // Bundle imports allocate scene-local member IDs outside Stream's YAML decoder. Snapshots
    // are stored outer-first, so each nested root is mapped before its own instance is visited.
    for (const BundleInstanceSnapshot& saved : bundleInstanceSnapshots) {
        Entity root = saved.rootEntity;
        auto rootRemap = entityRemap.find(root);
        if (rootRemap != entityRemap.end()) {
            root = rootRemap->second;
        }

        const EntityBundle* bundle = project->getEntityBundle(saved.path);
        const EntityBundle::Instance* instance = bundle ? bundle->getInstance(sceneId, root) : nullptr;
        if (!instance || instance->rootEntity != root) {
            continue;
        }

        for (const EntityBundle::EntityMember& oldMember : saved.members) {
            auto newMember = std::find_if(instance->members.begin(), instance->members.end(),
                [&](const EntityBundle::EntityMember& member) {
                    return member.registryEntity == oldMember.registryEntity;
                });
            if (newMember != instance->members.end() && newMember->localEntity != oldMember.localEntity) {
                entityRemap[oldMember.localEntity] = newMember->localEntity;
            }
        }
    }

    // recreateEntity normally reclaims the original IDs. If an ID was occupied, decodeEntity
    // allocated a replacement; repair links across the separately decoded snapshots without
    // clearing references to entities that were never part of this delete command.
    if (!entityRemap.empty()) {
        std::vector<Entity> referenceEntities;
        std::unordered_set<Entity> seenReferences;
        auto addReferenceEntity = [&](Entity entity) {
            if (sceneProject->scene->isEntityCreated(entity) && seenReferences.insert(entity).second) {
                referenceEntities.push_back(entity);
            }
        };
        for (Entity entity : restoredEntities) {
            addReferenceEntity(entity);
        }
        for (Entity entity : survivingEntities) {
            addReferenceEntity(entity);
        }
        Project::remapEntityProperties(sceneProject->scene, referenceEntities, entityRemap, false);

        auto remapEntity = [&](Entity& entity) {
            auto it = entityRemap.find(entity);
            if (it != entityRemap.end()) {
                entity = it->second;
            }
        };
        for (DeleteEntityData& entityData : entities) {
            remapEntity(entityData.parent);
            if (!entityData.recoveryBundleData.empty()) {
                remapEntity(entityData.entity);
            }
        }
        for (Entity& entity : requestedEntities) remapEntity(entity);
        for (Entity& entity : preservedEntities) remapEntity(entity);
        for (Entity& entity : lastSelected) remapEntity(entity);
    }

    if (lastSelected.size() > 0){
        project->replaceSelectedEntities(sceneId, lastSelected);
    }

    // recreated cameras get a new framebuffer; rebind linked textures
    CameraTextureLink::resolve(sceneProject->scene);

    sceneProject->isModified = wasModified;
}

bool editor::DeleteEntityCmd::mergeWith(editor::Command* otherCommand){
    DeleteEntityCmd* otherCmd = dynamic_cast<DeleteEntityCmd*>(otherCommand);
    if (otherCmd != nullptr){
        if (project == otherCmd->project && sceneId == otherCmd->sceneId && allowLockedRoots == otherCmd->allowLockedRoots){

            lastSelected = otherCmd->lastSelected;

            for (DeleteEntityData& otherEntityData :  otherCmd->entities){
                entities.push_back(otherEntityData);
            }
            for (Entity entity : otherCmd->preservedEntities) {
                if (std::find(preservedEntities.begin(), preservedEntities.end(), entity) == preservedEntities.end()) {
                    preservedEntities.push_back(entity);
                }
            }
            for (Entity entity : otherCmd->requestedEntities) {
                if (std::find(requestedEntities.begin(), requestedEntities.end(), entity) == requestedEntities.end()) {
                    requestedEntities.push_back(entity);
                }
            }
            for (const BundleInstanceSnapshot& instance : otherCmd->bundleInstanceSnapshots) {
                if (std::find_if(bundleInstanceSnapshots.begin(), bundleInstanceSnapshots.end(), [&](const BundleInstanceSnapshot& saved) {
                        return saved.path == instance.path && saved.rootEntity == instance.rootEntity;
                    }) == bundleInstanceSnapshots.end()) {
                    bundleInstanceSnapshots.push_back(instance);
                }
            }

            wasModified = wasModified && otherCmd->wasModified;

            return true;
        }
    }

    return false;
}
