#include "Project.h"
#include "Factory.h"

#include "Backend.h"
#include "window/TerrainEditWindow.h"

#include <fstream>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <unordered_set>
#include <algorithm>
#include <unordered_map>
#include <limits>

#include "render/SceneRender2D.h"
#include "render/SceneRender3D.h"

#include "lua.hpp"
#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "AppSettings.h"
#include "Out.h"
#include "subsystem/MeshSystem.h"
#include "subsystem/UISystem.h"
#include "command/CommandHandle.h"
#include "command/type/DeleteEntityCmd.h"
#include "command/type/CreateEntityCmd.h"
#include "command/type/MoveEntityOrderCmd.h"
#include "Stream.h"
#include "util/FileDialogs.h"
#include "util/SHA1.h"
#include "util/GraphicUtils.h"
#include "util/ProjectUtils.h"

#include "texture/Texture.h"
#include "pool/ShaderPool.h"
#include "SceneManager.h"
#include "BundleManager.h"

using namespace doriax;

std::vector<Entity> editor::Project::getTopLevelEntities(const EntityRegistry* registry, const std::vector<Entity>& orderedEntities) {
    std::unordered_set<Entity> entitySet(orderedEntities.begin(), orderedEntities.end());
    std::vector<Entity> topLevelEntities;
    topLevelEntities.reserve(orderedEntities.size());

    for (Entity entity : orderedEntities) {
        if (registry->getSignature(entity).test(registry->getComponentId<Transform>())) {
            const Transform& transform = registry->getComponent<Transform>(entity);
            if (entitySet.find(transform.parent) != entitySet.end()) {
                continue;
            }
        }
        topLevelEntities.push_back(entity);
    }

    return topLevelEntities;
}

void editor::Project::remapEntityProperties(EntityRegistry* registry, const std::vector<Entity>& entities, const std::unordered_map<Entity, Entity>& entityMap) {
    if (entityMap.empty()) {
        return;
    }

    for (Entity entity : entities) {
        std::vector<ComponentType> components = Catalog::findComponents(registry, entity);
        for (ComponentType componentType : components) {
            auto properties = Catalog::findEntityProperties(registry, entity, componentType);
            int updateFlags = 0;

            for (auto& [propertyName, property] : properties) {
                if (!property.ref || (property.type != PropertyType::Entity && property.type != PropertyType::EntityReference)) {
                    continue;
                }

                Entity* value = nullptr;
                if (property.type == PropertyType::Entity) {
                    value = static_cast<Entity*>(property.ref);
                } else if (property.type == PropertyType::EntityReference) {
                    EntityReference* ref = static_cast<EntityReference*>(property.ref);
                    if (ref->sceneId != 0) {
                        continue; // Cross-scene reference, entityMap doesn't apply
                    }
                    value = &ref->entity;
                }

                if (!value || *value == NULL_ENTITY) {
                    continue;
                }

                auto it = entityMap.find(*value);
                if (it == entityMap.end() || it->second == *value) {
                    continue;
                }

                *value = it->second;
                updateFlags |= property.updateFlags;
            }

            if (updateFlags != 0) {
                Catalog::updateEntity(registry, entity, updateFlags);
            }
        }
    }
}

void editor::Project::remapEntityPropertiesInComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, const std::vector<std::string>& properties, const std::unordered_map<Entity, Entity>& entityMap) {
    if (entityMap.empty()) return;

    auto allProperties = Catalog::findEntityProperties(registry, entity, componentType);

    // For ScriptComponent, build a set of cross-scene property names to skip
    std::unordered_set<std::string> crossSceneProps;
    if (componentType == ComponentType::ScriptComponent) {
        ScriptComponent* scriptComp = registry->findComponent<ScriptComponent>(entity);
        if (scriptComp) {
            for (size_t i = 0; i < scriptComp->scripts.size(); i++) {
                for (auto& prop : scriptComp->scripts[i].properties) {
                    if (prop.type == ScriptPropertyType::EntityReference && std::get<EntityReference>(prop.value).sceneId != 0) {
                        crossSceneProps.insert("scripts[" + std::to_string(i) + "]." + prop.name);
                    }
                }
            }
        }
    }

    for (auto& [propertyName, property] : allProperties) {
        if (!property.ref || (property.type != PropertyType::Entity && property.type != PropertyType::EntityReference)) {
            continue;
        }

        // Skip cross-scene entity references (they belong to another scene)
        if (!crossSceneProps.empty() && crossSceneProps.count(propertyName)) {
            continue;
        }

        // If specific properties were given, only remap those
        if (!properties.empty() && std::find(properties.begin(), properties.end(), propertyName) == properties.end()) {
            continue;
        }

        Entity* value = nullptr;
        if (property.type == PropertyType::Entity) {
            value = static_cast<Entity*>(property.ref);
        } else if (property.type == PropertyType::EntityReference) {
            EntityReference* ref = static_cast<EntityReference*>(property.ref);
            if (ref->sceneId != 0) {
                continue; // Cross-scene reference, entityMap doesn't apply
            }
            value = &ref->entity;
        }

        if (!value || *value == NULL_ENTITY) {
            continue;
        }

        auto it = entityMap.find(*value);
        if (it != entityMap.end()) {
            *value = it->second;
        }
    }
}

editor::Project::Project(){
    resetConfigs();
}

fs::path editor::Project::normalizeToProjectRelative(const fs::path& path) const {
    if (path.empty()) {
        return {};
    }

    fs::path normalizedPath = path.lexically_normal();
    std::error_code ec;

    if (normalizedPath.is_absolute()) {
        fs::path relativePath = fs::relative(normalizedPath, projectPath, ec);
        if (!ec) {
            return relativePath.lexically_normal();
        }
    }

    return normalizedPath;
}

bool editor::Project::matchesRelativePath(const fs::path& relativeBase, const fs::path& currentPath) {
    if (relativeBase.empty() || currentPath.empty()) {
        return false;
    }

    const std::string relativeBaseStr = relativeBase.lexically_normal().generic_string();
    const std::string currentPathStr = currentPath.lexically_normal().generic_string();
    const std::string relativePrefix = relativeBaseStr + "/";

    return currentPathStr == relativeBaseStr ||
           (!relativeBaseStr.empty() && currentPathStr.rfind(relativePrefix, 0) == 0);
}

bool editor::Project::matchesRelativeString(const fs::path& relativeBase, const std::string& currentPath) {
    if (relativeBase.empty() || currentPath.empty()) {
        return false;
    }

    return matchesRelativePath(relativeBase, fs::path(currentPath));
}

bool editor::Project::remapRelativePath(const fs::path& oldRelative, const fs::path& newRelative,
                                        const fs::path& currentPath, fs::path& updatedPath) {
    if (oldRelative.empty() || newRelative.empty() || currentPath.empty()) {
        return false;
    }

    const std::string oldRelativeStr = oldRelative.lexically_normal().generic_string();
    const std::string currentPathStr = currentPath.lexically_normal().generic_string();
    const bool isExactMatch = (currentPathStr == oldRelativeStr);
    const bool isChildMatch = matchesRelativePath(oldRelative, currentPath) && !isExactMatch;

    if (!isExactMatch && !isChildMatch) {
        return false;
    }

    std::string updated = newRelative.generic_string();
    if (isChildMatch) {
        updated += currentPathStr.substr(oldRelativeStr.size());
    }

    updatedPath = fs::path(updated).lexically_normal();
    return true;
}

bool editor::Project::remapRelativeString(const fs::path& oldRelative, const fs::path& newRelative,
                                          const std::string& currentPath, std::string& updatedPath) {
    fs::path updated;
    if (!remapRelativePath(oldRelative, newRelative, fs::path(currentPath), updated)) {
        return false;
    }

    updatedPath = updated.generic_string();
    return true;
}

bool editor::Project::remapScriptEntryPaths(ScriptEntry& scriptEntry, const fs::path& oldRelative,
                                            const fs::path& newRelative) {
    bool changed = false;

    if (!scriptEntry.path.empty()) {
        std::string updatedPath;
        if (remapRelativeString(oldRelative, newRelative, scriptEntry.path, updatedPath)) {
            scriptEntry.path = updatedPath;
            changed = true;
        }
    }

    if (!scriptEntry.headerPath.empty()) {
        std::string updatedHeaderPath;
        if (remapRelativeString(oldRelative, newRelative, scriptEntry.headerPath, updatedHeaderPath)) {
            scriptEntry.headerPath = updatedHeaderPath;
            changed = true;
        }
    }

    return changed;
}

bool editor::Project::remapScriptPathsInRegistry(EntityRegistry* registry, const fs::path& oldRelative,
                                                 const fs::path& newRelative) {
    if (!registry) {
        return false;
    }

    auto scriptsArray = registry->getComponentArray<ScriptComponent>();
    bool changed = false;

    for (size_t i = 0; i < scriptsArray->size(); ++i) {
        ScriptComponent& scriptComponent = scriptsArray->getComponentFromIndex(i);
        for (auto& scriptEntry : scriptComponent.scripts) {
            changed |= remapScriptEntryPaths(scriptEntry, oldRelative, newRelative);
        }
    }

    return changed;
}

bool editor::Project::cleanupScriptPathsInRegistry(EntityRegistry* registry, const fs::path& deletedRelative) {
    if (!registry) {
        return false;
    }

    auto scriptsArray = registry->getComponentArray<ScriptComponent>();
    bool changed = false;

    for (size_t i = 0; i < scriptsArray->size(); ++i) {
        ScriptComponent& scriptComponent = scriptsArray->getComponentFromIndex(i);
        const size_t originalSize = scriptComponent.scripts.size();

        scriptComponent.scripts.erase(
            std::remove_if(scriptComponent.scripts.begin(), scriptComponent.scripts.end(),
                [&deletedRelative](const ScriptEntry& scriptEntry) {
                    return matchesRelativeString(deletedRelative, scriptEntry.path) ||
                           matchesRelativeString(deletedRelative, scriptEntry.headerPath);
                }),
            scriptComponent.scripts.end());

        changed |= (scriptComponent.scripts.size() != originalSize);
    }

    return changed;
}

void editor::Project::linkMaterialFile(uint32_t sceneId, Entity entity, unsigned int submeshIndex, const std::string& filePath) {
    MaterialLinkKey key{sceneId, entity, submeshIndex};
    MaterialLinkEntry entry;
    entry.filePath = fs::path(filePath).lexically_normal().generic_string();

    fs::path absolutePath = projectPath / entry.filePath;
    std::error_code ec;
    entry.lastWriteTime = fs::last_write_time(absolutePath, ec);

    materialFileLinks[key] = entry;
}

bool editor::Project::isMaterialFileLinked(uint32_t sceneId, Entity entity, unsigned int submeshIndex) const {
    return materialFileLinks.find(MaterialLinkKey{sceneId, entity, submeshIndex}) != materialFileLinks.end();
}

std::string editor::Project::getMaterialFilePath(uint32_t sceneId, Entity entity, unsigned int submeshIndex) const {
    auto it = materialFileLinks.find(MaterialLinkKey{sceneId, entity, submeshIndex});
    if (it != materialFileLinks.end()) {
        return it->second.filePath;
    }
    return {};
}

void editor::Project::unlinkMaterialFile(uint32_t sceneId, Entity entity, unsigned int submeshIndex) {
    materialFileLinks.erase(MaterialLinkKey{sceneId, entity, submeshIndex});
}

void editor::Project::unlinkAllMaterialFiles(uint32_t sceneId, Entity entity) {
    for (auto it = materialFileLinks.begin(); it != materialFileLinks.end();) {
        if (std::get<0>(it->first) == sceneId && std::get<1>(it->first) == entity) {
            it = materialFileLinks.erase(it);
        } else {
            ++it;
        }
    }
}

void editor::Project::remapMaterialFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path oldRelative = normalizeToProjectRelative(oldPath);
    fs::path newRelative = normalizeToProjectRelative(newPath);

    if (oldRelative.empty() || newRelative.empty()) {
        return;
    }

    std::vector<std::pair<MaterialLinkKey, MaterialLinkEntry>> remappedEntries;

    for (auto it = materialFileLinks.begin(); it != materialFileLinks.end();) {
        std::string updatedFilePath;
        if (!remapRelativeString(oldRelative, newRelative, it->second.filePath, updatedFilePath)) {
            ++it;
            continue;
        }

        MaterialLinkEntry updatedEntry = it->second;
        const std::string oldFilePath = updatedEntry.filePath;
        updatedEntry.filePath = updatedFilePath;

        std::error_code ec;
        updatedEntry.lastWriteTime = fs::last_write_time(projectPath / updatedEntry.filePath, ec);

        const MaterialLinkKey key = it->first;
        const uint32_t sceneId = std::get<0>(key);
        const Entity entity = std::get<1>(key);
        const unsigned int submeshIndex = std::get<2>(key);

        SceneProject* sceneProject = getScene(sceneId);
        if (sceneProject && sceneProject->scene) {
            MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
            if (mesh && submeshIndex < mesh->numSubmeshes) {
                Material& material = mesh->submeshes[submeshIndex].material;
                std::string updatedMaterialName;
                if (remapRelativeString(oldRelative, newRelative, material.name, updatedMaterialName)) {
                    material.name = updatedMaterialName;
                    mesh->submeshes[submeshIndex].needUpdateTexture = true;
                    sceneProject->needUpdateRender = true;
                    sceneProject->isModified = true;
                }
            }
        }

        remappedEntries.emplace_back(key, updatedEntry);
        it = materialFileLinks.erase(it);
    }

    for (auto& [key, entry] : remappedEntries) {
        materialFileLinks[key] = entry;
    }
}

void editor::Project::remapSceneFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path oldRelative = normalizeToProjectRelative(oldPath);
    fs::path newRelative = normalizeToProjectRelative(newPath);

    if (oldRelative.empty() || newRelative.empty()) {
        return;
    }

    bool changed = false;

    for (auto& sceneProject : scenes) {
        if (sceneProject.filepath.empty()) {
            continue;
        }

        fs::path updatedPath;
        if (remapRelativePath(oldRelative, newRelative, sceneProject.filepath, updatedPath)) {
            std::string oldStr = sceneProject.filepath.string();
            sceneProject.filepath = updatedPath;
            // Update corresponding tab entry
            for (auto& tab : tabs) {
                if (tab.type == TabType::SCENE && tab.filepath == oldStr) {
                    tab.filepath = updatedPath.string();
                }
            }
            changed = true;
        }
    }

    if (changed) {
        saveProject();
    }
}

void editor::Project::remapEntityBundleFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path oldRelative = normalizeToProjectRelative(oldPath);
    fs::path newRelative = normalizeToProjectRelative(newPath);

    if (oldRelative.empty() || newRelative.empty()) {
        return;
    }

    std::vector<std::pair<fs::path, EntityBundle>> remappedBundles;
    std::unordered_set<uint32_t> affectedSceneIds;

    for (auto it = entityBundles.begin(); it != entityBundles.end();) {
        fs::path updatedPath;
        if (!remapRelativePath(oldRelative, newRelative, it->first, updatedPath)) {
            ++it;
            continue;
        }

        EntityBundle updatedBundle = std::move(it->second);
        for (const auto& [sceneId, instances] : updatedBundle.instances) {
            if (!instances.empty()) {
                affectedSceneIds.insert(sceneId);
            }
        }

        remappedBundles.emplace_back(updatedPath, std::move(updatedBundle));
        it = entityBundles.erase(it);
    }

    for (auto& [filepath, bundle] : remappedBundles) {
        entityBundles.emplace(std::move(filepath), std::move(bundle));
    }

    bool changed = !remappedBundles.empty();

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }

        bool sceneBundleChanged = false;
        auto bundleArray = sceneProject.scene->getComponentArray<BundleComponent>();
        for (size_t i = 0; i < bundleArray->size(); ++i) {
            BundleComponent& bundleComp = bundleArray->getComponentFromIndex(i);
            std::string updatedPath;
            if (remapRelativeString(oldRelative, newRelative, bundleComp.path, updatedPath)) {
                bundleComp.path = updatedPath;
                sceneProject.isModified = true;
                changed = true;
                sceneBundleChanged = true;
            }
        }

        if (sceneBundleChanged) {
            updateSceneBundles(&sceneProject);
        }
    }

    for (uint32_t sceneId : affectedSceneIds) {
        if (SceneProject* sceneProject = getScene(sceneId)) {
            sceneProject->isModified = true;
            if (sceneProject->scene) {
                updateSceneBundles(sceneProject);
            }
        }
    }
}

void editor::Project::remapScriptFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path oldRelative = normalizeToProjectRelative(oldPath);
    fs::path newRelative = normalizeToProjectRelative(newPath);

    if (oldRelative.empty() || newRelative.empty()) {
        return;
    }

    std::unordered_set<uint32_t> affectedSceneIds;

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }

        if (remapScriptPathsInRegistry(sceneProject.scene, oldRelative, newRelative)) {
            sceneProject.isModified = true;
            updateSceneCppScripts(&sceneProject);
        }
    }

    for (uint32_t sceneId : affectedSceneIds) {
        if (SceneProject* sceneProject = getScene(sceneId)) {
            sceneProject->isModified = true;
            if (sceneProject->scene) {
                updateSceneCppScripts(sceneProject);
            }
        }
    }
}

void editor::Project::cleanupMaterialFilePath(const std::filesystem::path& deletedPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path deletedRelative = normalizeToProjectRelative(deletedPath);
    if (deletedRelative.empty()) {
        return;
    }

    for (auto it = materialFileLinks.begin(); it != materialFileLinks.end();) {
        if (matchesRelativeString(deletedRelative, it->second.filePath)) {
            it = materialFileLinks.erase(it);
        } else {
            ++it;
        }
    }

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }

        auto meshes = sceneProject.scene->getComponentArray<MeshComponent>();
        bool sceneChanged = false;

        for (size_t i = 0; i < meshes->size(); ++i) {
            Entity entity = meshes->getEntity(i);
            MeshComponent& mesh = meshes->getComponentFromIndex(i);

            for (unsigned int submeshIndex = 0; submeshIndex < mesh.numSubmeshes; ++submeshIndex) {
                Material& material = mesh.submeshes[submeshIndex].material;
                if (!matchesRelativeString(deletedRelative, material.name)) {
                    continue;
                }

                material.name.clear();
                mesh.submeshes[submeshIndex].needUpdateTexture = true;
                sceneProject.needUpdateRender = true;
                sceneProject.isModified = true;
                sceneChanged = true;

                unlinkMaterialFile(sceneProject.id, entity, submeshIndex);
            }
        }

        if (sceneChanged) {
            sceneProject.needUpdateRender = true;
        }
    }
}

void editor::Project::cleanupSceneFilePath(const std::filesystem::path& deletedPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path deletedRelative = normalizeToProjectRelative(deletedPath);
    if (deletedRelative.empty()) {
        return;
    }

    bool changed = false;
    std::vector<uint32_t> scenesToRemove;

    for (auto& sceneProject : scenes) {
        if (sceneProject.filepath.empty()) {
            continue;
        }

        if (matchesRelativePath(deletedRelative, sceneProject.filepath)) {
            if (sceneProject.opened) {
                sceneProject.filepath.clear();
                sceneProject.isModified = true;
            } else {
                scenesToRemove.push_back(sceneProject.id);
            }
            changed = true;
        }
    }

    for (uint32_t sceneId : scenesToRemove) {
        auto it = std::find_if(scenes.begin(), scenes.end(),
            [sceneId](const SceneProject& s) { return s.id == sceneId; });
        if (it != scenes.end()) {
            deleteSceneProject(&(*it));
            cleanupEntityBundlesForScene(sceneId);
            removeTab(TabType::SCENE, it->filepath.string());
            markParentScenesNeedUpdate(sceneId);
            Backend::getApp().clearSceneWindowState(sceneId);
            scenes.erase(it);
        }
    }

    if (changed) {
        saveProject();
    }
}

void editor::Project::cleanupEntityBundleFilePath(const std::filesystem::path& deletedPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path deletedRelative = normalizeToProjectRelative(deletedPath);
    if (deletedRelative.empty()) {
        return;
    }

    std::vector<fs::path> bundlesToRemove;

    for (const auto& [filepath, bundle] : entityBundles) {
        if (matchesRelativePath(deletedRelative, filepath)) {
            bundlesToRemove.push_back(filepath);
        }
    }

    bool changed = !bundlesToRemove.empty();
    std::unordered_set<uint32_t> affectedSceneIds;

    for (const auto& bundlePath : bundlesToRemove) {
        EntityBundle* bundle = getEntityBundle(bundlePath);
        if (!bundle) {
            continue;
        }

        std::vector<std::pair<uint32_t, std::pair<Entity, std::vector<Entity>>>> instancesToRemove;
        for (const auto& [sceneId, instances] : bundle->instances) {
            affectedSceneIds.insert(sceneId);
            for (const auto& instance : instances) {
                std::vector<Entity> memberEntities;
                memberEntities.reserve(instance.members.size());

                for (const auto& member : instance.members) {
                    if (member.localEntity != NULL_ENTITY) {
                        memberEntities.push_back(member.localEntity);
                    }
                }

                instancesToRemove.emplace_back(sceneId, std::make_pair(instance.rootEntity, std::move(memberEntities)));
            }
        }

        for (const auto& [sceneId, rootAndMembers] : instancesToRemove) {
            unimportEntityBundle(sceneId, bundlePath, rootAndMembers.first, rootAndMembers.second);
        }

        removeEntityBundle(bundlePath);
    }

    for (uint32_t sceneId : affectedSceneIds) {
        if (SceneProject* sceneProject = getScene(sceneId)) {
            sceneProject->isModified = true;
            if (sceneProject->scene) {
                updateSceneBundles(sceneProject);
            }
        }
    }
}

void editor::Project::cleanupScriptFilePath(const std::filesystem::path& deletedPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path deletedRelative = normalizeToProjectRelative(deletedPath);
    if (deletedRelative.empty()) {
        return;
    }

    std::unordered_set<uint32_t> affectedSceneIds;

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }

        if (cleanupScriptPathsInRegistry(sceneProject.scene, deletedRelative)) {
            sceneProject.isModified = true;
            updateSceneCppScripts(&sceneProject);
        }
    }

    for (uint32_t sceneId : affectedSceneIds) {
        if (SceneProject* sceneProject = getScene(sceneId)) {
            sceneProject->isModified = true;
            if (sceneProject->scene) {
                updateSceneCppScripts(sceneProject);
            }
        }
    }
}

void editor::Project::refreshLinkedMaterials(bool force) {
    if (materialFileLinks.empty()) {
        return;
    }

    // Throttle: only check every materialRefreshIntervalSec seconds
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastMaterialRefreshTime).count();
    if (!force && elapsed < materialRefreshIntervalSec) {
        return;
    }
    lastMaterialRefreshTime = now;

    if (projectPath.empty() || !fs::exists(projectPath)) {
        materialFileLinks.clear();
        return;
    }

    // Collect changed files and stale entries
    struct ChangedFile {
        std::string filePath;
        Material updatedMaterial;
    };
    std::unordered_map<std::string, ChangedFile> changedFiles;
    std::vector<MaterialLinkKey> staleKeys;

    for (auto& [key, linkEntry] : materialFileLinks) {
        uint32_t keySceneId = std::get<0>(key);
        Entity keyEntity = std::get<1>(key);
        unsigned int keySubmeshIndex = std::get<2>(key);

        // Find the scene
        SceneProject* sceneProject = nullptr;
        for (auto& sp : scenes) {
            if (sp.id == keySceneId) {
                sceneProject = &sp;
                break;
            }
        }
        if (!sceneProject || !sceneProject->scene) {
            staleKeys.push_back(key);
            continue;
        }

        MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(keyEntity);
        if (!mesh || keySubmeshIndex >= mesh->numSubmeshes) {
            staleKeys.push_back(key);
            continue;
        }

        // If the material name no longer matches the linked file (e.g. after undo), remove the link
        std::string currentName = fs::path(mesh->submeshes[keySubmeshIndex].material.name).lexically_normal().generic_string();
        if (currentName != linkEntry.filePath) {
            staleKeys.push_back(key);
            continue;
        }

        fs::path absolutePath = projectPath / linkEntry.filePath;
        if (!fs::exists(absolutePath)) {
            staleKeys.push_back(key);
            continue;
        }

        std::error_code ec;
        auto writeTime = fs::last_write_time(absolutePath, ec);
        if (ec) {
            continue;
        }

        if (writeTime != linkEntry.lastWriteTime) {
            linkEntry.lastWriteTime = writeTime;

            // Only load the file once per unique path
            if (changedFiles.find(linkEntry.filePath) == changedFiles.end()) {
                try {
                    YAML::Node materialNode = YAML::LoadFile(absolutePath.string());
                    Material updatedMaterial = Stream::decodeMaterial(materialNode);
                    updatedMaterial.name = linkEntry.filePath;
                    changedFiles[linkEntry.filePath] = {linkEntry.filePath, updatedMaterial};
                } catch (const std::exception& e) {
                    Out::error("Error reloading linked material file '%s': %s", absolutePath.string().c_str(), e.what());
                }
            }
        }
    }

    // Remove stale entries
    for (const auto& key : staleKeys) {
        materialFileLinks.erase(key);
    }

    // Apply changes
    for (auto& [key, linkEntry] : materialFileLinks) {
        uint32_t keySceneId = std::get<0>(key);
        Entity keyEntity = std::get<1>(key);
        unsigned int keySubmeshIndex = std::get<2>(key);

        auto changedIt = changedFiles.find(linkEntry.filePath);
        if (changedIt == changedFiles.end()) {
            continue;
        }

        SceneProject* sceneProject = nullptr;
        for (auto& sp : scenes) {
            if (sp.id == keySceneId) {
                sceneProject = &sp;
                break;
            }
        }
        if (!sceneProject || !sceneProject->scene) {
            continue;
        }

        MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(keyEntity);
        if (!mesh || keySubmeshIndex >= mesh->numSubmeshes) {
            continue;
        }

        const Material& updatedMaterial = changedIt->second.updatedMaterial;
        if (mesh->submeshes[keySubmeshIndex].material != updatedMaterial) {
            mesh->submeshes[keySubmeshIndex].material = updatedMaterial;
            mesh->submeshes[keySubmeshIndex].needUpdateTexture = true;
            sceneProject->needUpdateRender = true;
            sceneProject->isModified = true;
        }
    }
}

editor::SceneRender* editor::Project::createSceneRender(SceneType type, Scene* scene) const {
    if (!scene) {
        return nullptr;
    }

    pauseEngineScene(scene, true);
    scene->getSystem<UISystem>()->setAnchorReferenceSize(canvasWidth, canvasHeight);

    switch (type) {
        case SceneType::SCENE_3D:
            return new SceneRender3D(scene);
        case SceneType::SCENE_2D:
            return new SceneRender2D(scene, canvasWidth, canvasHeight, false);
        case SceneType::SCENE_UI:
            return new SceneRender2D(scene, canvasWidth, canvasHeight, true);
        default:
            return new SceneRender3D(scene);
    }
}

Entity editor::Project::createDefaultCamera(SceneType type, Scene* scene) const {
    if (!scene) {
        return NULL_ENTITY;
    }
    if (type == SceneType::SCENE_3D){
        return NULL_ENTITY; // 3D scenes use Camera entity created in SceneRender3D
    }

    Entity defaultCamera = scene->createSystemEntity();
    scene->addComponent<CameraComponent>(defaultCamera, {});
    scene->addComponent<Transform>(defaultCamera, {});

    CameraComponent& camera = scene->getComponent<CameraComponent>(defaultCamera);
    camera.transparentSort = false;

    switch (type) {
        case SceneType::SCENE_UI:
            camera.type = CameraType::CAMERA_UI;
            break;
        case SceneType::SCENE_2D:
            camera.type = CameraType::CAMERA_ORTHO;
            break;
    }

    Transform& cameratransform = scene->getComponent<Transform>(defaultCamera);
    cameratransform.position = Vector3(0.0, 0.0, 1.0);

    return defaultCamera;
}

void editor::Project::checkUnsavedAndExecute(uint32_t sceneId, std::function<void()> action) {
    SceneProject* sceneProject = getScene(sceneId);

    if (sceneProject && hasSceneUnsavedChanges(sceneId)) {
        Backend::getApp().registerThreeButtonAlert(
            "Unsaved Changes",
            "There are unsaved changes. Do you want to save first?",
            [this, sceneId, action]() {
                // Yes callback - save and then execute action
                SceneProject* sceneProject = getScene(sceneId);
                if (sceneProject && !sceneProject->filepath.empty()) {
                    // Scene has filepath, save synchronously and execute action
                    saveScene(sceneId);
                    if (action) action();
                } else {
                    // Scene needs save dialog, pass action as callback
                    Backend::getApp().registerSaveSceneDialog(sceneId, action);
                }
            },
            [action]() {
                // No callback - execute action without saving
                if (action) action();
            },
            nullptr // Cancel: do nothing, keep scene open
        );
    } else {
        // No unsaved changes, execute action directly
        if (action) action();
    }
}

std::string editor::Project::getName() const {
    return name; 
}

void editor::Project::setName(std::string name){
    this->name = name;
    Backend::updateWindowTitle(name);
}

void editor::Project::setCanvasSize(unsigned int width, unsigned int height){
    this->canvasWidth = width;
    this->canvasHeight = height;

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }

        sceneProject.scene->getSystem<UISystem>()->setAnchorReferenceSize(width, height);

        if (sceneProject.sceneRender && sceneProject.sceneType != SceneType::SCENE_3D) {
            static_cast<SceneRender2D*>(sceneProject.sceneRender)->setCanvasFrameSize(width, height);
        }

        sceneProject.needUpdateRender = true;
    }
}

unsigned int editor::Project::getCanvasWidth() const{
    return canvasWidth;
}

unsigned int editor::Project::getCanvasHeight() const{
    return canvasHeight;
}

void editor::Project::setScalingMode(Scaling scalingMode){
    this->scalingMode = scalingMode;
}

Scaling editor::Project::getScalingMode() const{
    return scalingMode;
}

void editor::Project::setTextureStrategy(TextureStrategy textureStrategy){
    this->textureStrategy = textureStrategy;
}

TextureStrategy editor::Project::getTextureStrategy() const{
    return textureStrategy;
}

void editor::Project::setAssetsDir(const std::filesystem::path& assetsDir){
    this->assetsDir = assetsDir;
}

std::filesystem::path editor::Project::getAssetsDir() const{
    return assetsDir;
}

void editor::Project::setLuaDir(const std::filesystem::path& luaDir){
    this->luaDir = luaDir;
}

std::filesystem::path editor::Project::getLuaDir() const{
    return luaDir;
}

void editor::Project::setCMakeKit(const std::string& cCompiler, const std::string& cxxCompiler, const std::string& generator){
    this->cmakeCCompiler = cCompiler;
    this->cmakeCxxCompiler = cxxCompiler;
    this->cmakeGenerator = generator;
}

std::string editor::Project::getCMakeCCompiler() const{
    return cmakeCCompiler;
}

std::string editor::Project::getCMakeCxxCompiler() const{
    return cmakeCxxCompiler;
}

std::string editor::Project::getCMakeGenerator() const{
    return cmakeGenerator;
}

uint32_t editor::Project::getStartSceneId() const{
    return startSceneId;
}

void editor::Project::setStartSceneId(uint32_t sceneId){
    startSceneId = sceneId;
}

editor::TerrainEditorSettings& editor::Project::getTerrainEditorSettings(){
    return terrainEditorSettings;
}

const editor::TerrainEditorSettings& editor::Project::getTerrainEditorSettings() const{
    return terrainEditorSettings;
}

editor::CommandHistory* editor::Project::getProjectCommandHistory(){
    return &projectHistory;
}

uint32_t editor::Project::createNewScene(std::string sceneName, SceneType type){
    if (isAnyScenePlaying()){
        Out::warning("Cannot create a new scene while a scene is playing.");
        return NULL_PROJECT_SCENE;
    }

    uint32_t previousSceneId = getSelectedSceneId();

    checkUnsavedAndExecute(previousSceneId, [this, sceneName, type, previousSceneId]() {
        createNewSceneInternal(sceneName, type, previousSceneId);
    });

    return NULL_PROJECT_SCENE; // Scene may be created asynchronously
}

uint32_t editor::Project::createNewSceneInternal(std::string sceneName, SceneType type, uint32_t previousSceneId){
    uint32_t reusedSceneId = NULL_PROJECT_SCENE;
    if (previousSceneId != NULL_PROJECT_SCENE) {
        SceneProject* previousScene = getScene(previousSceneId);
        if (previousScene && previousScene->filepath.empty()) {
            reusedSceneId = previousSceneId;
            closeScene(previousSceneId, true);
        }
    }

    unsigned int nameCount = 2;
    std::string baseName = sceneName;
    bool foundName = true;
    while (foundName){
        foundName = false;
        for (auto& sceneProject : scenes) {
            std::string usedName = sceneProject.name;
            if (usedName == sceneName){
                sceneName = baseName + " " + std::to_string(nameCount);
                nameCount++;
                foundName = true;
            }
        }
    }

    SceneProject data;
    data.id = reusedSceneId != NULL_PROJECT_SCENE ? reusedSceneId : ++nextSceneId;
    data.name = sceneName;
    data.scene = new Scene();
    data.sceneType = type;
    data.sceneRender = createSceneRender(data.sceneType, data.scene);
    data.defaultCamera = createDefaultCamera(data.sceneType, data.scene);
    data.isVisible = true;

    scenes.push_back(data);

    setSelectedSceneId(data.id);

    if (data.sceneType == SceneType::SCENE_3D){
        CreateEntityCmd sunCreator(this, data.id, "Sun", EntityCreationType::DIRECTIONAL_LIGHT);
        sunCreator.addProperty<Vector3>(ComponentType::Transform, "position", Vector3(0.0f, 10.0f, 0.0f));
        sunCreator.addProperty<float>(ComponentType::LightComponent, "intensity", 4.0f);
        sunCreator.addProperty<Vector3>(ComponentType::LightComponent, "direction", Vector3(-0.2f, -0.5f, 0.3f));
        sunCreator.addProperty<bool>(ComponentType::LightComponent, "shadows", true);
        sunCreator.addProperty<float>(ComponentType::LightComponent, "range", 100);
        sunCreator.execute();

        CreateEntityCmd skyCreator(this, data.id, "Sky", EntityCreationType::SKY);
        Texture defaultSky;
        ProjectUtils::setDefaultSkyTexture(defaultSky);
        skyCreator.addProperty<Texture>(ComponentType::SkyComponent, "texture", defaultSky);
        skyCreator.execute();

        clearSelectedEntities(data.id);
        getScene(data.id)->isModified = false; // New scene starts as unmodified
    }

    Backend::getApp().addNewSceneToDock(data.id);

    // Close the previous scene after the new one is created
    if (previousSceneId != NULL_PROJECT_SCENE && previousSceneId != reusedSceneId) {
        closeScene(previousSceneId, true);
    }

    return data.id;
}

editor::SceneProject* editor::Project::createRuntimeCloneFromSource(const SceneProject* source) {
    if (!source) {
        return nullptr;
    }

    SceneProject* runtime = new SceneProject();
    runtime->opened = false;
    runtime->isModified = false;
    runtime->isVisible = false;
    runtime->needUpdateRender = false;
    runtime->filepath = source->filepath;

    fs::path fullPath = runtime->filepath;
    if (fullPath.is_relative()) {
        fullPath = getProjectPath() / fullPath;
    }

    YAML::Node sceneNode = YAML::LoadFile(fullPath.string());
    Stream::decodeSceneProject(runtime, sceneNode, true);
    //runtime->sceneRender = createSceneRender(runtime->sceneType, runtime->scene);
    runtime->defaultCamera = createDefaultCamera(runtime->sceneType, runtime->scene);
    Stream::decodeSceneProjectEntities(this, runtime, sceneNode);
    pauseEngineScene(runtime->scene, true);

    return runtime;
}

void editor::Project::updateSceneCppScripts(SceneProject* sceneProject) {
    if (!sceneProject || !sceneProject->scene) {
        return;
    }

    sceneProject->cppScripts.clear();

    std::unordered_set<std::string> uniqueScripts;
    auto scriptsArray = sceneProject->scene->getComponentArray<ScriptComponent>();

    for (int i = 0; i < scriptsArray->size(); i++) {
        const ScriptComponent& scriptComponent = scriptsArray->getComponentFromIndex(i);

        for (const auto& scriptEntry : scriptComponent.scripts) {
            if (!scriptEntry.enabled)
                continue;
            if (scriptEntry.type == ScriptType::SCRIPT_LUA)
                continue;
            if (scriptEntry.path.empty())
                continue;

            fs::path fullPath = scriptEntry.path;
            if (fullPath.is_relative()) {
                fullPath = getProjectPath() / fullPath;
            }

            if (!std::filesystem::exists(fullPath)) {
                Out::error("Script file not found: %s", fullPath.string().c_str());
                continue;
            }

            std::string key = fullPath.lexically_normal().generic_string();
            if (!uniqueScripts.insert(key).second) {
                continue;
            }

            std::vector<ScriptPropertyInfo> properties;
            properties.reserve(scriptEntry.properties.size());
            for (const auto& prop : scriptEntry.properties) {
                ScriptPropertyInfo propInfo;
                propInfo.name = prop.name;
                propInfo.isPtr = (prop.type == ScriptPropertyType::EntityReference) || !prop.ptrTypeName.empty();
                propInfo.ptrTypeName = prop.ptrTypeName;
                properties.push_back(std::move(propInfo));
            }

            SceneScriptSource sceneScript;
            sceneScript.path = scriptEntry.path;
            sceneScript.headerPath = scriptEntry.headerPath;
            sceneScript.className = scriptEntry.className;
            sceneScript.properties = std::move(properties);
            sceneProject->cppScripts.push_back(std::move(sceneScript));
        }
    }
}

void editor::Project::updateSceneBundles(SceneProject* sceneProject) {
    if (!sceneProject) {
        return;
    }

    sceneProject->bundles.clear();

    for (const auto& [bundlePath, bundle] : entityBundles) {
        if (bundle.instances.find(sceneProject->id) != bundle.instances.end()) {
            BundleSceneInfo info;
            info.bundlePath = bundlePath;
            info.functionName = Factory::bundleToFunctionName(bundlePath);
            sceneProject->bundles.push_back(std::move(info));
        }
    }
}

void editor::Project::calculateSceneMaxValues(const SceneProject* sceneProject, SceneMaxValues& maxValues) const {
    if (!sceneProject || !sceneProject->scene) {
        return;
    }

    auto meshes = sceneProject->scene->getComponentArray<MeshComponent>();
    for (size_t i = 0; i < meshes->size(); ++i) {
        const MeshComponent& mesh = meshes->getComponentFromIndex(i);
        maxValues.maxSubmeshes = std::max(maxValues.maxSubmeshes, mesh.numSubmeshes);
        maxValues.maxExternalBuffers = std::max(maxValues.maxExternalBuffers, mesh.numExternalBuffers);
    }

    auto sprites = sceneProject->scene->getComponentArray<SpriteComponent>();
    for (size_t i = 0; i < sprites->size(); ++i) {
        const SpriteComponent& sprite = sprites->getComponentFromIndex(i);
        maxValues.maxSpriteFrames = std::max(maxValues.maxSpriteFrames, sprite.numFramesRect);
    }

    auto points = sceneProject->scene->getComponentArray<PointsComponent>();
    for (size_t i = 0; i < points->size(); ++i) {
        const PointsComponent& pointsComponent = points->getComponentFromIndex(i);
        maxValues.maxSpriteFrames = std::max(maxValues.maxSpriteFrames, pointsComponent.numFramesRect);
    }

    auto tilemaps = sceneProject->scene->getComponentArray<TilemapComponent>();
    for (size_t i = 0; i < tilemaps->size(); ++i) {
        const TilemapComponent& tilemap = tilemaps->getComponentFromIndex(i);
        maxValues.maxTilemapTilesRect = std::max(maxValues.maxTilemapTilesRect, tilemap.numTilesRect);
        maxValues.maxTilemapTiles = std::max(maxValues.maxTilemapTiles, tilemap.numTiles);
    }
}

void editor::Project::collectSceneShaderKeys(const SceneProject* sceneProject, std::set<ShaderKey>& keys) const {
    if (!sceneProject || !sceneProject->scene) {
        return;
    }

    Scene* scene = sceneProject->scene;

    for (Entity entity : sceneProject->entities) {
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<MeshComponent>())) {
            const MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
            if (mesh.loaded) {
                for (unsigned int s = 0; s < mesh.numSubmeshes; ++s) {
                    keys.insert(ShaderPool::getShaderKey(ShaderType::MESH, mesh.submeshes[s].shaderProperties));
                    keys.insert(ShaderPool::getShaderKey(ShaderType::DEPTH, mesh.submeshes[s].depthShaderProperties));
                }
            }
        }

        if (signature.test(scene->getComponentId<UIComponent>())) {
            const UIComponent& ui = scene->getComponent<UIComponent>(entity);
            if (ui.loaded) {
                keys.insert(ShaderPool::getShaderKey(ShaderType::UI, ui.shaderProperties));
            }
        }

        if (signature.test(scene->getComponentId<PointsComponent>())) {
            const PointsComponent& pts = scene->getComponent<PointsComponent>(entity);
            if (pts.loaded) {
                keys.insert(ShaderPool::getShaderKey(ShaderType::POINTS, pts.shaderProperties));
            }
        }

        if (signature.test(scene->getComponentId<LinesComponent>())) {
            const LinesComponent& ln = scene->getComponent<LinesComponent>(entity);
            if (ln.loaded) {
                keys.insert(ShaderPool::getShaderKey(ShaderType::LINES, ln.shaderProperties));
            }
        }

        if (signature.test(scene->getComponentId<SkyComponent>())) {
            const SkyComponent& sky = scene->getComponent<SkyComponent>(entity);
            if (sky.loaded) {
                keys.insert(ShaderPool::getShaderKey(ShaderType::SKYBOX, 0));
            }
        }
    }

}

Entity editor::Project::getSceneCamera(const SceneProject* sceneProject) const {
    if (sceneProject->mainCamera != NULL_ENTITY && sceneProject->scene->isEntityCreated(sceneProject->mainCamera)) {
        return sceneProject->mainCamera;
    } else if (sceneProject->defaultCamera != NULL_ENTITY) {
        return sceneProject->defaultCamera;
    } else {
        return sceneProject->scene->getCamera();
    }
}

void editor::Project::prepareRuntimeScene(PlayRuntimeScene& entry) {
    if (!entry.runtime || !entry.runtime->scene) return;

    Entity camera = getSceneCamera(entry.runtime);
    entry.runtime->scene->setCamera(camera);

    entry.runtime->scene->getSystem<ActionSystem>()->resetRunningActions();

    entry.runtime->scene->getSystem<UISystem>()->clearAnchorReferenceSize();
    pauseEngineScene(entry.runtime->scene, false);

    entry.initialized = true;

    if (entry.runtime->sceneRender){
        entry.runtime->sceneRender->setPlayMode(true);
    }
}

void editor::Project::cleanupPlaySession(const std::shared_ptr<PlaySession>& session) {
    if (!session) {
        return;
    }

    for (const auto& entry : session->runtimeScenes) {
        SceneProject* runtime = entry.runtime;
        if (!runtime) {
            continue;
        }

        SceneManager::removeScenePtr(entry.sourceSceneId);

        if (entry.ownedRuntime) {
            deleteSceneProject(runtime);
            delete runtime;
        }
    }
}

void editor::Project::loadScene(fs::path filepath, bool opened, bool isNewScene, bool loadSceneData){
    try {
        fs::path fullPath = filepath;
        if (fullPath.is_relative()) {
            fullPath = getProjectPath() / fullPath;
        }

        YAML::Node sceneNode = YAML::LoadFile(fullPath.string());
        SceneProject* targetScene = nullptr;

        if (isNewScene) {
            scenes.emplace_back();
            targetScene = &scenes.back();
            std::error_code ec;
            fs::path relPath = fs::relative(fullPath, getProjectPath(), ec);
            if (ec || relPath.empty()) {
                Out::error("Scene filepath must be relative to project path: %s", fullPath.string().c_str());
                return;
            }
            targetScene->filepath = relPath;
        } else {
            auto it = std::find_if(scenes.begin(), scenes.end(),
                [this, &fullPath](const SceneProject& scene) { 
                    fs::path scenePath = scene.filepath;
                    if (scenePath.is_relative()) {
                        scenePath = getProjectPath() / scenePath;
                    }
                    return scenePath == fullPath; 
                });
            targetScene = &(*it);

            if (targetScene->scene != nullptr || targetScene->sceneRender != nullptr) {
                Out::error("Scene is already loaded");
                return;
            }
        }

        Stream::decodeSceneProject(targetScene, sceneNode, loadSceneData);

        if (loadSceneData){
            targetScene->sceneRender = createSceneRender(targetScene->sceneType, targetScene->scene);

            if (targetScene->editorCameraState.IsDefined()) {
                Camera* editorCam = targetScene->sceneRender->getCamera();
                if (editorCam) {
                    float zoom = 0.0f;
                    Stream::decodeEditorCamera(editorCam, targetScene->editorCameraState, zoom);
                    if ((targetScene->sceneType == SceneType::SCENE_2D || targetScene->sceneType == SceneType::SCENE_UI) && zoom > 0.0f) {
                        static_cast<SceneRender2D*>(targetScene->sceneRender)->setZoom(zoom);
                    }
                }
            }

            loadSceneProjectData(targetScene, sceneNode);
        }

        if (opened){
            // Sync linked materials with latest file contents on disk
            for (Entity entity : targetScene->entities) {
                MeshComponent* mesh = targetScene->scene->findComponent<MeshComponent>(entity);
                if (!mesh) continue;

                for (unsigned int submeshIndex = 0; submeshIndex < mesh->numSubmeshes; submeshIndex++) {
                    Material& material = mesh->submeshes[submeshIndex].material;
                    if (material.name.empty()) continue;

                    std::string normalizedPath = fs::path(material.name).lexically_normal().generic_string();

                    fs::path materialPath = projectPath / normalizedPath;
                    if (!fs::exists(materialPath) || fs::is_directory(materialPath)) continue;

                    try {
                        YAML::Node materialNode = YAML::LoadFile(materialPath.string());
                        Material fileMaterial = Stream::decodeMaterial(materialNode);
                        fileMaterial.name = normalizedPath;

                        std::error_code ec;
                        materialFileLinks[MaterialLinkKey{targetScene->id, entity, submeshIndex}].lastWriteTime = fs::last_write_time(materialPath, ec);

                        if (material != fileMaterial) {
                            material = fileMaterial;
                            mesh->submeshes[submeshIndex].needUpdateTexture = true;
                            targetScene->needUpdateRender = true;
                        }
                    } catch (const std::exception& e) {
                        Out::error("Error loading linked material file '%s': %s", materialPath.string().c_str(), e.what());
                    }
                }
            }

            setSelectedSceneId(targetScene->id);

            Backend::getApp().addNewSceneToDock(targetScene->id);
        }

        targetScene->needUpdateRender = opened;
        targetScene->isModified = false;
        targetScene->opened = opened;

        if (opened) {
            addTab(TabType::SCENE, targetScene->filepath.string());
        }

        // Check for ID collisions
        SceneProject* existing = getScene(targetScene->id);
        if (targetScene->id == NULL_PROJECT_SCENE || (existing && existing != targetScene)) {
            uint32_t oldId = targetScene->id;
            targetScene->id = ++nextSceneId;
            if (oldId != NULL_PROJECT_SCENE) {
                Out::warning("Scene with ID '%u' already exists, using ID %u", oldId, targetScene->id);
            } else {
                Out::warning("Scene has no ID, assigning ID %u", targetScene->id);
            }
        }

    } catch (const YAML::Exception& e) {
        if (isNewScene && !scenes.empty()) scenes.pop_back();
        Out::error("Failed to open scene: %s", e.what());
        Backend::getApp().registerAlert("Error", "Failed to open scene file!");
    } catch (const std::exception& e) {
        if (isNewScene && !scenes.empty()) scenes.pop_back();
        Out::error("Failed to open scene: %s", e.what());
        Backend::getApp().registerAlert("Error", "Failed to open scene file!");
    }
}

void editor::Project::openScene(fs::path filepath, bool closePrevious){
    if (isAnyScenePlaying()){
        Out::warning("Cannot open a new scene while a scene is playing.");
        return;
    }

    uint32_t sceneToClose = NULL_PROJECT_SCENE;
    if (closePrevious) {
        SceneProject* selectedScene = getSelectedScene();
        if (selectedScene) {
            sceneToClose = selectedScene->id;
        }
    }

    checkUnsavedAndExecute(sceneToClose, [this, filepath, sceneToClose]() {
        openSceneInternal(filepath, sceneToClose);
    });
}

void editor::Project::openSceneInternal(fs::path filepath, uint32_t sceneToClose){
    if (filepath.is_relative()) {
        filepath = getProjectPath() / filepath;
    }

    auto it = std::find_if(scenes.begin(), scenes.end(),
        [this, &filepath](const SceneProject& scene) { 
            fs::path scenePath = scene.filepath;
            if (scenePath.is_relative()) {
                scenePath = getProjectPath() / scenePath;
            }
            return scenePath == filepath; 
        });

    if (it != scenes.end()) {
        if (it->opened) {
            setSelectedSceneId(it->id);
            if (sceneToClose != NULL_PROJECT_SCENE && sceneToClose != it->id) {
                closeScene(sceneToClose, true);
            }
            return;
        }
        // If expanded inline, unload inline data first so loadScene can reload fully
        if (it->expandedInline) {
            unloadChildSceneInline(it->id);
        }
        // Scene exists in project but is closed
        if (sceneToClose != NULL_PROJECT_SCENE && sceneToClose != it->id) {
            closeScene(sceneToClose, true);
        }
        loadScene(filepath, true, false, true);
        saveProjectFile();
        return;
    }

    // Scene is not in project
    Backend::getApp().registerConfirmAlert(
        "Add Scene",
        "This scene is not part of the current project. Do you want to add it?",
        [this, filepath, sceneToClose]() {
            if (sceneToClose != NULL_PROJECT_SCENE) {
                closeScene(sceneToClose, true);
            }
            loadScene(filepath, true, true, true);
            saveProjectFile();
        },
        []() {
            // Do nothing
        }
    );
}

void editor::Project::closeScene(uint32_t sceneId, bool systemClose) {
    auto it = std::find_if(scenes.begin(), scenes.end(),
        [sceneId](const SceneProject& scene) { return scene.id == sceneId; });

    if (it == scenes.end() || !it->opened) {
        return;
    }

    // Count opened scenes
    int openedCount = 0;
    for (const auto& scene : scenes) {
        if (scene.opened) {
            openedCount++;
        }
    }

    if (!systemClose){
        if (openedCount == 1) {
            Out::error("Cannot close last scene");
            return;
        }

        if (selectedScene == sceneId) {
            Out::error("Scene is selected, cannot close it");
            return;
        }
    }

    deleteSceneProject(&(*it));

    cleanupEntityBundlesForScene(sceneId);

    removeTab(TabType::SCENE, it->filepath.string());

    // Notify parent scenes so they rebuild their layers without the deleted scene
    markParentScenesNeedUpdate(sceneId);

    // If the scene was never saved, remove it entirely
    if (it->filepath.empty()) {
        Backend::getApp().clearSceneWindowState(sceneId);
        scenes.erase(it);
    } else {
        it->opened = false;
        it->expandedInline = false;
    }

    if (!systemClose){
        saveProjectFile();
    }
}

void editor::Project::removeScene(uint32_t sceneId) {
    auto it = std::find_if(scenes.begin(), scenes.end(),
        [sceneId](const SceneProject& scene) { return scene.id == sceneId; });

    if (it == scenes.end()) {
        return;
    }

    if (scenes.size() <= 1) {
        Out::error("Cannot remove last scene");
        return;
    }

    // If selected, select another scene
    if (selectedScene == sceneId) {
         // Try to select the first one that is not this one
        for (const auto& scene : scenes) {
            if (scene.id != sceneId) {
                setSelectedSceneId(scene.id);
                break;
            }
        }
    }

    // Cleanup resources
    deleteSceneProject(&(*it));

    // Remove C++ source file
    generator.clearSceneSource(it->name, getProjectInternalPath());

    // Cleanup EntityBundles
    cleanupEntityBundlesForScene(sceneId);

    removeTab(TabType::SCENE, it->filepath.string());
    Backend::getApp().clearSceneWindowState(sceneId);
    scenes.erase(it);
}

void editor::Project::markParentScenesNeedUpdate(uint32_t childSceneId) {
    for (auto& s : scenes) {
        auto& cs = s.childScenes;
        if (std::find(cs.begin(), cs.end(), childSceneId) != cs.end()) {
            s.needUpdateRender = true;
        }
    }
}

void editor::Project::loadSceneProjectData(SceneProject* sceneProject, const YAML::Node& sceneNode) {
    sceneProject->defaultCamera = createDefaultCamera(sceneProject->sceneType, sceneProject->scene);

    Stream::decodeSceneProjectEntities(this, sceneProject, sceneNode);

    for (Entity entity : sceneProject->entities) {
        MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
        if (!mesh) continue;
        for (unsigned int submeshIndex = 0; submeshIndex < mesh->numSubmeshes; submeshIndex++) {
            Material& material = mesh->submeshes[submeshIndex].material;
            if (material.name.empty()) continue;
            std::string normalizedPath = fs::path(material.name).lexically_normal().generic_string();
            linkMaterialFile(sceneProject->id, entity, submeshIndex, normalizedPath);
        }
    }

    updateSceneCppScripts(sceneProject);
    updateSceneBundles(sceneProject);
}

bool editor::Project::loadChildSceneInline(uint32_t childSceneId) {
    SceneProject* childScene = getScene(childSceneId);
    if (!childScene) {
        Out::error("Child scene with ID %u not found", childSceneId);
        return false;
    }

    if (childScene->scene != nullptr) {
        // Already loaded (opened as tab or already expanded inline)
        childScene->expandedInline = true;
        markParentScenesNeedUpdate(childSceneId);
        return true;
    }

    if (childScene->filepath.empty()) {
        Out::error("Child scene has no filepath");
        return false;
    }

    try {
        fs::path fullPath = childScene->filepath;
        if (fullPath.is_relative()) {
            fullPath = getProjectPath() / fullPath;
        }

        YAML::Node sceneNode = YAML::LoadFile(fullPath.string());

        Stream::decodeSceneProject(childScene, sceneNode, true);
        loadSceneProjectData(childScene, sceneNode);

        childScene->expandedInline = true;
        childScene->needUpdateRender = true;
        childScene->isModified = false;

        markParentScenesNeedUpdate(childSceneId);

        Out::info("Loaded child scene '%s' inline", childScene->name.c_str());
        return true;

    } catch (const YAML::Exception& e) {
        Out::error("Failed to load child scene inline: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        Out::error("Failed to load child scene inline: %s", e.what());
        return false;
    }
}

void editor::Project::unloadChildSceneInline(uint32_t childSceneId) {
    SceneProject* childScene = getScene(childSceneId);
    if (!childScene) {
        return;
    }

    if (childScene->opened) {
        // Scene is open as a tab, only clear the inline flag
        childScene->expandedInline = false;
        markParentScenesNeedUpdate(childSceneId);
        return;
    }

    deleteSceneProject(childScene);
    cleanupEntityBundlesForScene(childSceneId);
    childScene->expandedInline = false;
    markParentScenesNeedUpdate(childSceneId);

    Out::info("Unloaded child scene '%s' from inline", childScene->name.c_str());
}

void editor::Project::addChildScene(uint32_t sceneId, uint32_t childSceneId) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Scene with ID %u not found", sceneId);
        return;
    }

    // Prevent adding self as child
    if (sceneId == childSceneId) {
        Out::error("Cannot add a scene as its own child");
        return;
    }

    // Check if child scene exists
    const SceneProject* childScene = getScene(childSceneId);
    if (!childScene) {
        Out::error("Child scene with ID %u not found", childSceneId);
        return;
    }

    // Check if already added
    auto& childScenes = sceneProject->childScenes;
    if (std::find(childScenes.begin(), childScenes.end(), childSceneId) != childScenes.end()) {
        Out::warning("Child scene '%s' already exists in scene '%s'", childScene->name.c_str(), sceneProject->name.c_str());
        return;
    }

    childScenes.push_back(childSceneId);
    sceneProject->isModified = true;
    Out::info("Added child scene '%s' to scene '%s'", childScene->name.c_str(), sceneProject->name.c_str());
}

void editor::Project::removeChildScene(uint32_t sceneId, uint32_t childSceneId) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Scene with ID %u not found", sceneId);
        return;
    }

    auto& childScenes = sceneProject->childScenes;
    auto it = std::find(childScenes.begin(), childScenes.end(), childSceneId);
    if (it != childScenes.end()) {
        childScenes.erase(it);
        sceneProject->isModified = true;
 
        const SceneProject* childScene = getScene(childSceneId);
        if (childScene) {
            Out::info("Removed child scene '%s' from scene '%s'", childScene->name.c_str(), sceneProject->name.c_str());
        }
    }
}

bool editor::Project::hasChildScene(uint32_t sceneId, uint32_t childSceneId) const {
    const SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return false;
    }

    const auto& childScenes = sceneProject->childScenes;
    return std::find(childScenes.begin(), childScenes.end(), childSceneId) != childScenes.end();
}

std::vector<uint32_t> editor::Project::getChildScenes(uint32_t sceneId) const {
    const SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return {};
    }
    return sceneProject->childScenes;
}

Entity editor::Project::createNewEntity(uint32_t sceneId, std::string entityName){
    for (int i = 0; i < scenes.size(); i++){
        if (scenes[i].id == sceneId){
            Entity entity = scenes[i].scene->createEntity();
            scenes[i].scene->setEntityName(entity, entityName);

            scenes[i].entities.push_back(entity);

            setSelectedEntity(sceneId, entity);

            return entity;
        }
    }

    return NULL_ENTITY;
}

bool editor::Project::createNewComponent(uint32_t sceneId, Entity entity, ComponentType component){
    for (int i = 0; i < scenes.size(); i++){
        if (scenes[i].id == sceneId){
            if (component == ComponentType::Transform){
                scenes[i].scene->addComponent<Transform>(entity, {});
            }
            if (component == ComponentType::MeshComponent){
                scenes[i].scene->addComponent<MeshComponent>(entity, {});
            }
            return true;
        }
    }

    return false;
}

void editor::Project::deleteSceneProject(SceneProject* sceneProject){
    if (sceneProject->sceneRender) {
        Camera* editorCam = sceneProject->sceneRender->getCamera();
        if (editorCam) {
            sceneProject->editorCameraState = Stream::encodeEditorCamera(editorCam);
        }
    }

    if (sceneProject->sceneRender)
        delete sceneProject->sceneRender;
    if (sceneProject->scene)
        delete sceneProject->scene;

    sceneProject->sceneRender = nullptr;
    sceneProject->scene = nullptr;

    sceneProject->mainCamera = NULL_ENTITY;
    sceneProject->defaultCamera = NULL_ENTITY;

    sceneProject->entities.clear();
    sceneProject->selectedEntities.clear();
}

void editor::Project::resetEngineConfigs(bool executeViewChanged) {
    Engine::setScalingMode(Scaling::NATIVE);
    Engine::setTextureStrategy(TextureStrategy::RESIZE);
    Engine::setFixedTimeSceneUpdate(false);

    if (executeViewChanged) {
        Engine::systemViewChanged();
    }
}

void editor::Project::resetConfigs() {
    // Clear existing scenes
    for (auto& sceneProject : scenes) {
        deleteSceneProject(&sceneProject);
    }
    scenes.clear();
    entityBundles.clear();
    Backend::getApp().resetLastActivatedScene();

    // Reset state
    name = "";
    canvasWidth = 1280;
    canvasHeight = 720;
    scalingMode = Scaling::FITWIDTH;
    textureStrategy = TextureStrategy::RESIZE;
    assetsDir = ".";
    luaDir = ".";
    cmakeCCompiler = "";
    cmakeCxxCompiler = "";
    cmakeGenerator = "";
    selectedScene = NULL_PROJECT_SCENE;
    selectedSceneForProperties = NULL_PROJECT_SCENE;
    nextSceneId = 0;
    startSceneId = NULL_PROJECT_SCENE;
    terrainEditorSettings = {};
    projectPath.clear();
    materialFileLinks.clear();
    tabs.clear();
    lastMaterialRefreshTime = std::chrono::steady_clock::time_point{};
    projectHistory.clear();

    Backend::updateWindowTitle(name);

    resetEngineConfigs(false);

    //createNewScene("New Scene");
}

std::vector<editor::SceneScriptSource> editor::Project::collectAllSceneCppScripts() const {
    std::unordered_set<std::string> uniquePaths;
    std::vector<SceneScriptSource> mergedScripts;

    for (const auto& sceneProject : scenes) {
        for (const auto& script : sceneProject.cppScripts) {
            std::string pathKey = script.path.lexically_normal().generic_string();
            if (!uniquePaths.insert(pathKey).second) {
                continue;
            }

            mergedScripts.push_back(script);
        }
    }

    return mergedScripts;
}

std::vector<editor::BundleSceneInfo> editor::Project::collectAllBundles() const {
    std::unordered_set<std::string> uniquePaths;
    std::vector<BundleSceneInfo> result;

    for (const auto& sceneProject : scenes) {
        for (const auto& bundle : sceneProject.bundles) {
            std::string pathKey = bundle.bundlePath.generic_string();
            if (!uniquePaths.insert(pathKey).second) {
                continue;
            }
            result.push_back(bundle);
        }
    }

    return result;
}

void editor::Project::pauseEngineScene(Scene* scene, bool pause) const{
    scene->getSystem<PhysicsSystem>()->setPaused(pause);
    scene->getSystem<ActionSystem>()->setPaused(pause);
    scene->getSystem<AudioSystem>()->setPaused(pause);
}

void editor::Project::copyEngineApiToProject() {
    try {
        std::filesystem::path exePath;
        #ifdef _WIN32
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            exePath = std::filesystem::path(path).parent_path();
        #else
            exePath = std::filesystem::canonical("/proc/self/exe").parent_path();
        #endif
        std::filesystem::path engineApiSource = exePath / "engine";

        if (!std::filesystem::exists(engineApiSource)) {
            Out::warning("engine folder not found at: %s", engineApiSource.string().c_str());
            return;
        }

        std::filesystem::path engineApiDest = getProjectInternalPath() / "engine-api";

        // Create internal path if it doesn't exist
        if (!std::filesystem::exists(getProjectInternalPath())) {
            std::filesystem::create_directories(getProjectInternalPath());
        }

        // Copy only headers with update_existing - only copies files that are newer
        for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(engineApiSource)) {
            if (dirEntry.is_regular_file()) {
                auto ext = dirEntry.path().extension().string();
                if (ext == ".h" || ext == ".hpp" || ext == ".inl" || ext == ".glsl" || ext == ".frag" || ext == ".vert") {
                    auto relPath = std::filesystem::relative(dirEntry.path(), engineApiSource);
                    auto destPath = engineApiDest / relPath;
                    std::filesystem::create_directories(destPath.parent_path());
                    std::filesystem::copy_file(dirEntry.path(), destPath, std::filesystem::copy_options::update_existing);
                }
            }
        }

        Out::info("Updated engine API in project: %s", engineApiDest.string().c_str());

    } catch (const std::exception& e) {
        Out::error("Failed to copy engine API: %s", e.what());
    }
}

void editor::Project::finalizeStart(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene>& runtimeScenes) {
    for (auto& entry : runtimeScenes) {
        SceneProject* sceneProject = entry.runtime;
        if (!sceneProject || !sceneProject->scene) {
            continue;
        }

        prepareRuntimeScene(entry);

        if (sceneProject != mainSceneProject) {
            Backend::getApp().enqueueMainThreadTask([sceneProject]() {
                Engine::addSceneLayer(sceneProject->scene);
            });
        }
    }

    Engine::pauseGameEvents(false);
    Engine::setCanvasSize(canvasWidth, canvasHeight);
    Engine::setScalingMode(scalingMode);
    Engine::setTextureStrategy(textureStrategy);
    Engine::onViewLoaded.call();
    Engine::systemViewChanged();

    if (mainSceneProject) {
        Out::success("Scene '%s' started", mainSceneProject->name.c_str());
    }
}

void editor::Project::finalizeStop(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene> runtimeScenes) {
    for (const auto& entry : runtimeScenes) {
        SceneProject* sceneProject = entry.runtime;
        if (!sceneProject || !sceneProject->scene) {
            continue;
        }

        pauseEngineScene(sceneProject->scene, true);
        sceneProject->scene->getSystem<UISystem>()->setAnchorReferenceSize(canvasWidth, canvasHeight);

        // Destroy all bundle instances created during play before restoring snapshot
        BundleManager::destroyAllInstances(sceneProject->scene);

        // Stop scene audio before restoring snapshot to prevent stale SoLoud handles on the next play
        sceneProject->scene->getSystem<AudioSystem>()->stopSceneSounds();

        // Restore snapshot if present
        if (sceneProject->playStateSnapshot && !sceneProject->playStateSnapshot.IsNull()) {
            Stream::decodeScene(sceneProject->scene, sceneProject->playStateSnapshot["scene"]);

            auto entitiesNode = sceneProject->playStateSnapshot["entities"];
            for (const auto& entityNode : entitiesNode) {
                Stream::decodeEntity(entityNode, sceneProject->scene, nullptr, nullptr, sceneProject, NULL_ENTITY, false);
            }

            // Remove InstancedMeshComponent dynamically added during play (e.g. particle targets).
            for (const auto& entityNode : entitiesNode) {
                if (!entityNode["entity"] || !entityNode["components"]) continue;
                ProjectUtils::removeDynamicInstmesh(entityNode["entity"].as<Entity>(), entityNode["components"], sceneProject->scene);
            }

            // Clear the snapshot
            sceneProject->playStateSnapshot = YAML::Node();
        }

        sceneProject->playState = ScenePlayState::STOPPED;

        Entity cameraEntity = sceneProject->scene->getCamera();
        if (cameraEntity != NULL_ENTITY && sceneProject->scene->isEntityCreated(cameraEntity)) {
            if (CameraComponent* cameraComponent = sceneProject->scene->findComponent<CameraComponent>(cameraEntity)) {
                cameraComponent->needUpdate = true;
            }
        }

        if (sceneProject->sceneRender){
            sceneProject->sceneRender->setPlayMode(false);
        }

        if (sceneProject != mainSceneProject) {
            Backend::getApp().enqueueMainThreadTask([this, sceneProject, entry]() {
                Engine::removeScene(sceneProject->scene);

                // Delete scene because its not opened in editor
                if (entry.ownedRuntime) {
                    deleteSceneProject(sceneProject);
                    delete sceneProject;
                }
            });
        }
    }

    Backend::getApp().enqueueMainThreadTask([]() {
        SceneManager::clearAll();
        BundleManager::clearAll();
        Backend::getApp().resetLastActivatedScene();
    });

    resetEngineConfigs(true);

    if (mainSceneProject) {
        Out::success("Scene '%s' stopped", mainSceneProject->name.c_str());
    }
}

bool editor::Project::createTempProject(std::string projectName, bool deleteIfExists) {
    if (isAnyScenePlaying()) {
        Out::warning("Cannot create a new project while a scene is running or stopping.");
        return false;
    }

    try {
        resetConfigs();

        // Clear the last project path in settings when creating a new temp project
        AppSettings::setLastProjectPath(std::filesystem::path());

        projectPath = std::filesystem::temp_directory_path() / projectName;
        fs::path projectFile = projectPath / "project.yaml";

        if (deleteIfExists && fs::exists(projectPath)) {
            fs::remove_all(projectPath);
        }

        if (!std::filesystem::exists(projectFile)) {
            if (!std::filesystem::exists(projectPath)) {
                std::filesystem::create_directory(projectPath);
            }
            Out::info("Created project directory: \"%s\"", projectPath.string().c_str());
            saveProject();
            createNewScene("New Scene", SceneType::SCENE_3D);
            copyEngineApiToProject();
        } else {
            Out::info("Project directory already exists: \"%s\"", projectPath.string().c_str());
            loadProject(projectPath);
        }

        Backend::getApp().updateResourcesPath();

    } catch (const std::exception& e) {
        printf("Error: %s\n", e.what());
        return false;
    }

    return true;
}

bool editor::Project::saveProject(bool userCalled, std::function<void()> callback) {
    if (isTempProject() && userCalled) {
        Backend::getApp().registerProjectSaveDialog(callback);
        return true;
    }

    bool saveret = saveProjectToPath(projectPath);

    if (callback) {
        callback();
    }

    return saveret;
}

bool editor::Project::saveProjectFile() {
    if (projectPath.empty()) {
        return false;
    }

    try {
        YAML::Node root = Stream::encodeProject(this);

        std::filesystem::path projectFile = projectPath / "project.yaml";
        std::ofstream fout(projectFile.string());
        if (!fout) {
            Out::error("Failed to open project file for writing: %s", projectFile.string().c_str());
            return false;
        }

        fout << YAML::Dump(root);
        fout.close();

        return true;
    } catch (const std::exception& e) {
        Out::error("Failed to save project file: \"%s\"", e.what());
        return false;
    }
}

void editor::Project::clearTrash() {
    if (projectPath.empty())
        return;

    std::filesystem::path trashPath = projectPath / ".trash";
    if (std::filesystem::exists(trashPath)) {
        try {
            std::filesystem::remove_all(trashPath);
            Out::info("Cleared trash directory: %s", trashPath.string().c_str());
        } catch (const std::exception& e) {
            Out::error("Failed to clear trash directory: %s", e.what());
        }
    }
}

bool editor::Project::saveProjectToPath(const std::filesystem::path& path) {
    // Try to create the directory if it doesn't exist
    if (!std::filesystem::exists(path)) {
        try {
            std::filesystem::create_directory(path);
        } catch (const std::exception& e) {
            Out::error("Failed to create project directory: %s", e.what());
            Backend::getApp().registerAlert("Error", "Failed to create project directory!");
            return false;
        }
    }

    // Check if we're moving from temp location
    bool wasTemp = isTempProject();
    std::filesystem::path oldPath = projectPath;

    projectPath = path;

    // If we're moving from a temp path, handle the file transfers
    if (wasTemp && oldPath != path) {
        try {
            std::filesystem::path oldBuildPath = getProjectInternalPath() / "build";
            if (std::filesystem::exists(oldBuildPath)) {
                std::filesystem::remove_all(oldBuildPath);
            }

            std::filesystem::path oldExtBuildPath = getProjectInternalPath() / "externalbuild";
            if (std::filesystem::exists(oldExtBuildPath)) {
                std::filesystem::remove_all(oldExtBuildPath);
            }

            // Copy all project files from temp dir to the new location
            for (const auto& entry : std::filesystem::directory_iterator(oldPath)) {
                std::filesystem::path destPath = path / entry.path().filename();
                std::filesystem::copy(entry.path(), destPath, 
                                     std::filesystem::copy_options::recursive);
            }

            // Scene filepaths are stored as relative paths within the project,
            // and the directory structure is preserved by the copy, so they
            // remain valid without modification.

            // Delete the temp directory after moving all files
            std::filesystem::remove_all(oldPath);

        } catch (const std::exception& e) {
            Out::error("Failed to move project files: %s", e.what());
            Backend::getApp().registerAlert("Error", "Failed to move project files to the new location!");
            return false;
        }
    }

    if (!saveProjectFile()) {
        return false;
    }

    if (!isTempProject()){
        AppSettings::setLastProjectPath(path);
    }
    Backend::getApp().updateResourcesPath();

    return true;
}

bool editor::Project::loadProject(const std::filesystem::path path) {
    if (isAnyScenePlaying()) {
        Out::warning("Cannot load a project while a scene is running or stopping.");
        return false;
    }

    resetConfigs();

    projectPath = path;

    try {
        if (!std::filesystem::exists(projectPath)) {
            Out::error("Project directory does not exist: \"%s\"", projectPath.string().c_str());
            return false;
        }

        std::filesystem::path projectFile = projectPath / "project.yaml";
        if (!std::filesystem::exists(projectFile)) {
            Out::error("Project file does not exist: %s", projectFile.string().c_str());
            return false;
        }

        // Load and parse project file
        YAML::Node projectNode = YAML::LoadFile(projectFile.string());
        Stream::decodeProject(this, projectNode);

        // Create a default scene if no scenes were loaded
        if (scenes.empty()) {
            createNewScene("New Scene", SceneType::SCENE_3D);
        }

        // Copy engine-api to project
        copyEngineApiToProject();

        Backend::getApp().updateResourcesPath();

        // Save this as the last opened project
        if (!isTempProject()) {
            AppSettings::setLastProjectPath(projectPath);
        }

        clearTrash();

        Out::info("Project loaded successfully: \"%s\"", projectPath.string().c_str());
        return true;

    } catch (const YAML::Exception& e) {
        Out::error("Failed to load project YAML: \"%s\"", e.what());
        Backend::getApp().registerAlert("Error", "Failed to load project file!");
        return false;
    } catch (const std::exception& e) {
        Out::error("Failed to load project: \"%s\"", e.what());
        Backend::getApp().registerAlert("Error", "Failed to load project!");
        return false;
    }
}

bool editor::Project::openProject() {
    if (isAnyScenePlaying()) {
        Out::warning("Cannot open a project while a scene is running or stopping.");
        return false;
    }

    // Get user's home directory as default path
    std::string homeDirPath;
    #ifdef _WIN32
    homeDirPath = std::filesystem::path(getenv("USERPROFILE")).string();
    #else
    homeDirPath = std::filesystem::path(getenv("HOME")).string();
    #endif

    // Open a folder selection dialog
    std::string selectedDir = FileDialogs::openFileDialog(homeDirPath, false, true);

    if (selectedDir.empty()) {
        return false; // User canceled the dialog
    }

    std::filesystem::path projectDir = std::filesystem::path(selectedDir);
    std::filesystem::path projectFile = projectDir / "project.yaml";

    // Check if the selected directory contains a project.yaml file
    if (!std::filesystem::exists(projectFile)) {
        Backend::getApp().registerAlert("Error", "The selected directory is not a valid project. No project.yaml file found!");
        return false;
    }

    if (loadProject(projectDir)) {
        return true;
    } else {
        Out::error("Failed to open project: \"%s\"", projectDir.string().c_str());
        Backend::getApp().registerAlert("Error", "Failed to open project!");
        return false;
    }
}

void editor::Project::saveScene(uint32_t sceneId) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Cannot save scene - invalid scene ID: %u", sceneId);
        return;
    }

    // If filepath is already set, just save to that path
    if (!sceneProject->filepath.empty()) {
        saveSceneToPath(sceneId, sceneProject->filepath);
    } else {
        // Otherwise show save dialog through the App
        Backend::getApp().registerSaveSceneDialog(sceneId);
    }

    // Also save modified child scenes that are loaded inline
    for (uint32_t childId : sceneProject->childScenes) {
        const SceneProject* childScene = getScene(childId);
        if (childScene && childScene->expandedInline && childScene->scene && hasSceneUnsavedChanges(childId)) {
            saveScene(childId);
        }
    }
}

void editor::Project::saveSceneToPath(uint32_t sceneId, const std::filesystem::path& path) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return;
    }

    fs::path fullPath = path;
    if (fullPath.is_relative()) {
        fullPath = getProjectPath() / fullPath;
    }

    std::error_code ec;
    fs::path relPath = fs::relative(fullPath, getProjectPath(), ec);
    if (ec || relPath.empty()) {
        Out::error("Scene filepath must be relative to project path: %s", path.string().c_str());
        return;
    }

    // Check if this scene has entities in bundles and save them first
    for (const auto& [filepath, bundle] : entityBundles) {
        if (bundle.instances.find(sceneId) != bundle.instances.end()) {
            if (bundle.isModified) {
                saveEntityBundleToDisk(filepath);
            }
        }
    }

    calculateSceneMaxValues(sceneProject, sceneProject->maxValues);
    collectSceneShaderKeys(sceneProject, sceneProject->shaderKeys);

    updateSceneCppScripts(sceneProject);
    updateSceneBundles(sceneProject);

    YAML::Node root = Stream::encodeSceneProject(this, sceneProject);
    std::ofstream fout(fullPath.string());
    fout << YAML::Dump(root);
    fout.close();

    std::string oldFilepath = sceneProject->filepath.string();
    sceneProject->filepath = relPath;
    sceneProject->isModified = false;

    // Update tabs: if filepath changed, update existing tab entry
    if (sceneProject->opened) {
        if (!oldFilepath.empty() && oldFilepath != relPath.string()) {
            removeTab(TabType::SCENE, oldFilepath);
        }
        addTab(TabType::SCENE, relPath.string());
    }

    TerrainEditWindow::cleanUnusedTerrainMaps(this);

    saveProject();

    std::vector<BundleInstanceInfo> bundleInstances = generator.writeBundleSources(entityBundles, sceneId, getProjectPath(),getProjectInternalPath());
    generator.writeSceneSource(sceneProject->scene, sceneProject->name, sceneProject->entities, getSceneCamera(sceneProject), getProjectPath(), getProjectInternalPath(), bundleInstances);

    std::vector<editor::SceneBuildInfo> scenesToConfig;
    for (SceneProject& sceneConf : scenes) {
        bool isMain = (sceneId == sceneConf.id);
        std::vector<uint32_t> involvedSceneIds;
        collectInvolvedScenes(sceneConf.id, involvedSceneIds);

        scenesToConfig.push_back({sceneConf.id, sceneConf.name, involvedSceneIds, isMain});
    }

    std::vector<SceneScriptSource> mergedCppScripts = collectAllSceneCppScripts();
    std::vector<BundleSceneInfo> bundleBuildInfos = collectAllBundles();
    generator.configure(scenesToConfig, libName, mergedCppScripts, bundleBuildInfos, getProjectPath(), getProjectInternalPath(), scalingMode, textureStrategy, canvasWidth, canvasHeight);

    Out::info("Scene saved to: \"%s\"", fullPath.string().c_str());
}

void editor::Project::saveAllScenes() {
    for (auto& sceneProject : scenes) {
        if (sceneProject.isModified) {
            saveScene(sceneProject.id);
        }
    }
}

void editor::Project::saveLastSelectedScene(){
    saveScene(selectedScene);
}

Ray editor::Project::screenToRayFromCamera(const CameraComponent& camera, float x, float y) const{
    float normalized_x = ((2.0f * x) / Engine::getCanvasWidth()) - 1.0f;
    float normalized_y = -(((2.0f * y) / Engine::getCanvasHeight()) - 1.0f);

    Vector4 near_point_ndc = {normalized_x, normalized_y, -1.0f, 1.0f};
    Vector4 far_point_ndc = {normalized_x, normalized_y, 1.0f, 1.0f};

    Matrix4 inverseViewProjection = camera.viewProjectionMatrix.inverse();
    Vector4 near_point_world = inverseViewProjection * near_point_ndc;
    Vector4 far_point_world = inverseViewProjection * far_point_ndc;

    near_point_world.divideByW();
    far_point_world.divideByW();

    Vector3 ray_origin = {near_point_world[0], near_point_world[1], near_point_world[2]};
    Vector3 ray_end = {far_point_world[0], far_point_world[1], far_point_world[2]};
    Vector3 ray_direction = ray_end - ray_origin;

    if (camera.type == CameraType::CAMERA_ORTHO) {
        float length = camera.farClip - camera.nearClip;
        ray_direction = ray_direction.normalize() * length;
    }

    return Ray(ray_origin, ray_direction);
}

AABB editor::Project::getEntityWorldAABB(Scene* scene, Entity entity, Scene* mainScene) const{
    AABB aabb;
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<MeshComponent>())){
        aabb = scene->getComponent<MeshComponent>(entity).worldAABB;
    }else if (signature.test(scene->getComponentId<UIComponent>())){
        aabb = scene->getComponent<UIComponent>(entity).worldAABB;
        if (signature.test(scene->getComponentId<UILayoutComponent>())){
            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
            if (layout.width > 0 && layout.height > 0){
                Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                Transform& transform = scene->getComponent<Transform>(entity);
                aabb = transform.modelMatrix * AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
            }
        }
    }else if (signature.test(scene->getComponentId<LightComponent>()) || 
              signature.test(scene->getComponentId<CameraComponent>()) ||
              signature.test(scene->getComponentId<SoundComponent>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        Transform& camtransform = mainScene->getComponent<Transform>(mainScene->getCamera());
        CameraComponent& camera = mainScene->getComponent<CameraComponent>(mainScene->getCamera());
        float dist = (transform.worldPosition - camtransform.worldPosition).length();
        float size = dist * tan(camera.yfov) * 0.01;
        aabb = transform.modelMatrix * AABB(-size, -size, -size, size, size, size);
    }else if (signature.test(scene->getComponentId<PointsComponent>())){
        if (signature.test(scene->getComponentId<Transform>())) {
            const PointsComponent& pts = scene->getComponent<PointsComponent>(entity);
            const Transform& transform = scene->getComponent<Transform>(entity);
            const Transform& camtransform = mainScene->getComponent<Transform>(mainScene->getCamera());
            const CameraComponent& camera = mainScene->getComponent<CameraComponent>(mainScene->getCamera());

            auto mergeWorldPoint = [&](const Vector3& worldPos) {
                float size;
                if (camera.type == CameraType::CAMERA_PERSPECTIVE) {
                    float dist = (worldPos - camtransform.worldPosition).length();
                    size = dist * std::tan(camera.yfov) * 0.01f;
                } else {
                    size = (camera.topClip - camera.bottomClip) * 0.02f;
                }
                if (size < 0.001f) size = 0.001f;
                aabb.merge(AABB(worldPos.x - size, worldPos.y - size, worldPos.z - size,
                                worldPos.x + size, worldPos.y + size, worldPos.z + size));
            };

            // Always include the entity origin
            mergeWorldPoint(transform.worldPosition);

            for (const PointData& pt : pts.points) {
                if (!pt.visible) continue;
                mergeWorldPoint(transform.modelMatrix * pt.position);
            }
        }
    }

    return aabb;
}

AABB editor::Project::getEntityLocalAABB(Scene* scene, Entity entity) const{
    AABB aabb;
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<MeshComponent>())){
        aabb = scene->getComponent<MeshComponent>(entity).aabb;
    }else if (signature.test(scene->getComponentId<UIComponent>())){
        aabb = scene->getComponent<UIComponent>(entity).aabb;
        if (signature.test(scene->getComponentId<UILayoutComponent>())){
            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
            if (layout.width > 0 && layout.height > 0){
                Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                aabb = AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
            }
        }
    }else if (signature.test(scene->getComponentId<LightComponent>()) || 
              signature.test(scene->getComponentId<CameraComponent>()) ||
              signature.test(scene->getComponentId<SoundComponent>())){
        aabb = AABB::ZERO;
    }else if (signature.test(scene->getComponentId<PointsComponent>())){
        aabb.setNull();
        const PointsComponent& pts = scene->getComponent<PointsComponent>(entity);
        // Always include the entity origin (local space zero)
        aabb.merge(Vector3::ZERO);
        for (const PointData& pt : pts.points) {
            if (!pt.visible) continue;
            aabb.merge(pt.position);
        }
    }

    return aabb;
}

Entity editor::Project::findBestEntityByRay(const std::vector<Entity>& entities, Scene* scene, const Ray& ray, Scene* mainScene, SceneType sceneType, float& distance, size_t& index) const{
    Entity selEntity = NULL_ENTITY;
    for (auto& entity : entities) {
        if (!scene->getSignature(entity).test(scene->getComponentId<Transform>())) continue;

        AABB aabb = getEntityWorldAABB(scene, entity, mainScene);

        if (!aabb.isNull() && !aabb.isInfinite()){
            RayReturn rreturn = ray.intersects(aabb);
            if (rreturn.hit){
                size_t nIndex = scene->getComponentArray<Transform>()->getIndex(entity);
                if (rreturn.distance < distance || (nIndex >= index && sceneType != SceneType::SCENE_3D)){
                    distance = rreturn.distance;
                    index = nIndex;
                    selEntity = entity;
                }
            }
        }
    }
    return selEntity;
}

bool editor::Project::selectEntitiesInRect(uint32_t sceneId, const std::vector<Entity>& entities, Scene* scene, const Matrix4& vpMatrix, Vector2 start, Vector2 end){
    Vector2 minRect = Vector2(std::min(start.x, end.x), std::min(start.y, end.y));
    Vector2 maxRect = Vector2(std::max(start.x, end.x), std::max(start.y, end.y));

    bool found = false;
    for (auto& entity : entities) {
        if (!scene->getSignature(entity).test(scene->getComponentId<Transform>())) continue;

        AABB aabb = getEntityLocalAABB(scene, entity);

        if (!aabb.isNull() && !aabb.isInfinite()){
            Transform& transform = scene->getComponent<Transform>(entity);
            const Vector3* corners = aabb.getCorners();

            bool inside = true;
            for (int c = 0; c < 8; c++){
                Vector4 clipCorner = vpMatrix * transform.modelMatrix * Vector4(corners[c], 1.0);
                Vector3 ndcCorner = Vector3(clipCorner) / clipCorner.w;

                if (!(ndcCorner.x >= minRect.x && ndcCorner.x <= maxRect.x && ndcCorner.y >= minRect.y && ndcCorner.y <= maxRect.y)){
                    inside = false;
                    break;
                }
            }

            if (inside){
                addSelectedEntity(sceneId, entity);
                found = true;
            }
        }
    }
    return found;
}

Entity editor::Project::findObjectByRay(uint32_t sceneId, float x, float y, uint32_t* outSceneId){
    SceneProject* scenedata = getScene(sceneId);
    Ray ray = scenedata->sceneRender->getCamera()->screenToRay(x, y);

    float distance = FLT_MAX;
    size_t index = 0;
    Entity selEntity = findBestEntityByRay(scenedata->entities, scenedata->scene, ray, scenedata->scene, scenedata->sceneType, distance, index);

    if (selEntity != NULL_ENTITY || !outSceneId) {
        if (outSceneId) *outSceneId = sceneId;
        return selEntity;
    }

    for (uint32_t childId : scenedata->childScenes) {
        SceneProject* childScene = getScene(childId);
        if (!childScene || !childScene->expandedInline || !childScene->scene) continue;

        Entity childCamEntity = childScene->scene->getCamera();
        if (childCamEntity == NULL_ENTITY) continue;
        Ray childRay = screenToRayFromCamera(childScene->scene->getComponent<CameraComponent>(childCamEntity), x, y);

        distance = FLT_MAX;
        index = 0;
        selEntity = findBestEntityByRay(childScene->entities, childScene->scene, childRay, scenedata->scene, scenedata->sceneType, distance, index);

        if (selEntity != NULL_ENTITY) {
            if (outSceneId) *outSceneId = childId;
            return selEntity;
        }
    }

    if (outSceneId) *outSceneId = sceneId;
    return NULL_ENTITY;
}

bool editor::Project::selectObjectByRay(uint32_t sceneId, float x, float y, bool shiftPressed){
    SceneProject* scenedata = getScene(sceneId);

    setSelectedSceneForProperties(sceneId);

    uint32_t hitSceneId = sceneId;
    Entity selEntity = findObjectByRay(sceneId, x, y, &hitSceneId);

    if (!scenedata->sceneRender->isAnyGizmoSideSelected()){
        if (selEntity != NULL_ENTITY){
            if (hitSceneId != sceneId){
                setSelectedSceneForProperties(hitSceneId);
            }
            if (!shiftPressed){
                clearAllSelections(sceneId);
            }
            addSelectedEntity(hitSceneId, selEntity);
            return true;
        }

        clearAllSelections(sceneId);
    }

    return false;
}

bool editor::Project::selectObjectsByRect(uint32_t sceneId, Vector2 start, Vector2 end){
    SceneProject* scenedata = getScene(sceneId);

    setSelectedSceneForProperties(sceneId);

    Camera* camera = scenedata->sceneRender->getCamera();

    clearAllSelections(sceneId);

    if (selectEntitiesInRect(sceneId, scenedata->entities, scenedata->scene, camera->getViewProjectionMatrix(), start, end)) {
        return false;
    }

    // Try children if no main scene selections
    for (uint32_t childId : scenedata->childScenes) {
        SceneProject* childScene = getScene(childId);
        if (!childScene || !childScene->expandedInline || !childScene->scene) continue;

        Entity childCamEntity = childScene->scene->getCamera();
        if (childCamEntity == NULL_ENTITY) continue;

        CameraComponent& childCam = childScene->scene->getComponent<CameraComponent>(childCamEntity);

        if (selectEntitiesInRect(childId, childScene->entities, childScene->scene, childCam.viewProjectionMatrix, start, end)) {
            setSelectedSceneForProperties(childId);
        }
    }

    return false;
}

std::vector<editor::SceneProject>& editor::Project::getScenes(){
    return scenes;
}

const std::vector<editor::SceneProject>& editor::Project::getScenes() const{
    return scenes;
}

std::vector<editor::TabEntry>& editor::Project::getTabs(){
    return tabs;
}

const std::vector<editor::TabEntry>& editor::Project::getTabs() const{
    return tabs;
}

void editor::Project::addTab(TabType type, const std::string& filepath){
    if (!hasTab(type, filepath)){
        tabs.push_back({type, filepath});
    }
}

void editor::Project::removeTab(TabType type, const std::string& filepath){
    tabs.erase(
        std::remove_if(tabs.begin(), tabs.end(), [&](const TabEntry& t){
            return t.type == type && t.filepath == filepath;
        }),
        tabs.end()
    );
}

bool editor::Project::hasTab(TabType type, const std::string& filepath) const{
    return std::any_of(tabs.begin(), tabs.end(), [&](const TabEntry& t){
        return t.type == type && t.filepath == filepath;
    });
}

template<typename T>
T* editor::Project::findScene(uint32_t sceneId) const {
    for (int i = 0; i < scenes.size(); i++) {
        if (scenes[i].id == sceneId) {
            return const_cast<T*>(&scenes[i]);
        }
    }
    return nullptr;
}

// Non-const version
editor::SceneProject* editor::Project::getScene(uint32_t sceneId) {
    return findScene<editor::SceneProject>(sceneId);
}

// Const version
const editor::SceneProject* editor::Project::getScene(uint32_t sceneId) const {
    return findScene<const editor::SceneProject>(sceneId);
}

editor::SceneProject* editor::Project::getSelectedScene(){
    return getScene(selectedScene);
}

const editor::SceneProject* editor::Project::getSelectedScene() const{
    return getScene(selectedScene);
}

void editor::Project::setNextSceneId(uint32_t nextSceneId){
    this->nextSceneId = nextSceneId;
}

uint32_t editor::Project::getNextSceneId() const{
    return nextSceneId;
}

void editor::Project::setSelectedSceneId(uint32_t selectedScene){
    if (this->selectedScene != selectedScene){
        this->selectedScene = selectedScene;
        this->selectedSceneForProperties = selectedScene;

        //debugSceneHierarchy();
    }
}

uint32_t editor::Project::getSelectedSceneId() const{
    return selectedScene;
}

void editor::Project::setSelectedSceneForProperties(uint32_t selectedScene){
    this->selectedSceneForProperties = selectedScene;
}

uint32_t editor::Project::getSelectedSceneForProperties() const{
    return selectedSceneForProperties;
}

bool editor::Project::isTempProject() const{
    std::error_code ec;
    auto relPath = std::filesystem::relative(projectPath, std::filesystem::temp_directory_path(), ec);

    if (ec) {
        return false;
    }

    return relPath.string().find("..") == std::string::npos;
}

bool editor::Project::isTempUnsavedProject() const{
    bool isTemp = isTempProject();

    if (isTemp){
        for (auto& scene : scenes){
            if (!scene.filepath.empty() || scene.isModified){
                return true;
            }
        }
    }

    return false;
}

std::filesystem::path editor::Project::getProjectPath() const{
    return projectPath;
}

std::filesystem::path editor::Project::getProjectInternalPath() const{
    return projectPath / ".doriax";
}

fs::path editor::Project::getTerrainMapsDir() const{
    return projectPath / "terrain_maps";
}

fs::path editor::Project::getThumbsDir() const{
    return getProjectInternalPath() / "thumbs";
}

fs::path editor::Project::getThumbnailPath(const fs::path& originalPath) const {
    fs::path thumbsDir = getThumbsDir();
    fs::path resolvedPath = originalPath;

    if (resolvedPath.is_relative() && !projectPath.empty()) {
        resolvedPath = projectPath / resolvedPath;
    }

    resolvedPath = resolvedPath.lexically_normal();

    // Get relative path from project root, as a string
    fs::path relativePath = fs::relative(resolvedPath, getProjectPath());
    std::string relPathStr = relativePath.generic_string();

    // Include file size and modification time in hash for uniqueness
    auto fileSize = fs::file_size(resolvedPath);
    auto modTime = fs::last_write_time(resolvedPath).time_since_epoch().count();
    std::string hashInput = relPathStr + "_" + std::to_string(static_cast<uint64_t>(fileSize)) + "_" + std::to_string(static_cast<int64_t>(modTime));

    // Hash the combined string
    std::string hash = SHA1::hash(hashInput);

    std::string thumbFilename = hash + ".thumb.png";
    return thumbsDir / thumbFilename;
}

std::vector<Entity> editor::Project::getEntities(uint32_t sceneId) const{
    return getScene(sceneId)->entities;
}

void editor::Project::replaceSelectedEntities(uint32_t sceneId, std::vector<Entity> selectedEntities){
    getScene(sceneId)->selectedEntities = selectedEntities;
}

void editor::Project::setSelectedEntity(uint32_t sceneId, Entity selectedEntity){
    std::vector<Entity>& entities = getScene(sceneId)->selectedEntities;

    entities.clear();
    if (selectedEntity != NULL_ENTITY){
        entities.push_back(selectedEntity);
    }
}

void editor::Project::addSelectedEntity(uint32_t sceneId, Entity selectedEntity){
    std::vector<Entity>& entities = getScene(sceneId)->selectedEntities;
    Scene* scene = getScene(sceneId)->scene;
    auto transforms = scene->getComponentArray<Transform>();

    if (selectedEntity != NULL_ENTITY){
        if (std::find(entities.begin(), entities.end(), selectedEntity) == entities.end()) {
            entities.push_back(selectedEntity);
        }
    }

    // removing childs of selected entities
    std::vector<Entity> removeChilds;
    for (auto& entity: entities){
        if (scene->getSignature(entity).test(scene->getComponentId<Transform>())){
            size_t firstIndex = transforms->getIndex(entity);
            size_t branchIndex = scene->findBranchLastIndex(entity);
            for (int t = (firstIndex+1); t <= branchIndex; t++){
                Entity childEntity = transforms->getEntity(t);
                if (std::find(entities.begin(), entities.end(), childEntity) != entities.end()) {
                    removeChilds.push_back(childEntity);
                    #ifdef _DEBUG
                    printf("DEBUG: Removed entity %u from selection\n", childEntity);
                    #endif
                }
            }
        }
    }
    entities.erase(
        std::remove_if(entities.begin(), entities.end(), [&removeChilds](Entity value) {
            return std::find(removeChilds.begin(), removeChilds.end(), value) != removeChilds.end();
        }),
        entities.end()
    );
}

bool editor::Project::isSelectedEntity(uint32_t sceneId, Entity selectedEntity){
    std::vector<Entity>& entities = getScene(sceneId)->selectedEntities;

    if (std::find(entities.begin(), entities.end(), selectedEntity) != entities.end()) {
        return true;
    }

    return false;
}

void editor::Project::clearSelectedEntities(uint32_t sceneId){
    getScene(sceneId)->selectedEntities.clear();
}

void editor::Project::clearAllSelections(uint32_t sceneId){
    clearSelectedEntities(sceneId);
    for (uint32_t cId : getChildScenes(sceneId)) {
        clearSelectedEntities(cId);
    }
}

std::vector<Entity> editor::Project::getSelectedEntities(uint32_t sceneId) const{
    return getScene(sceneId)->selectedEntities;
}

bool editor::Project::hasSelectedEntities(uint32_t sceneId) const{
    return (getScene(sceneId)->selectedEntities.size() > 0);
}

bool editor::Project::hasSelectedSceneUnsavedChanges() const{
    return hasSceneUnsavedChanges(selectedScene);
}

bool editor::Project::hasSelectedSceneUnsavedEntityBundles() const{
    return hasUnsavedEntityBundles(selectedScene);
}

bool editor::Project::hasSceneUnsavedChanges(uint32_t sceneId) const{
    const SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject){
        return false;
    }

    if (sceneProject->isModified){
        return true;
    }

    if (hasUnsavedEntityBundles(sceneId)){
        return true;
    }

    for (uint32_t childId : sceneProject->childScenes) {
        const SceneProject* childScene = getScene(childId);
        if (childScene && childScene->expandedInline && childScene->scene && hasSceneUnsavedChanges(childId)) {
            return true;
        }
    }

    return false;
}

bool editor::Project::hasUnsavedEntityBundles(uint32_t sceneId) const{
    for (const auto& [filepath, bundle] : entityBundles) {
        if (bundle.isModified && bundle.hasInstances(sceneId)) {
            return true;
        }
    }

    return false;
}

bool editor::Project::hasScenesUnsavedChanges() const{
    for (auto& scene: scenes){
        if (scene.isModified){
            return true;
        }
    }

    if (hasUnsavedEntityBundles()){
         return true;
    }

    return false;
}

bool editor::Project::hasUnsavedEntityBundles() const{
    for (const auto& [filepath, bundle] : entityBundles) {
        if (bundle.isModified) {
            return true;
        }
    }

    return false;
}


void editor::Project::updateAllScriptsProperties(uint32_t sceneId){
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) return;

    for (Entity entity : sceneProject->entities) {
        Signature signature = sceneProject->scene->getSignature(entity);
        if (signature.test(sceneProject->scene->getComponentId<ScriptComponent>())) {
            ScriptComponent& scriptComponent = sceneProject->scene->getComponent<ScriptComponent>(entity);
            updateScriptProperties(sceneProject, entity, scriptComponent.scripts);
        }
    }
}

void editor::Project::updateScriptProperties(SceneProject* sceneProject, Entity entity, std::vector<ScriptEntry>& scripts, const std::string& inMemoryContent, const std::string& inMemoryPath) {
    bool hasChanges = false;

    // Update properties for each script in the component
    for (auto& scriptEntry : scripts) {
        // C++ scripts: keep existing behavior
        if (scriptEntry.type == ScriptType::SUBCLASS ||
            scriptEntry.type == ScriptType::SCRIPT_CLASS) {

            fs::path fullPath = scriptEntry.headerPath;
            if (fullPath.is_relative()) {
                fullPath = getProjectPath() / fullPath;
            }

            std::vector<ScriptProperty> parsedProperties;
            if (!inMemoryContent.empty() && fullPath.string() == fs::path(inMemoryPath).string()) {
                parsedProperties = ScriptParser::parseScriptPropertiesFromString(inMemoryContent, fullPath.string());
            } else {
                parsedProperties = ScriptParser::parseScriptProperties(fullPath);
            }
            if (parsedProperties.empty()) {
                continue;
            }

            // Merge with existing properties to preserve user-modified values
            std::vector<ScriptProperty> mergedProperties;
            bool structuralChanges = false; // added/removed properties
            bool metaChanges = false;       // display name/type changed

            for (const auto& parsedProp : parsedProperties) {
                auto it = std::find_if(scriptEntry.properties.begin(), scriptEntry.properties.end(),
                    [&](const ScriptProperty& existing) { return existing.name == parsedProp.name; });

                if (it != scriptEntry.properties.end()) {
                    ScriptProperty merged = *it;

                    if (merged.displayName != parsedProp.displayName ||
                        merged.type != parsedProp.type ||
                        merged.ptrTypeName != parsedProp.ptrTypeName) {
                        metaChanges = true;
                        merged.displayName = parsedProp.displayName;
                        merged.type = parsedProp.type;
                        merged.ptrTypeName = parsedProp.ptrTypeName;
                    }

                    merged.defaultValue = parsedProp.defaultValue;

                    mergedProperties.push_back(std::move(merged));
                } else {
                    structuralChanges = true;
                    mergedProperties.push_back(parsedProp);
                }
            }

            if (scriptEntry.properties.size() != mergedProperties.size()) {
                structuralChanges = true;
            }

            scriptEntry.properties = std::move(mergedProperties);

            if (structuralChanges || metaChanges) {
                hasChanges = true;
            }

            continue;
        }

        // Lua scripts: load properties from Lua file
        if (scriptEntry.type == ScriptType::SCRIPT_LUA) {
            fs::path fullPath = scriptEntry.path;
            if (fullPath.is_relative()) {
                fullPath = getProjectPath() / fullPath;
            }

            // Keep previous properties to preserve current values
            std::vector<ScriptProperty> oldProps = scriptEntry.properties;

            if (!inMemoryContent.empty() && fullPath.string() == fs::path(inMemoryPath).string()) {
                ProjectUtils::loadLuaScriptPropertiesFromString(scriptEntry, inMemoryContent, fullPath.string());
            } else {
                ProjectUtils::loadLuaScriptProperties(scriptEntry, fullPath.string());
            }

            // Merge: keep old values if names match
            for (auto& newProp : scriptEntry.properties) {
                auto itOld = std::find_if(oldProps.begin(), oldProps.end(),
                    [&](const ScriptProperty& p) { return p.name == newProp.name; });

                if (itOld != oldProps.end()) {
                    // If type changed, reset to default; otherwise keep user value
                    if (itOld->type == newProp.type) {
                        newProp.value = itOld->value;
                    } else {
                        hasChanges = true;
                    }
                } else {
                    hasChanges = true;
                }
            }

            continue;
        }
    }

    if (hasChanges) {
        sceneProject->isModified = true;
    }
}

bool editor::Project::createEntityBundle(uint32_t sceneId, fs::path filepath, YAML::Node entityNode){
    if (!filepath.is_relative()) {
        Out::error("EntityBundle filepath must be relative: %s", filepath.string().c_str());
        return false;
    }

    auto it = entityBundles.find(filepath);
    if (it != entityBundles.end()) {
        Out::error("EntityBundle group already exists at %s", filepath.string().c_str());
        return false;
    }

    // Get all entities in the branch (root + children)
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return false;
    }

    // Create new group
    EntityBundle newGroup;
    newGroup.registry = std::make_unique<EntityRegistry>();
    newGroup.isModified = true;

    EntityBundle::Instance newInstance;
    newInstance.instanceId = newGroup.nextInstanceId++;

    // Keep original scene order for member mapping and undo operations.
    std::vector<Entity> branchEntities;
    ProjectUtils::collectEntities(entityNode, branchEntities);

    std::vector<Entity> regEntities = Stream::decodeEntitySelection(clearEntitiesNode(entityNode), newGroup.registry.get(), &newGroup.registryEntities);
    if (branchEntities.size() == regEntities.size()) {
        std::unordered_map<Entity, Entity> localToRegistry;
        for (size_t i = 0; i < branchEntities.size(); ++i) {
            localToRegistry[branchEntities[i]] = regEntities[i];
        }
        remapEntityProperties(newGroup.registry.get(), regEntities, localToRegistry);
    }

    Scene* scene = sceneProject->scene;
    Entity rootEntity = scene->createUserEntity();

    BundleComponent bundleComp;
    bundleComp.name = filepath.stem().string();
    bundleComp.path = filepath.string();
    scene->addComponent<BundleComponent>(rootEntity, bundleComp);

    // Only create a transform root when bundle top-level entities use hierarchy.
    std::vector<Entity> topLevelEntities = getTopLevelEntities(newGroup.registry.get(), regEntities);
    bool hasTopLevelTransform = false;
    for (Entity topLevelEntity : topLevelEntities) {
        if (newGroup.registry->getSignature(topLevelEntity).test(newGroup.registry->getComponentId<Transform>())) {
            hasTopLevelTransform = true;
            break;
        }
    }

    if (hasTopLevelTransform) {
        scene->addComponent<Transform>(rootEntity, {});
    }

    std::string rootName = filepath.stem().string();
    if (rootName.empty()) {
        rootName = "Bundle";
    }

    std::string uniqueRootName = rootName;
    unsigned int nameCount = 2;
    bool foundName = true;
    while (foundName) {
        foundName = false;
        for (Entity sceneEntity : sceneProject->entities) {
            if (scene->getEntityName(sceneEntity) == uniqueRootName) {
                uniqueRootName = rootName + " " + std::to_string(nameCount++);
                foundName = true;
                break;
            }
        }
    }

    scene->setEntityName(rootEntity, uniqueRootName);
    sceneProject->entities.push_back(rootEntity);

    Entity firstBundleEntity = NULL_ENTITY;
    Entity firstBundleTransformEntity = NULL_ENTITY;
    size_t firstBundleIndex = std::numeric_limits<size_t>::max();
    size_t firstBundleTransformIndex = std::numeric_limits<size_t>::max();

    for (Entity branchEntity : branchEntities) {
        auto itSceneEntity = std::find(sceneProject->entities.begin(), sceneProject->entities.end(), branchEntity);
        if (itSceneEntity == sceneProject->entities.end()) {
            continue;
        }

        size_t entityIndex = std::distance(sceneProject->entities.begin(), itSceneEntity);
        if (entityIndex < firstBundleIndex) {
            firstBundleIndex = entityIndex;
            firstBundleEntity = branchEntity;
        }

        if (scene->findComponent<Transform>(branchEntity) && entityIndex < firstBundleTransformIndex) {
            firstBundleTransformIndex = entityIndex;
            firstBundleTransformEntity = branchEntity;
        }
    }

    // Place root before the first bundled entity to preserve visual ordering.
    Entity moveTarget = hasTopLevelTransform ? firstBundleTransformEntity : firstBundleEntity;
    if (moveTarget != NULL_ENTITY) {
        Entity oldParent = NULL_ENTITY;
        size_t oldIndex = 0;
        bool hasTransform = false;
        ProjectUtils::moveEntityOrderByTarget(scene, sceneProject->entities, rootEntity, moveTarget, InsertionType::BEFORE, oldParent, oldIndex, hasTransform);
    }

    // Reparent top-level transformed bundle entities under the new root.
    if (hasTopLevelTransform) {
        std::vector<Entity> sceneTopLevelEntities = getTopLevelEntities(scene, branchEntities);
        for (Entity topLevelEntity : sceneTopLevelEntities) {
            if (scene->findComponent<Transform>(topLevelEntity)) {
                scene->addEntityChild(rootEntity, topLevelEntity, true);
            }
        }
        ProjectUtils::sortEntitiesByTransformOrder(scene, sceneProject->entities);
    }

    newInstance.rootEntity = rootEntity;

    for (int i = 0; i < regEntities.size(); i++) {
        newInstance.members.push_back({branchEntities[i], regEntities[i]});
    }

    newGroup.instances[sceneId].push_back(std::move(newInstance));

    entityBundles.emplace(filepath, std::move(newGroup));

    // Set up event subscriptions for this shared group
    saveEntityBundleToDisk(filepath);

    sceneProject->isModified = true;

    return true;
}

bool editor::Project::removeEntityBundle(const std::filesystem::path& filepath) {
    auto it = entityBundles.find(filepath);
    if (it == entityBundles.end()) {
        return false;
    }

    // Undo scene-side changes introduced by createEntityBundle for each instance.
    for (const auto& [sceneId, instances] : it->second.instances) {
        SceneProject* sceneProject = getScene(sceneId);
        if (!sceneProject) {
            continue;
        }

        for (const auto& instance : instances) {
            if (instance.rootEntity == NULL_ENTITY) {
                continue;
            }

            Scene* scene = sceneProject->scene;
            if (!scene || !scene->isEntityCreated(instance.rootEntity)) {
                continue;
            }

            // If the bundle root owns transformed members, detach them back before removing root.
            Transform* rootTransform = scene->findComponent<Transform>(instance.rootEntity);
            if (rootTransform) {
                Entity rootParent = rootTransform->parent;

                for (const auto& member : instance.members) {
                    if (member.localEntity == NULL_ENTITY || member.localEntity == instance.rootEntity) {
                        continue;
                    }

                    Transform* memberTransform = scene->findComponent<Transform>(member.localEntity);
                    if (memberTransform && memberTransform->parent == instance.rootEntity) {
                        scene->addEntityChild(rootParent, member.localEntity, true);
                    }
                }

                ProjectUtils::sortEntitiesByTransformOrder(scene, sceneProject->entities);
            }

            DeleteEntityCmd::destroyEntity(sceneProject->scene, instance.rootEntity, sceneProject->entities, this, sceneId);
        }
    }

    if (it->second.registry) {
        it->second.registry->clear();
    }

    entityBundles.erase(it);

    editor::Out::info("Removed entity bundle: %s", filepath.string().c_str());
    return true;
}

bool editor::Project::addComponentToBundle(uint32_t sceneId, Entity entity, ComponentType componentType, bool addToItself){
    ComponentRecovery recovery;
    return addComponentToBundle(sceneId, entity, componentType, recovery, addToItself);
}

bool editor::Project::addComponentToBundle(uint32_t sceneId, Entity entity, ComponentType componentType, const ComponentRecovery& recovery, bool addToItself){
    fs::path filepath = findEntityBundlePathFor(sceneId, entity);
    if (filepath.empty()) {
        Out::error("Entity %u in scene %u is not part of any bundle", entity, sceneId);
        return false;
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    uint32_t instanceId = bundle->getInstanceId(sceneId, entity);

    if (bundle->hasComponentOverride(sceneId, entity, componentType)){
        Out::warning("Component %s of entity %u in scene %u is overridden", Catalog::getComponentName(componentType).c_str(), entity, sceneId);
        return false;
    }

    Entity registryEntity = bundle->getRegistryEntity(sceneId, entity);
    if (registryEntity == NULL_ENTITY) {
        Out::error("Failed to find registry entities for bundle entities %u in scene %u", entity, sceneId);
        return false;
    }

    YAML::Node regNode;
    std::string recoveryDefKey = std::to_string(NULL_PROJECT_SCENE);
    if (recovery.find(recoveryDefKey) != recovery.end()) {
        if (recovery.at(recoveryDefKey).entity == registryEntity){
            regNode = recovery.at(recoveryDefKey).node;
        }else{
            Out::error("Component recovery entity (%u) does not match registry entity (%u)", recovery.at(recoveryDefKey).entity, registryEntity);
            return false;
        }
    }

    ProjectUtils::addEntityComponent(bundle->registry.get(), registryEntity, componentType, bundle->registryEntities, regNode);

    std::vector<uint32_t> staleBundleScenes;
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene) {
            Out::error("Failed to find scene %u", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        if (!otherScene->scene) {
            Out::error("Scene %u is not loaded", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        for (auto& instance : sceneInstances) {
            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || addToItself) {
                Entity otherEntity = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryEntity);

                if (otherEntity != NULL_ENTITY) {
                    if (!bundle->hasComponentOverride(otherSceneId, otherEntity, componentType)) {
                        YAML::Node compNode;
                        std::string recoveryKey = std::to_string(otherSceneId) + "_" + std::to_string(instance.instanceId);
                        if (recovery.find(recoveryKey) != recovery.end()) {
                            if (recovery.at(recoveryKey).entity == otherEntity){
                                compNode = recovery.at(recoveryKey).node;
                            }else{
                                Out::warning("Component recovery entity (%u) does not match scene (%u) entity (%u)", recovery.at(recoveryKey).entity, otherSceneId, otherEntity);
                                return false;
                            }
                        }

                        ProjectUtils::addEntityComponent(otherScene->scene, otherEntity, componentType, otherScene->entities, compNode);

                        otherScene->isModified = true;
                    }
                }
            }
        }
    }
    for (uint32_t staleId : staleBundleScenes) {
        cleanupEntityBundlesForScene(staleId);
    }

    bundle->isModified = true;

    return true;
}

editor::ComponentRecovery editor::Project::removeComponentFromBundle(uint32_t sceneId, Entity entity, ComponentType componentType, bool encodeComponent, bool removeToItself){
    fs::path filepath = findEntityBundlePathFor(sceneId, entity);
    if (filepath.empty()) {
        Out::error("Entity %u in scene %u is not part of any bundle", entity, sceneId);
        return {};
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    uint32_t instanceId = bundle->getInstanceId(sceneId, entity);

    if (bundle->hasComponentOverride(sceneId, entity, componentType)){
        Out::warning("Component %s of entity %u in scene %u is overridden", Catalog::getComponentName(componentType).c_str(), entity, sceneId);
        return {};
    }

    Entity registryEntity = bundle->getRegistryEntity(sceneId, entity);
    if (registryEntity == NULL_ENTITY) {
        Out::error("Failed to find registry entities for bundle entities %u in scene %u", entity, sceneId);
        return {};
    }

    ComponentRecovery recovery;
    std::string recoveryDefKey = std::to_string(NULL_PROJECT_SCENE);
    recovery[recoveryDefKey].entity = registryEntity;
    recovery[recoveryDefKey].node = ProjectUtils::removeEntityComponent(bundle->registry.get(), registryEntity, componentType, bundle->registryEntities, encodeComponent);

    std::vector<uint32_t> staleBundleScenes;
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene) {
            Out::error("Failed to find scene %u", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        if (!otherScene->scene) {
            Out::error("Scene %u is not loaded", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        for (auto& instance : sceneInstances) {
            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || removeToItself) {
                Entity otherEntity = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryEntity);

                if (otherEntity != NULL_ENTITY) {
                    if (!bundle->hasComponentOverride(otherSceneId, otherEntity, componentType)) {
                        std::string recoveryKey = std::to_string(otherSceneId) + "_" + std::to_string(instance.instanceId);
                        recovery[recoveryKey].entity = otherEntity;
                        recovery[recoveryKey].node = ProjectUtils::removeEntityComponent(otherScene->scene, otherEntity, componentType, otherScene->entities, encodeComponent);

                        otherScene->isModified = true;
                    }
                }
            }
        }
    }
    for (uint32_t staleId : staleBundleScenes) {
        cleanupEntityBundlesForScene(staleId);
    }

    bundle->isModified = true;

    return recovery;
}

void editor::Project::saveEntityBundleToDisk(const std::filesystem::path& filepath) {
    EntityBundle* bundle = getEntityBundle(filepath);
    YAML::Node encodedNode = encodeEntityBundleNode(filepath);
    if (encodedNode && !encodedNode.IsNull()) {
        std::filesystem::path fullBundlePath = getProjectPath() / filepath;
        std::ofstream fout(fullBundlePath.string());
        if (fout.is_open()) {  // Check if file opened successfully
            fout << YAML::Dump(encodedNode);
            fout.close();
            bundle->isModified = false;
        } else {
            Out::error("Failed to open file for writing: %s", fullBundlePath.string().c_str());
        }
    }
}

editor::EntityBundle* editor::Project::getEntityBundle(const std::filesystem::path& filepath){
    if (filepath.empty()){
        return nullptr;
    }
    auto it = entityBundles.find(filepath);
    if (it != entityBundles.end()) {
        return &it->second;
    }
    return nullptr;
}

const editor::EntityBundle* editor::Project::getEntityBundle(const std::filesystem::path& filepath) const{
    if (filepath.empty()){
        return nullptr;
    }
    auto it = entityBundles.find(filepath);
    if (it != entityBundles.end()) {
        return &it->second;
    }
    return nullptr;
}

std::map<std::filesystem::path, const editor::EntityBundle*> editor::Project::getEntityBundles(uint32_t sceneId) const {
    std::map<std::filesystem::path, const EntityBundle*> bundlesInScene;
    for (const auto& [filepath, bundle] : entityBundles) {
        if (bundle.hasInstances(sceneId)) {
            bundlesInScene[filepath] = &bundle;
        }
    }
    return bundlesInScene;
}

std::filesystem::path editor::Project::findEntityBundlePathFor(uint32_t sceneId, Entity entity) const {
    // First pass: prefer member matches (so nested bundle roots resolve to the outer bundle)
    for (const auto& [filepath, bundle] : entityBundles) {
        auto sceneIt = bundle.instances.find(sceneId);
        if (sceneIt == bundle.instances.end()) {
            continue;
        }

        for (const auto& instance : sceneIt->second) {
            for (const auto& member : instance.members) {
                if (member.localEntity == entity) {
                    return filepath;
                }
            }
        }
    }

    // Second pass: root matches
    for (const auto& [filepath, bundle] : entityBundles) {
        auto sceneIt = bundle.instances.find(sceneId);
        if (sceneIt == bundle.instances.end()) {
            continue;
        }

        for (const auto& instance : sceneIt->second) {
            if (instance.rootEntity == entity) {
                return filepath;
            }
        }
    }

    return std::filesystem::path();
}

YAML::Node editor::Project::clearEntitiesNode(YAML::Node node) {
    if (!node || !node.IsMap())
        return node;

    node.remove("entity");

    if (node["members"] && node["members"].IsSequence()) {
        for (size_t i = 0; i < node["members"].size(); ++i) {
            node["members"][i] = clearEntitiesNode(node["members"][i]);
        }
        return node;
    }

    if (node["children"] && node["children"].IsSequence()) {
        for (size_t i = 0; i < node["children"].size(); ++i) {
            node["children"][i] = clearEntitiesNode(node["children"][i]);
        }
    }

    return node;
}

YAML::Node editor::Project::changeEntitiesNode(Entity& firstEntity, YAML::Node node) {
    if (!node || !node.IsMap())
        return node;

    if (node["members"] && node["members"].IsSequence()) {
        for (size_t i = 0; i < node["members"].size(); ++i) {
            node["members"][i] = changeEntitiesNode(firstEntity, node["members"][i]);
        }
        return node;
    }

    // Assign the current entity ID
    node["entity"] = firstEntity++;

    // Recursively process children
    if (node["children"] && node["children"].IsSequence()) {
        for (size_t i = 0; i < node["children"].size(); ++i) {
            node["children"][i] = changeEntitiesNode(firstEntity, node["children"][i]);
        }
    }

    return node;
}

YAML::Node editor::Project::encodeEntityBundleNode(const std::filesystem::path& filepath) const {
    const EntityBundle* bundle = getEntityBundle(filepath);
    if (!bundle || !bundle->registry) {
        return YAML::Node();
    }

    std::vector<Entity> topLevelEntities = getTopLevelEntities(bundle->registry.get(), bundle->registryEntities);
    return Stream::encodeEntitySelection(topLevelEntities, bundle->registry.get(), this);
}

std::vector<Entity> editor::Project::importEntityBundle(SceneProject* sceneProject, std::vector<Entity>* entities, const std::filesystem::path& filepath, Entity rootEntity, bool needSaveScene, const YAML::Node& bundleOverrides, const YAML::Node& bundleLocalEntities) {
    if (!filepath.is_relative()) {
        Out::error("EntityBundle filepath must be relative: %s", filepath.string().c_str());
        return {};
    }

    // Cycle detection for nested bundles
    static thread_local std::unordered_set<std::string> loadingPaths;
    std::string pathStr = filepath.string();
    if (loadingPaths.count(pathStr)) {
        Out::error("Circular bundle reference detected: %s", pathStr.c_str());
        return {};
    }
    loadingPaths.insert(pathStr);

    auto it = entityBundles.find(filepath);

    if (it == entityBundles.end()) {
        EntityBundle newGroup;
        newGroup.registry = std::make_unique<EntityRegistry>();
        newGroup.isModified = false;

        auto [newIt, inserted] = entityBundles.emplace(filepath, std::move(newGroup));
        it = newIt;
    }

    auto& bundle = it->second;

    EntityBundle::Instance newInstance;
    newInstance.instanceId = bundle.nextInstanceId++;

    YAML::Node node;
    if (bundle.isModified) {
        YAML::Node bundleNode;
        bundleNode["type"] = "EntityBundle";
        YAML::Node membersNode(YAML::NodeType::Sequence);
        if (!bundle.registryEntities.empty()) {
            std::vector<Entity> topLevelEntities = getTopLevelEntities(bundle.registry.get(), bundle.registryEntities);
            for (Entity entity : topLevelEntities) {
                membersNode.push_back(Stream::encodeEntity(entity, bundle.registry.get(), this));
            }
        }
        bundleNode["members"] = membersNode;
        node = bundleNode;
    } else {
        try {
            std::filesystem::path fullBundlePath = getProjectPath() / filepath;
            node = YAML::LoadFile(fullBundlePath.string());
            bundle.registry->clear();
            bundle.registryEntities.clear();
            Stream::decodeEntitySelection(node, bundle.registry.get(), &bundle.registryEntities);
        } catch (const std::exception& e) {
            Out::error("Failed to load entity bundle file: %s", e.what());
            loadingPaths.erase(pathStr);
            return {};
        }
    }

    Scene* scene = sceneProject->scene;

    // Decode bundle entities into the scene
    std::vector<Entity> newEntities = Stream::decodeEntitySelection(node, scene, entities);

    // Reparent top-level transformed bundle entities under root
    bool hasTopLevelTransform = scene->getSignature(rootEntity).test(scene->getComponentId<Transform>());
    if (hasTopLevelTransform) {
        std::vector<Entity> sceneTopLevelEntities = getTopLevelEntities(scene, newEntities);
        for (Entity topLevelEntity : sceneTopLevelEntities) {
            if (scene->findComponent<Transform>(topLevelEntity)) {
                scene->addEntityChild(rootEntity, topLevelEntity, false);
            }
        }
    }

    newInstance.rootEntity = rootEntity;

    std::vector<Entity> regEntities = bundle.registryEntities;
    if (newEntities.size() == regEntities.size()) {
        std::unordered_map<Entity, Entity> registryToLocal;
        for (size_t i = 0; i < newEntities.size(); i++) {
            newInstance.members.push_back({newEntities[i], regEntities[i]});
            registryToLocal[regEntities[i]] = newEntities[i];
        }
        remapEntityProperties(scene, newEntities, registryToLocal);
    } else {
        Out::error("importEntityBundle(%s): entity count mismatch: newEntities=%zu, regEntities=%zu",
            filepath.string().c_str(), newEntities.size(), regEntities.size());
    }

    // Remove decoded children of nested bundle roots
    // (they will be recreated by the recursive import below)
    std::unordered_set<Entity> nestedBundleRoots;
    for (Entity ent : newEntities) {
        if (scene->findComponent<BundleComponent>(ent)) {
            nestedBundleRoots.insert(ent);
        }
    }
    if (!nestedBundleRoots.empty()) {
        std::unordered_set<Entity> entitiesToRemove;
        for (Entity ent : newEntities) {
            if (nestedBundleRoots.count(ent)) continue;
            Transform* tf = scene->findComponent<Transform>(ent);
            if (!tf) continue;
            Entity ancestor = tf->parent;
            while (ancestor != NULL_ENTITY) {
                if (nestedBundleRoots.count(ancestor)) {
                    entitiesToRemove.insert(ent);
                    break;
                }
                Transform* parentTf = scene->findComponent<Transform>(ancestor);
                ancestor = parentTf ? parentTf->parent : NULL_ENTITY;
            }
        }
        for (Entity ent : entitiesToRemove) {
            scene->destroyEntity(ent);
            if (entities) {
                entities->erase(std::remove(entities->begin(), entities->end(), ent), entities->end());
            }
        }
        newEntities.erase(
            std::remove_if(newEntities.begin(), newEntities.end(),
                [&entitiesToRemove](Entity e) { return entitiesToRemove.count(e); }),
            newEntities.end());
        newInstance.members.erase(
            std::remove_if(newInstance.members.begin(), newInstance.members.end(),
                [&entitiesToRemove](const EntityBundle::EntityMember& m) { return entitiesToRemove.count(m.localEntity); }),
            newInstance.members.end());
    }

    // Collect nested overrides/localEntities (entries with bundlePath)
    std::map<std::string, YAML::Node> nestedOverridesMap;
    std::map<std::string, YAML::Node> nestedLocalEntitiesMap;

    if (bundleOverrides && bundleOverrides.IsSequence()) {
        for (const auto& entry : bundleOverrides) {
            if (entry["bundlePath"]) {
                std::string bp = entry["bundlePath"].as<std::string>();
                if (entry["bundleRootRegistryEntity"]) {
                    bp += "_" + std::to_string(entry["bundleRootRegistryEntity"].as<Entity>());
                }
                YAML::Node cleanEntry;
                cleanEntry["registryEntity"] = entry["registryEntity"];
                if (entry["components"]) cleanEntry["components"] = entry["components"];
                nestedOverridesMap[bp].push_back(cleanEntry);
            }
        }
    }

    if (bundleLocalEntities && bundleLocalEntities.IsSequence()) {
        for (const auto& entry : bundleLocalEntities) {
            if (entry["bundlePath"]) {
                std::string bp = entry["bundlePath"].as<std::string>();
                if (entry["bundleRootRegistryEntity"]) {
                    bp += "_" + std::to_string(entry["bundleRootRegistryEntity"].as<Entity>());
                }
                YAML::Node cleanEntry = YAML::Clone(entry);
                cleanEntry.remove("bundlePath");
                cleanEntry.remove("bundleRootRegistryEntity");
                nestedLocalEntitiesMap[bp].push_back(cleanEntry);
            }
        }
    }

    // Apply component overrides keyed by registryEntity (skip nested entries)
    if (bundleOverrides && bundleOverrides.IsSequence()) {
        for (const auto& entry : bundleOverrides) {
            if (entry["bundlePath"]) continue;
            if (!entry["registryEntity"]) continue;
            Entity regEntity = entry["registryEntity"].as<Entity>();

            // Find the local entity for this registryEntity
            Entity localEntity = NULL_ENTITY;
            for (const auto& member : newInstance.members) {
                if (member.registryEntity == regEntity) {
                    localEntity = member.localEntity;
                    break;
                }
            }
            if (localEntity == NULL_ENTITY) continue;

            if (entry["components"]) {
                // Save parent before decoding so Transform overrides don't reparent
                Transform* existingTf = scene->findComponent<Transform>(localEntity);
                Entity savedParent = existingTf ? existingTf->parent : NULL_ENTITY;

                Stream::decodeComponents(localEntity, savedParent, scene, entry["components"]);

                // Track which components are overridden
                uint64_t overrideMask = 0;
                for (auto compIt = entry["components"].begin(); compIt != entry["components"].end(); ++compIt) {
                    ComponentType compType = Catalog::getComponentType(compIt->first.as<std::string>());
                    overrideMask |= 1ULL << static_cast<int>(compType);
                }
                newInstance.overrides[localEntity] = overrideMask;
            }
        }
    }

    std::vector<Entity> allResult = newEntities;

    // Recursively import nested bundles
    for (Entity ent : newEntities) {
        BundleComponent* bundleComp = scene->findComponent<BundleComponent>(ent);
        if (bundleComp && !bundleComp->path.empty()) {
            // Find the registry entity for this nested bundle root in the parent bundle
            Entity nestedRegEntity = NULL_ENTITY;
            for (const auto& member : newInstance.members) {
                if (member.localEntity == ent) {
                    nestedRegEntity = member.registryEntity;
                    break;
                }
            }

            std::string nestedKey = bundleComp->path;
            if (nestedRegEntity != NULL_ENTITY) {
                nestedKey += "_" + std::to_string(nestedRegEntity);
            }

            YAML::Node nestedOvr, nestedLoc;
            auto ovrIt = nestedOverridesMap.find(nestedKey);
            if (ovrIt != nestedOverridesMap.end()) nestedOvr = ovrIt->second;
            auto locIt = nestedLocalEntitiesMap.find(nestedKey);
            if (locIt != nestedLocalEntitiesMap.end()) nestedLoc = locIt->second;
            std::vector<Entity> nestedEntities = importEntityBundle(sceneProject, entities, bundleComp->path, ent, false, nestedOvr, nestedLoc);
            allResult.insert(allResult.end(), nestedEntities.begin(), nestedEntities.end());
        }
    }

    // Create scene-specific local entities (skip nested entries)
    if (bundleLocalEntities && bundleLocalEntities.IsSequence()) {
        for (const auto& localEntNode : bundleLocalEntities) {
            if (localEntNode["bundlePath"]) continue;
            Entity parentRegEntity = NULL_ENTITY;
            size_t childIndex = 0;
            if (localEntNode["parentRegistryEntity"]) {
                parentRegEntity = localEntNode["parentRegistryEntity"].as<Entity>();
            }
            if (localEntNode["childIndex"]) {
                childIndex = localEntNode["childIndex"].as<size_t>();
            }

            // Find the parent local entity (or use bundle root)
            Entity parentEntity = rootEntity;
            if (parentRegEntity != NULL_ENTITY) {
                for (const auto& member : newInstance.members) {
                    if (member.registryEntity == parentRegEntity) {
                        parentEntity = member.localEntity;
                        break;
                    }
                }
            }

            std::vector<Entity> decoded = Stream::decodeEntity(localEntNode, scene, entities, this, sceneProject, parentEntity);
            if (!decoded.empty()) {
                Entity localEntity = decoded[0];
                scene->addEntityChild(parentEntity, localEntity, true);

                // Position at childIndex using moveChildToIndex
                if (scene->findComponent<Transform>(localEntity)) {
                    auto transforms = scene->getComponentArray<Transform>();
                    size_t parentIndex = transforms->getIndex(parentEntity);
                    size_t targetIndex = parentIndex + 1 + childIndex;
                    scene->moveChildToIndex(localEntity, targetIndex);
                }

                allResult.insert(allResult.end(), decoded.begin(), decoded.end());
            }
        }
    }

    bundle.instances[sceneProject->id].push_back(std::move(newInstance));

    sceneProject->isModified = needSaveScene;

    loadingPaths.erase(pathStr);

    return allResult;
}

bool editor::Project::unimportEntityBundle(uint32_t sceneId, const std::filesystem::path& filepath, Entity rootEntity, const std::vector<Entity>& memberEntities) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return false;
    }

    Scene* scene = sceneProject->scene;

    // Remove the instance from the bundle
    EntityBundle* bundle = getEntityBundle(filepath);
    if (bundle) {
        uint32_t instanceId = bundle->getInstanceId(sceneId, rootEntity);
        if (instanceId != 0) {
            auto sceneIt = bundle->instances.find(sceneId);
            if (sceneIt != bundle->instances.end()) {
                auto& sceneInstances = sceneIt->second;
                sceneInstances.erase(
                    std::remove_if(sceneInstances.begin(), sceneInstances.end(),
                        [instanceId](const EntityBundle::Instance& inst) { return inst.instanceId == instanceId; }),
                    sceneInstances.end()
                );
                if (sceneInstances.empty()) {
                    bundle->instances.erase(sceneIt);
                }
            }
        }
    }

    // Clean up nested bundle instance tracking before destroying entities
    for (Entity memberEntity : memberEntities) {
        if (scene->isEntityCreated(memberEntity)) {
            BundleComponent* bc = scene->findComponent<BundleComponent>(memberEntity);
            if (bc && !bc->path.empty()) {
                EntityBundle* nestedBundle = getEntityBundle(bc->path);
                if (nestedBundle) {
                    auto nestedSceneIt = nestedBundle->instances.find(sceneId);
                    if (nestedSceneIt != nestedBundle->instances.end()) {
                        auto& nestedInstances = nestedSceneIt->second;
                        nestedInstances.erase(
                            std::remove_if(nestedInstances.begin(), nestedInstances.end(),
                                [memberEntity](const EntityBundle::Instance& inst) { return inst.rootEntity == memberEntity; }),
                            nestedInstances.end()
                        );
                        if (nestedInstances.empty()) {
                            nestedBundle->instances.erase(nestedSceneIt);
                        }
                    }
                }
            }
        }
    }

    // Collect all entities to destroy (members + local entities that are children of root/members)
    std::unordered_set<Entity> memberSet(memberEntities.begin(), memberEntities.end());
    memberSet.insert(rootEntity);
    std::vector<Entity> allEntitiesToDestroy;

    // Gather non-member children (local entities) from Transform hierarchy
    auto transforms = scene->getComponentArray<Transform>();
    for (size_t i = 0; i < transforms->size(); i++) {
        Entity ent = transforms->getEntity(i);
        if (memberSet.count(ent)) continue;
        // Walk up parent chain to see if this entity is a descendant of the root or a member
        Transform& tf = transforms->getComponentFromIndex(i);
        Entity ancestor = tf.parent;
        while (ancestor != NULL_ENTITY) {
            if (memberSet.count(ancestor)) {
                allEntitiesToDestroy.push_back(ent);
                break;
            }
            Transform* parentTf = scene->findComponent<Transform>(ancestor);
            ancestor = parentTf ? parentTf->parent : NULL_ENTITY;
        }
    }

    // Destroy local entities first (they may reference members as parents)
    for (Entity entity : allEntitiesToDestroy) {
        if (scene->isEntityCreated(entity)) {
            DeleteEntityCmd::destroyEntity(scene, entity, sceneProject->entities, this, sceneId);
        }
    }

    // Destroy all imported member entities
    for (Entity entity : memberEntities) {
        if (scene->isEntityCreated(entity)) {
            DeleteEntityCmd::destroyEntity(scene, entity, sceneProject->entities, this, sceneId);
        }
    }

    // Destroy root entity
    if (rootEntity != NULL_ENTITY && scene->isEntityCreated(rootEntity)) {
        DeleteEntityCmd::destroyEntity(scene, rootEntity, sceneProject->entities, this, sceneId);
    }

    sceneProject->isModified = true;

    return true;
}

bool editor::Project::addEntityToBundle(uint32_t sceneId, Entity entity, Entity parent, bool createItself){
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject){
        return false;
    }

    Scene* scene = sceneProject->scene;

    // Find which bundle the parent belongs to
    // First check if parent is a bundle root (for explicit "Insert to Bundle" targeting)
    fs::path filepath;
    for (const auto& [bundlePath, bundle] : entityBundles) {
        auto sceneIt = bundle.instances.find(sceneId);
        if (sceneIt == bundle.instances.end()) continue;
        for (const auto& instance : sceneIt->second) {
            if (instance.rootEntity == parent) {
                filepath = bundlePath;
                break;
            }
        }
        if (!filepath.empty()) break;
    }
    // Fall back to general lookup if parent is not a root
    if (filepath.empty()) {
        filepath = findEntityBundlePathFor(sceneId, parent);
    }
    if (filepath.empty()) {
        Out::error("Entity parent %u in scene %u is not part of any entity bundle", parent, sceneId);
        return false;
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    if (!bundle) {
        return false;
    }

    uint32_t instanceId = bundle->getInstanceId(sceneId, parent);
    if (instanceId == 0) {
        Out::error("Failed to find instance for parent entity %u in scene %u", parent, sceneId);
        return false;
    }

    Entity registryParent = bundle->getRegistryEntity(sceneId, parent);
    // parent could be the bundle root (no registryEntity)
    // In that case the entity will be a top-level member under root

    // Encode the entity (preserve original for entity ID collection)
    YAML::Node nodeOriginal = Stream::encodeEntity(entity, scene, nullptr, sceneProject);
    YAML::Node nodeRegData = clearEntitiesNode(YAML::Clone(nodeOriginal));

    // Don't add nested bundle children to parent bundle's registry
    bool isNestedBundle = scene->findComponent<BundleComponent>(entity) != nullptr;
    if (isNestedBundle) {
        nodeRegData.remove("children");
    }

    // Decode into registry
    std::vector<Entity> regEntities = Stream::decodeEntity(nodeRegData, bundle->registry.get(), &bundle->registryEntities);
    if (regEntities.empty()) {
        Out::error("Failed to decode entity into bundle registry");
        return false;
    }

    bool hasTransform = scene->getSignature(entity).test(scene->getComponentId<Transform>());
    Entity previousRegistryEntity = NULL_ENTITY;
    auto regEntityIt = std::find(bundle->registryEntities.begin(), bundle->registryEntities.end(), regEntities[0]);
    if (regEntityIt != bundle->registryEntities.begin() && regEntityIt != bundle->registryEntities.end()) {
        previousRegistryEntity = *std::prev(regEntityIt);
    }

    // Position in registry if parent has a registry entity
    if (registryParent != NULL_ENTITY) {
        ProjectUtils::moveEntityOrderByTransform(bundle->registry.get(), bundle->registryEntities, regEntities[0], registryParent, 0, false);
    }

    // For each instance, add the entity as a new member
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene || !otherScene->scene) {
            continue;
        }

        for (auto& instance : sceneInstances) {
            std::vector<Entity> newOtherEntities;

            // Find the parent in this instance.
            Entity otherParent = instance.rootEntity;
            if (registryParent != NULL_ENTITY) {
                for (const auto& member : instance.members) {
                    if (member.registryEntity == registryParent) {
                        otherParent = member.localEntity;
                        break;
                    }
                }
            }

            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || createItself) {

                YAML::Node nodeData = Stream::encodeEntity(regEntities[0], bundle->registry.get());
                newOtherEntities = Stream::decodeEntity(nodeData, otherScene->scene, &otherScene->entities);
                if (hasTransform && !newOtherEntities.empty()) {
                    ProjectUtils::moveEntityOrderByTransform(otherScene->scene, otherScene->entities, newOtherEntities[0], otherParent, 0, false);
                }

                // For nested bundles, import the bundle contents for this instance too
                if (isNestedBundle && !newOtherEntities.empty()) {
                    BundleComponent* bc = otherScene->scene->findComponent<BundleComponent>(newOtherEntities[0]);
                    if (bc) {
                        importEntityBundle(otherScene, &otherScene->entities, bc->path, newOtherEntities[0], false);
                    }
                }
            } else {
                // Current instance: collect the existing entity IDs and reparent under the bundle.
                if (isNestedBundle) {
                    newOtherEntities.push_back(entity);
                } else {
                    ProjectUtils::collectEntities(nodeOriginal, newOtherEntities);
                }

                if (hasTransform && !newOtherEntities.empty()) {
                    ProjectUtils::moveEntityOrderByTransform(otherScene->scene, otherScene->entities, newOtherEntities[0], otherParent, 0, false);
                }
            }

            if (!hasTransform && !newOtherEntities.empty()) {
                Entity orderAnchor = instance.rootEntity;
                if (previousRegistryEntity != NULL_ENTITY) {
                    Entity previousLocalEntity = bundle->getLocalEntity(otherSceneId, instance.instanceId, previousRegistryEntity);
                    if (previousLocalEntity != NULL_ENTITY) {
                        orderAnchor = previousLocalEntity;
                    }
                }

                if (orderAnchor != NULL_ENTITY && orderAnchor != newOtherEntities[0]) {
                    Entity oldParent = NULL_ENTITY;
                    size_t oldIndex = 0;
                    bool hasTransform = false;
                    ProjectUtils::moveEntityOrderByTarget(otherScene->scene, otherScene->entities, newOtherEntities[0], orderAnchor, InsertionType::AFTER, oldParent, oldIndex, hasTransform);
                }
            }

            // Add members mapping
            if (regEntities.size() == newOtherEntities.size()) {
                for (size_t e = 0; e < newOtherEntities.size(); e++) {
                    instance.members.push_back({newOtherEntities[e], regEntities[e]});
                }
            }

            otherScene->isModified = true;
        }
    }

    bundle->isModified = true;

    return true;
}

bool editor::Project::addEntityToBundle(uint32_t sceneId, const NodeRecovery& recoveryData, Entity parent, bool createItself){
    fs::path filepath = findEntityBundlePathFor(sceneId, parent);
    if (filepath.empty()) {
        Out::error("Entity parent %u in scene %u is not part of any entity bundle", parent, sceneId);
        return false;
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    if (!bundle) {
        return false;
    }

    Entity registryParent = bundle->getRegistryEntity(sceneId, parent);

    // Recover registry data
    std::string recoveryDefKey = std::to_string(NULL_PROJECT_SCENE);
    YAML::Node nodeRegData;
    size_t regTransformIndex = 0;
    bool hasRegRecoveryData = false;
    if (recoveryData.find(recoveryDefKey) != recoveryData.end()) {
        hasRegRecoveryData = true;
        nodeRegData = recoveryData.at(recoveryDefKey).node;
        regTransformIndex = recoveryData.at(recoveryDefKey).transformIndex;
    }

    if (!nodeRegData) {
        Out::error("No registry data in recovery for addEntityToBundle");
        return false;
    }

    std::vector<Entity> regEntities = Stream::decodeEntity(nodeRegData, bundle->registry.get(), &bundle->registryEntities);
    if (!regEntities.empty()) {
        bool regHasTransform = bundle->registry->getSignature(regEntities[0]).test(bundle->registry->getComponentId<Transform>());
        ProjectUtils::moveEntityOrderByIndex(bundle->registry.get(), bundle->registryEntities, regEntities[0], registryParent, regTransformIndex, regHasTransform);
    }

    // For each instance, recreate entities from recovery
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene || !otherScene->scene) {
            continue;
        }

        for (auto& instance : sceneInstances) {
            std::string recoveryKey = std::to_string(otherSceneId) + "_" + std::to_string(instance.rootEntity);
            YAML::Node nodeData;
            size_t transformIndex = 0;
            bool hasRecoveryData = false;

            if (recoveryData.find(recoveryKey) != recoveryData.end()) {
                hasRecoveryData = true;
                nodeData = recoveryData.at(recoveryKey).node;
                transformIndex = recoveryData.at(recoveryKey).transformIndex;
            } else {
                nodeData = Stream::encodeEntity(regEntities[0], bundle->registry.get());
            }

            // Find the parent in this instance
            Entity otherParent = instance.rootEntity;
            if (registryParent != NULL_ENTITY) {
                for (const auto& member : instance.members) {
                    if (member.registryEntity == registryParent) {
                        otherParent = member.localEntity;
                        break;
                    }
                }
            }

            std::vector<Entity> newOtherEntities;
            uint32_t currentInstanceId = bundle->getInstanceId(otherSceneId, instance.rootEntity);

            if ((otherSceneId != sceneId) || (currentInstanceId != bundle->getInstanceId(sceneId, parent)) || createItself) {
                newOtherEntities = Stream::decodeEntity(nodeData, otherScene->scene, &otherScene->entities);
            } else {
                ProjectUtils::collectEntities(nodeData, newOtherEntities);
            }

            if (!newOtherEntities.empty()) {
                bool otherHasTransform = otherScene->scene->getSignature(newOtherEntities[0]).test(otherScene->scene->getComponentId<Transform>());
                ProjectUtils::moveEntityOrderByIndex(otherScene->scene, otherScene->entities, newOtherEntities[0], otherParent, transformIndex, otherHasTransform);
            }

            // Add members mapping
            if (regEntities.size() == newOtherEntities.size()) {
                for (size_t e = 0; e < newOtherEntities.size(); e++) {
                    instance.members.push_back({newOtherEntities[e], regEntities[e]});
                }
            }

            otherScene->isModified = true;
        }
    }

    bundle->isModified = true;

    return true;
}

editor::NodeRecovery editor::Project::removeEntityFromBundle(uint32_t sceneId, Entity entity, bool destroyItself) {
    fs::path filepath = findEntityBundlePathFor(sceneId, entity);
    if (filepath.empty()) {
        Out::error("Entity %u in scene %u is not part of any entity bundle", entity, sceneId);
        return {};
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    if (!bundle) {
        return {};
    }

    uint32_t instanceId = bundle->getInstanceId(sceneId, entity);

    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return {};
    }

    Entity registryEntity = bundle->getRegistryEntity(sceneId, entity);
    if (registryEntity == NULL_ENTITY) {
        Out::error("Failed to find registry entity for bundle entity %u in scene %u", entity, sceneId);
        return {};
    }

    YAML::Node regData = Stream::encodeEntity(registryEntity, bundle->registry.get(), nullptr, nullptr);

    size_t transformIndex;
    NodeRecovery recovery;

    if (bundle->registry->findComponent<Transform>(registryEntity)) {
        transformIndex = ProjectUtils::getTransformIndex(bundle->registry.get(), registryEntity);
    } else {
        auto regIt = std::find(bundle->registryEntities.begin(), bundle->registryEntities.end(), registryEntity);
        transformIndex = (regIt != bundle->registryEntities.end()) ? std::distance(bundle->registryEntities.begin(), regIt) : 0;
    }
    std::string recoveryDefKey = std::to_string(NULL_PROJECT_SCENE);
    recovery[recoveryDefKey] = {YAML::Clone(regData), transformIndex};

    // Process each scene that has instances
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene || !otherScene->scene) {
            continue;
        }

        for (auto it = sceneInstances.rbegin(); it != sceneInstances.rend(); ++it) {
            auto& instance = *it;
            // Find the local entity for this registry entity in this instance
            Entity otherEntity = NULL_ENTITY;
            for (const auto& member : instance.members) {
                if (member.registryEntity == registryEntity) {
                    otherEntity = member.localEntity;
                    break;
                }
            }

            if (otherEntity == NULL_ENTITY) {
                continue;
            }

            YAML::Node nodeExtend = Stream::encodeEntity(otherEntity, otherScene->scene, nullptr, nullptr);

            std::vector<Entity> allEntities;
            ProjectUtils::collectEntities(nodeExtend, allEntities);

            if (otherScene->scene->findComponent<Transform>(otherEntity)) {
                transformIndex = ProjectUtils::getTransformIndex(otherScene->scene, otherEntity);
            } else {
                auto entityIt = std::find(otherScene->entities.begin(), otherScene->entities.end(), otherEntity);
                transformIndex = (entityIt != otherScene->entities.end()) ? std::distance(otherScene->entities.begin(), entityIt) : 0;
            }

            std::string recoveryKey = std::to_string(otherSceneId) + "_" + std::to_string(instance.rootEntity);
            recovery[recoveryKey] = {nodeExtend, transformIndex};

            // Remove members from this instance
            for (const Entity& e : allEntities) {
                auto itRem = std::find_if(instance.members.begin(), instance.members.end(),
                    [e](const EntityBundle::EntityMember& member) {
                        return member.localEntity == e;
                    });
                if (itRem != instance.members.end()) {
                    instance.members.erase(itRem);
                }

                instance.overrides.erase(e);
            }

            // Destroy entities in other instances (or in this one if destroyItself)
            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || destroyItself) {
                for (const Entity& entityToDestroy : allEntities) {
                    DeleteEntityCmd::destroyEntity(otherScene->scene, entityToDestroy, otherScene->entities, this, otherSceneId);
                }
            }

            otherScene->isModified = true;
        }

        sceneInstances.erase(
            std::remove_if(sceneInstances.begin(), sceneInstances.end(),
                [](const EntityBundle::Instance& inst) {
                    return inst.members.empty() && inst.rootEntity == NULL_ENTITY;
                }),
            sceneInstances.end()
        );
    }

    for (auto it = bundle->instances.begin(); it != bundle->instances.end(); ) {
        if (it->second.empty()) {
            it = bundle->instances.erase(it);
        } else {
            ++it;
        }
    }

    // Destroy from registry
    std::vector<Entity> registryEntitiesToRemove;
    ProjectUtils::collectEntities(regData, registryEntitiesToRemove);

    for (Entity regEntity : registryEntitiesToRemove) {
        DeleteEntityCmd::destroyEntity(bundle->registry.get(), regEntity, bundle->registryEntities);
    }

    bundle->isModified = true;

    return recovery;
}

bool editor::Project::isEntityInBundle(uint32_t sceneId, Entity entity) const{
    for (const auto& [filepath, bundle] : entityBundles){
        if (bundle.containsEntity(sceneId, entity)) {
            return true;
        }
    }
    return false;
}

void editor::Project::cleanupEntityBundlesForScene(uint32_t sceneId){
    for (auto it = entityBundles.begin(); it != entityBundles.end(); ) {
        it->second.instances.erase(sceneId);
        if (it->second.instances.empty()) {
            it = entityBundles.erase(it);
        } else {
            ++it;
        }
    }
}

bool editor::Project::bundlePropertyChanged(uint32_t sceneId, Entity entity, ComponentType componentType, std::vector<std::string> properties, bool changeItself){
    fs::path filepath = findEntityBundlePathFor(sceneId, entity);

    if (filepath.empty()) {
        Out::error("Entity %u in scene %u is not part of any entity bundle", entity, sceneId);
        return false;
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    uint32_t instanceId = bundle->getInstanceId(sceneId, entity);

    // Bundle roots don't have registry entities; their properties are per-instance
    if (bundle->getRootEntity(sceneId, entity) == entity) {
        return true;
    }

    if (!bundle->hasComponentOverride(sceneId, entity, componentType)){
        Entity registryEntity = bundle->getRegistryEntity(sceneId, entity);
        if (registryEntity == NULL_ENTITY) {
            Out::error("Failed to find registry entity for bundle entity %u in scene %u", entity, sceneId);
            return false;
        }
        EntityRegistry* registry = bundle->registry.get();

        // Build localToRegistry map from the source instance
        std::unordered_map<Entity, Entity> localToRegistry;
        const EntityBundle::Instance* sourceInstance = bundle->getInstanceById(sceneId, instanceId);
        if (sourceInstance) {
            for (const auto& member : sourceInstance->members) {
                localToRegistry[member.localEntity] = member.registryEntity;
            }
        }

        if (properties.size() == 0){
            Catalog::copyComponent(getScene(sceneId)->scene, entity, registry, registryEntity, componentType);
        }else{
            for (const auto& property : properties) {
                Catalog::copyPropertyValue(getScene(sceneId)->scene, entity, registry, registryEntity, componentType, property);
            }
        }
        // Remap entity references from scene-local IDs to registry IDs
        remapEntityPropertiesInComponent(registry, registryEntity, componentType, properties, localToRegistry);

        std::vector<uint32_t> staleBundleScenes;
        for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
            SceneProject* otherScene = getScene(otherSceneId);
            if (!otherScene) {
                Out::error("Failed to find scene %u", otherSceneId);
                staleBundleScenes.push_back(otherSceneId);
                continue;
            }
            if (!otherScene->scene) {
                Out::error("Scene %u is not loaded", otherSceneId);
                staleBundleScenes.push_back(otherSceneId);
                continue;
            }
            for (auto& instance : sceneInstances) {
                if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || changeItself) {
                    Entity otherEntity = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryEntity);

                    if (!bundle->hasComponentOverride(otherSceneId, otherEntity, componentType)) {
                        if (otherScene->isVisible){
                            otherScene->needUpdateRender = true;
                        }
                        // Copy from registry (with correct registry IDs) to target scene
                        if (properties.size() == 0){
                            Catalog::copyComponent(registry, registryEntity, otherScene->scene, otherEntity, componentType);
                        }else{
                            for (const auto& property : properties) {
                                Catalog::copyPropertyValue(registry, registryEntity, otherScene->scene, otherEntity, componentType, property);
                            }
                        }
                        // Remap entity references from registry IDs to target-local IDs
                        std::unordered_map<Entity, Entity> registryToOtherLocal;
                        for (const auto& member : instance.members) {
                            registryToOtherLocal[member.registryEntity] = member.localEntity;
                        }
                        remapEntityPropertiesInComponent(otherScene->scene, otherEntity, componentType, properties, registryToOtherLocal);
                    }
                }
            }
        }
        for (uint32_t staleId : staleBundleScenes) {
            cleanupEntityBundlesForScene(staleId);
        }
    }

    bundle->isModified = true;

    return true;
}

bool editor::Project::bundleNameChanged(uint32_t sceneId, Entity entity, std::string name, bool changeItself){
    fs::path filepath = findEntityBundlePathFor(sceneId, entity);

    if (filepath.empty()) {
        Out::error("Entity %u in scene %u is not part of any entity bundle", entity, sceneId);
        return false;
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    uint32_t instanceId = bundle->getInstanceId(sceneId, entity);

    // Bundle roots don't have registry entities; their name is per-instance
    if (bundle->getRootEntity(sceneId, entity) == entity) {
        return true;
    }

    Entity registryEntity = bundle->getRegistryEntity(sceneId, entity);
    if (registryEntity == NULL_ENTITY) {
        Out::error("Failed to find registry entity for bundle entity %u in scene %u", entity, sceneId);
        return false;
    }
    EntityRegistry* registry = bundle->registry.get();

    registry->setEntityName(registryEntity, name);

    std::vector<uint32_t> staleBundleScenes;
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene) {
            Out::error("Failed to find scene %u", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        if (!otherScene->scene) {
            Out::error("Scene %u is not loaded", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        for (auto& instance : sceneInstances) {
            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || changeItself) {
                Entity otherEntity = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryEntity);

                otherScene->scene->setEntityName(otherEntity, name);
                otherScene->isModified = true;
            }
        }
    }
    for (uint32_t staleId : staleBundleScenes) {
        cleanupEntityBundlesForScene(staleId);
    }

    bundle->isModified = true;

    return true;
}

editor::SharedMoveRecovery editor::Project::moveEntityFromBundle(uint32_t sceneId, Entity entity, Entity target, InsertionType type, bool moveItself){
    fs::path filepath = findEntityBundlePathFor(sceneId, entity);
    if (filepath.empty()) {
        Out::error("Entity %u in scene %u is not part of any entity bundle", entity, sceneId);
        return {};
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    uint32_t instanceId = bundle->getInstanceId(sceneId, entity);

    if (!isEntityInBundle(sceneId, target)){
        if (type == InsertionType::INTO){
            Out::error("Cannot move bundle entity %u into non-bundle target %u in scene %u", entity, target, sceneId);
            return {};
        }

        auto& entities = getScene(sceneId)->entities;
        auto entityIt = std::find(entities.begin(), entities.end(), entity);
        auto targetIt = std::find(entities.begin(), entities.end(), target);

        if (entityIt != entities.end() && targetIt != entities.end()) {
            Entity nextBundle = NULL_ENTITY;

            if (entityIt < targetIt) {
                for (auto it = targetIt - 1; it > entityIt; --it) {
                    if (isEntityInBundle(sceneId, *it)) {
                        nextBundle = *it;
                        break;
                    }
                }
            } else {
                for (auto it = targetIt + 1; it < entityIt; ++it) {
                    if (isEntityInBundle(sceneId, *it)) {
                        nextBundle = *it;
                        break;
                    }
                }
            }

            if (nextBundle != NULL_ENTITY) {
                target = nextBundle;
            }else{
                // Not need to move entity in other scenes and registry if target is not in bundle
                return {};
            }
        }
    }

    Entity registryEntity = bundle->getRegistryEntity(sceneId, entity);
    Entity registryTarget = bundle->getRegistryEntity(sceneId, target);
    if (registryEntity == NULL_ENTITY || registryTarget == NULL_ENTITY) {
        Out::error("Failed to find registry entities for bundle entities %u or %u in scene %u", entity, target, sceneId);
        return {};
    }

    SharedMoveRecovery recovery;

    Entity oldParent;
    size_t oldIndex;
    bool hasTransform;
    ProjectUtils::moveEntityOrderByTarget(bundle->registry.get(), bundle->registryEntities, registryEntity, registryTarget, type, oldParent, oldIndex, hasTransform);
    std::string recoveryDefKey = std::to_string(NULL_PROJECT_SCENE);
    recovery[recoveryDefKey] = {oldParent, oldIndex, hasTransform};

    std::vector<uint32_t> staleBundleScenes;
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene) {
            Out::error("Failed to find scene %u", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        if (!otherScene->scene) {
            Out::error("Scene %u is not loaded", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        for (auto& instance : sceneInstances) {
            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || moveItself) {
                Entity otherEntity = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryEntity);
                Entity otherTarget = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryTarget);

                if (otherEntity != NULL_ENTITY && otherTarget != NULL_ENTITY) {
                    Entity otherOldParent;
                    size_t otherOldIndex;
                    bool otherHasTransform;
                    ProjectUtils::moveEntityOrderByTarget(otherScene->scene, otherScene->entities, otherEntity, otherTarget, type, otherOldParent, otherOldIndex, otherHasTransform);
                    std::string recoveryKey = std::to_string(otherSceneId) + "_" + std::to_string(instance.instanceId);
                    recovery[recoveryKey] = {otherOldParent, otherOldIndex, otherHasTransform};

                    otherScene->isModified = true;
                }
            }
        }
    }
    for (uint32_t staleId : staleBundleScenes) {
        cleanupEntityBundlesForScene(staleId);
    }

    bundle->isModified = true;

    return recovery;
}

bool editor::Project::undoMoveEntityInBundle(uint32_t sceneId, Entity entity, Entity target, const SharedMoveRecovery& recovery, bool moveItself){
    fs::path filepath = findEntityBundlePathFor(sceneId, entity);
    if (filepath.empty()) {
        Out::error("Entity %u in scene %u is not part of any entity bundle", entity, sceneId);
        return false;
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    uint32_t instanceId = bundle->getInstanceId(sceneId, entity);

    Entity registryEntity = bundle->getRegistryEntity(sceneId, entity);
    Entity registryTarget = bundle->getRegistryEntity(sceneId, target);
    if (registryEntity == NULL_ENTITY || registryTarget == NULL_ENTITY) {
        Out::error("Failed to find registry entities for bundle entities %u or %u in scene %u", entity, target, sceneId);
        return false;
    }

    std::string recoveryDefKey = std::to_string(NULL_PROJECT_SCENE);
    if (recovery.find(recoveryDefKey) == recovery.end()) {
        Out::error("No recovery data provided for undoing move of entity %u in scene %u", entity, sceneId);
        return false;
    }
    ProjectUtils::moveEntityOrderByIndex(bundle->registry.get(), bundle->registryEntities, registryEntity, recovery.at(recoveryDefKey).oldParent, recovery.at(recoveryDefKey).oldIndex, recovery.at(recoveryDefKey).hasTransform);

    std::vector<uint32_t> staleBundleScenes;
    for (auto& [otherSceneId, sceneInstances] : bundle->instances) {
        SceneProject* otherScene = getScene(otherSceneId);
        if (!otherScene) {
            Out::error("Failed to find scene %u", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        if (!otherScene->scene) {
            Out::error("Scene %u is not loaded", otherSceneId);
            staleBundleScenes.push_back(otherSceneId);
            continue;
        }
        // Inverting to get correct entity index
        for (auto it = sceneInstances.rbegin(); it != sceneInstances.rend(); ++it) {
            auto& instance = *it;
            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || moveItself) {
                std::string recoveryKey = std::to_string(otherSceneId) + "_" + std::to_string(instance.instanceId);
                if (recovery.find(recoveryKey) != recovery.end()) {
                    Entity otherEntity = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryEntity);
                    Entity otherTarget = bundle->getLocalEntity(otherSceneId, instance.instanceId, registryTarget);
                    if (otherEntity != NULL_ENTITY && otherTarget != NULL_ENTITY) {
                        ProjectUtils::moveEntityOrderByIndex(otherScene->scene, otherScene->entities, otherEntity, recovery.at(recoveryKey).oldParent, recovery.at(recoveryKey).oldIndex, recovery.at(recoveryKey).hasTransform);
                    }
                }
            }
        }
    }
    for (uint32_t staleId : staleBundleScenes) {
        cleanupEntityBundlesForScene(staleId);
    }

    bundle->isModified = true;

    return true;
}

void editor::Project::collectInvolvedScenes(uint32_t sceneId, std::vector<uint32_t>& involvedSceneIds) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) return;

    // Avoid duplicates if the scene is already in the list
    if (std::find(involvedSceneIds.begin(), involvedSceneIds.end(), sceneId) == involvedSceneIds.end()) {
        involvedSceneIds.push_back(sceneId);
    }

    for (uint32_t childId : sceneProject->childScenes) {
        collectInvolvedScenes(childId, involvedSceneIds);
    }
}

bool editor::Project::isAnyScenePlaying() const{
    {
        std::scoped_lock lock(playSessionMutex);
        if (activePlaySession) {
            return true;
        }
    }

    for (const auto& sceneProject : scenes) {
        if (sceneProject.playState != ScenePlayState::STOPPED) {
            return true;
        }
    }
    return false;
}

void editor::Project::registerSceneManager() {
    SceneManager::clearAll();
    for (SceneProject& sceneProject : scenes) {
        SceneManager::registerScene(sceneProject.id, sceneProject.name, [this, sceneId = sceneProject.id]() {
            std::shared_ptr<PlaySession> session;
            {
                std::scoped_lock lock(playSessionMutex);
                session = activePlaySession;
            }
            if (!session || session->cancelled.load(std::memory_order_acquire)) return;

            std::vector<uint32_t> involvedSceneIds;
            collectInvolvedScenes(sceneId, involvedSceneIds);

            std::vector<size_t> currentStackIndices;

            for (uint32_t invSceneId : involvedSceneIds) {
                size_t entryIndex = (size_t)-1;
                {
                    std::scoped_lock lock(playSessionMutex);
                    auto it = std::find_if(session->runtimeScenes.begin(), session->runtimeScenes.end(),
                        [invSceneId](const PlayRuntimeScene& entry) {
                            return entry.sourceSceneId == invSceneId;
                        });

                    if (it != session->runtimeScenes.end()) {
                        entryIndex = std::distance(session->runtimeScenes.begin(), it);
                    } else {
                        SceneProject* sourceScene = getScene(invSceneId);
                        if (sourceScene) {
                            PlayRuntimeScene newEntry;
                            newEntry.sourceSceneId = invSceneId;
                            newEntry.runtime = createRuntimeCloneFromSource(sourceScene);
                            newEntry.ownedRuntime = true;

                            if (newEntry.runtime) {
                                session->runtimeScenes.push_back(newEntry);
                                entryIndex = session->runtimeScenes.size() - 1;
                            }
                        }
                    }
                }

                if (entryIndex != (size_t)-1) {
                    currentStackIndices.push_back(entryIndex);
                }
            }

            // Register all runtime scenes so cross-scene entity references can resolve
            for (size_t entryIndex : currentStackIndices) {
                PlayRuntimeScene& entry = session->runtimeScenes[entryIndex];
                if (entry.runtime && entry.runtime->scene) {
                    SceneManager::setScenePtr(entry.sourceSceneId, entry.runtime->scene);
                }
            }

            for (size_t entryIndex : currentStackIndices) {
                PlayRuntimeScene& entry = session->runtimeScenes[entryIndex];
                if (entry.initialized) return;

                if (conector.isLibraryConnected()) {
                    conector.init(entry.runtime->scene);
                }else{
                    LuaBinding::initializeLuaScripts(entry.runtime->scene);
                }

                prepareRuntimeScene(entry);

                if (entry.sourceSceneId == sceneId) {
                    Engine::setScene(entry.runtime->scene);
                } else {
                    Engine::addSceneLayer(entry.runtime->scene);
                }
            }

            // Cleanup scripts for scenes no longer in the current stack
            {
                std::scoped_lock lock(playSessionMutex);
                for (auto& entry : session->runtimeScenes) {
                    if (!entry.initialized) continue;

                    bool inCurrentStack = false;
                    for (size_t idx : currentStackIndices) {
                        if (&session->runtimeScenes[idx] == &entry) {
                            inCurrentStack = true;
                            break;
                        }
                    }

                    if (!inCurrentStack) {
                        if (conector.isLibraryConnected()) {
                            conector.cleanup(entry.runtime->scene);
                        } else {
                            LuaBinding::cleanupLuaScripts(entry.runtime->scene);
                        }
                        entry.initialized = false;
                    }
                }
            }
        });
    }
}

// Safe: only called from BundleManager lambdas on the game thread (pauseGameEvents=false);
// editor thread won't mutate scenes or runtimeScenes while game events are active.
editor::SceneProject* editor::Project::findSceneProjectByScene(Scene* scene) {
    for (auto& sp : scenes) {
        if (sp.scene == scene) return &sp;
    }
    std::scoped_lock lock(playSessionMutex);
    if (activePlaySession) {
        for (auto& entry : activePlaySession->runtimeScenes) {
            if (entry.runtime && entry.runtime->scene == scene) {
                return entry.runtime;
            }
        }
    }
    return nullptr;
}

void editor::Project::registerBundleManager() {
    BundleManager::clearAll();
    uint32_t bundleId = 0;
    for (const auto& [bundlePath, bundle] : entityBundles) {
        bundleId++;
        fs::path noExt = bundlePath;
        noExt.replace_extension();
        std::string bundleName = noExt.generic_string();

        // Determine if root needs Transform based on bundle's top-level entities
        bool hasTopLevelTransform = false;
        if (bundle.registry) {
            std::vector<Entity> topLevelEntities = getTopLevelEntities(bundle.registry.get(), bundle.registryEntities);
            for (Entity topLevelEntity : topLevelEntities) {
                if (bundle.registry->getSignature(topLevelEntity).test(bundle.registry->getComponentId<Transform>())) {
                    hasTopLevelTransform = true;
                    break;
                }
            }
        }

        BundleManager::registerBundle(bundleId, bundleName,
            // Factory: create bundle instance via importEntityBundle
            [this, bundlePath, bundleName, hasTopLevelTransform](Scene* scene, Entity root) {
                SceneProject* sceneProject = findSceneProjectByScene(scene);
                if (!sceneProject) return;

                // Set up root entity (matching createEntityBundle pattern)
                scene->setEntityName(root, bundlePath.stem().string());

                BundleComponent bundleComp;
                bundleComp.name = bundlePath.stem().string();
                bundleComp.path = bundlePath.string();
                scene->addComponent<BundleComponent>(root, bundleComp);

                if (hasTopLevelTransform) {
                    scene->addComponent<Transform>(root, {});
                }

                sceneProject->entities.push_back(root);

                importEntityBundle(sceneProject, &sceneProject->entities, bundlePath, root, false);
            },
            // Destroyer: remove bundle instance via unimportEntityBundle
            [this, bundlePath](Scene* scene, Entity root) -> bool {
                SceneProject* sceneProject = findSceneProjectByScene(scene);
                if (!sceneProject) return false;

                EntityBundle* bundle = getEntityBundle(bundlePath);
                if (!bundle) return false;

                auto sceneIt = bundle->instances.find(sceneProject->id);
                if (sceneIt == bundle->instances.end()) return false;

                for (const auto& instance : sceneIt->second) {
                    if (instance.rootEntity == root) {
                        std::vector<Entity> members;
                        for (const auto& m : instance.members) {
                            members.push_back(m.localEntity);
                        }
                        bool wasModified = sceneProject->isModified;
                        bool result = unimportEntityBundle(sceneProject->id, bundlePath, root, members);
                        sceneProject->isModified = wasModified;
                        return result;
                    }
                }
                return false;
            }
        );
    }
}

void editor::Project::start(uint32_t sceneId) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Failed to find scene %u to start", sceneId);
        return;
    }

    {
        std::scoped_lock lock(playSessionMutex);
        if (activePlaySession) {
            Out::warning("Cannot start: a play session is already active");
            return;
        }
    }

    doriax::FunctionSubscribeGlobal::getCrashHandler() = 
        [this, sceneId](const std::string& tag, const std::string& errorInfo) {
            // Log the scene and entity context
            SceneProject* sceneProject = getScene(sceneId);
            std::string sceneName = sceneProject ? sceneProject->name : "Unknown";

            Out::error("Script crash in scene '%s' (ID: %u)\nLocation: %s\nError: %s", sceneName.c_str(), sceneId, tag.c_str(), errorInfo.c_str());

            // 1. Pause immediately.
            // This sets a flag to prevent the Engine from starting the NEXT frame update.
            pause(sceneId);

            // 2. Stop asynchronously.
            // We use a background thread to wait for the Main Thread to finish the CURRENT frame
            // and unwind the stack. Destroying the scene (via stop) while the Main Thread 
            // is still executing code inside it will cause a Segmentation Fault.
            std::thread([this, sceneId]() {
                // Heuristic delay to allow the stack to unwind safely
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                stop(sceneId);
            }).detach();
        };

    auto session = std::make_shared<PlaySession>();
    session->mainSceneId = sceneId;

    std::vector<uint32_t> involvedMainSceneIds;
    collectInvolvedScenes(sceneId, involvedMainSceneIds);

    std::vector<editor::SceneBuildInfo> scenesToGenerate;
    for (SceneProject& sceneProject : scenes) {
        bool savedNow = false;
        bool isInvolvedScene = std::find(involvedMainSceneIds.begin(), involvedMainSceneIds.end(), sceneProject.id) != involvedMainSceneIds.end();

        if (sceneProject.opened || (isInvolvedScene && sceneProject.scene)) {
            updateAllScriptsProperties(sceneProject.id);

            if (sceneProject.isModified && !sceneProject.filepath.empty()) {
                saveScene(sceneProject.id);
                savedNow = true;
            } else {
                updateSceneCppScripts(&sceneProject);
                updateSceneBundles(&sceneProject);
            }
        }

        // Just create runtime for main scene and its children
        if (isInvolvedScene) {
            PlayRuntimeScene entry;
            entry.sourceSceneId = sceneProject.id;

            if (sceneProject.id == sceneId) {
                // Main scene plays in-place
                entry.runtime = &sceneProject;
                entry.ownedRuntime = false;
                //entry.manageSourceState = true;
            } else {
                // Child scene: clone for runtime to keep standalone tab independent
                entry.runtime = createRuntimeCloneFromSource(&sceneProject);
                entry.ownedRuntime = true;
                //entry.manageSourceState = false;
            }

            if (!entry.runtime) {
                Log::error("Failed to create runtime for scene %u", sceneProject.id);
                continue;
            }

            if (!savedNow) {
                std::vector<BundleInstanceInfo> bundleInstances = generator.writeBundleSources(entityBundles, sceneId, getProjectPath(),getProjectInternalPath());
                generator.writeSceneSource(entry.runtime->scene, entry.runtime->name, entry.runtime->entities, getSceneCamera(entry.runtime), getProjectPath(), getProjectInternalPath(), bundleInstances);
            }

            session->runtimeScenes.push_back(entry);
        }

        bool isMain = (sceneId == sceneProject.id);
        std::vector<uint32_t> involvedSceneIds;
        collectInvolvedScenes(sceneProject.id, involvedSceneIds);
        scenesToGenerate.push_back({sceneProject.id, sceneProject.name, involvedSceneIds, isMain});
    }

    {
        std::scoped_lock lock(playSessionMutex);
        activePlaySession = session;
    }

    registerSceneManager();
    registerBundleManager();

    // Save current scene state before starting
    sceneProject->playStateSnapshot = Stream::encodeSceneProject(nullptr, sceneProject);
    sceneProject->playState = ScenePlayState::PLAYING;

    Backend::getApp().getCodeEditor()->saveAll();

    std::vector<SceneScriptSource> mergedCppScripts = collectAllSceneCppScripts();
    std::vector<BundleSceneInfo> bundleBuildInfos = collectAllBundles();
    generator.configure(scenesToGenerate, libName, mergedCppScripts, bundleBuildInfos, getProjectPath(), getProjectInternalPath(), scalingMode, textureStrategy, canvasWidth, canvasHeight);

    // Check if we have C++ scripts that need building
    bool hasCppScripts = !mergedCppScripts.empty();

    if (hasCppScripts) {
        std::string missingTools = Generator::checkBuildTools();
        if (!missingTools.empty()) {
            Out::error("Cannot build C++ scripts: missing build tools");
            Backend::getApp().registerAlert("Missing Build Tools",
                "C++ scripts require build tools that were not found on your system:\n\n" + missingTools +
                "\nPlease install the missing tools and try again.");
            cleanupPlaySession(session);
            {
                std::scoped_lock lock(playSessionMutex);
                if (activePlaySession == session) activePlaySession.reset();
            }
            sceneProject->playState = ScenePlayState::STOPPED;
            return;
        }

        fs::path buildPath = getProjectInternalPath() / "build";

        generator.build(getProjectPath(), getProjectInternalPath(), buildPath, cmakeCCompiler, cmakeCxxCompiler, cmakeGenerator);

        std::thread connectThread([this, session, sceneId, buildPath]() {
            generator.waitForBuildToComplete();

            if (session->cancelled.load(std::memory_order_acquire)) {
                session->startupThreadDone.store(true, std::memory_order_release);
                return;
            }

            SceneProject* mainSceneProject = getScene(sceneId);

            if (!generator.didLastBuildSucceed()) {
                cleanupPlaySession(session);
                {
                    std::scoped_lock lock(playSessionMutex);
                    if (activePlaySession == session) activePlaySession.reset();
                }
                mainSceneProject->playState = ScenePlayState::STOPPED;
                session->startupThreadDone.store(true, std::memory_order_release);
                return;
            }

            if (session->cancelled.load(std::memory_order_acquire)) {
                session->startupThreadDone.store(true, std::memory_order_release);
                return;
            }

            if (conector.connect(buildPath, libName)) {
                // Register all runtime scenes so cross-scene entity references can resolve
                for (const auto& entry : session->runtimeScenes) {
                    if (entry.runtime && entry.runtime->scene) {
                        SceneManager::setScenePtr(entry.sourceSceneId, entry.runtime->scene);
                    }
                }

                for (const auto& entry : session->runtimeScenes) {
                    if (entry.runtime){
                        conector.init(entry.runtime->scene);
                    }
                }

                if (session->cancelled.load(std::memory_order_acquire)) {
                    session->startupThreadDone.store(true, std::memory_order_release);
                    return;
                }

                // Enqueue scene finalization to the main/GL thread so that
                // engine, scene, and render state is accessed with a valid GL context.
                Backend::getApp().enqueueMainThreadTask([this, session, sceneId]() {
                    SceneProject* msp = getScene(sceneId);
                    finalizeStart(msp, session->runtimeScenes);
                    session->startupSucceeded.store(true, std::memory_order_release);
                    session->startupThreadDone.store(true, std::memory_order_release);
                });
            } else {
                Out::error("Failed to connect to library");
                cleanupPlaySession(session);
                {
                    std::scoped_lock lock(playSessionMutex);
                    if (activePlaySession == session) activePlaySession.reset();
                }
                mainSceneProject->playState = ScenePlayState::STOPPED;
                session->startupThreadDone.store(true, std::memory_order_release);
            }
        });
        connectThread.detach();
    } else {
        // No C++ scripts, just initialize Lua scripts directly
        // Register all runtime scenes so cross-scene entity references can resolve
        for (const auto& entry : session->runtimeScenes) {
            if (entry.runtime && entry.runtime->scene) {
                SceneManager::setScenePtr(entry.sourceSceneId, entry.runtime->scene);
            }
        }

        for (const auto& entry : session->runtimeScenes) {
            if (entry.runtime){
                LuaBinding::initializeLuaScripts(entry.runtime->scene);
            }
        }

        SceneProject* mainSceneProject = getScene(sceneId);

        finalizeStart(mainSceneProject, session->runtimeScenes);
        session->startupSucceeded.store(true, std::memory_order_release);
        session->startupThreadDone.store(true, std::memory_order_release);
    }
}

void editor::Project::pause(uint32_t sceneId) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Failed to find scene %u to pause", sceneId);
        return;
    }

    std::shared_ptr<PlaySession> session;
    {
        std::scoped_lock lock(playSessionMutex);
        session = activePlaySession;
    }

    if (session && session->mainSceneId == sceneId) {
        if (sceneProject->playState == ScenePlayState::PLAYING) {
            Engine::onPause.call();

            std::vector<PlayRuntimeScene> runtimeScenesCopy;
            {
                std::scoped_lock lock(playSessionMutex);
                runtimeScenesCopy = session->runtimeScenes;
            }

            for (const auto& entry : runtimeScenesCopy) {
                if (!entry.runtime) continue;
                pauseEngineScene(entry.runtime->scene, true);
            }
            Engine::pauseGameEvents(true);
            sceneProject->playState = ScenePlayState::PAUSED;
        }
        return;
    }

    Engine::onPause.call();
}

void editor::Project::resume(uint32_t sceneId) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Failed to find scene %u to resume", sceneId);
        return;
    }

    std::shared_ptr<PlaySession> session;
    {
        std::scoped_lock lock(playSessionMutex);
        session = activePlaySession;
    }

    if (session && session->mainSceneId == sceneId) {
        if (sceneProject->playState == ScenePlayState::PAUSED) {
            Engine::onResume.call();

            std::vector<PlayRuntimeScene> runtimeScenesCopy;
            {
                std::scoped_lock lock(playSessionMutex);
                runtimeScenesCopy = session->runtimeScenes;
            }

            for (const auto& entry : runtimeScenesCopy) {
                if (!entry.runtime) continue;
                pauseEngineScene(entry.runtime->scene, false);
            }
            Engine::pauseGameEvents(false);
            sceneProject->playState = ScenePlayState::PLAYING;
        }
        return;
    }

    Engine::onResume.call();
}

void editor::Project::stop(uint32_t sceneId) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Failed to find scene %u to stop", sceneId);
        return;
    }

    std::shared_ptr<PlaySession> session;
    {
        std::scoped_lock lock(playSessionMutex);
        session = activePlaySession;
    }

    if (!session) {
        Out::warning("No active play session for scene %u", sceneId);
        return;
    }

    if (session->mainSceneId != sceneId) {
        Out::warning("Scene %u is not the main scene of the active play session", sceneId);
        return;
    }

    session->cancelled.store(true, std::memory_order_release);
    sceneProject->playState = ScenePlayState::CANCELLING;

    //Should be called before conector disconnects to allow scripts to receive the event
    Engine::onViewDestroyed.call();
    Engine::onShutdown.call();
    Engine::pauseGameEvents(true);

    Engine::clearAllSubscriptions(true);

    // Clear crash handler when stopping
    doriax::FunctionSubscribeGlobal::getCrashHandler() = nullptr;

    // Check if we have C++ library connected
    bool hasLibraryConnected = conector.isLibraryConnected();
    // Request cancellation asynchronously (returns a future we can wait on later if needed)
    auto cancelFuture = generator.cancelBuild();

    std::vector<PlayRuntimeScene> runtimeScenesCopy;
    {
        std::scoped_lock lock(playSessionMutex);
        runtimeScenesCopy = session->runtimeScenes;
    }

    if (hasLibraryConnected) {
        // Cleanup script instances / disconnect if the library is currently connected.
        for (const auto& entry : runtimeScenesCopy) {
            if (entry.runtime) {
                conector.cleanup(entry.runtime->scene);
            }
        }
        conector.disconnect();

        // After cancellation completes perform the rest of the stop work on a background thread
        std::thread finalizeStopThread([this, session, sceneId, cancelFuture = std::move(cancelFuture), runtimeScenesCopy]() mutable {
            if (cancelFuture.valid()) {
                // wait for cancellation to finish
                cancelFuture.wait();
            }

            generator.waitForBuildToComplete();

            // Wait for the connect/startup thread to finish to avoid races
            while (!session->startupThreadDone.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Enqueue scene restoration to the main/GL thread so that
            // texture and image creation happens with a valid GL context.
            Backend::getApp().enqueueMainThreadTask([this, session, sceneId, runtimeScenesCopy]() {
                SceneProject* mainSceneProject = getScene(sceneId);
                if (session->startupSucceeded.load(std::memory_order_acquire)) {
                    finalizeStop(mainSceneProject, runtimeScenesCopy);
                } else {
                    // Build was cancelled before startup succeeded, reset playState manually
                    if (mainSceneProject) {
                        mainSceneProject->playState = ScenePlayState::STOPPED;
                    }
                }
                {
                    std::scoped_lock lock(playSessionMutex);
                    if (activePlaySession == session) activePlaySession.reset();
                }
            });
        });
        finalizeStopThread.detach();
    } else {
        Backend::getApp().enqueueMainThreadTask([this, session, sceneId, runtimeScenesCopy]() {
            for (const auto& entry : runtimeScenesCopy) {
                if (entry.runtime) {
                    LuaBinding::cleanupLuaScripts(entry.runtime->scene);
                }
            }

            // No C++ library connected, just finalize directly
            SceneProject* mainSceneProject = getScene(sceneId);
            if (session->startupSucceeded.load(std::memory_order_acquire)) {
                finalizeStop(mainSceneProject, runtimeScenesCopy);
            } else {
                // Build was cancelled before startup succeeded, reset playState manually
                if (mainSceneProject) {
                    mainSceneProject->playState = ScenePlayState::STOPPED;
                }
            }
            {
                std::scoped_lock lock(playSessionMutex);
                if (activePlaySession == session) activePlaySession.reset();
            }
        });
    }
}

void editor::Project::waitForPlaySessionToFinish() {
    // Wait for the detached finalizeStop thread to complete
    // so that all cleanup finishes before the app tears down.
    // Also pump the main-thread task queue, because finalizeStop
    // is now enqueued there and the caller (closeWindow) is on
    // the main thread — without pumping we would deadlock.
    while (true) {
        {
            std::scoped_lock lock(playSessionMutex);
            if (!activePlaySession) {
                return;
            }
        }
        Backend::getApp().processMainThreadTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void editor::Project::restoreRuntimeLayers(uint32_t sceneId) {
    std::scoped_lock lock(playSessionMutex);
    if (!activePlaySession || activePlaySession->mainSceneId != sceneId) {
        return;
    }

    if (!activePlaySession->startupSucceeded.load(std::memory_order_acquire)) {
        return;
    }

    for (const auto& entry : activePlaySession->runtimeScenes) {
        SceneProject* runtimeProject = entry.runtime;
        if (runtimeProject && runtimeProject->scene) {
             if (entry.sourceSceneId != sceneId) {
                 Engine::addSceneLayer(runtimeProject->scene);
             }
        }
    }
}

void editor::Project::debugSceneHierarchy(){
    if (SceneProject* sceneProject = getSelectedScene()){
        printf("Debug scene: %s\n", sceneProject->name.c_str());
        auto transforms = sceneProject->scene->getComponentArray<Transform>();
        for (int i = 0; i < transforms->size(); i++){
            auto transform = transforms->getComponentFromIndex(i);
            printf("Transform %i - Entity: %i - Parent: %i: %s\n", i, transforms->getEntity(i), transform.parent, sceneProject->scene->getEntityName(transforms->getEntity(i)).c_str());
        }
        printf("\n");
    }
}