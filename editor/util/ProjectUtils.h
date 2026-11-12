// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "lua.hpp"

#include <string>
#include <filesystem>
#include <unordered_set>
#include <vector>

#include "LuaBridge.h"
#include "Out.h"

#include "Project.h"
#include "script/ScriptProperty.h"

#include "texture/Texture.h"
#include "ecs/Entity.h"
#include "render/Render.h"

#include "command/Command.h"

namespace doriax::editor {

class ProjectUtils {
public:

    static Entity getLockedEntityParent(Scene* scene, Entity entity);
    static bool isEntityLocked(Scene* scene, Entity entity);
    // Model and foliage parts, to check a whole tree in one pass
    static std::unordered_set<Entity> getOwnedEntities(Scene* scene);
    static bool isEntityLocked(Scene* scene, Entity entity, const std::unordered_set<Entity>& ownedEntities);
    static Entity getParentModel(Scene* scene, Entity entity);
    // Model owning the imported parts an entity is or contains, never a model itself
    static Entity getModelBranchOwner(Scene* scene, Entity entity);
    // Any entity the loader generated for the model: nodes, parts, bones, skeleton
    static bool isModelNode(const ModelComponent& model, Entity entity);
    static bool canEditModelBranch(Scene* scene, Entity entity, std::string* reason = nullptr);
    static bool hasCustomMeshParenting(Scene* scene, Entity model);
    static Entity getEffectiveParent(Scene* scene, Entity entity);
    static bool canMoveLockedEntityOrder(Scene* scene, Entity source, Entity target, InsertionType type, std::string* reason = nullptr);
    static std::string makeUniqueEntityName(const std::string& baseName, const std::unordered_set<std::string>& existingNames);
    static std::string makeUniqueEntityName(Scene* scene, const std::vector<Entity>& entities, const std::string& baseName, const std::unordered_set<Entity>& ignoredEntities = {});

    static size_t getTransformIndex(EntityRegistry* registry, Entity entity);
    static size_t getEntityOrderIndex(EntityRegistry* registry, const std::vector<Entity>& entities, Entity entity);
    static void sortEntitiesByTransformOrder(EntityRegistry* registry, std::vector<Entity>& entities);

    static bool moveEntityOrderByTarget(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity target, InsertionType type, Entity& oldParent, size_t& oldIndex, bool& hasTransform);
    static void moveEntityOrderByIndex(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity parent, size_t index, bool hasTransform);
    static void moveEntityOrderByTransform(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity parent, size_t transformIndex, bool enableMove = true);

    static void addEntityComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, std::vector<Entity>& entities, YAML::Node componentNode = YAML::Node());
    static YAML::Node removeEntityComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, std::vector<Entity>& entities, bool encodeComponent = false);
    static void reconcileTrackedEntities(EntityRegistry* registry, std::vector<Entity>& entities, Project* project = nullptr, uint32_t sceneId = NULL_PROJECT_SCENE);

    // --- Virtual children utilities ---
    static Entity getVirtualParent(Scene* scene, Entity entity);
    static std::vector<Entity> getVirtualChildren(Scene* scene, const std::vector<Entity>& parentEntities, const std::unordered_set<Entity>& excludedEntities = {});

    // --- Ownership cascade utilities ---
    static std::vector<Entity> getOwnedCascadeEntities(EntityRegistry* registry, Entity owner, ComponentType componentType);

    // --- Lua script utilities ---
    static ScriptPropertyValue luaValueToScriptPropertyValue(lua_State* L, int idx, ScriptPropertyType type);
    static void loadLuaScriptProperties(ScriptEntry& entry, const std::string& luaPath);
    static void loadLuaScriptPropertiesFromString(ScriptEntry& entry, const std::string& scriptContent, const std::string& chunkName);

    static void collectModelEntities(Scene* scene, const ModelComponent& model, std::vector<Entity>& out);
    // Also the runtime nodes of a bundle member, which DeleteEntityCmd cannot see
    static bool destroyModelNodes(Scene* scene, ModelComponent& model);
    static void clearModelNodes(ModelComponent& model);
    // Nodes made in the editor are tracked and saved, a bundle member builds them when it loads
    static bool hasTrackedModelEntities(Scene* scene, const ModelComponent& model, const std::vector<Entity>& entities);

    // True when a model root has no renderable submeshes of its own while its
    // generated model children do. Components that only render same-entity mesh
    // data (such as InstancedMeshComponent) cannot render that root directly.
    static bool hasModelMeshChildrenWithoutRootGeometry(EntityRegistry* registry, Entity entity);

    static void collectEntities(const YAML::Node& entityNode, std::vector<Entity>& allEntities);

    // Engine class wrapping an entity, used to generate C++ that names it (subclass
    // script parents, DPROPERTY members). Every class here is declared in the flat
    // header "<name>.h" and has a (Scene*, Entity) constructor.
    struct EntityClassInfo {
        std::string name;           // e.g. "Terrain", declared in "Terrain.h"
        bool derivesObject = false; // has doriax::Object API (getPosition, setPosition, ...)
        bool derivesMesh = false;   // has doriax::Mesh API (setColor, ...), implies derivesObject
    };
    static EntityClassInfo getEntityClassInfo(Scene* scene, Entity entity);

    // Returns entity type name (e.g. "Mesh", "Camera", "Light") based on its components
    static std::string getEntityTypeName(Scene* scene, Entity entity);

    static void removeDynamicInstmesh(Entity entity, const YAML::Node& savedComponents, EntityRegistry* registry);

    // Keeps the action settings edited during a preview in its restore snapshot
    static void updateActionPreviewSnapshot(Entity entity, YAML::Node& components, EntityRegistry* registry);

    // Fills a Texture with the editor built-in default skybox cubemap.
    static void setDefaultSkyTexture(Texture& outTexture);

    // a starting shader file of the type, and the same written under shaders/; its project path, empty on failure
    static std::string getShaderTemplate(ShaderType shaderType);
    static std::string writeShaderTemplate(const std::filesystem::path& projectRoot, const std::string& desiredName, ShaderType shaderType);
    // a shader graph with only the type's output node, and the same written under shaders/
    static std::string getShaderGraphTemplate(ShaderType shaderType);
    static std::string writeShaderGraphTemplate(const std::filesystem::path& projectRoot, const std::string& desiredName, ShaderType shaderType);
    // a function graph passing one value through, for Subgraph nodes to call
    static std::string getShaderFunctionTemplate();
    // a script graph with the two events most scripts start from
    static std::string getLogicGraphTemplate();
    // the Lua script of a logic graph, beside it; a .lua written by hand is left alone
    static bool writeLogicScript(Project* project, const std::filesystem::path& graph, const std::string& code, std::string& error);
    // a project copy of an engine shader file
    static std::string copyEngineShaderToProject(const std::filesystem::path& projectRoot, const std::string& enginePath, const std::string& desiredName);
    // the engine's shader files of the type, under shaders/; a project file of the same path wins
    static std::vector<std::string> getEngineShaders(ShaderType shaderType);
    static bool isEngineShader(const std::string& path);
    // a shader file from the project, else the engine library
    static bool readShaderSource(const std::filesystem::path& projectRoot, const std::string& path, std::string& source);
    // the shader file a component draws with, its own or the scene default; a mesh's of one submesh
    static std::string getComponentShader(Scene* scene, ComponentType cpType, ShaderType shaderType, Entity entity, int submesh = -1);

    // Builds a MultiPropertyCmd that removes tile at tileIndex from a TilemapComponent.
    // Returns the command (caller must add it to CommandHandle) or nullptr if invalid.
    static Command* buildDeleteTileCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int tileIndex);

    // Builds a PropertyCmd that removes the instance at instanceIndex from an InstancedMeshComponent.
    // Returns the command (caller must add it to CommandHandle) or nullptr if invalid.
    static Command* buildDeleteInstanceCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int instanceIndex);

    // Builds a PropertyCmd that duplicates the instance at instanceIndex in an InstancedMeshComponent.
    // Returns the command (caller must add it to CommandHandle) or nullptr if invalid.
    static Command* buildDuplicateInstanceCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int instanceIndex);

    // Builds a MultiPropertyCmd that duplicates tile at tileIndex in a TilemapComponent.
    // The new tile is placed with a small offset. Returns nullptr if invalid or array is full.
    static Command* buildDuplicateTileCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int tileIndex);
};

} // namespace doriax::editor
