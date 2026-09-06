// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "Project.h"
#include "Factory.h"

#include "EditorHost.h"
#include "util/FileUtils.h"
#include "window/CodeEditor.h"
#include "window/ImageViewerWindow.h"
#include "window/TerrainEditWindow.h"

#include <cmath>
#include <fstream>
#include <system_error>
#include <chrono>
#include <unordered_set>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <thread>

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
#include "util/CameraTextureLink.h"
#include "util/ProjectUtils.h"
#include "util/Util.h"

#include "texture/Texture.h"
#include "Engine.h"
#include "pool/ShaderPool.h"
#include "shader/ShaderBuilder.h"
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

// Translate a local entity through the supplied map. Bundle-boundary remaps clear values
// outside the map; recovery remaps leave those unrelated values untouched.
static bool remapLocalEntity(Entity& value, const std::unordered_map<Entity, Entity>& entityMap, bool clearUnmapped) {
    if (value == NULL_ENTITY) {
        return false;
    }
    auto it = entityMap.find(value);
    if (it == entityMap.end() && !clearUnmapped) {
        return false;
    }
    Entity remapped = (it != entityMap.end()) ? it->second : NULL_ENTITY;
    if (remapped == value) {
        return false;
    }
    value = remapped;
    return true;
}

// Remap one entity-typed property in place. Cross-scene references are cleared only when
// crossing a bundle boundary; recovery leaves them untouched.
static bool remapEntityRef(const editor::PropertyData& property, const std::unordered_map<Entity, Entity>& entityMap, bool clearUnmapped) {
    if (!property.ref) {
        return false;
    }
    if (property.type == editor::PropertyType::EntityReference) {
        EntityReference* ref = static_cast<EntityReference*>(property.ref);
        if (ref->sceneId != 0) {
            if (!clearUnmapped) {
                return false;
            }
            if (ref->entity == NULL_ENTITY) {
                ref->sceneId = 0;
                return false;
            }
            ref->entity = NULL_ENTITY;
            ref->sceneId = 0;
            return true;
        }
        return remapLocalEntity(ref->entity, entityMap, clearUnmapped);
    }
    if (property.type == editor::PropertyType::Entity) {
        return remapLocalEntity(*static_cast<Entity*>(property.ref), entityMap, clearUnmapped);
    }
    return false;
}

// A requested property matches its own name and, for an aggregate property such as
// "scripts", any indexed field expanded from it (e.g. "scripts[0].target"). This
// lets a whole-"scripts" update still reach the nested entity references it holds.
static bool propertyRequested(const std::string& propertyName, const std::vector<std::string>& requested) {
    for (const std::string& req : requested) {
        if (propertyName == req) {
            return true;
        }
        if (propertyName.size() > req.size() && propertyName.compare(0, req.size(), req) == 0) {
            char next = propertyName[req.size()];
            if (next == '[' || next == '.') {
                return true;
            }
        }
    }
    return false;
}

static bool scriptPropertyFloatEqual(float lhs, float rhs, bool useTolerance) {
    if (lhs == rhs) return true;
    if (std::isnan(lhs) || std::isnan(rhs)) return std::isnan(lhs) && std::isnan(rhs);
    return useTolerance && std::fabs(lhs - rhs) <= 1e-4f;
}

// A matching index only means both sides hold the same alternative, not that it is the
// one type asks for, so a leftover pair compares unequal instead of throwing
static bool scriptPropertyValuesEqual(ScriptPropertyType type, const ScriptPropertyValue& lhs, const ScriptPropertyValue& rhs, bool useTolerance = false) {
    if (lhs.index() != rhs.index()) {
        return false;
    }

    switch (type) {
        case ScriptPropertyType::Bool: {
            const bool* a = std::get_if<bool>(&lhs);
            const bool* b = std::get_if<bool>(&rhs);
            return a && b && *a == *b;
        }
        case ScriptPropertyType::Int: {
            const int* a = std::get_if<int>(&lhs);
            const int* b = std::get_if<int>(&rhs);
            return a && b && *a == *b;
        }
        case ScriptPropertyType::Float: {
            const float* a = std::get_if<float>(&lhs);
            const float* b = std::get_if<float>(&rhs);
            return a && b && scriptPropertyFloatEqual(*a, *b, false);
        }
        case ScriptPropertyType::String: {
            const std::string* a = std::get_if<std::string>(&lhs);
            const std::string* b = std::get_if<std::string>(&rhs);
            return a && b && *a == *b;
        }
        case ScriptPropertyType::Vector2: {
            const Vector2* a = std::get_if<Vector2>(&lhs);
            const Vector2* b = std::get_if<Vector2>(&rhs);
            return a && b &&
                scriptPropertyFloatEqual(a->x, b->x, useTolerance) &&
                scriptPropertyFloatEqual(a->y, b->y, useTolerance);
        }
        case ScriptPropertyType::Vector3:
        case ScriptPropertyType::Color3: {
            const Vector3* a = std::get_if<Vector3>(&lhs);
            const Vector3* b = std::get_if<Vector3>(&rhs);
            return a && b &&
                scriptPropertyFloatEqual(a->x, b->x, useTolerance) &&
                scriptPropertyFloatEqual(a->y, b->y, useTolerance) &&
                scriptPropertyFloatEqual(a->z, b->z, useTolerance);
        }
        case ScriptPropertyType::Vector4:
        case ScriptPropertyType::Color4: {
            const Vector4* a = std::get_if<Vector4>(&lhs);
            const Vector4* b = std::get_if<Vector4>(&rhs);
            return a && b &&
                scriptPropertyFloatEqual(a->x, b->x, useTolerance) &&
                scriptPropertyFloatEqual(a->y, b->y, useTolerance) &&
                scriptPropertyFloatEqual(a->z, b->z, useTolerance) &&
                scriptPropertyFloatEqual(a->w, b->w, useTolerance);
        }
        case ScriptPropertyType::EntityReference: {
            const EntityReference* a = std::get_if<EntityReference>(&lhs);
            const EntityReference* b = std::get_if<EntityReference>(&rhs);
            return a && b && a->entity == b->entity;
        }
    }

    return false;
}

static bool mergeScriptProperties(std::vector<ScriptProperty>& properties, const std::vector<ScriptProperty>& parsedProperties) {
    bool hasChanges = properties.size() != parsedProperties.size();
    std::vector<ScriptProperty> mergedProperties;
    mergedProperties.reserve(parsedProperties.size());

    for (size_t parsedIndex = 0; parsedIndex < parsedProperties.size(); ++parsedIndex) {
        const ScriptProperty& parsedProp = parsedProperties[parsedIndex];
        auto currentIt = std::find_if(properties.begin(), properties.end(),
            [&](const ScriptProperty& current) { return current.name == parsedProp.name; });

        if (currentIt == properties.end()) {
            hasChanges = true;
            mergedProperties.push_back(parsedProp);
            continue;
        }

        const size_t currentIndex = static_cast<size_t>(currentIt - properties.begin());
        const bool sameType = currentIt->type == parsedProp.type;
        const bool defaultChanged = sameType &&
            !scriptPropertyValuesEqual(currentIt->type, currentIt->defaultValue, parsedProp.defaultValue);
        const bool updateValue = !sameType || (defaultChanged &&
            scriptPropertyValuesEqual(currentIt->type, currentIt->value, currentIt->defaultValue, true));

        if (currentIndex != parsedIndex ||
            currentIt->displayName != parsedProp.displayName ||
            !sameType ||
            currentIt->ptrTypeName != parsedProp.ptrTypeName ||
            defaultChanged) {
            hasChanges = true;
        }

        ScriptProperty merged = *currentIt;
        merged.displayName = parsedProp.displayName;
        merged.type = parsedProp.type;
        merged.ptrTypeName = parsedProp.ptrTypeName;
        merged.defaultValue = parsedProp.defaultValue;

        if (updateValue) {
            merged.value = parsedProp.defaultValue;
        }

        mergedProperties.push_back(std::move(merged));
    }

    properties = std::move(mergedProperties);
    return hasChanges;
}

static std::vector<editor::ScriptPropertyInfo> toScriptPropertyInfos(const std::vector<ScriptProperty>& properties) {
    std::vector<editor::ScriptPropertyInfo> infos;
    infos.reserve(properties.size());

    for (const auto& prop : properties) {
        editor::ScriptPropertyInfo info;
        info.name = prop.name;
        info.isPtr = (prop.type == ScriptPropertyType::EntityReference) || !prop.ptrTypeName.empty();
        info.ptrTypeName = prop.ptrTypeName;
        infos.push_back(std::move(info));
    }

    return infos;
}

// True when a component holds an entity reference pointing outside the bundle
// instance: a cross-scene reference, or a local entity that is not one of the
// instance's members. Such references cannot be shared through the registry.
static bool componentHasExternalEntityRef(EntityRegistry* registry, Entity entity, editor::ComponentType componentType, const std::unordered_set<Entity>& memberLocals) {
    for (auto& [propertyName, property] : editor::Catalog::findEntityProperties(registry, entity, componentType)) {
        if (!property.ref) {
            continue;
        }
        Entity target = NULL_ENTITY;
        if (property.type == editor::PropertyType::EntityReference) {
            EntityReference* ref = static_cast<EntityReference*>(property.ref);
            if (ref->sceneId != 0) {
                return true; // cross-scene reference
            }
            target = ref->entity;
        } else if (property.type == editor::PropertyType::Entity) {
            target = *static_cast<Entity*>(property.ref);
        } else {
            continue;
        }
        if (target != NULL_ENTITY && !memberLocals.count(target)) {
            return true;
        }
    }
    return false;
}

void editor::Project::remapEntityProperties(EntityRegistry* registry, const std::vector<Entity>& entities, const std::unordered_map<Entity, Entity>& entityMap, bool clearUnmapped) {
    if (!clearUnmapped && entityMap.empty()) {
        return;
    }
    for (Entity entity : entities) {
        for (ComponentType componentType : Catalog::findComponents(registry, entity)) {
            uint64_t updateFlags = 0;

            for (auto& [propertyName, property] : Catalog::findEntityProperties(registry, entity, componentType)) {
                if (remapEntityRef(property, entityMap, clearUnmapped)) {
                    updateFlags |= property.updateFlags;
                }
            }

            if (updateFlags != 0) {
                Catalog::updateEntity(registry, entity, updateFlags);
            }
        }
    }
}

void editor::Project::remapEntityPropertiesInComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, const std::vector<std::string>& properties, const std::unordered_map<Entity, Entity>& entityMap) {
    for (auto& [propertyName, property] : Catalog::findEntityProperties(registry, entity, componentType)) {
        // If specific properties were requested, only remap those.
        if (!properties.empty() && !propertyRequested(propertyName, properties)) {
            continue;
        }

        remapEntityRef(property, entityMap, true);
    }
}

void editor::Project::overrideExternalRefComponents(EntityRegistry* scene, EntityBundle::Instance& instance) {
    std::unordered_set<Entity> memberLocals;
    for (const auto& member : instance.members) {
        memberLocals.insert(member.localEntity);
    }

    // An entity reference pointing outside the bundle is per-instance data that cannot
    // be shared through the registry. Promote each component that holds one to a
    // per-instance override so the value is kept on this instance and persisted.
    for (const auto& member : instance.members) {
        for (ComponentType componentType : Catalog::findComponents(scene, member.localEntity)) {
            if (componentHasExternalEntityRef(scene, member.localEntity, componentType, memberLocals)) {
                instance.overrides[member.localEntity] |= 1ULL << static_cast<int>(componentType);
            }
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

std::vector<editor::ChildSceneRef>::iterator editor::Project::findChildScene(std::vector<ChildSceneRef>& childScenes, uint32_t childSceneId) {
    return std::find_if(childScenes.begin(), childScenes.end(),
        [childSceneId](const ChildSceneRef& childScene) {
            return childScene.id == childSceneId;
        });
}

std::vector<editor::ChildSceneRef>::const_iterator editor::Project::findChildScene(const std::vector<ChildSceneRef>& childScenes, uint32_t childSceneId) {
    return std::find_if(childScenes.begin(), childScenes.end(),
        [childSceneId](const ChildSceneRef& childScene) {
            return childScene.id == childSceneId;
        });
}

bool editor::Project::eraseChildSceneReference(std::vector<ChildSceneRef>& childScenes, uint32_t childSceneId) {
    auto it = std::remove_if(childScenes.begin(), childScenes.end(),
        [childSceneId](const ChildSceneRef& childScene) {
            return childScene.id == childSceneId;
        });
    if (it == childScenes.end()) {
        return false;
    }

    childScenes.erase(it, childScenes.end());
    return true;
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

bool editor::Project::remapScriptPathsInRegistry(EntityRegistry* registry, const fs::path& oldPath,
                                                 const fs::path& newPath) {
    if (!registry) {
        return false;
    }

    const fs::path oldRelative = normalizeToProjectRelative(oldPath);
    const fs::path newRelative = normalizeToProjectRelative(newPath);
    const fs::path oldLuaRelative = normalizeToLuaRelative(oldPath);
    const fs::path newLuaRelative = normalizeToLuaRelative(newPath);

    auto scriptsArray = registry->getComponentArray<ScriptComponent>();
    bool changed = false;

    for (size_t i = 0; i < scriptsArray->size(); ++i) {
        ScriptComponent& scriptComponent = scriptsArray->getComponentFromIndex(i);
        for (auto& scriptEntry : scriptComponent.scripts) {
            const bool isLua = scriptEntry.type == ScriptType::LUA;
            changed |= remapScriptEntryPaths(scriptEntry,
                                             isLua ? oldLuaRelative : oldRelative,
                                             isLua ? newLuaRelative : newRelative);
        }
    }

    return changed;
}

bool editor::Project::cleanupScriptPathsInRegistry(EntityRegistry* registry, const fs::path& deletedPath) {
    if (!registry) {
        return false;
    }

    const fs::path deletedRelative = normalizeToProjectRelative(deletedPath);
    const fs::path deletedLuaRelative = normalizeToLuaRelative(deletedPath);

    auto scriptsArray = registry->getComponentArray<ScriptComponent>();
    bool changed = false;

    for (size_t i = 0; i < scriptsArray->size(); ++i) {
        ScriptComponent& scriptComponent = scriptsArray->getComponentFromIndex(i);
        const size_t originalSize = scriptComponent.scripts.size();

        scriptComponent.scripts.erase(
            std::remove_if(scriptComponent.scripts.begin(), scriptComponent.scripts.end(),
                [&deletedRelative, &deletedLuaRelative](const ScriptEntry& scriptEntry) {
                    if (scriptEntry.type == ScriptType::LUA) {
                        return matchesRelativeString(deletedLuaRelative, scriptEntry.path);
                    }
                    return matchesRelativeString(deletedRelative, scriptEntry.path) ||
                           matchesRelativeString(deletedRelative, scriptEntry.headerPath);
                }),
            scriptComponent.scripts.end());

        changed |= (scriptComponent.scripts.size() != originalSize);
    }

    return changed;
}

static bool visitTexturePaths(Texture& texture, const std::function<bool(std::string&)>& transform) {
    // Framebuffer and camera-linked textures carry an id instead of a file path.
    if (texture.isFramebuffer()) {
        return false;
    }

    // Per-face paths only exist on cubemaps, incomplete ones included
    bool hasFacePaths = false;
    for (size_t face = 1; face < 6; face++) {
        if (!texture.getPath(face).empty()) {
            hasFacePaths = true;
            break;
        }
    }

    if (hasFacePaths) {
        bool changed = false;
        for (size_t face = 0; face < 6; face++) {
            std::string path = texture.getPath(face);
            if (path.empty() || !transform(path)) {
                continue;
            }
            // One missing face leaves the whole cubemap unloadable
            if (path.empty()) {
                texture = Texture();
                return true;
            }
            texture.setCubePath(face, path);
            changed = true;
        }

        return changed;
    }

    std::string path = texture.getPath(0);
    if (path.empty() || !transform(path)) {
        return false;
    }

    // setPath("") would instead leave the texture waiting for a load that cannot happen
    if (path.empty()) {
        texture = Texture();
        return true;
    }

    if (texture.isCubeMap()) {
        // Single-file cubemap: setPath() would turn it into a 2D texture.
        texture.setCubeMap(path);
        return true;
    }

    const float svgScale = texture.getSvgScale();
    texture.setPath(path);
    texture.setSvgScale(svgScale);

    return true;
}

bool editor::Project::visitAssetPathsInRegistry(EntityRegistry* registry, const std::function<bool(std::string&)>& transform) {
    if (!registry) {
        return false;
    }

    bool changed = false;

    auto visitComponents = [&](auto* componentArray, auto&& visitComponent) {
        for (size_t i = 0; i < componentArray->size(); i++) {
            visitComponent(componentArray->getComponentFromIndex(i));
        }
    };

    visitComponents(registry->getComponentArray<MeshComponent>(), [&](MeshComponent& mesh) {
        for (unsigned int s = 0; s < mesh.numSubmeshes; s++) {
            Material& material = mesh.submeshes[s].material;
            bool submeshChanged = visitTexturePaths(material.baseColorTexture, transform);
            submeshChanged |= visitTexturePaths(material.emissiveTexture, transform);
            submeshChanged |= visitTexturePaths(material.metallicRoughnessTexture, transform);
            submeshChanged |= visitTexturePaths(material.occlusionTexture, transform);
            submeshChanged |= visitTexturePaths(material.normalTexture, transform);

            if (submeshChanged) {
                mesh.submeshes[s].needUpdateTexture = true;
                changed = true;
            }
        }
    });

    visitComponents(registry->getComponentArray<UIComponent>(), [&](UIComponent& ui) {
        if (visitTexturePaths(ui.texture, transform)) {
            ui.needUpdateTexture = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<ButtonComponent>(), [&](ButtonComponent& button) {
        bool buttonChanged = visitTexturePaths(button.textureNormal, transform);
        buttonChanged |= visitTexturePaths(button.textureHovered, transform);
        buttonChanged |= visitTexturePaths(button.texturePressed, transform);
        buttonChanged |= visitTexturePaths(button.textureDisabled, transform);

        if (buttonChanged) {
            button.needUpdateButton = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<SkyComponent>(), [&](SkyComponent& sky) {
        if (visitTexturePaths(sky.texture, transform)) {
            sky.needUpdateTexture = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<TerrainComponent>(), [&](TerrainComponent& terrain) {
        bool heightMapChanged = visitTexturePaths(terrain.heightMap, transform);
        bool terrainChanged = heightMapChanged;
        terrainChanged |= visitTexturePaths(terrain.blendMap, transform);
        terrainChanged |= visitTexturePaths(terrain.textureDetailRed, transform);
        terrainChanged |= visitTexturePaths(terrain.textureDetailGreen, transform);
        terrainChanged |= visitTexturePaths(terrain.textureDetailBlue, transform);

        for (TerrainFoliageLayer& layer : terrain.foliageLayers) {
            if (visitTexturePaths(layer.densityMap, transform) ||
                (!layer.meshPath.empty() && transform(layer.meshPath))) {
                terrain.needUpdateFoliage = true;
                changed = true;
            }
        }

        // The node tree and the physics heightfield are built from the map, so they follow it
        if (heightMapChanged) {
            terrain.heightMapLoaded = false;
            terrain.needUpdateTerrain = true;
        }

        if (terrainChanged) {
            terrain.needUpdateTexture = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<LightComponent>(), [&](LightComponent& light) {
        if (visitTexturePaths(light.spotMask, transform)) {
            light.needUpdateShadowCamera = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<PointsComponent>(), [&](PointsComponent& points) {
        if (visitTexturePaths(points.texture, transform)) {
            points.needUpdateTexture = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<ReflectionProbeComponent>(), [&](ReflectionProbeComponent& probe) {
        if (visitTexturePaths(probe.texture, transform)) {
            probe.needUpdate = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<ModelComponent>(), [&](ModelComponent& model) {
        if (!model.filename.empty() && transform(model.filename)) {
            model.needUpdateModel = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<SoundComponent>(), [&](SoundComponent& sound) {
        if (!sound.filename.empty() && transform(sound.filename)) {
            sound.loaded = false;
            sound.needUpdate = true;
            changed = true;
        }
    });

    visitComponents(registry->getComponentArray<TextComponent>(), [&](TextComponent& text) {
        for (std::string& font : text.font) {
            if (!font.empty() && transform(font)) {
                text.needReloadAtlas = true;
                changed = true;
            }
        }
    });

    return changed;
}

bool editor::Project::visitLuaPathsInRegistry(EntityRegistry* registry, const std::function<bool(std::string&)>& transform) {
    if (!registry) {
        return false;
    }

    auto scriptsArray = registry->getComponentArray<ScriptComponent>();
    bool changed = false;

    for (size_t i = 0; i < scriptsArray->size(); i++) {
        ScriptComponent& scriptComponent = scriptsArray->getComponentFromIndex(i);
        for (auto& scriptEntry : scriptComponent.scripts) {
            if (scriptEntry.type != ScriptType::LUA) {
                continue;
            }
            changed |= transform(scriptEntry.path);
        }
    }

    return changed;
}

bool editor::Project::detachChildSceneFromParents(uint32_t childSceneId, const std::set<uint32_t>& skippedSceneIds) {
    bool changed = false;

    for (auto& sceneProject : scenes) {
        if (sceneProject.id == childSceneId || skippedSceneIds.find(sceneProject.id) != skippedSceneIds.end()) {
            continue;
        }

        if (!eraseChildSceneReference(sceneProject.childScenes, childSceneId)) {
            continue;
        }

        if (sceneProject.scene) {
            sceneProject.needUpdateRender = true;
            sceneProject.isModified = true;
        }

        changed = true;
    }

    if (changed) {
        // Dropped an inline child, so the Engine layers need rebuilding.
        editor::getEditorHost().resetLastActivatedScene();
    }

    return changed;
}

bool editor::Project::removeMissingChildSceneReferences(SceneProject& sceneProject) {
    bool changed = false;

    for (auto it = sceneProject.childScenes.begin(); it != sceneProject.childScenes.end();) {
        uint32_t childSceneId = it->id;
        if (getScene(childSceneId)) {
            ++it;
            continue;
        }

        Out::warning("Scene '%s' references missing child scene ID %u. Removing child scene reference.",
            sceneProject.name.c_str(), childSceneId);
        it = sceneProject.childScenes.erase(it);
        changed = true;
    }

    if (changed && sceneProject.scene) {
        sceneProject.needUpdateRender = true;
        sceneProject.isModified = true;
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

    bool standaloneChanged = false;
    for (fs::path& bundlePath : standaloneBundles) {
        fs::path updatedPath;
        if (remapRelativePath(oldRelative, newRelative, bundlePath, updatedPath)) {
            bundlePath = updatedPath;
            standaloneChanged = true;
        }
    }

    if (standaloneChanged) {
        saveProject();
    }

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
    if (projectPath.empty() || oldPath.empty() || newPath.empty()) {
        return;
    }

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }

        if (remapScriptPathsInRegistry(sceneProject.scene, oldPath, newPath)) {
            sceneProject.isModified = true;
            updateSceneCppScripts(&sceneProject);
        }
    }

    for (auto& [bundlePath, bundle] : entityBundles) {
        if (!remapScriptPathsInRegistry(bundle.registry.get(), oldPath, newPath)) {
            continue;
        }
        bundle.isModified = true;
        saveEntityBundleToDisk(bundlePath);
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
    std::set<uint32_t> removedChildSceneIds;

    for (auto& sceneProject : scenes) {
        if (sceneProject.filepath.empty()) {
            continue;
        }

        if (matchesRelativePath(deletedRelative, sceneProject.filepath)) {
            removedChildSceneIds.insert(sceneProject.id);
            if (sceneProject.opened) {
                sceneProject.filepath.clear();
                sceneProject.isModified = true;
                sceneProject.needUpdateRender = true;
            } else {
                scenesToRemove.push_back(sceneProject.id);
            }
            changed = true;
        }
    }

    for (uint32_t childSceneId : removedChildSceneIds) {
        if (detachChildSceneFromParents(childSceneId, removedChildSceneIds)) {
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
            editor::getEditorHost().clearSceneWindowState(sceneId);
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

    const size_t listedBefore = standaloneBundles.size();
    standaloneBundles.erase(
        std::remove_if(standaloneBundles.begin(), standaloneBundles.end(),
            [&](const fs::path& bundlePath) { return matchesRelativePath(deletedRelative, bundlePath); }),
        standaloneBundles.end());
    if (standaloneBundles.size() != listedBefore) {
        saveProject();
    }

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
    if (projectPath.empty() || deletedPath.empty()) {
        return;
    }

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }

        if (cleanupScriptPathsInRegistry(sceneProject.scene, deletedPath)) {
            sceneProject.isModified = true;
            updateSceneCppScripts(&sceneProject);
        }
    }

    for (auto& [bundlePath, bundle] : entityBundles) {
        if (!cleanupScriptPathsInRegistry(bundle.registry.get(), deletedPath)) {
            continue;
        }
        bundle.isModified = true;
        saveEntityBundleToDisk(bundlePath);
    }
}

void editor::Project::applyAssetPathChange(const std::function<bool(std::string&)>& transform) {
    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene || !visitAssetPathsInRegistry(sceneProject.scene, transform)) {
            continue;
        }

        sceneProject.isModified = true;
        sceneProject.needUpdateRender = true;
    }

    for (auto& [bundlePath, bundle] : entityBundles) {
        if (!bundle.registry || !visitAssetPathsInRegistry(bundle.registry.get(), transform)) {
            continue;
        }

        bundle.isModified = true;
        saveEntityBundleToDisk(bundlePath);
    }

    visitAssetPathsInMaterialFiles(transform);
}

void editor::Project::remapAssetFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    if (projectPath.empty()) {
        return;
    }

    // A file that left the assets directory cannot be referenced, so it counts as deleted
    if (!isInsideAssetsPath(newPath)) {
        cleanupAssetFilePath(oldPath);
        return;
    }

    fs::path oldRelative = normalizeToAssetsRelative(oldPath);
    fs::path newRelative = normalizeToAssetsRelative(newPath);

    if (oldRelative.empty() || newRelative.empty()) {
        return;
    }

    applyAssetPathChange([&](std::string& assetPath) -> bool {
        std::string updated;
        if (!remapRelativeString(oldRelative, newRelative, assetPath, updated)) {
            return false;
        }
        assetPath = updated;
        return true;
    });
}

void editor::Project::cleanupAssetFilePath(const std::filesystem::path& deletedPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path deletedRelative = normalizeToAssetsRelative(deletedPath);
    if (deletedRelative.empty()) {
        return;
    }

    applyAssetPathChange([&](std::string& assetPath) -> bool {
        if (!matchesRelativeString(deletedRelative, assetPath)) {
            return false;
        }
        assetPath.clear();
        return true;
    });
}

void editor::Project::applyCustomShaderPathChange(const std::function<bool(std::string&)>& transform) {
    // customShader lives on every renderable component type; visit each array in a registry,
    // apply the transform, and flag a shader reload on the entities that changed.
    auto applyToRegistry = [&](EntityRegistry* registry) -> bool {
        bool changed = false;
        auto applyToArray = [&](auto arr) {
            for (size_t i = 0; i < arr->size(); ++i) {
                auto& comp = arr->getComponentFromIndex(i);
                if (comp.customShader.empty())
                    continue;
                if (transform(comp.customShader)) {
                    Catalog::updateEntity(registry, arr->getEntity(i), UpdateFlags_Shader_Reload);
                    changed = true;
                }
            }
        };
        applyToArray(registry->getComponentArray<MeshComponent>());
        applyToArray(registry->getComponentArray<UIComponent>());
        applyToArray(registry->getComponentArray<PointsComponent>());
        applyToArray(registry->getComponentArray<LinesComponent>());
        applyToArray(registry->getComponentArray<SkyComponent>());
        return changed;
    };

    // scene-level default shaders reference the same files; the setter flags the reload
    auto applyToSceneDefaults = [&](Scene* scene) -> bool {
        bool changed = false;
        for (ShaderType type : {ShaderType::MESH, ShaderType::UI, ShaderType::SKYBOX, ShaderType::POINTS, ShaderType::LINES}) {
            std::string defaultShader = scene->getDefaultCustomShader(type);
            if (!defaultShader.empty() && transform(defaultShader)) {
                scene->setDefaultCustomShader(type, defaultShader);
                changed = true;
            }
        }
        return changed;
    };

    for (auto& sceneProject : scenes) {
        if (!sceneProject.scene) {
            continue;
        }
        bool changed = applyToRegistry(sceneProject.scene);
        changed |= applyToSceneDefaults(sceneProject.scene);
        if (changed) {
            sceneProject.isModified = true;
            sceneProject.needUpdateRender = true;
        }
    }

    for (auto& [bundlePath, bundle] : entityBundles) {
        if (!bundle.registry) {
            continue;
        }
        if (applyToRegistry(bundle.registry.get())) {
            bundle.isModified = true;
            saveEntityBundleToDisk(bundlePath);
        }
    }
}

void editor::Project::remapShaderFilePath(const std::filesystem::path& oldPath, const std::filesystem::path& newPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path oldRelative = normalizeToProjectRelative(oldPath);
    fs::path newRelative = normalizeToProjectRelative(newPath);

    if (oldRelative.empty() || newRelative.empty()) {
        return;
    }

    // customShader resolves to a .vert and a .frag entry point; remap each path against the
    // rename and rebuild the value. A single-file rename of a shared-base pair naturally
    // becomes the separate-file form; a directory rename remaps both via child match.
    applyCustomShaderPathChange([&](std::string& customShader) -> bool {
        Util::CustomShaderPaths paths = Util::resolveCustomShaderPaths(customShader);
        std::string newVert = paths.vert;
        std::string newFrag = paths.frag;
        bool changed = false;
        std::string updated;
        if (remapRelativeString(oldRelative, newRelative, paths.vert, updated)) {
            newVert = updated;
            changed = true;
        }
        if (remapRelativeString(oldRelative, newRelative, paths.frag, updated)) {
            newFrag = updated;
            changed = true;
        }
        if (changed) {
            customShader = Util::makeCustomShader(newVert, newFrag);
        }
        return changed;
    });
}

void editor::Project::cleanupShaderFilePath(const std::filesystem::path& deletedPath) {
    if (projectPath.empty()) {
        return;
    }

    fs::path deletedRelative = normalizeToProjectRelative(deletedPath);
    if (deletedRelative.empty()) {
        return;
    }

    // A custom shader needs both its .vert and .frag; deleting either entry point (or the
    // containing folder) leaves the reference dangling, so reset it to built-in. Match the
    // deleted path against each resolved entry point (handles shared-base and separate forms).
    applyCustomShaderPathChange([&](std::string& customShader) -> bool {
        Util::CustomShaderPaths paths = Util::resolveCustomShaderPaths(customShader);
        if (matchesRelativeString(deletedRelative, paths.vert) || matchesRelativeString(deletedRelative, paths.frag)) {
            customShader.clear();
            return true;
        }
        return false;
    });
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
        default:
            break;
    }

    Transform& cameratransform = scene->getComponent<Transform>(defaultCamera);
    cameratransform.position = Vector3(0.0, 0.0, 1.0);

    return defaultCamera;
}

void editor::Project::checkUnsavedAndExecute(uint32_t sceneId, std::function<void()> action) {
    SceneProject* sceneProject = getScene(sceneId);

    if (sceneProject && hasSceneUnsavedChanges(sceneId)) {
        editor::getEditorHost().registerThreeButtonAlert(
            "Unsaved Changes",
            "There are unsaved changes. Do you want to save first?",
            [this, sceneId, action]() {
                saveScene(sceneId, [action](bool success) {
                    if (success && action) action();
                });
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
    this->libName = Factory::toIdentifier(name);
    editor::getEditorHost().updateWindowTitle(name);
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

void editor::Project::setVSyncEnabled(bool enabled){
    this->vsyncEnabled = enabled;
}

bool editor::Project::isVSyncEnabled() const{
    return vsyncEnabled;
}

void editor::Project::setWindowMode(WindowMode windowMode){
    this->windowMode = windowMode;
}

editor::WindowMode editor::Project::getWindowMode() const{
    return windowMode;
}

void editor::Project::setWindowSize(unsigned int width, unsigned int height){
    this->windowWidth = width;
    this->windowHeight = height;
}

unsigned int editor::Project::getWindowWidth() const{
    return windowWidth;
}

unsigned int editor::Project::getWindowHeight() const{
    return windowHeight;
}

void editor::Project::setWindowResizable(bool resizable){
    this->windowResizable = resizable;
}

bool editor::Project::isWindowResizable() const{
    return windowResizable;
}

void editor::Project::setWindowTitle(const std::string& title){
    this->windowTitle = title;
}

std::string editor::Project::getWindowTitle() const{
    return windowTitle;
}

void editor::Project::setWindowIcon(const std::filesystem::path& iconPath){
    this->windowIcon = iconPath;
}

std::filesystem::path editor::Project::getWindowIcon() const{
    return windowIcon;
}

editor::WindowSettings editor::Project::getWindowSettings() const{
    WindowSettings settings;
    settings.mode = windowMode;
    settings.width = windowWidth;
    settings.height = windowHeight;
    settings.resizable = windowResizable;
    if (!windowTitle.empty()) {
        settings.title = windowTitle;
    } else if (!name.empty()) {
        settings.title = name;
    }
    // The title is embedded in generated C++ and CMake string literals whose
    // escaping only covers quotes/backslashes; a control character (e.g. a
    // newline from a hand-edited project.yaml) would still break the literal.
    // Unsigned compare so UTF-8 continuation bytes are left untouched.
    for (char& c : settings.title) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F) c = ' ';
    }
    return settings;
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

void editor::Project::setScriptDirs(std::vector<std::filesystem::path> scriptDirs){
    this->scriptDirs = std::move(scriptDirs);
}

const std::vector<std::filesystem::path>& editor::Project::getScriptDirs() const{
    return scriptDirs;
}

static bool isInsideRoot(const std::filesystem::path& path, const std::filesystem::path& root){
    std::error_code ec;
    std::filesystem::path relativePath = std::filesystem::relative(path, root, ec);
    if (ec || relativePath.empty()){
        return false;
    }

    return *relativePath.begin() != "..";
}

static std::filesystem::path resolveRootDir(const std::filesystem::path& projectPath, const std::filesystem::path& dir){
    if (dir.empty() || dir == "."){
        return projectPath;
    }
    if (dir.is_absolute()){
        return dir.lexically_normal();
    }
    return (projectPath / dir).lexically_normal();
}

static std::filesystem::path resolveAgainstRoot(const std::filesystem::path& root, const std::filesystem::path& relative){
    if (relative.empty()){
        return {};
    }
    if (relative.is_absolute()){
        return relative.lexically_normal();
    }
    return (root / relative).lexically_normal();
}

static std::filesystem::path normalizeAgainstRoot(const std::filesystem::path& root, const std::filesystem::path& path){
    if (path.empty()){
        return {};
    }

    std::filesystem::path normalizedPath = path.lexically_normal();
    if (!normalizedPath.is_absolute()){
        return normalizedPath;
    }

    std::error_code ec;
    std::filesystem::path relativePath = std::filesystem::relative(normalizedPath, root, ec);
    if (ec || relativePath.empty()){
        return normalizedPath;
    }

    return relativePath.lexically_normal();
}

std::filesystem::path editor::Project::getAssetsPath() const{
    return resolveRootDir(projectPath, assetsDir);
}

std::filesystem::path editor::Project::getLuaPath() const{
    return resolveRootDir(projectPath, luaDir);
}

std::filesystem::path editor::Project::resolveAssetPath(const std::filesystem::path& assetRelative) const{
    return resolveAgainstRoot(getAssetsPath(), assetRelative);
}

std::filesystem::path editor::Project::normalizeToAssetsRelative(const std::filesystem::path& path) const{
    return normalizeAgainstRoot(getAssetsPath(), path);
}

bool editor::Project::isInsideAssetsPath(const std::filesystem::path& path) const{
    return isInsideRoot(resolveAssetPath(path), getAssetsPath());
}

std::filesystem::path editor::Project::resolveLuaPath(const std::filesystem::path& luaRelative) const{
    return resolveAgainstRoot(getLuaPath(), luaRelative);
}

std::filesystem::path editor::Project::normalizeToLuaRelative(const std::filesystem::path& path) const{
    return normalizeAgainstRoot(getLuaPath(), path);
}

// Only a directory without project files can be moved as a whole: those are referenced
// from the project root and would need their own remap.
static bool holdsOnlyAssets(const std::filesystem::path& directory){
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it){
        const auto& entry = *it;
        if (!entry.is_regular_file()){
            continue;
        }

        const std::string path = entry.path().string();
        if (editor::Util::isSceneFile(path) || editor::Util::isBundleFile(path) || editor::Util::isMaterialFile(path) ||
            editor::Util::isScriptFile(path) || editor::Util::isShaderFile(path)){
            return false;
        }
    }

    return !ec;
}

bool editor::Project::visitAssetPathsInMaterialFiles(const std::function<bool(std::string&)>& transform){
    if (projectPath.empty()){
        return false;
    }

    std::error_code ec;
    bool changed = false;

    for (auto it = fs::recursive_directory_iterator(projectPath, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it){
        const auto& entry = *it;

        const std::string name = entry.path().filename().string();
        if (entry.is_directory() && !name.empty() && (name[0] == '.' || name == "build")){
            it.disable_recursion_pending();
            continue;
        }

        if (!entry.is_regular_file() || !Util::isMaterialFile(entry.path().string())){
            continue;
        }

        Material material;
        try {
            material = Stream::decodeMaterial(YAML::LoadFile(entry.path().string()));
        } catch (const std::exception& e) {
            Out::warning("Could not read material %s: %s", entry.path().string().c_str(), e.what());
            continue;
        }

        bool materialChanged = visitTexturePaths(material.baseColorTexture, transform);
        materialChanged |= visitTexturePaths(material.emissiveTexture, transform);
        materialChanged |= visitTexturePaths(material.metallicRoughnessTexture, transform);
        materialChanged |= visitTexturePaths(material.occlusionTexture, transform);
        materialChanged |= visitTexturePaths(material.normalTexture, transform);

        if (!materialChanged){
            continue;
        }

        std::ofstream file(entry.path());
        if (!file){
            Out::error("Failed to write material: %s", entry.path().string().c_str());
            continue;
        }
        file << YAML::Dump(Stream::encodeMaterial(material));
        changed = true;
    }

    return changed;
}

void editor::Project::changeAssetRoots(const std::filesystem::path& newAssetsDir, const std::filesystem::path& newLuaDir){
    const fs::path oldAssetsRoot = getAssetsPath();
    const fs::path oldLuaRoot = getLuaPath();
    const fs::path newAssetsRoot = resolveRootDir(projectPath, newAssetsDir);
    const fs::path newLuaRoot = resolveRootDir(projectPath, newLuaDir);

    if (projectPath.empty() || (oldAssetsRoot == newAssetsRoot && oldLuaRoot == newLuaRoot)){
        assetsDir = newAssetsDir;
        luaDir = newLuaDir;
        return;
    }

    // Buffers must reach disk before their files move, as a play session does
    editor::getEditorHost().saveAllCodeEditors();

    const bool assetsChanged = oldAssetsRoot != newAssetsRoot;
    const bool luaChanged = oldLuaRoot != newLuaRoot;

    std::error_code ec;
    fs::create_directories(newAssetsRoot, ec);
    fs::create_directories(newLuaRoot, ec);

    // Scenes that are not open still hold references, so they are loaded and unloaded again
    std::vector<uint32_t> temporarilyLoaded;
    for (size_t i = 0; i < scenes.size(); i++){
        if (scenes[i].filepath.empty() || scenes[i].scene){
            continue;
        }
        const uint32_t sceneId = scenes[i].id;
        loadScene(scenes[i].filepath, false, false, true);
        temporarilyLoaded.push_back(sceneId);
    }

    auto unloadTemporaryScenes = [&]() {
        for (uint32_t sceneId : temporarilyLoaded){
            if (SceneProject* sceneProject = getScene(sceneId)){
                deleteSceneProject(sceneProject);
            }
        }
    };

    std::vector<std::pair<fs::path, fs::path>> completedMoves;
    std::vector<std::pair<fs::path, fs::path>> keptExisting;
    bool relocationFailed = false;

    // Merges into an existing destination, so a folder whose files were relocated one
    // by one still ends up whole.
    std::function<bool(const fs::path&, const fs::path&)> moveEntry =
        [&](const fs::path& source, const fs::path& destination) {
        std::error_code moveEc;
        if (!fs::exists(source, moveEc)){
            return false;
        }

        if (fs::is_directory(source, moveEc)){
            fs::create_directories(destination, moveEc);
            bool moved = false;
            for (const auto& entry : fs::directory_iterator(source, moveEc)){
                moved |= moveEntry(entry.path(), destination / entry.path().filename());
                // Reporting a move here would point the reference at what stayed behind.
                if (relocationFailed){
                    return false;
                }
            }
            // Only removes the source when everything left it.
            fs::remove(source, moveEc);
            return moved;
        }

        if (fs::exists(destination, moveEc)){
            Out::warning("Kept the existing %s: the reference points at it and %s is no longer used",
                         destination.string().c_str(), source.string().c_str());
            keptExisting.emplace_back(source, destination);
            return false;
        }

        fs::create_directories(destination.parent_path(), moveEc);
        fs::rename(source, destination, moveEc);
        if (moveEc){
            // A root on another volume rejects rename, so the file is copied and only then dropped.
            std::error_code copyEc;
            fs::copy_file(source, destination, copyEc);
            if (copyEc){
                // A partial copy would pass as an already relocated file on the next attempt.
                std::error_code cleanupEc;
                fs::remove(destination, cleanupEc);

                Out::error("Failed to copy %s: %s", source.string().c_str(), copyEc.message().c_str());
                relocationFailed = true;
                return false;
            }
            if (!fs::remove(source, copyEc)){
                Out::warning("Copied %s but could not remove the original", source.string().c_str());
            }
        }

        completedMoves.emplace_back(source, destination);
        return true;
    };

    // Keeps the project on its previous roots when a relocation could not finish.
    auto undoMoves = [&]() {
        for (size_t i = completedMoves.size(); i > 0; i--){
            const auto& [source, destination] = completedMoves[i - 1];
            std::error_code undoEc;

            // A directory emptied by the move is gone by now.
            fs::create_directories(source.parent_path(), undoEc);

            // The copy fallback may have left the original in place.
            if (fs::exists(source, undoEc)){
                fs::remove(destination, undoEc);
                continue;
            }

            fs::rename(destination, source, undoEc);
            if (!undoEc){
                continue;
            }

            fs::copy_file(destination, source, undoEc);
            if (undoEc){
                // References still point here, so a partial copy would pass as the file itself.
                std::error_code cleanupEc;
                fs::remove(source, cleanupEc);

                Out::error("Could not restore %s, the file is at %s: %s", source.string().c_str(),
                           destination.string().c_str(), undoEc.message().c_str());
                continue;
            }
            fs::remove(destination, undoEc);
        }
    };

    std::map<fs::path, fs::path> relocations;

    // Files outside the new root are moved into it, keeping their layout relative to
    // the old root so references between them (a model and its textures) still match.
    auto relocate = [&](const fs::path& absolutePath, const fs::path& oldRoot, const fs::path& newRoot) {
        if (relocationFailed || isInsideRoot(absolutePath, newRoot) || relocations.count(absolutePath)){
            return;
        }

        std::error_code relocateEc;
        const fs::path relativeToOld = fs::relative(absolutePath, oldRoot, relocateEc);
        if (relocateEc || relativeToOld.empty() || *relativeToOld.begin() == ".."){
            Out::warning("Reference outside both asset directories was kept as is: %s", absolutePath.string().c_str());
            relocations[absolutePath] = absolutePath;
            return;
        }

        const fs::path destination = (newRoot / relativeToOld).lexically_normal();
        relocations[absolutePath] = destination;

        // Models carry sidecar files referenced from inside the model, so their folder
        // moves along when it holds nothing else.
        fs::path source = absolutePath;
        fs::path sourceDestination = destination;
        if (Util::isModelFile(absolutePath.string()) && relativeToOld.has_parent_path()){
            const fs::path modelDir = (oldRoot / relativeToOld.parent_path()).lexically_normal();
            if (!isInsideRoot(newRoot, modelDir) && holdsOnlyAssets(modelDir)){
                source = modelDir;
                sourceDestination = (newRoot / relativeToOld.parent_path()).lexically_normal();
            }
        }

        if (moveEntry(source, sourceDestination)){
            Out::info("Moved %s to %s", source.string().c_str(), sourceDestination.string().c_str());
            return;
        }

        // Nothing moved: the reference only follows when the file is already there.
        if (fs::exists(destination, relocateEc)){
            return;
        }

        relocations[absolutePath] = absolutePath;
    };

    // Resolved before anything moves: a file that cannot be relocated aborts the whole change.
    std::vector<fs::path> assetSources;
    std::vector<fs::path> luaSources;

    const std::function<bool(std::string&)> assetCollect = [&](std::string& stored) {
        if (!stored.empty()){
            assetSources.push_back(resolveAgainstRoot(oldAssetsRoot, fs::path(stored)));
        }
        return false;
    };
    const std::function<bool(std::string&)> luaCollect = [&](std::string& stored) {
        if (!stored.empty()){
            luaSources.push_back(resolveAgainstRoot(oldLuaRoot, fs::path(stored)));
        }
        return false;
    };

    auto rebase = [&](std::string& stored, const fs::path& oldRoot, const fs::path& newRoot) {
        if (stored.empty()){
            return false;
        }

        const fs::path source = resolveAgainstRoot(oldRoot, fs::path(stored));
        const auto relocated = relocations.find(source);
        const fs::path absolutePath = relocated != relocations.end() ? relocated->second : source;

        // A file left outside the new root keeps its stored path: re-basing it would
        // only turn it into a "../" reference the runtime cannot resolve.
        if (!isInsideRoot(absolutePath, newRoot)){
            return false;
        }

        std::error_code rebaseEc;
        const fs::path updated = fs::relative(absolutePath, newRoot, rebaseEc);
        if (rebaseEc || updated.empty()){
            return false;
        }

        const std::string updatedPath = updated.lexically_normal().generic_string();
        if (updatedPath == stored){
            return false;
        }

        stored = updatedPath;
        return true;
    };

    const std::function<bool(std::string&)> assetTransform = [&](std::string& stored) {
        return rebase(stored, oldAssetsRoot, newAssetsRoot);
    };
    const std::function<bool(std::string&)> luaTransform = [&](std::string& stored) {
        return rebase(stored, oldLuaRoot, newLuaRoot);
    };

    // Walks every reference the project holds, writing back only what a visitor changed.
    auto visitReferences = [&](const std::function<bool(std::string&)>& assetVisitor,
                               const std::function<bool(std::string&)>& luaVisitor) {
        for (auto& sceneProject : scenes){
            if (!sceneProject.scene){
                continue;
            }

            bool sceneChanged = assetsChanged && visitAssetPathsInRegistry(sceneProject.scene, assetVisitor);
            sceneChanged |= luaChanged && visitLuaPathsInRegistry(sceneProject.scene, luaVisitor);

            if (!sceneChanged){
                continue;
            }

            sceneProject.isModified = true;
            sceneProject.needUpdateRender = true;
            if (!sceneProject.filepath.empty()){
                saveSceneToPath(sceneProject.id, sceneProject.filepath);
            }
        }

        for (auto& [bundlePath, bundle] : entityBundles){
            if (!bundle.registry){
                continue;
            }

            bool bundleChanged = assetsChanged && visitAssetPathsInRegistry(bundle.registry.get(), assetVisitor);
            bundleChanged |= luaChanged && visitLuaPathsInRegistry(bundle.registry.get(), luaVisitor);

            if (!bundleChanged){
                continue;
            }

            bundle.isModified = true;
            saveEntityBundleToDisk(bundlePath);
        }

        if (assetsChanged){
            visitAssetPathsInMaterialFiles(assetVisitor);
        }
    };

    visitReferences(assetCollect, luaCollect);

    // Generated maps follow the assets root, moved before references resolve against it
    const fs::path oldTerrainMaps = oldAssetsRoot / "terrain_maps";
    if (assetsChanged && fs::is_directory(oldTerrainMaps, ec) &&
        !isInsideRoot(oldTerrainMaps, newAssetsRoot) && !isInsideRoot(newAssetsRoot, oldTerrainMaps)){
        moveEntry(oldTerrainMaps, newAssetsRoot / "terrain_maps");
    }

    for (const fs::path& source : assetSources){
        relocate(source, oldAssetsRoot, newAssetsRoot);
    }
    for (const fs::path& source : luaSources){
        relocate(source, oldLuaRoot, newLuaRoot);
    }

    if (relocationFailed){
        undoMoves();
        unloadTemporaryScenes();

        Out::error("Asset directories not changed: a referenced file could not be moved");
        editor::getEditorHost().registerAlert("Error", "Failed to move files to the new directories!");
        return;
    }

    assetsDir = newAssetsDir;
    luaDir = newLuaDir;

    visitReferences(assetTransform, luaTransform);

    // Open tabs still hold the old path, where a later save would recreate the script.
    if (CodeEditor* codeEditor = editor::getEditorHost().getCodeEditor()){
        for (const auto& [source, destination] : completedMoves){
            codeEditor->handleFileRename(source, destination);
        }

        // Nothing moved here, so the tab is reopened on the file the reference took.
        // One that could not be saved is left alone, its content would be discarded.
        for (const auto& [source, destination] : keptExisting){
            if (codeEditor->isFileOpen(source.string()) && !codeEditor->isFileModified(source.string())){
                codeEditor->closeFile(source.string());
                codeEditor->openFile(destination.string());
            }
        }
    }

    if (ImageViewerWindow* imageViewer = editor::getEditorHost().getImageViewerWindow()){
        for (const auto& [source, destination] : completedMoves){
            imageViewer->handleFileRename(source, destination);
        }
    }

    unloadTemporaryScenes();

    saveProjectFile();
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

void editor::Project::setCMakeBuildJobs(unsigned int jobs){
    // Stored unclamped: the per-machine cap is applied at build time
    // (Generator::build), so a value configured on a machine with more
    // cores survives load/save round-trips on this one.
    cmakeBuildJobs = jobs;
}

unsigned int editor::Project::getCMakeBuildJobs() const{
    return cmakeBuildJobs;
}

void editor::Project::setPackNativeResources(bool enabled){
    packNativeResources = enabled;
}

bool editor::Project::shouldPackNativeResources() const{
    return packNativeResources;
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
        // Scene-appearance defaults for newly created 3D scenes. Loaded scenes keep
        // the values decoded from disk (see SceneRender3D constructor note).
        data.scene->setLightState(LightState::ON);
        data.scene->setGlobalIllumination(0.2);
        data.scene->setBackgroundColor(Vector4(0.25, 0.45, 0.65, 1.0));

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

    editor::getEditorHost().addNewSceneToDock(data.id);

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
            if (scriptEntry.type == ScriptType::LUA)
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

            SceneScriptSource sceneScript;
            sceneScript.path = scriptEntry.path;
            sceneScript.headerPath = scriptEntry.headerPath;
            sceneScript.className = scriptEntry.className;
            sceneScript.properties = toScriptPropertyInfos(scriptEntry.properties);
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
        maxValues.maxBones = std::max(maxValues.maxBones,
            static_cast<unsigned int>(mesh.bonesMatrix.size()));
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

    // SpriteAnimationComponent.frames / framesTime are also HybridArray<int, MAX_SPRITE_FRAMES>.
    // Their counts are independent of (and can exceed) a sprite's numFramesRect (e.g. a ping-pong
    // sequence reuses frame indices), and the generated component code writes them with unchecked
    // operator[], so they must factor into MAX_SPRITE_FRAMES too.
    auto spriteAnims = sceneProject->scene->getComponentArray<SpriteAnimationComponent>();
    for (size_t i = 0; i < spriteAnims->size(); ++i) {
        const SpriteAnimationComponent& spriteAnim = spriteAnims->getComponentFromIndex(i);
        maxValues.maxSpriteFrames = std::max(maxValues.maxSpriteFrames, spriteAnim.framesSize);
        maxValues.maxSpriteFrames = std::max(maxValues.maxSpriteFrames, spriteAnim.framesTimeSize);
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
    const bool hasReflectionProbes = scene->getComponentArray<ReflectionProbeComponent>()->size() > 0;

    // resolve the effective custom shader (component > scene default > built-in) from the
    // strings rather than the cached customShaderId, which lags a reload frame after a
    // scene-default change and is zeroed by the build-fail fallback
    auto effectiveShaderId = [&](const std::string& customShader, ShaderType type) -> uint16_t {
        return ShaderPool::registerCustomShader(customShader.empty() ? scene->getDefaultCustomShader(type) : customShader);
    };

    // the runtime falls back to the built-in variant when a custom shader fails to build,
    // so always collect the customId-0 key alongside a custom one
    auto insertKeys = [&](ShaderType type, uint32_t properties, uint16_t customShaderId) {
        keys.insert(ShaderPool::getShaderKey(type, properties, customShaderId));
        if (customShaderId != 0) {
            keys.insert(ShaderPool::getShaderKey(type, properties, 0));
        }
    };

    // Foliage entities are created at runtime and kept out of sceneProject->entities, so the
    // authored list alone would export the field without its instanced variant.
    std::vector<Entity> shaderEntities = sceneProject->entities;
    auto meshSystem = scene->getSystem<MeshSystem>();
    auto terrains = scene->getComponentArray<TerrainComponent>();
    for (size_t i = 0; i < terrains->size(); ++i) {
        const std::vector<Entity> foliage = meshSystem->getFoliageEntities(terrains->getEntity(i));
        shaderEntities.insert(shaderEntities.end(), foliage.begin(), foliage.end());
    }

    for (Entity entity : shaderEntities) {
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<MeshComponent>())) {
            const MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
            if (mesh.loaded) {
                uint16_t customShaderId = effectiveShaderId(mesh.customShader, ShaderType::MESH);
                for (unsigned int s = 0; s < mesh.numSubmeshes; ++s) {
                    uint32_t meshProperties = mesh.submeshes[s].shaderProperties;
                    // Saving/exporting can happen before RenderSystem has reloaded
                    // meshes after a probe was added. Derive the required IBL
                    // variant here so the exported shader set is still complete.
                    if (hasReflectionProbes && mesh.receiveLights && mesh.receiveIBL) {
                        meshProperties &= ~(1u << 0); // no longer MATERIAL_UNLIT
                        meshProperties |= (1u << 6);  // HAS_NORMALS
                        meshProperties |= (1u << 19); // USE_IBL
                        // the unlit->lit transition also pulls in the submesh's
                        // normal-map/tangent attributes and SSAO, mirroring
                        // RenderSystem::loadMesh
                        if (mesh.submeshes[s].hasNormalMap) {
                            meshProperties |= (1u << 7); // HAS_NORMAL_MAP
                        }
                        if (mesh.submeshes[s].hasTangent) {
                            meshProperties |= (1u << 8); // HAS_TANGENTS
                        }
                        if (scene->isSSAOEnabled()) {
                            meshProperties |= (1u << 21); // USE_SSAO
                        }
                    }
                    insertKeys(ShaderType::MESH, meshProperties, customShaderId);
                    // The runtime only computes depthShaderProperties while
                    // shadows or SSAO are active, so the stored value can be a
                    // stale 0 for scenes whose shadow lights arrive at runtime
                    // (scripts spawning them). Derive the props from the same
                    // submesh flags RenderSystem::loadMesh uses, and keep the
                    // stored key too (identical when it was computed).
                    bool isTerrain = signature.test(scene->getComponentId<TerrainComponent>());
                    bool isInstanced = signature.test(scene->getComponentId<InstancedMeshComponent>());
                    uint32_t depthProperties = ShaderPool::getDepthMeshProperties(
                        mesh.submeshes[s].textureShadow, mesh.submeshes[s].hasSkinning,
                        mesh.submeshes[s].hasMorphTarget, mesh.submeshes[s].hasMorphNormal,
                        mesh.submeshes[s].hasMorphTangent, isTerrain, isInstanced);
                    keys.insert(ShaderPool::getShaderKey(ShaderType::DEPTH, depthProperties));
                    keys.insert(ShaderPool::getShaderKey(ShaderType::DEPTH, mesh.submeshes[s].depthShaderProperties));
                    if (mesh.submeshes[s].gbufferShader) {
                        keys.insert(ShaderPool::getShaderKey(ShaderType::GBUFFER, mesh.submeshes[s].gbufferShaderProperties));
                    }
                }
            }
        }

        if (signature.test(scene->getComponentId<UIComponent>())) {
            const UIComponent& ui = scene->getComponent<UIComponent>(entity);
            if (ui.loaded) {
                insertKeys(ShaderType::UI, ui.shaderProperties, effectiveShaderId(ui.customShader, ShaderType::UI));
            }
        }

        if (signature.test(scene->getComponentId<PointsComponent>())) {
            const PointsComponent& pts = scene->getComponent<PointsComponent>(entity);
            if (pts.loaded) {
                insertKeys(ShaderType::POINTS, pts.shaderProperties, effectiveShaderId(pts.customShader, ShaderType::POINTS));
            }
        }

        if (signature.test(scene->getComponentId<LinesComponent>())) {
            const LinesComponent& ln = scene->getComponent<LinesComponent>(entity);
            if (ln.loaded) {
                insertKeys(ShaderType::LINES, ln.shaderProperties, effectiveShaderId(ln.customShader, ShaderType::LINES));
            }
        }

        if (signature.test(scene->getComponentId<SkyComponent>())) {
            const SkyComponent& sky = scene->getComponent<SkyComponent>(entity);
            if (sky.loaded) {
                insertKeys(ShaderType::SKYBOX, 0, effectiveShaderId(sky.customShader, ShaderType::SKYBOX));
            }
        }
    }

    if (scene->isSSAOEnabled()) {
        keys.insert(ShaderPool::getShaderKey(ShaderType::SSAO, 0));
        keys.insert(ShaderPool::getShaderKey(ShaderType::SSAO_BLUR, 0));
    }

    if (scene->isSSREnabled()) {
        keys.insert(ShaderPool::getShaderKey(ShaderType::SSR, 0));
        keys.insert(ShaderPool::getShaderKey(ShaderType::SSR_BLUR, 0));
        keys.insert(ShaderPool::getShaderKey(ShaderType::COMPOSITE, 0));
    }

    // disabled passes ship too: the generated scene carries the whole chain
    for (const PostProcessPass& pass : scene->getPostProcessPasses()) {
        insertKeys(ShaderType::POSTPROCESS, 0, ShaderPool::registerCustomShader(pass.shader));
    }

    // fixed resolution upscales with it and a scene stack is presented with it
    keys.insert(ShaderPool::getShaderKey(ShaderType::BLIT, 0));

    // 2D lights with shadows render occluder segments through the shadow2d
    // pass. The runtime additionally gates on an enabled Occluder2D existing
    // (RenderSystem::loadLights2D), but scripts can spawn occluders at runtime,
    // so a shadow-casting light alone is enough to require the shader.
    {
        auto lights2d = scene->getComponentArray<Light2DComponent>();
        for (int i = 0; i < (int)lights2d->size(); i++) {
            if (lights2d->getComponentFromIndex(i).shadows) {
                keys.insert(ShaderPool::getShaderKey(ShaderType::SHADOW2D, 0));
                break;
            }
        }
    }
}

void editor::Project::buildStandaloneShaderCache() {
    // the generated main.cpp registers every scene stack, so any of them can be loaded
    std::set<ShaderKey> shaderKeys;
    for (const auto& sceneProject : getScenes()) {
        shaderKeys.insert(sceneProject.shaderKeys.begin(), sceneProject.shaderKeys.end());
        // stored keys are only as new as the last save of that scene
        collectSceneShaderKeys(&sceneProject, shaderKeys);
    }

    ShaderBuilder builder;
    builder.buildMissingShaders(shaderKeys, this);
}

void editor::Project::invalidateCustomShaders() {
    // Drop the editor's compiled cache for forked shaders, free their GPU handles, and
    // flag every renderable that uses one so RenderSystem recompiles it on the next draw.
    editor::ShaderBuilder::invalidateCustomShaders();
    ShaderPool::destroyCustomShaders();

    for (auto& sceneProject : getScenes()) {
        if (!sceneProject.scene)
            continue;
        Scene* scene = sceneProject.scene;
        // a component rides on a fork either via its own customShader or the scene default
        bool sceneMesh = !scene->getDefaultMeshShader().empty();
        bool sceneUI = !scene->getDefaultUIShader().empty();
        bool scenePoints = !scene->getDefaultPointsShader().empty();
        bool sceneLines = !scene->getDefaultLinesShader().empty();
        bool sceneSky = !scene->getDefaultSkyShader().empty();

        // needReload is only consumed while the scene draws, and the idle loop skips
        // scenes that are not flagged, so the redraw has to be requested here. Saving a
        // shader file is not a command, so nothing else would request it.
        auto flagReload = [&sceneProject](bool& needReload, bool usesFork) {
            if (!usesFork)
                return;
            needReload = true;
            sceneProject.needUpdateRender = true;
        };

        for (Entity entity : sceneProject.entities) {
            if (MeshComponent* mesh = scene->findComponent<MeshComponent>(entity))
                flagReload(mesh->needReload, sceneMesh || !mesh->customShader.empty());
            if (UIComponent* ui = scene->findComponent<UIComponent>(entity))
                flagReload(ui->needReload, sceneUI || !ui->customShader.empty());
            if (PointsComponent* pts = scene->findComponent<PointsComponent>(entity))
                flagReload(pts->needReload, scenePoints || !pts->customShader.empty());
            if (LinesComponent* ln = scene->findComponent<LinesComponent>(entity))
                flagReload(ln->needReload, sceneLines || !ln->customShader.empty());
            if (SkyComponent* sky = scene->findComponent<SkyComponent>(entity))
                flagReload(sky->needReload, sceneSky || !sky->customShader.empty());
        }

        // post-process passes hold their shader directly, so the chain is rebuilt whole
        if (!scene->getPostProcessPasses().empty()) {
            scene->getSystem<RenderSystem>()->needReloadPostProcess();
            sceneProject.needUpdateRender = true;
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

    entry.runtime->scene->getSystem<MeshSystem>()->setFoliagePreviewEntity(NULL_ENTITY);

    Entity camera = getSceneCamera(entry.runtime);
    entry.runtime->scene->setCamera(camera);

    // play reuses the edit-time Scene without a reload, so re-arm reflection
    // probes: "On Load" (and static capture-once) probes should capture the
    // game's starting state, not keep the edit-mode capture
    auto probes = entry.runtime->scene->getComponentArray<ReflectionProbeComponent>();
    for (size_t i = 0; i < probes->size(); ++i) {
        ReflectionProbeComponent& probe = probes->getComponentFromIndex(i);
        probe.needUpdate = true;
        probe.captureRevision++;
    }

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

    std::vector<PlayRuntimeScene> runtimeScenes;
    {
        std::scoped_lock lock(playSessionMutex);
        runtimeScenes.swap(session->runtimeScenes);
    }

    for (const auto& entry : runtimeScenes) {
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
                scenes.pop_back();
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
                    float walkSpeedOffset = 0.0f;
                    Stream::decodeEditorCamera(editorCam, targetScene->editorCameraState, zoom, walkSpeedOffset);
                    if ((targetScene->sceneType == SceneType::SCENE_2D || targetScene->sceneType == SceneType::SCENE_UI) && zoom > 0.0f) {
                        static_cast<SceneRender2D*>(targetScene->sceneRender)->setZoom(zoom);
                    }
                    if (targetScene->sceneType == SceneType::SCENE_3D) {
                        static_cast<SceneRender3D*>(targetScene->sceneRender)->setWalkSpeedOffset(walkSpeedOffset);
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

            editor::getEditorHost().addNewSceneToDock(targetScene->id);
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

        if (opened && !isNewScene) {
            removeMissingChildSceneReferences(*targetScene);
        }

    } catch (const YAML::Exception& e) {
        if (isNewScene && !scenes.empty()) scenes.pop_back();
        Out::error("Failed to open scene: %s", e.what());
        editor::getEditorHost().registerAlert("Error", "Failed to open scene file!");
    } catch (const std::exception& e) {
        if (isNewScene && !scenes.empty()) scenes.pop_back();
        Out::error("Failed to open scene: %s", e.what());
        editor::getEditorHost().registerAlert("Error", "Failed to open scene file!");
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
    editor::getEditorHost().registerConfirmAlert(
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
        scenes.erase(it);
    } else {
        it->opened = false;
        it->expandedInline = false;
    }

    editor::getEditorHost().clearSceneWindowState(sceneId);

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
    editor::getEditorHost().clearSceneWindowState(sceneId);
    scenes.erase(it);

    if (startSceneId == sceneId) {
        startSceneId = NULL_PROJECT_SCENE;
    }
}

std::vector<std::filesystem::path> editor::Project::findSceneFiles() const {
    return findProjectFiles(&Util::isSceneFile);
}

uint32_t editor::Project::findSceneByPath(const std::filesystem::path& filepath) const {
    const fs::path normalized = normalizeToProjectRelative(filepath);
    if (normalized.empty()) {
        return NULL_PROJECT_SCENE;
    }

    for (const SceneProject& sceneProject : scenes) {
        if (sceneProject.filepath.lexically_normal() == normalized) {
            return sceneProject.id;
        }
    }

    return NULL_PROJECT_SCENE;
}

void editor::Project::markParentScenesNeedUpdate(uint32_t childSceneId) {
    bool foundParent = false;

    for (auto& s : scenes) {
        auto& cs = s.childScenes;
        if (findChildScene(cs, childSceneId) != cs.end()) {
            s.needUpdateRender = true;
            foundParent = true;
        }
    }

    if (foundParent) {
        // The inline child layers feed SceneRender::activate(), so force a re-activation.
        editor::getEditorHost().resetLastActivatedScene();
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

        removeMissingChildSceneReferences(*childScene);

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

void editor::Project::addChildScene(uint32_t sceneId, uint32_t childSceneId, bool startActive) {
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
    if (findChildScene(childScenes, childSceneId) != childScenes.end()) {
        Out::warning("Child scene '%s' already exists in scene '%s'", childScene->name.c_str(), sceneProject->name.c_str());
        return;
    }

    childScenes.push_back({childSceneId, startActive});
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
    auto it = findChildScene(childScenes, childSceneId);
    if (it != childScenes.end()) {
        childScenes.erase(it);
        sceneProject->isModified = true;
        sceneProject->needUpdateRender = true;
        // Dropped an inline child, so the Engine layers need rebuilding.
        editor::getEditorHost().resetLastActivatedScene();

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
    return findChildScene(childScenes, childSceneId) != childScenes.end();
}

bool editor::Project::isChildSceneStartActive(uint32_t sceneId, uint32_t childSceneId) const {
    const SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return true;
    }

    auto it = findChildScene(sceneProject->childScenes, childSceneId);
    return it == sceneProject->childScenes.end() ? true : it->startActive;
}

void editor::Project::setChildSceneStartActive(uint32_t sceneId, uint32_t childSceneId, bool startActive) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Scene with ID %u not found", sceneId);
        return;
    }

    auto it = findChildScene(sceneProject->childScenes, childSceneId);
    if (it == sceneProject->childScenes.end()) {
        Out::error("Child scene with ID %u not found in scene '%s'", childSceneId, sceneProject->name.c_str());
        return;
    }

    if (it->startActive == startActive) {
        return;
    }

    it->startActive = startActive;
    sceneProject->isModified = true;
}

std::vector<uint32_t> editor::Project::getChildScenes(uint32_t sceneId) const {
    const SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return {};
    }
    std::vector<uint32_t> childSceneIds;
    childSceneIds.reserve(sceneProject->childScenes.size());
    for (const ChildSceneRef& childScene : sceneProject->childScenes) {
        childSceneIds.push_back(childScene.id);
    }
    return childSceneIds;
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
            float zoom = 0.0f;
            float walkSpeedOffset = 0.0f;
            if (sceneProject->sceneType == SceneType::SCENE_2D || sceneProject->sceneType == SceneType::SCENE_UI) {
                zoom = static_cast<SceneRender2D*>(sceneProject->sceneRender)->getZoom();
            } else if (sceneProject->sceneType == SceneType::SCENE_3D) {
                walkSpeedOffset = static_cast<SceneRender3D*>(sceneProject->sceneRender)->getWalkSpeedOffset();
            }
            sceneProject->editorCameraState = Stream::encodeEditorCamera(editorCam, zoom, walkSpeedOffset);
        }
    }

    if (sceneProject->scene) {
        if (auto meshSystem = sceneProject->scene->getSystem<MeshSystem>())
            meshSystem->cancelAsyncModelLoads();
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
    Engine::setMouseCursor(CursorType::ARROW);
    Engine::setMouseMode(MouseMode::NORMAL);

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
    standaloneBundles.clear();
    editor::getEditorHost().resetLastActivatedScene();

    // A project may be closed, edited externally, then reopened in the same editor
    // process. Do not carry its dependency snapshots or compiled custom data across
    // that boundary.
    editor::ShaderBuilder::invalidateCustomShaders();

    // Scenes are gone: drop unreferenced pool assets from the previous project.
    // Keep entries still held by the editor/engine (fonts, active shaders, etc.).
    Engine::clearUnusedPools();

    // Reset state
    name = "";
    canvasWidth = defaultCanvasWidth;
    canvasHeight = defaultCanvasHeight;
    scalingMode = defaultScalingMode;
    textureStrategy = defaultTextureStrategy;
    vsyncEnabled = defaultVSyncEnabled;
    windowMode = defaultWindowMode;
    windowWidth = defaultWindowWidth;
    windowHeight = defaultWindowHeight;
    windowResizable = defaultWindowResizable;
    windowTitle = defaultWindowTitle;
    windowIcon.clear();
    assetsDir = defaultAssetsDir;
    luaDir = defaultLuaDir;
    scriptDirs.clear();
    cmakeCCompiler = "";
    cmakeCxxCompiler = "";
    cmakeGenerator = "";
    cmakeBuildJobs = defaultCMakeBuildJobs;
    packNativeResources = defaultPackNativeResources;
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

    editor::getEditorHost().updateWindowTitle(name);

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

            fs::path sourcePath = script.path;
            if (sourcePath.is_relative()) {
                sourcePath = getProjectPath() / sourcePath;
            }
            if (sourcePath.empty() || !fs::exists(sourcePath)) {
                continue;
            }

            SceneScriptSource merged = script;

            // scene data can predate the last header edit, and only one scene contributes each script
            fs::path headerPath = merged.headerPath;
            if (headerPath.is_relative()) {
                headerPath = getProjectPath() / headerPath;
            }
            if (!merged.headerPath.empty()) {
                if (!fs::exists(headerPath)) {
                    continue;
                }
                merged.properties = toScriptPropertyInfos(ScriptParser::parseScriptProperties(headerPath));
            }

            mergedScripts.push_back(std::move(merged));
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

            fs::path fullPath = bundle.bundlePath;
            if (fullPath.is_relative()) {
                fullPath = getProjectPath() / fullPath;
            }
            if (!fs::exists(fullPath)) {
                continue;
            }

            result.push_back(bundle);
        }
    }

    // Standalone bundles have no scene recording them, and only the loaded ones get a source
    for (const fs::path& bundlePath : standaloneBundles) {
        const EntityBundle* bundle = getEntityBundle(bundlePath);
        if (!bundle || !bundle->registry || !uniquePaths.insert(bundlePath.generic_string()).second) {
            continue;
        }

        BundleSceneInfo info;
        info.bundlePath = bundlePath;
        info.functionName = Factory::bundleToFunctionName(bundlePath);
        result.push_back(std::move(info));
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
        std::filesystem::path engineApiSource = FileUtils::getEngineDir();

        if (!std::filesystem::exists(engineApiSource)) {
            Out::warning("engine folder not found at: %s", engineApiSource.string().c_str());
            return;
        }

        std::filesystem::path engineApiDest = getProjectInternalPath() / "engine-api";

        // Create internal path if it doesn't exist
        if (!std::filesystem::exists(getProjectInternalPath())) {
            std::filesystem::create_directories(getProjectInternalPath());
        }

        int updatedFiles = 0;

        // Sync only files whose contents actually changed so reopening the editor
        // does not touch header timestamps and force a rebuild of game scripts.
        for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(engineApiSource)) {
            if (dirEntry.is_regular_file()) {
                auto ext = dirEntry.path().extension().string();
                auto relPath = std::filesystem::relative(dirEntry.path(), engineApiSource);

                // The native application backends are compiled by the project's
                // own build (see Generator::getPlatformCMakeConfig), so their
                // implementations travel with the snapshot; everything else is
                // header-only API.
                const bool isPlatformSource =
                    !relPath.empty() && relPath.begin()->string() == "platform" &&
                    (ext == ".cpp" || ext == ".mm" || ext == ".m");

                if (ext == ".h" || ext == ".hpp" || ext == ".inl" || ext == ".glsl" || ext == ".frag" || ext == ".vert" || isPlatformSource) {
                    auto destPath = engineApiDest / relPath;
                    std::filesystem::create_directories(destPath.parent_path());

                    std::ifstream sourceFile(dirEntry.path(), std::ios::in | std::ios::binary);
                    if (!sourceFile) {
                        Out::warning("Failed to read engine API source file: %s", dirEntry.path().string().c_str());
                        continue;
                    }

                    std::string sourceContent(
                        (std::istreambuf_iterator<char>(sourceFile)),
                        std::istreambuf_iterator<char>());

                    if (FileUtils::writeIfChanged(destPath, sourceContent)) {
                        updatedFiles++;
                    }
                }
            }
        }

        Out::info("Synced engine API in project: %s (%d files updated)", engineApiDest.string().c_str(), updatedFiles);

    } catch (const std::exception& e) {
        Out::error("Failed to copy engine API: %s", e.what());
    }
}

void editor::Project::finalizeStart(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene>& runtimeScenes) {
    std::vector<uint32_t> activeSceneIds;
    if (mainSceneProject) {
        collectStartActiveScenes(mainSceneProject->id, activeSceneIds);
    }

    for (auto& entry : runtimeScenes) {
        SceneProject* sceneProject = entry.runtime;
        if (!sceneProject || !sceneProject->scene) {
            continue;
        }

        prepareRuntimeScene(entry);

        bool isActiveScene = std::find(activeSceneIds.begin(), activeSceneIds.end(), entry.sourceSceneId) != activeSceneIds.end();
        if (sceneProject != mainSceneProject && isActiveScene) {
            Engine::addSceneLayer(sceneProject->scene);
        }
    }

    Engine::pauseGameEvents(false);
    Engine::setCanvasSize(canvasWidth, canvasHeight);
    Engine::setScalingMode(scalingMode);
    Engine::setTextureStrategy(textureStrategy);
    Engine::onViewLoaded.call();
    Engine::systemViewChanged();

    if (mainSceneProject) {
        mainSceneProject->playState = ScenePlayState::PLAYING;
        mainSceneProject->needUpdateRender = true;
        editor::getEditorHost().requestScenePlayFocus(mainSceneProject->id);
        editor::getEditorHost().resetLastActivatedScene();
        Out::success("Scene '%s' started", mainSceneProject->name.c_str());
    }
}

void editor::Project::finalizeStop(SceneProject* mainSceneProject, std::vector<PlayRuntimeScene> runtimeScenes) {
    bool saveMainOnStop = false;
    bool keepMainModified = false;

    for (const auto& entry : runtimeScenes) {
        SceneProject* sceneProject = entry.runtime;
        if (!sceneProject || !sceneProject->scene) {
            continue;
        }

        bool saveOnStop = false;
        bool keepModified = false;

        pauseEngineScene(sceneProject->scene, true);
        sceneProject->scene->getSystem<UISystem>()->setAnchorReferenceSize(canvasWidth, canvasHeight);

        // Destroy all bundle instances created during play before restoring snapshot
        BundleManager::destroyAllInstances(sceneProject->scene);

        // Stop scene audio before restoring snapshot to prevent stale SoLoud handles on the next play
        sceneProject->scene->getSystem<AudioSystem>()->stopSceneSounds();

        // Collect shader keys while components are still loaded (before snapshot restore wipes loaded state).
        // Merge newly discovered keys into the editor-side scene and mark it modified so the file is saved.
        if (entry.sourceSceneId != NULL_PROJECT_SCENE) {
            std::set<ShaderKey> playKeys;
            collectSceneShaderKeys(sceneProject, playKeys);
            if (!playKeys.empty()) {
                SceneProject* editorScene = entry.ownedRuntime ? getScene(entry.sourceSceneId) : sceneProject;
                if (editorScene) {
                    const bool wasModified = editorScene->isModified;
                    const size_t prevSize = editorScene->shaderKeys.size();
                    editorScene->shaderKeys.insert(playKeys.begin(), playKeys.end());
                    if (editorScene->shaderKeys.size() > prevSize) {
                        editorScene->isModified = true;
                        saveOnStop = true;
                        keepModified = wasModified;

                        if (mainSceneProject && entry.sourceSceneId == mainSceneProject->id) {
                            saveMainOnStop = true;
                            keepMainModified = wasModified;
                        }
                    }
                }
            }
        }

        // Restore snapshot if present
        if (sceneProject->playStateSnapshot && !sceneProject->playStateSnapshot.IsNull()) {
            Stream::decodeScene(sceneProject->scene, sceneProject->playStateSnapshot["scene"]);

            // entities are still live, so the decode also removes components added during play
            auto entitiesNode = sceneProject->playStateSnapshot["entities"];
            size_t insertAt = 0;
            for (const auto& entityNode : entitiesNode) {
                std::vector<Entity> restored = Stream::decodeEntity(entityNode, sceneProject->scene, nullptr, nullptr, sceneProject, NULL_ENTITY, false, true);

                // A script can destroy entities while playing and the decode recreates them, but
                // only the tracked list makes them visible to the editor again
                for (Entity entity : restored) {
                    auto it = std::find(sceneProject->entities.begin(), sceneProject->entities.end(), entity);
                    if (it != sceneProject->entities.end()) {
                        insertAt = std::distance(sceneProject->entities.begin(), it) + 1;
                        continue;
                    }

                    insertAt = std::min(insertAt, sceneProject->entities.size());
                    sceneProject->entities.insert(sceneProject->entities.begin() + insertAt, entity);
                    insertAt++;
                }
            }

            // snapshot decode leaves camera-linked textures unresolved (no framebuffer)
            CameraTextureLink::resolve(sceneProject->scene);

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
            editor::getEditorHost().enqueueMainThreadTask([this, sceneProject, entry, saveOnStop, keepModified]() {
                SceneProject* editorScene = entry.ownedRuntime ? getScene(entry.sourceSceneId) : sceneProject;
                if (saveOnStop && editorScene && !editorScene->filepath.empty()) {
                    saveLoadedSceneOnStop(sceneProject, editorScene, keepModified);
                }

                Engine::removeScene(sceneProject->scene);

                // Delete runtime clones that aren't opened in the editor.
                if (entry.ownedRuntime) {
                    deleteSceneProject(sceneProject);
                    delete sceneProject;
                }
            });
        }
    }

    editor::getEditorHost().enqueueMainThreadTask([]() {
        SceneManager::clearAll();
        BundleManager::clearAll();
        editor::getEditorHost().resetLastActivatedScene();
    });

    resetEngineConfigs(true);

    if (mainSceneProject) {
        Out::success("Scene '%s' stopped", mainSceneProject->name.c_str());
        if (saveMainOnStop && !mainSceneProject->filepath.empty()) {
            uint32_t mainSceneId = mainSceneProject->id;
            editor::getEditorHost().enqueueMainThreadTask([this, mainSceneId, keepMainModified]() {
                SceneProject* sceneProject = getScene(mainSceneId);
                if (sceneProject) {
                    saveLoadedSceneOnStop(sceneProject, sceneProject, keepMainModified);
                }
            });
        }
    }
}

bool editor::Project::createTempProject(std::string projectName, bool deleteIfExists) {
    if (isAnyScenePlaying()) {
        Out::warning("Cannot create a new project while a scene is running or stopping.");
        return false;
    }

    try {
        editor::getEditorHost().prepareForProjectSwitch();

        resetConfigs();

        // Clear the last project path in settings when creating a new temp project
        AppSettings::setLastProjectPath(std::filesystem::path());

        projectPath = std::filesystem::temp_directory_path() / projectName;
        fs::path projectFile = projectPath / "project.yaml";

        // Set the project name so libName (used for the generated CMake target
        // and project()) is populated. Without this a freshly created temp
        // project has an empty name, producing an invalid CMakeLists.txt.
        setName(projectName);

        // Inherit the compiler chosen in a previous session so new temp projects
        // don't silently fall back to the Default toolchain every time. Drop a
        // stale compiler path that no longer exists on disk.
        {
            std::string cc = AppSettings::getLastCMakeCCompiler();
            std::string cxx = AppSettings::getLastCMakeCxxCompiler();
            std::string gen = AppSettings::getLastCMakeGenerator();
            if (!cxx.empty() && !fs::exists(cxx)) {
                cc.clear(); cxx.clear(); gen.clear();
            }
            if (!cc.empty() || !cxx.empty() || !gen.empty()) {
                setCMakeKit(cc, cxx, gen);
            }
        }

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

        editor::getEditorHost().updateResourcesPath();

    } catch (const std::exception& e) {
        printf("Error: %s\n", e.what());
        // prepareForProjectSwitch() suspends background resource work.
        // Always release that suspension even when project creation fails.
        editor::getEditorHost().updateResourcesPath();
        return false;
    }

    return true;
}

bool editor::Project::saveProject(bool userCalled, std::function<void()> callback) {
    if (isTempProject() && userCalled) {
        editor::getEditorHost().registerProjectSaveDialog(callback);
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
            editor::getEditorHost().registerAlert("Error", "Failed to create project directory!");
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
            editor::getEditorHost().registerAlert("Error", "Failed to move project files to the new location!");
            return false;
        }
    }

    if (!saveProjectFile()) {
        return false;
    }

    if (!isTempProject()){
        AppSettings::setLastProjectPath(path);
    }
    editor::getEditorHost().updateResourcesPath();

    return true;
}

bool editor::Project::loadProject(const std::filesystem::path path, bool updateLastOpened) {
    if (isAnyScenePlaying()) {
        Out::warning("Cannot load a project while a scene is running or stopping.");
        return false;
    }

    try {
        if (!std::filesystem::exists(path)) {
            Out::error("Project directory does not exist: \"%s\"", path.string().c_str());
            return false;
        }

        std::filesystem::path projectFile = path / "project.yaml";
        if (!std::filesystem::exists(projectFile)) {
            Out::error("Project file does not exist: %s", projectFile.string().c_str());
            return false;
        }

        // Load and parse project file
        YAML::Node projectNode = YAML::LoadFile(projectFile.string());

        editor::getEditorHost().prepareForProjectSwitch();

        resetConfigs();
        projectPath = path;

        Stream::decodeProject(this, projectNode);

        // Guarantee a non-empty name: project.yaml files without a "name" field
        // (older or hand-authored projects) would otherwise leave libName empty
        // and generate an invalid CMakeLists.txt. Fall back to the folder name.
        if (getName().empty()) {
            setName(path.filename().string());
        }

        // Create a default scene if no scenes were loaded
        if (scenes.empty()) {
            createNewScene("New Scene", SceneType::SCENE_3D);
        }

        for (auto& sceneProject : scenes) {
            if (sceneProject.opened) {
                removeMissingChildSceneReferences(sceneProject);
            }
        }

        if (editor::getEditorHost().shouldSyncEngineApi()) {
            copyEngineApiToProject();
        }

        editor::getEditorHost().updateResourcesPath();

        // Save this as the last opened project
        if (updateLastOpened && !isTempProject()) {
            AppSettings::setLastProjectPath(projectPath);
        }

        clearTrash();

        Out::info("Project loaded successfully: \"%s\"", projectPath.string().c_str());
        return true;

    } catch (const YAML::Exception& e) {
        Out::error("Failed to load project YAML: \"%s\"", e.what());
        editor::getEditorHost().registerAlert("Error", "Failed to load project file!");
        editor::getEditorHost().updateResourcesPath();
        return false;
    } catch (const std::exception& e) {
        Out::error("Failed to load project: \"%s\"", e.what());
        editor::getEditorHost().registerAlert("Error", "Failed to load project!");
        editor::getEditorHost().updateResourcesPath();
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
        editor::getEditorHost().registerAlert("Error", "The selected directory is not a valid project. No project.yaml file found!");
        return false;
    }

    if (loadProject(projectDir)) {
        return true;
    } else {
        Out::error("Failed to open project: \"%s\"", projectDir.string().c_str());
        editor::getEditorHost().registerAlert("Error", "Failed to open project!");
        return false;
    }
}

bool editor::Project::saveLoadedSceneOnStop(SceneProject* loadedSceneProject, SceneProject* editorScene, bool keepModified) {
    if (!loadedSceneProject || !editorScene || editorScene->filepath.empty()) {
        return true;
    }

    bool success = false;
    if (loadedSceneProject == editorScene) {
        success = saveSceneFile(editorScene, editorScene->filepath, false);
        if (success) {
            editorScene->isModified = keepModified;
        }
    } else {
        loadedSceneProject->shaderKeys = editorScene->shaderKeys;
        success = saveSceneFile(loadedSceneProject, editorScene->filepath, false);
        if (success) {
            editorScene->filepath = loadedSceneProject->filepath;
            editorScene->shaderKeys = loadedSceneProject->shaderKeys;
            editorScene->isModified = keepModified;
        }
    }

    if (success) {
        Out::info("Auto-saved scene '%s' on stop", editorScene->name.c_str());
    }

    return success;
}

void editor::Project::saveScene(uint32_t sceneId, std::function<void(bool)> callback) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        Out::error("Cannot save scene - invalid scene ID: %u", sceneId);
        if (callback) callback(false);
        return;
    }

    if (sceneProject->playState != ScenePlayState::STOPPED) {
        Out::warning("Cannot save scene '%s' while it is busy.", sceneProject->name.c_str());
        if (callback) callback(false);
        return;
    }

    auto finishWithChildren = [this, sceneId, callback](bool success) {
        if (!success) {
            if (callback) callback(false);
            return;
        }

        saveModifiedChildScenes(sceneId, callback);
    };

    if (!sceneProject->scene) {
        finishWithChildren(true);
        return;
    }

    if (!sceneProject->filepath.empty()) {
        saveSceneToPathAsync(sceneId, sceneProject->filepath, finishWithChildren);
    } else {
        editor::getEditorHost().registerSaveSceneDialog(sceneId, [finishWithChildren]() {
            finishWithChildren(true);
        });
    }
}

void editor::Project::saveSceneListSequentially(std::vector<uint32_t> sceneIds, std::function<void(bool)> callback) {
    auto ids = std::make_shared<std::vector<uint32_t>>(std::move(sceneIds));
    auto index = std::make_shared<size_t>(0);
    auto saveNext = std::make_shared<std::function<void(bool)>>();
    *saveNext = [this, ids, index, callback, saveNext](bool previousSuccess) {
        if (!previousSuccess) {
            if (callback) callback(false);
            return;
        }

        while (*index < ids->size()) {
            uint32_t id = (*ids)[(*index)++];
            if (hasSceneUnsavedChanges(id)) {
                saveScene(id, *saveNext);
                return;
            }
        }

        if (callback) callback(true);
    };

    (*saveNext)(true);
}

void editor::Project::saveModifiedChildScenes(uint32_t sceneId, std::function<void(bool)> callback) {
    std::vector<uint32_t> involvedIds;
    collectInvolvedScenes(sceneId, involvedIds);

    std::vector<uint32_t> childIds;
    for (uint32_t id : involvedIds) {
        if (id == sceneId) continue;
        const SceneProject* childScene = getScene(id);
        if (childScene && hasLocalUnsavedChanges(id)) {
            childIds.push_back(id);
        }
    }

    saveSceneListSequentially(std::move(childIds), std::move(callback));
}

bool editor::Project::saveSceneFile(SceneProject* sceneProject, const std::filesystem::path& path, bool stopTransientPreviews) {
    if (!sceneProject || path.empty()) {
        return false;
    }

    if (stopTransientPreviews) {
        editor::getEditorHost().stopTransientPreviews();
    }

    fs::path fullPath = path;
    if (fullPath.is_relative()) {
        fullPath = getProjectPath() / fullPath;
    }

    std::error_code ec;
    fs::path relPath = fs::relative(fullPath, getProjectPath(), ec);
    if (ec || relPath.empty()) {
        Out::error("Scene filepath must be relative to project path: %s", path.string().c_str());
        return false;
    }

    for (const auto& [filepath, bundle] : entityBundles) {
        if (bundle.instances.find(sceneProject->id) != bundle.instances.end() && bundle.isModified) {
            saveEntityBundleToDisk(filepath);
        }
    }

    SceneMaxValues maxValues;
    maxValues.maxBones = sceneProject->maxValues.maxBones;
    calculateSceneMaxValues(sceneProject, maxValues);
    sceneProject->maxValues = maxValues;

    std::set<ShaderKey> shaderKeys;
    collectSceneShaderKeys(sceneProject, shaderKeys);
    // Merge with existing keys so those from components not yet rendered by RenderSystem are preserved.
    shaderKeys.insert(sceneProject->shaderKeys.begin(), sceneProject->shaderKeys.end());
    sceneProject->shaderKeys = std::move(shaderKeys);

    updateSceneCppScripts(sceneProject);
    updateSceneBundles(sceneProject);

    try {
        YAML::Node root = Stream::encodeSceneProject(this, sceneProject);
        std::ofstream fout(fullPath.string());
        if (!fout) {
            Out::error("Failed to open scene file for writing: %s", fullPath.string().c_str());
            return false;
        }

        fout << YAML::Dump(root);
        fout.close();

        sceneProject->filepath = relPath;
        // Terrain map writes are asynchronous; when any failed to reach disk the
        // save is incomplete even though the scene YAML is written: keep the
        // scene dirty so quit/open flows still prompt, and report failure so
        // save-and-quit flows don't proceed to close while sculpt data exists
        // only in memory. The next save retries the writes.
        if (!TerrainEditWindow::cleanUnusedTerrainMaps(this)){
            sceneProject->isModified = true;
            return false;
        }
        sceneProject->isModified = false;
    } catch (const std::exception& e) {
        Out::error("Failed to save scene file: %s", e.what());
        return false;
    }

    return true;
}

bool editor::Project::saveSceneToPath(uint32_t sceneId, const std::filesystem::path& path, bool stopTransientPreviews) {
    if (!writeSceneToPath(sceneId, path, stopTransientPreviews)) {
        return false;
    }

    return saveProject();
}

bool editor::Project::writeSceneToPath(uint32_t sceneId, const std::filesystem::path& path, bool stopTransientPreviews) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return false;
    }

    fs::path fullPath = path;
    if (fullPath.is_relative()) {
        fullPath = getProjectPath() / fullPath;
    }

    std::string oldFilepath = sceneProject->filepath.string();
    if (!saveSceneFile(sceneProject, path, stopTransientPreviews)) {
        return false;
    }

    // Update tabs: if filepath changed, update existing tab entry
    if (sceneProject->opened) {
        if (!oldFilepath.empty() && oldFilepath != sceneProject->filepath.string()) {
            removeTab(TabType::SCENE, oldFilepath);
        }
        addTab(TabType::SCENE, sceneProject->filepath.string());
    }

    std::vector<BundleInstanceInfo> bundleInstances = generator.writeBundleSources(entityBundles, sceneId, getProjectPath(),getProjectInternalPath());
    generator.writeSceneSource(sceneProject->scene, sceneProject->name, sceneProject->entities, getSceneCamera(sceneProject), getProjectPath(), getProjectInternalPath(), bundleInstances);

    std::vector<editor::SceneBuildInfo> scenesToConfig;
    for (SceneProject& sceneConf : scenes) {
        bool isMain = (sceneId == sceneConf.id);
        std::vector<uint32_t> involvedSceneIds;
        std::vector<uint32_t> activeSceneIds;
        collectInvolvedScenes(sceneConf.id, involvedSceneIds);
        collectStartActiveScenes(sceneConf.id, activeSceneIds);

        scenesToConfig.push_back({sceneConf.id, sceneConf.name, involvedSceneIds, activeSceneIds, isMain});
    }

    std::vector<SceneScriptSource> mergedCppScripts = collectAllSceneCppScripts();
    std::vector<BundleSceneInfo> bundleBuildInfos = collectAllBundles();
    generator.configure(scenesToConfig, libName, mergedCppScripts, bundleBuildInfos, getProjectPath(), getProjectInternalPath(), getAssetsPath(), getLuaPath(), getScriptDirs(), scalingMode, textureStrategy, canvasWidth, canvasHeight, vsyncEnabled, getWindowSettings());

    Out::info("Scene saved to: \"%s\"", fullPath.string().c_str());

    return true;
}

void editor::Project::saveSceneToPathAsync(uint32_t sceneId, const std::filesystem::path& path, std::function<void(bool)> callback) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        if (callback) callback(false);
        return;
    }

    if (sceneProject->playState != ScenePlayState::STOPPED) {
        Out::warning("Cannot save scene '%s' while it is busy.", sceneProject->name.c_str());
        if (callback) callback(false);
        return;
    }

    editor::getEditorHost().stopTransientPreviews();

    sceneProject->playState = ScenePlayState::SAVING;
    fs::path savePath = path;

    std::thread([this, sceneId, savePath, callback = std::move(callback)]() mutable {
        bool success = false;
        try {
            success = writeSceneToPath(sceneId, savePath, false);
        } catch (const std::exception& e) {
            Out::error("Failed to save scene in background: %s", e.what());
        }

        editor::getEditorHost().enqueueMainThreadTask([this, sceneId, success, callback = std::move(callback)]() mutable {
            if (success) {
                success = saveProject();
            }

            if (SceneProject* sceneProject = getScene(sceneId)) {
                if (sceneProject->playState == ScenePlayState::SAVING) {
                    sceneProject->playState = ScenePlayState::STOPPED;
                }
            }

            if (callback) callback(success);
        });
    }).detach();
}

void editor::Project::saveAllScenes(std::function<void(bool)> callback) {
    std::vector<uint32_t> sceneIds;
    for (auto& sceneProject : scenes) {
        if (hasSceneUnsavedChanges(sceneProject.id)) {
            sceneIds.push_back(sceneProject.id);
        }
    }

    saveSceneListSequentially(std::move(sceneIds), std::move(callback));
}

void editor::Project::saveLastSelectedScene(std::function<void(bool)> callback){
    saveScene(selectedScene, callback);
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
        if (!signature.test(scene->getComponentId<PolygonComponent>()) && signature.test(scene->getComponentId<UILayoutComponent>())){
            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
            if (layout.width > 0 && layout.height > 0){
                Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                Transform& transform = scene->getComponent<Transform>(entity);
                aabb = transform.modelMatrix * AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
            }
        }
    }else if (signature.test(scene->getComponentId<LightComponent>()) ||
              signature.test(scene->getComponentId<Light2DComponent>()) ||
              signature.test(scene->getComponentId<CameraComponent>()) ||
              signature.test(scene->getComponentId<SoundComponent>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        float size = 16.0f;
        SceneRender* editorRender = nullptr;
        for (const SceneProject& sceneProject : scenes){
            if (sceneProject.scene == mainScene){
                editorRender = sceneProject.sceneRender;
                break;
            }
        }
        if (editorRender){
            size = editorRender->billboardScreenScale(transform.worldPosition, 16.0f);
        }else{
            Transform& camtransform = mainScene->getComponent<Transform>(mainScene->getCamera());
            CameraComponent& camera = mainScene->getComponent<CameraComponent>(mainScene->getCamera());
            if (camera.type == CameraType::CAMERA_ORTHO){
                float zoom = (camera.topClip - camera.bottomClip) / std::max(1.0f, (float)Engine::getCanvasHeight());
                size = 16.0f * zoom;
            }else{
                float dist = (transform.worldPosition - camtransform.worldPosition).length();
                size = dist * tan(camera.yfov) * 0.01f;
            }
        }
        aabb = transform.modelMatrix * AABB(-size, -size, -size, size, size, size);
    }else if (signature.test(scene->getComponentId<Occluder2DComponent>())){
        const Occluder2DComponent& occluder = scene->getComponent<Occluder2DComponent>(entity);
        Transform& transform = scene->getComponent<Transform>(entity);

        // hug the polygon points; the entity origin is only the fallback when
        // there is nothing else to bound
        AABB localAABB(Vector3(0, 0, -1), Vector3(0, 0, 1));
        if (occluder.shape == Occluder2DShape::POLYGON && occluder.points.size() > 0){
            localAABB.setNull();
            for (const Vector2& point : occluder.points){
                localAABB.merge(Vector3(point.x, point.y, 0.0f));
            }
            Vector3 mn = localAABB.getMinimum(); mn.z = -1.0f;
            Vector3 mx = localAABB.getMaximum(); mx.z = 1.0f;
            localAABB.setExtents(mn, mx);
        }
        aabb = transform.modelMatrix * localAABB;
    }else if (signature.test(scene->getComponentId<PointsComponent>()) || signature.test(scene->getComponentId<LinesComponent>())){
        if (signature.test(scene->getComponentId<Transform>())) {
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

            if (signature.test(scene->getComponentId<PointsComponent>())){
                const PointsComponent& pts = scene->getComponent<PointsComponent>(entity);
                for (const PointData& pt : pts.points) {
                    if (!pt.visible) continue;
                    mergeWorldPoint(transform.modelMatrix * pt.position);
                }
            }

            if (signature.test(scene->getComponentId<LinesComponent>())){
                const LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
                for (const LineData& line : lines.lines) {
                    mergeWorldPoint(transform.modelMatrix * line.pointA);
                    mergeWorldPoint(transform.modelMatrix * line.pointB);
                }
            }
        }
    }

    return aabb;
}

AABB editor::Project::getEntityLocalAABB(Scene* scene, Entity entity) const{
    AABB aabb;
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<MeshComponent>())){
        aabb = SceneRender::getMeshLocalAABB(scene->getComponent<MeshComponent>(entity));
    }else if (signature.test(scene->getComponentId<UIComponent>())){
        aabb = scene->getComponent<UIComponent>(entity).aabb;
        if (!signature.test(scene->getComponentId<PolygonComponent>()) && signature.test(scene->getComponentId<UILayoutComponent>())){
            UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
            if (layout.width > 0 && layout.height > 0){
                Vector2 center = GraphicUtils::getUILayoutCenter(scene, entity, layout);
                aabb = AABB(-center.x, -center.y, 0, layout.width-center.x, layout.height-center.y, 0);
            }
        }
    }else if (signature.test(scene->getComponentId<LightComponent>()) ||
              signature.test(scene->getComponentId<Light2DComponent>()) ||
              signature.test(scene->getComponentId<CameraComponent>()) ||
              signature.test(scene->getComponentId<SoundComponent>())){
        aabb = AABB::ZERO;
    }else if (signature.test(scene->getComponentId<Occluder2DComponent>())){
        const Occluder2DComponent& occluder = scene->getComponent<Occluder2DComponent>(entity);
        aabb.setNull();
        if (occluder.shape == Occluder2DShape::POLYGON && occluder.points.size() > 0){
            // hug the polygon points only
            for (const Vector2& point : occluder.points){
                aabb.merge(Vector3(point.x, point.y, 0.0f));
            }
        }else{
            aabb.merge(Vector3::ZERO); // fallback: the entity origin
        }
    }else if (signature.test(scene->getComponentId<PointsComponent>()) || signature.test(scene->getComponentId<LinesComponent>())){
        aabb.setNull();
        // Always include the entity origin (local space zero)
        aabb.merge(Vector3::ZERO);
        if (signature.test(scene->getComponentId<PointsComponent>())){
            const PointsComponent& pts = scene->getComponent<PointsComponent>(entity);
            for (const PointData& pt : pts.points) {
                if (!pt.visible) continue;
                aabb.merge(pt.position);
            }
        }
        if (signature.test(scene->getComponentId<LinesComponent>())){
            const LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
            for (const LineData& line : lines.lines) {
                aabb.merge(line.pointA);
                aabb.merge(line.pointB);
            }
        }
    }

    return aabb;
}

Entity editor::Project::findBestEntityByRay(const std::vector<Entity>& entities, Scene* scene, const Ray& ray, Scene* mainScene, SceneType sceneType, float& distance, size_t& index) const{
    Entity selEntity = NULL_ENTITY;
    Vector3 rayDirection = ray.getDirection();
    float rayLengthSq = rayDirection.dotProduct(rayDirection);
    if (rayLengthSq <= std::numeric_limits<float>::epsilon()) return NULL_ENTITY;

    for (auto& entity : entities) {
        Signature signature = scene->getSignature(entity);
        if (!signature.test(scene->getComponentId<Transform>())) continue;

        if (signature.test(scene->getComponentId<TerrainComponent>())) {
            TerrainComponent& terrain = scene->getComponent<TerrainComponent>(entity);
            Transform& transform = scene->getComponent<Transform>(entity);
            Vector3 worldPoint;
            if (!scene->getSystem<MeshSystem>()->raycastTerrainSurface(ray, terrain, transform, worldPoint)) continue;

            float terrainDistance = (worldPoint - ray.getOrigin()).dotProduct(rayDirection) / rayLengthSq;
            if (terrainDistance < 0.0f || terrainDistance > 1.0f) continue;

            size_t nIndex = scene->getComponentArray<Transform>()->getIndex(entity);
            if (terrainDistance < distance || (nIndex >= index && sceneType != SceneType::SCENE_3D)){
                distance = terrainDistance;
                index = nIndex;
                selEntity = entity;
            }
            continue;
        }

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

    for (const ChildSceneRef& childSceneRef : scenedata->childScenes) {
        uint32_t childId = childSceneRef.id;
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
    for (const ChildSceneRef& childSceneRef : scenedata->childScenes) {
        uint32_t childId = childSceneRef.id;
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
        if (SceneProject* previousScene = getScene(this->selectedScene)){
            if (previousScene->sceneRender){
                previousScene->sceneRender->getUILayer()->setCameraGaugeVisible(false);
            }
        }

        this->selectedScene = selectedScene;
        this->selectedSceneForProperties = selectedScene;

        // A newly selected scene must draw at least once.
        if (SceneProject* sceneProject = getScene(selectedScene)){
            sceneProject->needUpdateRender = true;
        }

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

    // relative() returns empty across different roots, like a project on D: while temp is on C:
    if (ec || relPath.empty()) {
        return false;
    }

    return *relPath.begin() != "..";
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
    // Painted maps are referenced like any other texture, so they live under the
    // assets root and ship with it.
    return getAssetsPath() / "terrain_maps";
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

    fs::path relativePath;
    if (!projectPath.empty()) {
        relativePath = resolvedPath.lexically_relative(projectPath.lexically_normal());
    }

    std::string hashInput = relativePath.empty() ? resolvedPath.generic_string() : relativePath.generic_string();

    std::error_code ec;
    const auto fileSize = fs::file_size(resolvedPath, ec);
    if (!ec) {
        hashInput += "_" + std::to_string(static_cast<uint64_t>(fileSize));
    }

    ec.clear();
    const auto modTime = fs::last_write_time(resolvedPath, ec);
    if (!ec) {
        hashInput += "_" + std::to_string(static_cast<int64_t>(modTime.time_since_epoch().count()));
    }

    // Hash the combined string
    std::string hash = SHA1::hash(hashInput);

    std::string thumbFilename = hash + ".thumb.png";
    return thumbsDir / thumbFilename;
}

std::vector<Entity> editor::Project::getEntities(uint32_t sceneId) const{
    return getScene(sceneId)->entities;
}

void editor::Project::replaceSelectedEntities(uint32_t sceneId, std::vector<Entity> selectedEntities){
    // Ctrl-click deselect reaches the selection only through here.
    getScene(sceneId)->needUpdateRender = true;
    getScene(sceneId)->selectedEntities = selectedEntities;
}

void editor::Project::setSelectedEntity(uint32_t sceneId, Entity selectedEntity){
    // The selection outline/gizmo is rendered, so a selection change needs a redraw.
    getScene(sceneId)->needUpdateRender = true;
    std::vector<Entity>& entities = getScene(sceneId)->selectedEntities;

    entities.clear();
    if (selectedEntity != NULL_ENTITY){
        entities.push_back(selectedEntity);
    }
}

void editor::Project::addSelectedEntity(uint32_t sceneId, Entity selectedEntity){
    getScene(sceneId)->needUpdateRender = true;
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
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject->selectedEntities.empty()){
        sceneProject->needUpdateRender = true;
    }
    sceneProject->selectedEntities.clear();
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

bool editor::Project::hasLocalUnsavedChanges(uint32_t sceneId) const{
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

    return false;
}

bool editor::Project::hasSceneUnsavedChangesImpl(uint32_t sceneId, std::unordered_set<uint32_t>& visited) const{
    if (!visited.insert(sceneId).second){
        return false;
    }

    const SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject){
        return false;
    }

    if (hasLocalUnsavedChanges(sceneId)){
        return true;
    }

    for (const ChildSceneRef& childSceneRef : sceneProject->childScenes) {
        if (hasSceneUnsavedChangesImpl(childSceneRef.id, visited)) {
            return true;
        }
    }

    return false;
}

bool editor::Project::hasSceneUnsavedChanges(uint32_t sceneId) const{
    std::unordered_set<uint32_t> visited;
    return hasSceneUnsavedChangesImpl(sceneId, visited);
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

bool editor::Project::updateScriptProperties(SceneProject* sceneProject, Entity entity, std::vector<ScriptEntry>& scripts, const std::string& inMemoryContent, const std::string& inMemoryPath) {
    bool hasChanges = false;

    // Update properties for each script in the component
    for (auto& scriptEntry : scripts) {
        // C++ scripts: keep existing behavior
        if (scriptEntry.type == ScriptType::CPP) {

            if (scriptEntry.headerPath.empty()) continue;

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

            hasChanges |= mergeScriptProperties(scriptEntry.properties, parsedProperties);

            continue;
        }

        // Lua scripts: load properties from Lua file
        if (scriptEntry.type == ScriptType::LUA) {
            if (scriptEntry.path.empty()) continue;

            fs::path fullPath = resolveLuaPath(scriptEntry.path);

            ScriptEntry parsedEntry = scriptEntry;

            if (!inMemoryContent.empty() && fullPath.string() == fs::path(inMemoryPath).string()) {
                ProjectUtils::loadLuaScriptPropertiesFromString(parsedEntry, inMemoryContent, fullPath.string());
            } else {
                ProjectUtils::loadLuaScriptProperties(parsedEntry, fullPath.string());
            }

            hasChanges |= mergeScriptProperties(scriptEntry.properties, parsedEntry.properties);

            continue;
        }
    }

    if (hasChanges) {
        sceneProject->isModified = true;
    }

    return hasChanges;
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
    } else {
        Out::error("createEntityBundle(%s): entity count mismatch: branchEntities=%zu, regEntities=%zu",
            filepath.string().c_str(), branchEntities.size(), regEntities.size());
        // Without a member mapping every ref would keep a scene-local ID; store
        // None rather than letting stale IDs into the registry
        remapEntityProperties(newGroup.registry.get(), regEntities, {});
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

    std::unordered_set<Entity> ignoredEntities(branchEntities.begin(), branchEntities.end());
    scene->setEntityName(rootEntity, ProjectUtils::makeUniqueEntityName(scene, sceneProject->entities, rootName, ignoredEntities));
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

    // References to entities outside the bundle stayed on the scene members while the
    // registry stored None; keep and persist them by overriding those components.
    overrideExternalRefComponents(scene, newInstance);

    newGroup.instances[sceneId].push_back(std::move(newInstance));

    entityBundles.emplace(filepath, std::move(newGroup));

    // Set up event subscriptions for this shared group
    saveEntityBundleToDisk(filepath);

    updateSceneBundles(sceneProject);

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

    for (SceneProject& sceneProject : scenes) {
        updateSceneBundles(&sceneProject);
    }

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
        ProjectUtils::reconcileTrackedEntities(otherScene->scene, otherScene->entities, this, otherSceneId);
    }
    for (uint32_t staleId : staleBundleScenes) {
        cleanupEntityBundlesForScene(staleId);
    }

    bundle->isModified = true;

    return recovery;
}

const std::vector<std::filesystem::path>& editor::Project::getStandaloneBundles() const {
    return standaloneBundles;
}

bool editor::Project::isStandaloneBundle(const std::filesystem::path& filepath) const {
    const std::string key = filepath.generic_string();
    for (const fs::path& bundlePath : standaloneBundles) {
        if (bundlePath.generic_string() == key) {
            return true;
        }
    }
    return false;
}

// Loads the listed bundles, dropping the ones whose file is gone or cannot be read
void editor::Project::setStandaloneBundles(std::vector<std::filesystem::path> bundlePaths) {
    standaloneBundles = std::move(bundlePaths);

    for (auto it = entityBundles.begin(); it != entityBundles.end();) {
        if (it->second.instances.empty() && !isStandaloneBundle(it->first)) {
            it = entityBundles.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = standaloneBundles.begin(); it != standaloneBundles.end();) {
        const fs::path fullPath = getProjectPath() / *it;
        std::error_code ec;

        if (!entityBundles.count(*it) && fs::exists(fullPath, ec)) {
            EntityBundle bundle;
            bundle.registry = std::make_unique<EntityRegistry>();

            try {
                YAML::Node node = YAML::LoadFile(fullPath.string());
                Stream::decodeEntitySelection(node, bundle.registry.get(), &bundle.registryEntities);
                entityBundles.emplace(*it, std::move(bundle));
            } catch (const std::exception& e) {
                Out::error("Failed to load entity bundle: %s", e.what());
            }
        }

        if (!entityBundles.count(*it)) {
            Out::warning("Entity bundle \"%s\" is not available, removed from the standalone bundles", it->string().c_str());
            it = standaloneBundles.erase(it);
            continue;
        }

        ++it;
    }
}

std::vector<std::filesystem::path> editor::Project::findProjectFiles(const std::function<bool(const std::string&)>& matches) const {
    std::vector<fs::path> files;
    if (projectPath.empty()) {
        return files;
    }

    std::error_code ec;

    for (fs::recursive_directory_iterator it(projectPath, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const fs::directory_entry& entry = *it;

        // '.doriax' holds the engine copy and the generated resources, 'build' the artifacts
        const std::string filename = entry.path().filename().string();
        if (filename.rfind('.', 0) == 0 || filename == "build") {
            if (entry.is_directory(ec)) it.disable_recursion_pending();
            continue;
        }

        if (!entry.is_regular_file(ec) || !matches(entry.path().string())) {
            continue;
        }

        fs::path relativePath = normalizeToProjectRelative(entry.path());
        if (!relativePath.empty() && !relativePath.is_absolute()) {
            files.push_back(std::move(relativePath));
        }
    }

    std::sort(files.begin(), files.end());

    return files;
}

std::vector<std::filesystem::path> editor::Project::findBundleFiles() const {
    return findProjectFiles(&Util::isBundleFile);
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

std::vector<Entity> editor::Project::importEntityBundle(SceneProject* sceneProject, std::vector<Entity>* entities, const std::filesystem::path& filepath, Entity rootEntity, bool needSaveScene, const YAML::Node& bundleOverrides, const YAML::Node& bundleLocalEntities, std::unordered_map<Entity, Entity>* entityRemap) {
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
    struct LoadingPathGuard {
        std::unordered_set<std::string>& paths;
        const std::string& path;
        ~LoadingPathGuard() { paths.erase(path); }
    } loadingGuard{loadingPaths, pathStr};

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
        // Without a member mapping every ref would keep a registry ID; store None
        // rather than letting it resolve to an arbitrary entity in this scene
        remapEntityProperties(scene, newEntities, {});
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
            std::vector<Entity> nestedEntities = importEntityBundle(sceneProject, entities, bundleComp->path, ent, false, nestedOvr, nestedLoc, entityRemap);
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

            std::vector<Entity> decoded = Stream::decodeEntity(localEntNode, scene, entities, this, sceneProject,
                parentEntity, true, false, entityRemap);
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

    updateSceneBundles(sceneProject);

    sceneProject->isModified = needSaveScene;

    return allResult;
}

void editor::Project::removeBundleInstanceTracking(uint32_t sceneId, Entity rootEntity) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return;
    }

    Scene* scene = sceneProject->scene;
    std::unordered_set<Entity> visitedEntities;
    auto removeBundleInstance = [&](auto&& self, Entity entity) -> void {
        if (entity == NULL_ENTITY || !scene->isEntityCreated(entity) ||
            !visitedEntities.insert(entity).second) {
            return;
        }

        const BundleComponent* component = scene->findComponent<BundleComponent>(entity);
        EntityBundle* nestedBundle = component ? getEntityBundle(component->path) : nullptr;
        if (!nestedBundle) {
            return;
        }

        std::vector<Entity> nestedMembers;
        auto sceneIt = nestedBundle->instances.find(sceneId);
        if (sceneIt != nestedBundle->instances.end()) {
            auto instance = std::find_if(sceneIt->second.begin(), sceneIt->second.end(),
                [entity](const EntityBundle::Instance& candidate) {
                    return candidate.rootEntity == entity;
                });
            if (instance != sceneIt->second.end()) {
                nestedMembers.reserve(instance->members.size());
                for (const EntityBundle::EntityMember& member : instance->members) {
                    nestedMembers.push_back(member.localEntity);
                }
            }
        }

        for (Entity member : nestedMembers) {
            self(self, member);
        }

        sceneIt = nestedBundle->instances.find(sceneId);
        if (sceneIt != nestedBundle->instances.end()) {
            auto& instances = sceneIt->second;
            instances.erase(std::remove_if(instances.begin(), instances.end(),
                [entity](const EntityBundle::Instance& candidate) {
                    return candidate.rootEntity == entity;
                }), instances.end());
            if (instances.empty()) {
                nestedBundle->instances.erase(sceneIt);
            }
        }
    };

    removeBundleInstance(removeBundleInstance, rootEntity);

    updateSceneBundles(sceneProject);
}

bool editor::Project::unimportEntityBundle(uint32_t sceneId, const std::filesystem::path& filepath, Entity rootEntity, const std::vector<Entity>& memberEntities) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) {
        return false;
    }

    Scene* scene = sceneProject->scene;

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

    // Drop bundle metadata recursively before any corresponding scene entity disappears.
    // The additional flat pass covers nested bundle roots stored as scene-local children
    // rather than members of their containing bundle.
    removeBundleInstanceTracking(sceneId, rootEntity);
    if (EntityBundle* rootBundle = getEntityBundle(filepath)) {
        auto sceneIt = rootBundle->instances.find(sceneId);
        if (sceneIt != rootBundle->instances.end()) {
            auto& instances = sceneIt->second;
            instances.erase(std::remove_if(instances.begin(), instances.end(),
                [rootEntity](const EntityBundle::Instance& instance) {
                    return instance.rootEntity == rootEntity;
                }), instances.end());
            if (instances.empty()) {
                rootBundle->instances.erase(sceneIt);
            }
        }
    }
    for (Entity entity : memberEntities) {
        removeBundleInstanceTracking(sceneId, entity);
    }
    for (Entity entity : allEntitiesToDestroy) {
        removeBundleInstanceTracking(sceneId, entity);
    }

    // Destroy local entities first (they may reference members as parents)
    DeleteEntityCmd::destroyEntities(scene, allEntitiesToDestroy,
        sceneProject->entities, this, sceneId);

    // Destroy all imported member entities
    DeleteEntityCmd::destroyEntities(scene, memberEntities,
        sceneProject->entities, this, sceneId);

    // Destroy root entity
    if (rootEntity != NULL_ENTITY && scene->isEntityCreated(rootEntity)) {
        DeleteEntityCmd::destroyEntity(scene, rootEntity, sceneProject->entities, this, sceneId);
    }

    updateSceneBundles(sceneProject);

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

    // Remap the newly decoded registry entities: references to an existing member or
    // to the new subtree translate to registry IDs; anything outside the bundle is
    // cleared to None so a scene-local ID never leaks into the shared registry.
    {
        std::unordered_map<Entity, Entity> localToRegistry;
        if (const EntityBundle::Instance* srcInstance = bundle->getInstanceById(sceneId, instanceId)) {
            for (const auto& member : srcInstance->members) {
                localToRegistry[member.localEntity] = member.registryEntity;
            }
        }
        std::vector<Entity> srcBranch;
        ProjectUtils::collectEntities(nodeOriginal, srcBranch);
        if (srcBranch.size() == regEntities.size()) {
            for (size_t i = 0; i < regEntities.size(); i++) {
                localToRegistry[srcBranch[i]] = regEntities[i];
            }
        }
        remapEntityProperties(bundle->registry.get(), regEntities, localToRegistry);
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

                if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || createItself) {
                    // Freshly decoded from the registry: translate registry IDs to this
                    // instance's local IDs (references outside the bundle stay None).
                    std::unordered_map<Entity, Entity> registryToLocal;
                    for (const auto& member : instance.members) {
                        registryToLocal[member.registryEntity] = member.localEntity;
                    }
                    remapEntityProperties(otherScene->scene, newOtherEntities, registryToLocal);
                } else {
                    // Current instance kept its original scene references; persist any
                    // that point outside the bundle by overriding those components.
                    overrideExternalRefComponents(otherScene->scene, instance);
                }
            }

            otherScene->isModified = true;
        }
    }

    bundle->isModified = true;

    return true;
}

bool editor::Project::addEntityToBundle(uint32_t sceneId, const NodeRecovery& recoveryData, Entity parent,
        bool createItself, std::unordered_map<Entity, Entity>* entityRemap, std::vector<Entity>* restoredEntities){
    fs::path filepath = findEntityBundlePathFor(sceneId, parent);
    if (filepath.empty()) {
        Out::error("Entity parent %u in scene %u is not part of any entity bundle", parent, sceneId);
        return false;
    }

    EntityBundle* bundle = getEntityBundle(filepath);
    if (!bundle) {
        return false;
    }
    bool wasBundleModified = bundle->isModified;
    bool deferSourceRemap = entityRemap && restoredEntities;

    Entity registryParent = bundle->getRegistryEntity(sceneId, parent);

    // Recover registry data
    std::string recoveryDefKey = std::to_string(NULL_PROJECT_SCENE);
    YAML::Node nodeRegData;
    size_t regTransformIndex = 0;
    if (auto recoveryIt = recoveryData.find(recoveryDefKey); recoveryIt != recoveryData.end()) {
        nodeRegData = recoveryIt->second.node;
        regTransformIndex = recoveryIt->second.transformIndex;
    }

    if (!nodeRegData) {
        Out::error("No registry data in recovery for addEntityToBundle");
        return false;
    }

    std::unordered_map<Entity, Entity> recoveredRegistryIds;
    std::vector<Entity> regEntities = Stream::decodeEntity(nodeRegData, bundle->registry.get(),
        &bundle->registryEntities, nullptr, nullptr, NULL_ENTITY, true, false, &recoveredRegistryIds);
    if (regEntities.empty()) {
        Out::error("Could not recover registry data for entity bundle");
        return false;
    }
    remapEntityProperties(bundle->registry.get(), bundle->registryEntities, recoveredRegistryIds, false);
    bool regHasTransform = bundle->registry->getSignature(regEntities[0]).test(bundle->registry->getComponentId<Transform>());
    ProjectUtils::moveEntityOrderByIndex(bundle->registry.get(), bundle->registryEntities, regEntities[0], registryParent, regTransformIndex, regHasTransform);
    uint32_t sourceInstanceId = bundle->getInstanceId(sceneId, parent);

    struct InstanceRecoveryTarget {
        uint32_t sceneId = NULL_PROJECT_SCENE;
        uint32_t instanceId = 0;
        Entity rootEntity = NULL_ENTITY;
    };

    std::vector<InstanceRecoveryTarget> recoveryTargets;
    for (const auto& [otherSceneId, sceneInstances] : bundle->instances) {
        for (const EntityBundle::Instance& instance : sceneInstances) {
            recoveryTargets.push_back({otherSceneId, instance.instanceId, instance.rootEntity});
        }
    }

    struct PendingInstanceRecovery {
        uint32_t sceneId = NULL_PROJECT_SCENE;
        uint32_t instanceId = 0;
        const NodeRecoveryEntry* recoveredEntry = nullptr;
        std::vector<Entity> entities;
        std::vector<EntityBundle::EntityMember> members;
        std::unordered_map<Entity, Entity> localRemap;
        Entity parent = NULL_ENTITY;
        size_t transformIndex = 0;
        bool decodeBranch = false;
    };

    std::vector<PendingInstanceRecovery> pendingRecoveries;
    std::map<uint32_t, bool> sceneModifiedStates;
    std::map<uint32_t, std::vector<Entity>> referenceEntitiesByScene;
    bool validRecovery = true;

    // Decode every instance first. Membership and override metadata are committed only after
    // all exact saved pairs have resolved, so a failed nested import can be rolled back cleanly.
    const std::unordered_set<Entity> recoveredRegistryEntities(regEntities.begin(), regEntities.end());
    for (const InstanceRecoveryTarget& target : recoveryTargets) {
        SceneProject* otherScene = getScene(target.sceneId);
        if (!otherScene || !otherScene->scene) {
            continue;
        }
        sceneModifiedStates.emplace(target.sceneId, otherScene->isModified);
        if (!deferSourceRemap || target.sceneId != sceneId) {
            referenceEntitiesByScene.try_emplace(target.sceneId, otherScene->entities);
        }

        EntityBundle::Instance* instance = bundle->getInstanceById(target.sceneId, target.instanceId);
        if (!instance) {
            validRecovery = false;
            break;
        }

        std::string recoveryKey = std::to_string(target.sceneId) + "_" + std::to_string(target.rootEntity);
        YAML::Node nodeData;
        size_t transformIndex = 0;
        const NodeRecoveryEntry* recoveredEntry = nullptr;

        auto recoveryIt = recoveryData.find(recoveryKey);
        if (recoveryIt != recoveryData.end()) {
            recoveredEntry = &recoveryIt->second;
            nodeData = recoveredEntry->node;
            transformIndex = recoveredEntry->transformIndex;
        } else {
            nodeData = Stream::encodeEntity(regEntities[0], bundle->registry.get());
        }

        // Resolve everything needed from the instance before decoding. A malformed circular
        // bundle can recursively append another instance of this bundle and reallocate its
        // instance vector, so no iterator, reference, or pointer may survive the decode.
        Entity otherParent = instance->rootEntity;
        if (registryParent != NULL_ENTITY) {
            for (const auto& member : instance->members) {
                if (member.registryEntity == registryParent) {
                    otherParent = member.localEntity;
                    break;
                }
            }
        }

        std::vector<Entity> newOtherEntities;
        bool isSourceInstance = target.sceneId == sceneId && target.instanceId == sourceInstanceId;

        bool decodeBranch = !isSourceInstance || createItself;
        std::unordered_map<Entity, Entity> recoveredLocalIds;
        if (decodeBranch) {
            newOtherEntities = Stream::decodeEntity(nodeData, otherScene->scene,
                &otherScene->entities, this, otherScene, NULL_ENTITY, true, false, &recoveredLocalIds);
        } else {
            ProjectUtils::collectEntities(nodeData, newOtherEntities);
        }

        const std::unordered_set<Entity> branchEntities(newOtherEntities.begin(), newOtherEntities.end());
        bool nestedBundlesMapped = true;
        if (recoveredEntry) {
            for (const NestedBundleRecovery& savedBundle : recoveredEntry->nestedBundles) {
                Entity nestedRoot = savedBundle.rootEntity;
                if (auto rootRemap = recoveredLocalIds.find(nestedRoot); rootRemap != recoveredLocalIds.end()) {
                    nestedRoot = rootRemap->second;
                }

                EntityBundle* nestedBundle = getEntityBundle(savedBundle.path);
                const EntityBundle::Instance* nestedInstance = nestedBundle ?
                    nestedBundle->getInstance(target.sceneId, nestedRoot) : nullptr;
                if (!nestedInstance || nestedInstance->rootEntity != nestedRoot ||
                    !otherScene->scene->isEntityCreated(nestedRoot) ||
                    (decodeBranch && !branchEntities.count(nestedRoot))) {
                    nestedBundlesMapped = false;
                    break;
                }

                for (const EntityBundle::EntityMember& savedMember : savedBundle.members) {
                    Entity registryEntity = savedMember.registryEntity;
                    if (nestedBundle == bundle) {
                        if (auto registryRemap = recoveredRegistryIds.find(registryEntity);
                            registryRemap != recoveredRegistryIds.end()) {
                            registryEntity = registryRemap->second;
                        }
                    }

                    auto restoredMember = std::find_if(nestedInstance->members.begin(), nestedInstance->members.end(),
                        [&](const EntityBundle::EntityMember& member) {
                            return member.registryEntity == registryEntity;
                        });
                    if (restoredMember == nestedInstance->members.end() ||
                        !otherScene->scene->isEntityCreated(restoredMember->localEntity) ||
                        (decodeBranch && !branchEntities.count(restoredMember->localEntity))) {
                        nestedBundlesMapped = false;
                        break;
                    }
                    if (restoredMember->localEntity != savedMember.localEntity) {
                        recoveredLocalIds[savedMember.localEntity] = restoredMember->localEntity;
                    }
                }
                if (!nestedBundlesMapped) {
                    break;
                }
            }
        }

        std::vector<EntityBundle::EntityMember> membersToRestore;
        if (recoveredEntry && !recoveredEntry->members.empty()) {
            membersToRestore.reserve(recoveredEntry->members.size());
            for (const EntityBundle::EntityMember& saved : recoveredEntry->members) {
                Entity localEntity = saved.localEntity;
                auto localRemap = recoveredLocalIds.find(localEntity);
                if (localRemap != recoveredLocalIds.end()) {
                    localEntity = localRemap->second;
                }

                Entity registryEntity = saved.registryEntity;
                auto registryRemap = recoveredRegistryIds.find(registryEntity);
                if (registryRemap != recoveredRegistryIds.end()) {
                    registryEntity = registryRemap->second;
                }

                if (branchEntities.count(localEntity) && recoveredRegistryEntities.count(registryEntity) &&
                    otherScene->scene->isEntityCreated(localEntity) &&
                    bundle->registry->isEntityCreated(registryEntity)) {
                    membersToRestore.push_back({localEntity, registryEntity});
                }
            }
        } else if (regEntities.size() == newOtherEntities.size()) {
            for (size_t i = 0; i < regEntities.size(); ++i) {
                membersToRestore.push_back({newOtherEntities[i], regEntities[i]});
            }
        }

        bool canPairMembers = nestedBundlesMapped && (recoveredEntry && !recoveredEntry->members.empty() ?
            membersToRestore.size() == recoveredEntry->members.size() :
            membersToRestore.size() == regEntities.size());
        pendingRecoveries.push_back({target.sceneId, target.instanceId, recoveredEntry,
            std::move(newOtherEntities), std::move(membersToRestore),
            std::move(recoveredLocalIds), otherParent, transformIndex,
            decodeBranch});
        if (!canPairMembers) {
            validRecovery = false;
            break;
        }
    }

    if (validRecovery) {
        validRecovery = std::all_of(pendingRecoveries.begin(), pendingRecoveries.end(),
            [&](const PendingInstanceRecovery& pending) {
                return bundle->getInstanceById(pending.sceneId, pending.instanceId) != nullptr;
            });
    }

    if (!validRecovery) {
        for (PendingInstanceRecovery& pending : pendingRecoveries) {
            if (!pending.decodeBranch) {
                continue;
            }
            SceneProject* pendingScene = getScene(pending.sceneId);
            if (!pendingScene || !pendingScene->scene) {
                continue;
            }
            for (Entity entity : pending.entities) {
                removeBundleInstanceTracking(pending.sceneId, entity);
                if (!pendingScene->scene->isEntityCreated(entity)) {
                    continue;
                }
                if (ActionComponent* action = pendingScene->scene->findComponent<ActionComponent>(entity)) {
                    action->ownedTarget = false;
                }
                if (AnimationComponent* animation = pendingScene->scene->findComponent<AnimationComponent>(entity)) {
                    animation->ownedActions = false;
                }
            }
            DeleteEntityCmd::destroyEntities(pendingScene->scene, pending.entities,
                pendingScene->entities, this, pending.sceneId);
        }

        std::unordered_map<Entity, Entity> reverseRegistryRemap;
        for (const auto& [oldEntity, newEntity] : recoveredRegistryIds) {
            reverseRegistryRemap[newEntity] = oldEntity;
        }
        remapEntityProperties(bundle->registry.get(), bundle->registryEntities,
            reverseRegistryRemap, false);
        DeleteEntityCmd::destroyEntities(bundle->registry.get(), regEntities, bundle->registryEntities);
        for (const auto& [otherSceneId, modified] : sceneModifiedStates) {
            if (SceneProject* otherScene = getScene(otherSceneId)) {
                otherScene->isModified = modified;
            }
        }
        bundle->isModified = wasBundleModified;
        Out::error("Could not pair recovered entity bundle members");
        return false;
    }

    std::map<uint32_t, std::unordered_map<Entity, Entity>> sceneEntityRemaps;
    for (PendingInstanceRecovery& pending : pendingRecoveries) {
        SceneProject* pendingScene = getScene(pending.sceneId);
        EntityBundle::Instance* instance = bundle->getInstanceById(pending.sceneId, pending.instanceId);
        if (!pendingScene || !pendingScene->scene || !instance) {
            continue;
        }
        Scene* otherScene = pendingScene->scene;

        if (!pending.entities.empty()) {
            bool hasTransform = otherScene->getSignature(pending.entities[0]).test(
                otherScene->getComponentId<Transform>());
            ProjectUtils::moveEntityOrderByIndex(otherScene, pendingScene->entities,
                pending.entities[0], pending.parent, pending.transformIndex, hasTransform);
        }

        instance->members.insert(instance->members.end(),
            pending.members.begin(), pending.members.end());

        if (entityRemap && pending.recoveredEntry && pending.sceneId == sceneId) {
            for (const auto& [oldEntity, newEntity] : pending.localRemap) {
                (*entityRemap)[oldEntity] = newEntity;
            }
        }
        if (pending.recoveredEntry) {
            for (const auto& [oldEntity, overrideMask] : pending.recoveredEntry->overrides) {
                Entity localEntity = oldEntity;
                auto localRemap = pending.localRemap.find(oldEntity);
                if (localRemap != pending.localRemap.end()) {
                    localEntity = localRemap->second;
                }
                if (otherScene->isEntityCreated(localEntity)) {
                    instance->overrides[localEntity] = overrideMask;
                }
            }
        }
        if (restoredEntities && pending.sceneId == sceneId) {
            restoredEntities->insert(restoredEntities->end(),
                pending.entities.begin(), pending.entities.end());
        }
        if (!deferSourceRemap || pending.sceneId != sceneId) {
            auto& referenceEntities = referenceEntitiesByScene[pending.sceneId];
            referenceEntities.insert(referenceEntities.end(), pending.entities.begin(), pending.entities.end());
        }

        if (pending.decodeBranch && !pending.recoveredEntry) {
            std::unordered_map<Entity, Entity> registryToLocal;
            for (const EntityBundle::EntityMember& member : instance->members) {
                registryToLocal[member.registryEntity] = member.localEntity;
            }
            remapEntityProperties(otherScene, pending.entities, registryToLocal);
        } else {
            if (pending.decodeBranch && !pending.localRemap.empty()) {
                auto& sceneRemap = sceneEntityRemaps[pending.sceneId];
                for (const auto& [oldEntity, newEntity] : pending.localRemap) {
                    sceneRemap[oldEntity] = newEntity;
                }
            }
            if (!pending.recoveredEntry) {
                overrideExternalRefComponents(otherScene, *instance);
            }
        }

        pendingScene->isModified = true;
    }

    for (const auto& [otherSceneId, entityMap] : sceneEntityRemaps) {
        if (deferSourceRemap && otherSceneId == sceneId) {
            continue;
        }
        SceneProject* otherScene = getScene(otherSceneId);
        auto references = referenceEntitiesByScene.find(otherSceneId);
        if (!otherScene || !otherScene->scene || references == referenceEntitiesByScene.end()) {
            continue;
        }
        std::vector<Entity> liveReferences;
        std::unordered_set<Entity> seenReferences;
        for (Entity entity : references->second) {
            if (otherScene->scene->isEntityCreated(entity) && seenReferences.insert(entity).second) {
                liveReferences.push_back(entity);
            }
        }
        remapEntityProperties(otherScene->scene, liveReferences, entityMap, false);
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

    // Registry entities behind members the engine cascade orphaned in the scene instances.
    // The shared registry has no systems and never cascades, so these are mirrored into the
    // registry removal below to keep the saved bundle definition consistent with the instances.
    std::unordered_set<Entity> cascadedRegistryEntities;

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

            const BundleComponent* nestedComponent = otherScene->scene->findComponent<BundleComponent>(otherEntity);
            const EntityBundle* nestedBundle = nestedComponent ? getEntityBundle(nestedComponent->path) : nullptr;
            const EntityBundle::Instance* nestedInstance = nestedBundle ?
                nestedBundle->getInstance(otherSceneId, otherEntity) : nullptr;
            bool isNestedBundleRoot = nestedInstance && nestedInstance->rootEntity == otherEntity;
            YAML::Node nodeExtend = Stream::encodeEntity(otherEntity, otherScene->scene,
                this, otherScene, !isNestedBundleRoot);

            std::vector<Entity> allEntities;
            ProjectUtils::collectEntities(nodeExtend, allEntities);

            // Project-aware snapshots keep nested bundle branches opaque. Expand their live
            // instance members and hierarchy here so the scene-side removal still destroys
            // the complete branch while recovery remains bundle-aware.
            std::unordered_set<Entity> collectedEntities(allEntities.begin(), allEntities.end());
            // Expansion records each imported root before appending its members, preserving
            // the parent-before-child order used to resolve deeper roots during recovery.
            std::vector<NestedBundleRecovery> nestedBundles;
            auto appendEntity = [&](Entity candidate) {
                if (candidate != NULL_ENTITY && otherScene->scene->isEntityCreated(candidate) &&
                    collectedEntities.insert(candidate).second) {
                    allEntities.push_back(candidate);
                }
            };
            auto transforms = otherScene->scene->getComponentArray<Transform>();
            for (size_t entityIndex = 0; entityIndex < allEntities.size(); ++entityIndex) {
                Entity branchEntity = allEntities[entityIndex];
                const BundleComponent* component = otherScene->scene->findComponent<BundleComponent>(branchEntity);
                const EntityBundle* childBundle = component ? getEntityBundle(component->path) : nullptr;
                const EntityBundle::Instance* childInstance = childBundle ?
                    childBundle->getInstance(otherSceneId, branchEntity) : nullptr;
                if (childInstance && childInstance->rootEntity == branchEntity) {
                    nestedBundles.push_back({component->path, branchEntity, childInstance->members});
                    for (const EntityBundle::EntityMember& member : childInstance->members) {
                        appendEntity(member.localEntity);
                    }
                }

                for (size_t transformIndex = 0; transformIndex < transforms->size(); ++transformIndex) {
                    if (transforms->getComponentFromIndex(transformIndex).parent == branchEntity) {
                        appendEntity(transforms->getEntity(transformIndex));
                    }
                }
            }

            if (otherScene->scene->findComponent<Transform>(otherEntity)) {
                transformIndex = ProjectUtils::getTransformIndex(otherScene->scene, otherEntity);
            } else {
                auto entityIt = std::find(otherScene->entities.begin(), otherScene->entities.end(), otherEntity);
                transformIndex = (entityIt != otherScene->entities.end()) ? std::distance(otherScene->entities.begin(), entityIt) : 0;
            }

            std::string recoveryKey = std::to_string(otherSceneId) + "_" + std::to_string(instance.rootEntity);
            NodeRecoveryEntry& recoveryEntry = recovery[recoveryKey];
            recoveryEntry = {nodeExtend, transformIndex};
            recoveryEntry.nestedBundles = std::move(nestedBundles);

            // Remove members from this instance
            for (const Entity& e : allEntities) {
                auto itRem = std::find_if(instance.members.begin(), instance.members.end(),
                    [e](const EntityBundle::EntityMember& member) {
                        return member.localEntity == e;
                    });
                if (itRem != instance.members.end()) {
                    recoveryEntry.members.push_back(*itRem);
                    instance.members.erase(itRem);
                }

                auto override = instance.overrides.find(e);
                if (override != instance.overrides.end()) {
                    recoveryEntry.overrides[e] = override->second;
                }
                instance.overrides.erase(e);
            }

            // Destroy entities in other instances (or in this one if destroyItself)
            if ((otherSceneId != sceneId) || (instance.instanceId != instanceId) || destroyItself) {
                // Remove nested-instance metadata and suppress authored ownership callbacks
                // before any component removal can cascade into a later entity in the branch.
                for (const Entity& entityToDestroy : allEntities) {
                    removeBundleInstanceTracking(otherSceneId, entityToDestroy);
                    if (ActionComponent* action = otherScene->scene->findComponent<ActionComponent>(entityToDestroy)) {
                        action->ownedTarget = false;
                    }
                    if (AnimationComponent* animation = otherScene->scene->findComponent<AnimationComponent>(entityToDestroy)) {
                        animation->ownedActions = false;
                    }
                }
                // DeleteEntityCmd captures ownership dependencies explicitly in the source
                // scene. Other instance overrides must not broaden that shared deletion.
                DeleteEntityCmd::destroyEntities(otherScene->scene, allEntities,
                    otherScene->entities, this, otherSceneId);
            }

            // A destroy above can cascade inside the engine and take other members of this
            // instance with it. Drop any such missing member (and its override entry) to keep
            // the bundle instance metadata in sync with the scene.
            for (auto memberIt = instance.members.begin(); memberIt != instance.members.end(); ) {
                if (!otherScene->scene->isEntityCreated(memberIt->localEntity)) {
                    if (destroyItself && memberIt->registryEntity != NULL_ENTITY) {
                        cascadedRegistryEntities.insert(memberIt->registryEntity);
                    }
                    instance.overrides.erase(memberIt->localEntity);
                    memberIt = instance.members.erase(memberIt);
                } else {
                    ++memberIt;
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

    // Mirror scene-side system cascades into the shared definition. The registry has no
    // systems and therefore does not perform the equivalent cleanup on its own.
    for (Entity regEntity : cascadedRegistryEntities) {
        registryEntitiesToRemove.push_back(regEntity);
    }

    DeleteEntityCmd::destroyEntities(bundle->registry.get(), registryEntitiesToRemove,
        bundle->registryEntities);

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
        if (it->second.instances.empty() && !isStandaloneBundle(it->first)) {
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

        // Build the source instance's member map (the only shareable reference targets).
        std::unordered_map<Entity, Entity> localToRegistry;
        std::unordered_set<Entity> memberLocals;
        if (const EntityBundle::Instance* sourceInstance = bundle->getInstanceById(sceneId, instanceId)) {
            for (const auto& member : sourceInstance->members) {
                localToRegistry[member.localEntity] = member.registryEntity;
                memberLocals.insert(member.localEntity);
            }
        }

        if (componentHasExternalEntityRef(getScene(sceneId)->scene, entity, componentType, memberLocals)) {
            // The component now points outside the bundle, so it is per-instance data.
            // Promote it to an override on this instance and leave the shared registry
            // (and the other instances) untouched.
            bundle->setComponentOverride(sceneId, entity, componentType);
        } else {
            if (properties.size() == 0){
                Catalog::copyComponent(getScene(sceneId)->scene, entity, registry, registryEntity, componentType);
            }else{
                for (const auto& property : properties) {
                    Catalog::copyPropertyValue(getScene(sceneId)->scene, entity, registry, registryEntity, componentType, property);
                }
            }
            // Remap entity references from scene-local IDs to registry IDs.
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

                        // Instances that hold their own external reference on this component
                        // are overridden, so they are skipped here and keep their wiring.
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

    // Skip if already visited - also guards against cycles in the child-scene graph
    if (std::find(involvedSceneIds.begin(), involvedSceneIds.end(), sceneId) != involvedSceneIds.end()) {
        return;
    }
    involvedSceneIds.push_back(sceneId);

    for (const ChildSceneRef& childSceneRef : sceneProject->childScenes) {
        collectInvolvedScenes(childSceneRef.id, involvedSceneIds);
    }
}

void editor::Project::collectStartActiveScenes(uint32_t sceneId, std::vector<uint32_t>& activeSceneIds) {
    SceneProject* sceneProject = getScene(sceneId);
    if (!sceneProject) return;

    if (std::find(activeSceneIds.begin(), activeSceneIds.end(), sceneId) == activeSceneIds.end()) {
        activeSceneIds.push_back(sceneId);
    }

    for (const ChildSceneRef& childSceneRef : sceneProject->childScenes) {
        if (childSceneRef.startActive) {
            collectStartActiveScenes(childSceneRef.id, activeSceneIds);
        }
    }
}

bool editor::Project::isAnyScenePlaying() const{
    if (isPlaySessionActive()) {
        return true;
    }

    for (const auto& sceneProject : scenes) {
        if (sceneProject.playState != ScenePlayState::STOPPED) {
            return true;
        }
    }
    return false;
}

bool editor::Project::isPlaySessionActive() const{
    std::scoped_lock lock(playSessionMutex);
    return activePlaySession != nullptr;
}

bool editor::Project::isMainScenePlaying() const{
    uint32_t mainSceneId;
    {
        std::scoped_lock lock(playSessionMutex);
        if (!activePlaySession) {
            return false;
        }
        mainSceneId = activePlaySession->mainSceneId;
    }
    for (const auto& sceneProject : scenes) {
        if (sceneProject.id == mainSceneId) {
            return sceneProject.playState == ScenePlayState::PLAYING;
        }
    }
    return false;
}

bool editor::Project::isAnySceneSaving() const{
    for (const auto& sceneProject : scenes) {
        if (sceneProject.playState == ScenePlayState::SAVING) {
            return true;
        }
    }
    return false;
}

void editor::Project::failPlayStartup(const std::shared_ptr<PlaySession>& session, uint32_t sceneId, const std::string& message,
                                      const std::string& alertTitle, const std::string& alertMessage) {
    editor::getEditorHost().enqueueMainThreadTask([this, session, sceneId, message, alertTitle, alertMessage]() {
        if (session->cancelled.load(std::memory_order_acquire)) {
            session->startupThreadDone.store(true, std::memory_order_release);
            return;
        }

        if (!message.empty()) {
            Out::error("%s", message.c_str());
        }

        cleanupPlaySession(session);
        SceneManager::clearAll();
        BundleManager::clearAll();
        doriax::FunctionSubscribeGlobal::getCrashHandler() = nullptr;

        if (SceneProject* sceneProject = getScene(sceneId)) {
            sceneProject->playState = ScenePlayState::STOPPED;
        }

        {
            std::scoped_lock lock(playSessionMutex);
            if (activePlaySession == session) {
                activePlaySession.reset();
            }
        }

        session->startupThreadDone.store(true, std::memory_order_release);

        if (!alertTitle.empty()) {
            editor::getEditorHost().registerAlert(alertTitle, alertMessage);
        }
    });
}

bool editor::Project::saveSceneForPlayStartup(SceneProject* sceneProject) {
    if (!sceneProject || sceneProject->filepath.empty()) {
        return false;
    }

    return saveSceneFile(sceneProject, sceneProject->filepath, false);
}

void editor::Project::runPlayStartup(const std::shared_ptr<PlaySession>& session, uint32_t sceneId) {
    auto isCancelled = [session]() {
        return session->cancelled.load(std::memory_order_acquire);
    };

    auto markStartupDone = [session]() {
        session->startupThreadDone.store(true, std::memory_order_release);
    };

    try {
        SceneProject* mainSceneProject = getScene(sceneId);
        if (!mainSceneProject) {
            failPlayStartup(session, sceneId, "Failed to find scene to start");
            return;
        }

        std::vector<uint32_t> involvedMainSceneIds;
        collectInvolvedScenes(sceneId, involvedMainSceneIds);

        std::vector<editor::SceneBuildInfo> scenesToGenerate;
        for (SceneProject& currentSceneProject : scenes) {
            if (isCancelled()) {
                markStartupDone();
                return;
            }

            bool isInvolvedScene = std::find(involvedMainSceneIds.begin(), involvedMainSceneIds.end(), currentSceneProject.id) != involvedMainSceneIds.end();

            if (currentSceneProject.opened || (isInvolvedScene && currentSceneProject.scene)) {
                updateAllScriptsProperties(currentSceneProject.id);

                if (currentSceneProject.isModified && !currentSceneProject.filepath.empty()) {
                    if (!saveSceneForPlayStartup(&currentSceneProject)) {
                        failPlayStartup(session, sceneId, "Failed to save modified scene before play startup");
                        return;
                    }
                } else {
                    updateSceneCppScripts(&currentSceneProject);
                    updateSceneBundles(&currentSceneProject);
                }
            }

            if (isInvolvedScene) {
                PlayRuntimeScene entry;
                entry.sourceSceneId = currentSceneProject.id;

                if (currentSceneProject.id == sceneId) {
                    entry.runtime = &currentSceneProject;
                    entry.ownedRuntime = false;
                } else {
                    entry.runtime = createRuntimeCloneFromSource(&currentSceneProject);
                    entry.ownedRuntime = true;
                }

                if (!entry.runtime) {
                    failPlayStartup(session, sceneId, "Failed to create runtime scene for play startup");
                    return;
                }

                if (isCancelled()) {
                    if (entry.ownedRuntime) {
                        deleteSceneProject(entry.runtime);
                        delete entry.runtime;
                    }
                    markStartupDone();
                    return;
                }

                std::vector<BundleInstanceInfo> bundleInstances = generator.writeBundleSources(entityBundles, currentSceneProject.id, getProjectPath(), getProjectInternalPath());
                generator.writeSceneSource(entry.runtime->scene, entry.runtime->name, entry.runtime->entities, getSceneCamera(entry.runtime), getProjectPath(), getProjectInternalPath(), bundleInstances);

                {
                    std::scoped_lock lock(playSessionMutex);
                    session->runtimeScenes.push_back(entry);
                }
            }

            bool isMain = (sceneId == currentSceneProject.id);
            std::vector<uint32_t> involvedSceneIds;
            std::vector<uint32_t> activeSceneIds;
            collectInvolvedScenes(currentSceneProject.id, involvedSceneIds);
            collectStartActiveScenes(currentSceneProject.id, activeSceneIds);
            scenesToGenerate.push_back({currentSceneProject.id, currentSceneProject.name, involvedSceneIds, activeSceneIds, isMain});
        }

        if (isCancelled()) {
            markStartupDone();
            return;
        }

        registerSceneManager();
        registerBundleManager();

        YAML::Node playSnapshot = Stream::encodeSceneProject(nullptr, mainSceneProject);

        if (isCancelled()) {
            markStartupDone();
            return;
        }

        std::vector<SceneScriptSource> mergedCppScripts = collectAllSceneCppScripts();
        std::vector<BundleSceneInfo> bundleBuildInfos = collectAllBundles();
        const unsigned int requestedBuildJobs = cmakeBuildJobs.load();
        generator.configure(scenesToGenerate, libName, mergedCppScripts, bundleBuildInfos, getProjectPath(), getProjectInternalPath(), getAssetsPath(), getLuaPath(), getScriptDirs(), scalingMode, textureStrategy, canvasWidth, canvasHeight, vsyncEnabled, getWindowSettings());

        // play regenerates the standalone project, so its shaders are ensured here too
        buildStandaloneShaderCache();

        if (isCancelled()) { markStartupDone(); return; }

        const bool hasCppScripts = !mergedCppScripts.empty();

        if (hasCppScripts) {
            std::string missingTools = Generator::checkBuildTools();
            if (!missingTools.empty()) {
                failPlayStartup(session, sceneId, "Cannot build C++ scripts: missing build tools", "Missing Build Tools",
                    "C++ scripts require build tools that were not found on your system:\n\n" + missingTools +
                    "\nPlease install the missing tools and try again.");
                return;
            }

            fs::path buildPath = getProjectInternalPath() / "build";
            generator.build(getProjectPath(), getProjectInternalPath(), buildPath, cmakeCCompiler, cmakeCxxCompiler, cmakeGenerator, requestedBuildJobs);
            generator.waitForBuildToComplete();

            if (isCancelled()) { markStartupDone(); return; }

            if (!generator.didLastBuildSucceed()) {
                failPlayStartup(session, sceneId, "C++ script build failed");
                return;
            }

            if (!conector.connect(buildPath, libName)) {
                failPlayStartup(session, sceneId, "Failed to connect to library");
                return;
            }
        }

        if (isCancelled()) { markStartupDone(); return; }

        // Final initialization runs on the main/GL thread so that script
        // constructors observing a current GL context behave correctly,
        // and so that finalizeStart is invoked from the UI thread.
        editor::getEditorHost().enqueueMainThreadTask(
            [this, session, sceneId, hasCppScripts, playSnapshot = std::move(playSnapshot)]() mutable {
                if (session->cancelled.load(std::memory_order_acquire)) {
                    session->startupThreadDone.store(true, std::memory_order_release);
                    return;
                }

                SceneProject* sceneProject = getScene(sceneId);
                if (!sceneProject) {
                    cleanupPlaySession(session);
                    {
                        std::scoped_lock lock(playSessionMutex);
                        if (activePlaySession == session) activePlaySession.reset();
                    }
                    session->startupThreadDone.store(true, std::memory_order_release);
                    return;
                }

                sceneProject->playStateSnapshot = std::move(playSnapshot);

                std::vector<PlayRuntimeScene> runtimeScenesToInitialize;
                {
                    std::scoped_lock lock(playSessionMutex);
                    runtimeScenesToInitialize = session->runtimeScenes;
                }

                for (const auto& entry : runtimeScenesToInitialize) {
                    if (entry.runtime && entry.runtime->scene) {
                        SceneManager::setScenePtr(entry.sourceSceneId, entry.runtime->scene);
                    }
                }
                // C++ projects run Lua scripts too, through the plugin's initScripts
                LuaBinding::clearLoadedProjectModules();

                for (const auto& entry : runtimeScenesToInitialize) {
                    if (!entry.runtime) continue;
                    if (hasCppScripts) {
                        conector.init(entry.runtime->scene);
                    } else {
                        LuaBinding::initializeLuaScripts(entry.runtime->scene);
                    }
                }

                finalizeStart(sceneProject, session->runtimeScenes);
                session->startupSucceeded.store(true, std::memory_order_release);
                session->startupThreadDone.store(true, std::memory_order_release);
            });
    } catch (const std::exception& e) {
        failPlayStartup(session, sceneId, std::string("Failed to start scene: ") + e.what());
    }
}

void editor::Project::registerSceneManager() {
    SceneManager::clearAll();
    for (SceneProject& sceneProject : scenes) {
        std::vector<uint32_t> stackSceneIds;
        collectStartActiveScenes(sceneProject.id, stackSceneIds);

        SceneManager::registerScene(sceneProject.id, sceneProject.name, [this, sceneId = sceneProject.id]() {
            std::shared_ptr<PlaySession> session;
            {
                std::scoped_lock lock(playSessionMutex);
                session = activePlaySession;
            }
            if (!session || session->cancelled.load(std::memory_order_acquire)) return;

            std::vector<uint32_t> involvedSceneIds;
            std::vector<uint32_t> activeSceneIds;
            collectInvolvedScenes(sceneId, involvedSceneIds);
            collectStartActiveScenes(sceneId, activeSceneIds);

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
                if (entry.initialized) {
                    continue;
                }

                if (conector.isLibraryConnected()) {
                    conector.init(entry.runtime->scene);
                }else{
                    LuaBinding::initializeLuaScripts(entry.runtime->scene);
                }

                prepareRuntimeScene(entry);

                if (entry.runtime && entry.runtime->scene) {
                    SceneManager::setScenePtr(entry.sourceSceneId, entry.runtime->scene);
                }
            }

            std::vector<size_t> activeStackIndices;
            for (uint32_t activeSceneId : activeSceneIds) {
                auto it = std::find_if(session->runtimeScenes.begin(), session->runtimeScenes.end(),
                    [activeSceneId](const PlayRuntimeScene& entry) {
                        return entry.sourceSceneId == activeSceneId;
                    });

                if (it != session->runtimeScenes.end()) {
                    activeStackIndices.push_back(std::distance(session->runtimeScenes.begin(), it));
                }
            }

            for (size_t entryIndex : activeStackIndices) {
                PlayRuntimeScene& entry = session->runtimeScenes[entryIndex];
                if (!entry.runtime || !entry.runtime->scene) {
                    continue;
                }

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
        }, stackSceneIds);
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
        // Same set the standalone build compiles, see collectAllBundles
        if (bundle.instances.empty() && !isStandaloneBundle(bundlePath)) {
            continue;
        }

        bundleId++;
        fs::path capturableBundlePath = bundlePath;
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
            [this, capturableBundlePath, hasTopLevelTransform](Scene* scene, Entity root) -> bool {
                SceneProject* sceneProject = findSceneProjectByScene(scene);
                if (!sceneProject) return false;

                EntityBundle* existingBundle = getEntityBundle(capturableBundlePath);
                if (existingBundle) {
                    const EntityBundle::Instance* existing = existingBundle->getInstance(sceneProject->id, root);
                    if (existing && existing->rootEntity == root) {
                        Out::error("BundleManager: root entity %u already hosts bundle '%s'",
                            root, capturableBundlePath.string().c_str());
                        return false;
                    }
                }

                const std::string oldName = scene->getEntityName(root);
                const bool addedBundle = !scene->findComponent<BundleComponent>(root);
                const bool addedTransform = hasTopLevelTransform && !scene->findComponent<Transform>(root);
                const std::vector<Entity> entitiesSnapshot = sceneProject->entities;

                // BundleManager destroys the entities created by the import, this only has to
                // undo what the factory itself changed on the root plus the instance metadata
                auto restoreRoot = [&]() {
                    removeBundleInstanceTracking(sceneProject->id, root);
                    if (scene->isEntityCreated(root)) {
                        scene->setEntityName(root, oldName);
                        if (addedBundle)
                            scene->removeComponent<BundleComponent>(root);
                        if (addedTransform)
                            scene->removeComponent<Transform>(root);
                    }
                    sceneProject->entities = entitiesSnapshot;
                };

                if (oldName.empty()) {
                    std::string rootName = capturableBundlePath.stem().string();
                    if (rootName.empty())
                        rootName = "Bundle";
                    scene->setEntityName(root, ProjectUtils::makeUniqueEntityName(scene, sceneProject->entities, rootName));
                }

                if (addedBundle) {
                    BundleComponent bundleComp;
                    bundleComp.name = capturableBundlePath.stem().string();
                    bundleComp.path = capturableBundlePath.string();
                    scene->addComponent<BundleComponent>(root, bundleComp);
                }

                if (addedTransform)
                    scene->addComponent<Transform>(root, {});

                if (std::find(sceneProject->entities.begin(), sceneProject->entities.end(), root) == sceneProject->entities.end())
                    sceneProject->entities.push_back(root);

                try {
                    importEntityBundle(sceneProject, &sceneProject->entities, capturableBundlePath, root, false);
                } catch (const std::exception& e) {
                    Out::error("BundleManager: import of '%s' failed: %s",
                        capturableBundlePath.string().c_str(), e.what());
                    restoreRoot();
                    return false;
                } catch (...) {
                    Out::error("BundleManager: import of '%s' failed",
                        capturableBundlePath.string().c_str());
                    restoreRoot();
                    return false;
                }

                EntityBundle* bundle = getEntityBundle(capturableBundlePath);
                const EntityBundle::Instance* instance = bundle ? bundle->getInstance(sceneProject->id, root) : nullptr;
                if (instance && instance->rootEntity == root)
                    return true;

                restoreRoot();
                return false;
            },
            // Destroyer: remove bundle instance via unimportEntityBundle
            [this, capturableBundlePath](Scene* scene, Entity root) -> bool {
                SceneProject* sceneProject = findSceneProjectByScene(scene);
                if (!sceneProject) return false;

                EntityBundle* bundle = getEntityBundle(capturableBundlePath);
                if (!bundle) return false;

                const EntityBundle::Instance* instance = bundle->getInstance(sceneProject->id, root);
                if (!instance || instance->rootEntity != root) return false;

                std::vector<Entity> members;
                for (const auto& m : instance->members)
                    members.push_back(m.localEntity);
                bool wasModified = sceneProject->isModified;
                bool result = unimportEntityBundle(sceneProject->id, capturableBundlePath, root, members);
                sceneProject->isModified = wasModified;
                return result;
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

    {
        std::scoped_lock lock(playSessionMutex);
        activePlaySession = session;
    }

    editor::getEditorHost().stopTransientPreviews();

    // Exit any editor camera preview before starting: covers the owned-runtime path
    // where the source scene's render never enters play mode, so stopping wouldn't
    // otherwise drop the user back out of the preview view.
    if (sceneProject->sceneRender){
        sceneProject->sceneRender->clearPreviewCamera();
    }

    sceneProject->playState = ScenePlayState::LOADING;
    editor::getEditorHost().saveAllCodeEditors();
    editor::getEditorHost().requestScenePlayFocus(sceneId);

    std::thread startupThread([this, session, sceneId]() {
        runPlayStartup(session, sceneId);
    });
    startupThread.detach();
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
            editor::getEditorHost().requestScenePlayFocus(sceneId);
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

    if (sceneProject->playState == ScenePlayState::STOPPED) {
        Out::warning("Scene %u is not currently playing", sceneId);
        return;
    }

    if (sceneProject->playState == ScenePlayState::CANCELLING) {
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

    const bool startupSucceededAtStop = session->startupSucceeded.load(std::memory_order_acquire);
    if (startupSucceededAtStop) {
        // Notify scripts/engine before tearing the runtime down so that
        // shutdown handlers still see a valid engine state.
        Engine::onViewDestroyed.call();
        Engine::onShutdown.call();
        Engine::pauseGameEvents(true);
        Engine::clearAllSubscriptions(true);
    }

    // Clear crash handler when stopping
    doriax::FunctionSubscribeGlobal::getCrashHandler() = nullptr;

    // Request cancellation asynchronously (returns a future we can wait on later if needed)
    auto cancelFuture = generator.cancelBuild();

    std::thread finalizeStopThread([this, session, sceneId, startupSucceededAtStop, cancelFuture = std::move(cancelFuture)]() mutable {
        if (cancelFuture.valid()) {
            cancelFuture.wait();
        }
        generator.waitForBuildToComplete();

        // Wait for the connect/startup thread to finish to avoid races
        while (!session->startupThreadDone.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // All teardown happens on the main/GL thread so that script
        // destructors and engine resource cleanup run with a valid GL context.
        editor::getEditorHost().enqueueMainThreadTask([this, session, sceneId, startupSucceededAtStop]() {
            SceneProject* mainSceneProject = getScene(sceneId);
            const bool startupSucceeded = session->startupSucceeded.load(std::memory_order_acquire);

            if (startupSucceeded && !startupSucceededAtStop) {
                Engine::onViewDestroyed.call();
                Engine::onShutdown.call();
                Engine::pauseGameEvents(true);
                Engine::clearAllSubscriptions(true);
            }

            std::vector<PlayRuntimeScene> runtimeScenes;
            {
                std::scoped_lock lock(playSessionMutex);
                runtimeScenes = session->runtimeScenes;
            }

            const bool libraryConnected = conector.isLibraryConnected();
            if (libraryConnected) {
                for (const auto& entry : runtimeScenes) {
                    if (entry.runtime) conector.cleanup(entry.runtime->scene);
                }
            } else if (startupSucceeded) {
                for (const auto& entry : runtimeScenes) {
                    if (entry.runtime) LuaBinding::cleanupLuaScripts(entry.runtime->scene);
                }
            }

            // What the scripts registered and did not remove is dropped here, while the code
            // behind those callbacks is still loaded
            for (const auto& entry : runtimeScenes) {
                if (entry.runtime && entry.runtime->scene) Engine::clearComponentSubscriptions(entry.runtime->scene);
            }

            if (libraryConnected) {
                conector.disconnect();
            }

            if (startupSucceeded) {
                finalizeStop(mainSceneProject, runtimeScenes);
            } else {
                cleanupPlaySession(session);
                SceneManager::clearAll();
                BundleManager::clearAll();
                editor::getEditorHost().resetLastActivatedScene();
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
}

void editor::Project::stopActivePlay() {
    uint32_t mainSceneId;
    {
        std::scoped_lock lock(playSessionMutex);
        if (!activePlaySession) {
            return;
        }
        mainSceneId = activePlaySession->mainSceneId;
    }
    stop(mainSceneId);
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
        editor::getEditorHost().processMainThreadTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::vector<Scene*> editor::Project::getRunningRuntimeLayers(uint32_t sceneId) {
    std::vector<Scene*> runningLayers;

    std::scoped_lock lock(playSessionMutex);
    if (!activePlaySession || activePlaySession->mainSceneId != sceneId) {
        return runningLayers;
    }
    if (!activePlaySession->startupSucceeded.load(std::memory_order_acquire)) {
        return runningLayers;
    }

    for (const auto& entry : activePlaySession->runtimeScenes) {
        SceneProject* runtimeProject = entry.runtime;
        if (!runtimeProject || !runtimeProject->scene || entry.sourceSceneId == sceneId) {
            continue;
        }
        if (Engine::isSceneRunning(runtimeProject->scene)) {
            runningLayers.push_back(runtimeProject->scene);
        }
    }

    return runningLayers;
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
