#include "Structure.h"

#include "external/IconsFontAwesome6.h"
#include "command/CommandHandle.h"
#include "command/type/MoveEntityOrderCmd.h"
#include "command/type/AddChildSceneCmd.h"
#include "command/type/RemoveChildSceneCmd.h"
#include "command/type/CreateEntityCmd.h"
#include "command/type/EntityNameCmd.h"
#include "command/type/SceneNameCmd.h"
#include "command/type/DeleteEntityCmd.h"
#include "command/type/DuplicateEntityCmd.h"
#include "command/type/AddComponentCmd.h"
#include "command/type/MultiPropertyCmd.h"
#include "component/InstancedMeshComponent.h"
#include "command/type/ImportEntityBundleCmd.h"
#include "command/type/RemoveEntityFromBundleCmd.h"
#include "command/type/AddEntityToBundleCmd.h"
#include "command/type/SetMainCameraCmd.h"
#include "util/EntityPayload.h"
#include "util/UIUtils.h"
#include "util/Util.h"
#include "util/ProjectUtils.h"
#include "Out.h"
#include "Stream.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

using namespace doriax;

std::vector<Entity> editor::Structure::getTopLevelSelectedEntities(Entity draggedEntity) {
    SceneProject* sceneProject = project->getSelectedScene();
    if (!sceneProject) {
        return {draggedEntity};
    }

    std::vector<Entity> selection = project->getSelectedEntities(sceneProject->id);
    if (selection.empty() || std::find(selection.begin(), selection.end(), draggedEntity) == selection.end()) {
        return {draggedEntity};
    }

    std::vector<Entity> result = Project::getTopLevelEntities(sceneProject->scene, selection);

    if (result.empty()) {
        result.push_back(draggedEntity);
    }

    return result;
}

editor::Structure::Structure(Project* project, SceneWindow* sceneWindow){
    this->project = project;
    this->sceneWindow = sceneWindow;
    this->windowOpen = true;
    this->focusRequested = false;
    this->openParent = NULL_ENTITY;
}

void editor::Structure::setOpen(bool open){
    if (open){
        if (!windowOpen){
            focusRequested = true;
        }
        windowOpen = true;
        return;
    }

    windowOpen = false;
    focusRequested = false;
}

bool editor::Structure::isOpen() const{
    return windowOpen;
}

void editor::Structure::showNewEntityMenu(bool isScene, Entity parent, bool addToBundle){
    if (isScene){
        parent = NULL_ENTITY;

        if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT"  Empty entity")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Entity", addToBundle));
        }

        ImGui::Separator();
    }

    if (ImGui::MenuItem(ICON_FA_SITEMAP"  Empty object")){
        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Object", EntityCreationType::OBJECT, parent, addToBundle));
        openParent = parent;
    }

    if (ImGui::MenuItem(ICON_FA_CLOUD"  Sky")){
        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Sky", EntityCreationType::SKY, parent, addToBundle));
        openParent = parent;
    }

    if (ImGui::MenuItem(ICON_FA_VIDEO"  Camera")){
        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Camera", EntityCreationType::CAMERA, parent, addToBundle));
        openParent = parent;
    }

    if (ImGui::BeginMenu(ICON_FA_VOLUME_HIGH"  Sound")){
        if (ImGui::MenuItem(ICON_FA_FILE_AUDIO"  Sound Source")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Sound Source", EntityCreationType::SOUND, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_VOLUME_HIGH"  3D Sound")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "3D Sound", EntityCreationType::SOUND_3D, parent, addToBundle));
            openParent = parent;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_DICE_D20"  Basic shape")){
        if (ImGui::MenuItem(ICON_FA_DICE_D20"  Box")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Box", EntityCreationType::BOX, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_DICE_D20"  Plane")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Plane", EntityCreationType::PLANE, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_DICE_D20"  Sphere")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Sphere", EntityCreationType::SPHERE, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_DICE_D20"  Cylinder")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Cylinder", EntityCreationType::CYLINDER, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_DICE_D20"  Capsule")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Capsule", EntityCreationType::CAPSULE, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_DICE_D20"  Torus")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Torus", EntityCreationType::TORUS, parent, addToBundle));
            openParent = parent;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_CUBES_STACKED"  2D")){
        if (ImGui::MenuItem(ICON_FA_IMAGE"  Sprite")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Sprite", EntityCreationType::SPRITE, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_BORDER_ALL"  Tilemap")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Tilemap", EntityCreationType::TILEMAP, parent, addToBundle));
            openParent = parent;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_WINDOW_RESTORE"  UI")){
        if (ImGui::MenuItem(ICON_FA_OBJECT_GROUP"  Container")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Container", EntityCreationType::CONTAINER, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_IMAGE"  Image")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Image", EntityCreationType::IMAGE, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_FONT"  Text")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Text", EntityCreationType::TEXT, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_SQUARE_CARET_RIGHT"  Button")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Button", EntityCreationType::BUTTON, parent, addToBundle));
            openParent = parent;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_LIGHTBULB"  Light")){
        if (ImGui::MenuItem(ICON_FA_LIGHTBULB"  Point")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Point Light", EntityCreationType::POINT_LIGHT, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_LIGHTBULB"  Directional")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Directional Light", EntityCreationType::DIRECTIONAL_LIGHT, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_LIGHTBULB"  Spot")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Spot Light", EntityCreationType::SPOT_LIGHT, parent, addToBundle));
            openParent = parent;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_ATOM"  Physics")){
        if (ImGui::MenuItem(ICON_FA_LINK"  Joint 2D")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Joint2D", EntityCreationType::JOINT2D, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_LINK"  Joint 3D")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Joint3D", EntityCreationType::JOINT3D, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_WEIGHT_HANGING"  Empty Body 2D")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Body2D", EntityCreationType::BODY2D, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_WEIGHT_HANGING"  Empty Body 3D")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Body3D", EntityCreationType::BODY3D, parent, addToBundle));
            openParent = parent;
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT"  Point Sprites")){
        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "PointSprites", EntityCreationType::POINTS, parent, addToBundle));
        openParent = parent;
    }

    if (ImGui::MenuItem(ICON_FA_FIRE"  Particles")){
        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Particles", EntityCreationType::PARTICLES, parent, addToBundle));
        openParent = parent;
    }

    if (ImGui::BeginMenu(ICON_FA_FILM"  Animation")){
        if (ImGui::MenuItem(ICON_FA_FILM"  Animation")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Animation", EntityCreationType::ANIMATION, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_PLAY"  Sprite Animation")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "SpriteAnimation", EntityCreationType::SPRITE_ANIMATION, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT"  Position Action")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "PositionAction", EntityCreationType::POSITION_ACTION, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_ROTATE"  Rotation Action")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "RotationAction", EntityCreationType::ROTATION_ACTION, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER"  Scale Action")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "ScaleAction", EntityCreationType::SCALE_ACTION, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_PALETTE"  Color Action")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "ColorAction", EntityCreationType::COLOR_ACTION, parent, addToBundle));
            openParent = parent;
        }
        if (ImGui::MenuItem(ICON_FA_EYE"  Alpha Action")){
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "AlphaAction", EntityCreationType::ALPHA_ACTION, parent, addToBundle));
            openParent = parent;
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(ICON_FA_PERSON_RUNNING"  Model")){
        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Model", EntityCreationType::MODEL, parent, addToBundle));
        openParent = parent;
    }

    if (ImGui::MenuItem(ICON_FA_MOUNTAIN"  Terrain")){
        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new CreateEntityCmd(project, project->getSelectedSceneId(), "Terrain", EntityCreationType::TERRAIN, parent, addToBundle));
        openParent = parent;
    }

    ImGui::EndMenu();
}

void editor::Structure::showIconMenu(){
    if (ImGui::Button(ICON_FA_PLUS)) {
        ImGui::OpenPopup("NewObjectMenu");
    }

    ImGui::SameLine();

    UIUtils::searchInput("##structure_search", "", searchBuffer, sizeof(searchBuffer), false, &matchCase);

    if (ImGui::BeginPopup("NewObjectMenu"))
    {
        ImGui::Text("New scene:");
        ImVec2 buttonSize = ImVec2(ImGui::GetFontSize() * 8, ImGui::GetFontSize() * 2);
        if (ImGui::Button(ICON_FA_CUBES "  3D Scene", buttonSize)) {
            project->createNewScene("New Scene", SceneType::SCENE_3D);
            ImGui::CloseCurrentPopup();
        }
        //ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CUBES_STACKED "  2D Scene", buttonSize)) {
            project->createNewScene("New Scene", SceneType::SCENE_2D);
            ImGui::CloseCurrentPopup();
        }
        //ImGui::SameLine();
        if (ImGui::Button(ICON_FA_WINDOW_RESTORE "  UI Scene", buttonSize)) {
            project->createNewScene("New Scene", SceneType::SCENE_UI);
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_FA_CIRCLE_DOT"  Create entity"))
        {
            showNewEntityMenu(true, NULL_ENTITY, false);
        }

        ImGui::EndPopup();
    }
}

void editor::Structure::drawInsertionMarker(const ImVec2& p1, const ImVec2& p2) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
    float thickness = 2.0f;
    draw_list->AddLine(p1, p2, col, thickness);
}

std::string editor::Structure::getObjectIcon(Signature signature, Scene* scene){
    if (signature.test(scene->getComponentId<BundleComponent>())){
        return ICON_FA_CUBE;
    }else if (signature.test(scene->getComponentId<ModelComponent>())){
        return ICON_FA_PERSON_WALKING;
    }else if (signature.test(scene->getComponentId<BoneComponent>())){
        return ICON_FA_BONE;
    }else if (signature.test(scene->getComponentId<TilemapComponent>())){
        return ICON_FA_BORDER_ALL;
    }else if (signature.test(scene->getComponentId<TerrainComponent>())){
        return ICON_FA_MOUNTAIN;
    }else if (signature.test(scene->getComponentId<MeshComponent>())){
        return ICON_FA_DICE_D20;
    }else if (signature.test(scene->getComponentId<SkyComponent>())){
        return ICON_FA_CLOUD;
    }else if (signature.test(scene->getComponentId<UIContainerComponent>())){
        return ICON_FA_OBJECT_GROUP;
    }else if (signature.test(scene->getComponentId<ButtonComponent>())){
        return ICON_FA_SQUARE_CARET_RIGHT;
    }else if (signature.test(scene->getComponentId<TextComponent>())){
        return ICON_FA_FONT;
    }else if (signature.test(scene->getComponentId<TextEditComponent>())){
        return ICON_FA_I_CURSOR;
    }else if (signature.test(scene->getComponentId<ImageComponent>())){
        return ICON_FA_IMAGE;
    }else if (signature.test(scene->getComponentId<PanelComponent>())){
        return ICON_FA_SQUARE;
    }else if (signature.test(scene->getComponentId<UIComponent>())){
        return ICON_FA_IMAGE;
    }else if (signature.test(scene->getComponentId<LightComponent>())){
        return ICON_FA_LIGHTBULB;
    }else if (signature.test(scene->getComponentId<CameraComponent>())){
        return ICON_FA_VIDEO;
    }else if (signature.test(scene->getComponentId<Joint2DComponent>()) || signature.test(scene->getComponentId<Joint3DComponent>())){
        return ICON_FA_LINK;
    }else if (signature.test(scene->getComponentId<AnimationComponent>())){
        return ICON_FA_FILM;
    }else if (signature.test(scene->getComponentId<ParticlesComponent>())){
        return ICON_FA_FIRE;
    }else if (signature.test(scene->getComponentId<PointsComponent>())){
        return ICON_FA_CIRCLE_DOT;
    }else if (signature.test(scene->getComponentId<ActionComponent>())){
        return ICON_FA_PLAY;
    }else if (signature.test(scene->getComponentId<AudioComponent>())){
        if (signature.test(scene->getComponentId<Transform>())) {
            return ICON_FA_VOLUME_HIGH;
        }
        return ICON_FA_FILE_AUDIO;
    }else if (signature.test(scene->getComponentId<Transform>())){
        return ICON_FA_SITEMAP;
    }

    return ICON_FA_CIRCLE_DOT;
}

void editor::Structure::moveEntityToRootLevel(Entity sourceEntity, const std::unordered_set<Entity>& entitiesSet) {
    SceneProject* sceneProject = project->getSelectedScene();
    auto transforms = sceneProject->scene->getComponentArray<Transform>();
    Entity lastRootEntity = NULL_ENTITY;

    for (int i = transforms->size() - 1; i >= 0; i--) {
        Transform& transform = transforms->getComponentFromIndex(i);
        Entity entity = transforms->getEntity(i);

        if (transform.parent == NULL_ENTITY &&
            entitiesSet.find(entity) != entitiesSet.end()) {
            lastRootEntity = entity;
            break;
        }
    }

    if (lastRootEntity != NULL_ENTITY && lastRootEntity != sourceEntity) {
        CommandHandle::get(project->getSelectedSceneId())->addCommand(new MoveEntityOrderCmd(project, project->getSelectedSceneId(), sourceEntity, lastRootEntity, InsertionType::AFTER));
    }
}

void editor::Structure::handleEntityFilesDrop(const std::vector<std::string>& filePaths, Entity parent) {
    for (const std::string& filePath : filePaths) {
        std::filesystem::path path(filePath);

        if (path.extension() == ".bundle") {
            // Import the entity bundle into the current scene using command
            std::filesystem::path relativePath = std::filesystem::relative(path, project->getProjectPath());

            ImportEntityBundleCmd* importCmd = new ImportEntityBundleCmd(project, project->getSelectedSceneId(), relativePath, parent, true);
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(importCmd);

            std::vector<Entity> newEntities = importCmd->getImportedEntities();

            if (newEntities.size() > 0) {
                Out::info("Successfully imported bundle from: %s", path.string().c_str());
            } else {
                Out::warning("Failed to import bundle from: %s", path.string().c_str());
            }
        }
    }
}

void editor::Structure::handleSceneFilesDropAsChildScenes(const std::vector<std::string>& filePaths, uint32_t ownerSceneId) {
    if (ownerSceneId == NULL_PROJECT_SCENE) {
        return;
    }

    auto normalizeDroppedPath = [this](const std::filesystem::path& p) -> std::filesystem::path {
        if (p.empty()) {
            return p;
        }
        if (p.is_relative()) {
            return (project->getProjectPath() / p).lexically_normal();
        }
        return p.lexically_normal();
    };

    auto pathsReferToSameFile = [](const std::filesystem::path& a, const std::filesystem::path& b) -> bool {
        std::error_code ec;
        if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec)) {
            if (std::filesystem::equivalent(a, b, ec) && !ec) {
                return true;
            }
        }
        return a.lexically_normal() == b.lexically_normal();
    };

    for (const std::string& filePath : filePaths) {
        std::filesystem::path droppedPath = normalizeDroppedPath(std::filesystem::path(filePath));
        if (droppedPath.extension() != ".scene") {
            continue;
        }

        uint32_t droppedSceneId = NULL_PROJECT_SCENE;
        for (const auto& scene : project->getScenes()) {
            if (!scene.filepath.empty() && pathsReferToSameFile(scene.filepath, droppedPath)) {
                droppedSceneId = scene.id;
                break;
            }
        }

        if (droppedSceneId == NULL_PROJECT_SCENE) {
            Out::warning("Dropped scene is not part of the current project: %s", droppedPath.string().c_str());
            continue;
        }

        CommandHandle::get(ownerSceneId)->addCommand(new AddChildSceneCmd(project, ownerSceneId, droppedSceneId));
    }
}

void editor::Structure::showAddChildSceneMenu() {
    if (ImGui::BeginMenu(ICON_FA_FOLDER_TREE "  Add child scene")) {
        uint32_t currentSceneId = project->getSelectedSceneId();
        const auto& scenes = project->getScenes();

        bool hasAvailableScenes = false;
        for (const auto& scene : scenes) {
            // Skip current scene and already added child scenes
            if (scene.id == currentSceneId) {
                continue;
            }
            if (project->hasChildScene(currentSceneId, scene.id)) {
                continue;
            }

            hasAvailableScenes = true;
            std::string menuLabel = scene.name + " (ID: " + std::to_string(scene.id) + ")";
            if (ImGui::MenuItem(menuLabel.c_str())) {
                CommandHandle::get(currentSceneId)->addCommand(new AddChildSceneCmd(project, currentSceneId, scene.id));
            }
        }

        if (!hasAvailableScenes) {
            ImGui::TextDisabled("No available scenes");
        }

        ImGui::EndMenu();
    }
}

bool editor::Structure::nodeMatchesSearch(const TreeNode& node, const std::string& searchLower) {
    std::string nodeName = node.name;
    std::string searchStr = searchLower;

    if (!matchCase) {
        // Convert both to lowercase for case-insensitive search
        std::transform(nodeName.begin(), nodeName.end(), nodeName.begin(), ::tolower);
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
    }

    // Check if the node name contains the search string
    return nodeName.find(searchStr) != std::string::npos;
}

bool editor::Structure::hasMatchingDescendant(const TreeNode& node, const std::string& searchLower) {
    // Check if any child matches
    for (const auto& child : node.children) {
        if (nodeMatchesSearch(child, searchLower)) {
            return true;
        }
        // Recursively check descendants
        if (hasMatchingDescendant(child, searchLower)) {
            return true;
        }
    }
    return false;
}

void editor::Structure::markMatchingNodes(TreeNode& node, const std::string& searchLower) {
    node.matchesSearch = nodeMatchesSearch(node, searchLower);
    node.hasMatchingDescendant = false;

    for (auto& child : node.children) {
        markMatchingNodes(child, searchLower);
        if (child.matchesSearch || child.hasMatchingDescendant) {
            node.hasMatchingDescendant = true;
        }
    }
}

void editor::Structure::showTreeNode(editor::TreeNode& node) {
    // Skip nodes that don't match search and don't have matching descendants
    bool hasSearch = strlen(searchBuffer) > 0;
    if (hasSearch && !node.matchesSearch && !node.hasMatchingDescendant) {
        return;
    }

    pushNodeImGuiId(node);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;

    if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if ((node.isScene && std::find(selectedScenes.begin(), selectedScenes.end(), node.id) != selectedScenes.end()) ||
        (node.isChildScene && std::find(selectedScenes.begin(), selectedScenes.end(), node.childSceneId) != selectedScenes.end())) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool isChildSceneEntity = (!node.isScene && !node.isChildScene && node.entitySceneId != 0 && node.entitySceneId != project->getSelectedSceneId());

    if (!node.isScene && !node.isChildScene && !isChildSceneEntity && project->isSelectedEntity(project->getSelectedSceneId(), node.id)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (isChildSceneEntity && project->isSelectedEntity(node.entitySceneId, node.id)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Auto-expand nodes when searching to show matches
    if (hasSearch && node.hasMatchingDescendant) {
        ImGui::SetNextItemOpen(true);
    } else if (!hasSearch) {
        ImGui::SetNextItemOpen(!node.isBone, ImGuiCond_Once);
    }

    if(!node.isScene && openParent == node.id) {
        ImGui::SetNextItemOpen(true);
        openParent = NULL_ENTITY;
    }

    // Push colors for visual feedback
    bool pushedHighlightColor = false;

    // Highlight matching nodes when searching
    if (hasSearch && node.matchesSearch) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.5f, 1.0f)); // Yellow for search matches
        pushedHighlightColor = true;
    } else if (!node.isScene && !node.isChildScene && node.isLocked && node.isBundle) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.67f, 0.74f, 0.85f, 0.95f)); // Muted blue-gray for locked bundle entities
        pushedHighlightColor = true;
    } else if (!node.isScene && !node.isChildScene && node.isLocked) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); // Use theme's disabled color
        pushedHighlightColor = true;
    } else if (node.isChildScene) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.80f, 0.85f, 1.0f)); // Soft teal for child scenes
        pushedHighlightColor = true;
    } else if (node.isBundle && !node.isParentBundle) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.8f, 1.0f)); // Pale gold for bundle root
        pushedHighlightColor = true;
    } else if (node.isBundleRoot && node.isParentBundle) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f)); // Light blue for nested bundle root
        pushedHighlightColor = true;
    } else if (node.isBundle && node.isParentBundle) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.82f, 1.0f, 0.82f)); // Softer blue for bundle children
        pushedHighlightColor = true;
    }

    bool nodeOpen = ImGui::TreeNodeEx("##node", flags, "%s  %s", node.icon.c_str(), node.name.c_str());
    bool nodeHovered = ImGui::IsItemHovered();
    bool nodeActive = ImGui::IsItemActive();
    bool nodeRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    // Pop color if we pushed it
    if (pushedHighlightColor) {
        ImGui::PopStyleColor();
    }

    if (node.isScene){
        ImGui::SetItemTooltip("Id: %u", node.id);
    }else if (node.isChildScene){
        ImGui::SetItemTooltip("Child Scene\nId: %u", node.childSceneId);
    }else{
        if (node.isBundle) {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                std::filesystem::path tooltipPath = node.isBundleRoot ? node.nestedBundleFilepath : node.bundleFilepath;
                EntityBundle* bundle = project->getEntityBundle(tooltipPath);
                uint32_t instanceId = bundle ? bundle->getInstanceId(project->getSelectedSceneId(), node.id) : 0;
                Entity registryEntity = bundle ? bundle->getRegistryEntity(project->getSelectedSceneId(), node.id) : NULL_ENTITY;

                ImGui::BeginTooltip();
                ImGui::Text("Entity: %u", node.id);
                ImGui::Separator();
                ImGui::Text("Path: %s", tooltipPath.string().c_str());
                if (instanceId != 0) {
                    ImGui::Text("Instance: %u", instanceId);
                }
                if (registryEntity != NULL_ENTITY) {
                    ImGui::Text("Bundle: %u", registryEntity);
                }
                ImGui::EndTooltip();
            }
        } else {
            ImGui::SetItemTooltip("Entity: %u", node.id);
        }
    }

    if (!node.isChildScene && !isChildSceneEntity) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            // Add entity drag drop payload for dragging to resources
            if (!node.isScene) {
                SceneProject* sceneProject = project->getSelectedScene();
                std::vector<Entity> draggedEntities = getTopLevelSelectedEntities(node.id);
                std::vector<Entity> exportEntities = draggedEntities;

                // Add virtual children (and chained children) of dragged entities
                std::vector<Entity> virtualChildren = ProjectUtils::getVirtualChildren(sceneProject->scene, draggedEntities);
                exportEntities.insert(exportEntities.end(), virtualChildren.begin(), virtualChildren.end());

                if (draggedEntities.size() == 1 && exportEntities.size() == 1){
                    YAML::Node entityData = Stream::encodeEntity(draggedEntities[0], sceneProject->scene, project, sceneProject);

                    std::string yamlString = YAML::Dump(entityData);

                    size_t yamlSize = yamlString.size();
                    size_t payloadSize = sizeof(EntityPayload) + yamlSize;
                    std::vector<char> payloadData(payloadSize);

                    EntityPayload* payload = reinterpret_cast<EntityPayload*>(payloadData.data());
                    payload->entity = node.id;
                    payload->parent = node.parent;
                    payload->order = node.order;
                    payload->hasTransform = node.hasTransform;
                    payload->entitySceneId = sceneProject->id;
                    memcpy(payloadData.data() + sizeof(EntityPayload), yamlString.data(), yamlSize);

                    ImGui::SetDragDropPayload("entity", payloadData.data(), payloadSize);
                }else{
                    YAML::Node entityData = Stream::encodeEntitySelection(exportEntities, sceneProject->scene);

                    std::string yamlString = YAML::Dump(entityData);

                    ImGui::SetDragDropPayload("bundle", yamlString.data(), yamlString.size());
                }
        }

        ImGui::Text("Moving %s", node.name.c_str());
        ImGui::EndDragDropSource();
    }
    }

    // Child scene entities: allow drag for cross-scene entity reference (ExternalEntity properties)
    if (isChildSceneEntity && !node.isScene) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            SceneProject* childSceneProject = project->getScene(node.entitySceneId);
            if (childSceneProject && childSceneProject->scene) {
                YAML::Node entityData = Stream::encodeEntity(node.id, childSceneProject->scene, project, childSceneProject);
                std::string yamlString = YAML::Dump(entityData);

                size_t yamlSize = yamlString.size();
                size_t payloadSize = sizeof(EntityPayload) + yamlSize;
                std::vector<char> payloadData(payloadSize);

                EntityPayload* payload = reinterpret_cast<EntityPayload*>(payloadData.data());
                payload->entity = node.id;
                payload->parent = node.parent;
                payload->order = node.order;
                payload->hasTransform = node.hasTransform;
                payload->entitySceneId = node.entitySceneId;
                memcpy(payloadData.data() + sizeof(EntityPayload), yamlString.data(), yamlSize);

                ImGui::SetDragDropPayload("entity", payloadData.data(), payloadSize);
            }

            ImGui::Text("Reference %s", node.name.c_str());
            ImGui::EndDragDropSource();
        }
    }

    bool insertBefore = false;
    bool insertAfter = false;

    if (!node.isChildScene && !isChildSceneEntity && ImGui::BeginDragDropTarget()) {
        bool allowEntityDragDrop = false;
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        Entity sourceEntity = 0;
        Entity sourceParent = 0;
        size_t sourceOrder = 0;
        bool sourceHasTransform = false;
        if (payload && payload->IsDataType("entity")) {
            if (payload->DataSize >= sizeof(EntityPayload)) {
                allowEntityDragDrop = true;
                const EntityPayload* p = reinterpret_cast<const EntityPayload*>(payload->Data);
                sourceEntity = p->entity;
                sourceParent = p->parent;
                sourceOrder = p->order;
                sourceHasTransform = p->hasTransform;
            }

            if (sourceHasTransform != node.hasTransform && !node.isScene) {
                allowEntityDragDrop = false;
            }
            // For scene node, only allow if entity has a parent (has transform and is a child)
            if (node.isScene && (!sourceHasTransform || sourceParent == NULL_ENTITY)) {
                allowEntityDragDrop = false;
            }

        }

        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImVec2 itemMax = ImGui::GetItemRectMax();
        if (!node.isScene && node.hasTransform){
            insertBefore = (mousePos.y - itemMin.y) < (itemMax.y - itemMin.y) * 0.2f;
            insertAfter = (mousePos.y - itemMin.y) > (itemMax.y - itemMin.y) * 0.8f;
        }else{
            insertBefore = (mousePos.y - itemMin.y) < (itemMax.y - itemMin.y) * 0.5f;
            insertAfter = (mousePos.y - itemMin.y) >= (itemMax.y - itemMin.y) * 0.5f;
        }

        if (node.parent == sourceParent){
            if (node.order == (sourceOrder+1) && insertBefore){
                allowEntityDragDrop = false;
            }
            if (node.order == (sourceOrder-1) && insertAfter){
                allowEntityDragDrop = false;
            }
        }

        // Block reordering between entities with different virtual parents
        if (!sourceHasTransform && !node.hasTransform && !node.isScene
            && (insertBefore || insertAfter)
            && sourceParent != node.parent
            && (sourceParent != NULL_ENTITY || node.parent != NULL_ENTITY)) {
            allowEntityDragDrop = false;
        }

        if (allowEntityDragDrop && ProjectUtils::isEntityLocked(project->getSelectedScene()->scene, sourceEntity)) {
            if (node.isScene) {
                allowEntityDragDrop = false;
            } else {
                InsertionType insertionType;
                if (insertBefore) {
                    insertionType = InsertionType::BEFORE;
                } else if (insertAfter) {
                    insertionType = InsertionType::AFTER;
                } else {
                    insertionType = InsertionType::INTO;
                }

                allowEntityDragDrop = ProjectUtils::canMoveLockedEntityOrder(project->getSelectedScene()->scene, sourceEntity, node.id, insertionType);
            }
        }

        if (allowEntityDragDrop){
            ImGuiDragDropFlags flags = 0;
            //ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery;

            if (!node.isScene && (insertBefore || insertAfter)){
                flags |= ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
            }

            if (ImGui::AcceptDragDropPayload("entity", flags)) {

                if (payload->IsDelivery()){
                    if (node.isScene) {
                        SceneProject* sceneProject = project->getSelectedScene();
                        std::unordered_set<Entity> entitiesSet(sceneProject->entities.begin(), sceneProject->entities.end());
                        moveEntityToRootLevel(sourceEntity, entitiesSet);
                    } else {
                        InsertionType type;
                        if (insertBefore){
                            type = InsertionType::BEFORE;
                        }else if (insertAfter){
                            type = InsertionType::AFTER;
                        }else{
                            type = InsertionType::INTO;
                        }
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(new MoveEntityOrderCmd(project, project->getSelectedSceneId(), sourceEntity, node.id, type));
                    }
                }

            }

            if (!node.isScene && insertBefore) {
                const ImVec2& padding = ImGui::GetStyle().FramePadding;

                ImVec2 lineStart = ImGui::GetCursorScreenPos();
                ImVec2 lineEnd = lineStart;
                lineEnd.x += ImGui::GetContentRegionAvail().x;

                lineStart.y -= padding.y * 2.0;
                lineEnd.y -= padding.y * 2.0;

                ImVec2 node_size = ImGui::GetItemRectSize();

                lineStart.y -= node_size.y;
                lineEnd.y -= node_size.y;

                drawInsertionMarker(lineStart, lineEnd);
            }

            if (!node.isScene && insertAfter) {
                const ImVec2& padding = ImGui::GetStyle().FramePadding;

                ImVec2 lineStart = ImGui::GetCursorScreenPos();
                ImVec2 lineEnd = lineStart;
                lineEnd.x += ImGui::GetContentRegionAvail().x;

                lineStart.y -= padding.y;
                lineEnd.y -= padding.y;

                drawInsertionMarker(lineStart, lineEnd);
            }

        }

        bool allowResourceDragDrop = false;
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload()) {
            if (payload->IsDataType("resource_files")) {
                const char* data = (const char*)payload->Data;
                size_t dataSize = payload->DataSize;

                size_t offset = 0;
                while (offset < dataSize) {
                    std::string path(data + offset);
                    if (!path.empty()) {
                        if (node.isScene) {
                            if (Util::isSceneFile(path) || Util::isBundleFile(path)) {
                                allowResourceDragDrop = true;
                                break;
                            }
                        } else {
                            if (Util::isBundleFile(path)) {
                                allowResourceDragDrop = true;
                                break;
                            }
                        }
                    }
                    offset += path.size() + 1;
                }
            }
        }

        if (allowResourceDragDrop) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
                // Parse the dropped file paths
                std::vector<std::string> droppedPaths;
                const char* data = (const char*)payload->Data;
                size_t dataSize = payload->DataSize;

                // Parse null-terminated strings from the payload
                size_t offset = 0;
                while (offset < dataSize) {
                    std::string path(data + offset);
                    if (!path.empty()) {
                        droppedPaths.push_back(path);
                    }
                    offset += path.size() + 1; // +1 for null terminator
                }

                // Dropping on the scene root: allow adding child scenes via .scene files,
                // and keep supporting .bundle imports.
                if (node.isScene) {
                    handleSceneFilesDropAsChildScenes(droppedPaths, node.id);
                    handleEntityFilesDrop(droppedPaths, NULL_ENTITY);
                } else {
                    // Determine the parent entity for the drop
                    Entity parentEntity = NULL_ENTITY;
                    // If dropping on an entity, use it as parent if it has transform
                    if (node.hasTransform) {
                        parentEntity = node.id;
                    }
                    handleEntityFilesDrop(droppedPaths, parentEntity);
                }

                if (payload->IsDelivery()) {
                    ImGui::SetWindowFocus(Structure::WINDOW_NAME);
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        project->clearAllSelections(project->getSelectedSceneId());
        selectedScenes.clear();
        project->setSelectedSceneForProperties(project->getSelectedSceneId());
    }

    // Check for selection on mouse release (not click) to allow drag without selection
    if (nodeHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        if (node.isChildScene) {
            project->clearAllSelections(project->getSelectedSceneId());
            selectedScenes.clear();
            selectedScenes.push_back(node.childSceneId);
            project->setSelectedSceneForProperties(node.childSceneId);
        } else if (isChildSceneEntity) {
            ImGuiIO& io = ImGui::GetIO();
            if (!io.KeyShift){
                project->clearAllSelections(project->getSelectedSceneId());
            }
            selectedScenes.clear();
            project->addSelectedEntity(node.entitySceneId, node.id);
            project->setSelectedSceneForProperties(node.entitySceneId);
            // Clear any selected instance or tile that belonged to this entity
            SceneProject* sp = project->getScene(node.entitySceneId);
            if (sp && sp->sceneRender) {
                if (sp->sceneRender->getSelectedInstanceEntity() == node.id) {
                    sp->sceneRender->clearInstanceSelection();
                }
                if (sp->sceneRender->getSelectedTileEntity() == (Entity)node.id) {
                    sp->sceneRender->clearTileSelection();
                }
            }
        } else if (!node.isScene){
            ImGuiIO& io = ImGui::GetIO();
            if (!io.KeyShift){
                project->clearAllSelections(project->getSelectedSceneId());
            }
            project->addSelectedEntity(project->getSelectedSceneId(), node.id);
            project->setSelectedSceneForProperties(project->getSelectedSceneId());
            // Clear any selected instance or tile that belonged to this entity
            SceneProject* sp = project->getSelectedScene();
            if (sp && sp->sceneRender) {
                if (sp->sceneRender->getSelectedInstanceEntity() == node.id) {
                    sp->sceneRender->clearInstanceSelection();
                }
                if (sp->sceneRender->getSelectedTileEntity() == (Entity)node.id) {
                    sp->sceneRender->clearTileSelection();
                }
            }
        }else{
            project->clearAllSelections(project->getSelectedSceneId());
            selectedScenes.clear();
            selectedScenes.push_back(node.id);
            project->setSelectedSceneForProperties(node.id);
        }
    }

    if (project->hasSelectedEntities(project->getSelectedSceneId())){
        selectedScenes.clear();
    }

    // Handle double-click on child scene to select it (kept for UX consistency)
    if (node.isChildScene && nodeHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        SceneProject* childScene = project->getScene(node.childSceneId);
        if (childScene) {
            project->openScene(childScene->filepath);
        }
    }

    // Handle double-click on entity to focus camera on it
    if (!node.isScene && !node.isChildScene && nodeHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        SceneProject* sceneProject = project->getSelectedScene();
        if (sceneProject) {
            std::vector<Entity> entities = { static_cast<Entity>(node.id) };
            sceneWindow->focusOnEntities(sceneProject, entities);
        }
    }

    if (nodeRightClicked) {
        strncpy(nameBuffer, node.name.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::OpenPopup("ContextMenu");
    }

    if (ImGui::BeginPopup("ContextMenu")) {
        // Child scene context menu
        if (node.isChildScene) {
            ImGui::Text("Name:");
            ImGui::PushItemWidth(200);
            ImGui::BeginDisabled(true);
            if (ImGui::InputText("##ChangeChildSceneNameInput", nameBuffer, IM_ARRAYSIZE(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)){
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (nameBuffer[0] != '\0' && strcmp(nameBuffer, node.name.c_str()) != 0) {
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new SceneNameCmd(project, node.childSceneId, nameBuffer));
                }
            }
            ImGui::PopItemWidth();

            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH "  Remove child scene")) {
                CommandHandle::get(node.ownerSceneId)->addCommand(new RemoveChildSceneCmd(project, node.ownerSceneId, node.childSceneId));
            }
            ImGui::EndPopup();
        } else if (isChildSceneEntity) {
            // Child scene entity context menu - view only (no structural changes)
            ImGui::Text("Name: %s", node.name.c_str());
            ImGui::Text("Entity: %u", node.id);
            const SceneProject* childScene = project->getScene(node.entitySceneId);
            if (childScene) {
                ImGui::Text("Scene: %s", childScene->name.c_str());
            }
            ImGui::EndPopup();
        } else {
            // Regular entity/scene context menu
            ImGui::Text("Name:");

            ImGui::PushItemWidth(200);
            if (ImGui::InputText("##ChangeNameInput", nameBuffer, IM_ARRAYSIZE(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)){
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (nameBuffer[0] != '\0' && strcmp(nameBuffer, node.name.c_str()) != 0) {
                    if (node.isScene){
                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new SceneNameCmd(project, node.id, nameBuffer));
                    }else{
                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new EntityNameCmd(project, project->getSelectedSceneId(), node.id, nameBuffer));
                    }
                }
            }

            ImGui::Separator();
            bool entityDeleted = false;
            if (ImGui::MenuItem(ICON_FA_COPY"  Duplicate", "Ctrl+D", false, !node.isScene && !node.isLocked)){
                if (!node.isScene){
                    std::vector<Entity> entitiesToDuplicate = project->getSelectedEntities(project->getSelectedSceneId());
                    if (entitiesToDuplicate.empty() || !project->isSelectedEntity(project->getSelectedSceneId(), node.id)){
                        entitiesToDuplicate = {node.id};
                    }
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new DuplicateEntityCmd(project, project->getSelectedSceneId(), entitiesToDuplicate));
                }
            }
            if (ImGui::MenuItem(ICON_FA_TRASH"  Delete", nullptr, false, !node.isLocked)){
                if (!node.isScene){
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new DeleteEntityCmd(project, project->getSelectedSceneId(), node.id));
                    entityDeleted = true;
                }
            }
            if (!entityDeleted && !node.isScene && node.hasTransform) {
                SceneProject* selectedScene = project->getSelectedScene();
                bool allowPhysicsBody = selectedScene &&
                    (selectedScene->sceneType == SceneType::SCENE_2D || selectedScene->sceneType == SceneType::SCENE_3D);

                if (allowPhysicsBody) {
                    ComponentType bodyType = selectedScene->sceneType == SceneType::SCENE_3D ? ComponentType::Body3DComponent : ComponentType::Body2DComponent;
                    Signature signature = selectedScene->scene->getSignature(node.id);
                    bool hasBody = false;

                    if (bodyType == ComponentType::Body3DComponent) {
                        hasBody = signature.test(selectedScene->scene->getComponentId<Body3DComponent>());
                    } else {
                        hasBody = signature.test(selectedScene->scene->getComponentId<Body2DComponent>());
                    }

                    if (ImGui::MenuItem(ICON_FA_DUMBBELL "  Add Physics Body", nullptr, false, !node.isLocked && !hasBody)) {
                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new AddComponentCmd(project, project->getSelectedSceneId(), node.id, bodyType));
                    }

                    bool hasMesh = signature.test(selectedScene->scene->getComponentId<MeshComponent>());
                    bool hasInstanced = signature.test(selectedScene->scene->getComponentId<InstancedMeshComponent>());
                    if (hasMesh && ImGui::MenuItem(ICON_FA_LAYER_GROUP "  Add Instanced Mesh", nullptr, false, !node.isLocked && !hasInstanced)) {
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        multiCmd->addCommand(std::make_unique<AddComponentCmd>(project, project->getSelectedSceneId(), node.id, ComponentType::InstancedMeshComponent));
                        std::vector<InstanceData> initialInstances = { InstanceData() };
                        multiCmd->addPropertyCmd<std::vector<InstanceData>>(project, project->getSelectedSceneId(), node.id, ComponentType::InstancedMeshComponent, "instances", initialInstances);
                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(multiCmd);
                    }
                }
            }
            if (!entityDeleted && node.isParentBundle && !node.isBundleRoot){
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_LOCK_OPEN"  Remove from bundle", nullptr, false, node.isBundle)){
                    if (node.isBundle){
                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new RemoveEntityFromBundleCmd(project, project->getSelectedSceneId(), node.id, node.parent));
                    }
                }
                if (ImGui::MenuItem(ICON_FA_BOXES_STACKED"  Insert into bundle", nullptr, false, !node.isBundle)){
                    if (!node.isBundle){
                        CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new AddEntityToBundleCmd(project, project->getSelectedSceneId(), node.id, node.parent));
                    }
                }
            }
            if (!entityDeleted && node.isBundleRoot && node.hasBundleParent){
                ImGui::Separator();
                bool isNestedMember = node.isBundle && (node.bundleFilepath != node.nestedBundleFilepath);
                if (ImGui::MenuItem(ICON_FA_LOCK_OPEN"  Remove from bundle (nested)", nullptr, false, isNestedMember)){
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new RemoveEntityFromBundleCmd(project, project->getSelectedSceneId(), node.id, node.parent));
                }
                if (ImGui::MenuItem(ICON_FA_BOXES_STACKED"  Insert into bundle (nested)", nullptr, false, !isNestedMember)){
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new AddEntityToBundleCmd(project, project->getSelectedSceneId(), node.id, node.parent));
                }
            }
            if (!entityDeleted && !node.isScene && !node.isBundle && !node.isParentBundle){
                uint32_t sceneId = project->getSelectedSceneId();
                const auto& sceneBundles = project->getEntityBundles(sceneId);

                if (!sceneBundles.empty()) {
                    ImGui::Separator();
                    if (ImGui::BeginMenu(ICON_FA_BOXES_STACKED "  Insert to Bundle")) {
                        for (const auto& [path, bundlePtr] : sceneBundles) {
                            auto sceneIt = bundlePtr->instances.find(sceneId);
                            if (sceneIt == bundlePtr->instances.end() || sceneIt->second.empty()) continue;
                            std::string label = path.stem().string();
                            if (ImGui::MenuItem(label.c_str())) {
                                Entity rootEntity = sceneIt->second.front().rootEntity;
                                CommandHandle::get(sceneId)->addCommandNoMerge(new AddEntityToBundleCmd(project, sceneId, node.id, rootEntity));
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
            }
            if (!entityDeleted && (node.hasTransform || node.isScene)){
                ImGui::Separator();
                static bool createBundleChild = false;
                if (node.isBundle){
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y * 0.5f));
                    ImGui::Checkbox("Create new bundle", &createBundleChild);
                    ImGui::PopStyleVar(1);
                }
                if (ImGui::BeginMenu(ICON_FA_CIRCLE_DOT"  Create child")){
                    showNewEntityMenu(node.isScene, node.id, createBundleChild);
                }
            }

            // Scene-specific options
            if (node.isScene) {
                ImGui::Separator();
                showAddChildSceneMenu();
            }

            if (!entityDeleted && !node.isScene) {
                SceneProject* sceneProject = project->getSelectedScene();
                if (sceneProject->scene->getSignature(node.id).test(sceneProject->scene->getComponentId<CameraComponent>())) {
                    ImGui::Separator();
                    if (node.isMainCamera) {
                        if (ImGui::MenuItem(ICON_FA_EYE_SLASH"  Unset as Main Camera", nullptr, false, node.isMainCamera)) {
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(new SetMainCameraCmd(project, project->getSelectedSceneId(), NULL_ENTITY));
                        }
                    } else {
                        if (ImGui::MenuItem(ICON_FA_EYE"  Set as Main Camera", nullptr, false, !node.isMainCamera)) {
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(new SetMainCameraCmd(project, project->getSelectedSceneId(), node.id));
                        }
                    }
                }
            }

            ImGui::EndPopup();
        }
    }

    if (node.isMainCamera) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y * 0.5f);
        ImGui::SetWindowFontScale(0.7f);
        ImGui::TextUnformatted(ICON_FA_EYE);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
    }

    // Eye icon toggle for child scene inline loading
    if (node.isChildScene) {
        const SceneProject* childScene = project->getScene(node.childSceneId);
        bool isLoaded = childScene && childScene->expandedInline;

        const char* icon = isLoaded ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
        float buttonWidth = ImGui::CalcTextSize(icon).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float buttonPosX = ImGui::GetWindowContentRegionMax().x - buttonWidth;

        // Ensure it doesn't overlap the text too much if the window is very narrow
        if (buttonPosX > ImGui::GetCursorPosX() + 10.0f) {
            ImGui::SameLine(buttonPosX);
        } else {
            ImGui::SameLine();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, isLoaded ? ImVec4(0.55f, 0.80f, 0.85f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

        // Remove frame padding for this button so it doesn't stretch the tree node height
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));

        ImGui::PushID("ChildSceneEye");
        ImGui::PushID(node.childSceneId);

        if (ImGui::Button(icon)) {
            if (isLoaded) {
                project->unloadChildSceneInline(node.childSceneId);
            } else {
                project->loadChildSceneInline(node.childSceneId);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isLoaded ? "Unload child scene" : "Load child scene");
        }

        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
    }

    if (nodeOpen) {
        for (auto& child : node.children) {
            showTreeNode(child);
        }
        ImGui::TreePop();
    }

    if (node.separator){
        ImGui::Separator();
    }

    popNodeImGuiId(node);
}

void editor::Structure::pushNodeImGuiId(const TreeNode& node){
    if (node.isScene){
        ImGui::PushID("SceneNode");
        ImGui::PushID((int)node.id);
    }else if (node.isChildScene){
        ImGui::PushID("ChildSceneNode");
        ImGui::PushID((int)node.ownerSceneId);
        ImGui::PushID((int)node.childSceneId);
    }else if (node.entitySceneId != 0 && node.entitySceneId != project->getSelectedSceneId()){
        ImGui::PushID("ChildSceneEntityNode");
        ImGui::PushID((int)node.entitySceneId);
        ImGui::PushID((int)node.id);
    }else{
        ImGui::PushID("EntityNode");
        ImGui::PushID((int)project->getSelectedSceneId());
        ImGui::PushID((int)node.id);
    }
}

void editor::Structure::popNodeImGuiId(const TreeNode& node){
    if (node.isScene){
        ImGui::PopID();
        ImGui::PopID();
    }else if (node.isChildScene){
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }else if (node.entitySceneId != 0 && node.entitySceneId != project->getSelectedSceneId()){
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }else{
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }
}

void editor::Structure::show(){
    if (!windowOpen) {
        return;
    }

    SceneProject* sceneProject = project->getSelectedScene();
    if (!sceneProject || !sceneProject->scene) {
        return;
    }

    if (focusRequested) {
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }

    bool wasOpen = windowOpen;

    if (!ImGui::Begin(Structure::WINDOW_NAME, &windowOpen)) {
        ImGui::End();
        if (wasOpen && !windowOpen) {
            setOpen(false);
        }
        return;
    }

    Entity mainCamera = sceneProject->mainCamera;
    size_t order = 0;
    std::unordered_set<Entity> sceneEntitiesSet(sceneProject->entities.begin(), sceneProject->entities.end());
    std::unordered_map<Entity, TreeNode*> entityNodeMap;
    std::unordered_map<Entity, std::filesystem::path> bundleEntityPaths;
    std::unordered_map<Entity, std::list<TreeNode>> virtualBundleChildren;
    std::unordered_map<Entity, std::list<TreeNode>> virtualActionChildren;

    for (Entity entity : sceneProject->entities) {
        std::filesystem::path bundlePath = project->findEntityBundlePathFor(sceneProject->id, entity);
        if (!bundlePath.empty()) {
            bundleEntityPaths.emplace(entity, std::move(bundlePath));
        }
    }

    TreeNode root;

    root.icon = ICON_FA_TV;
    root.id = sceneProject->id;
    root.isScene = true;
    root.order = order++;
    root.name = sceneProject->name;

    // child scenes (shown before entities)
    bool hasChildScenes = !sceneProject->childScenes.empty();
    for (const auto& childSceneId : sceneProject->childScenes) {
        const SceneProject* childScene = project->getScene(childSceneId);
        if (!childScene) {
            continue; // Skip invalid child scene references
        }

        TreeNode childSceneNode;
        switch (childScene->sceneType) {
            case SceneType::SCENE_3D:
                childSceneNode.icon = ICON_FA_CUBES;
                break;
            case SceneType::SCENE_2D:
                childSceneNode.icon = ICON_FA_CUBES_STACKED;
                break;
            case SceneType::SCENE_UI:
                childSceneNode.icon = ICON_FA_WINDOW_RESTORE;
                break;
        }
        childSceneNode.name = childScene->name;
        childSceneNode.isChildScene = true;
        childSceneNode.id = childSceneId;
        childSceneNode.childSceneId = childSceneId;
        childSceneNode.ownerSceneId = sceneProject->id;
        childSceneNode.order = order++;

        // If child scene is expanded inline, build entity tree nodes for it
        if (childScene->expandedInline && childScene->scene) {
            std::unordered_set<Entity> childEntitiesSet(childScene->entities.begin(), childScene->entities.end());
            std::unordered_map<Entity, TreeNode*> childEntityNodeMap;

            // non-hierarchical entities
            for (auto& entity : childScene->entities) {
                Signature signature = childScene->scene->getSignature(entity);
                if (!signature.test(childScene->scene->getComponentId<Transform>())) {
                    TreeNode enode;
                    enode.icon = getObjectIcon(signature, childScene->scene);
                    enode.id = entity;
                    enode.entitySceneId = childSceneId;
                    enode.isMainCamera = (entity == childScene->mainCamera);
                    enode.isBone = signature.test(childScene->scene->getComponentId<BoneComponent>());
                    enode.order = order++;
                    enode.name = childScene->scene->getEntityName(entity);
                    childSceneNode.children.push_back(enode);
                    childEntityNodeMap[entity] = &childSceneNode.children.back();
                }
            }

            // hierarchical entities
            auto childTransforms = childScene->scene->getComponentArray<Transform>();
            for (int i = 0; i < childTransforms->size(); i++) {
                Transform& transform = childTransforms->getComponentFromIndex(i);
                Entity entity = childTransforms->getEntity(i);
                Signature signature = childScene->scene->getSignature(entity);

                if (childEntitiesSet.find(entity) != childEntitiesSet.end()) {
                    TreeNode enode;
                    enode.icon = getObjectIcon(signature, childScene->scene);
                    enode.id = entity;
                    enode.entitySceneId = childSceneId;
                    enode.isMainCamera = (entity == childScene->mainCamera);
                    enode.isBone = signature.test(childScene->scene->getComponentId<BoneComponent>());
                    enode.hasTransform = true;
                    enode.isLocked = ProjectUtils::isEntityLocked(childScene->scene, entity);
                    enode.order = order++;
                    enode.name = childScene->scene->getEntityName(entity);

                    if (transform.parent == NULL_ENTITY) {
                        childSceneNode.children.push_back(enode);
                        childEntityNodeMap[entity] = &childSceneNode.children.back();
                    } else {
                        auto parentIt = childEntityNodeMap.find(transform.parent);
                        TreeNode* parentNode = parentIt != childEntityNodeMap.end() ? parentIt->second : nullptr;
                        if (parentNode) {
                            enode.parent = parentNode->id;
                            parentNode->children.push_back(enode);
                            childEntityNodeMap[entity] = &parentNode->children.back();
                        }
                    }
                }
            }
        }

        root.children.push_back(childSceneNode);
    }

    bool separatorAfterChildScenes = hasChildScenes;

    // non-hierarchical entities
    for (auto& entity : sceneProject->entities) {
        Signature signature = sceneProject->scene->getSignature(entity);

        if (!signature.test(sceneProject->scene->getComponentId<Transform>())){
            if (separatorAfterChildScenes && !root.children.empty()) {
                root.children.back().separator = true;
                separatorAfterChildScenes = false;
            }

            TreeNode child;
            child.icon = getObjectIcon(signature, sceneProject->scene);
            child.id = entity;
            child.isMainCamera = (entity == mainCamera);
            child.isBone = signature.test(sceneProject->scene->getComponentId<BoneComponent>());
            child.isLocked = ProjectUtils::isEntityLocked(sceneProject->scene, entity);
            child.order = order++;
            child.name = sceneProject->scene->getEntityName(entity);
            auto bundleIt = bundleEntityPaths.find(entity);
            if (bundleIt != bundleEntityPaths.end()) {
                child.isBundle = true;
                child.bundleFilepath = bundleIt->second;
                if (signature.test(sceneProject->scene->getComponentId<BundleComponent>())) {
                    child.isBundleRoot = true;
                    child.nestedBundleFilepath = sceneProject->scene->getComponent<BundleComponent>(entity).path;
                }
            }

            bool addedAsVirtualChild = false;
            Entity actionTarget = ProjectUtils::getVirtualParent(sceneProject->scene, entity);
            if (actionTarget != NULL_ENTITY && sceneEntitiesSet.find(actionTarget) != sceneEntitiesSet.end()) {
                // Check if they are in the same bundle
                bool sameBundle = false;
                auto targetBundleIt = bundleEntityPaths.find(actionTarget);
                if (bundleIt != bundleEntityPaths.end() && targetBundleIt != bundleEntityPaths.end() && bundleIt->second == targetBundleIt->second) {
                    sameBundle = true;
                }

                // Priority is Action target, but if both are bundles, ensure it prefers organizing by the actual local parent element correctly.
                if (sameBundle || bundleIt == bundleEntityPaths.end()) {
                    child.parent = actionTarget;
                    if (sameBundle) {
                        child.isParentBundle = true;
                        child.hasBundleParent = true;
                    }
                    virtualActionChildren[actionTarget].push_back(std::move(child));
                    addedAsVirtualChild = true;
                }
            }
            
            if (!addedAsVirtualChild && bundleIt != bundleEntityPaths.end()) {
                EntityBundle* bundle = project->getEntityBundle(bundleIt->second);
                Entity bundleRoot = bundle ? bundle->getRootEntity(sceneProject->id, entity) : NULL_ENTITY;
                if (bundleRoot != NULL_ENTITY && bundleRoot != entity) {
                    child.parent = bundleRoot;
                    child.isParentBundle = true;
                    virtualBundleChildren[bundleRoot].push_back(std::move(child));
                    addedAsVirtualChild = true;
                }
            }
            
            if (!addedAsVirtualChild) {
                root.children.push_back(child);
                entityNodeMap[entity] = &root.children.back();
            }
        }
    }

    bool applySeparator = false;
    if (root.children.size() > 0){
        applySeparator = true;
    }

    // hierarchical entities
    auto transforms = sceneProject->scene->getComponentArray<Transform>();
    for (int i = 0; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);
        Entity entity = transforms->getEntity(i);
        Signature signature = sceneProject->scene->getSignature(entity);

        if (sceneEntitiesSet.find(entity) != sceneEntitiesSet.end()){
            if (applySeparator){
                root.children.back().separator = true;
                applySeparator = false;
            }

            if (separatorAfterChildScenes && !root.children.empty()) {
                root.children.back().separator = true;
                separatorAfterChildScenes = false;
            }

            TreeNode child;
            child.icon = getObjectIcon(signature, sceneProject->scene);
            child.id = entity;
            child.isMainCamera = (entity == mainCamera);
            child.isBone = signature.test(sceneProject->scene->getComponentId<BoneComponent>());
            child.hasTransform = true;
            child.isLocked = ProjectUtils::isEntityLocked(sceneProject->scene, entity);
            child.order = order++;
            child.name = sceneProject->scene->getEntityName(entity);
            auto bundleIt = bundleEntityPaths.find(entity);
            if (bundleIt != bundleEntityPaths.end()) {
                child.isBundle = true;
                child.bundleFilepath = bundleIt->second;
            }
            if (signature.test(sceneProject->scene->getComponentId<BundleComponent>())) {
                child.isBundleRoot = true;
                child.nestedBundleFilepath = sceneProject->scene->getComponent<BundleComponent>(entity).path;
            }
            if (transform.parent == NULL_ENTITY){
                root.children.push_back(child);
                entityNodeMap[entity] = &root.children.back();
            }else{
                auto parentIt = entityNodeMap.find(transform.parent);
                TreeNode* parent = parentIt != entityNodeMap.end() ? parentIt->second : nullptr;
                if (parent){
                    child.parent = parent->id;
                    auto parentBundleIt = bundleEntityPaths.find(transform.parent);
                    if (parentBundleIt != bundleEntityPaths.end()) {
                        child.isParentBundle = true;
                        child.hasBundleParent = true;
                        // Locally added bundle roots treated as top-level visually, not nested
                        if (child.isBundleRoot && child.bundleFilepath != parentBundleIt->second) {
                            child.isParentBundle = false;
                        }
                    }
                    parent->children.push_back(child);
                    entityNodeMap[entity] = &parent->children.back();
                }else{
                    printf("ERROR: Could not find parent of entity %u\n", entity);
                }
            }
        }
    }

    for (auto& [rootEntity, children] : virtualBundleChildren) {
        auto parentIt = entityNodeMap.find(rootEntity);
        if (parentIt == entityNodeMap.end() || children.empty()) {
            continue;
        }

        TreeNode* parent = parentIt->second;
        if (!parent->children.empty()) {
            children.back().separator = true;
        }

        parent->children.splice(parent->children.begin(), children);
    }

    // Splice action children, resolving targets that may themselves be action children
    bool splicedNew;
    do {
        splicedNew = false;
        for (auto it = virtualActionChildren.begin(); it != virtualActionChildren.end(); ) {
            auto parentIt = entityNodeMap.find(it->first);
            if (parentIt == entityNodeMap.end() || it->second.empty()) {
                ++it;
                continue;
            }

            TreeNode* parent = parentIt->second;
            if (!parent->children.empty()) {
                it->second.back().separator = true;
            }

            // Splice and register in entityNodeMap (std::list nodes are stable)
            parent->children.splice(parent->children.begin(), it->second);
            for (auto& child : parent->children) {
                if (entityNodeMap.find(child.id) == entityNodeMap.end()) {
                    entityNodeMap[child.id] = &child;
                }
            }

            it = virtualActionChildren.erase(it);
            splicedNew = true;
        }
    } while (splicedNew);
    showIconMenu();
    ImGui::BeginChild("StructureScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    // Apply search filtering if there's a search term
    if (strlen(searchBuffer) > 0) {
        std::string searchStr = searchBuffer;
        if (!matchCase) {
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
        }
        markMatchingNodes(root, searchStr);
    }

    showTreeNode(root);
    float treeLastCursorY = ImGui::GetCursorScreenPos().y;

    // Handle right-click in empty space
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        ImGui::OpenPopup("EmptySpaceContextMenu");
    }

    // Empty space context menu
    if (ImGui::BeginPopup("EmptySpaceContextMenu")) {
        if (ImGui::BeginMenu(ICON_FA_CIRCLE_DOT"  Create entity")) {
            showNewEntityMenu(true, NULL_ENTITY, false);
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    // Handle drag and drop for entity files and entities to empty space
    if (ImGui::GetMousePos().y > treeLastCursorY && ImGui::BeginDragDropTarget()) {

        // Handle entity drag-drop to move to root level (last position)
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload()) {
            if (payload->IsDataType("entity")) {
                if (payload->DataSize >= sizeof(EntityPayload)) {
                    const EntityPayload* p = reinterpret_cast<const EntityPayload*>(payload->Data);
                    Entity sourceEntity = p->entity;
                    bool sourceHasTransform = p->hasTransform;
                    Entity sourceParent = p->parent;

                    // Only allow moving if source has transform and is a child (has parent)
                    if (sourceHasTransform && sourceParent != NULL_ENTITY) {
                        if (ImGui::AcceptDragDropPayload("entity")) {
                            if (payload->IsDelivery()) {
                                moveEntityToRootLevel(sourceEntity, sceneEntitiesSet);
                            }
                        }
                    }
                }
            }
        }

        bool allowResourceDragDrop = false;
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload()) {
            if (payload->IsDataType("resource_files")) {
                const char* data = (const char*)payload->Data;
                size_t dataSize = payload->DataSize;

                size_t offset = 0;
                while (offset < dataSize) {
                    std::string path(data + offset);
                    if (!path.empty()) {
                        if (Util::isSceneFile(path) || Util::isBundleFile(path)) {
                            allowResourceDragDrop = true;
                            break;
                        }
                    }
                    offset += path.size() + 1;
                }
            }
        }

        if(allowResourceDragDrop) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
                // Parse the dropped file paths
                std::vector<std::string> droppedPaths;
                const char* data = (const char*)payload->Data;
                size_t dataSize = payload->DataSize;

                // Parse null-terminated strings from the payload
                size_t offset = 0;
                while (offset < dataSize) {
                    std::string path(data + offset);
                    if (!path.empty()) {
                        droppedPaths.push_back(path);
                    }
                    offset += path.size() + 1; // +1 for null terminator
                }

                handleSceneFilesDropAsChildScenes(droppedPaths, project->getSelectedSceneId());
                handleEntityFilesDrop(droppedPaths);

                if (payload->IsDelivery()) {
                    ImGui::SetWindowFocus(Structure::WINDOW_NAME);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();

    if (wasOpen && !windowOpen) {
        setOpen(false);
    }
}