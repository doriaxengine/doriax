// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "shader/ShaderParameterParser.h"
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

    // Resolved plan for a built-in shader fork: the project-relative base path (no
    // extension), the absolute target paths, and the source contents to write. Computed
    // without touching the filesystem so it can drive an undoable command.
    struct ShaderForkPlan {
        bool valid = false;
        std::string error;
        std::string base;                  // e.g. "shaders/myMesh"
        std::filesystem::path vertPath;    // absolute
        std::filesystem::path fragPath;    // absolute
        // Empty unless includes were forked, in which case the fork owns this directory
        // and holds its entry points plus the copied includes/ tree inside it.
        std::filesystem::path forkDirPath;      // absolute
        std::string vertContent;
        std::string fragContent;
        struct IncludeFile {
            std::filesystem::path path;    // absolute
            std::string content;
        };
        std::vector<IncludeFile> includeFiles;
    };

    // Default used only by non-interactive callers. The editor dialog lets the user
    // select any directory inside the project for each individual fork.
    static std::filesystem::path defaultShaderForkDir();

    // Converts an entity/scene label to a safe filename and adds a numeric suffix until
    // neither fork layout (loose .vert/.frag pair, or a directory of that name) is taken.
    static std::string makeUniqueShaderName(const std::filesystem::path& absDir,
                                            ShaderType shaderType,
                                            const std::string& desiredName);

    // Plans a built-in shader fork without writing anything. targetDirRel must be a safe
    // project-relative directory and baseName the exact filename stem. Without includes
    // the entry points stay loose in it; with them the fork takes <targetDirRel>/<base>/,
    // so its copies override the engine library for no other shader.
    static ShaderForkPlan prepareShaderFork(Project* project, ShaderType shaderType,
                                            const std::filesystem::path& targetDirRel,
                                            const std::string& baseName,
                                            bool forkIncludes = false);

    // Writes all planned files. It refuses collisions and removes partial output when a
    // later write fails, so a failed command never truncates or strands source files.
    static bool writeShaderFork(const ShaderForkPlan& plan);

    // Removes only files owned by the plan, then prunes the directories it created.
    static void removeShaderFork(const ShaderForkPlan& plan);

    // a starting vertex offset in the default shader directory; its project path, empty on failure
    static std::string writeVertexOffsetTemplate(const std::filesystem::path& projectRoot, const std::string& desiredName);
    // a starting coverage function, the same way
    static std::string writeCoverageTemplate(const std::filesystem::path& projectRoot, const std::string& desiredName);
    // a project copy of an engine effect function, to change it
    static std::string copyEffectFunctionToProject(const std::filesystem::path& projectRoot, const std::string& enginePath, const std::string& desiredName);
    // the engine's own vertex offsets, under offsets/ in its shader library
    static std::vector<std::string> getEngineVertexOffsets();
    static bool isEngineVertexOffset(const std::string& path);
    // the engine's own coverage functions, under coverage/
    static std::vector<std::string> getEngineCoverages();
    static bool isEngineCoverage(const std::string& path);
    // either kind; a project file of the same path overrides it
    static bool isEngineEffectFunction(const std::string& path);
    // a shader file from the project, else the engine library
    static bool readShaderSource(const std::filesystem::path& projectRoot, const std::string& path, std::string& source);
    // the annotations of shader files and their includes, resolved as the shader build does:
    // includeRoot, the project root, then the engine library
    static ShaderEffectAnnotations getShaderAnnotations(const std::filesystem::path& projectRoot, const std::vector<std::string>& files,
                                                        const std::filesystem::path& includeRoot);
    // the annotations of a fork (its .vert and .frag) or of an effect file
    static ShaderEffectAnnotations getSourceAnnotations(const std::filesystem::path& projectRoot, const std::string& source, bool fork);
    // the files a component's shaders are built from, as the renderer picks them: its fork (own or the
    // scene default), a mesh's depth fork and effect files, a water's underwater fork; true for forks
    static std::vector<std::pair<std::string, bool>> getComponentShaderSources(Scene* scene, ComponentType cpType, ShaderType shaderType, Entity entity);

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
