// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "ProjectUtils.h"

#include "Out.h"
#include "Catalog.h"
#include "Stream.h"
#include "shaders.h"
#include "util/Util.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <unordered_set>

#include "component/ActionComponent.h"
#include "component/AnimationComponent.h"
#include "component/BoneComponent.h"
#include "component/ButtonComponent.h"
#include "component/ModelComponent.h"
#include "component/Transform.h"
#include "component/MeshComponent.h"
#include "component/SkyComponent.h"
#include "component/FogComponent.h"
#include "component/LightComponent.h"
#include "component/CameraComponent.h"
#include "component/SpriteComponent.h"
#include "component/PointsComponent.h"
#include "component/LinesComponent.h"
#include "component/TerrainComponent.h"
#include "component/SoundComponent.h"
#include "component/UIContainerComponent.h"
#include "component/UIComponent.h"
#include "component/TextComponent.h"
#include "component/TextEditComponent.h"
#include "component/ImageComponent.h"
#include "component/PanelComponent.h"
#include "component/ScrollbarComponent.h"
#include "component/ProgressbarComponent.h"
#include "component/PolygonComponent.h"
#include "component/MeshPolygonComponent.h"
#include "component/Body2DComponent.h"
#include "subsystem/UISystem.h"
#include "component/Body3DComponent.h"
#include "component/Joint2DComponent.h"
#include "component/Joint3DComponent.h"

#include "component/TilemapComponent.h"
#include "component/InstancedMeshComponent.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/PropertyCmd.h"

#include "EntityHandle.h"
#include "Object.h"
#include "Mesh.h"
#include "Shape.h"
#include "Model.h"
#include "Points.h"
#include "Sprite.h"
#include "Light.h"
#include "Camera.h"

#include "resources/sky/Daylight_Box_Back_png.h"
#include "resources/sky/Daylight_Box_Bottom_png.h"
#include "resources/sky/Daylight_Box_Front_png.h"
#include "resources/sky/Daylight_Box_Left_png.h"
#include "resources/sky/Daylight_Box_Right_png.h"
#include "resources/sky/Daylight_Box_Top_png.h"

using namespace doriax;

static void parseLuaPropertiesTable(lua_State* L, ScriptEntry& entry);

namespace {

bool splitTrailingDuplicateNumber(const std::string& name, std::string& baseName, size_t& nextNumber){
    size_t end = name.find_last_not_of(' ');
    if (end == std::string::npos || !std::isdigit(static_cast<unsigned char>(name[end])))
        return false;

    size_t start = end;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(name[start - 1]))) {
        start--;
    }

    if (start == 0 || name[start - 1] != ' ' || start - 1 == 0)
        return false;

    size_t value = 0;
    constexpr size_t maxValue = std::numeric_limits<size_t>::max();
    for (size_t i = start; i <= end; i++) {
        size_t digit = static_cast<size_t>(name[i] - '0');
        if (value > (maxValue - digit) / 10)
            return false;

        value = value * 10 + digit;
    }

    if (value == maxValue)
        return false;

    baseName = name.substr(0, start - 1);
    nextNumber = std::max<size_t>(2, value + 1);
    return true;
}

}

void editor::ProjectUtils::setDefaultSkyTexture(Texture& outTexture) {
    TextureData skyBack;
    TextureData skyBottom;
    TextureData skyFront;
    TextureData skyLeft;
    TextureData skyRight;
    TextureData skyTop;

    skyBack.loadTextureFromMemory(Daylight_Box_Back_png, Daylight_Box_Back_png_len);
    skyBottom.loadTextureFromMemory(Daylight_Box_Bottom_png, Daylight_Box_Bottom_png_len);
    skyFront.loadTextureFromMemory(Daylight_Box_Front_png, Daylight_Box_Front_png_len);
    skyLeft.loadTextureFromMemory(Daylight_Box_Left_png, Daylight_Box_Left_png_len);
    skyRight.loadTextureFromMemory(Daylight_Box_Right_png, Daylight_Box_Right_png_len);
    skyTop.loadTextureFromMemory(Daylight_Box_Top_png, Daylight_Box_Top_png_len);

    outTexture.setId("editor:resources:default_sky");
    outTexture.setCubeDatas("editor:resources:default_sky", skyFront, skyBack, skyLeft, skyRight, skyTop, skyBottom);
}

namespace {

std::string shaderTypeFileName(ShaderType shaderType) {
    switch (shaderType) {
        case ShaderType::MESH:   return "mesh";
        case ShaderType::UI:     return "ui";
        case ShaderType::POINTS: return "points";
        case ShaderType::LINES:  return "lines";
        case ShaderType::SKYBOX: return "sky";
        case ShaderType::POSTPROCESS: return "postprocess";
        default:                 return "";
    }
}

std::string sanitizeShaderName(ShaderType shaderType, const std::string& desiredName) {
    std::string name;
    for (char c : desiredName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            name += c;
    }
    std::string typeFile = shaderTypeFileName(shaderType);
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
        name = typeFile + name;
    return name.empty() ? "shader" : name;
}

// Every .glsl the entry points reach through the engine library, keyed by its
// include key ("includes/pbr.glsl"). Sorted, so the dialog preview is stable.
std::map<std::string, std::string> collectBuiltInShaderIncludes(const std::string& vertSource,
                                                                const std::string& fragSource) {
    std::map<std::string, std::string> collected;
    std::unordered_set<std::string> visited;

    std::function<void(const std::string&)> visitSource = [&](const std::string& source) {
        for (const std::string& key : editor::Util::getShaderIncludeKeys(source)) {
            if (!visited.insert(key).second)
                continue;

            auto sourceIt = editor::shaderMap.find(key);
            if (sourceIt == editor::shaderMap.end())
                continue;

            if (std::filesystem::path(key).extension() == ".glsl")
                collected[key] = sourceIt->second;
            visitSource(sourceIt->second);
        }
    };

    visitSource(vertSource);
    visitSource(fragSource);
    return collected;
}

// Removes the directories a fork created. remove() only succeeds on an empty one,
// so anything the user put there survives.
void pruneForkDirectories(const std::filesystem::path& root) {
    if (root.empty())
        return;

    std::error_code ec;
    std::vector<std::filesystem::path> dirs;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec))
            dirs.push_back(it->path());
    }
    dirs.push_back(root);

    // The iterator visits parents first, so deleting in reverse empties the leaves first.
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
        std::filesystem::remove(*it, ec);
    }
}

} // namespace

std::filesystem::path editor::ProjectUtils::defaultShaderForkDir() {
    return "shaders";
}

std::string editor::ProjectUtils::makeUniqueShaderName(const std::filesystem::path& absDir,
                                                       ShaderType shaderType,
                                                       const std::string& desiredName) {
    // Both layouts are checked, so toggling "fork includes" cannot land on a name
    // that is free in one and taken in the other.
    std::string baseName = sanitizeShaderName(shaderType, desiredName);
    std::string name = baseName;
    int suffix = 2;
    while (std::filesystem::exists(absDir / (name + ".vert")) ||
           std::filesystem::exists(absDir / (name + ".frag")) ||
           std::filesystem::exists(absDir / name)) {
        name = baseName + std::to_string(suffix++);
    }
    return name;
}

editor::ProjectUtils::ShaderForkPlan editor::ProjectUtils::prepareShaderFork(
        Project* project, ShaderType shaderType, const std::filesystem::path& targetDirRel,
        const std::string& baseName, bool forkIncludes) {
    ShaderForkPlan plan;
    if (!project) {
        plan.error = "Project is unavailable.";
        return plan;
    }

    std::string typeFile = shaderTypeFileName(shaderType);
    if (typeFile.empty()) {
        plan.error = "This shader type cannot be forked.";
        return plan;
    }

    if (baseName.empty() || std::filesystem::path(baseName).filename() != baseName) {
        plan.error = "Enter a shader name without a path or extension.";
        return plan;
    }
    for (char c : baseName) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            plan.error = "Shader names may contain only letters, numbers, and underscores.";
            return plan;
        }
    }

    std::filesystem::path normalizedDir = targetDirRel.empty() ? std::filesystem::path(".")
                                                               : targetDirRel.lexically_normal();
    if (!Util::isSafeRelativePath(normalizedDir)) {
        plan.error = "The shader directory must be inside the project.";
        return plan;
    }

    std::filesystem::path projectRoot = project->getProjectPath();

    // fullscreen passes share one vertex entry point; the fork still gets its own copy
    std::string vertFile = (shaderType == ShaderType::POSTPROCESS) ? "fullscreen" : typeFile;
    auto vertIt = editor::shaderMap.find(vertFile + ".vert");
    auto fragIt = editor::shaderMap.find(typeFile + ".frag");
    if (vertIt == editor::shaderMap.end() || fragIt == editor::shaderMap.end()) {
        plan.error = "The built-in shader source is unavailable.";
        return plan;
    }

    // Forked includes override the engine library for every entry point beside them,
    // so a fork carrying them gets a directory to itself.
    std::filesystem::path forkDirRel = forkIncludes ? normalizedDir / baseName : normalizedDir;
    std::filesystem::path forkDir = projectRoot / forkDirRel;

    plan.base = (forkDirRel / baseName).lexically_normal().generic_string();
    plan.vertPath = forkDir / (baseName + ".vert");
    plan.fragPath = forkDir / (baseName + ".frag");
    plan.vertContent = vertIt->second;
    plan.fragContent = fragIt->second;
    if (forkIncludes)
        plan.forkDirPath = forkDir;

    // With includes the whole directory must be free, so a fork never merges into
    // someone else's folder.
    std::vector<std::filesystem::path> collisions = {plan.vertPath, plan.fragPath};
    if (forkIncludes)
        collisions = {plan.forkDirPath};

    for (const std::filesystem::path& collision : collisions) {
        std::error_code ec;
        if (std::filesystem::exists(collision, ec) && !ec) {
            plan.error = "A fork target already exists: " +
                         std::filesystem::relative(collision, projectRoot, ec).generic_string();
            return plan;
        }
    }

    if (forkIncludes) {
        // The fork directory mirrors the shaderlib root, so each key keeps its shape:
        // "includes/pbr.glsl" -> <fork>/includes/pbr.glsl.
        for (const auto& [key, content] : collectBuiltInShaderIncludes(plan.vertContent, plan.fragContent)) {
            if (Util::isSafeRelativePath(key))
                plan.includeFiles.push_back({forkDir / key, content});
        }
    }

    plan.valid = true;
    return plan;
}

bool editor::ProjectUtils::writeShaderFork(const ShaderForkPlan& plan) {
    if (!plan.valid)
        return false;

    // Never overwrite: undoing a fork that clobbered a file would destroy its contents.
    std::vector<std::filesystem::path> written;
    auto writeFile = [&](const std::filesystem::path& path, const std::string& content) -> bool {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return false;
        if (std::filesystem::exists(path, ec) || ec)
            return false;

        std::ofstream f(path, std::ios::trunc);
        if (!f.is_open())
            return false;
        written.push_back(path);
        f << content;
        return static_cast<bool>(f);
    };

    bool success = writeFile(plan.vertPath, plan.vertContent) &&
                   writeFile(plan.fragPath, plan.fragContent);
    for (const auto& include : plan.includeFiles) {
        if (!success)
            break;
        success = writeFile(include.path, include.content);
    }

    if (!success) {
        std::error_code ec;
        for (const auto& path : written)
            std::filesystem::remove(path, ec);
        pruneForkDirectories(plan.forkDirPath);
        Out::error("Could not write forked shader files");
    }
    return success;
}

void editor::ProjectUtils::removeShaderFork(const ShaderForkPlan& plan) {
    std::error_code ec;
    std::filesystem::remove(plan.vertPath, ec);
    std::filesystem::remove(plan.fragPath, ec);
    for (const auto& include : plan.includeFiles)
        std::filesystem::remove(include.path, ec);
    pruneForkDirectories(plan.forkDirPath);
}

void editor::ProjectUtils::collectModelEntities(Scene* scene, const ModelComponent& model, std::vector<Entity>& out){
    if (!model.nodesIdMapping.empty()) {
        for (const auto& node : model.nodesIdMapping){
            out.push_back(node.second);
        }
    } else {
        for (const auto& bone : model.bonesIdMapping){
            out.push_back(bone.second);
        }
        for (const auto& node : model.meshNodesMapping){
            out.push_back(node.second);
        }
    }
    for (const auto& anim : model.animations){
        out.push_back(anim);
        AnimationComponent* animComp = scene->findComponent<AnimationComponent>(anim);
        if (animComp){
            for (const auto& frame : animComp->actions){
                if (frame.action != NULL_ENTITY){
                    out.push_back(frame.action);
                }
            }
        }
    }
}

bool editor::ProjectUtils::hasModelMeshChildrenWithoutRootGeometry(EntityRegistry* registry, Entity entity){
    if (!registry)
        return false;

    MeshComponent* rootMesh = registry->findComponent<MeshComponent>(entity);
    ModelComponent* model = registry->findComponent<ModelComponent>(entity);
    if (!rootMesh || !model || rootMesh->numSubmeshes != 0 || model->meshNodesMapping.empty())
        return false;

    for (const auto& node : model->meshNodesMapping){
        MeshComponent* childMesh = registry->findComponent<MeshComponent>(node.second);
        if (childMesh && childMesh->numSubmeshes > 0)
            return true;
    }

    return false;
}

Entity editor::ProjectUtils::getLockedEntityParent(Scene* scene, Entity entity){
    if (entity == NULL_ENTITY)
        return NULL_ENTITY;

    Signature signature = scene->getSignature(entity);

    // Foliage instances are resolved, not authored: they belong to their terrain the same way
    // model nodes belong to their model.
    auto terrains = scene->getComponentArray<TerrainComponent>();
    for (size_t i = 0; i < terrains->size(); ++i) {
        TerrainComponent& terrain = terrains->getComponentFromIndex(i);
        for (const TerrainFoliageInstances& instances : terrain.foliageInstances) {
            for (const TerrainFoliageChunk& chunk : instances.chunks) {
                if (chunk.entity == entity) {
                    return terrains->getEntity(i);
                }
            }
        }
    }

    auto models = scene->getComponentArray<ModelComponent>();
    for (size_t i = 0; i < models->size(); ++i) {
        Entity modelEntity = models->getEntity(i);
        ModelComponent& model = models->getComponentFromIndex(i);

        if (model.skeleton == entity) {
            return modelEntity;
        }

        for (const auto& anim : model.animations){
            if (anim == entity) {
                return modelEntity;
            }

            AnimationComponent* animComp = scene->findComponent<AnimationComponent>(anim);
            if (animComp){
                for (const auto& frame : animComp->actions){
                    if (frame.action == entity){
                        return anim;
                    }
                }
            }
        }

        for (const auto& bone : model.bonesIdMapping) {
            if (bone.second == entity) {
                Transform* transform = scene->findComponent<Transform>(entity);
                if (transform && transform->parent != NULL_ENTITY) {
                    return transform->parent;
                }
                return modelEntity;
            }
        }

        for (const auto& node : model.meshNodesMapping) {
            if (node.second == entity) {
                Transform* transform = scene->findComponent<Transform>(entity);
                if (transform && transform->parent != NULL_ENTITY) {
                    return transform->parent;
                }
                return modelEntity;
            }
        }
        for (const auto& node : model.nodesIdMapping) {
            if (node.second == entity) {
                Transform* transform = scene->findComponent<Transform>(entity);
                if (transform && transform->parent != NULL_ENTITY) {
                    return transform->parent;
                }
                return modelEntity;
            }
        }
    }

    if (!signature.test(scene->getComponentId<Transform>())){
        return NULL_ENTITY;
    }

    Transform& transform = scene->getComponent<Transform>(entity);
    if (transform.parent == NULL_ENTITY){
        return NULL_ENTITY;
    }

    Entity parent = transform.parent;
    Signature parentSignature = scene->getSignature(parent);

    if (parentSignature.test(scene->getComponentId<ButtonComponent>())){
        ButtonComponent& button = scene->getComponent<ButtonComponent>(parent);
        if (button.label == entity){
            return parent;
        }
    }

    if (parentSignature.test(scene->getComponentId<ScrollbarComponent>())){
        ScrollbarComponent& scrollbar = scene->getComponent<ScrollbarComponent>(parent);
        if (scrollbar.bar == entity){
            return parent;
        }
    }

    if (parentSignature.test(scene->getComponentId<ProgressbarComponent>())){
        ProgressbarComponent& progressbar = scene->getComponent<ProgressbarComponent>(parent);
        if (progressbar.fill == entity){
            return parent;
        }
    }

    if (parentSignature.test(scene->getComponentId<TextEditComponent>())){
        TextEditComponent& textedit = scene->getComponent<TextEditComponent>(parent);
        if (textedit.text == entity || textedit.selection == entity || textedit.cursor == entity){
            return parent;
        }
    }

    auto panels = scene->getComponentArray<PanelComponent>();
    for (size_t i = 0; i < panels->size(); ++i) {
        Entity panelEntity = panels->getEntity(i);
        PanelComponent& panel = panels->getComponentFromIndex(i);
        if (panel.headerimage == entity || panel.headercontainer == entity || panel.headertext == entity) {
            return panelEntity;
        }
    }

    return NULL_ENTITY;
}

bool editor::ProjectUtils::isEntityLocked(Scene* scene, Entity entity){
    return getLockedEntityParent(scene, entity) != NULL_ENTITY;
}

Entity editor::ProjectUtils::getEffectiveParent(Scene* scene, Entity entity) {
    Entity virtualParent = getVirtualParent(scene, entity);
    if (virtualParent != NULL_ENTITY) {
        return virtualParent;
    }

    Transform* transform = scene->findComponent<Transform>(entity);
    if (transform) {
        return transform->parent;
    }

    return getLockedEntityParent(scene, entity);
}

bool editor::ProjectUtils::canMoveLockedEntityOrder(Scene* scene, Entity source, Entity target, InsertionType type) {
    if (!isEntityLocked(scene, source)) {
        return true;
    }

    if (type == InsertionType::INTO) {
        return false;
    }

    return getEffectiveParent(scene, source) == getEffectiveParent(scene, target);
}

std::string editor::ProjectUtils::makeUniqueEntityName(const std::string& baseName, const std::unordered_set<std::string>& existingNames) {
    if (baseName.empty() || existingNames.find(baseName) == existingNames.end()) {
        return baseName;
    }

    std::string cleanBase = baseName;
    size_t nameCount = 2;
    splitTrailingDuplicateNumber(baseName, cleanBase, nameCount);

    std::string uniqueName = cleanBase + " " + std::to_string(nameCount++);
    while (existingNames.find(uniqueName) != existingNames.end()) {
        uniqueName = cleanBase + " " + std::to_string(nameCount++);
    }

    return uniqueName;
}

std::string editor::ProjectUtils::makeUniqueEntityName(Scene* scene, const std::vector<Entity>& entities, const std::string& baseName, const std::unordered_set<Entity>& ignoredEntities) {
    if (!scene || baseName.empty()) {
        return baseName;
    }

    std::unordered_set<std::string> existingNames;
    for (Entity sceneEntity : entities) {
        if (ignoredEntities.find(sceneEntity) != ignoredEntities.end()) {
            continue;
        }

        existingNames.insert(scene->getEntityName(sceneEntity));
    }

    return makeUniqueEntityName(baseName, existingNames);
}

size_t editor::ProjectUtils::getTransformIndex(EntityRegistry* registry, Entity entity){
    Signature signature = registry->getSignature(entity);
    if (signature.test(registry->getComponentId<Transform>())) {
        Transform& transform = registry->getComponent<Transform>(entity);
        auto transforms = registry->getComponentArray<Transform>();
        return transforms->getIndex(entity);
    }

    return 0;
}

void editor::ProjectUtils::sortEntitiesByTransformOrder(EntityRegistry* registry, std::vector<Entity>& entities) {
    auto transforms = registry->getComponentArray<Transform>();
    std::unordered_map<Entity, size_t> transformOrder;
    for (size_t i = 0; i < transforms->size(); ++i) {
        Entity ent = transforms->getEntity(i);
        transformOrder[ent] = i;
    }
    std::sort(entities.begin(), entities.end(),
        [&transformOrder](const Entity& a, const Entity& b) {
            return transformOrder[a] < transformOrder[b];
        }
    );
}

bool editor::ProjectUtils::moveEntityOrderByTarget(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity target, InsertionType type, Entity& oldParent, size_t& oldIndex, bool& hasTransform) {
    Transform* transformSource = registry->findComponent<Transform>(source);
    Transform* transformTarget = registry->findComponent<Transform>(target);

    if (transformSource && transformTarget){
        hasTransform = true;

        if (registry->isParentOf(source, target)){
            Out::error("Cannot move an entity to its own child");
            return false;
        }

        auto transforms = registry->getComponentArray<Transform>();

        size_t sourceTransformIndex = transforms->getIndex(source);
        size_t targetTransformIndex = transforms->getIndex(target);

        Entity newParent = NULL_ENTITY;
        if (type == InsertionType::INTO){
            newParent = target;
        }else{
            newParent = transformTarget->parent;
        }

        oldParent = transformSource->parent;
        oldIndex = sourceTransformIndex;

        if (type == InsertionType::AFTER || type == InsertionType::INTO){
            targetTransformIndex++;
        }

        moveEntityOrderByTransform(registry, entities, source, newParent, targetTransformIndex);

    }else{

        hasTransform = false;

        auto itSource = std::find(entities.begin(), entities.end(), source);
        auto itTarget = std::find(entities.begin(), entities.end(), target);

        if (itSource == entities.end() || itTarget == entities.end()) {
            Out::error("Source or Target entity not found in entities vector");
            return false;
        }

        oldIndex = std::distance(entities.begin(), itSource);

        Entity tempSource = *itSource;
        size_t sourceIndex = std::distance(entities.begin(), itSource);
        size_t targetIndex = std::distance(entities.begin(), itTarget);

        entities.erase(itSource);

        if (type == InsertionType::BEFORE) {
            if (sourceIndex < targetIndex) targetIndex--;
            entities.insert(entities.begin() + targetIndex, tempSource);
        } else if (type == InsertionType::AFTER) {
            if (sourceIndex < targetIndex) targetIndex--;
            entities.insert(entities.begin() + targetIndex + 1, tempSource);
        } else if (type == InsertionType::INTO) {
            // "IN" is ambiguous without hierarchy, treat as AFTER
            if (sourceIndex < targetIndex) targetIndex--;
            entities.insert(entities.begin() + targetIndex + 1, tempSource);
        }

    }

    return true;
}

void editor::ProjectUtils::moveEntityOrderByIndex(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity parent, size_t index, bool hasTransform){
    if (hasTransform){

        auto transforms = registry->getComponentArray<Transform>();
        size_t sourceTransformIndex = transforms->getIndex(source);

        size_t sizeOfSourceBranch = registry->findBranchLastIndex(source) - sourceTransformIndex + 1;
        if (sourceTransformIndex < index){
            index += sizeOfSourceBranch;
        }

        moveEntityOrderByTransform(registry, entities, source, parent, index);

    }else{

        auto itSource = std::find(entities.begin(), entities.end(), source);
        if (itSource == entities.end()) {
            Out::error("Source entity not found in entities vector for undo");
            return;
        }

        Entity tempSource = *itSource;
        entities.erase(itSource);

        // Clamp index for safety
        if (index > entities.size()) {
            entities.push_back(tempSource);
        } else {
            entities.insert(entities.begin() + index, tempSource);
        }

    }
}

void editor::ProjectUtils::moveEntityOrderByTransform(EntityRegistry* registry, std::vector<Entity>& entities, Entity source, Entity parent, size_t transformIndex, bool enableMove){
    registry->addEntityChild(parent, source, true);

    if (enableMove){
        registry->moveChildToIndex(source, transformIndex);
    }

    ProjectUtils::sortEntitiesByTransformOrder(registry, entities);
}

void editor::ProjectUtils::addEntityComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, std::vector<Entity>& entities, YAML::Node componentNode){
    switch (componentType) {
        case ComponentType::Transform:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Transform>(entity, {});
            }else{
                registry->addComponent<Transform>(entity, Stream::decodeTransform(componentNode));
            }
            ProjectUtils::sortEntitiesByTransformOrder(registry, entities);
            break;
        case ComponentType::MeshComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<MeshComponent>(entity, {});
            }else{
                registry->addComponent<MeshComponent>(entity, Stream::decodeMeshComponent(componentNode));
            }
            break;
        case ComponentType::UIComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<UIComponent>(entity, {});
            }else{
                registry->addComponent<UIComponent>(entity, Stream::decodeUIComponent(componentNode));
            }
            break;
        case ComponentType::UILayoutComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<UILayoutComponent>(entity, {});
            }else{
                registry->addComponent<UILayoutComponent>(entity, Stream::decodeUILayoutComponent(componentNode));
            }
            break;
        case ComponentType::ActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ActionComponent>(entity, {});
            }else{
                registry->addComponent<ActionComponent>(entity, Stream::decodeActionComponent(componentNode));
            }
            break;
        case ComponentType::AlphaActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<AlphaActionComponent>(entity, {});
            }else{
                registry->addComponent<AlphaActionComponent>(entity, Stream::decodeAlphaActionComponent(componentNode));
            }
            break;
        case ComponentType::AnimationComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<AnimationComponent>(entity, {});
            }else{
                registry->addComponent<AnimationComponent>(entity, Stream::decodeAnimationComponent(componentNode));
            }
            break;
        case ComponentType::SoundComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<SoundComponent>(entity, {});
            }else{
                registry->addComponent<SoundComponent>(entity, Stream::decodeSoundComponent(componentNode));
            }
            break;
        case ComponentType::Body2DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Body2DComponent>(entity, {});
            }else{
                registry->addComponent<Body2DComponent>(entity, Stream::decodeBody2DComponent(componentNode));
            }
            break;
        case ComponentType::Body3DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Body3DComponent>(entity, {});
            }else{
                Transform* bodyTransform = registry->findComponent<Transform>(entity);
                const Vector3 bodyScale = bodyTransform ? bodyTransform->scale : Vector3::UNIT_SCALE;
                registry->addComponent<Body3DComponent>(entity, Stream::decodeBody3DComponent(componentNode, nullptr, bodyScale));
            }
            break;
        case ComponentType::BoneComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<BoneComponent>(entity, {});
            }else{
                registry->addComponent<BoneComponent>(entity, Stream::decodeBoneComponent(componentNode));
            }
            break;
        case ComponentType::ButtonComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ButtonComponent>(entity, {});
            }else{
                registry->addComponent<ButtonComponent>(entity, Stream::decodeButtonComponent(componentNode));
            }
            break;
        case ComponentType::BundleComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<BundleComponent>(entity, {});
            }else{
                registry->addComponent<BundleComponent>(entity, Stream::decodeBundleComponent(componentNode));
            }
            break;
        case ComponentType::CameraComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<CameraComponent>(entity, {});
            }else{
                registry->addComponent<CameraComponent>(entity, Stream::decodeCameraComponent(componentNode));
            }
            break;
        case ComponentType::ColorActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ColorActionComponent>(entity, {});
            }else{
                registry->addComponent<ColorActionComponent>(entity, Stream::decodeColorActionComponent(componentNode));
            }
            break;
        case ComponentType::FogComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<FogComponent>(entity, {});
            }else{
                registry->addComponent<FogComponent>(entity, Stream::decodeFogComponent(componentNode));
            }
            break;
        case ComponentType::MirrorComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<MirrorComponent>(entity, {});
            }else{
                registry->addComponent<MirrorComponent>(entity, Stream::decodeMirrorComponent(componentNode));
            }
            break;
        case ComponentType::ReflectionProbeComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ReflectionProbeComponent>(entity, {});
            }else{
                registry->addComponent<ReflectionProbeComponent>(entity, Stream::decodeReflectionProbeComponent(componentNode));
            }
            break;
        case ComponentType::Light2DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Light2DComponent>(entity, {});
            }else{
                registry->addComponent<Light2DComponent>(entity, Stream::decodeLight2DComponent(componentNode));
            }
            break;
        case ComponentType::Occluder2DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Occluder2DComponent>(entity, {});
            }else{
                registry->addComponent<Occluder2DComponent>(entity, Stream::decodeOccluder2DComponent(componentNode));
            }
            break;
        case ComponentType::ImageComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ImageComponent>(entity, {});
            }else{
                registry->addComponent<ImageComponent>(entity, Stream::decodeImageComponent(componentNode));
            }
            break;
        case ComponentType::InstancedMeshComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<InstancedMeshComponent>(entity, {});
            }else{
                registry->addComponent<InstancedMeshComponent>(entity, Stream::decodeInstancedMeshComponent(componentNode));
            }
            break;
        case ComponentType::Joint2DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Joint2DComponent>(entity, {});
            }else{
                registry->addComponent<Joint2DComponent>(entity, Stream::decodeJoint2DComponent(componentNode));
            }
            break;
        case ComponentType::Joint3DComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<Joint3DComponent>(entity, {});
            }else{
                registry->addComponent<Joint3DComponent>(entity, Stream::decodeJoint3DComponent(componentNode));
            }
            break;
        case ComponentType::KeyframeTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<KeyframeTracksComponent>(entity, {});
            }else{
                registry->addComponent<KeyframeTracksComponent>(entity, Stream::decodeKeyframeTracksComponent(componentNode));
            }
            break;
        case ComponentType::LightComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<LightComponent>(entity, {});
            }else{
                registry->addComponent<LightComponent>(entity, Stream::decodeLightComponent(componentNode));
            }
            break;
        case ComponentType::LinesComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<LinesComponent>(entity, {});
            }else{
                registry->addComponent<LinesComponent>(entity, Stream::decodeLinesComponent(componentNode));
            }
            break;
        case ComponentType::MeshPolygonComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<MeshPolygonComponent>(entity, {});
            }else{
                registry->addComponent<MeshPolygonComponent>(entity, Stream::decodeMeshPolygonComponent(componentNode));
            }
            break;
        case ComponentType::ModelComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ModelComponent>(entity, {});
            }else{
                registry->addComponent<ModelComponent>(entity, Stream::decodeModelComponent(componentNode));
            }
            break;
        case ComponentType::MorphTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<MorphTracksComponent>(entity, {});
            }else{
                registry->addComponent<MorphTracksComponent>(entity, Stream::decodeMorphTracksComponent(componentNode));
            }
            break;
        case ComponentType::PanelComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PanelComponent>(entity, {});
            }else{
                registry->addComponent<PanelComponent>(entity, Stream::decodePanelComponent(componentNode));
            }
            break;
        case ComponentType::ParticlesComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ParticlesComponent>(entity, {});
            }else{
                registry->addComponent<ParticlesComponent>(entity, Stream::decodeParticlesComponent(componentNode));
            }
            break;
        case ComponentType::PointsComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PointsComponent>(entity, {});
            }else{
                registry->addComponent<PointsComponent>(entity, Stream::decodePointsComponent(componentNode));
            }
            break;
        case ComponentType::PolygonComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PolygonComponent>(entity, {});
            }else{
                registry->addComponent<PolygonComponent>(entity, Stream::decodePolygonComponent(componentNode));
            }
            break;
        case ComponentType::PositionActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<PositionActionComponent>(entity, {});
            }else{
                registry->addComponent<PositionActionComponent>(entity, Stream::decodePositionActionComponent(componentNode));
            }
            break;
        case ComponentType::ProgressbarComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ProgressbarComponent>(entity, {});
            }else{
                registry->addComponent<ProgressbarComponent>(entity, Stream::decodeProgressbarComponent(componentNode));
            }
            break;
        case ComponentType::RotateTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<RotateTracksComponent>(entity, {});
            }else{
                registry->addComponent<RotateTracksComponent>(entity, Stream::decodeRotateTracksComponent(componentNode));
            }
            break;
        case ComponentType::RotationActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<RotationActionComponent>(entity, {});
            }else{
                registry->addComponent<RotationActionComponent>(entity, Stream::decodeRotationActionComponent(componentNode));
            }
            break;
        case ComponentType::ScaleActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScaleActionComponent>(entity, {});
            }else{
                registry->addComponent<ScaleActionComponent>(entity, Stream::decodeScaleActionComponent(componentNode));
            }
            break;
        case ComponentType::ScaleTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScaleTracksComponent>(entity, {});
            }else{
                registry->addComponent<ScaleTracksComponent>(entity, Stream::decodeScaleTracksComponent(componentNode));
            }
            break;
        case ComponentType::ScriptComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScriptComponent>(entity, {});
            }else{
                registry->addComponent<ScriptComponent>(entity, Stream::decodeScriptComponent(componentNode));
            }
            break;
        case ComponentType::ScrollbarComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<ScrollbarComponent>(entity, {});
            }else{
                registry->addComponent<ScrollbarComponent>(entity, Stream::decodeScrollbarComponent(componentNode));
            }
            break;
        case ComponentType::SkyComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<SkyComponent>(entity, {});
            }else{
                registry->addComponent<SkyComponent>(entity, Stream::decodeSkyComponent(componentNode));
            }
            break;
        case ComponentType::SpriteAnimationComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<SpriteAnimationComponent>(entity, {});
            }else{
                registry->addComponent<SpriteAnimationComponent>(entity, Stream::decodeSpriteAnimationComponent(componentNode));
            }
            break;
        case ComponentType::SpriteComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<SpriteComponent>(entity, {});
            }else{
                registry->addComponent<SpriteComponent>(entity, Stream::decodeSpriteComponent(componentNode));
            }
            break;
        case ComponentType::TerrainComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TerrainComponent>(entity, {});
            }else{
                registry->addComponent<TerrainComponent>(entity, Stream::decodeTerrainComponent(componentNode));
            }
            break;
        case ComponentType::TextComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TextComponent>(entity, {});
            }else{
                registry->addComponent<TextComponent>(entity, Stream::decodeTextComponent(componentNode));
            }
            break;
        case ComponentType::TextEditComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TextEditComponent>(entity, {});
            }else{
                registry->addComponent<TextEditComponent>(entity, Stream::decodeTextEditComponent(componentNode));
            }
            break;
        case ComponentType::TilemapComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TilemapComponent>(entity, {});
            }else{
                registry->addComponent<TilemapComponent>(entity, Stream::decodeTilemapComponent(componentNode));
            }
            break;
        case ComponentType::TimedActionComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TimedActionComponent>(entity, {});
            }else{
                registry->addComponent<TimedActionComponent>(entity, Stream::decodeTimedActionComponent(componentNode));
            }
            break;
        case ComponentType::TranslateTracksComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<TranslateTracksComponent>(entity, {});
            }else{
                registry->addComponent<TranslateTracksComponent>(entity, Stream::decodeTranslateTracksComponent(componentNode));
            }
            break;
        case ComponentType::UIContainerComponent:
            if (!componentNode.IsDefined() || componentNode.IsNull()){
                registry->addComponent<UIContainerComponent>(entity, {});
            }else{
                registry->addComponent<UIContainerComponent>(entity, Stream::decodeUIContainerComponent(componentNode));
            }
            break;
        default:
            break;
    }

    Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(componentType));
}

Entity editor::ProjectUtils::getVirtualParent(Scene* scene, Entity entity) {
    Signature signature = scene->getSignature(entity);
    if (!signature.test(scene->getComponentId<ActionComponent>())) return NULL_ENTITY;
    if (signature.test(scene->getComponentId<Transform>())) return NULL_ENTITY;

    ActionComponent& action = scene->getComponent<ActionComponent>(entity);
    if (action.target != NULL_ENTITY && action.target != entity) {
        return action.target;
    }

    return NULL_ENTITY;
}

std::vector<Entity> editor::ProjectUtils::getVirtualChildren(Scene* scene, const std::vector<Entity>& parentEntities, const std::unordered_set<Entity>& excludedEntities) {
    std::vector<Entity> result;
    auto actionArr = scene->getComponentArray<ActionComponent>();
    if (!actionArr) {
        return result;
    }

    // Expand parent entities to include all Transform descendants
    std::vector<Entity> allEntities = parentEntities;
    auto transforms = scene->getComponentArray<Transform>();
    if (transforms) {
        for (const Entity& parent : parentEntities) {
            if (!scene->findComponent<Transform>(parent)) {
                continue;
            }
            size_t parentIdx = transforms->getIndex(parent);
            size_t branchEnd = scene->findBranchLastIndex(parent);
            for (size_t i = parentIdx + 1; i <= branchEnd; ++i) {
                Entity descendant = transforms->getEntity(i);
                if (std::find(allEntities.begin(), allEntities.end(), descendant) == allEntities.end()) {
                    allEntities.push_back(descendant);
                }
            }
        }
    }

    bool foundNew;
    do {
        foundNew = false;
        for (size_t i = 0; i < actionArr->size(); ++i) {
            Entity entity = actionArr->getEntity(i);
            if (excludedEntities.count(entity)) {
                continue;
            }
            Entity vParent = getVirtualParent(scene, entity);
            if (vParent != NULL_ENTITY
                && std::find(allEntities.begin(), allEntities.end(), vParent) != allEntities.end()
                && std::find(allEntities.begin(), allEntities.end(), entity) == allEntities.end()) {
                allEntities.push_back(entity);
                result.push_back(entity);
                foundNew = true;
            }
        }
    } while (foundNew);

    return result;
}

std::vector<Entity> editor::ProjectUtils::getOwnedCascadeEntities(EntityRegistry* registry,
        Entity owner, ComponentType componentType) {
    std::vector<Entity> result;
    auto append = [&](Entity entity) {
        if (entity != NULL_ENTITY && entity != owner &&
            std::find(result.begin(), result.end(), entity) == result.end()) {
            result.push_back(entity);
        }
    };

    if (componentType == ComponentType::ActionComponent) {
        const ActionComponent* action = registry->findComponent<ActionComponent>(owner);
        if (action && action->ownedTarget) {
            append(action->target);
        }
    } else if (componentType == ComponentType::AnimationComponent) {
        const AnimationComponent* animation = registry->findComponent<AnimationComponent>(owner);
        if (animation && animation->ownedActions) {
            for (const ActionFrame& frame : animation->actions) {
                append(frame.action);
            }
        }
    }

    return result;
}

YAML::Node editor::ProjectUtils::removeEntityComponent(EntityRegistry* registry, Entity entity, ComponentType componentType, std::vector<Entity>& entities, bool encodeComponent){
    YAML::Node oldComponent;

    // Editor commands own the undo policy for owned targets/actions. Suppress those two
    // callbacks here so component removal never destroys entities outside its recovery data.
    switch (componentType) {
        case ComponentType::Transform:
            if (encodeComponent){
                oldComponent = Stream::encodeTransform(registry->getComponent<Transform>(entity));
            }
            registry->removeComponent<Transform>(entity);
            ProjectUtils::sortEntitiesByTransformOrder(registry, entities);
            break;
        case ComponentType::MeshComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeMeshComponent(registry->getComponent<MeshComponent>(entity));
            }
            registry->removeComponent<MeshComponent>(entity);
            break;
        case ComponentType::UIComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeUIComponent(registry->getComponent<UIComponent>(entity));
            }
            registry->removeComponent<UIComponent>(entity);
            break;
        case ComponentType::UILayoutComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeUILayoutComponent(registry->getComponent<UILayoutComponent>(entity));
            }
            registry->removeComponent<UILayoutComponent>(entity);
            break;
        case ComponentType::ActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeActionComponent(registry->getComponent<ActionComponent>(entity));
            }
            registry->getComponent<ActionComponent>(entity).ownedTarget = false;
            registry->removeComponent<ActionComponent>(entity);
            break;
        case ComponentType::AlphaActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeAlphaActionComponent(registry->getComponent<AlphaActionComponent>(entity));
            }
            registry->removeComponent<AlphaActionComponent>(entity);
            break;
        case ComponentType::AnimationComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeAnimationComponent(registry->getComponent<AnimationComponent>(entity));
            }
            registry->getComponent<AnimationComponent>(entity).ownedActions = false;
            registry->removeComponent<AnimationComponent>(entity);
            break;
        case ComponentType::SoundComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeSoundComponent(registry->getComponent<SoundComponent>(entity));
            }
            registry->removeComponent<SoundComponent>(entity);
            break;
        case ComponentType::Body2DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBody2DComponent(registry->getComponent<Body2DComponent>(entity));
            }
            registry->removeComponent<Body2DComponent>(entity);
            break;
        case ComponentType::Body3DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBody3DComponent(registry->getComponent<Body3DComponent>(entity));
            }
            registry->removeComponent<Body3DComponent>(entity);
            break;
        case ComponentType::BoneComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBoneComponent(registry->getComponent<BoneComponent>(entity));
            }
            registry->removeComponent<BoneComponent>(entity);
            break;
        case ComponentType::ButtonComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeButtonComponent(registry->getComponent<ButtonComponent>(entity));
            }
            registry->removeComponent<ButtonComponent>(entity);
            break;
        case ComponentType::BundleComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeBundleComponent(registry->getComponent<BundleComponent>(entity));
            }
            registry->removeComponent<BundleComponent>(entity);
            break;
        case ComponentType::CameraComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeCameraComponent(registry->getComponent<CameraComponent>(entity));
            }
            registry->removeComponent<CameraComponent>(entity);
            break;
        case ComponentType::ColorActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeColorActionComponent(registry->getComponent<ColorActionComponent>(entity));
            }
            registry->removeComponent<ColorActionComponent>(entity);
            break;
        case ComponentType::FogComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeFogComponent(registry->getComponent<FogComponent>(entity));
            }
            registry->removeComponent<FogComponent>(entity);
            break;
        case ComponentType::MirrorComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeMirrorComponent(registry->getComponent<MirrorComponent>(entity));
            }
            registry->removeComponent<MirrorComponent>(entity);
            break;
        case ComponentType::ReflectionProbeComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeReflectionProbeComponent(registry->getComponent<ReflectionProbeComponent>(entity));
            }
            registry->removeComponent<ReflectionProbeComponent>(entity);
            break;
        case ComponentType::Light2DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeLight2DComponent(registry->getComponent<Light2DComponent>(entity));
            }
            registry->removeComponent<Light2DComponent>(entity);
            break;
        case ComponentType::Occluder2DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeOccluder2DComponent(registry->getComponent<Occluder2DComponent>(entity));
            }
            registry->removeComponent<Occluder2DComponent>(entity);
            break;
        case ComponentType::ImageComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeImageComponent(registry->getComponent<ImageComponent>(entity));
            }
            registry->removeComponent<ImageComponent>(entity);
            break;
        case ComponentType::InstancedMeshComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeInstancedMeshComponent(registry->getComponent<InstancedMeshComponent>(entity));
            }
            registry->removeComponent<InstancedMeshComponent>(entity);
            break;
        case ComponentType::Joint2DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeJoint2DComponent(registry->getComponent<Joint2DComponent>(entity));
            }
            registry->removeComponent<Joint2DComponent>(entity);
            break;
        case ComponentType::Joint3DComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeJoint3DComponent(registry->getComponent<Joint3DComponent>(entity));
            }
            registry->removeComponent<Joint3DComponent>(entity);
            break;
        case ComponentType::KeyframeTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeKeyframeTracksComponent(registry->getComponent<KeyframeTracksComponent>(entity));
            }
            registry->removeComponent<KeyframeTracksComponent>(entity);
            break;
        case ComponentType::LightComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeLightComponent(registry->getComponent<LightComponent>(entity));
            }
            registry->removeComponent<LightComponent>(entity);
            break;
        case ComponentType::LinesComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeLinesComponent(registry->getComponent<LinesComponent>(entity));
            }
            registry->removeComponent<LinesComponent>(entity);
            break;
        case ComponentType::MeshPolygonComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeMeshPolygonComponent(registry->getComponent<MeshPolygonComponent>(entity));
            }
            registry->removeComponent<MeshPolygonComponent>(entity);
            break;
        case ComponentType::ModelComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeModelComponent(registry->getComponent<ModelComponent>(entity));
            }
            registry->removeComponent<ModelComponent>(entity);
            break;
        case ComponentType::MorphTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeMorphTracksComponent(registry->getComponent<MorphTracksComponent>(entity));
            }
            registry->removeComponent<MorphTracksComponent>(entity);
            break;
        case ComponentType::PanelComponent:
            if (encodeComponent){
                oldComponent = Stream::encodePanelComponent(registry->getComponent<PanelComponent>(entity));
            }
            registry->removeComponent<PanelComponent>(entity);
            break;
        case ComponentType::ParticlesComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeParticlesComponent(registry->getComponent<ParticlesComponent>(entity));
            }
            registry->removeComponent<ParticlesComponent>(entity);
            break;
        case ComponentType::PointsComponent:
            if (encodeComponent){
                oldComponent = Stream::encodePointsComponent(registry->getComponent<PointsComponent>(entity));
            }
            registry->removeComponent<PointsComponent>(entity);
            break;
        case ComponentType::PolygonComponent:
            if (encodeComponent){
                oldComponent = Stream::encodePolygonComponent(registry->getComponent<PolygonComponent>(entity));
            }
            registry->removeComponent<PolygonComponent>(entity);
            break;
        case ComponentType::PositionActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodePositionActionComponent(registry->getComponent<PositionActionComponent>(entity));
            }
            registry->removeComponent<PositionActionComponent>(entity);
            break;
        case ComponentType::ProgressbarComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeProgressbarComponent(registry->getComponent<ProgressbarComponent>(entity));
            }
            registry->removeComponent<ProgressbarComponent>(entity);
            break;
        case ComponentType::RotateTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeRotateTracksComponent(registry->getComponent<RotateTracksComponent>(entity));
            }
            registry->removeComponent<RotateTracksComponent>(entity);
            break;
        case ComponentType::RotationActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeRotationActionComponent(registry->getComponent<RotationActionComponent>(entity));
            }
            registry->removeComponent<RotationActionComponent>(entity);
            break;
        case ComponentType::ScaleActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeScaleActionComponent(registry->getComponent<ScaleActionComponent>(entity));
            }
            registry->removeComponent<ScaleActionComponent>(entity);
            break;
        case ComponentType::ScaleTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeScaleTracksComponent(registry->getComponent<ScaleTracksComponent>(entity));
            }
            registry->removeComponent<ScaleTracksComponent>(entity);
            break;
        case ComponentType::ScriptComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeScriptComponent(registry->getComponent<ScriptComponent>(entity));
            }
            registry->removeComponent<ScriptComponent>(entity);
            break;
        case ComponentType::ScrollbarComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeScrollbarComponent(registry->getComponent<ScrollbarComponent>(entity));
            }
            registry->removeComponent<ScrollbarComponent>(entity);
            break;
        case ComponentType::SkyComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeSkyComponent(registry->getComponent<SkyComponent>(entity));
            }
            registry->removeComponent<SkyComponent>(entity);
            break;
        case ComponentType::SpriteAnimationComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeSpriteAnimationComponent(registry->getComponent<SpriteAnimationComponent>(entity));
            }
            registry->removeComponent<SpriteAnimationComponent>(entity);
            break;
        case ComponentType::SpriteComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeSpriteComponent(registry->getComponent<SpriteComponent>(entity));
            }
            registry->removeComponent<SpriteComponent>(entity);
            break;
        case ComponentType::TerrainComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTerrainComponent(registry->getComponent<TerrainComponent>(entity));
            }
            registry->removeComponent<TerrainComponent>(entity);
            break;
        case ComponentType::TextComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTextComponent(registry->getComponent<TextComponent>(entity));
            }
            registry->removeComponent<TextComponent>(entity);
            break;
        case ComponentType::TextEditComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTextEditComponent(registry->getComponent<TextEditComponent>(entity));
            }
            registry->removeComponent<TextEditComponent>(entity);
            break;
        case ComponentType::TilemapComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTilemapComponent(registry->getComponent<TilemapComponent>(entity));
            }
            registry->removeComponent<TilemapComponent>(entity);
            break;
        case ComponentType::TimedActionComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTimedActionComponent(registry->getComponent<TimedActionComponent>(entity));
            }
            registry->removeComponent<TimedActionComponent>(entity);
            break;
        case ComponentType::TranslateTracksComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeTranslateTracksComponent(registry->getComponent<TranslateTracksComponent>(entity));
            }
            registry->removeComponent<TranslateTracksComponent>(entity);
            break;
        case ComponentType::UIContainerComponent:
            if (encodeComponent){
                oldComponent = Stream::encodeUIContainerComponent(registry->getComponent<UIContainerComponent>(entity));
            }
            registry->removeComponent<UIContainerComponent>(entity);
            break;
        default:
            break;
    }

    Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(componentType));

    return oldComponent;
}

void editor::ProjectUtils::reconcileTrackedEntities(EntityRegistry* registry, std::vector<Entity>& entities, Project* project, uint32_t sceneId){
    bool clearSelection = false;
    SceneProject* sceneProject = project ? project->getScene(sceneId) : nullptr;

    auto firstRemoved = std::remove_if(entities.begin(), entities.end(), [&](Entity entity) {
        if (registry->isEntityCreated(entity)) {
            return false;
        }
        if (project && project->isSelectedEntity(sceneId, entity)) {
            clearSelection = true;
        }
        if (sceneProject && sceneProject->mainCamera == entity) {
            sceneProject->mainCamera = NULL_ENTITY;
        }
        return true;
    });
    entities.erase(firstRemoved, entities.end());

    if (clearSelection) {
        project->clearSelectedEntities(sceneId);
    }
}

ScriptPropertyValue editor::ProjectUtils::luaValueToScriptPropertyValue(lua_State* L, int idx, ScriptPropertyType type) {
    switch (type) {
    case ScriptPropertyType::Bool:
        return ScriptPropertyValue(lua_toboolean(L, idx) != 0);

    case ScriptPropertyType::Int:
        return ScriptPropertyValue((int)luaL_optinteger(L, idx, 0));

    case ScriptPropertyType::Float:
        return ScriptPropertyValue((float)luaL_optnumber(L, idx, 0.0));

    case ScriptPropertyType::String:
        if (lua_isstring(L, idx))
            return ScriptPropertyValue(std::string(lua_tostring(L, idx)));
        return ScriptPropertyValue(std::string());

    case ScriptPropertyType::Vector2: {
        Vector2 v(0, 0);
        if (lua_istable(L, idx)) {
            lua_rawgeti(L, idx, 1);
            lua_rawgeti(L, idx, 2);
            v.x = (float)luaL_optnumber(L, -2, 0.0);
            v.y = (float)luaL_optnumber(L, -1, 0.0);
            lua_pop(L, 2);
        }
        return ScriptPropertyValue(v);
    }

    case ScriptPropertyType::Vector3:
    case ScriptPropertyType::Color3: {
        Vector3 v(0, 0, 0);
        if (lua_istable(L, idx)) {
            lua_rawgeti(L, idx, 1);
            lua_rawgeti(L, idx, 2);
            lua_rawgeti(L, idx, 3);
            v.x = (float)luaL_optnumber(L, -3, 0.0);
            v.y = (float)luaL_optnumber(L, -2, 0.0);
            v.z = (float)luaL_optnumber(L, -1, 0.0);
            lua_pop(L, 3);
        }
        return ScriptPropertyValue(v);
    }

    case ScriptPropertyType::Vector4:
    case ScriptPropertyType::Color4: {
        Vector4 v(0, 0, 0, 1);
        if (lua_istable(L, idx)) {
            lua_rawgeti(L, idx, 1);
            lua_rawgeti(L, idx, 2);
            lua_rawgeti(L, idx, 3);
            lua_rawgeti(L, idx, 4);
            v.x = (float)luaL_optnumber(L, -4, 0.0);
            v.y = (float)luaL_optnumber(L, -3, 0.0);
            v.z = (float)luaL_optnumber(L, -2, 0.0);
            v.w = (float)luaL_optnumber(L, -1, 1.0);
            lua_pop(L, 4);
        }
        return ScriptPropertyValue(v);
    }

    case ScriptPropertyType::EntityReference: {
        // For now, leave as null entity. The editor will fill it later.
        return ScriptPropertyValue(EntityReference{NULL_ENTITY, 0});
    }
    }

    return ScriptPropertyValue{};
}

void editor::ProjectUtils::loadLuaScriptProperties(ScriptEntry& entry, const std::string& luaPath) {
    lua_State* L = LuaBinding::getLuaState();
    if (!L) return;

    if (luaL_dofile(L, luaPath.c_str()) != LUA_OK) {
        Out::error("Failed to load Lua script \"%s\": %s", luaPath.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }

    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    parseLuaPropertiesTable(L, entry);
}

void editor::ProjectUtils::loadLuaScriptPropertiesFromString(ScriptEntry& entry, const std::string& scriptContent, const std::string& chunkName) {
    lua_State* L = LuaBinding::getLuaState();
    if (!L) return;

    if (luaL_dostring(L, scriptContent.c_str()) != LUA_OK) {
        // Silently ignore parse errors for in-memory content (code may be mid-edit)
        lua_pop(L, 1);
        return;
    }

    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    parseLuaPropertiesTable(L, entry);
}

static void parseLuaPropertiesTable(lua_State* L, ScriptEntry& entry) {

    lua_getfield(L, -1, "properties");  // Stack: script_table, properties_table

    if (lua_istable(L, -1)) {
        entry.properties.clear();

        lua_pushnil(L);  // Stack: script_table, properties_table, nil
        while (lua_next(L, -2) != 0) {  // Stack: script_table, properties_table, key, property_table
            if (lua_istable(L, -1)) {
                ScriptProperty prop;

                lua_getfield(L, -1, "name");
                if (lua_isstring(L, -1)) prop.name = lua_tostring(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, -1, "displayName");
                prop.displayName = lua_isstring(L, -1) ? lua_tostring(L, -1) : prop.name;
                lua_pop(L, 1);

                lua_getfield(L, -1, "type");
                if (lua_isstring(L, -1)) {
                    std::string typeStr = lua_tostring(L, -1);
                    std::string typeLower = typeStr;
                    std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), 
                                [](unsigned char c){ return (char)std::tolower(c); });

                    if (typeLower == "bool" || typeLower == "boolean"){
                        prop.type = ScriptPropertyType::Bool;
                    }else if (typeLower == "int"  || typeLower == "integer"){
                        prop.type = ScriptPropertyType::Int;
                    }else if (typeLower == "float" || typeLower == "number"){
                        prop.type = ScriptPropertyType::Float;
                    }else if (typeLower == "string"){
                        prop.type = ScriptPropertyType::String;
                    }else if (typeLower == "vector2" || typeLower == "vec2"){
                        prop.type = ScriptPropertyType::Vector2;
                    }else if (typeLower == "vector3" || typeLower == "vec3"){
                        prop.type = ScriptPropertyType::Vector3;
                    }else if (typeLower == "vector4" || typeLower == "vec4"){
                        prop.type = ScriptPropertyType::Vector4;
                    }else if (typeLower == "color3"){
                        prop.type = ScriptPropertyType::Color3;
                    }else if (typeLower == "color4"){
                        prop.type = ScriptPropertyType::Color4;
                    }else if (typeLower == "entity" || typeLower == "entitypointer" || typeLower == "pointer"){
                        prop.type = ScriptPropertyType::EntityReference;
                    }else{
                        prop.type = ScriptPropertyType::EntityReference;
                        prop.ptrTypeName = typeStr;
                    }
                } else {
                    prop.type = ScriptPropertyType::Float;
                }
                lua_pop(L, 1);

                lua_getfield(L, -1, "default");
                prop.defaultValue = editor::ProjectUtils::luaValueToScriptPropertyValue(L, -1, prop.type);
                prop.value = prop.defaultValue;
                lua_pop(L, 1);

                entry.properties.push_back(std::move(prop));
            }
            lua_pop(L, 1);  // pop value, keep key
        }
    }

    lua_pop(L, 2);  // pop properties_table and script_table
}

void editor::ProjectUtils::collectEntities(const YAML::Node& entityNode, std::vector<Entity>& allEntities) {
    if (!entityNode || !entityNode.IsMap())
        return;

    if (entityNode["members"] && entityNode["members"].IsSequence()) {
        for (const auto& member : entityNode["members"]) {
            collectEntities(member, allEntities);
        }
        return;
    }

    if (entityNode["entity"]) {
        allEntities.push_back(entityNode["entity"].as<Entity>());
    }

    // Recursively process children
    if (entityNode["children"] && entityNode["children"].IsSequence()) {
        for (const auto& child : entityNode["children"]) {
            collectEntities(child, allEntities);
        }
    }
}

editor::Command* editor::ProjectUtils::buildDuplicateTileCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int tileIndex) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return nullptr;
    }

    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap || tileIndex >= tilemap->numTiles) {
        return nullptr;
    }

    unsigned int newSlot = tilemap->numTiles;
    if (newSlot >= tilemap->tiles.size()) {
        return nullptr;
    }

    ComponentType cpType = ComponentType::TilemapComponent;
    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

    const TileData& src = tilemap->tiles[tileIndex];
    std::string dstPrefix = "tiles[" + std::to_string(newSlot) + "]";

    // Copy tile data with a small position offset
    multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, dstPrefix + ".name", src.name);
    multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, dstPrefix + ".rectId", src.rectId);
    multiCmd->addPropertyCmd<Vector2>(project, sceneId, entity, cpType, dstPrefix + ".position", src.position + Vector2(10.0f, 10.0f));
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".width", src.width);
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".height", src.height);

    multiCmd->addPropertyCmd<unsigned int>(project, sceneId, entity, cpType, "numTiles", (unsigned int)(newSlot + 1));

    multiCmd->setNoMerge();
    return multiCmd;
}

editor::Command* editor::ProjectUtils::buildDeleteTileCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int tileIndex) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return nullptr;
    }

    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap || tileIndex >= tilemap->numTiles) {
        return nullptr;
    }

    ComponentType cpType = ComponentType::TilemapComponent;
    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

    // Shift tiles down
    for (unsigned int j = tileIndex; j < tilemap->numTiles - 1; j++) {
        std::string dstPrefix = "tiles[" + std::to_string(j) + "]";
        multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, dstPrefix + ".name", tilemap->tiles[j + 1].name);
        multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, dstPrefix + ".rectId", tilemap->tiles[j + 1].rectId);
        multiCmd->addPropertyCmd<Vector2>(project, sceneId, entity, cpType, dstPrefix + ".position", tilemap->tiles[j + 1].position);
        multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".width", tilemap->tiles[j + 1].width);
        multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, dstPrefix + ".height", tilemap->tiles[j + 1].height);
    }

    // Clear last slot
    unsigned int lastIdx = tilemap->numTiles - 1;
    std::string lastPrefix = "tiles[" + std::to_string(lastIdx) + "]";
    multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, lastPrefix + ".name", std::string(""));
    multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, lastPrefix + ".rectId", 0);
    multiCmd->addPropertyCmd<Vector2>(project, sceneId, entity, cpType, lastPrefix + ".position", Vector2(0, 0));
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, lastPrefix + ".width", 0.0f);
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, cpType, lastPrefix + ".height", 0.0f);

    // Decrement count
    multiCmd->addPropertyCmd<unsigned int>(project, sceneId, entity, cpType, "numTiles", (unsigned int)(tilemap->numTiles - 1));

    multiCmd->setNoMerge();
    return multiCmd;
}

editor::Command* editor::ProjectUtils::buildDeleteInstanceCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int instanceIndex) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return nullptr;
    }

    InstancedMeshComponent* instmesh = sceneProject->scene->findComponent<InstancedMeshComponent>(entity);
    if (!instmesh || instanceIndex >= instmesh->instances.size()) {
        return nullptr;
    }

    std::vector<InstanceData> newInstances = instmesh->instances;
    newInstances.erase(newInstances.begin() + instanceIndex);

    auto* cmd = new PropertyCmd<std::vector<InstanceData>>(project, sceneId, entity, ComponentType::InstancedMeshComponent, "instances", newInstances);
    cmd->setNoMerge();
    return cmd;
}

editor::Command* editor::ProjectUtils::buildDuplicateInstanceCmd(Project* project, uint32_t sceneId, Entity entity, unsigned int instanceIndex) {
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return nullptr;
    }

    InstancedMeshComponent* instmesh = sceneProject->scene->findComponent<InstancedMeshComponent>(entity);
    if (!instmesh || instanceIndex >= instmesh->instances.size()) {
        return nullptr;
    }

    std::vector<InstanceData> newInstances = instmesh->instances;
    newInstances.push_back(instmesh->instances[instanceIndex]);

    auto* cmd = new PropertyCmd<std::vector<InstanceData>>(project, sceneId, entity, ComponentType::InstancedMeshComponent, "instances", newInstances);
    cmd->setNoMerge();
    return cmd;
}

void editor::ProjectUtils::removeDynamicInstmesh(Entity entity, const YAML::Node& savedComponents, EntityRegistry* registry) {
    if (!savedComponents || savedComponents.IsNull()) return;
    if (savedComponents[Catalog::getComponentName(ComponentType::InstancedMeshComponent, true)]) return;
    if (entity == NULL_ENTITY || !registry->isEntityCreated(entity)) return;
    if (registry->getSignature(entity).test(registry->getComponentId<InstancedMeshComponent>())) {
        registry->removeComponent<InstancedMeshComponent>(entity);
    }
}

namespace {

using EntityClassInfo = editor::ProjectUtils::EntityClassInfo;

// Base each class inherits, stated next to the component that selects it
EntityClassInfo meshClass(const char* name)   { return {name, true,  true }; }
EntityClassInfo objectClass(const char* name) { return {name, true,  false}; }
EntityClassInfo handleClass(const char* name) { return {name, false, false}; }

} // namespace

editor::ProjectUtils::EntityClassInfo editor::ProjectUtils::getEntityClassInfo(Scene* scene, Entity entity) {
    if (!scene || entity == NULL_ENTITY || !scene->isEntityCreated(entity)) return handleClass("EntityHandle");

    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<ModelComponent>()))       return meshClass("Model");
    if (signature.test(scene->getComponentId<BoneComponent>()))        return objectClass("Bone");
    if (signature.test(scene->getComponentId<TilemapComponent>()))     return meshClass("Tilemap");
    if (signature.test(scene->getComponentId<TerrainComponent>()))     return meshClass("Terrain");
    if (signature.test(scene->getComponentId<SpriteComponent>()))      return meshClass("Sprite");
    if (signature.test(scene->getComponentId<PointsComponent>()))      return objectClass("Points");
    if (signature.test(scene->getComponentId<LinesComponent>()))       return objectClass("Lines");
    if (signature.test(scene->getComponentId<PolygonComponent>()))     return objectClass("Polygon");
    if (signature.test(scene->getComponentId<MeshPolygonComponent>())) return meshClass("MeshPolygon");
    if (signature.test(scene->getComponentId<MirrorComponent>()))      return meshClass("Mirror");
    if (signature.test(scene->getComponentId<MeshComponent>()))        return meshClass("Mesh");
    if (signature.test(scene->getComponentId<SkyComponent>()))         return handleClass("SkyBox");
    if (signature.test(scene->getComponentId<FogComponent>()))         return handleClass("Fog");
    if (signature.test(scene->getComponentId<SoundComponent>()))       return handleClass("Sound");
    if (signature.test(scene->getComponentId<ButtonComponent>()))      return objectClass("Button");
    if (signature.test(scene->getComponentId<ScrollbarComponent>()))   return objectClass("Scrollbar");
    if (signature.test(scene->getComponentId<ProgressbarComponent>())) return objectClass("Progressbar");
    if (signature.test(scene->getComponentId<TextEditComponent>()))    return objectClass("TextEdit");
    if (signature.test(scene->getComponentId<TextComponent>()))        return objectClass("Text");
    if (signature.test(scene->getComponentId<PanelComponent>()))       return objectClass("Panel");
    if (signature.test(scene->getComponentId<ImageComponent>()))       return objectClass("Image");
    if (signature.test(scene->getComponentId<UIContainerComponent>())) return objectClass("Container");
    if (signature.test(scene->getComponentId<UIComponent>()))          return objectClass("UILayout");
    if (signature.test(scene->getComponentId<Light2DComponent>()))     return objectClass("Light2D");
    if (signature.test(scene->getComponentId<Occluder2DComponent>()))  return objectClass("Occluder2D");
    if (signature.test(scene->getComponentId<LightComponent>()))       return objectClass("Light");
    if (signature.test(scene->getComponentId<CameraComponent>()))      return objectClass("Camera");
    if (signature.test(scene->getComponentId<ReflectionProbeComponent>())) return objectClass("ReflectionProbe");
    if (signature.test(scene->getComponentId<Body2DComponent>()))      return handleClass("Body2D");
    if (signature.test(scene->getComponentId<Body3DComponent>()))      return handleClass("Body3D");
    if (signature.test(scene->getComponentId<Joint2DComponent>()))     return handleClass("Joint2D");
    if (signature.test(scene->getComponentId<Joint3DComponent>()))     return handleClass("Joint3D");
    if (signature.test(scene->getComponentId<AlphaActionComponent>())) return handleClass("AlphaAction");
    if (signature.test(scene->getComponentId<ColorActionComponent>())) return handleClass("ColorAction");
    if (signature.test(scene->getComponentId<PositionActionComponent>())) return handleClass("PositionAction");
    if (signature.test(scene->getComponentId<RotationActionComponent>())) return handleClass("RotationAction");
    if (signature.test(scene->getComponentId<ScaleActionComponent>())) return handleClass("ScaleAction");
    if (signature.test(scene->getComponentId<TimedActionComponent>())) return handleClass("TimedAction");
    if (signature.test(scene->getComponentId<AnimationComponent>()))   return handleClass("Animation");
    if (signature.test(scene->getComponentId<SpriteAnimationComponent>())) return handleClass("SpriteAnimation");
    if (signature.test(scene->getComponentId<MorphTracksComponent>())) return handleClass("MorphTracks");
    if (signature.test(scene->getComponentId<RotateTracksComponent>())) return handleClass("RotateTracks");
    if (signature.test(scene->getComponentId<ScaleTracksComponent>())) return handleClass("ScaleTracks");
    if (signature.test(scene->getComponentId<TranslateTracksComponent>())) return handleClass("TranslateTracks");
    if (signature.test(scene->getComponentId<ParticlesComponent>()))   return handleClass("Particles");
    if (signature.test(scene->getComponentId<ActionComponent>()))      return handleClass("Action");
    if (signature.test(scene->getComponentId<Transform>()))            return objectClass("Object");

    // generic engine type for entities with no recognized component: valid as a
    // C++ pointer type (doriax::EntityHandle*) and known to the Lua dispatcher
    return handleClass("EntityHandle");
}

std::string editor::ProjectUtils::getEntityTypeName(Scene* scene, Entity entity) {
    return getEntityClassInfo(scene, entity).name;
}
