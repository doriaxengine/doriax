#include "Properties.h"

#include "imgui_internal.h"

#include "util/Util.h"
#include "util/FileDialogs.h"
#include "util/EntityPayload.h"
#include "external/IconsFontAwesome6.h"
#include "command/CommandHandle.h"
#include "command/type/PropertyCmd.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/UnlinkMaterialCmd.h"
#include "command/type/LinkMaterialCmd.h"
#include "command/type/EntityNameCmd.h"
#include "command/type/SceneNameCmd.h"
#include "command/type/MeshChangeCmd.h"
#include "command/type/ModelLoadCmd.h"
#include "command/type/AddComponentCmd.h"
#include "command/type/RemoveComponentCmd.h"
#include "command/type/ComponentToBundleSharedCmd.h"
#include "command/type/ComponentToBundleLocalCmd.h"
#include "command/type/ScenePropertyCmd.h"
#include "render/SceneRender2D.h"
#include "window/Structure.h"
#include "App.h"
#include "Backend.h"
#include "component/ActionComponent.h"
#include "component/AlphaActionComponent.h"
#include "component/AnimationComponent.h"
#include "component/BoneComponent.h"
#include "component/ColorActionComponent.h"
#include "component/KeyframeTracksComponent.h"
#include "component/PositionActionComponent.h"
#include "component/RotateTracksComponent.h"
#include "component/RotationActionComponent.h"
#include "component/ScaleActionComponent.h"
#include "component/ScaleTracksComponent.h"
#include "component/TimedActionComponent.h"
#include "component/TranslateTracksComponent.h"
#include "component/MorphTracksComponent.h"
#include "component/ParticlesComponent.h"
#include "component/PointsComponent.h"
#include "util/SHA1.h"
#include "util/ProjectUtils.h"
#include "Stream.h"
#include "Out.h"
#include "subsystem/ActionSystem.h"
#include "subsystem/PhysicsSystem.h"
#include "pool/TexturePool.h"
#include "yaml-cpp/yaml.h"

#include <map>
#include <type_traits>
#include <cstring>
#include <fstream>
#include <cctype>
#include <unordered_set>

using namespace doriax;

static std::vector<editor::EnumEntry> entriesPrimitiveType = {
    { (int)PrimitiveType::TRIANGLES, "Triangles" },
    { (int)PrimitiveType::TRIANGLE_STRIP, "Triangle Strip" },
    { (int)PrimitiveType::POINTS, "Points" },
    { (int)PrimitiveType::LINES, "Lines" }
};

static std::vector<editor::EnumEntry> entriesPivotPreset = {
    { (int)PivotPreset::CENTER, "Center" },
    { (int)PivotPreset::TOP_CENTER, "Top Center" },
    { (int)PivotPreset::BOTTOM_CENTER, "Bottom Center" },
    { (int)PivotPreset::LEFT_CENTER, "Left Center" },
    { (int)PivotPreset::RIGHT_CENTER, "Right Center" },
    { (int)PivotPreset::TOP_LEFT, "Top Left" },
    { (int)PivotPreset::BOTTOM_LEFT, "Bottom Left" },
    { (int)PivotPreset::TOP_RIGHT, "Top Right" },
    { (int)PivotPreset::BOTTOM_RIGHT, "Bottom Right" }
};

static std::vector<editor::EnumEntry> entriesAnchorPreset = {
    { (int)AnchorPreset::NONE, "None" },
    { (int)AnchorPreset::TOP_LEFT, "Top Left" },
    { (int)AnchorPreset::TOP_RIGHT, "Top Right" },
    { (int)AnchorPreset::BOTTOM_LEFT, "Bottom Left" },
    { (int)AnchorPreset::BOTTOM_RIGHT, "Bottom Right" },
    { (int)AnchorPreset::CENTER_LEFT, "Center Left" },
    { (int)AnchorPreset::CENTER_TOP, "Center Top" },
    { (int)AnchorPreset::CENTER_RIGHT, "Center Right" },
    { (int)AnchorPreset::CENTER_BOTTOM, "Center Bottom" },
    { (int)AnchorPreset::CENTER, "Center" },
    { (int)AnchorPreset::LEFT_WIDE, "Left Wide" },
    { (int)AnchorPreset::TOP_WIDE, "Top Wide" },
    { (int)AnchorPreset::RIGHT_WIDE, "Right Wide" },
    { (int)AnchorPreset::BOTTOM_WIDE, "Bottom Wide" },
    { (int)AnchorPreset::VERTICAL_CENTER_WIDE, "Vertical Center Wide" },
    { (int)AnchorPreset::HORIZONTAL_CENTER_WIDE, "Horizontal Center Wide" },
    { (int)AnchorPreset::FULL_LAYOUT, "Full Layout" }
};

static std::vector<editor::EnumEntry> entriesLightType = {
    { (int)LightType::DIRECTIONAL, "Directional" },
    { (int)LightType::POINT, "Point" },
    { (int)LightType::SPOT, "Spot" }
};

static std::vector<editor::EnumEntry> entriesCameraType = {
    { (int)CameraType::CAMERA_ORTHO, "Orthographic" },
    { (int)CameraType::CAMERA_PERSPECTIVE, "Perspective" }
};

static std::vector<editor::EnumEntry> entriesBodyType = {
    { (int)BodyType::STATIC, "Static" },
    { (int)BodyType::KINEMATIC, "Kinematic" },
    { (int)BodyType::DYNAMIC, "Dynamic" }
};

static std::vector<editor::EnumEntry> entriesShape2DType = {
    { (int)Shape2DType::POLYGON, "Polygon" },
    { (int)Shape2DType::CIRCLE, "Circle" },
    { (int)Shape2DType::CAPSULE, "Capsule" },
    { (int)Shape2DType::SEGMENT, "Segment" },
    { (int)Shape2DType::CHAIN, "Chain" }
};

static std::vector<editor::EnumEntry> entriesShape3DType = {
    { (int)Shape3DType::BOX, "Box" },
    { (int)Shape3DType::SPHERE, "Sphere" },
    { (int)Shape3DType::CAPSULE, "Capsule" },
    { (int)Shape3DType::TAPERED_CAPSULE, "Tapered Capsule" },
    { (int)Shape3DType::CYLINDER, "Cylinder" },
    { (int)Shape3DType::CONVEX_HULL, "Convex Hull" },
    { (int)Shape3DType::MESH, "Mesh" },
    { (int)Shape3DType::HEIGHTFIELD, "Heightfield" }
};

static std::vector<editor::EnumEntry> entriesShape3DSource = {
    { (int)Shape3DSource::NONE, "None" },
    { (int)Shape3DSource::RAW_VERTICES, "Raw Vertices" },
    { (int)Shape3DSource::RAW_MESH, "Raw Mesh" },
    { (int)Shape3DSource::ENTITY_MESH, "Entity Mesh" },
    { (int)Shape3DSource::ENTITY_HEIGHTFIELD, "Entity Heightfield" }
};

static std::vector<editor::EnumEntry> entriesShape3DSourceConvexHull = {
    { (int)Shape3DSource::RAW_VERTICES, "Raw Vertices" },
    { (int)Shape3DSource::ENTITY_MESH, "Entity Mesh" }
};

static std::vector<editor::EnumEntry> entriesShape3DSourceMesh = {
    { (int)Shape3DSource::RAW_MESH, "Raw Mesh" },
    { (int)Shape3DSource::ENTITY_MESH, "Entity Mesh" }
};

static std::vector<editor::EnumEntry> entriesShape3DSourceHeightfield = {
    { (int)Shape3DSource::ENTITY_HEIGHTFIELD, "Entity Heightfield" }
};

static std::vector<editor::EnumEntry> entriesJoint2DType = {
    { (int)Joint2DType::DISTANCE, "Distance" },
    { (int)Joint2DType::REVOLUTE, "Revolute" },
    { (int)Joint2DType::PRISMATIC, "Prismatic" },
    { (int)Joint2DType::MOUSE, "Mouse" },
    { (int)Joint2DType::WHEEL, "Wheel" },
    { (int)Joint2DType::WELD, "Weld" },
    { (int)Joint2DType::MOTOR, "Motor" }
};

static std::vector<editor::EnumEntry> entriesJoint3DType = {
    { (int)Joint3DType::FIXED, "Fixed" },
    { (int)Joint3DType::DISTANCE, "Distance" },
    { (int)Joint3DType::POINT, "Point" },
    { (int)Joint3DType::HINGE, "Hinge" },
    { (int)Joint3DType::CONE, "Cone" },
    { (int)Joint3DType::PRISMATIC, "Prismatic" },
    { (int)Joint3DType::SWINGTWIST, "Swing Twist" },
    { (int)Joint3DType::SIXDOF, "Six DOF" },
    { (int)Joint3DType::PATH, "Path" },
    { (int)Joint3DType::GEAR, "Gear" },
    { (int)Joint3DType::RACKANDPINON, "Rack and Pinion" },
    { (int)Joint3DType::PULLEY, "Pulley" }
};

static std::vector<int> cascadeValues = { 1, 2, 3, 4, 5, 6 };
static std::vector<int> po2Values = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384 };

static std::vector<editor::EnumEntry> entriesActionState = {
    { (int)ActionState::Running, "Running" },
    { (int)ActionState::Paused, "Paused" },
    { (int)ActionState::Stopped, "Stopped" }
};

namespace {
    enum class ScenePropertyInputType {
        Checkbox,
        DragFloat,
        SliderFloat,
        ColorRGB,
        ColorRGBA,
        Combo
    };

    struct DirtyMaterialEntry {
        unsigned int sceneId;
        Entity entity;
        int submeshIndex;
        std::string relativePath;
        float timer;
    };

    static std::vector<DirtyMaterialEntry> dirtyMaterials;
    static constexpr float materialWriteDelaySec = 0.3f;

    void markMaterialDirty(unsigned int sceneId, Entity entity, int submeshIndex, const std::string& relativePath) {
        for (auto& entry : dirtyMaterials) {
            if (entry.sceneId == sceneId && entry.entity == entity && entry.submeshIndex == submeshIndex) {
                entry.timer = 0.0f;
                entry.relativePath = relativePath;
                return;
            }
        }

        dirtyMaterials.push_back({sceneId, entity, submeshIndex, relativePath, 0.0f});
    }

    void flushDirtyMaterials(editor::Project* project, float deltaTime) {
        if (dirtyMaterials.empty()) {
            return;
        }

        auto it = dirtyMaterials.begin();
        while (it != dirtyMaterials.end()) {
            it->timer += deltaTime;
            if (it->timer < materialWriteDelaySec) {
                ++it;
                continue;
            }

            editor::SceneProject* sp = project->getScene(it->sceneId);
            if (sp) {
                MeshComponent* mesh = sp->scene->findComponent<MeshComponent>(it->entity);
                if (mesh && it->submeshIndex < mesh->numSubmeshes) {
                    Material& material = mesh->submeshes[it->submeshIndex].material;
                    std::filesystem::path absolutePath = project->getProjectPath() / it->relativePath;

                    try {
                        std::ofstream out(absolutePath, std::ios::binary | std::ios::trunc);
                        if (out.is_open()) {
                            std::string payload = YAML::Dump(editor::Stream::encodeMaterial(material));
                            out.write(payload.c_str(), payload.size());
                            out.close();

                            project->linkMaterialFile(it->sceneId, it->entity, it->submeshIndex, it->relativePath);
                            project->refreshLinkedMaterials(true);

                            if (editor::ResourcesWindow* resourcesWindow = editor::Backend::getApp().getResourcesWindow()) {
                                resourcesWindow->notifyResourceFileChanged(absolutePath);
                            }
                        }
                    } catch (const std::exception& e) {
                        editor::Out::error("Error saving linked material file '%s': %s", absolutePath.string().c_str(), e.what());
                    }
                }
            }

            it = dirtyMaterials.erase(it);
        }
    }

    template<typename T>
    void drawScenePropertyRow(editor::SceneProject* sceneProject, const std::string& propertyName, const char* label, ScenePropertyInputType inputType, float minValue = 0.0f, float maxValue = 1.0f) {
        T value = doriax::editor::Catalog::getSceneProperty<T>(sceneProject->scene, propertyName);
        bool changed = false;

        editor::Command* cmd = nullptr;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", label);
        ImGui::TableSetColumnIndex(1);

        switch (inputType) {
            case ScenePropertyInputType::Checkbox:
                if constexpr (std::is_same_v<T, bool>) {
                    changed = ImGui::Checkbox(("##" + propertyName).c_str(), &value);
                }
                break;
            case ScenePropertyInputType::DragFloat:
                if constexpr (std::is_same_v<T, float>) {
                    changed = ImGui::DragFloat(("##" + propertyName).c_str(), &value, 0.01f);
                } else if constexpr (std::is_same_v<T, Vector3>) {
                    changed = ImGui::DragFloat3(("##" + propertyName).c_str(), (float*)&value.x);
                } else if constexpr (std::is_same_v<T, Vector4>) {
                    changed = ImGui::DragFloat4(("##" + propertyName).c_str(), (float*)&value.x);
                }
                break;
            case ScenePropertyInputType::SliderFloat:
                if constexpr (std::is_same_v<T, float>) {
                    ImGui::SetNextItemWidth(-1);
                    changed = ImGui::SliderFloat(("##" + propertyName).c_str(), &value, minValue, maxValue);
                }
                break;
            case ScenePropertyInputType::ColorRGB:
                if constexpr (std::is_same_v<T, Vector3>) {
                    changed = ImGui::ColorEdit3(("##" + propertyName).c_str(), (float*)&value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                }
                break;
            case ScenePropertyInputType::ColorRGBA:
                if constexpr (std::is_same_v<T, Vector4>) {
                    changed = ImGui::ColorEdit4(("##" + propertyName).c_str(), (float*)&value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                }
                break;
            case ScenePropertyInputType::Combo:
                if constexpr (std::is_same_v<T, LightState>) {
                    const char* lightStateNames[] = { "Off", "On", "Auto" };
                    int currentItem = static_cast<int>(value);
                    changed = ImGui::Combo(("##" + propertyName).c_str(), &currentItem, lightStateNames, IM_ARRAYSIZE(lightStateNames));
                    if (changed) {
                        value = static_cast<LightState>(currentItem);
                    }
                }
                break;
        }

        if (changed) {
            cmd = new editor::ScenePropertyCmd<T>(sceneProject, propertyName, value);
            editor::CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (cmd) {
                cmd->setNoMerge();
                cmd = nullptr;
            }
        }
    }

    std::string formatPropertyLabelValue(const editor::PropertyData& prop) {
        if (!prop.ref) {
            return "-";
        }

        char buffer[64];
        switch (prop.type) {
            case editor::PropertyType::Bool:
                return (*static_cast<bool*>(prop.ref)) ? "true" : "false";
            case editor::PropertyType::Float:
                snprintf(buffer, sizeof(buffer), "%.3f", *static_cast<float*>(prop.ref));
                return buffer;
            case editor::PropertyType::Int:
                return std::to_string(*static_cast<int*>(prop.ref));
            case editor::PropertyType::UInt:
                return std::to_string(*static_cast<unsigned int*>(prop.ref));
            case editor::PropertyType::Entity:
            case editor::PropertyType::EntityReference:
                return std::to_string(*static_cast<Entity*>(prop.ref));
            case editor::PropertyType::String:
                return *static_cast<std::string*>(prop.ref);
            default:
                return "-";
        }
    }
}

editor::Properties::Properties(Project* project){
    this->project = project;
    this->cmd = nullptr;

    this->finishProperty = false;
}

editor::RowPropertyType editor::Properties::scriptPropertyTypeToRowPropertyType(ScriptPropertyType scriptType){
    switch (scriptType) {
        case doriax::ScriptPropertyType::Bool: return RowPropertyType::Bool;
        case doriax::ScriptPropertyType::Int: return RowPropertyType::Int;
        case doriax::ScriptPropertyType::Float: return RowPropertyType::Float;
        case doriax::ScriptPropertyType::String: return RowPropertyType::String;
        case doriax::ScriptPropertyType::Vector2: return RowPropertyType::Vector2;
        case doriax::ScriptPropertyType::Vector3: return RowPropertyType::Vector3;
        case doriax::ScriptPropertyType::Vector4: return RowPropertyType::Vector4;
        case doriax::ScriptPropertyType::Color3: return RowPropertyType::Color3L;
        case doriax::ScriptPropertyType::Color4: return RowPropertyType::Color4L;
        case doriax::ScriptPropertyType::EntityReference: return RowPropertyType::ExternalEntity;
        default: return RowPropertyType::Custom;
    }
}

std::string editor::Properties::replaceNumberedBrackets(const std::string& input) {
    std::string result = input;
    size_t pos = 0;

    while ((pos = result.find('[', pos)) != std::string::npos) {
        size_t end_pos = result.find(']', pos);
        if (end_pos != std::string::npos && std::isdigit(result[pos + 1])) {
            result.replace(pos, end_pos - pos + 1, "[]");
            pos += 2;
        } else {
            ++pos;
        }
    }

    return result;
}

Vector3 editor::Properties::roundZero(const Vector3& val, const float threshold){
    return Vector3(
        (fabs(val.x) < threshold) ? 0.0f : val.x,
        (fabs(val.y) < threshold) ? 0.0f : val.y,
        (fabs(val.z) < threshold) ? 0.0f : val.z
    );
}

bool editor::Properties::compareVectorFloat(const float* a, const float* b, size_t elements, const float threshold){
    for (size_t i = 0; i < elements; ++i) {
        if (fabs(a[i] - b[i]) > threshold) {
            return true;
        }
    }
    return false;
}

float editor::Properties::getLabelSize(std::string label, bool addRotateIconSpace){
    float iconSize = ImGui::CalcTextSize(ICON_FA_ROTATE_LEFT).x;
    float labelSize = ImGui::CalcTextSize(label.c_str()).x;
    return labelSize + (addRotateIconSpace ? iconSize : iconSize / 2.0f);
}

void editor::Properties::helpMarker(std::string desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

Texture* editor::Properties::findThumbnail(const std::string& path) {
    if (path.empty()) return nullptr;

    // Compute the thumbnail path
    std::filesystem::path texPath = path;
    std::filesystem::path projectPath = project->getProjectPath();
    std::filesystem::path thumbnailPath;

    if (!texPath.empty() && texPath.is_relative() && !projectPath.empty()) {
        texPath = projectPath / texPath;
    }

    texPath = texPath.lexically_normal();
    projectPath = projectPath.lexically_normal();

    if (!texPath.empty() && texPath.is_absolute()) {
        thumbnailPath = project->getThumbnailPath(texPath);

        // If the thumbnail exists, load and use it
        if (std::filesystem::exists(thumbnailPath)) {
            // Check if we already have this thumbnail loaded in cache
            std::string thumbPathStr = thumbnailPath.string();
            auto thumbIt = thumbnailTextures.find(thumbPathStr);

            if (thumbIt == thumbnailTextures.end()) {
                // Load the thumbnail texture if not in cache
                Texture thumbTexture;
                thumbTexture.setPath(thumbPathStr);
                if (thumbTexture.load()) {
                    thumbnailTextures[thumbPathStr] = thumbTexture;
                    return &thumbnailTextures[thumbPathStr];
                }
            } else if (!thumbIt->second.empty()) {
                // Return cached texture
                return &thumbIt->second;
            }
        }
    }

    return nullptr;
}

void editor::Properties::drawImageWithBorderAndRounding(Texture* texture, const ImVec2& size, float rounding, ImU32 border_col, float border_thickness, bool flipY) {
    if (!texture) return;

    ImTextureID tex_id = (ImTextureID)(intptr_t)texture->getRender()->getGLHandler();
    int texWidth = texture->getWidth();
    int texHeight = texture->getHeight();

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Calculate source aspect and target aspect
    float srcAspect = static_cast<float>(texWidth) / texHeight;
    float dstAspect = size.x / size.y;

    // Default UVs (full image)
    ImVec2 uv0(0, 0);
    ImVec2 uv1(1, 1);

    // If aspect ratios differ, calculate the crop
    if (fabs(srcAspect - dstAspect) > 1e-3f) {
        if (srcAspect > dstAspect) {
            // Source is wider; crop left and right
            float newWidth = texHeight * dstAspect;
            float x0 = (texWidth - newWidth) / 2.0f;
            uv0.x = x0 / texWidth;
            uv1.x = (x0 + newWidth) / texWidth;
        } else {
            // Source is taller; crop top and bottom
            float newHeight = texWidth / dstAspect;
            float y0 = (texHeight - newHeight) / 2.0f;
            uv0.y = y0 / texHeight;
            uv1.y = (y0 + newHeight) / texHeight;
        }
    }

    if (flipY) {
        float temp = uv0.y;
        uv0.y = uv1.y;
        uv1.y = temp;
    }

    ImVec2 p_min = cursor;
    ImVec2 p_max = ImVec2(cursor.x + size.x, cursor.y + size.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Draw the cropped image with rounding
    draw_list->AddImageRounded(tex_id, p_min, p_max, uv0, uv1, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersAll);

    // Draw the border
    draw_list->AddRect(p_min, p_max, border_col, rounding, ImDrawFlags_RoundCornersAll, border_thickness);

    // Reserve space for interaction
    ImGui::InvisibleButton("##image", size);
}


void editor::Properties::dragDropResourcesFont(ComponentType cpType, std::string id, SceneProject* sceneProject, std::vector<Entity> entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    if (ImGui::BeginDragDropTarget()){

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (receivedStrings.size() > 0){
                const std::string droppedRelativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();

                bool isFont = Util::isFontFile(droppedRelativePath);

                if (isFont) {
                    if (!hasFontDrag.count(id)){
                        hasFontDrag[id] = true;
                        for (Entity& entity : entities){
                            std::string* valueRef = Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, cpType, id);
                            originalFont[id][entity] = *valueRef;
                            if (*valueRef != droppedRelativePath){
                                *valueRef = droppedRelativePath;
                                if (componentType == ComponentType::TextComponent){
                                    sceneProject->scene->getComponent<TextComponent>(entity).needReloadAtlas = true;
                                    sceneProject->scene->getComponent<TextComponent>(entity).needUpdateText = true;
                                }
                            }
                        }
                    }
                    if (payload->IsDelivery()){
                        for (Entity& entity : entities){
                            std::string* valueRef = Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, cpType, id);
                            *valueRef = originalFont[id][entity];
                            cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, droppedRelativePath);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasFontDrag.erase(id);
                        originalFont.erase(id);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasFontDrag.count(id) && hasFontDrag[id]){
            for (Entity& entity : entities){
                std::string* valueRef = Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, cpType, id);
                if (*valueRef != originalFont[id][entity]){
                    *valueRef = originalFont[id][entity];
                    if (componentType == ComponentType::TextComponent){
                        sceneProject->scene->getComponent<TextComponent>(entity).needReloadAtlas = true;
                        sceneProject->scene->getComponent<TextComponent>(entity).needUpdateText = true;
                    }
                }
            }

            hasFontDrag.erase(id);
            originalFont.erase(id);
        }
    }
}


void editor::Properties::dragDropResourcesTexture(ComponentType cpType, std::string id, SceneProject* sceneProject, std::vector<Entity> entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    if (ImGui::BeginDragDropTarget()){

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (receivedStrings.size() > 0){
                const std::string droppedRelativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();

                if (Util::isImageFile(droppedRelativePath)) {
                    if (!hasTextureDrag.count(id)){
                        hasTextureDrag[id] = true;
                        for (Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            originalTex[id][entity] = Texture(*valueRef);
                            if (*valueRef != Texture(droppedRelativePath)){
                                *valueRef = Texture(droppedRelativePath);
                                if (componentType == ComponentType::MeshComponent){
                                    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                                    for (unsigned int i = 0; i < numSubmeshes; i++){
                                        sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                                    }
                                }
                                if (componentType == ComponentType::UIComponent){
                                    sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                                }
                                //printf("needUpdateTexture %s\n", name.c_str());
                            }
                        }
                    }
                    if (payload->IsDelivery()){
                        Texture texture(droppedRelativePath);
                        for (Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            *valueRef = originalTex[id][entity];
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasTextureDrag.erase(id);
                        originalTex.erase(id);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasTextureDrag.count(id) && hasTextureDrag[id]){
            for (Entity& entity : entities){
                Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                if (*valueRef != originalTex[id][entity]){
                    *valueRef = originalTex[id][entity];
                    if (componentType == ComponentType::MeshComponent){
                        unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                        for (unsigned int i = 0; i < numSubmeshes; i++){
                            sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                        }
                    }
                    if (componentType == ComponentType::UIComponent){
                        sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                    }
                    //printf("needUpdateTexture %s\n", id.c_str());
                }
            }

            hasTextureDrag.erase(id);
            originalTex.erase(id);
        }
    }
}

void editor::Properties::dragDropResourcesTextureCubeFace(ComponentType cpType, const std::string& id, size_t faceIndex, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    const std::string dragId = id + "##cube_face_" + std::to_string(faceIndex);

    const ImRect rect(rectMin, rectMax);
    if (ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID(dragId.c_str()))){
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (!receivedStrings.empty()){
                const std::string droppedRelativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();

                if (Util::isImageFile(droppedRelativePath)) {
                    if (!hasTextureDrag.count(dragId)){
                        hasTextureDrag[dragId] = true;
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            originalTex[dragId][entity] = Texture(*valueRef);

                            Texture updated = Texture(*valueRef);
                            updated.setCubePath(faceIndex, droppedRelativePath);
                            if (*valueRef != updated){
                                *valueRef = updated;
                                if (componentType == ComponentType::MeshComponent){
                                    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                                    for (unsigned int i = 0; i < numSubmeshes; i++){
                                        sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                                    }
                                }
                                if (componentType == ComponentType::UIComponent){
                                    sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                                }
                                if (componentType == ComponentType::SkyComponent){
                                    sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                                }
                            }
                        }
                    }

                    if (payload->IsDelivery()){
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            *valueRef = originalTex[dragId][entity];

                            Texture updated = Texture(*valueRef);
                            updated.setCubePath(faceIndex, droppedRelativePath);
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, updated);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasTextureDrag.erase(dragId);
                        originalTex.erase(dragId);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasTextureDrag.count(dragId) && hasTextureDrag[dragId]){
            for (const Entity& entity : entities){
                Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                if (*valueRef != originalTex[dragId][entity]){
                    *valueRef = originalTex[dragId][entity];
                    if (componentType == ComponentType::MeshComponent){
                        unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                        for (unsigned int i = 0; i < numSubmeshes; i++){
                            sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                        }
                    }
                    if (componentType == ComponentType::UIComponent){
                        sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                    }
                    if (componentType == ComponentType::SkyComponent){
                        sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                    }
                }
            }

            hasTextureDrag.erase(dragId);
            originalTex.erase(dragId);
        }
    }
}

void editor::Properties::dragDropResourcesTextureCubeSingleFile(ComponentType cpType, const std::string& id, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    const std::string dragId = id + "##cube_single";

    const ImRect rect(rectMin, rectMax);
    if (ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID(dragId.c_str()))){
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (!receivedStrings.empty()){
                const std::string droppedRelativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();

                if (Util::isImageFile(droppedRelativePath)) {
                    if (!hasTextureDrag.count(dragId)){
                        hasTextureDrag[dragId] = true;
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            originalTex[dragId][entity] = Texture(*valueRef);

                            Texture updated = Texture(*valueRef);
                            updated.setCubeMap(droppedRelativePath);
                            if (*valueRef != updated){
                                *valueRef = updated;
                                if (componentType == ComponentType::MeshComponent){
                                    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                                    for (unsigned int i = 0; i < numSubmeshes; i++){
                                        sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                                    }
                                }
                                if (componentType == ComponentType::UIComponent){
                                    sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                                }
                                if (componentType == ComponentType::SkyComponent){
                                    sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                                }
                            }
                        }
                    }

                    if (payload->IsDelivery()){
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            *valueRef = originalTex[dragId][entity];

                            Texture updated = Texture(*valueRef);
                            updated.setCubeMap(droppedRelativePath);
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, updated);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasTextureDrag.erase(dragId);
                        originalTex.erase(dragId);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasTextureDrag.count(dragId) && hasTextureDrag[dragId]){
            for (const Entity& entity : entities){
                Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                if (*valueRef != originalTex[dragId][entity]){
                    *valueRef = originalTex[dragId][entity];
                    if (componentType == ComponentType::MeshComponent){
                        unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                        for (unsigned int i = 0; i < numSubmeshes; i++){
                            sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                        }
                    }
                    if (componentType == ComponentType::UIComponent){
                        sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                    }
                    if (componentType == ComponentType::SkyComponent){
                        sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                    }
                }
            }

            hasTextureDrag.erase(dragId);
            originalTex.erase(dragId);
        }
    }
}

void editor::Properties::handleComponentMenu(SceneProject* sceneProject, std::vector<Entity> entities, ComponentType cpType, bool isBundle, bool isBundleOverridden, bool& headerOpen, bool readOnly) {
    if (ImGui::BeginPopupContextItem(("component_options_menu_" + std::to_string(static_cast<int>(cpType))).c_str())) {
        ImGui::TextDisabled("Component options");
        ImGui::Separator();

        ImGui::BeginDisabled(readOnly); // disable all actions while playing

        if (isBundle){
            if (isBundleOverridden) {
                if (ImGui::MenuItem(ICON_FA_CUBE " Revert to Bundle")) {
                    for (Entity& entity : entities){
                        cmd = new ComponentToBundleSharedCmd(project, sceneProject->id, entity, cpType);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    cmd->setNoMerge();
                }

            } else {
                if (ImGui::MenuItem(ICON_FA_LOCK_OPEN " Make Unique")) {
                    for (Entity& entity : entities){
                        cmd = new ComponentToBundleLocalCmd(project, sceneProject->id, entity, cpType);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    cmd->setNoMerge();
                }
            }
        }

        bool canRemove = !(cpType == ComponentType::Transform && isBundle);
        if (ImGui::MenuItem(ICON_FA_TRASH " Remove", nullptr, false, canRemove)) {
            for (Entity& entity : entities){
                cmd = new RemoveComponentCmd(project, sceneProject->id, entity, cpType);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            cmd->setNoMerge();

            headerOpen = false;
        }

        ImGui::EndDisabled();

        ImGui::EndPopup();
    }
}

bool editor::Properties::canAddComponent(SceneProject* sceneProject, Entity entity, ComponentType cpType) {
    // Check if entity already has this component
    std::vector<ComponentType> existingComponents = Catalog::findComponents(sceneProject->scene, entity);
    return std::find(existingComponents.begin(), existingComponents.end(), cpType) == existingComponents.end();
}

Texture editor::Properties::getMaterialPreview(const Material& material, const std::string id){
    MaterialRender& materialRender = materialRenders[id];

    auto texPending = [](const Texture& t) {
        std::string tid = t.getId();
        return !tid.empty() && !TexturePool::get(tid);
    };

    bool pending = texPending(material.baseColorTexture) || texPending(material.emissiveTexture) ||
                   texPending(material.metallicRoughnessTexture) || texPending(material.occlusionTexture) ||
                   texPending(material.normalTexture);

    if ((materialRender.getMaterial() != material) || !materialRender.getFramebuffer()->isCreated() || pending){
        materialRender.applyMaterial(material);
        Engine::executeSceneOnce(materialRender.getScene());
    }

    usedPreviewIds.insert(id);

    return materialRender.getTexture();
}

Texture editor::Properties::getDirectionPreview(const Vector3& direction, const std::string id){
    DirectionRender& directionRender = directionRenders[id];

    if ((directionRender.getDirection() != direction) || !directionRender.getFramebuffer()->isCreated()){
        directionRender.setDirection(direction);
        Engine::executeSceneOnce(directionRender.getScene());
    }

    usedPreviewIds.insert(id);

    return directionRender.getTexture();
}

bool editor::Properties::drawSpriteFramePreview(Texture* texture, const Rect& rect, const ImVec2& size, const char* itemId){
    if (!texture || texture->empty() || !texture->getRender()) {
        return false;
    }

    const float texWidth = static_cast<float>(texture->getWidth());
    const float texHeight = static_cast<float>(texture->getHeight());
    const float rectWidth = rect.getWidth();
    const float rectHeight = rect.getHeight();

    if (texWidth <= 0.0f || texHeight <= 0.0f || rectWidth <= 0.0f || rectHeight <= 0.0f) {
        return false;
    }

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 p_min = cursor;
    ImVec2 p_max = ImVec2(cursor.x + size.x, cursor.y + size.y);

    float srcAspect = rectWidth / rectHeight;
    float dstAspect = size.x / size.y;

    float drawWidth = size.x;
    float drawHeight = size.y;
    if (fabs(srcAspect - dstAspect) > 1e-3f) {
        if (srcAspect > dstAspect) {
            drawHeight = size.x / srcAspect;
        } else {
            drawWidth = size.y * srcAspect;
        }
    }

    ImVec2 imageMin(cursor.x + (size.x - drawWidth) * 0.5f, cursor.y + (size.y - drawHeight) * 0.5f);
    ImVec2 imageMax(imageMin.x + drawWidth, imageMin.y + drawHeight);

    ImVec2 uv0(rect.getX() / texWidth, rect.getY() / texHeight);
    ImVec2 uv1((rect.getX() + rectWidth) / texWidth, (rect.getY() + rectHeight) / texHeight);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float rounding = ImGui::GetStyle().FrameRounding;
    ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);

    drawList->AddRectFilled(p_min, p_max, ImGui::GetColorU32(App::ThemeColors::filenameLabel), rounding, ImDrawFlags_RoundCornersAll);
    drawList->AddImageRounded((ImTextureID)(intptr_t)texture->getRender()->getGLHandler(), imageMin, imageMax, uv0, uv1, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersAll);
    drawList->AddRect(p_min, p_max, borderColor, rounding, ImDrawFlags_RoundCornersAll, 1.0f);

    ImGui::InvisibleButton(itemId, size);
    return true;
}

void editor::Properties::updateShapePreview(const ShapeParameters& shapeParams){
    ImVec4 frameBgColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    std::shared_ptr<doriax::MeshSystem> meshSys = shapePreviewRender.getScene()->getSystem<MeshSystem>();

    MeshComponent meshComp;

    updateMeshShape(meshComp, meshSys.get(), shapeParams);

    shapePreviewRender.applyMesh(Stream::encodeMeshComponent(meshComp), true, true);
    shapePreviewRender.setBackground(Vector4(frameBgColor.x, frameBgColor.y, frameBgColor.z, frameBgColor.w));

    Engine::executeSceneOnce(shapePreviewRender.getScene());
}

void editor::Properties::updateMeshShape(MeshComponent& meshComp, MeshSystem* meshSys, const ShapeParameters& shapeParams){
    switch (shapeParams.geometryType) {
        case 0: // Plane
            meshSys->createPlane(meshComp, shapeParams.planeWidth, shapeParams.planeDepth, shapeParams.planeTiles);
            break;
        case 1: // Box
            meshSys->createBox(meshComp, shapeParams.boxWidth, shapeParams.boxHeight, shapeParams.boxDepth, shapeParams.boxTiles);
            break;
        case 2: // Sphere
            meshSys->createSphere(meshComp, shapeParams.sphereRadius, shapeParams.sphereSlices, shapeParams.sphereStacks);
            break;
        case 3: // Cylinder
            meshSys->createCylinder(meshComp, shapeParams.cylinderBaseRadius, shapeParams.cylinderTopRadius, shapeParams.cylinderHeight, shapeParams.cylinderSlices, shapeParams.cylinderStacks);
            break;
        case 4: // Capsule
            meshSys->createCapsule(meshComp, shapeParams.capsuleBaseRadius, shapeParams.capsuleTopRadius, shapeParams.capsuleHeight, shapeParams.capsuleSlices, shapeParams.capsuleStacks);
            break;
        case 5: // Torus
            meshSys->createTorus(meshComp, shapeParams.torusRadius, shapeParams.torusRingRadius, shapeParams.torusSides, shapeParams.torusRings);
            break;
    }
}

void editor::Properties::drawNinePatchesPreview(const ImageComponent& img, Texture* texture, Texture* thumbTexture, const ImVec2& size){
    float availWidth = ImGui::GetContentRegionAvail().x;

    // Calculate display size based on input parameter or default to texture size
    float displayWidth = (size.x > 0) ? size.x : thumbTexture->getWidth();
    float displayHeight = (size.y > 0) ? size.y : thumbTexture->getHeight();

    // If only one dimension is specified, maintain aspect ratio
    if (size.x > 0 && size.y <= 0) {
        float aspectRatio = (float)thumbTexture->getHeight() / thumbTexture->getWidth();
        displayHeight = displayWidth * aspectRatio;
    } else if (size.x <= 0 && size.y > 0) {
        float aspectRatio = (float)thumbTexture->getWidth() / thumbTexture->getHeight();
        displayWidth = displayHeight * aspectRatio;
    }

    // Calculate position to center the image
    float xPos = (availWidth - displayWidth) * 0.5f;
    // Set cursor position to create centering effect
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xPos);

    // Store the cursor position before drawing the image
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Draw the image with the calculated size
    ImGui::Image(thumbTexture->getRender()->getGLHandler(), ImVec2(displayWidth, displayHeight));

    // Get draw list for custom rendering
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Define line color
    ImU32 line_color = IM_COL32(255, 0, 0, 255); // Red color

    // Get the original texture and thumbnail dimensions
    float origWidth = static_cast<float>(texture->getWidth());
    float origHeight = static_cast<float>(texture->getHeight());
    float thumbWidth = static_cast<float>(thumbTexture->getWidth());
    float thumbHeight = static_cast<float>(thumbTexture->getHeight());

    // Calculate scale factors between original texture and display size
    float scaleX = displayWidth / origWidth;
    float scaleY = displayHeight / origHeight;

    // Scale the margin values by the appropriate scale factor
    float scaledLeftMargin = img.patchMarginLeft * scaleX;
    float scaledRightMargin = img.patchMarginRight * scaleX;
    float scaledTopMargin = img.patchMarginTop * scaleY;
    float scaledBottomMargin = img.patchMarginBottom * scaleY;

    // Draw left margin line
    float leftX = cursor.x + scaledLeftMargin;
    draw_list->AddLine(
        ImVec2(leftX, cursor.y),
        ImVec2(leftX, cursor.y + displayHeight),
        line_color, 1.0f
    );

    // Draw right margin line
    float rightX = cursor.x + displayWidth - scaledRightMargin;
    draw_list->AddLine(
        ImVec2(rightX, cursor.y),
        ImVec2(rightX, cursor.y + displayHeight),
        line_color, 1.0f
    );

    // Draw top margin line
    float topY = cursor.y + scaledTopMargin;
    draw_list->AddLine(
        ImVec2(cursor.x, topY),
        ImVec2(cursor.x + displayWidth, topY),
        line_color, 1.0f
    );

    // Draw bottom margin line
    float bottomY = cursor.y + displayHeight - scaledBottomMargin;
    draw_list->AddLine(
        ImVec2(cursor.x, bottomY),
        ImVec2(cursor.x + displayWidth, bottomY),
        line_color, 1.0f
    );
}


void editor::Properties::beginTable(ComponentType cpType, float firstColSize, std::string nameAddon){
    ImGui::PushItemWidth(-1);
    if (!nameAddon.empty()){
        nameAddon = "_"+nameAddon;
    }
    ImGui::BeginTable(("table_"+Catalog::getComponentName(cpType)+nameAddon).c_str(), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, firstColSize);
    ImGui::TableSetupColumn("Value");

}

void editor::Properties::endTable(){
    ImGui::EndTable();
}

editor::Properties::EntityPickerResult editor::Properties::drawEntityPickerPopup(
        const std::string& popupId, const Signature& filter,
        SceneProject* owningScene, bool includeChildScenes,
        Entity currentValue, uint32_t currentValueSceneId) {

    EntityPickerResult result;
    if (!ImGui::BeginPopup(popupId.c_str())) return result;

    ImGui::Text("Select Entity");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ep_search", entityPickerSearchBuffer, sizeof(entityPickerSearchBuffer));

    if (ImGui::BeginChild("##ep_list", ImVec2(300.0f, 220.0f), true)) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y);

        std::string searchStr = entityPickerSearchBuffer;
        for (char& c : searchStr) c = (char)tolower((unsigned char)c);

        // Build list of (SceneProject*, storedSceneId) to display
        // storedSceneId = 0 for owning scene entities, actual scene ID for child scene entities
        std::vector<std::pair<SceneProject*, uint32_t>> scenesToShow;
        scenesToShow.emplace_back(owningScene, (uint32_t)0);

        if (includeChildScenes) {
            std::unordered_set<uint32_t> visited;
            visited.insert(owningScene->id);
            std::vector<uint32_t> toVisit;
            for (uint32_t childId : project->getChildScenes(owningScene->id))
                toVisit.push_back(childId);
            while (!toVisit.empty()) {
                uint32_t sid = toVisit.back(); toVisit.pop_back();
                if (visited.count(sid)) continue;
                visited.insert(sid);
                SceneProject* child = project->getScene(sid);
                if (child && child->scene)
                    scenesToShow.emplace_back(child, sid);
                for (uint32_t childId : project->getChildScenes(sid))
                    if (!visited.count(childId)) toVisit.push_back(childId);
            }
        }

        bool anyVisible = false;
        for (auto& [sp, storedSceneId] : scenesToShow) {
            bool sceneMatches = false;
            if (!searchStr.empty() && storedSceneId != 0) {
                std::string slow = sp->name;
                for (char& c : slow) c = (char)tolower((unsigned char)c);
                sceneMatches = slow.find(searchStr) != std::string::npos;
            }

            for (Entity e : sp->entities) {
                if (!sp->scene->isEntityCreated(e)) continue;
                if (filter.any() && (sp->scene->getSignature(e) & filter) != filter) continue;

                std::string ename = sp->scene->getEntityName(e);
                if (ename.empty()) ename = "Entity " + std::to_string(e);

                if (!searchStr.empty() && !sceneMatches) {
                    std::string elow = ename;
                    for (char& c : elow) c = (char)tolower((unsigned char)c);
                    if (elow.find(searchStr) == std::string::npos) continue;
                }

                anyVisible = true;
                bool isSelected = (e == currentValue && storedSceneId == currentValueSceneId);
                std::string eicon = Structure::getObjectIcon(sp->scene->getSignature(e), sp->scene);
                std::string selId = "##ep_ent_" + std::to_string(storedSceneId) + "_" + std::to_string(e);

                if (ImGui::Selectable(selId.c_str(), isSelected)) {
                    result.chosen = true;
                    result.entity = e;
                    result.sceneId = storedSceneId;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (storedSceneId != 0)
                    ImGui::Text("%s  %s  [%s]  (%u)", eicon.c_str(), ename.c_str(), sp->name.c_str(), static_cast<unsigned int>(e));
                else
                    ImGui::Text("%s  %s  (%u)", eicon.c_str(), ename.c_str(), static_cast<unsigned int>(e));
            }
        }

        if (!anyVisible) ImGui::TextDisabled("No entities found");
    }
    ImGui::EndChild();
    ImGui::EndPopup();

    return result;
}

bool editor::Properties::propertyHeader(std::string label, float secondColSize, bool defChanged, bool child){
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
    ImGui::TableNextRow();
    if (child){
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_PopupBg]));
    }
    ImGui::TableNextColumn();
    //ImGui::Dummy(ImVec2(0, 10));
    ImGui::Text("%s", label.c_str());
    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    bool button = false;
    if (defChanged){
        button = ImGui::Button((ICON_FA_ROTATE_LEFT"##"+label).c_str());
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(secondColSize);

    return button;
}

bool editor::Properties::propertyRow(RowPropertyType type, ComponentType cpType, std::string id, std::string label, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings){
    bool result = true;

    constexpr float compThreshold = 1e-4;
    constexpr float zeroThreshold = 1e-4;

    if (type == RowPropertyType::Label){
        std::string displayValue;
        bool different = false;

        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            std::string value = formatPropertyLabelValue(prop);
            if (displayValue.empty()) {
                displayValue = value;
            } else if (displayValue != value) {
                different = true;
            }
        }

        propertyHeader(label, settings.secondColSize, false, settings.child);
        if (different) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextUnformatted("---");
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(displayValue.c_str());
        }

    }else if (type == RowPropertyType::Vector2){
        Vector2* value = nullptr;
        bool difX = false;
        bool difY = false;
        std::map<Entity, Vector2> eValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector2*>(prop.ref);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
            }
            value = &eValue[entity];
        }

        Vector2 newValue = *value;
    bool clampNonNegativeTilePosition = cpType == ComponentType::TilemapComponent
        && id.rfind("tiles[", 0) == 0
        && id.size() >= 9
        && id.compare(id.size() - 9, 9, ".position") == 0;

        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&newValue, defArr, 2, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector2>(project, sceneProject->id, entity, cpType, id, static_cast<Vector2>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushMultiItemsWidths(2, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[2] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255)    // Green for Y
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                Vector2 nextValue(newValue.x, eValue[entity].y);
                if (clampNonNegativeTilePosition){
                    nextValue.x = std::max(0.0f, nextValue.x);
                    nextValue.y = std::max(0.0f, nextValue.y);
                }
                cmd = new PropertyCmd<Vector2>(project, sceneProject->id, entity, cpType, id, nextValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                Vector2 nextValue(eValue[entity].x, newValue.y);
                if (clampNonNegativeTilePosition){
                    nextValue.x = std::max(0.0f, nextValue.x);
                    nextValue.y = std::max(0.0f, nextValue.y);
                }
                cmd = new PropertyCmd<Vector2>(project, sceneProject->id, entity, cpType, id, nextValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::Vector3 || type == RowPropertyType::Direction){
        Vector3* value = nullptr;
        bool difX = false;
        bool difY = false;
        bool difZ = false;
        std::map<Entity, Vector3> eValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector3*>(prop.ref);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
                if (std::fabs(value->z - eValue[entity].z) > compThreshold)
                    difZ = true;
            }
            value = &eValue[entity];
        }

        Vector3 newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&newValue, defArr, 3, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, static_cast<Vector3>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        float min = 0.0f;
        float max = 0.0f;
        if (type == RowPropertyType::Direction){
            min = -1.0f;
            max = 1.0f;

            Texture dirTexRender = getDirectionPreview(newValue, id);
            float thumbSize = ImGui::GetFrameHeight() * 3;
            ImU32 border_col = IM_COL32(128, 128, 128, 255); // Gray border

            drawImageWithBorderAndRounding(&dirTexRender, ImVec2(thumbSize, thumbSize), 4.0f, border_col, 1.0f, true);

            static bool draggingDirection = false;

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                draggingDirection = true;

                ImVec2 mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

                float sensitivity = 1.5f;

                float yawAngle = mouseDelta.x * sensitivity;   // Horizontal movement -> roll (around Y)
                float pitchAngle = -mouseDelta.y * sensitivity; // Vertical movement -> pitch (around X)

                Quaternion rollRotation(yawAngle, Vector3(0, 0, 1));     // Roll around world Z-axis
                Quaternion pitchRotation(pitchAngle, Vector3(1, 0, 0)); // Pitch around X-axis
                // Combine rotations (order matters: apply roll first, then pitch)
                Vector3 newDirection =  rollRotation * pitchRotation * newValue;

                // Apply to all entities
                for (Entity& entity : entities) {
                    cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, newDirection, settings.onValueChanged);
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                }

                newValue = newDirection;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }

            if (draggingDirection && ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                finishProperty = true;
            }
        }

        ImGui::SetNextItemWidth(settings.secondColSize);

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[3] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255),   // Green for Y
            IM_COL32(60, 60, 220, 255)    // Blue for Z
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, min, max, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Vector3(newValue.x, eValue[entity].y, eValue[entity].z), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, min, max, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Vector3(eValue[entity].x, newValue.y, eValue[entity].z), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difZ)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosZ = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_z_"+id).c_str(), &(newValue.z), settings.stepSize, min, max, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Vector3(eValue[entity].x, eValue[entity].y, newValue.z), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw blue bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosZ.x + 2, inputPosZ.y + 2);
            float inputHeightZ = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosZ.x + 2 + colorBarWidth, inputPosZ.y + inputHeightZ - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[2]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Z");
        }

        if (difZ)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::Vector4){
        Vector4* value = nullptr;
        bool difX = false;
        bool difY = false;
        bool difZ = false;
        bool difW = false;
        std::map<Entity, Vector4> eValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector4*>(prop.ref);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
                if (std::fabs(value->z - eValue[entity].z) > compThreshold)
                    difZ = true;
                if (std::fabs(value->w - eValue[entity].w) > compThreshold)
                    difW = true;
            }
            value = &eValue[entity];
        }

        Vector4 newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&newValue, defArr, 4, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, static_cast<Vector4>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushMultiItemsWidths(4, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[4] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255),   // Green for Y
            IM_COL32(60, 60, 220, 255),   // Blue for Z
            IM_COL32(220, 220, 220, 255)  // White/Grey for W
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(newValue.x, eValue[entity].y, eValue[entity].z, eValue[entity].w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(eValue[entity].x, newValue.y, eValue[entity].z, eValue[entity].w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difZ)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosZ = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_z_"+id).c_str(), &(newValue.z), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(eValue[entity].x, eValue[entity].y, newValue.z, eValue[entity].w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw blue bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosZ.x + 2, inputPosZ.y + 2);
            float inputHeightZ = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosZ.x + 2 + colorBarWidth, inputPosZ.y + inputHeightZ - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[2]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Z");
        }

        if (difZ)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difW)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosW = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_w_"+id).c_str(), &(newValue.w), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(eValue[entity].x, eValue[entity].y, eValue[entity].z, newValue.w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw white/grey bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosW.x + 2, inputPosW.y + 2);
            float inputHeightW = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosW.x + 2 + colorBarWidth, inputPosW.y + inputHeightW - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[3]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("W");
        }

        if (difW)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::Quat){
        RotationOrder order = RotationOrder::ZYX;
        Vector3* value = nullptr;
        bool difX = false;
        bool difY = false;
        bool difZ = false;
        std::map<Entity, Vector3> eValue;
        Quaternion qValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            qValue = *static_cast<Quaternion*>(prop.ref);
            eValue[entity] = roundZero(Quaternion(qValue).normalize().getEulerAngles(order), zeroThreshold);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
                if (std::fabs(value->z - eValue[entity].z) > compThreshold)
                    difZ = true;
            }
            value = &eValue[entity];
        }

        Vector3 newValue = *value;

        // using 'qValue' to compare quaternions
        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&qValue, defArr, 4, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, static_cast<Quaternion>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[3] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255),   // Green for Y
            IM_COL32(60, 60, 220, 255)    // Blue for Z
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, 0.0f, 0.0f, (std::string(settings.format) + "°").c_str())){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, Quaternion(newValue.x, eValue[entity].y, eValue[entity].z, order), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, 0.0f, 0.0f, "%.2f°")){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, Quaternion(eValue[entity].x, newValue.y, eValue[entity].z, order), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difZ)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosZ = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_z_"+id).c_str(), &(newValue.z), settings.stepSize, 0.0f, 0.0f, "%.2f°")){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, Quaternion(eValue[entity].x, eValue[entity].y, newValue.z, order), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw blue bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosZ.x + 2, inputPosZ.y + 2);
            float inputHeightZ = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosZ.x + 2 + colorBarWidth, inputPosZ.y + inputHeightZ - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[2]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Z");
        }

        if (difZ)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s in degrees (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::String || type == RowPropertyType::MultilineString){
        std::string* value = nullptr;
        std::map<Entity, std::string> eValue;
        bool dif = false;
        std::string* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<std::string*>(prop.def);
            eValue[entity] = *static_cast<std::string*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        std::string newValue = *value;

        std::vector<char> buffer(newValue.begin(), newValue.end());
        buffer.resize(std::max((size_t)1024, newValue.size() + 256));
        buffer[newValue.size()] = '\0';

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        bool inputChanged = false;
        if (type == RowPropertyType::MultilineString){
            inputChanged = ImGui::InputTextMultiline(("##input_string_"+id).c_str(), buffer.data(), buffer.size(), ImVec2(0, ImGui::GetTextLineHeight() * 6));
        }else{
            inputChanged = ImGui::InputText(("##input_string_"+id).c_str(), buffer.data(), buffer.size());
        }

        if (inputChanged){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, std::string(buffer.data()), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();

    }else if (type == RowPropertyType::Bool){
        bool* value = nullptr;
        std::map<Entity, bool> eValue;
        bool dif = false;
        bool* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<bool*>(prop.def);
            eValue[entity] = *static_cast<bool*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        bool newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<bool>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Checkbox(("##checkbox_"+id).c_str(), &newValue)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<bool>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

        if (!settings.help.empty()){
            ImGui::SameLine(); helpMarker(settings.help);
        }

    }else if (type == RowPropertyType::Float || type == RowPropertyType::FloatPositive || type == RowPropertyType::Float_0_1 || type == RowPropertyType::HalfCone){
        float* value = nullptr;
        std::map<Entity, float> eValue;
        bool dif = false;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<float*>(prop.ref);
            if (type == RowPropertyType::HalfCone){
                eValue[entity] = Angle::radToDefault(std::acos(eValue[entity]) * 2);
            }
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        float newValue = *value;

        bool defChanged = false;
        if (defArr){
            if (type == RowPropertyType::HalfCone){
                float def = *defArr;
                defChanged = (newValue != Angle::radToDefault(std::acos(def) * 2));
            }else{
                defChanged = (newValue != *defArr);
            }
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        float v_min = (0.0F);
        float v_max = (0.0F);
        if (type == RowPropertyType::Float_0_1){
            v_min = 0.0F;
            v_max = 1.0F;
        }else if (type == RowPropertyType::FloatPositive){
            v_min = 0.0F;
            v_max = FLT_MAX;
        }

        std::string newFormat = settings.format;
        if (type == RowPropertyType::HalfCone){
            newFormat = std::string(settings.format) + "°";
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::DragFloat(("##input_float_"+id).c_str(), &newValue, settings.stepSize, v_min, v_max, newFormat.c_str())){
            for (Entity& entity : entities){
                if (type == RowPropertyType::HalfCone){
                    cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, id, cos(Angle::defaultToRad(newValue / 2)), settings.onValueChanged);
                }else{
                    cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                }
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

        if (!settings.help.empty()){
            ImGui::SameLine(); helpMarker(settings.help);
        }

    }else if (type == RowPropertyType::UInt){
        unsigned int* value = nullptr;
        std::map<Entity, unsigned int> eValue;
        bool dif = false;
        unsigned int* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<unsigned int*>(prop.def);
            eValue[entity] = *static_cast<unsigned int*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        unsigned int newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::DragInt(("##input_uint_"+id).c_str(), (int*)&newValue, static_cast<int>(ceil(settings.stepSize)), 0.0f, INT_MAX)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if (type == RowPropertyType::Int){
        int* value = nullptr;
        std::map<Entity, int> eValue;
        bool dif = false;
        int* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<int*>(prop.def);
            eValue[entity] = *static_cast<int*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        int newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::DragInt(("##input_int_"+id).c_str(), &newValue, static_cast<int>(ceil(settings.stepSize)), 0.0f, 0.0f)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if (type == RowPropertyType::Color3L){
        Vector3* value = nullptr;
        std::map<Entity, Vector3> eValue;
        bool dif = false;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector3*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Vector3 newValue = Color::linearTosRGB(*value);

        // using 'value' beacause it is linear too
        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)value, defArr, 3, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, static_cast<Vector3>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::ColorEdit3((label+"##checkbox_"+id).c_str(), (float*)&newValue.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Color::sRGBToLinear(newValue), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if (type == RowPropertyType::Color4L){
        Vector4* value = nullptr;
        std::map<Entity, Vector4> eValue;
        bool dif = false;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector4*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Vector4 newValue = Color::linearTosRGB(*value);

        // using 'value' beacause it is linear too
        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)value, defArr, 4, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, static_cast<Vector4>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::ColorEdit4((label+"##checkbox_"+id).c_str(), (float*)&newValue.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Color::sRGBToLinear(newValue), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if ((type == RowPropertyType::IntSlider || type == RowPropertyType::UIntSlider) && settings.sliderValues){
        int* value = nullptr;
        std::map<Entity, int> eValue;
        bool dif = false;
        void* defArr = nullptr;
        std::vector<int>* sliderValues = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = prop.def;
            sliderValues = settings.sliderValues;
            if (type == RowPropertyType::IntSlider) {
                eValue[entity] = *static_cast<int*>(prop.ref);
            } else {
                eValue[entity] = *static_cast<unsigned int*>(prop.ref);
            }
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        int newValue = *value;

        bool defChanged = false;
        if (defArr){
            if (type == RowPropertyType::IntSlider) {
                defChanged = (newValue != *static_cast<int*>(defArr));
            } else {
                defChanged = (newValue != *static_cast<unsigned int*>(defArr));
            }
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                if (type == RowPropertyType::IntSlider) {
                    cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, *static_cast<int*>(defArr), settings.onValueChanged);
                } else {
                    cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, *static_cast<unsigned int*>(defArr), settings.onValueChanged);
                }
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        // Find current value index in the slider values array
        int currentIndex = 0;
        for (size_t i = 0; i < sliderValues->size(); ++i) {
            if ((*sliderValues)[i] == newValue) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Create format string with current value
        char formatStr[32];
        snprintf(formatStr, sizeof(formatStr), "%d", (*sliderValues)[currentIndex]);

        if (ImGui::SliderInt(("##intslider_"+id).c_str(), &currentIndex, 0, static_cast<int>(sliderValues->size() - 1), formatStr)) {
            int newSliderValue = (*sliderValues)[currentIndex];
            for (Entity& entity : entities){
                if (type == RowPropertyType::IntSlider) {
                    cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, newSliderValue, settings.onValueChanged);
                } else {
                    cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, static_cast<unsigned int>(newSliderValue), settings.onValueChanged);
                }
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        if (dif)
            ImGui::PopStyleColor();

    }else if (type == RowPropertyType::Enum && settings.enumEntries) {
        int* value = nullptr;
        std::map<Entity, int> eValue;
        bool dif = false;
        int* defArr = nullptr;
        std::vector<EnumEntry>* enumEntries = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<int*>(prop.def);
            enumEntries = settings.enumEntries;
            eValue[entity] = *static_cast<int*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        int item_current = 0;
        // Find current index in enumEntries
        for (size_t i = 0; i < enumEntries->size(); ++i) {
            if ((*enumEntries)[i].value == *value) {
                item_current = static_cast<int>(i);
                break;
            }
        }
        int item_default = item_current;

        bool defChanged = false;
        if (defArr){
            int defValue = *defArr;
            // Find index of default value in enumEntries
            for (size_t i = 0; i < enumEntries->size(); ++i) {
                if ((*enumEntries)[i].value == defValue) {
                    item_default = static_cast<int>(i);
                    break;
                }
            }
            defChanged = (item_current != item_default);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                int defValue = (*enumEntries)[item_default].value;
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, defValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        // Build names array
        std::vector<const char*> names;
        for (const auto& entry : *enumEntries) {
            names.push_back(entry.name);
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Combo(("##combo_" + id).c_str(), &item_current, names.data(), static_cast<int>(names.size()))) {
            int newValue = (*enumEntries)[item_current].value;
            for (Entity& entity : entities){
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();

    }else if (type == RowPropertyType::Font){
        std::string* value = nullptr;
        std::map<Entity, std::string> eValue;
        bool dif = false;
        std::string* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<std::string*>(prop.def);
            eValue[entity] = *static_cast<std::string*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        std::string newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushID(("font_"+id).c_str());

        ImGui::PushStyleColor(ImGuiCol_ChildBg, App::ThemeColors::filenameLabel);

        // Use calculated width for the frame
        ImGui::BeginChild("fontframe", ImVec2(- ImGui::CalcTextSize(ICON_FA_GEAR).x - ImGui::GetStyle().ItemSpacing.x * 2 - ImGui::GetStyle().FramePadding.x * 2, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2), 
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        std::string fontName = newValue;
        std::error_code ec;
        if (std::filesystem::exists(fontName, ec)) {
            fontName = std::filesystem::path(fontName).filename().string();
        }
        if (fontName.empty()) {
            fontName = "< Default >";
        }

        float textWidth = ImGui::CalcTextSize(fontName.c_str()).x;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(availWidth - textWidth - 2);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", fontName.c_str());
        if (dif)
            ImGui::PopStyleColor();

        ImGui::EndChild();
        if (!newValue.empty()){
            ImGui::SetItemTooltip("%s", newValue.c_str());
        }

        dragDropResourcesFont(cpType, id, sceneProject, entities, cpType);

        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
            std::string path = editor::FileDialogs::openFileDialog(project->getProjectPath().string(), FILE_DIALOG_FONT);
            if (!path.empty()) {
                std::filesystem::path projectPath = project->getProjectPath();
                std::filesystem::path filePath = std::filesystem::absolute(path);

                // Check if file path is within project directory
                std::error_code ec;
                auto relative = std::filesystem::relative(filePath, projectPath, ec);
                if (ec || relative.string().find("..") != std::string::npos) {
                    ImGui::OpenPopup("File Import Error");
                }else{
                    std::string finalPath = relative.string();
                    for (Entity& entity : entities){
                        cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, finalPath, settings.onValueChanged);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                        finishProperty = true;
                    }
                }
            }
        }

        // Error popup modal
        if (ImGui::BeginPopupModal("File Import Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be within the project directory.");
            ImGui::Separator();

            float buttonWidth = 120;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        ImGui::EndGroup();

        if (ImGui::BeginDragDropTarget()){
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
                if (receivedStrings.size() > 0){
                    if (payload->IsDelivery()){
                        const std::string relativeFontPath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();

                        for (Entity& entity : entities){
                            cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, relativeFontPath, settings.onValueChanged);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            finishProperty = true;
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

    }else if (type == RowPropertyType::Texture){
        Texture* value = nullptr;
        std::map<Entity, Texture> eValue;
        bool dif = false;
        Texture* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<Texture*>(prop.def);
            eValue[entity] = *static_cast<Texture*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Texture newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushID(("texture_"+id).c_str());

        ImGui::PushStyleColor(ImGuiCol_ChildBg, App::ThemeColors::filenameLabel);

        float thumbSize = ImGui::GetFrameHeight() * 3;
        Texture* thumbTexture = findThumbnail(newValue.getPath());
        if (thumbTexture) {
            ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            if (dif){
                border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            }
            drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Image(thumbTexture->getRender()->getGLHandler(), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                ImGui::EndTooltip();
            }
        }

        // Use calculated width for the frame
        ImGui::BeginChild("textureframe", ImVec2(- ImGui::CalcTextSize(ICON_FA_GEAR).x - ImGui::GetStyle().ItemSpacing.x * 2 - ImGui::GetStyle().FramePadding.x * 2, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2), 
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        std::string texName = newValue.getId();
        if (std::filesystem::exists(texName)) {
            texName = std::filesystem::path(texName).filename().string();
        }
        if (texName.empty()) {
            texName = "< Not set >";
        }

        float textWidth = ImGui::CalcTextSize(texName.c_str()).x;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(availWidth - textWidth - 2);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", texName.c_str());
        if (dif)
            ImGui::PopStyleColor();

        ImGui::EndChild();
        if (!newValue.getId().empty()){
            ImGui::SetItemTooltip("%s", newValue.getId().c_str());
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
            std::string path = editor::FileDialogs::openFileDialog(project->getProjectPath().string(), FILE_DIALOG_IMAGE);
            if (!path.empty()) {
                std::filesystem::path projectPath = project->getProjectPath();
                std::filesystem::path filePath = std::filesystem::absolute(path);

                // Check if file path is within project directory
                std::error_code ec;
                auto relative = std::filesystem::relative(filePath, projectPath, ec);
                if (ec || relative.string().find("..") != std::string::npos) {
                    ImGui::OpenPopup("File Import Error");
                }else{
                    Texture texture(relative.string());
                    for (Entity& entity : entities){
                        cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture, settings.onValueChanged);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                        finishProperty = true;
                    }
                }
            }
        }

        // Error popup modal
        if (ImGui::BeginPopupModal("File Import Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be within the project directory.");
            ImGui::Separator();

            float buttonWidth = 120;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        ImGui::EndGroup();

        dragDropResourcesTexture(cpType, id, sceneProject, entities, cpType);

    }else if (type == RowPropertyType::TextureCube){
        Texture* value = nullptr;
        std::map<Entity, Texture> eValue;
        bool dif = false;
        Texture* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<Texture*>(prop.def);
            eValue[entity] = *static_cast<Texture*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Texture newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushID(("texturecube_"+id).c_str());

        auto isSingleFileCube = [](const Texture& t) -> bool {
            for (size_t i = 1; i < 6; i++){
                if (!t.getPath(i).empty())
                    return false;
            }
            return true;
        };

        if (!textureCubeSingleMode.count(id)){
            textureCubeSingleMode[id] = isSingleFileCube(newValue);
        }

        bool& singleMode = textureCubeSingleMode[id];

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Mode");
        ImGui::SameLine();
        {
            static const char* modeItems[] = {"Cubemap file", "Separate files"};
            int modeIndex = singleMode ? 0 : 1;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##cube_mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems))){
                singleMode = (modeIndex == 0);

                for (Entity& entity : entities){
                    cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    finishProperty = true;
                }
            }
        }

        if (singleMode){
            std::string cubePath = newValue.getPath(0);
            std::string cubeName = cubePath;
            if (std::filesystem::exists(cubeName)) {
                cubeName = std::filesystem::path(cubeName).filename().string();
            }
            if (cubeName.empty()) {
                cubeName = "< Not set >";
            }

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Cubemap");
            ImGui::PopStyleVar();

            ImVec2 dragMin = ImGui::GetCursorScreenPos();
            ImVec2 dragMax = dragMin;

            float thumbSize = ImGui::GetFrameHeight() * 3;
            Texture* thumbTexture = findThumbnail(cubePath);
            if (thumbTexture) {
                ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
                if (dif){
                    border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                }
                drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Image(thumbTexture->getRender()->getGLHandler(), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                    ImGui::EndTooltip();
                }
                dragMax = ImGui::GetItemRectMax();
            }

            float iconButtonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, App::ThemeColors::filenameLabel);

            ImGui::BeginChild("textureframe", ImVec2(- iconButtonWidth - ImGui::GetStyle().ItemSpacing.x, rowHeight),
                false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            float textWidth = ImGui::CalcTextSize(cubeName.c_str()).x;
            float availWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(availWidth - textWidth - 2);
            ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
            if (dif)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::Text("%s", cubeName.c_str());
            if (dif)
                ImGui::PopStyleColor();

            ImGui::EndChild();
            if (!cubePath.empty()){
                ImGui::SetItemTooltip("%s", cubePath.c_str());
            }

            ImGui::PopStyleColor();

            dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
            dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
                std::string path = editor::FileDialogs::openFileDialog(project->getProjectPath().string(), FILE_DIALOG_IMAGE);
                if (!path.empty()) {
                    std::filesystem::path projectPath = project->getProjectPath();
                    std::filesystem::path filePath = std::filesystem::absolute(path);

                    std::error_code ec;
                    auto relative = std::filesystem::relative(filePath, projectPath, ec);
                    if (ec || relative.string().find("..") != std::string::npos) {
                        ImGui::OpenPopup("File Import Error##cube");
                    }else{
                        Texture texture = newValue;
                        texture.setCubeMap(relative.string());
                        for (Entity& entity : entities){
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture, settings.onValueChanged);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            finishProperty = true;
                        }
                    }
                }
            }

            dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
            dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

            dragDropResourcesTextureCubeSingleFile(cpType, id, dragMin, dragMax, sceneProject, entities, cpType);

            if (ImGui::BeginPopupModal("File Import Error##cube", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Selected file must be within the project directory.");
                ImGui::Separator();

                float buttonWidth = 120;
                float windowWidth = ImGui::GetWindowSize().x;
                ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }else{
            // Engine cubemap indexing (sokol/OpenGL-style):
            // 0=(+X), 1=(-X), 2=(+Y), 3=(-Y), 4=(+Z), 5=(-Z)
            static const char* faceNames[6] = {"Positive X", "Negative X", "Positive Y", "Negative Y", "Positive Z", "Negative Z"};
            static const size_t faceIndexMap[6] = {0, 1, 2, 3, 4, 5};

            for (size_t uiFace = 0; uiFace < 6; uiFace++){
                const size_t faceIndex = faceIndexMap[uiFace];
                ImGui::PushID(static_cast<int>(uiFace));

                std::string facePath = newValue.getPath(faceIndex);
                std::string faceName = facePath;
                if (std::filesystem::exists(faceName)) {
                    faceName = std::filesystem::path(faceName).filename().string();
                }
                if (faceName.empty()) {
                    faceName = "< Not set >";
                }

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", faceNames[uiFace]);
                ImGui::PopStyleVar();

                // Drag/drop target rect should cover thumbnail + path + button
                ImVec2 dragMin = ImGui::GetCursorScreenPos();
                ImVec2 dragMax = dragMin;

                float thumbSize = ImGui::GetFrameHeight() * 3;
                Texture* thumbTexture = findThumbnail(facePath);
                if (thumbTexture) {
                    ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
                    if (dif){
                        border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    }
                    drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Image(thumbTexture->getRender()->getGLHandler(), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                        ImGui::EndTooltip();
                    }
                    dragMax = ImGui::GetItemRectMax();
                }

                // Path row under thumbnail
                float iconButtonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;

                ImGui::PushStyleColor(ImGuiCol_ChildBg, App::ThemeColors::filenameLabel);

                ImGui::BeginChild("textureframe", ImVec2(- iconButtonWidth - ImGui::GetStyle().ItemSpacing.x, rowHeight),
                    false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                float textWidth = ImGui::CalcTextSize(faceName.c_str()).x;
                float availWidth = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(availWidth - textWidth - 2);
                ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
                if (dif)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::Text("%s", faceName.c_str());
                if (dif)
                    ImGui::PopStyleColor();

                ImGui::EndChild();
                if (!facePath.empty()){
                    ImGui::SetItemTooltip("%s", facePath.c_str());
                }

                ImGui::PopStyleColor();

                dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
                dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

                ImGui::SameLine();

                if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
                    std::string path = editor::FileDialogs::openFileDialog(project->getProjectPath().string(), FILE_DIALOG_IMAGE);
                    if (!path.empty()) {
                        std::filesystem::path projectPath = project->getProjectPath();
                        std::filesystem::path filePath = std::filesystem::absolute(path);

                        // Check if file path is within project directory
                        std::error_code ec;
                        auto relative = std::filesystem::relative(filePath, projectPath, ec);
                        if (ec || relative.string().find("..") != std::string::npos) {
                            ImGui::OpenPopup("File Import Error##cube");
                        }else{
                            Texture texture = newValue;
                            texture.setCubePath(faceIndex, relative.string());
                            for (Entity& entity : entities){
                                cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture, settings.onValueChanged);
                                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                                finishProperty = true;
                            }
                        }
                    }
                }

                dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
                dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

                dragDropResourcesTextureCubeFace(cpType, id, faceIndex, dragMin, dragMax, sceneProject, entities, cpType);

                // Error popup modal
                if (ImGui::BeginPopupModal("File Import Error##cube", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Selected file must be within the project directory.");
                    ImGui::Separator();

                    float buttonWidth = 120;
                    float windowWidth = ImGui::GetWindowSize().x;
                    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
                    if (ImGui::Button("OK", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::PopID();
        ImGui::EndGroup();

    }else if (type == RowPropertyType::Material){
        Material* value = nullptr;
        std::map<Entity, Material> eValue;
        bool dif = false;
        Material* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<Material*>(prop.def);
            eValue[entity] = *static_cast<Material*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }
        Material newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Material>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        Texture texRender = getMaterialPreview(newValue, id);
        float thumbSize = ImGui::GetFrameHeight() * 3;
        ImGui::Image(texRender.getRender()->getGLHandler(), ImVec2(thumbSize, thumbSize), ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            materialButtonGroups[id] = !materialButtonGroups[id];
        }

        unsigned int materialSubmeshIndex = 0;
        bool hasMaterialSubmeshIndex = false;
        auto submeshPos = id.find('[');
        auto submeshEnd = id.find(']');
        if (submeshPos != std::string::npos && submeshEnd != std::string::npos) {
            try {
                materialSubmeshIndex = std::stoul(id.substr(submeshPos + 1, submeshEnd - submeshPos - 1));
                hasMaterialSubmeshIndex = true;
            } catch (const std::exception&) {
                materialSubmeshIndex = 0;
            }
        }

        bool hasLinkedMaterial = false;
        if (sceneProject && hasMaterialSubmeshIndex) {
            for (Entity& entity : entities) {
                if (project->isMaterialFileLinked(sceneProject->id, entity, materialSubmeshIndex)) {
                    hasLinkedMaterial = true;
                    break;
                }
            }
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, App::ThemeColors::filenameLabel);

        ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
        ImVec2 unlinkButtonSize = arrowButtonSize;
        float frameWidth = -arrowButtonSize.x - unlinkButtonSize.x - ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::BeginChild("textureframe", ImVec2(frameWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2), 
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        std::string matName = newValue.name;
        if (!matName.empty()) {
            std::filesystem::path matPath = std::filesystem::path(matName);
            if (!matPath.is_absolute()) {
                matPath = project->getProjectPath() / matPath;
            }
            if (std::filesystem::exists(matPath)) {
                matName = std::filesystem::path(newValue.name).filename().string();
            }
        }
        if (matName.empty()) {
            matName = "< Not defined >";
        }

        float textWidth = ImGui::CalcTextSize(matName.c_str()).x;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(availWidth - textWidth - 2);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", matName.c_str());
        if (dif)
            ImGui::PopStyleColor();

        ImGui::EndChild();
        if (!newValue.name.empty()){
            ImGui::SetItemTooltip("%s", newValue.name.c_str());
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::ArrowButton("##toggle_mesh", materialButtonGroups[id] ? ImGuiDir_Up : ImGuiDir_Down)){
            materialButtonGroups[id] = !materialButtonGroups[id];
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!hasLinkedMaterial);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));

        float currentScale = ImGui::GetFont()->Scale;
        ImGui::SetWindowFontScale(currentScale * 0.75f);

        if (ImGui::Button(ICON_FA_LINK_SLASH "##unlink_material", unlinkButtonSize)){
            if (sceneProject && hasMaterialSubmeshIndex) {
                auto* unlinkCmd = new UnlinkMaterialCmd(project, sceneProject->id, cpType, id, materialSubmeshIndex, entities, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(unlinkCmd);
                finishProperty = true;
            }
        }

        ImGui::SetWindowFontScale(currentScale);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(hasLinkedMaterial ? "Unlink material file" : "Material is not linked to a file");
        }

        ImGui::EndGroup();

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            std::string materialStr = YAML::Dump(Stream::encodeMaterial(newValue));

            std::vector<char> materialPayload;
            MaterialPayload header{0x4D54524C, NULL_PROJECT_SCENE, NULL_ENTITY, 0};

            if (cpType == ComponentType::MeshComponent && !entities.empty()) {
                header.sceneId = sceneProject->id;
                header.entity = entities[0];

                auto p = id.find('[');
                auto e = id.find(']');
                if (p != std::string::npos && e != std::string::npos) {
                    try {
                        header.submeshIndex = std::stoul(id.substr(p + 1, e - p - 1));
                    } catch (const std::exception&) {
                        header.submeshIndex = 0;
                    }
                }
            }

            materialPayload.resize(sizeof(MaterialPayload) + materialStr.size());
            std::memcpy(materialPayload.data(), &header, sizeof(MaterialPayload));
            std::memcpy(materialPayload.data() + sizeof(MaterialPayload), materialStr.data(), materialStr.size());

            ImGui::SetDragDropPayload("material", materialPayload.data(), materialPayload.size());
            ImGui::Text("Moving material");
            float imageDragSize = 32;
            float availWidth = ImGui::GetCurrentWindow()->Size.x;
            float xPos = (availWidth - imageDragSize) * 0.5f;
            ImGui::SetCursorPosX(xPos);
            ImGui::Image(texRender.getRender()->getGLHandler(), ImVec2(imageDragSize, imageDragSize), ImVec2(0, 1), ImVec2(1, 0));
            ImGui::EndDragDropSource();
        }

        // Material file drop preview state (must be outside drag-drop blocks for restore access)
        static std::string cachedMatDropPath;
        static Material cachedMatDropMaterial;
        static std::map<Entity, Material> matDropOriginals;
        static bool matDropPreviewing = false;
        static std::string matDropPropertyId;

        auto restoreMatDropPreview = [&]() {
            if (matDropPreviewing) {
                for (auto& [ent, origMat] : matDropOriginals) {
                    PropertyData prop = Catalog::getProperty(sceneProject->scene, ent, cpType, matDropPropertyId);
                    if (prop.ref) {
                        Material* matRef = static_cast<Material*>(prop.ref);
                        *matRef = origMat;
                        MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(ent);
                        if (mesh) {
                            auto p = matDropPropertyId.find('[');
                            auto e = matDropPropertyId.find(']');
                            if (p != std::string::npos && e != std::string::npos) {
                                unsigned int sIdx = std::stoul(matDropPropertyId.substr(p + 1, e - p - 1));
                                if (sIdx < mesh->numSubmeshes) {
                                    mesh->submeshes[sIdx].needUpdateTexture = true;
                                }
                            }
                        }
                    }
                }
                matDropPreviewing = false;
                matDropOriginals.clear();
                cachedMatDropPath.clear();
            }
        };

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                std::vector<std::string> receivedStrings = Util::getStringsFromPayload(payload);

                if (!receivedStrings.empty()) {
                    std::error_code ec;
                    std::filesystem::path relativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath(), ec);

                    if (ec) {
                        if (payload->IsDelivery()) {
                            restoreMatDropPreview();
                            ImGui::OpenPopup("File Import Error##material");
                        }
                    } else {
                        std::string droppedRelativePath = relativePath.lexically_normal().generic_string();

                        if (Util::isMaterialFile(droppedRelativePath)) {
                            try {
                                if (cachedMatDropPath != droppedRelativePath) {
                                    cachedMatDropPath = droppedRelativePath;
                                    YAML::Node materialNode = YAML::LoadFile((project->getProjectPath() / relativePath).string());
                                    cachedMatDropMaterial = Stream::decodeMaterial(materialNode);
                                    cachedMatDropMaterial.name = droppedRelativePath;
                                }

                                if (!payload->IsDelivery()) {
                                    // Preview: save originals and apply
                                    if (!matDropPreviewing) {
                                        matDropOriginals.clear();
                                        matDropPropertyId = id;
                                        for (Entity& entity : entities) {
                                            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
                                            matDropOriginals[entity] = *static_cast<Material*>(prop.ref);
                                        }
                                        matDropPreviewing = true;
                                    }
                                    for (Entity& entity : entities) {
                                        PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
                                        Material* matRef = static_cast<Material*>(prop.ref);
                                        if (*matRef != cachedMatDropMaterial) {
                                            *matRef = cachedMatDropMaterial;
                                            MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
                                            if (mesh) {
                                                auto pos = id.find('[');
                                                auto end = id.find(']');
                                                if (pos != std::string::npos && end != std::string::npos) {
                                                    unsigned int sIdx = std::stoul(id.substr(pos + 1, end - pos - 1));
                                                    if (sIdx < mesh->numSubmeshes) {
                                                        mesh->submeshes[sIdx].needUpdateTexture = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    // Delivery: restore originals, then issue commands
                                    restoreMatDropPreview();

                                    for (Entity& entity : entities) {
                                        auto pos = id.find('[');
                                        auto end = id.find(']');
                                        if (pos != std::string::npos && end != std::string::npos) {
                                            unsigned int sIdx = std::stoul(id.substr(pos + 1, end - pos - 1));
                                            cmd = new LinkMaterialCmd(project, sceneProject->id, entity, cpType, id, sIdx, cachedMatDropMaterial, settings.onValueChanged);
                                        } else {
                                            cmd = new PropertyCmd<Material>(project, sceneProject->id, entity, cpType, id, cachedMatDropMaterial, settings.onValueChanged);
                                        }
                                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                                        finishProperty = true;
                                    }

                                    cachedMatDropPath.clear();
                                    ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                                }
                            } catch (const std::exception& e) {
                                Out::error("Error loading material file '%s': %s", droppedRelativePath.c_str(), e.what());
                            }
                        } else if (Util::isImageFile(droppedRelativePath)) {
                            std::string baseTexId = id + ".baseColorTexture";

                            if (!payload->IsDelivery()) {
                                // Preview: save originals and apply texture
                                if (!matDropPreviewing) {
                                    matDropOriginals.clear();
                                    matDropPropertyId = id;
                                    for (Entity& entity : entities) {
                                        PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
                                        matDropOriginals[entity] = *static_cast<Material*>(prop.ref);
                                    }
                                    matDropPreviewing = true;
                                }
                                for (Entity& entity : entities) {
                                    PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
                                    Material* matRef = static_cast<Material*>(prop.ref);
                                    if (matRef->baseColorTexture != Texture(droppedRelativePath)) {
                                        matRef->baseColorTexture = Texture(droppedRelativePath);
                                        MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
                                        if (mesh) {
                                            auto pos = id.find('[');
                                            auto end = id.find(']');
                                            if (pos != std::string::npos && end != std::string::npos) {
                                                unsigned int sIdx = std::stoul(id.substr(pos + 1, end - pos - 1));
                                                if (sIdx < mesh->numSubmeshes) {
                                                    mesh->submeshes[sIdx].needUpdateTexture = true;
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                // Delivery: restore originals, then issue texture command
                                restoreMatDropPreview();

                                Texture texture(droppedRelativePath);
                                for (Entity& entity : entities) {
                                    cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, baseTexId, texture, settings.onValueChanged);
                                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                                    finishProperty = true;
                                }

                                cachedMatDropPath.clear();
                                ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        } else if (matDropPreviewing && matDropPropertyId == id) {
            // Drag ended without delivery — restore preview
            restoreMatDropPreview();
        }

        if (ImGui::BeginPopupModal("File Import Error##material", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be within the project directory.");
            ImGui::Separator();

            float buttonWidth = 120;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        result = materialButtonGroups[id];

    }else if (type == RowPropertyType::LocalEntity){
        Entity* value = nullptr;
        std::map<Entity, Entity> eValue;
        bool different = false;
        unsigned int* defVal = nullptr;

        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defVal = static_cast<unsigned int*>(prop.def);
            eValue[entity] = *static_cast<unsigned int*>(prop.ref);
            if (value && *value != eValue[entity]){
                different = true;
            }
            value = &eValue[entity];
        }

        Entity newValue = value ? *value : NULL_ENTITY;
        bool defChanged = (defVal && newValue != *defVal);

        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, *defVal, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        std::string entityName = "None";
        if (newValue != NULL_ENTITY && sceneProject->scene->isEntityCreated(newValue)) {
            entityName = sceneProject->scene->getEntityName(newValue);
            if (entityName.empty()) {
                entityName = "Entity " + std::to_string(newValue);
            }
        }

        bool invalidSelection = false;

        if (different || invalidSelection) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (different) {
                entityName = "---";
            }
        }

        std::string buttonLabel = ICON_FA_CIRCLE_DOT " " + entityName + "##local_entity_" + id;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_XMARK).x;
        ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);

        std::string pickerPopupId = "##ep_" + id;

        if (ImGui::Button(buttonLabel.c_str(), inputSize)) {
            if (newValue != NULL_ENTITY && sceneProject->scene->isEntityCreated(newValue)) {
                project->clearSelectedEntities(sceneProject->id);
                project->addSelectedEntity(sceneProject->id, newValue);
            } else if (!different) {
                ImGui::OpenPopup(pickerPopupId.c_str());
                memset(entityPickerSearchBuffer, 0, sizeof(entityPickerSearchBuffer));
            }
        }

        if (ImGui::IsItemHovered() && !different) {
            if (newValue != NULL_ENTITY) {
                ImGui::SetTooltip("Entity: %s (ID: %u)", entityName.c_str(), static_cast<unsigned int>(newValue));
            } else {
                ImGui::SetTooltip("Click to select entity");
            }
        }

        // Entity picker popup
        auto pickerResult = drawEntityPickerPopup(pickerPopupId, settings.entityFilter, sceneProject, false, newValue, 0);
        if (pickerResult.chosen) {
            for (Entity& entity : entities) {
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, pickerResult.entity, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                const EntityPayload* entityPayload = static_cast<const EntityPayload*>(payload->Data);
                Entity droppedEntity = entityPayload->entity;
                bool valid = sceneProject->scene->isEntityCreated(droppedEntity);

                if (!valid && ImGui::IsItemHovered()){
                    ImGui::SetTooltip("Invalid entity");
                }

                if (payload->IsDelivery() && valid) {
                    for (Entity& entity : entities) {
                        cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, droppedEntity, settings.onValueChanged);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    finishProperty = true;
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("bundle", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                Entity droppedEntity = NULL_ENTITY;
                bool valid = false;
                try {
                    std::string yamlString(static_cast<const char*>(payload->Data), payload->DataSize);
                    YAML::Node bundleNode = YAML::Load(yamlString);
                    if (bundleNode["members"] && bundleNode["members"].IsSequence() && bundleNode["members"].size() > 0) {
                        droppedEntity = bundleNode["members"][0]["entity"].as<Entity>();
                        valid = sceneProject->scene->isEntityCreated(droppedEntity);
                    }
                } catch (...) {}

                if (!valid && ImGui::IsItemHovered()){
                    ImGui::SetTooltip("Invalid entity");
                }

                if (payload->IsDelivery() && valid) {
                    for (Entity& entity : entities) {
                        cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, droppedEntity, settings.onValueChanged);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    finishProperty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (different || invalidSelection) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(newValue == NULL_ENTITY && !different);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button((ICON_FA_XMARK "##clear_local_entity_" + id).c_str())) {
            for (Entity& entity : entities) {
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, NULL_ENTITY, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear entity reference");
        }

        ImGui::EndGroup();

    }else if (type == RowPropertyType::ExternalEntity){
        Entity* value = nullptr;
        std::map<Entity, Entity> eValue;
        std::map<Entity, uint32_t> eSceneId; // sceneId per selected entity
        bool different = false;
        unsigned int* defVal = nullptr;

        // Parse script index and property name from id (format: "scripts[N].propName")
        size_t scriptIdx = 0;
        std::string propName;
        {
            size_t bracket = id.find('[');
            size_t closeBracket = id.find(']');
            size_t dot = id.find('.', closeBracket != std::string::npos ? closeBracket : 0);
            if (bracket != std::string::npos && closeBracket != std::string::npos && dot != std::string::npos) {
                scriptIdx = std::stoul(id.substr(bracket + 1, closeBracket - bracket - 1));
                propName = id.substr(dot + 1);
            }
        }

        for (Entity& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, id);
            defVal = static_cast<unsigned int*>(prop.def);
            eValue[entity] = *static_cast<unsigned int*>(prop.ref);

            // Read sceneId from ScriptProperty
            uint32_t sid = 0;
            ScriptComponent* sc = sceneProject->scene->findComponent<ScriptComponent>(entity);
            if (sc && scriptIdx < sc->scripts.size()) {
                for (auto& sp : sc->scripts[scriptIdx].properties) {
                    if (sp.name == propName) {
                        sid = std::get<EntityReference>(sp.value).sceneId;
                        break;
                    }
                }
            }
            eSceneId[entity] = sid;

            if (value && (*value != eValue[entity] || eSceneId[entities[0]] != sid)){
                different = true;
            }
            value = &eValue[entity];
        }

        Entity newValue = value ? *value : NULL_ENTITY;
        uint32_t currentSceneId = !entities.empty() ? eSceneId[entities[0]] : 0;
        bool defChanged = (defVal && newValue != *defVal);

        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                auto multiCmd = new MultiPropertyCmd();
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, *defVal, settings.onValueChanged);
                multiCmd->addPropertyCmd<uint32_t>(project, sceneProject->id, entity, cpType, id + ".sceneId", (uint32_t)0, nullptr);
                cmd = multiCmd;
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        // Resolve entity name, potentially from a different scene
        std::string entityName = "None";
        std::string sceneSuffix = "";
        Scene* resolvedScene = nullptr;

        if (newValue != NULL_ENTITY) {
            if (currentSceneId != 0) {
                SceneProject* targetSceneProject = project->getScene(currentSceneId);
                if (targetSceneProject && targetSceneProject->scene && targetSceneProject->scene->isEntityCreated(newValue)) {
                    resolvedScene = targetSceneProject->scene;
                    entityName = resolvedScene->getEntityName(newValue);
                    if (entityName.empty()) {
                        entityName = "Entity " + std::to_string(newValue);
                    }
                    sceneSuffix = " (" + targetSceneProject->name + ")";
                }
            } else if (sceneProject->scene->isEntityCreated(newValue)) {
                resolvedScene = sceneProject->scene;
                entityName = resolvedScene->getEntityName(newValue);
                if (entityName.empty()) {
                    entityName = "Entity " + std::to_string(newValue);
                }
            }
        }

        bool invalidSelection = (newValue != NULL_ENTITY && !resolvedScene);

        if (different || invalidSelection) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (different) {
                entityName = "---";
                sceneSuffix = "";
            } else if (invalidSelection) {
                entityName = "Entity " + std::to_string(newValue);
                SceneProject* storedScene = (currentSceneId != 0) ? project->getScene(currentSceneId) : sceneProject;
                sceneSuffix = storedScene ? " (" + storedScene->name + ")" : "";
            }
        }

        std::string displayLabel = entityName + sceneSuffix;
        std::string buttonLabel = ICON_FA_CIRCLE_DOT " " + displayLabel + "##ext_entity_" + id;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_XMARK).x;
        ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);

        // Tint button color to differentiate ExternalEntity from LocalEntity
        ImGui::PushStyleColor(ImGuiCol_Button, App::ThemeColors::ExtEntityButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, App::ThemeColors::ExtEntityButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, App::ThemeColors::ExtEntityButtonActive);

        std::string extPickerPopupId = "##ep_ext_" + id;

        if (ImGui::Button(buttonLabel.c_str(), inputSize)) {
            if (resolvedScene && newValue != NULL_ENTITY) {
                uint32_t selectSceneId = (currentSceneId != 0) ? currentSceneId : sceneProject->id;
                project->clearSelectedEntities(selectSceneId);
                project->addSelectedEntity(selectSceneId, newValue);
            } else if (!different) {
                ImGui::OpenPopup(extPickerPopupId.c_str());
                memset(entityPickerSearchBuffer, 0, sizeof(entityPickerSearchBuffer));
            }
        }
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered() && !different) {
            if (newValue != NULL_ENTITY) {
                std::string tipSceneName = sceneProject->name;
                uint32_t tipSceneId = sceneProject->id;
                if (currentSceneId != 0) {
                    SceneProject* tipScene = project->getScene(currentSceneId);
                    if (tipScene) {
                        tipSceneName = tipScene->name;
                        tipSceneId = currentSceneId;
                    }
                }
                ImGui::SetTooltip("Entity: %s (ID: %u)\nScene: %s (ID: %u)",
                    entityName.c_str(), static_cast<unsigned int>(newValue),
                    tipSceneName.c_str(), static_cast<unsigned int>(tipSceneId));
            } else {
                ImGui::SetTooltip("Click to select entity");
            }
        }

        // Entity picker popup
        auto extPickerResult = drawEntityPickerPopup(extPickerPopupId, settings.entityFilter, sceneProject, true, newValue, currentSceneId);
        if (extPickerResult.chosen) {
            for (Entity& entity : entities) {
                auto multiCmd = new MultiPropertyCmd();
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, extPickerResult.entity, settings.onValueChanged);
                multiCmd->addPropertyCmd<uint32_t>(project, sceneProject->id, entity, cpType, id + ".sceneId", extPickerResult.sceneId, nullptr);
                cmd = multiCmd;
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                const EntityPayload* entityPayload = static_cast<const EntityPayload*>(payload->Data);
                Entity droppedEntity = entityPayload->entity;
                uint32_t droppedSceneId = entityPayload->entitySceneId;

                bool valid = false;
                SceneProject* sourceScene = project->getScene(droppedSceneId);
                if (sourceScene && sourceScene->scene) {
                    valid = sourceScene->scene->isEntityCreated(droppedEntity);
                }

                // Determine sceneId to store: 0 if same scene, source scene ID if cross-scene
                uint32_t storedSceneId = (droppedSceneId != sceneProject->id) ? droppedSceneId : 0;

                if (!valid && ImGui::IsItemHovered()){
                    ImGui::SetTooltip("Invalid entity");
                }

                if (payload->IsDelivery() && valid) {
                    for (Entity& entity : entities) {
                        auto multiCmd = new MultiPropertyCmd();
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, droppedEntity, settings.onValueChanged);
                        multiCmd->addPropertyCmd<uint32_t>(project, sceneProject->id, entity, cpType, id + ".sceneId", storedSceneId, nullptr);
                        cmd = multiCmd;
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    finishProperty = true;
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("bundle", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                Entity droppedEntity = NULL_ENTITY;
                bool valid = false;
                try {
                    std::string yamlString(static_cast<const char*>(payload->Data), payload->DataSize);
                    YAML::Node bundleNode = YAML::Load(yamlString);
                    if (bundleNode["members"] && bundleNode["members"].IsSequence() && bundleNode["members"].size() > 0) {
                        droppedEntity = bundleNode["members"][0]["entity"].as<Entity>();
                        valid = sceneProject->scene->isEntityCreated(droppedEntity);
                    }
                } catch (...) {}

                if (!valid && ImGui::IsItemHovered()){
                    ImGui::SetTooltip("Invalid entity");
                }

                if (payload->IsDelivery() && valid) {
                    for (Entity& entity : entities) {
                        auto multiCmd = new MultiPropertyCmd();
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, droppedEntity, settings.onValueChanged);
                        multiCmd->addPropertyCmd<uint32_t>(project, sceneProject->id, entity, cpType, id + ".sceneId", (uint32_t)0, nullptr);
                        cmd = multiCmd;
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    finishProperty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (different || invalidSelection) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(newValue == NULL_ENTITY && !different);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button((ICON_FA_XMARK "##clear_ext_entity_" + id).c_str())) {
            for (Entity& entity : entities) {
                auto multiCmd = new MultiPropertyCmd();
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, (unsigned int)NULL_ENTITY, settings.onValueChanged);
                multiCmd->addPropertyCmd<uint32_t>(project, sceneProject->id, entity, cpType, id + ".sceneId", (uint32_t)0, nullptr);
                cmd = multiCmd;
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear entity reference");
        }

        ImGui::EndGroup();

    }

    if (ImGui::IsItemDeactivatedAfterEdit() || finishProperty) {
        if (cmd){
            cmd->setNoMerge();
            cmd = nullptr;
        }
        finishProperty = false;
    }

    return result;
}

bool editor::Properties::propertyRowWithAutoButton(RowPropertyType propType, ComponentType cpType, std::string id, std::string label, std::string autoId, std::string autoLabel, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings) {
    auto onValueChanged = settings.onValueChanged;
    settings.onValueChanged = [this, sceneProject, entities, cpType, autoId, onValueChanged]() {
        if (onValueChanged) {
            onValueChanged();
        }
        for (auto& entity : entities){
            PropertyData autoProp = Catalog::getProperty(sceneProject->scene, entity, cpType, autoId);
            bool* autoVal = static_cast<bool*>(autoProp.ref);
            if (autoVal && *autoVal) {
                editor::MultiPropertyCmd* cmd = new editor::MultiPropertyCmd();
                cmd->addPropertyCmd<bool>(project, sceneProject->id, entity, cpType, autoId, false);
                cmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
    };

    std::string helpText = settings.help;
    settings.help = "";

    bool rowChanged = propertyRow(propType, cpType, id, label, sceneProject, entities, settings);

    ImGui::SameLine();
    bool allAuto = true;
    bool anyAuto = false;
    for (auto& entity : entities){
        PropertyData autoProp = Catalog::getProperty(sceneProject->scene, entity, cpType, autoId);
        bool val = *static_cast<bool*>(autoProp.ref);
        if (val) anyAuto = true;
        else allAuto = false;
    }

    if (allAuto) {
        ImGui::PushStyleColor(ImGuiCol_Button, App::ThemeColors::ButtonActivated);
    } else if (anyAuto) {
        ImGui::PushStyleColor(ImGuiCol_Button, App::ThemeColors::ButtonActivated);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
    }

    float btnPaddingY = std::max(0.0f, (ImGui::GetFrameHeight() - ImGui::GetFontSize()) / 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 4.0f, btnPaddingY));

    std::string autoBtnId = ICON_FA_WAND_MAGIC_SPARKLES "##auto_" + id;
    if (ImGui::Button(autoBtnId.c_str())) {
        bool targetValue = !allAuto;
        editor::MultiPropertyCmd* cmdAuto = new editor::MultiPropertyCmd();
        for (auto& entity : entities) {
            cmdAuto->addPropertyCmd<bool>(project, sceneProject->id, entity, cpType, autoId, targetValue);
        }
        cmdAuto->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmdAuto);
    }

    ImGui::PopStyleVar();

    if (anyAuto) {
        ImGui::PopStyleColor(allAuto ? 1 : 2);
    } else {
        ImGui::PopStyleColor(1);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", autoLabel.c_str());
    }

    if (!helpText.empty()) {
        ImGui::SameLine(); helpMarker(helpText);
    }

    return rowChanged;
}

void editor::Properties::drawTransform(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    // Add this code to calculate appropriate step size based on selected scene
    RowSettings settingsPos;
    if (sceneProject && sceneProject->sceneRender) {
        Camera* camera = sceneProject->sceneRender->getCamera();
        if (sceneProject->sceneType == SceneType::SCENE_3D) {
            // For 3D scenes, scale step based on distance from target
            float distanceFromTarget = camera->getDistanceFromTarget();
            settingsPos.stepSize = std::max(0.01f, distanceFromTarget / 200.0f);
        } else {
            // For 2D scenes, use the zoom level
            SceneRender2D* sceneRender2D = static_cast<SceneRender2D*>(sceneProject->sceneRender);
            float zoom = sceneRender2D->getZoom();
            settingsPos.stepSize = std::max(0.01f, zoom * 1.0f);
        }
    }

    beginTable(cpType, getLabelSize("billboard"));

    propertyRow(RowPropertyType::Vector3, cpType, "position", "Position", sceneProject, entities, settingsPos);
    propertyRow(RowPropertyType::Quat, cpType, "rotation", "Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "scale", "Scale", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "visible", "Visible", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "billboard", "Billboard", sceneProject, entities);

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_GEAR)){
        ImGui::OpenPopup("menusettings_billboard");
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(19 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_billboard")){
        ImGui::Text("Billboard settings");
        ImGui::Separator();

        RowSettings settingsRotation;
        settingsRotation.secondColSize = 12 * ImGui::GetFontSize();

        beginTable(cpType, getLabelSize("cylindrical"));
        propertyRow(RowPropertyType::Bool, cpType, "fakeBillboard", "Fake", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "cylindricalBillboard", "Cylindrical", sceneProject, entities);
        propertyRow(RowPropertyType::Quat, cpType, "billboardRotation", "Rotation", sceneProject, entities, settingsRotation);
        endTable();

        ImGui::EndPopup();
    }

    endTable();
}

void editor::Properties::drawMeshComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("receive shadows"));

    // Static variables for shape parameters
    static ShapeParameters shapeParams;

    // Add New Geometry property row
    propertyHeader("Geometry");
    if (ImGui::Button("Create Shape")){
        ImGui::OpenPopup("menusettings_shape_geometry");

        updateShapePreview(shapeParams);
    }

    // Geometry creation popup
    ImGui::SetNextWindowSizeConstraints(ImVec2(17 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_shape_geometry")){
        ImGui::Text("Create Shape");
        ImGui::Separator();

        const char* geometryTypes[] = { "Plane", "Box", "Sphere", "Cylinder", "Capsule", "Torus" };

        beginTable(cpType, getLabelSize("Geometry Type"), "geometry_popup");

        // Geometry type selection
        propertyHeader("Geometry Type");

        float secondColSize = 8 * ImGui::GetFontSize();
        bool updatedPreview = false;

        Texture texRender = shapePreviewRender.getTexture();
        ImGui::Image(texRender.getRender()->getGLHandler(), ImVec2(secondColSize, secondColSize), ImVec2(0, 1), ImVec2(1, 0));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##geometry_type", &shapeParams.geometryType, geometryTypes, IM_ARRAYSIZE(geometryTypes))) {
            updatedPreview = true;
        }

        // Show parameters based on selected geometry type
        switch (shapeParams.geometryType) {
            case 0: // Plane
                propertyHeader("Width", secondColSize);
                if (ImGui::DragFloat("##plane_width", &shapeParams.planeWidth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Depth", secondColSize);
                if (ImGui::DragFloat("##plane_depth", &shapeParams.planeDepth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Tiles", secondColSize);
                if (ImGui::DragInt("##plane_tiles", (int*)&shapeParams.planeTiles, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 1: // Box
                propertyHeader("Width", secondColSize);
                if (ImGui::DragFloat("##box_width", &shapeParams.boxWidth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Height", secondColSize);
                if (ImGui::DragFloat("##box_height", &shapeParams.boxHeight, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Depth", secondColSize);
                if (ImGui::DragFloat("##box_depth", &shapeParams.boxDepth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Tiles", secondColSize);
                if (ImGui::DragInt("##box_tiles", (int*)&shapeParams.boxTiles, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 2: // Sphere
                propertyHeader("Radius", secondColSize);
                if (ImGui::DragFloat("##sphere_radius", &shapeParams.sphereRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Slices", secondColSize);
                if (ImGui::DragInt("##sphere_slices", (int*)&shapeParams.sphereSlices, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Stacks", secondColSize);
                if (ImGui::DragInt("##sphere_stacks", (int*)&shapeParams.sphereStacks, 1, 3, 100)) {
                    updatedPreview = true;
                }
                break;

            case 3: // Cylinder
                propertyHeader("Base Radius", secondColSize);
                if (ImGui::DragFloat("##cylinder_base_radius", &shapeParams.cylinderBaseRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Top Radius", secondColSize);
                if (ImGui::DragFloat("##cylinder_top_radius", &shapeParams.cylinderTopRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Height", secondColSize);
                if (ImGui::DragFloat("##cylinder_height", &shapeParams.cylinderHeight, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Slices", secondColSize);
                if (ImGui::DragInt("##cylinder_slices", (int*)&shapeParams.cylinderSlices, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Stacks", secondColSize);
                if (ImGui::DragInt("##cylinder_stacks", (int*)&shapeParams.cylinderStacks, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 4: // Capsule
                propertyHeader("Base Radius", secondColSize);
                if (ImGui::DragFloat("##capsule_base_radius", &shapeParams.capsuleBaseRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Top Radius", secondColSize);
                if (ImGui::DragFloat("##capsule_top_radius", &shapeParams.capsuleTopRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Height", secondColSize);
                if (ImGui::DragFloat("##capsule_height", &shapeParams.capsuleHeight, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Slices", secondColSize);
                if (ImGui::DragInt("##capsule_slices", (int*)&shapeParams.capsuleSlices, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Stacks", secondColSize);
                if (ImGui::DragInt("##capsule_stacks", (int*)&shapeParams.capsuleStacks, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 5: // Torus
                propertyHeader("Radius", secondColSize);
                if (ImGui::DragFloat("##torus_radius", &shapeParams.torusRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Ring Radius", secondColSize);
                if (ImGui::DragFloat("##torus_ring_radius", &shapeParams.torusRingRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Sides", secondColSize);
                if (ImGui::DragInt("##torus_sides", (int*)&shapeParams.torusSides, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Rings", secondColSize);
                if (ImGui::DragInt("##torus_rings", (int*)&shapeParams.torusRings, 1, 3, 100)) {
                    updatedPreview = true;
                }
                break;
        }

        if (updatedPreview){
            updateShapePreview(shapeParams);
        }

        endTable();

        ImGui::Separator();

        // Create geometry button
        if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            for (Entity& entity : entities) {
                std::shared_ptr<doriax::MeshSystem> meshSys = sceneProject->scene->getSystem<MeshSystem>();
                MeshComponent meshComp = sceneProject->scene->getComponent<MeshComponent>(entity);

                updateMeshShape(meshComp, meshSys.get(), shapeParams);

                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new MeshChangeCmd(project, sceneProject->id, entities[0], meshComp));
            }

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    propertyRow(RowPropertyType::Bool, cpType, "castShadows", "Cast Shadows", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "receiveShadows", "Receive Shadows", sceneProject, entities);

    RowSettings transparencySettings;
    transparencySettings.help = "Just for render ordering";
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "transparent", "Transparent", "autoTransparency", "Auto Transparency", sceneProject, entities, transparencySettings);

    endTable();

    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entities[0]).numSubmeshes;
    for (Entity& entity : entities){
        numSubmeshes = std::min(numSubmeshes, sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes);
    }
    bool hasGeneratedSubmeshes = false;
    for (const Entity& entity : entities) {
        const MeshComponent& checkMesh = sceneProject->scene->getComponent<MeshComponent>(entity);
        for (unsigned int gs = 0; gs < checkMesh.numSubmeshes; gs++) {
            if (checkMesh.submeshes[gs].generated) {
                hasGeneratedSubmeshes = true;
                break;
            }
        }
        if (hasGeneratedSubmeshes) break;
    }
    if (!hasGeneratedSubmeshes && ImGui::Button(ICON_FA_PLUS " Add Submesh", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        for (Entity& entity : entities) {
            MeshComponent meshComp = sceneProject->scene->getComponent<MeshComponent>(entity);
            const unsigned int newSubmeshIndex = meshComp.numSubmeshes;
            meshComp.numSubmeshes++;
            meshComp.submeshes[newSubmeshIndex] = Submesh();
            meshComp.submeshes[newSubmeshIndex].needUpdateTexture = true;

            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(
                new MeshChangeCmd(project, sceneProject->id, entity, meshComp));
        }
    }

    for (int s = 0; s < numSubmeshes; s++){
        ImGui::PushID(s);
        bool submeshGenerated = false;
        for (const Entity& entity : entities) {
            const MeshComponent& checkMesh = sceneProject->scene->getComponent<MeshComponent>(entity);
            if ((unsigned int)s < checkMesh.numSubmeshes && checkMesh.submeshes[s].generated) {
                submeshGenerated = true;
                break;
            }
        }
        std::string submeshLabel = submeshGenerated ? ("Submesh " + std::to_string(s) + " (Internal)") : ("Submesh " + std::to_string(s));
        ImGui::SeparatorText(submeshLabel.c_str());

        ImVec2 separatorMin = ImGui::GetItemRectMin();
        ImVec2 separatorMax = ImGui::GetItemRectMax();
        ImVec2 cursorAfterSeparator = ImGui::GetCursorPos();

        if (!submeshGenerated) {
            float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
            float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
            ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);

            float buttonX = separatorMax.x - deleteButtonSize.x;
            float buttonY = separatorMin.y + (separatorMax.y - separatorMin.y - ImGui::GetFrameHeight()) * 0.5f;

            ImDrawList* separatorDrawList = ImGui::GetWindowDrawList();
            float separatorGap = ImGui::GetStyle().ItemSpacing.x;
            separatorDrawList->AddRectFilled(
                ImVec2(buttonX - separatorGap, separatorMin.y),
                separatorMax,
                ImGui::GetColorU32(ImGuiCol_WindowBg));

            ImGui::SetCursorScreenPos(ImVec2(buttonX, buttonY));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
            if (ImGui::Button((std::string(ICON_FA_TRASH_CAN) + "##delete_submesh_" + std::to_string(s)).c_str(), deleteButtonSize)) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                ImGui::SetCursorPos(cursorAfterSeparator);

                for (Entity& entity : entities) {
                    MeshComponent meshComp = sceneProject->scene->getComponent<MeshComponent>(entity);
                    if ((unsigned int)s >= meshComp.numSubmeshes) {
                        continue;
                    }

                    for (unsigned int j = (unsigned int)s; j + 1 < meshComp.numSubmeshes; j++) {
                        meshComp.submeshes[j] = meshComp.submeshes[j + 1];
                    }

                    if (meshComp.numSubmeshes > 0) {
                        meshComp.numSubmeshes--;
                        meshComp.submeshes[meshComp.numSubmeshes] = Submesh();
                    }

                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(
                        new MeshChangeCmd(project, sceneProject->id, entity, meshComp));
                }

                ImGui::PopID();
                return;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove submesh");
            }
        }
        ImGui::SetCursorPos(cursorAfterSeparator);
        if (submeshGenerated) ImGui::BeginDisabled(true);

        beginTable(cpType, getLabelSize("Material"), "submeshes_material");

        bool materialOpened = false;
        if (propertyRow(RowPropertyType::Material, cpType, "submeshes["+std::to_string(s)+"].material", "Material", sceneProject, entities)){
            materialOpened = true;
        }
        endTable();

        if (materialOpened){
            RowSettings settingsFactor;
            settingsFactor.stepSize = 0.01f;
            settingsFactor.secondColSize = 4 * ImGui::GetFontSize();
            settingsFactor.child = true;

            RowSettings settingsMaterial;
            settingsMaterial.child = true;
            settingsMaterial.onValueChanged = [this, sceneProject, entities, s]() {
                for (const Entity& entity : entities) {
                    MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
                    if (!mesh || s >= mesh->numSubmeshes) {
                        continue;
                    }

                    Material& material = mesh->submeshes[s].material;
                    if (material.name.empty()) {
                        continue;
                    }

                    std::string relativePath = std::filesystem::path(material.name).lexically_normal().generic_string();
                    markMaterialDirty(sceneProject->id, entity, s, relativePath);
                }
            };

            settingsFactor.onValueChanged = settingsMaterial.onValueChanged;

            beginTable(cpType, getLabelSize("Met. Roug. Texture"), "submeshes_material_options");
            propertyRow(RowPropertyType::Color4L, cpType, "submeshes["+std::to_string(s)+"].material.baseColorFactor", "Base Color", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.baseColorTexture", "Base Texture", sceneProject, entities,settingsMaterial);
            propertyRow(RowPropertyType::Float_0_1, cpType, "submeshes["+std::to_string(s)+"].material.metallicFactor", "Metallic Factor", sceneProject, entities, settingsFactor);
            propertyRow(RowPropertyType::Float_0_1, cpType, "submeshes["+std::to_string(s)+"].material.roughnessFactor", "Roughness Factor", sceneProject, entities, settingsFactor);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.metallicRoughnessTexture", "Met. Roug. Texture", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Color3L, cpType, "submeshes["+std::to_string(s)+"].material.emissiveFactor", "Emissive Factor", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.emissiveTexture", "Emissive Texture", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.occlusionTexture", "Occlusion Texture", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.normalTexture", "Normal Texture", sceneProject, entities, settingsMaterial);
            endTable();
        }

        beginTable(cpType, getLabelSize("Texture Shadow"), "submeshes_settings");
        RowSettings settingsPrimitive;
        settingsPrimitive.enumEntries = &entriesPrimitiveType;

        propertyRow(RowPropertyType::Bool, cpType, "submeshes["+std::to_string(s)+"].faceCulling", "Face Culling", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "submeshes["+std::to_string(s)+"].textureShadow", "Texture Shadow", sceneProject, entities);
        propertyRow(RowPropertyType::Enum, cpType, "submeshes["+std::to_string(s)+"].primitiveType", "Primitive", sceneProject, entities, settingsPrimitive);
        RowSettings settingsTextureRect;
        settingsTextureRect.showColors = false;
        propertyRow(RowPropertyType::Vector4, cpType, "submeshes["+std::to_string(s)+"].textureRect", "Texture Rect", sceneProject, entities, settingsTextureRect);

        endTable();

        if (submeshGenerated) ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void editor::Properties::drawModelComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Model File"));

    propertyHeader("Model File");

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ModelComponent& model = sceneProject->scene->getComponent<ModelComponent>(entity);

        std::string currentPath = model.filename;
        std::string displayName = currentPath.empty() ? "< Not set >" : std::filesystem::path(currentPath).filename().string();

        ImGui::BeginGroup();

        float thumbSize = ImGui::GetFrameHeight() * 3;
        Texture* thumbTexture = findThumbnail(currentPath);
        if (thumbTexture) {
            ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Image(thumbTexture->getRender()->getGLHandler(), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                ImGui::EndTooltip();
            }
        }

        float availWidth = ImGui::GetContentRegionAvail().x;
        float buttonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, App::ThemeColors::filenameLabel);

        ImGui::BeginChild("modelfilename", ImVec2(availWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x, ImGui::GetFrameHeight()),
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
        float childAvail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(childAvail - textWidth - 2);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (currentPath.empty())
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", displayName.c_str());
        if (currentPath.empty())
            ImGui::PopStyleColor();
        ImGui::EndChild();
        if (!currentPath.empty()){
            ImGui::SetItemTooltip("%s", currentPath.c_str());
        }

        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##model_load")) {
            std::string path = editor::FileDialogs::openFileDialog(project->getProjectPath().string(), FILE_DIALOG_MODEL);
            if (!path.empty()) {
                std::filesystem::path projectPath = project->getProjectPath();
                std::filesystem::path filePath = std::filesystem::absolute(path);

                std::error_code ec;
                auto relative = std::filesystem::relative(filePath, projectPath, ec);
                if (ec || relative.string().find("..") != std::string::npos) {
                    ImGui::OpenPopup("Model Import Error");
                }else{
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new ModelLoadCmd(project, sceneProject->id, entity, relative.string()));
                }
            }
        }

        if (ImGui::BeginPopupModal("Model Import Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be within the project directory.");
            ImGui::Separator();
            float bw = 120;
            float ww = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((ww - bw) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndGroup();

        // Drag and drop model files from Resources
        if (sceneProject && sceneProject->playState == ScenePlayState::STOPPED) {
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
                    std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
                    if (!receivedStrings.empty()) {
                        const std::string droppedRelativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath()).generic_string();
                        if (Util::isModelFile(droppedRelativePath)) {
                            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new ModelLoadCmd(project, sceneProject->id, entity, droppedRelativePath));
                            ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

    }else{
        ImGui::TextDisabled("Select single entity");
    }

    propertyRow(RowPropertyType::LocalEntity, cpType, "skeleton", "Skeleton", sceneProject, entities);

    endTable();

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ModelComponent* model = sceneProject->scene->findComponent<ModelComponent>(entity);

        if (model && !model->animations.empty()) {
            ImGui::SeparatorText("Animations");

            beginTable(cpType, getLabelSize("Animation 00"), "model_animations");

            for (size_t i = 0; i < model->animations.size(); i++) {
                std::string propId = "animations[" + std::to_string(i) + "]";
                std::string label = "Animation " + std::to_string(i);
                propertyRow(RowPropertyType::LocalEntity, cpType, propId, label, sceneProject, entities);
            }

            endTable();
        }
    }
}

void editor::Properties::drawUIComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Texture"));
    propertyRow(RowPropertyType::Color4L, cpType, "color", "Color", sceneProject, entities);
    propertyRow(RowPropertyType::Texture, cpType, "texture", "Texture", sceneProject, entities);
    endTable();
}

void editor::Properties::drawButtonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settings;
    settings.onValueChanged = [sceneProject, entities](){
        for (const Entity& entity : entities){
            if (ButtonComponent* button = sceneProject->scene->findComponent<ButtonComponent>(entity)){
                button->needUpdateButton = true;
            }
        }
    };

    beginTable(cpType, getLabelSize("Texture Disabled"));
    propertyRow(RowPropertyType::Bool, cpType, "disabled", "Disabled", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Texture, cpType, "textureNormal", "Texture Normal", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Texture, cpType, "texturePressed", "Texture Pressed", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Texture, cpType, "textureDisabled", "Texture Disabled", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "colorNormal", "Color Normal", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "colorPressed", "Color Pressed", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "colorDisabled", "Color Disabled", sceneProject, entities, settings);
    endTable();
}

void editor::Properties::drawTextComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Multiline"));
    propertyRow(RowPropertyType::MultilineString, cpType, "text", "Text", sceneProject, entities);
    propertyRow(RowPropertyType::Font, cpType, "font", "Font", sceneProject, entities);
    propertyRow(RowPropertyType::UInt, cpType, "fontSize", "FontSize", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Bool, cpType, "multiline", "Multiline", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Fixed size");
    beginTable(cpType, getLabelSize("Height"), "text_settings_fixed");
    propertyRow(RowPropertyType::Bool, cpType, "fixedWidth", "Width", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "fixedHeight", "Height", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Pivot");
    beginTable(cpType, getLabelSize("Baseline"), "text_settings_pivot");
    propertyRow(RowPropertyType::Bool, cpType, "pivotBaseline", "Baseline", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "pivotCentered", "Center", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Advanced settings");
    beginTable(cpType, getLabelSize("Max. text size"), "text_settings_size");
    propertyRow(RowPropertyType::UInt, cpType, "maxTextSize", "Max. text size", sceneProject, entities, settingsInt);
    endTable();
}

void editor::Properties::drawUILayoutComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsAnchorPreset;
    settingsAnchorPreset.enumEntries = &entriesAnchorPreset;

    RowSettings settingsAnchorPoint;
    settingsAnchorPoint.stepSize = 0.01f;
    settingsAnchorPoint.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsOffset;
    settingsOffset.stepSize = 1.0f;
    settingsOffset.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsPositionOffset;
    settingsPositionOffset.stepSize = 1.0f;
    settingsPositionOffset.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Position Offset"));
    propertyRow(RowPropertyType::UInt, cpType, "width", "Width", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "height", "Height", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Bool, cpType, "usingAnchors", "Use Anchors", sceneProject, entities);
    bool showAnchorProperties = true;
    bool disableAnchorDetails = false;
    for (const Entity& entity : entities){
        const UILayoutComponent& layout = sceneProject->scene->getComponent<UILayoutComponent>(entity);
        if (layout.usingAnchors){
            showAnchorProperties = false;
        }
        if (layout.anchorPreset != AnchorPreset::NONE){
            disableAnchorDetails = true;
        }
    }

    if (!showAnchorProperties){
        propertyRow(RowPropertyType::Enum, cpType, "anchorPreset", "Preset", sceneProject, entities, settingsAnchorPreset);
        ImGui::BeginDisabled(disableAnchorDetails);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointLeft", "Anchor Left", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointTop", "Anchor Top", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointRight", "Anchor Right", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointBottom", "Anchor Bottom", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetLeft", "Offset Left", sceneProject, entities, settingsOffset);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetTop", "Offset Top", sceneProject, entities, settingsOffset);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetRight", "Offset Right", sceneProject, entities, settingsOffset);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetBottom", "Offset Bottom", sceneProject, entities, settingsOffset);
        ImGui::EndDisabled();
    }
    propertyRow(RowPropertyType::Vector2, cpType, "positionOffset", "Position Offset", sceneProject, entities, settingsPositionOffset);
    propertyRow(RowPropertyType::Bool, cpType, "ignoreScissor", "Ignore Scissor", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "ignoreEvents", "Ignore Events", sceneProject, entities);
    endTable();
}

void editor::Properties::drawUIContainerComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    static std::vector<editor::EnumEntry> entriesContainerType = {
        { (int)ContainerType::VERTICAL, "Vertical" },
        { (int)ContainerType::HORIZONTAL, "Horizontal" },
        { (int)ContainerType::VERTICAL_WRAP, "Vertical Wrap" },
        { (int)ContainerType::HORIZONTAL_WRAP, "Horizontal Wrap" }
    };

    RowSettings settingsContainerType;
    settingsContainerType.enumEntries = &entriesContainerType;

    RowSettings settingsCellSize;
    settingsCellSize.stepSize = 1.0f;
    settingsCellSize.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsRect;
    settingsRect.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsLayout;
    settingsLayout.stepSize = 1.0f;
    settingsLayout.secondColSize = 6 * ImGui::GetFontSize();

    unsigned int numBoxes = sceneProject->scene->getComponent<UIContainerComponent>(entities[0]).numBoxes;
    for (Entity& entity : entities){
        numBoxes = std::min(numBoxes, sceneProject->scene->getComponent<UIContainerComponent>(entity).numBoxes);
    }

    beginTable(cpType, getLabelSize("Num Boxes"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, settingsContainerType);
    propertyRow(RowPropertyType::Label, cpType, "numBoxes", "Num Boxes", sceneProject, entities, settingsLayout);

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_GEAR)){
        ImGui::OpenPopup("menusettings_container_boxes");
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(10 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_container_boxes")){

        if (numBoxes == 0){
            ImGui::TextDisabled("No boxes available");
        }

        for (unsigned int b = 0; b < numBoxes; b++){
            ImGui::SeparatorText(("Box " + std::to_string(b + 1)).c_str());

            beginTable(cpType, getLabelSize("Rect Height"), "box_popup_" + std::to_string(b));

            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].layout", "Layout Entity", sceneProject, entities, settingsLayout);
            propertyRow(RowPropertyType::Bool, cpType, "boxes["+std::to_string(b)+"].expand", "Expand", sceneProject, entities);

            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.x", "Rect X", sceneProject, entities, settingsRect);
            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.y", "Rect Y", sceneProject, entities, settingsRect);
            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.width", "Rect Width", sceneProject, entities, settingsRect);
            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.height", "Rect Height", sceneProject, entities, settingsRect);

            endTable();
        }

        ImGui::EndPopup();
    }
    endTable();

    bool showWrapSettings = false;
    for (const Entity& entity : entities){
        const UIContainerComponent& container = sceneProject->scene->getComponent<UIContainerComponent>(entity);
        if (container.type == ContainerType::VERTICAL_WRAP || container.type == ContainerType::HORIZONTAL_WRAP){
            showWrapSettings = true;
        }
    }

    if (showWrapSettings) {
        ImGui::SeparatorText("Wrap settings");
        beginTable(cpType, getLabelSize("Use All Space"), "wrap_settings");
        propertyRow(RowPropertyType::Bool, cpType, "useAllWrapSpace", "Use All Space", sceneProject, entities);
        propertyRow(RowPropertyType::UInt, cpType, "wrapCellWidth", "Cell Width", sceneProject, entities, settingsCellSize);
        propertyRow(RowPropertyType::UInt, cpType, "wrapCellHeight", "Cell Height", sceneProject, entities, settingsCellSize);
        endTable();
    }

}

void editor::Properties::drawImageComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    ImGui::SeparatorText("Nine-patch rect");

    if (entities.size() == 1) {
        if (UIComponent* ui = sceneProject->scene->findComponent<UIComponent>(entities[0])){
            Texture* thumbTexture = findThumbnail(ui->texture.getPath());
            if (thumbTexture) {
                drawNinePatchesPreview(sceneProject->scene->getComponent<ImageComponent>(entities[0]), &ui->texture, thumbTexture, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
            }
        }
    }

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsTexScale;
    settingsTexScale.secondColSize = 6 * ImGui::GetFontSize();
    settingsTexScale.help = "Increase or decrease texture area by a factor";

    beginTable(cpType, getLabelSize("Margin Bottom"), "nine_margin_table");
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginLeft", "Margin Left", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginRight", "Margin Right", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginTop", "Margin Top", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginBottom", "Margin Bottom", sceneProject, entities, settingsInt);
    endTable();

    beginTable(cpType, getLabelSize("Texture Scale"));
    propertyRow(RowPropertyType::Float, cpType, "textureScaleFactor", "Texture Scale", sceneProject, entities, settingsTexScale);
    endTable();
}

void editor::Properties::drawSpriteComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsTexScale;
    settingsTexScale.secondColSize = 6 * ImGui::GetFontSize();
    settingsTexScale.help = "Increase or decrease texture area by a factor";

    RowSettings settingsPivot;
    settingsPivot.enumEntries = &entriesPivotPreset;

    beginTable(cpType, getLabelSize("Texture Scale"));
    propertyRow(RowPropertyType::UInt, cpType, "width", "Width", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "height", "Height", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Enum, cpType, "pivotPreset", "Pivot", sceneProject, entities, settingsPivot);
    propertyRow(RowPropertyType::Float, cpType, "textureScaleFactor", "Texture Scale", sceneProject, entities, settingsTexScale);
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "flipY", "Flip Y", "automaticFlipY", "Automatic Flip Y", sceneProject, entities);

    endTable();

    // --- Frames Rect Section ---
    if (entities.size() == 1) {
        SpriteComponent& sprite = sceneProject->scene->getComponent<SpriteComponent>(entities[0]);
        MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entities[0]);
        Texture previewTexture;
        if (meshComp) {
            if (meshComp->numSubmeshes > 0) {
                previewTexture = meshComp->submeshes[0].material.baseColorTexture;
            }
        }

        ImGui::SeparatorText("Sprite Frames");

        // Frame count from numFramesRect
        int activeFrameCount = (int)sprite.numFramesRect;

        // Sprite slicer tool button
        float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button(ICON_FA_GRIP " Slicer Tool", ImVec2(buttonWidth, 0))) {
            textureSlicerToolDialog.open(
                previewTexture,
                static_cast<int>(sprite.width),
                static_cast<int>(sprite.height),
                [this, sceneProject, entities, cpType, previewTexture](const TextureSlicerToolDialog::SliceResult& result) {
                    editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                    // Clear existing frames beyond new count
                    SpriteComponent& sprite = sceneProject->scene->getComponent<SpriteComponent>(entities[0]);
                    for (unsigned int i = (unsigned int)result.rects.size(); i < sprite.numFramesRect; i++) {
                        std::string prefix = "framesRect[" + std::to_string(i) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(0, 0, 0, 0));
                    }

                    // Set new frames from slicer result
                    for (size_t i = 0; i < result.rects.size(); i++) {
                        std::string prefix = "framesRect[" + std::to_string(i) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", result.rects[i].name);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(result.rects[i].rect.getX(), result.rects[i].rect.getY(),
                                result.rects[i].rect.getWidth(), result.rects[i].rect.getHeight()));
                    }

                    // Update numFramesRect
                    unsigned int newCount = (unsigned int)result.rects.size();
                    multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                        "numFramesRect", newCount);

                    // Apply first frame to sprite mesh
                    MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entities[0]);
                    int sheetWidth = !previewTexture.empty() ? previewTexture.getWidth() : (int)sprite.width;
                    int sheetHeight = !previewTexture.empty() ? previewTexture.getHeight() : (int)sprite.height;
                    if (meshComp && meshComp->numSubmeshes > 0 && !result.rects.empty() && sheetWidth > 0 && sheetHeight > 0) {
                        const Rect& firstFrameRect = result.rects[0].rect;
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], ComponentType::MeshComponent,
                            "submeshes[0].textureRect", Vector4(
                                firstFrameRect.getX() / (float)sheetWidth,
                                firstFrameRect.getY() / (float)sheetHeight,
                                firstFrameRect.getWidth() / (float)sheetWidth,
                                firstFrameRect.getHeight() / (float)sheetHeight));
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }
            );
        }

        ImGui::SameLine();

        // Add frame button
        if (ImGui::Button(ICON_FA_PLUS " Add Frame", ImVec2(buttonWidth, 0))) {
            int freeSlot = (int)sprite.numFramesRect;
            if (freeSlot < (int)sprite.framesRect.size()) {
                editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                std::string prefix = "framesRect[" + std::to_string(freeSlot) + "]";
                multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".name", "frame_" + std::to_string(freeSlot));
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                    "numFramesRect", (unsigned int)(freeSlot + 1));
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }

        beginTable(cpType, getLabelSize("Frames"), "sprite_frames_header");
        propertyHeader("Frames", -1, false, false);
        ImGui::Text("%d", activeFrameCount);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##toggle_all_frames", spriteFramesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
            spriteFramesExpanded = !spriteFramesExpanded;
        }
        endTable();

        if (spriteFramesExpanded) {
            // Draw each frame
            RowSettings settingsFrameRect;
            settingsFrameRect.stepSize = 1.0f;
            settingsFrameRect.format = "%.0f";
            settingsFrameRect.showColors = false;

            for (unsigned int i = 0; i < sprite.numFramesRect; i++) {

                ImGui::PushID(i);

                std::string frameLabel = "[" + std::to_string(i) + "] " + sprite.framesRect[i].name;
                std::string frameGroupStr = "frame_" + std::to_string(i);
                std::string prefix = "framesRect[" + std::to_string(i) + "]";

                ImGui::SeparatorText(frameLabel.c_str());

                beginTable(cpType, getLabelSize("Frame"), frameGroupStr);

                // Frame row: Preview + Trash button
                propertyHeader("Frame", -1, false, false);

                {
                    float previewSize = ImGui::GetFrameHeight() * 2.2f;
                    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                    ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
                    ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
                    ImVec2 applyButtonSize = arrowButtonSize;
                    ImVec2 resizeButtonSize = arrowButtonSize;

                    bool hasFramePreview = !previewTexture.empty() && sprite.framesRect[i].rect.getWidth() > 0.0f && sprite.framesRect[i].rect.getHeight() > 0.0f;
                    bool hasValidMeshTextureRect = meshComp && meshComp->numSubmeshes > 0;
                    bool canApplyFrame = hasValidMeshTextureRect && !previewTexture.empty() && previewTexture.getWidth() > 0 && previewTexture.getHeight() > 0;
                    bool canResizeToFrame = sprite.framesRect[i].rect.getWidth() > 0.0f && sprite.framesRect[i].rect.getHeight() > 0.0f;
                    unsigned int frameWidth = canResizeToFrame ? static_cast<unsigned int>(sprite.framesRect[i].rect.getWidth()) : 0;
                    unsigned int frameHeight = canResizeToFrame ? static_cast<unsigned int>(sprite.framesRect[i].rect.getHeight()) : 0;
                    Rect normalizedFrameRect = sprite.framesRect[i].rect;
                    if (canApplyFrame) {
                        normalizedFrameRect = Rect(
                            sprite.framesRect[i].rect.getX() / (float)previewTexture.getWidth(),
                            sprite.framesRect[i].rect.getY() / (float)previewTexture.getHeight(),
                            sprite.framesRect[i].rect.getWidth() / (float)previewTexture.getWidth(),
                            sprite.framesRect[i].rect.getHeight() / (float)previewTexture.getHeight()
                        );
                    }
                    bool isAppliedFrame = false;
                    if (hasValidMeshTextureRect) {
                        const Rect& appliedRect = meshComp->submeshes[0].textureRect;
                        isAppliedFrame = appliedRect.getX() == normalizedFrameRect.getX()
                                && appliedRect.getY() == normalizedFrameRect.getY()
                                && appliedRect.getWidth() == normalizedFrameRect.getWidth()
                                && appliedRect.getHeight() == normalizedFrameRect.getHeight();
                    }
                        bool isSizedToFrame = canResizeToFrame && sprite.width == frameWidth && sprite.height == frameHeight;

                    float trailingWidth = arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + applyButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + resizeButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + deleteButtonSize.x;
                    if (hasFramePreview) {
                        trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
                    }

                    float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
                    ImGui::SetCursorPosX(targetX);

                    if (hasFramePreview) {
                        drawSpriteFramePreview(&previewTexture, sprite.framesRect[i].rect, ImVec2(previewSize, previewSize), "##frame_preview");
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", frameLabel.c_str());

                            float tooltipMaxSize = 200.0f;
                            float scale = std::min(tooltipMaxSize / std::max(1.0f, sprite.framesRect[i].rect.getWidth()),
                                                   tooltipMaxSize / std::max(1.0f, sprite.framesRect[i].rect.getHeight()));
                            scale = std::min(scale, 1.0f);
                            ImVec2 tooltipSize(std::max(32.0f, sprite.framesRect[i].rect.getWidth() * scale),
                                               std::max(32.0f, sprite.framesRect[i].rect.getHeight() * scale));

                            drawSpriteFramePreview(&previewTexture, sprite.framesRect[i].rect, tooltipSize, "##frame_preview_tooltip");
                            ImGui::EndTooltip();
                        }

                        ImGui::SameLine();
                    }

                    if (ImGui::ArrowButton("##toggle_frame", spriteFramesButtonGroups[frameGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
                        spriteFramesButtonGroups[frameGroupStr] = !spriteFramesButtonGroups[frameGroupStr];
                    }
                    ImGui::SameLine();

                    bool deleted = false;
                    ImGui::BeginDisabled(!canApplyFrame || isAppliedFrame);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_CHECK "##apply_frame", applyButtonSize)) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], ComponentType::MeshComponent,
                            "submeshes[0].textureRect", Vector4(normalizedFrameRect.getX(), normalizedFrameRect.getY(), normalizedFrameRect.getWidth(), normalizedFrameRect.getHeight()));
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    if (ImGui::IsItemHovered()) {
                        if (!meshComp) {
                            ImGui::SetTooltip("Sprite entity has no MeshComponent");
                        } else if (meshComp->numSubmeshes == 0) {
                            ImGui::SetTooltip("Sprite mesh has no submeshes");
                        } else if (!canApplyFrame) {
                            ImGui::SetTooltip("Sprite needs a valid texture to apply a frame");
                        } else {
                            ImGui::SetTooltip("Apply frame to sprite");
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndDisabled();

                    ImGui::SameLine();

                    ImGui::BeginDisabled(!canResizeToFrame || isSizedToFrame);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_EXPAND "##resize_to_frame", resizeButtonSize)) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "width", frameWidth);
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "height", frameHeight);
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    if (ImGui::IsItemHovered()) {
                        if (!canResizeToFrame) {
                            ImGui::SetTooltip("Frame needs a valid size to resize the sprite");
                        } else if (isSizedToFrame) {
                            ImGui::SetTooltip("Sprite already matches this frame size");
                        } else {
                            ImGui::SetTooltip("Resize sprite to this frame size");
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndDisabled();

                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_TRASH_CAN"##delete_frame")) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                        // Shift frames after deleted one
                        for (unsigned int j = i; j + 1 < sprite.numFramesRect; j++) {
                            std::string srcPrefix = "framesRect[" + std::to_string(j + 1) + "]";
                            std::string dstPrefix = "framesRect[" + std::to_string(j) + "]";
                            multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".name", sprite.framesRect[j + 1].name);
                            multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".rect", Vector4(sprite.framesRect[j + 1].rect.getX(), sprite.framesRect[j + 1].rect.getY(),
                                    sprite.framesRect[j + 1].rect.getWidth(), sprite.framesRect[j + 1].rect.getHeight()));
                        }

                        // Clear last slot
                        unsigned int lastIdx = sprite.numFramesRect - 1;
                        std::string lastPrefix = "framesRect[" + std::to_string(lastIdx) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType, lastPrefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType, lastPrefix + ".rect", Vector4(0, 0, 0, 0));

                        // Decrement count
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "numFramesRect", sprite.numFramesRect - 1);

                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                        deleted = true;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);

                    // Name and Rect rows
                    if (!deleted && spriteFramesButtonGroups[frameGroupStr]) {
                        propertyRow(RowPropertyType::String, cpType, prefix + ".name", "Name", sceneProject, entities);
                        propertyRow(RowPropertyType::Vector4, cpType, prefix + ".rect", "Rect", sceneProject, entities, settingsFrameRect);
                    }
                }

                endTable();

                ImGui::PopID();
            }
        }
    } else {
        ImGui::SeparatorText("Sprite Frames");
        ImGui::TextDisabled("Select a single entity to edit sprite frames");
    }
}

void editor::Properties::drawCameraComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;
    CameraComponent& camera = scene->getComponent<CameraComponent>(entities[0]);

    float firstColSize = getLabelSize("Auto Resize");

    beginTable(cpType, firstColSize);

    RowSettings enumSettings;
    enumSettings.enumEntries = &entriesCameraType;
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, enumSettings);

    RowSettings defaultSettings;

    propertyRow(RowPropertyType::Bool, cpType, "useTarget", "Use Target", sceneProject, entities, defaultSettings);

    if (camera.useTarget){
        propertyRow(RowPropertyType::Vector3, cpType, "target", "Target", sceneProject, entities, defaultSettings);
    }
    propertyRow(RowPropertyType::Direction, cpType, "up", "Up", sceneProject, entities, defaultSettings);

    if (camera.type == CameraType::CAMERA_PERSPECTIVE) {
        RowSettings fovSettings;
        fovSettings.format = "%.3f";
        fovSettings.stepSize = 0.01f;
        propertyRow(RowPropertyType::Float, cpType, "yfov", "Y FOV", sceneProject, entities, fovSettings);

        propertyRow(RowPropertyType::Float, cpType, "aspect", "Aspect", sceneProject, entities, defaultSettings);
    }

    if (camera.type == CameraType::CAMERA_ORTHO || camera.type == CameraType::CAMERA_UI) {
        propertyRow(RowPropertyType::Float, cpType, "left", "Left", sceneProject, entities, defaultSettings);
        propertyRow(RowPropertyType::Float, cpType, "right", "Right", sceneProject, entities, defaultSettings);
        propertyRow(RowPropertyType::Float, cpType, "bottom", "Bottom", sceneProject, entities, defaultSettings);
        propertyRow(RowPropertyType::Float, cpType, "top", "Top", sceneProject, entities, defaultSettings);
    }

    propertyRow(RowPropertyType::Float, cpType, "near", "Near", sceneProject, entities, defaultSettings);
    propertyRow(RowPropertyType::Float, cpType, "far", "Far", sceneProject, entities, defaultSettings);

    //propertyRow(RowPropertyType::Bool, cpType, "renderToTexture", "Render To Texture", sceneProject, entities, defaultSettings);
    //propertyRow(RowPropertyType::Bool, cpType, "transparentSort", "Transparent Sort", sceneProject, entities, defaultSettings);
    propertyRow(RowPropertyType::Bool, cpType, "autoResize", "Auto Resize", sceneProject, entities, defaultSettings);

    endTable();
}

void editor::Properties::drawTilemapComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsTexScale;
    settingsTexScale.secondColSize = 6 * ImGui::GetFontSize();
    settingsTexScale.help = "Increase or decrease texture area by a factor";

    beginTable(cpType, getLabelSize("Reserve Tiles"));
    propertyRow(RowPropertyType::UInt, cpType, "width", "Width", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "height", "Height", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Float, cpType, "textureScaleFactor", "Texture Scale", sceneProject, entities, settingsTexScale);
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "flipY", "Flip Y", "automaticFlipY", "Automatic Flip Y", sceneProject, entities);
    propertyRow(RowPropertyType::UInt, cpType, "reserveTiles", "Reserve Tiles", sceneProject, entities, settingsInt);
    endTable();

    if (entities.size() == 1) {
        TilemapComponent& tilemap = sceneProject->scene->getComponent<TilemapComponent>(entities[0]);
        MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entities[0]);

        // Build submesh list for tileset slicer
        std::vector<TextureSlicerToolDialog::SubmeshInfo> submeshInfos;
        if (meshComp) {
            for (unsigned int s = 0; s < meshComp->numSubmeshes; s++) {
                TextureSlicerToolDialog::SubmeshInfo info;
                info.name = "Submesh " + std::to_string(s);
                info.texture = meshComp->submeshes[s].material.baseColorTexture;
                submeshInfos.push_back(info);
            }
        }

        // --- Tile Rects Section ---
        ImGui::SeparatorText("Tile Rects");

        int activeTileRectCount = (int)tilemap.numTilesRect;

        float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        // Slicer tool button
        if (ImGui::Button(ICON_FA_TABLE_CELLS " Slicer Tool", ImVec2(buttonWidth, 0))) {
            textureSlicerToolDialog.openTileset(
                submeshInfos,
                static_cast<int>(tilemap.width),
                static_cast<int>(tilemap.height),
                [this, sceneProject, entities, cpType](const TextureSlicerToolDialog::SliceResult& result) {
                    editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                    TilemapComponent& tilemap = sceneProject->scene->getComponent<TilemapComponent>(entities[0]);

                    // Collect existing rects from other submeshes (keep them)
                    struct KeptRect {
                        std::string name;
                        int submeshId;
                        Vector4 rect;
                    };
                    std::vector<KeptRect> kept;
                    for (unsigned int i = 0; i < tilemap.numTilesRect; i++) {
                        if (tilemap.tilesRect[i].submeshId != result.submeshId) {
                            KeptRect kr;
                            kr.name = tilemap.tilesRect[i].name;
                            kr.submeshId = tilemap.tilesRect[i].submeshId;
                            kr.rect = Vector4(tilemap.tilesRect[i].rect.getX(), tilemap.tilesRect[i].rect.getY(),
                                              tilemap.tilesRect[i].rect.getWidth(), tilemap.tilesRect[i].rect.getHeight());
                            kept.push_back(kr);
                        }
                    }

                    unsigned int newTotal = (unsigned int)(kept.size() + result.rects.size());
                    unsigned int writeIndex = 0;

                    // Write kept rects (from other submeshes)
                    for (size_t i = 0; i < kept.size(); i++, writeIndex++) {
                        std::string prefix = "tilesRect[" + std::to_string(writeIndex) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", kept[i].name);
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".submeshId", kept[i].submeshId);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", kept[i].rect);
                    }

                    // Write new rects from slicer result
                    for (size_t i = 0; i < result.rects.size(); i++, writeIndex++) {
                        std::string prefix = "tilesRect[" + std::to_string(writeIndex) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", result.rects[i].name);
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".submeshId", result.submeshId);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(result.rects[i].rect.getX(), result.rects[i].rect.getY(),
                                result.rects[i].rect.getWidth(), result.rects[i].rect.getHeight()));
                    }

                    // Clear leftover slots beyond new total
                    for (unsigned int i = newTotal; i < tilemap.numTilesRect; i++) {
                        std::string prefix = "tilesRect[" + std::to_string(i) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".submeshId", -1);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(0, 0, 0, 0));
                    }

                    // Update numTilesRect
                    multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                        "numTilesRect", newTotal);

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }
            );
        }

        ImGui::SameLine();

        // Add tile rect button
        if (ImGui::Button(ICON_FA_PLUS " Add Tile Rect", ImVec2(buttonWidth, 0))) {
            int freeSlot = (int)tilemap.numTilesRect;
            if (freeSlot < (int)tilemap.tilesRect.size()) {
                editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                std::string prefix = "tilesRect[" + std::to_string(freeSlot) + "]";
                multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".name", "rect_" + std::to_string(freeSlot));
                multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".submeshId", 0);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                    "numTilesRect", (unsigned int)(freeSlot + 1));
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }

        ImGui::TextDisabled(ICON_FA_GRIP_VERTICAL " Drag rect preview to Scene to place tile");

        beginTable(cpType, getLabelSize("Tile Rects"), "tilemap_rects_header");
        propertyHeader("Tile Rects", -1, false, false);
        ImGui::Text("%d", activeTileRectCount);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##toggle_all_rects", tilemapRectsExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
            tilemapRectsExpanded = !tilemapRectsExpanded;
        }
        endTable();

        if (tilemapRectsExpanded) {
            // Draw each tile rect
            RowSettings settingsRect;
            settingsRect.stepSize = 1.0f;
            settingsRect.format = "%.0f";
            settingsRect.showColors = false;

            for (unsigned int i = 0; i < tilemap.numTilesRect; i++) {
                ImGui::PushID(i);

                std::string label = "[" + std::to_string(i) + "] " + tilemap.tilesRect[i].name;
                std::string groupStr = "tilerect_" + std::to_string(i);
                std::string prefix = "tilesRect[" + std::to_string(i) + "]";

                ImGui::SeparatorText(label.c_str());

                beginTable(cpType, getLabelSize("Name"), groupStr);

                propertyHeader("Rect", -1, false, false);
                {
                    float previewSize = ImGui::GetFrameHeight() * 2.2f;
                    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                    ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
                    ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

                    int submeshId = tilemap.tilesRect[i].submeshId;
                    Texture* rectTex = nullptr;
                    if (meshComp && submeshId >= 0 && (unsigned int)submeshId < meshComp->numSubmeshes) {
                        rectTex = &meshComp->submeshes[submeshId].material.baseColorTexture;
                        if (rectTex->empty()) rectTex = nullptr;
                    }

                    bool hasFramePreview = rectTex && tilemap.tilesRect[i].rect.getWidth() > 0.0f && tilemap.tilesRect[i].rect.getHeight() > 0.0f;

                    float trailingWidth = arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + deleteButtonSize.x;
                    if (hasFramePreview) {
                        trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
                    }

                    float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
                    ImGui::SetCursorPosX(targetX);

                    if (hasFramePreview) {
                        drawSpriteFramePreview(rectTex, tilemap.tilesRect[i].rect, ImVec2(previewSize, previewSize), "##rect_preview");
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            TileRectPayload payload{(uint32_t)sceneProject->id, entities[0], (int)i};
                            ImGui::SetDragDropPayload("tile_rect", &payload, sizeof(payload));
                            drawSpriteFramePreview(rectTex, tilemap.tilesRect[i].rect, ImVec2(48, 48), "##rect_drag_preview");
                            ImGui::EndDragDropSource();
                        } else if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", label.c_str());

                            float tooltipMaxSize = 200.0f;
                            float scale = std::min(tooltipMaxSize / std::max(1.0f, tilemap.tilesRect[i].rect.getWidth()),
                                                   tooltipMaxSize / std::max(1.0f, tilemap.tilesRect[i].rect.getHeight()));
                            scale = std::min(scale, 1.0f);
                            ImVec2 tooltipSize(std::max(32.0f, tilemap.tilesRect[i].rect.getWidth() * scale),
                                               std::max(32.0f, tilemap.tilesRect[i].rect.getHeight() * scale));

                            drawSpriteFramePreview(rectTex, tilemap.tilesRect[i].rect, tooltipSize, "##rect_preview_tooltip");
                            ImGui::EndTooltip();
                        }
                        ImGui::SameLine();
                    }

                    if (ImGui::ArrowButton("##toggle_rect", tilemapRectsButtonGroups[groupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
                        tilemapRectsButtonGroups[groupStr] = !tilemapRectsButtonGroups[groupStr];
                    }
                    ImGui::SameLine();

                    {
                    ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_SQUARE_PLUS);
                    ImVec2 centerPad((arrowButtonSize.x - iconSize.x) * 0.5f, (arrowButtonSize.y - iconSize.y) * 0.5f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, centerPad);
                    }
                    if (ImGui::Button(ICON_FA_SQUARE_PLUS "##add_tile_from_rect")) {
                        int freeSlot = (int)tilemap.numTiles;
                        if (freeSlot < (int)tilemap.tiles.size()) {
                            editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                            std::string tilePrefix = "tiles[" + std::to_string(freeSlot) + "]";
                            multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".name", tilemap.tilesRect[i].name);
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".rectId", (int)i);
                            multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".width", tilemap.tilesRect[i].rect.getWidth());
                            multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".height", tilemap.tilesRect[i].rect.getHeight());
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                                "numTiles", (unsigned int)(freeSlot + 1));
                            multiCmd->setNoMerge();
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                        }
                    }
                    ImGui::PopStyleVar();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Create tile from this rect");
                    }
                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_rect", deleteButtonSize)) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                        // Shift subsequent rects down
                        for (unsigned int j = i; j < tilemap.numTilesRect - 1; j++) {
                            std::string dstPrefix = "tilesRect[" + std::to_string(j) + "]";
                            multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".name", tilemap.tilesRect[j + 1].name);
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".submeshId", tilemap.tilesRect[j + 1].submeshId);
                            multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".rect", Vector4(tilemap.tilesRect[j + 1].rect.getX(), tilemap.tilesRect[j + 1].rect.getY(),
                                    tilemap.tilesRect[j + 1].rect.getWidth(), tilemap.tilesRect[j + 1].rect.getHeight()));
                        }
                        // Clear last slot
                        unsigned int lastIdx = tilemap.numTilesRect - 1;
                        std::string lastPrefix = "tilesRect[" + std::to_string(lastIdx) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            lastPrefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            lastPrefix + ".submeshId", -1);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            lastPrefix + ".rect", Vector4(0, 0, 0, 0));
                        for (unsigned int j = 0; j < tilemap.numTiles; j++) {
                            int rectId = tilemap.tiles[j].rectId;
                            if (rectId == (int)i) {
                                rectId = -1;
                            } else if (rectId > (int)i) {
                                rectId--;
                            }

                            if (rectId != tilemap.tiles[j].rectId) {
                                multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                                    "tiles[" + std::to_string(j) + "].rectId", rectId);
                            }
                        }
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "numTilesRect", (unsigned int)(tilemap.numTilesRect - 1));
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                        endTable();
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                }

                if (tilemapRectsButtonGroups[groupStr]) {
                    propertyRow(RowPropertyType::String, cpType, prefix + ".name", "Name", sceneProject, entities);
                    propertyRow(RowPropertyType::Vector4, cpType, prefix + ".rect", "Rect", sceneProject, entities, settingsRect);

                    // Submesh selector combo
                    {
                        unsigned int numSubmeshes = meshComp ? meshComp->numSubmeshes : 1;
                        int currentSubmeshId = tilemap.tilesRect[i].submeshId;

                        propertyHeader("Submesh");
                        ImGui::SetNextItemWidth(-1);

                        std::string comboLabel = "##submeshId_" + std::to_string(i);
                        std::string previewStr = (currentSubmeshId >= 0 && (unsigned int)currentSubmeshId < numSubmeshes)
                            ? "Submesh " + std::to_string(currentSubmeshId)
                            : "None";

                        if (ImGui::BeginCombo(comboLabel.c_str(), previewStr.c_str())) {
                            for (unsigned int s = 0; s < numSubmeshes; s++) {
                                bool isSelected = ((int)s == currentSubmeshId);
                                std::string itemLabel = "Submesh " + std::to_string(s);
                                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                    if ((int)s != currentSubmeshId) {
                                        editor::PropertyCmd<int>* propCmd = new editor::PropertyCmd<int>(
                                            project, sceneProject->id, entities[0], cpType,
                                            prefix + ".submeshId", (int)s);
                                        propCmd->setNoMerge();
                                        CommandHandle::get(project->getSelectedSceneId())->addCommand(propCmd);
                                    }
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                }

                endTable();
                ImGui::PopID();
            }
        }

        // --- Tiles Section ---
        ImGui::SeparatorText("Tiles");

        int activeTileCount = (int)tilemap.numTiles;

        // Add tile button
        if (ImGui::Button(ICON_FA_PLUS " Add Tile", ImVec2(buttonWidth, 0))) {
            int freeSlot = (int)tilemap.numTiles;
            if (freeSlot < (int)tilemap.tiles.size()) {
                float defaultWidth = 100.0f;
                float defaultHeight = 100.0f;
                if (tilemap.numTilesRect > 0 && tilemap.tilesRect[0].rect.getWidth() > 0.0f && tilemap.tilesRect[0].rect.getHeight() > 0.0f) {
                    defaultWidth = tilemap.tilesRect[0].rect.getWidth();
                    defaultHeight = tilemap.tilesRect[0].rect.getHeight();
                }
                editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                std::string prefix = "tiles[" + std::to_string(freeSlot) + "]";
                multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".name", "tile_" + std::to_string(freeSlot));
                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".width", defaultWidth);
                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".height", defaultHeight);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                    "numTiles", (unsigned int)(freeSlot + 1));
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }

        beginTable(cpType, getLabelSize("Tiles"), "tilemap_tiles_header");
        propertyHeader("Tiles", -1, false, false);
        ImGui::Text("%d", activeTileCount);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##toggle_all_tiles", tilemapTilesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
            tilemapTilesExpanded = !tilemapTilesExpanded;
        }
        endTable();

        if (tilemapTilesExpanded) {
            RowSettings settingsTileFloat;
            settingsTileFloat.stepSize = 1.0f;

            for (unsigned int i = 0; i < tilemap.numTiles; i++) {
                ImGui::PushID(1000 + i);

                std::string label = "[" + std::to_string(i) + "] " + tilemap.tiles[i].name;
                std::string groupStr = "tile_" + std::to_string(i);
                std::string prefix = "tiles[" + std::to_string(i) + "]";

                bool isTileSelected = sceneProject->sceneRender &&
                    sceneProject->sceneRender->getSelectedTileIndex() == (int)i &&
                    sceneProject->sceneRender->getSelectedTileEntity() == entities[0];

                if (isTileSelected) {
                    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                    float width = ImGui::GetContentRegionAvail().x;
                    float height = ImGui::GetFrameHeight();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    ImVec4 highlightColor = ImGui::GetStyle().Colors[ImGuiCol_HeaderActive];
                    highlightColor.w = 0.35f;
                    drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + width, cursorPos.y + height), ImGui::GetColorU32(highlightColor), 3.0f);
                }
                ImGui::SeparatorText(label.c_str());

                beginTable(cpType, getLabelSize("Position"), groupStr);

                propertyHeader("Tile", -1, false, false);
                {
                    float previewSize = ImGui::GetFrameHeight() * 2.2f;
                    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                    ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
                    ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

                    Texture* tileTex = nullptr;
                    Rect tileRect;
                    if (tilemap.tiles[i].rectId >= 0 && tilemap.tiles[i].rectId < (int)tilemap.numTilesRect) {
                        const auto& rectData = tilemap.tilesRect[tilemap.tiles[i].rectId];
                        tileRect = rectData.rect;
                        if (meshComp && rectData.submeshId >= 0 && (unsigned int)rectData.submeshId < meshComp->numSubmeshes) {
                            tileTex = &meshComp->submeshes[rectData.submeshId].material.baseColorTexture;
                            if (tileTex->empty()) tileTex = nullptr;
                        }
                    }

                    bool hasFramePreview = tileTex && tileRect.getWidth() > 0.0f && tileRect.getHeight() > 0.0f;

                    float trailingWidth = arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + deleteButtonSize.x;
                    if (hasFramePreview) {
                        trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
                    }

                    float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
                    ImGui::SetCursorPosX(targetX);

                    if (hasFramePreview) {
                        drawSpriteFramePreview(tileTex, tileRect, ImVec2(previewSize, previewSize), "##tile_preview");
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", label.c_str());

                            float tooltipMaxSize = 200.0f;
                            float scale = std::min(tooltipMaxSize / std::max(1.0f, tileRect.getWidth()),
                                                   tooltipMaxSize / std::max(1.0f, tileRect.getHeight()));
                            scale = std::min(scale, 1.0f);
                            ImVec2 tooltipSize(std::max(32.0f, tileRect.getWidth() * scale),
                                               std::max(32.0f, tileRect.getHeight() * scale));

                            drawSpriteFramePreview(tileTex, tileRect, tooltipSize, "##tile_preview_tooltip");
                            ImGui::EndTooltip();
                        }
                        ImGui::SameLine();
                    }

                    if (ImGui::ArrowButton("##toggle_tile", tilemapTilesButtonGroups[groupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
                        tilemapTilesButtonGroups[groupStr] = !tilemapTilesButtonGroups[groupStr];
                    }
                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_TRASH_CAN, deleteButtonSize)) {
                        Command* deleteCmd = ProjectUtils::buildDeleteTileCmd(project, sceneProject->id, entities[0], i);
                        if (deleteCmd) {
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(deleteCmd);
                        }
                        endTable();
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                }

                if (tilemapTilesButtonGroups[groupStr]) {
                    propertyRow(RowPropertyType::String, cpType, prefix + ".name", "Name", sceneProject, entities);
                    propertyRow(RowPropertyType::Int, cpType, prefix + ".rectId", "Rect ID", sceneProject, entities, settingsInt);
                    propertyRow(RowPropertyType::Vector2, cpType, prefix + ".position", "Position", sceneProject, entities, settingsTileFloat);
                    propertyRow(RowPropertyType::Float, cpType, prefix + ".width", "Width", sceneProject, entities, settingsTileFloat);
                    propertyRow(RowPropertyType::Float, cpType, prefix + ".height", "Height", sceneProject, entities, settingsTileFloat);
                }

                endTable();
                ImGui::PopID();
            }
        }
    }
}

void editor::Properties::drawTerrainComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entities[0]);

    RowSettings textureSettings;
    textureSettings.secondColSize = -1;

    ImGui::SeparatorText("Maps");
    beginTable(cpType, getLabelSize("Blend Map"), "terrain_maps");
    propertyRow(RowPropertyType::Texture, cpType, "heightMap", "Height Map", sceneProject, entities, textureSettings);
    propertyRow(RowPropertyType::Texture, cpType, "blendMap", "Blend Map", sceneProject, entities, textureSettings);
    endTable();

    if (entities.size() == 1){
        if (ImGui::Button(ICON_FA_MOUNTAIN " Open Terrain Editor")){
            if (TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow()){
                terrainEditWindow->openForEntity(entities[0], sceneProject->id);
            }
        }
    }

    ImGui::SeparatorText("Details");
    beginTable(cpType, getLabelSize("Detail Green"), "terrain_details");
    propertyRow(RowPropertyType::Texture, cpType, "textureDetailRed", "Detail Red", sceneProject, entities, textureSettings);
    propertyRow(RowPropertyType::Texture, cpType, "textureDetailGreen", "Detail Green", sceneProject, entities, textureSettings);
    propertyRow(RowPropertyType::Texture, cpType, "textureDetailBlue", "Detail Blue", sceneProject, entities, textureSettings);
    endTable();

    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsResolution = settingsFloat;
    settingsResolution.stepSize = 1.0f;
    settingsResolution.format = "%.0f";

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    ImGui::SeparatorText("Shape");
    beginTable(cpType, getLabelSize("Root Grid Size"), "terrain_shape");
    propertyRow(RowPropertyType::FloatPositive, cpType, "terrainSize", "Size", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::FloatPositive, cpType, "maxHeight", "Max Height", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::FloatPositive, cpType, "resolution", "Resolution", sceneProject, entities, settingsResolution);
    propertyRow(RowPropertyType::Int, cpType, "rootGridSize", "Root Grid Size", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "levels", "Levels", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Vector2, cpType, "offset", "Offset", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Texturing");
    beginTable(cpType, getLabelSize("Detail Tiles"), "terrain_tiling");
    propertyRow(RowPropertyType::FloatPositive, cpType, "textureBaseTiles", "Base Tiles", sceneProject, entities, settingsResolution);
    propertyRow(RowPropertyType::FloatPositive, cpType, "textureDetailTiles", "Detail Tiles", sceneProject, entities, settingsResolution);
    endTable();

    ImGui::SeparatorText("Ranges");
    beginTable(cpType, getLabelSize("Auto Ranges"), "terrain_ranges_auto");
    propertyRow(RowPropertyType::Bool, cpType, "autoSetRanges", "Auto Ranges", sceneProject, entities);
    endTable();

    if (!terrain.autoSetRanges) {
        drawTrackValues<TerrainComponent, float>(cpType, sceneProject, entities, RowPropertyType::FloatPositive, 0.0f, "terrain", &TerrainComponent::ranges, "ranges");
    }
}

void editor::Properties::drawLightComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    LightComponent& light = sceneProject->scene->getComponent<LightComponent>(entities[0]);

    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsCone;
    settingsCone.secondColSize = 6 * ImGui::GetFontSize();
    settingsCone.format = "%.1f";

    RowSettings settingsLightType;
    settingsLightType.enumEntries = &entriesLightType;

    beginTable(cpType, getLabelSize("Inner cone"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, settingsLightType);
    propertyRow(RowPropertyType::Float, cpType, "intensity", "Intensity", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "range", "Range", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Color3L, cpType, "color", "Color", sceneProject, entities);
    if (light.type != LightType::POINT){
        propertyRow(RowPropertyType::Direction, cpType, "direction", "Direction", sceneProject, entities);
    }
    if (light.type == LightType::SPOT){
        propertyRow(RowPropertyType::HalfCone, cpType, "innerConeCos", "Inner Cone", sceneProject, entities, settingsCone);
        propertyRow(RowPropertyType::HalfCone, cpType, "outerConeCos", "Outer Cone", sceneProject, entities, settingsCone);
    }
    endTable();

    ImGui::SeparatorText("Shadow settings");

    RowSettings settingsBias;
    settingsBias.stepSize = 0.000001f;
    settingsBias.secondColSize = 6 * ImGui::GetFontSize();
    settingsBias.format = "%.6f";

    RowSettings settingsMapRes;
    settingsMapRes.sliderValues = &po2Values;

    RowSettings settingsCascade;
    settingsCascade.sliderValues = &cascadeValues;

    beginTable(cpType, getLabelSize("Map Resolution"), "shadow_settings_table");
    propertyRow(RowPropertyType::Bool, cpType, "shadows", "Enabled", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "shadowBias", "Bias", sceneProject, entities, settingsBias);
    propertyRow(RowPropertyType::UIntSlider, cpType, "mapResolution", "Map Resolution", sceneProject, entities, settingsMapRes);
    propertyRow(RowPropertyType::UIntSlider, cpType, "numShadowCascades", "Num Cascades", sceneProject, entities, settingsCascade);

    propertyHeader("Shadow Camera");
    if (ImGui::Button(ICON_FA_GEAR)){
        ImGui::OpenPopup("menusettings_shadow_camera");
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(14 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_shadow_camera")){
        ImGui::Text("Shadow camera settings");
        ImGui::Separator();

        RowSettings settingsFloat;
        settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

        beginTable(cpType, getLabelSize("Camera Near"), "shadow_camera_popup");

        propertyRow(RowPropertyType::Bool, cpType, "automaticShadowCamera", "Automatic", sceneProject, entities);
        ImGui::BeginDisabled(light.automaticShadowCamera);
        propertyRow(RowPropertyType::Float, cpType, "shadowCameraNear", "Camera Near", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "shadowCameraFar", "Camera Far", sceneProject, entities, settingsFloat);
        ImGui::EndDisabled();

        endTable();

        ImGui::EndPopup();
    }
    endTable();
}

void editor::Properties::drawScriptComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    if (entities.empty()) return;

    ScriptComponent& firstScriptComp = sceneProject->scene->getComponent<ScriptComponent>(entities[0]);

    // Identify common scripts by index
    std::vector<size_t> commonIndices;
    for (size_t i = 0; i < firstScriptComp.scripts.size(); i++) {
        const ScriptEntry& refScript = firstScriptComp.scripts[i];
        bool isCommon = true;
        for (size_t e = 1; e < entities.size(); e++) {
            ScriptComponent& otherScriptComp = sceneProject->scene->getComponent<ScriptComponent>(entities[e]);
            if (i >= otherScriptComp.scripts.size()) {
                isCommon = false;
                break;
            }
            const ScriptEntry& otherScript = otherScriptComp.scripts[i];
            if (refScript.type != otherScript.type || 
                refScript.className != otherScript.className || 
                refScript.path != otherScript.path) {
                isCommon = false;
                break;
            }
        }
        if (isCommon) {
            commonIndices.push_back(i);
        }
    }

    if (commonIndices.empty()) {
        if (entities.size() > 1) {
            ImGui::TextDisabled("Select a single entity to view script details");
        }
        return;
    }

    bool removedScriptThisFrame = false;

    for (size_t scriptIdx : commonIndices) {
        if (removedScriptThisFrame) break; // Avoid using invalidated references after removal

        ScriptEntry& script = firstScriptComp.scripts[scriptIdx];

        ImGui::PushID(static_cast<int>(scriptIdx));

        std::string scriptLabel = script.className.empty() ? "Unnamed Script" : script.className;
        std::string typeLabel;
        if (script.type == ScriptType::SUBCLASS) {
            typeLabel = " [Subclass]";
        } else if (script.type == ScriptType::SCRIPT_CLASS) {
            typeLabel = " [C++]";
        } else if (script.type == ScriptType::SCRIPT_LUA) {
            typeLabel = " [Lua]";
        }

        const float indentation = 10.0f;

        // Indent to show scripts are nested inside ScriptComponent
        ImGui::Indent(indentation);

        // Custom styling for script headers
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.35f, 0.35f, 0.4f, 1.0f));

        // Add icon to distinguish from component headers
        std::string headerText = ICON_FA_FILE_CODE " " + scriptLabel + typeLabel;

        bool headerOpen = ImGui::CollapsingHeader(headerText.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        // Right-click menu on the script header
        if (ImGui::BeginPopupContextItem(("script_options_menu_" + std::to_string(scriptIdx)).c_str())) {
            ImGui::TextDisabled("Script options");
            ImGui::Separator();

            bool canMoveUp = scriptIdx > 0;
            if (ImGui::MenuItem(ICON_FA_ARROW_UP " Move Up", nullptr, false, canMoveUp)) {
                for (const Entity& entity : entities) {
                    ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                    std::vector<ScriptEntry> newScripts = sc.scripts;
                    if (scriptIdx < newScripts.size() && scriptIdx > 0) {
                        std::swap(newScripts[scriptIdx], newScripts[scriptIdx - 1]);

                        project->updateScriptProperties(sceneProject, entity, newScripts);

                        cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                }
                if (cmd) cmd->setNoMerge();
            }

            bool canMoveDown = scriptIdx < firstScriptComp.scripts.size() - 1;
            if (ImGui::MenuItem(ICON_FA_ARROW_DOWN " Move Down", nullptr, false, canMoveDown)) {
                for (const Entity& entity : entities) {
                    ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                    std::vector<ScriptEntry> newScripts = sc.scripts;
                    if (scriptIdx < newScripts.size() - 1) {
                        std::swap(newScripts[scriptIdx], newScripts[scriptIdx + 1]);

                        project->updateScriptProperties(sceneProject, entity, newScripts);

                        cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                }
                if (cmd) cmd->setNoMerge();
            }

            if (ImGui::MenuItem(ICON_FA_TRASH " Remove")) {
                // Remove this script entry from all selected entities
                for (const Entity& entity : entities) {
                    ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                    std::vector<ScriptEntry> newScripts = sc.scripts;
                    if (scriptIdx < newScripts.size()) {
                        newScripts.erase(newScripts.begin() + scriptIdx);

                        // Refresh parsed properties for remaining scripts
                        project->updateScriptProperties(sceneProject, entity, newScripts);

                        // Apply change through command system
                        cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                }
                if (cmd) cmd->setNoMerge();

                removedScriptThisFrame = true;
            }

            ImGui::EndPopup();
        }

        if (headerOpen && !removedScriptThisFrame) {
            ImGui::Unindent(indentation); // Unindent for content

            beginTable(cpType, getLabelSize("Script", false), "script_" + std::to_string(scriptIdx));

            // Path and enabled status
            propertyHeader("Script");

            // Enabled Checkbox Logic
            bool allEnabled = true;
            bool anyEnabled = false;
            for (const Entity& e : entities) {
                bool en = sceneProject->scene->getComponent<ScriptComponent>(e).scripts[scriptIdx].enabled;
                if (en) anyEnabled = true;
                else allEnabled = false;
            }

            bool enabled = allEnabled;
            bool mixed = anyEnabled && !allEnabled;

            if (mixed) ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::Checkbox(("##script_enabled_" + std::to_string(scriptIdx)).c_str(), &enabled);
            if (mixed) ImGui::PopStyleColor();

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string propName = "scripts[" + std::to_string(scriptIdx) + "].enabled";
                for (const Entity& entity : entities) {
                    cmd = new PropertyCmd<bool>(project, sceneProject->id, entity, ComponentType::ScriptComponent, propName, enabled);
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                }
                if (cmd) cmd->setNoMerge();
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(script.className.c_str());

            //ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 3.0, ImGui::GetStyle().FramePadding.y / 2.0));

            if (ImGui::Button((ICON_FA_PEN_TO_SQUARE "##edit_name_" + std::to_string(scriptIdx)).c_str())) {
                ImGui::OpenPopup(("Edit Script##" + std::to_string(scriptIdx)).c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Edit class name");
            }

            ImGui::PopStyleVar();

            ImGui::SetNextWindowSizeConstraints(ImVec2(23 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
            if (ImGui::BeginPopup(("Edit Script##" + std::to_string(scriptIdx)).c_str())) {
                ImGui::Text("Edit Script Details");
                ImGui::Separator();

                static char nameBuffer[128];
                static char sourceBuffer[256];
                static char headerBuffer[256];

                std::filesystem::path projectPath = project->getProjectPath();
                std::filesystem::path srcPath(script.path);
                std::filesystem::path hdrPath(script.headerPath);
                if (ImGui::IsWindowAppearing()) {
                    strncpy(nameBuffer, script.className.c_str(), sizeof(nameBuffer) - 1);
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

                    strncpy(sourceBuffer, srcPath.filename().string().c_str(), sizeof(sourceBuffer) - 1);
                    sourceBuffer[sizeof(sourceBuffer) - 1] = '\0';

                    strncpy(headerBuffer, hdrPath.filename().string().c_str(), sizeof(headerBuffer) - 1);
                    headerBuffer[sizeof(headerBuffer) - 1] = '\0';
                }

                bool changed = false;
                float secondColSize = 15 * ImGui::GetFontSize();

                beginTable(cpType, getLabelSize("Source Path"), "edit_script_details");

                // Class Name
                propertyHeader("Class Name", secondColSize);
                if (ImGui::InputText("##new_name", nameBuffer, sizeof(nameBuffer))) {
                    // Edit happening
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    changed = true;
                }

                // Source Path
                propertyHeader("Source File", secondColSize);
                //ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                float buttonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x;
                ImGui::SetNextItemWidth(secondColSize - buttonWidth - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x * 2.0);

                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::InputText("##new_source", sourceBuffer, sizeof(sourceBuffer), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", script.path.c_str());
                }

                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FOLDER_OPEN "##source_btn")) {
                    fs::path fullSrcPath(script.path);
                    if (fullSrcPath.is_relative()) fullSrcPath = projectPath / fullSrcPath;
                    std::string selected = FileDialogs::openFileDialog(fullSrcPath.parent_path().string());
                    if (!selected.empty()) {
                        std::filesystem::path p(selected);
                        std::error_code ec;
                        std::filesystem::path rel = std::filesystem::relative(p, projectPath, ec);
                        if (!ec && rel.string().find("..") == std::string::npos) {
                            srcPath = rel;
                            strncpy(sourceBuffer, rel.filename().string().c_str(), sizeof(sourceBuffer) - 1);
                            changed = true;
                        } else {
                            Backend::getApp().registerAlert("Error", "File must be inside project directory.");
                        }
                    }
                }
                //ImGui::PopStyleVar();

                // Header Path
                if (script.type != ScriptType::SCRIPT_LUA) {
                    propertyHeader("Header File", secondColSize);
                    //ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                    ImGui::SetNextItemWidth(secondColSize - buttonWidth - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x * 2.0);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::InputText("##new_header", headerBuffer, sizeof(headerBuffer), ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", script.headerPath.c_str());
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_FOLDER_OPEN "##header_btn")) {
                        fs::path fullHdrPath(script.headerPath);
                        if (fullHdrPath.is_relative()) fullHdrPath = projectPath / fullHdrPath;
                        std::string selected = FileDialogs::openFileDialog(fullHdrPath.parent_path().string());
                        if (!selected.empty()) {
                            std::filesystem::path p(selected);
                            std::error_code ec;
                            std::filesystem::path rel = std::filesystem::relative(p, projectPath, ec);
                            if (!ec && rel.string().find("..") == std::string::npos) {
                                hdrPath = rel;
                                strncpy(headerBuffer, rel.filename().string().c_str(), sizeof(headerBuffer) - 1);
                                changed = true;
                            } else {
                                Backend::getApp().registerAlert("Error", "File must be inside project directory.");
                            }
                        }
                    }
                    //ImGui::PopStyleVar();
                }

                endTable();

                if (changed) {
                    std::string newName = nameBuffer;
                    std::string newSource = srcPath.string();
                    std::string newHeader = hdrPath.string();

                    for (const Entity& entity : entities) {
                        ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                        std::vector<ScriptEntry> newScripts = sc.scripts;
                        if (scriptIdx < newScripts.size()) {
                            newScripts[scriptIdx].className = newName;
                            newScripts[scriptIdx].path = newSource;
                            if (script.type != ScriptType::SCRIPT_LUA) {
                                newScripts[scriptIdx].headerPath = newHeader;
                            }
 
                            project->updateScriptProperties(sceneProject, entity, newScripts);

                            cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                        }
                    }
                    if (cmd) cmd->setNoMerge();
                }

                ImGui::EndPopup();
            }

            ImGui::SameLine();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 3.0, ImGui::GetStyle().FramePadding.y / 2.0));

            // Resolve header path (if present)
            std::filesystem::path projectPath = project->getProjectPath();
            std::filesystem::path hdrPath = script.headerPath;
            bool hasHeader = !script.headerPath.empty();
            if (hasHeader) {
                if (hdrPath.is_relative()) hdrPath = projectPath / hdrPath;
            }
            bool hdrExists = hasHeader && std::filesystem::exists(hdrPath);
            ImGui::BeginDisabled(!hdrExists);
            if (ImGui::Button((ICON_FA_FILE_LINES "##open_hdr_" + std::to_string(scriptIdx)).c_str())) {
                Backend::getApp().getCodeEditor()->openFile(hdrPath.string());
            }
            if (ImGui::IsItemHovered()) {
                if (hdrExists){
                    ImGui::SetTooltip("Header: %s", script.headerPath.c_str());
                }
                else ImGui::SetTooltip("Header file not found");
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            // Resolve source path
            std::filesystem::path srcPath = script.path;
            if (srcPath.is_relative()) srcPath = projectPath / srcPath;
            bool srcExists = std::filesystem::exists(srcPath);
            ImGui::BeginDisabled(!srcExists);
            if (ImGui::Button((ICON_FA_FILE_CODE "##open_src_" + std::to_string(scriptIdx)).c_str())) {
                Backend::getApp().getCodeEditor()->openFile(srcPath.string());
            }
            if (ImGui::IsItemHovered()) {
                if (srcExists){
                    ImGui::SetTooltip("Source: %s", script.path.c_str());
                }
                else ImGui::SetTooltip("Source file not found");
            }
            ImGui::EndDisabled();

            ImGui::PopStyleVar();

            endTable();

            // Display script properties if available
            if (!script.properties.empty()) {
                ImGui::SeparatorText("Properties");

                ImGui::BeginDisabled(!enabled && !mixed);

                // Compute dynamic first column width based on longest property label
                float maxPropLabelSize = 0;
                for (const ScriptProperty& prop : script.properties) {
                    std::string displayName = prop.displayName.empty() ? prop.name : prop.displayName;
                    float size = getLabelSize(displayName);
                    if (size > maxPropLabelSize)
                        maxPropLabelSize = size;
                }

                beginTable(cpType, maxPropLabelSize, "script_properties_" + std::to_string(scriptIdx));

                for (size_t propIdx = 0; propIdx < script.properties.size(); propIdx++) {
                    ScriptProperty& prop = script.properties[propIdx];

                    std::string propertyId = "scripts[" + std::to_string(scriptIdx) + "]." + prop.name;
                    std::string displayName = prop.displayName.empty() ? prop.name : prop.displayName;

                    RowPropertyType propType = scriptPropertyTypeToRowPropertyType(prop.type);

                    RowSettings propSettings;
                    propSettings.onValueChanged = [this, sceneProject, entities, scriptIdx, propName = prop.name]() {
                        for (const Entity& e : entities) {
                            ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(e);
                            if (scriptIdx < sc.scripts.size()) {
                                ScriptEntry& se = sc.scripts[scriptIdx];
                                for (ScriptProperty& sp : se.properties) {
                                    if (sp.name == propName) {
                                        sp.syncToMember();
                                        break;
                                    }
                                }
                            }
                        }
                    };

                    propertyRow(propType, cpType, propertyId, displayName, sceneProject, entities, propSettings);
                }

                endTable();

                ImGui::EndDisabled();
            }

            ImGui::Indent(indentation); // Re-indent after content
        }

        ImGui::PopStyleColor(3);
        ImGui::Unindent(indentation);

        ImGui::PopID();
    }
}

void editor::Properties::drawSkyComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Default sky"));

    bool allDefault = true;
    for (auto& entity : entities){
        PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, "texture");
        Texture* tex = static_cast<Texture*>(prop.ref);
        if (tex->getId() != "editor:resources:default_sky") {
            allDefault = false;
            break;
        }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Default sky");
    ImGui::TableNextColumn();

    if (allDefault) ImGui::BeginDisabled();
    if (ImGui::Button("Apply")){
        for (auto& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, "texture");
            Texture newTex;

            ProjectUtils::setDefaultSkyTexture(newTex);

            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, "texture", newTex, nullptr);
            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
        }
    }
    if (allDefault) ImGui::EndDisabled();

    endTable();

    ImGui::SeparatorText("Sky settings");

    beginTable(cpType, getLabelSize("Rotation"));

    propertyRow(RowPropertyType::TextureCube, cpType, "texture", "Texture", sceneProject, entities);
    propertyRow(RowPropertyType::Color4L, cpType, "color", "Color", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "rotation", "Rotation", sceneProject, entities);

    endTable();
}

void editor::Properties::drawInstancedMeshComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsUInt;
    settingsUInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Cylindrical Billboard"));
    propertyRow(RowPropertyType::UInt, cpType, "maxInstances", "Max Instances", sceneProject, entities, settingsUInt);
    propertyRow(RowPropertyType::Bool, cpType, "instancedBillboard", "Billboard", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "instancedCylindricalBillboard", "Cylindrical Billboard", sceneProject, entities);
    endTable();

    if (entities.size() != 1) {
        ImGui::SeparatorText("Instances");
        ImGui::TextDisabled("Select a single entity to edit instances");
        return;
    }

    Entity entity = entities[0];
    InstancedMeshComponent& instmesh = sceneProject->scene->getComponent<InstancedMeshComponent>(entity);

    ImGui::SeparatorText("Instances");

    if (ImGui::Button(ICON_FA_PLUS " Add Instance", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (InstancedMeshComponent* instComp = sceneProject->scene->findComponent<InstancedMeshComponent>(selectedEntity)) {
                std::vector<InstanceData> newInstances = instComp->instances;
                InstanceData newInst;
                if (!newInstances.empty()) {
                    newInst = newInstances.back();
                }
                newInstances.push_back(newInst);
                multiCmd->addPropertyCmd<std::vector<InstanceData>>(project, sceneProject->id, selectedEntity, cpType, "instances", newInstances);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    beginTable(cpType, getLabelSize("Instances"), "instanced_mesh_instances_header");
    propertyHeader("Instances", -1, false, false);
    ImGui::Text("%zu", instmesh.instances.size());
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_instances", instancesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        instancesExpanded = !instancesExpanded;
    }
    endTable();

    if (!instancesExpanded || instmesh.instances.empty()) {
        return;
    }

    bool removedInstance = false;

    for (size_t i = 0; i < instmesh.instances.size(); i++) {
        ImGui::PushID((int)i);

        std::string instanceGroupStr = "instance_" + std::to_string(i);
        std::string instanceLabel = "[" + std::to_string(i) + "] Instance " + std::to_string(i);

        bool isInstanceSelected = sceneProject->sceneRender &&
            sceneProject->sceneRender->getSelectedInstanceIndex() == (int)i &&
            sceneProject->sceneRender->getSelectedInstanceEntity() == entities[0];

        if (isInstanceSelected) {
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            float width = ImGui::GetContentRegionAvail().x;
            float height = ImGui::GetFrameHeight();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec4 highlightColor = ImGui::GetStyle().Colors[ImGuiCol_HeaderActive];
            highlightColor.w = 0.35f;
            drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + width, cursorPos.y + height), ImGui::GetColorU32(highlightColor), 3.0f);
        }
        ImGui::SeparatorText(instanceLabel.c_str());

        beginTable(cpType, getLabelSize("Position"), instanceGroupStr);
        propertyHeader("Instance", -1, false, false);

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        float arrowButtonWidth = ImGui::GetFrameHeight();
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonWidth;
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (ImGui::ArrowButton("##toggle_instance", instancesButtonGroups[instanceGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            instancesButtonGroups[instanceGroupStr] = !instancesButtonGroups[instanceGroupStr];
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_instance", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (InstancedMeshComponent* instComp = sceneProject->scene->findComponent<InstancedMeshComponent>(selectedEntity)) {
                    if (i < instComp->instances.size()) {
                        std::vector<InstanceData> newInstances = instComp->instances;
                        newInstances.erase(newInstances.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<InstanceData>>(project, sceneProject->id, selectedEntity, cpType, "instances", newInstances);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            removedInstance = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            endTable();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (instancesButtonGroups[instanceGroupStr]) {
            std::string propPrefix = "instances[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".position", "Position", sceneProject, entities);
            propertyRow(RowPropertyType::Quat, cpType, propPrefix + ".rotation", "Rotation", sceneProject, entities);
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".scale", "Scale", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".color", "Color", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, propPrefix + ".visible", "Visible", sceneProject, entities);
        }

        endTable();
        ImGui::PopID();
    }

    if (removedInstance) {
        return;
    }
}

void editor::Properties::drawParticlesComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsInt;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Max Per Update"));
    propertyRow(RowPropertyType::UInt, cpType, "maxParticles", "Max Particles", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Bool, cpType, "emitter", "Emitter", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    propertyRow(RowPropertyType::Int, cpType, "rate", "Rate", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "maxPerUpdate", "Max Per Update", sceneProject, entities, settingsInt);
    endTable();

    ImGui::SeparatorText("Life");
    beginTable(cpType, getLabelSize("Max Life"), "life_table");
    propertyRow(RowPropertyType::Float, cpType, "lifeInitializer.minLife", "Min Life", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "lifeInitializer.maxLife", "Max Life", sceneProject, entities, settingsFloat);
    endTable();

    ImGui::SeparatorText("Position");
    beginTable(cpType, getLabelSize("From Position"), "position_table");
    propertyRow(RowPropertyType::Vector3, cpType, "positionInitializer.minPosition", "Min Position", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "positionInitializer.maxPosition", "Max Position", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "positionModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "positionModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Vector3, cpType, "positionModifier.fromPosition", "From Position", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "positionModifier.toPosition", "To Position", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Velocity");
    beginTable(cpType, getLabelSize("From Velocity"), "velocity_table");
    propertyRow(RowPropertyType::Vector3, cpType, "velocityInitializer.minVelocity", "Min Velocity", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "velocityInitializer.maxVelocity", "Max Velocity", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "velocityModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "velocityModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Vector3, cpType, "velocityModifier.fromVelocity", "From Velocity", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "velocityModifier.toVelocity", "To Velocity", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Acceleration");
    beginTable(cpType, getLabelSize("From Acceleration"), "acceleration_table");
    propertyRow(RowPropertyType::Vector3, cpType, "accelerationInitializer.minAcceleration", "Min Acceleration", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "accelerationInitializer.maxAcceleration", "Max Acceleration", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "accelerationModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "accelerationModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Vector3, cpType, "accelerationModifier.fromAcceleration", "From Acceleration", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "accelerationModifier.toAcceleration", "To Acceleration", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Color");
    beginTable(cpType, getLabelSize("From Color"), "color_table");
    propertyRow(RowPropertyType::Color3L, cpType, "colorInitializer.minColor", "Min Color", sceneProject, entities);
    propertyRow(RowPropertyType::Color3L, cpType, "colorInitializer.maxColor", "Max Color", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "colorInitializer.useSRGB", "Use sRGB", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "colorModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "colorModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Color3L, cpType, "colorModifier.fromColor", "From Color", sceneProject, entities);
    propertyRow(RowPropertyType::Color3L, cpType, "colorModifier.toColor", "To Color", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "colorModifier.useSRGB", "Mod sRGB", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Alpha");
    beginTable(cpType, getLabelSize("From Alpha"), "alpha_table");
    propertyRow(RowPropertyType::Float, cpType, "alphaInitializer.minAlpha", "Min Alpha", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "alphaInitializer.maxAlpha", "Max Alpha", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "alphaModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "alphaModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "alphaModifier.fromAlpha", "From Alpha", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "alphaModifier.toAlpha", "To Alpha", sceneProject, entities, settingsFloat);
    endTable();

    ImGui::SeparatorText("Size");
    beginTable(cpType, getLabelSize("From Size"), "size_table");
    propertyRow(RowPropertyType::Float, cpType, "sizeInitializer.minSize", "Min Size", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "sizeInitializer.maxSize", "Max Size", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "sizeModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "sizeModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "sizeModifier.fromSize", "From Size", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "sizeModifier.toSize", "To Size", sceneProject, entities, settingsFloat);
    endTable();

    ImGui::SeparatorText("Rotation");
    beginTable(cpType, getLabelSize("Shortest Path"), "rotation_table");
    propertyRow(RowPropertyType::Quat, cpType, "rotationInitializer.minRotation", "Min Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Quat, cpType, "rotationInitializer.maxRotation", "Max Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "rotationInitializer.shortestPath", "Shortest Path", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "rotationModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "rotationModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Quat, cpType, "rotationModifier.fromRotation", "From Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Quat, cpType, "rotationModifier.toRotation", "To Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "rotationModifier.shortestPath", "Mod Shortest", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Scale");
    beginTable(cpType, getLabelSize("From Scale"), "scale_table");
    propertyRow(RowPropertyType::Vector3, cpType, "scaleInitializer.minScale", "Min Scale", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "scaleInitializer.maxScale", "Max Scale", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "scaleModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "scaleModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Vector3, cpType, "scaleModifier.fromScale", "From Scale", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "scaleModifier.toScale", "To Scale", sceneProject, entities);
    endTable();
}

void editor::Properties::drawPointsComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Transparent"));
    propertyRow(RowPropertyType::Texture, cpType, "texture", "Texture", sceneProject, entities);

    RowSettings transparencySettings;
    transparencySettings.help = "Just for render ordering";
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "transparent", "Transparent", "autoTransparency", "Auto Transparency", sceneProject, entities, transparencySettings);

    propertyRow(RowPropertyType::UInt, cpType, "maxPoints", "Max Points", sceneProject, entities, settingsInt);
    endTable();

    if (entities.size() != 1) {
        ImGui::SeparatorText("Points");
        ImGui::TextDisabled("Select a single entity to edit points");
        return;
    }

    Entity entity = entities[0];
    PointsComponent& pts = sceneProject->scene->getComponent<PointsComponent>(entity);

    ImGui::SeparatorText("Points");

    if (ImGui::Button(ICON_FA_PLUS " Add Point", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (PointsComponent* ptsComp = sceneProject->scene->findComponent<PointsComponent>(selectedEntity)) {
                std::vector<PointData> newPoints = ptsComp->points;
                PointData newPt;
                if (!newPoints.empty()) {
                    newPt = newPoints.back();
                }
                newPoints.push_back(newPt);
                multiCmd->addPropertyCmd<std::vector<PointData>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    beginTable(cpType, getLabelSize("Points"), "points_header");
    propertyHeader("Points", -1, false, false);
    ImGui::Text("%zu", pts.points.size());
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_points", pointsExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        pointsExpanded = !pointsExpanded;
    }
    endTable();

    if (!pointsExpanded || pts.points.empty()) {
        return;
    }

    bool removedPoint = false;

    for (size_t i = 0; i < pts.points.size(); i++) {
        ImGui::PushID((int)i);

        std::string pointGroupStr = "point_" + std::to_string(i);
        std::string pointLabel = "[" + std::to_string(i) + "] Point " + std::to_string(i);

        ImGui::SeparatorText(pointLabel.c_str());

        beginTable(cpType, getLabelSize("Position"), pointGroupStr);
        propertyHeader("Point", -1, false, false);

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        float arrowButtonWidth = ImGui::GetFrameHeight();
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonWidth;
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (ImGui::ArrowButton("##toggle_point", pointsButtonGroups[pointGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            pointsButtonGroups[pointGroupStr] = !pointsButtonGroups[pointGroupStr];
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_point", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (PointsComponent* ptsComp = sceneProject->scene->findComponent<PointsComponent>(selectedEntity)) {
                    if (i < ptsComp->points.size()) {
                        std::vector<PointData> newPoints = ptsComp->points;
                        newPoints.erase(newPoints.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<PointData>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            removedPoint = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            endTable();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (pointsButtonGroups[pointGroupStr]) {
            std::string propPrefix = "points[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".position", "Position", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".color", "Color", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, propPrefix + ".size", "Size", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, propPrefix + ".rotation", "Rotation", sceneProject, entities);
            propertyRow(RowPropertyType::Vector4, cpType, propPrefix + ".textureRect", "Texture Rect", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, propPrefix + ".visible", "Visible", sceneProject, entities);
        }

        endTable();
        ImGui::PopID();
    }

    if (removedPoint) {
        return;
    }
}

void editor::Properties::drawBody2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Body2DComponent& body = sceneProject->scene->getComponent<Body2DComponent>(entities[0]);

    auto getSizeForShape = [sceneProject](Entity entity, float defaultSize = 100.0f) -> Vector2 {
        float width = defaultSize;
        float height = defaultSize;

        if (SpriteComponent* spriteComp = sceneProject->scene->findComponent<SpriteComponent>(entity)){
            if (spriteComp->width > 0){
                width = (float)spriteComp->width;
            }
            if (spriteComp->height > 0){
                height = (float)spriteComp->height;
            }
        }else if (UILayoutComponent* layoutComp = sceneProject->scene->findComponent<UILayoutComponent>(entity)){
            if (layoutComp->width > 0){
                width = (float)layoutComp->width;
            }
            if (layoutComp->height > 0){
                height = (float)layoutComp->height;
            }
        }

        return Vector2(width, height);
    };

    RowSettings settingsBodyType;
    settingsBodyType.enumEntries = &entriesBodyType;

    beginTable(cpType, getLabelSize("Update Shapes"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Body Type", sceneProject, entities, settingsBodyType);
    endTable();

    ImGui::SeparatorText("Shapes");
    ImGui::Text("Shapes: %zu", body.numShapes);

    static int createShape2DType = (int)Shape2DType::POLYGON;
    const char* createShape2DLabel = "Polygon";
    for (const EnumEntry& entry : entriesShape2DType){
        if (entry.value == createShape2DType){
            createShape2DLabel = entry.name;
            break;
        }
    }

    ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
    if (ImGui::BeginCombo("##shape2d_create_type", createShape2DLabel)){
        for (const EnumEntry& entry : entriesShape2DType){
            bool selected = (createShape2DType == entry.value);
            if (ImGui::Selectable(entry.name, selected)){
                createShape2DType = entry.value;
            }
            if (selected){
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Shape##body2d")){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

        for (Entity entity : entities){
            if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){

                Shape2D shape;
                shape.type = (Shape2DType)createShape2DType;

                switch (shape.type){
                case Shape2DType::POLYGON:
                {
                    Vector2 layoutSize = getSizeForShape(entity, 0.0f);

                    shape.numVertices = 4;

                    if (layoutSize != Vector2::ZERO){
                        shape.pointA = Vector2(0.0f, 0.0f);
                        shape.pointB = Vector2(layoutSize.x, layoutSize.y);
                        shape.vertices[0] = Vector2(0.0f, 0.0f);
                        shape.vertices[1] = Vector2(layoutSize.x, 0.0f);
                        shape.vertices[2] = Vector2(layoutSize.x, layoutSize.y);
                        shape.vertices[3] = Vector2(0.0f, layoutSize.y);
                    }else{
                        layoutSize = Vector2(100.0f, 100.0f);

                        const float halfW = layoutSize.x * 0.5f;
                        const float halfH = layoutSize.y * 0.5f;

                        shape.pointA = Vector2(-halfW, -halfH);
                        shape.pointB = Vector2(halfW, halfH);
                        shape.vertices[0] = Vector2(-halfW, -halfH);
                        shape.vertices[1] = Vector2(halfW, -halfH);
                        shape.vertices[2] = Vector2(halfW, halfH);
                        shape.vertices[3] = Vector2(-halfW, halfH);
                    }

                    break;
                }
                case Shape2DType::CIRCLE:
                    shape.pointA = Vector2::ZERO;
                    shape.radius = 0.5f;
                    break;
                case Shape2DType::CAPSULE:
                    shape.pointA = Vector2(0.0f, -0.5f);
                    shape.pointB = Vector2(0.0f, 0.5f);
                    shape.radius = 0.5f;
                    break;
                case Shape2DType::SEGMENT:
                    shape.pointA = Vector2(-1.0f, 0.0f);
                    shape.pointB = Vector2(1.0f, 0.0f);
                    break;
                case Shape2DType::CHAIN:
                    shape.numVertices = 4;
                    shape.vertices[0] = Vector2(-0.5f, -0.5f);
                    shape.vertices[1] = Vector2(0.5f, -0.5f);
                    shape.vertices[2] = Vector2(0.5f, 0.5f);
                    shape.vertices[3] = Vector2(-0.5f, 0.5f);
                    break;
                }

                size_t shapeIdx = bodyComp->numShapes;
                multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(shapeIdx) + "]", shape);
                multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", shapeIdx + 1);
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    size_t numShapes = body.numShapes;
    for (Entity entity : entities){
        numShapes = std::min(numShapes, sceneProject->scene->getComponent<Body2DComponent>(entity).numShapes);
    }

    RowSettings settingsShapeType;
    settingsShapeType.enumEntries = &entriesShape2DType;

    RowSettings settingsShapeValue;

    for (size_t s = 0; s < numShapes; s++){
        Body2DComponent& bodyRef = sceneProject->scene->getComponent<Body2DComponent>(entities[0]);
        Shape2D& shape = bodyRef.shapes[s];

        ImGui::SeparatorText(("Shape " + std::to_string(s + 1)).c_str());
        ImGui::PushID((int)s);
        if (ImGui::Button("Remove Shape")){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

            for (Entity entity : entities){
                if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                    if (s >= bodyComp->numShapes){
                        continue;
                    }

                    for (size_t i = s + 1; i < bodyComp->numShapes; i++){
                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(i - 1) + "]", bodyComp->shapes[i]);
                    }

                    multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(bodyComp->numShapes - 1) + "]", Shape2D());
                    multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", bodyComp->numShapes - 1);
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();

        std::string shapeKey = "shapes[" + std::to_string(s) + "]";

        beginTable(cpType, getLabelSize("Enable Hit Events"), "body2d_shape");
        propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".type", "Type", sceneProject, entities, settingsShapeType);

        RowSettings settingsFloat;
        //settingsFloat.stepSize = 0.1f;
        settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

        if (shape.type == Shape2DType::POLYGON){
            propertyHeader("Vertices");
            ImGui::Text("%d", shape.numVertices);
            ImGui::SameLine();
            if (ImGui::Button("Add Vertex")){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;
                        Shape2D shapeValue = bodyComp->shapes[s];

                        const uint8_t oldCount = shapeValue.numVertices;
                        if (oldCount >= 2){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                        }else if (oldCount == 1){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector2(10.0f, 0.0f);
                        }else{
                            shapeValue.vertices[oldCount] = Vector2::ZERO;
                        }
                        shapeValue.numVertices = oldCount + 1;
                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            bool removedVertex = false;
            float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
            float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
            ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
            if (inputVerSize.x < 100.0f){
                inputVerSize.x = 100.0f;
            }

            RowSettings settingsVertex = settingsShapeValue;
            settingsVertex.secondColSize = inputVerSize.x;

            for (int v = 0; v < (int)shape.numVertices; v++){

                propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_polygon_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                if (ImGui::Button(removeVertexId.c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;

                            Shape2D shapeValue = bodyComp->shapes[s];
                            if (v >= shapeValue.numVertices) continue;

                            for (size_t i = (size_t)v + 1; i < shapeValue.numVertices; i++){
                                shapeValue.vertices[i - 1] = shapeValue.vertices[i];
                            }
                            shapeValue.numVertices -= 1;

                            multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                    removedVertex = true;
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    break;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Remove vertex");
                }
            }

            if (removedVertex){
                endTable();
                return;
            }

            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsFloat);
        }else if (shape.type == Shape2DType::CIRCLE){
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointA", "Center", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsFloat);
        }else if (shape.type == Shape2DType::CAPSULE){
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointA", "Center A", sceneProject, entities);
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointB", "Center B", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsFloat);
        }else if (shape.type == Shape2DType::SEGMENT){
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointA", "Point A", sceneProject, entities);
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointB", "Point B", sceneProject, entities);
        }else if (shape.type == Shape2DType::CHAIN){
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".loop", "Loop", sceneProject, entities);

            propertyHeader("Vertices");
            ImGui::Text("%d", shape.numVertices);
            ImGui::SameLine();
            if (ImGui::Button("Add Vertex")){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;
                        Shape2D shapeValue = bodyComp->shapes[s];

                        const uint8_t oldCount = shapeValue.numVertices;
                        if (oldCount >= 2){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                        }else if (oldCount == 1){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector2(10.0f, 0.0f);
                        }else{
                            shapeValue.vertices[oldCount] = Vector2::ZERO;
                        }
                        shapeValue.numVertices = oldCount + 1;
                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }

            bool removedVertex = false;
            float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
            float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
            ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
            if (inputVerSize.x < 100.0f){
                inputVerSize.x = 100.0f;
            }

            RowSettings settingsVertex = settingsShapeValue;
            settingsVertex.secondColSize = inputVerSize.x;

            for (int v = 0; v < (int)shape.numVertices; v++){
                propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_chain_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                if (ImGui::Button(removeVertexId.c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;

                            Shape2D shapeValue = bodyComp->shapes[s];
                            if (v >= shapeValue.numVertices) continue;

                            for (size_t i = (size_t)v + 1; i < shapeValue.numVertices; i++){
                                shapeValue.vertices[i - 1] = shapeValue.vertices[i];
                            }
                            shapeValue.numVertices -= 1;

                            multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                    removedVertex = true;
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    break;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Remove vertex");
                }
            }

            if (removedVertex){
                endTable();
                return;
            }
        }

        if (shape.type != Shape2DType::SEGMENT && shape.type != Shape2DType::CHAIN){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".density", "Density", sceneProject, entities, settingsFloat);
        }
        propertyRow(RowPropertyType::Float, cpType, shapeKey + ".friction", "Friction", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, shapeKey + ".restitution", "Restitution", sceneProject, entities, settingsFloat);
        if (shape.type != Shape2DType::CHAIN){
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".enableHitEvents", "Enable Hit Events", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".contactEvents", "Contact Events", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".preSolveEvents", "PreSolve Events", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".sensorEvents", "Sensor Events", sceneProject, entities);
        }
        if (shape.type == Shape2DType::POLYGON){
            ImGui::PushStyleColor(ImGuiCol_Text, App::ThemeColors::SubtleText);
            propertyHeader("Reset shape");
            ImGui::PopStyleColor();
            if (ImGui::SmallButton(("Box##shape_preset_box_" + std::to_string(s)).c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;

                        Shape2D shapeValue = bodyComp->shapes[s];
                        Vector2 layoutSize = getSizeForShape(entity);

                        shapeValue.numVertices = 4;
                        shapeValue.radius = 0.0f;
                        shapeValue.pointA = Vector2(0.0f, 0.0f);
                        shapeValue.pointB = Vector2(layoutSize.x, layoutSize.y);
                        shapeValue.vertices[0] = Vector2(0.0f, 0.0f);
                        shapeValue.vertices[1] = Vector2(layoutSize.x, 0.0f);
                        shapeValue.vertices[2] = Vector2(layoutSize.x, layoutSize.y);
                        shapeValue.vertices[3] = Vector2(0.0f, layoutSize.y);

                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            if (ImGui::SmallButton(("Centered Box##shape_preset_centered_box_" + std::to_string(s)).c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;

                        Shape2D shapeValue = bodyComp->shapes[s];
                        Vector2 layoutSize = getSizeForShape(entity);
                        const float halfW = layoutSize.x * 0.5f;
                        const float halfH = layoutSize.y * 0.5f;

                        shapeValue.numVertices = 4;
                        shapeValue.radius = 0.0f;
                        shapeValue.pointA = Vector2(-halfW, -halfH);
                        shapeValue.pointB = Vector2(halfW, halfH);
                        shapeValue.vertices[0] = Vector2(-halfW, -halfH);
                        shapeValue.vertices[1] = Vector2(halfW, -halfH);
                        shapeValue.vertices[2] = Vector2(halfW, halfH);
                        shapeValue.vertices[3] = Vector2(-halfW, halfH);

                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            if (ImGui::SmallButton(("Rounded Box##shape_preset_rounded_box_" + std::to_string(s)).c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;

                        Shape2D shapeValue = bodyComp->shapes[s];
                        Vector2 layoutSize = getSizeForShape(entity);
                        const float halfW = layoutSize.x * 0.5f;
                        const float halfH = layoutSize.y * 0.5f;

                        shapeValue.numVertices = 4;
                        shapeValue.radius = std::max(1.0f, std::min(layoutSize.x, layoutSize.y) * 0.04f);
                        shapeValue.pointA = Vector2(-halfW, -halfH);
                        shapeValue.pointB = Vector2(halfW, halfH);
                        shapeValue.vertices[0] = Vector2(-halfW, -halfH);
                        shapeValue.vertices[1] = Vector2(halfW, -halfH);
                        shapeValue.vertices[2] = Vector2(halfW, halfH);
                        shapeValue.vertices[3] = Vector2(-halfW, halfH);

                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }
        endTable();
    }
}

void editor::Properties::drawBody3DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Body3DComponent& body = sceneProject->scene->getComponent<Body3DComponent>(entities[0]);

    RowSettings settingsBodyType;
    settingsBodyType.enumEntries = &entriesBodyType;

    RowSettings settingsBodyValue;

    beginTable(cpType, getLabelSize("Override Mass"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Body Type", sceneProject, entities, settingsBodyType);
    propertyRow(RowPropertyType::Bool, cpType, "overrideMassProperties", "Override Mass", sceneProject, entities, settingsBodyValue);

    if (body.overrideMassProperties) {
        propertyRow(RowPropertyType::Vector3, cpType, "solidBoxSize", "Solid Box Size", sceneProject, entities, settingsBodyValue);
        propertyRow(RowPropertyType::Float, cpType, "solidBoxDensity", "Solid Box Density", sceneProject, entities, settingsBodyValue);
    }
    endTable();

    ImGui::SeparatorText("Shapes");
    ImGui::Text("Shapes: %zu", body.numShapes);

    static int createShape3DType = (int)Shape3DType::BOX;
    const char* createShape3DLabel = "Unknown";
    for (const EnumEntry& entry : entriesShape3DType){
        if (entry.value == createShape3DType){
            createShape3DLabel = entry.name;
            break;
        }
    }

    ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
    if (ImGui::BeginCombo("##shape3d_create_type", createShape3DLabel)){
        for (const EnumEntry& entry : entriesShape3DType){
            bool selected = (createShape3DType == entry.value);
            if (ImGui::Selectable(entry.name, selected)){
                createShape3DType = entry.value;
            }
            if (selected){
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Shape##body3d")){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

        for (Entity entity : entities){
            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){

                Shape3D shape;
                shape.type = (Shape3DType)createShape3DType;
                if (shape.type == Shape3DType::MESH){
                    shape.source = Shape3DSource::ENTITY_MESH;
                    if (sceneProject->scene->findComponent<MeshComponent>(entity)){
                        shape.sourceEntity = entity;
                    }
                }else if (shape.type == Shape3DType::CONVEX_HULL){
                    shape.source = Shape3DSource::ENTITY_MESH;
                    if (sceneProject->scene->findComponent<MeshComponent>(entity)){
                        shape.sourceEntity = entity;
                    }
                }else if (shape.type == Shape3DType::HEIGHTFIELD){
                    shape.source = Shape3DSource::ENTITY_HEIGHTFIELD;
                    shape.samplesSize = 256;
                    if (sceneProject->scene->findComponent<TerrainComponent>(entity)){
                        shape.sourceEntity = entity;
                    }
                }

                size_t shapeIdx = bodyComp->numShapes;
                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(shapeIdx) + "]", shape);
                multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", shapeIdx + 1);
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    size_t numShapes = body.numShapes;
    for (Entity entity : entities){
        numShapes = std::min(numShapes, sceneProject->scene->getComponent<Body3DComponent>(entity).numShapes);
    }

    RowSettings settingsShapeSourceConvexHull;
    settingsShapeSourceConvexHull.enumEntries = &entriesShape3DSourceConvexHull;

    RowSettings settingsShapeSourceMesh;
    settingsShapeSourceMesh.enumEntries = &entriesShape3DSourceMesh;

    RowSettings settingsShapeSourceHeightfield;
    settingsShapeSourceHeightfield.enumEntries = &entriesShape3DSourceHeightfield;

    RowSettings settingsShapeValue;

    auto normalizeShape3DSource = [](Shape3DType type, Shape3DSource source) {
        if (type == Shape3DType::CONVEX_HULL){
            if (source != Shape3DSource::RAW_VERTICES && source != Shape3DSource::ENTITY_MESH){
                return Shape3DSource::RAW_VERTICES;
            }
        }else if (type == Shape3DType::MESH){
            if (source != Shape3DSource::RAW_MESH && source != Shape3DSource::ENTITY_MESH){
                return Shape3DSource::RAW_MESH;
            }
        }else if (type == Shape3DType::HEIGHTFIELD){
            if (source != Shape3DSource::ENTITY_HEIGHTFIELD){
                return Shape3DSource::ENTITY_HEIGHTFIELD;
            }
        }

        return source;
    };

    for (size_t s = 0; s < numShapes; s++){
        Body3DComponent& bodyRef = sceneProject->scene->getComponent<Body3DComponent>(entities[0]);
        Shape3D& shape = bodyRef.shapes[s];
        const bool suspiciousSingleShapeOffset = bodyRef.numShapes == 1 && shape.position.length() > 50.0f;

        ImGui::SeparatorText(("Shape " + std::to_string(s + 1)).c_str());
        ImGui::PushID((int)s);
        if (ImGui::Button("Remove Shape")){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

            for (Entity entity : entities){
                if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                    if (s >= bodyComp->numShapes){
                        continue;
                    }

                    for (size_t i = s + 1; i < bodyComp->numShapes; i++){
                        multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(i - 1) + "]", bodyComp->shapes[i]);
                    }

                    multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(bodyComp->numShapes - 1) + "]", Shape3D());
                    multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", bodyComp->numShapes - 1);
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();

        std::string shapeKey = "shapes[" + std::to_string(s) + "]";

        beginTable(cpType, getLabelSize("Bottom Radius"), "body3d_shape");

        PropertyData shapeTypeProp = Catalog::getProperty(sceneProject->scene, entities[0], cpType, shapeKey + ".type");
        int* shapeTypeDef = static_cast<int*>(shapeTypeProp.def);
        int shapeTypeValue = *static_cast<int*>(shapeTypeProp.ref);
        bool shapeTypeMixed = false;
        for (size_t entityIndex = 1; entityIndex < entities.size(); entityIndex++){
            PropertyData otherShapeTypeProp = Catalog::getProperty(sceneProject->scene, entities[entityIndex], cpType, shapeKey + ".type");
            if (*static_cast<int*>(otherShapeTypeProp.ref) != shapeTypeValue){
                shapeTypeMixed = true;
                break;
            }
        }

        int shapeTypeItemCurrent = 0;
        for (size_t i = 0; i < entriesShape3DType.size(); ++i) {
            if (entriesShape3DType[i].value == shapeTypeValue) {
                shapeTypeItemCurrent = static_cast<int>(i);
                break;
            }
        }

        int shapeTypeItemDefault = shapeTypeItemCurrent;
        bool shapeTypeDefChanged = false;
        if (shapeTypeDef){
            for (size_t i = 0; i < entriesShape3DType.size(); ++i) {
                if (entriesShape3DType[i].value == *shapeTypeDef) {
                    shapeTypeItemDefault = static_cast<int>(i);
                    break;
                }
            }
            shapeTypeDefChanged = (shapeTypeItemCurrent != shapeTypeItemDefault);
        }

        if (propertyHeader("Type", -1, shapeTypeDefChanged, false)){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            Shape3DType resetType = static_cast<Shape3DType>(entriesShape3DType[shapeTypeItemDefault].value);

            for (Entity entity : entities){
                if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                    if (s >= bodyComp->numShapes) continue;

                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".type", (int)resetType);

                    Shape3DSource normalizedSource = normalizeShape3DSource(resetType, bodyComp->shapes[s].source);
                    if (normalizedSource != bodyComp->shapes[s].source){
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".source", (int)normalizedSource);
                    }
                    if (bodyComp->shapes[s].sourceEntity == NULL_ENTITY && normalizedSource != Shape3DSource::NONE){
                        multiCmd->addPropertyCmd<Entity>(project, sceneProject->id, entity, cpType, shapeKey + ".sourceEntity", entity);
                    }
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
        }

        std::vector<const char*> shapeTypeNames;
        for (const auto& entry : entriesShape3DType) {
            shapeTypeNames.push_back(entry.name);
        }

        if (shapeTypeMixed)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Combo(("##combo_" + shapeKey + ".type").c_str(), &shapeTypeItemCurrent, shapeTypeNames.data(), static_cast<int>(shapeTypeNames.size()))) {
            Shape3DType newShapeType = static_cast<Shape3DType>(entriesShape3DType[shapeTypeItemCurrent].value);
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

            for (Entity entity : entities){
                if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                    if (s >= bodyComp->numShapes) continue;

                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".type", (int)newShapeType);

                    Shape3DSource normalizedSource = normalizeShape3DSource(newShapeType, bodyComp->shapes[s].source);
                    if (normalizedSource != bodyComp->shapes[s].source){
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".source", (int)normalizedSource);
                    }
                    if (bodyComp->shapes[s].sourceEntity == NULL_ENTITY && normalizedSource != Shape3DSource::NONE){
                        multiCmd->addPropertyCmd<Entity>(project, sceneProject->id, entity, cpType, shapeKey + ".sourceEntity", entity);
                    }
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
        }
        if (shapeTypeMixed)
            ImGui::PopStyleColor();

        Shape3DType shapeTypeUI = sceneProject->scene->getComponent<Body3DComponent>(entities[0]).shapes[s].type;
        Shape3DSource shapeSourceUI = sceneProject->scene->getComponent<Body3DComponent>(entities[0]).shapes[s].source;
        Shape3DSource normalizedSourceUI = normalizeShape3DSource(shapeTypeUI, shapeSourceUI);

        propertyRow(RowPropertyType::Vector3, cpType, shapeKey + ".position", "Position", sceneProject, entities, settingsShapeValue);

        if (suspiciousSingleShapeOffset){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Warning");
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 196, 64, 255));
            ImGui::TextUnformatted("Large local collider offset can destabilize joints.");
            ImGui::PopStyleColor();
            ImGui::SameLine();

            std::string resetButtonId = "Reset##shape3d_pos_reset_" + std::to_string(s);
            if (ImGui::SmallButton(resetButtonId.c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

                for (Entity entity : entities){
                    if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                        if (s >= bodyComp->numShapes){
                            continue;
                        }

                        multiCmd->addPropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, shapeKey + ".position", Vector3::ZERO);
                    }
                }

                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }

        propertyRow(RowPropertyType::Quat, cpType, shapeKey + ".rotation", "Rotation", sceneProject, entities, settingsShapeValue);

        if (shape.type == Shape3DType::BOX){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".width", "Width", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".height", "Height", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".depth", "Depth", sceneProject, entities, settingsShapeValue);
        }else if (shape.type == Shape3DType::SPHERE){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsShapeValue);
        }else if (shape.type == Shape3DType::CAPSULE || shape.type == Shape3DType::CYLINDER){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".halfHeight", "Half Height", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsShapeValue);
        }else if (shape.type == Shape3DType::TAPERED_CAPSULE){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".halfHeight", "Half Height", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".topRadius", "Top Radius", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".bottomRadius", "Bottom Radius", sceneProject, entities, settingsShapeValue);
        }

        if (shapeTypeUI == Shape3DType::CONVEX_HULL){
            propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".source", "Source", sceneProject, entities, settingsShapeSourceConvexHull);
            if (normalizedSourceUI == Shape3DSource::ENTITY_MESH){
                propertyRow(RowPropertyType::LocalEntity, cpType, shapeKey + ".sourceEntity", "Source Entity", sceneProject, entities, settingsShapeValue);
            }else if (normalizedSourceUI == Shape3DSource::RAW_VERTICES){
                propertyHeader("Vertices");
                ImGui::Text("%d", shape.numVertices);
                ImGui::SameLine();
                if (ImGui::Button(("Add Vertex##convex_" + std::to_string(s)).c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;
                            Shape3D shapeValue = bodyComp->shapes[s];
                            const uint16_t oldCount = shapeValue.numVertices;
                            if (oldCount >= 2){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                            }else if (oldCount == 1){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector3(1.0f, 0.0f, 0.0f);
                            }else{
                                shapeValue.vertices[oldCount] = Vector3::ZERO;
                            }
                            shapeValue.numVertices = oldCount + 1;
                            multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }
                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }

                float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
                if (inputVerSize.x < 100.0f){
                    inputVerSize.x = 100.0f;
                }
                RowSettings settingsVertex = settingsShapeValue;
                settingsVertex.secondColSize = inputVerSize.x;

                for (int v = 0; v < (int)shape.numVertices; v++){
                    propertyRow(RowPropertyType::Vector3, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);
                    ImGui::SameLine();

                    std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_convex_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(removeVertexId.c_str())){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (v >= shapeValue.numVertices) continue;
                                for (int vi = v; vi < shapeValue.numVertices - 1; vi++){
                                    shapeValue.vertices[vi] = shapeValue.vertices[vi + 1];
                                }
                                shapeValue.numVertices--;
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove vertex");
                    }
                }
            }
        }else if (shapeTypeUI == Shape3DType::MESH){
            propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".source", "Source", sceneProject, entities, settingsShapeSourceMesh);
            if (normalizedSourceUI == Shape3DSource::ENTITY_MESH){
                propertyRow(RowPropertyType::LocalEntity, cpType, shapeKey + ".sourceEntity", "Source Entity", sceneProject, entities, settingsShapeValue);
            }else if (normalizedSourceUI == Shape3DSource::RAW_MESH){
                // Vertices
                propertyHeader("Vertices");
                ImGui::Text("%d", shape.numVertices);
                ImGui::SameLine();
                if (ImGui::Button(("Add Vertex##mesh_" + std::to_string(s)).c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;
                            Shape3D shapeValue = bodyComp->shapes[s];
                            const uint16_t oldCount = shapeValue.numVertices;
                            if (oldCount >= 2){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                            }else if (oldCount == 1){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector3(1.0f, 0.0f, 0.0f);
                            }else{
                                shapeValue.vertices[oldCount] = Vector3::ZERO;
                            }
                            shapeValue.numVertices = oldCount + 1;
                            multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }
                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }

                float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
                if (inputVerSize.x < 100.0f){
                    inputVerSize.x = 100.0f;
                }
                RowSettings settingsVertex = settingsShapeValue;
                settingsVertex.secondColSize = inputVerSize.x;

                for (int v = 0; v < (int)shape.numVertices; v++){
                    propertyRow(RowPropertyType::Vector3, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);
                    ImGui::SameLine();

                    std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_mesh_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(removeVertexId.c_str())){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (v >= shapeValue.numVertices) continue;
                                for (int vi = v; vi < shapeValue.numVertices - 1; vi++){
                                    shapeValue.vertices[vi] = shapeValue.vertices[vi + 1];
                                }
                                shapeValue.numVertices--;
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove vertex");
                    }
                }

                // Triangles (indices)
                const int triangleCount = shape.numIndices / 3;
                propertyHeader("Triangles");
                ImGui::Text("%d", triangleCount);
                ImGui::SameLine();
                if (ImGui::Button(("Add Triangle##mesh_tri_" + std::to_string(s)).c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;
                            Shape3D shapeValue = bodyComp->shapes[s];
                            const uint16_t idx = shapeValue.numIndices;
                            shapeValue.indices[idx] = 0;
                            shapeValue.indices[idx + 1] = (shapeValue.numVertices > 1) ? (uint16_t)1 : (uint16_t)0;
                            shapeValue.indices[idx + 2] = (shapeValue.numVertices > 2) ? (uint16_t)2 : (uint16_t)0;
                            shapeValue.numIndices = idx + 3;
                            multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }
                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }

                for (int t = 0; t < triangleCount; t++){
                    int baseIdx = t * 3;
                    std::string triLabel = "Triangle " + std::to_string(t + 1);
                    propertyHeader(triLabel);

                    int triIndices[3] = { (int)shape.indices[baseIdx], (int)shape.indices[baseIdx + 1], (int)shape.indices[baseIdx + 2] };
                    std::string triId = "##mesh_tri_" + std::to_string(s) + "_" + std::to_string(t);

                    ImGui::SetNextItemWidth(inputVerSize.x);
                    if (ImGui::InputInt3(triId.c_str(), triIndices)){
                        for (int k = 0; k < 3; k++){
                            triIndices[k] = std::max(0, std::min(triIndices[k], (int)shape.numVertices - 1));
                        }
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (baseIdx + 2 >= shapeValue.numIndices) continue;
                                shapeValue.indices[baseIdx] = (uint16_t)triIndices[0];
                                shapeValue.indices[baseIdx + 1] = (uint16_t)triIndices[1];
                                shapeValue.indices[baseIdx + 2] = (uint16_t)triIndices[2];
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::SameLine();
                    std::string removeTriId = std::string(ICON_FA_TRASH_CAN) + "##remove_mesh_tri_" + std::to_string(s) + "_" + std::to_string(t);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(removeTriId.c_str())){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (baseIdx + 2 >= shapeValue.numIndices) continue;
                                for (int ii = baseIdx; ii < shapeValue.numIndices - 3; ii++){
                                    shapeValue.indices[ii] = shapeValue.indices[ii + 3];
                                }
                                shapeValue.numIndices -= 3;
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove triangle");
                    }
                }
            }
        }else if (shapeTypeUI == Shape3DType::HEIGHTFIELD){
            propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".source", "Source", sceneProject, entities, settingsShapeSourceHeightfield);
            if (normalizedSourceUI == Shape3DSource::ENTITY_HEIGHTFIELD){
                propertyRow(RowPropertyType::LocalEntity, cpType, shapeKey + ".sourceEntity", "Source Entity", sceneProject, entities, settingsShapeValue);
            }
            propertyRow(RowPropertyType::UInt, cpType, shapeKey + ".samplesSize", "Samples Size", sceneProject, entities, settingsShapeValue);
        }

        propertyRow(RowPropertyType::Float, cpType, shapeKey + ".density", "Density", sceneProject, entities, settingsShapeValue);
        endTable();
    }
}

void editor::Properties::drawJoint2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Joint2DComponent& joint = sceneProject->scene->getComponent<Joint2DComponent>(entities[0]);

    auto markJoint2DDirty = [sceneProject, entities](){
        for (Entity entity : entities){
            if (Joint2DComponent* jointComp = sceneProject->scene->findComponent<Joint2DComponent>(entity)){
                jointComp->needUpdateJoint = true;
            }
        }
    };

    RowSettings settingsJointType;
    settingsJointType.enumEntries = &entriesJoint2DType;
    settingsJointType.onValueChanged = markJoint2DDirty;

    RowSettings settingsJointValue;
    settingsJointValue.onValueChanged = markJoint2DDirty;
    settingsJointValue.entityFilter.set(sceneProject->scene->getComponentId<Body2DComponent>());

    beginTable(cpType, getLabelSize("Joint Type"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Joint Type", sceneProject, entities, settingsJointType);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyA", "Body A", sceneProject, entities, settingsJointValue);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyB", "Body B", sceneProject, entities, settingsJointValue);

    endTable();

    bool hasTypeSettings = joint.type != Joint2DType::MOTOR;
    if (!hasTypeSettings){
        return;
    }

    const char* joint2DTableLabel = "Anchor";
    if (joint.type == Joint2DType::DISTANCE){
        joint2DTableLabel = "Anchor A";
    }else if (joint.type == Joint2DType::MOUSE){
        joint2DTableLabel = "Target";
    }

    ImGui::SeparatorText("Joint settings");
    std::string joint2DTypeTableId = "joint2d_type_settings_" + std::to_string((int)joint.type);
    beginTable(cpType, getLabelSize(joint2DTableLabel), joint2DTypeTableId);

    if (joint.type == Joint2DType::DISTANCE){
        propertyRow(RowPropertyType::Bool, cpType, "autoAnchors", "Auto Anchors", sceneProject, entities, settingsJointValue);
        if (!joint.autoAnchors){
            propertyRow(RowPropertyType::Vector2, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
            propertyRow(RowPropertyType::Vector2, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        }
        propertyRow(RowPropertyType::Bool, cpType, "rope", "Rope", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint2DType::REVOLUTE || joint.type == Joint2DType::WELD){
        propertyRow(RowPropertyType::Vector2, cpType, "anchorA", "Anchor", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint2DType::PRISMATIC || joint.type == Joint2DType::WHEEL){
        propertyRow(RowPropertyType::Vector2, cpType, "anchorA", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector2, cpType, "axis", "Axis", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint2DType::MOUSE){
        propertyRow(RowPropertyType::Vector2, cpType, "target", "Target", sceneProject, entities, settingsJointValue);
    }

    endTable();
}

void editor::Properties::drawJoint3DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Joint3DComponent& joint = sceneProject->scene->getComponent<Joint3DComponent>(entities[0]);

    auto markJoint3DDirty = [sceneProject, entities](){
        for (Entity entity : entities){
            if (Joint3DComponent* jointComp = sceneProject->scene->findComponent<Joint3DComponent>(entity)){
                jointComp->needUpdateJoint = true;
            }
        }
    };

    RowSettings settingsJointType;
    settingsJointType.enumEntries = &entriesJoint3DType;
    settingsJointType.onValueChanged = markJoint3DDirty;

    RowSettings settingsJointValue;
    settingsJointValue.onValueChanged = markJoint3DDirty;
    settingsJointValue.entityFilter.set(sceneProject->scene->getComponentId<Body3DComponent>());

    beginTable(cpType, getLabelSize("Joint Type"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Joint Type", sceneProject, entities, settingsJointType);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyA", "Body A", sceneProject, entities, settingsJointValue);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyB", "Body B", sceneProject, entities, settingsJointValue);

    endTable();

    bool hasTypeSettings = joint.type != Joint3DType::FIXED;
    if (!hasTypeSettings){
        return;
    }

    const char* joint3DTableLabel = "Anchor";
    switch (joint.type){
        case Joint3DType::DISTANCE:
            joint3DTableLabel = "Auto Anchors";
            break;
        case Joint3DType::POINT:
            joint3DTableLabel = "Anchor";
            break;
        case Joint3DType::HINGE:
            joint3DTableLabel = "Normal";
            break;
        case Joint3DType::CONE:
            joint3DTableLabel = "Twist Axis";
            break;
        case Joint3DType::PRISMATIC:
            joint3DTableLabel = "Limits Min";
            break;
        case Joint3DType::SWINGTWIST:
            joint3DTableLabel = "Normal HalfCone";
            break;
        case Joint3DType::SIXDOF:
            joint3DTableLabel = "Anchor A";
            break;
        case Joint3DType::GEAR:
            joint3DTableLabel = "Teeth Gear A";
            break;
        case Joint3DType::RACKANDPINON:
            joint3DTableLabel = "Rack Length";
            break;
        case Joint3DType::PULLEY:
            joint3DTableLabel = "Fixed Point A";
            break;
        case Joint3DType::PATH:
            joint3DTableLabel = "Position";
            break;
        default:
            break;
    }

    ImGui::SeparatorText("Joint settings");
    std::string joint3DTypeTableId = "joint3d_type_settings_" + std::to_string((int)joint.type);
    beginTable(cpType, getLabelSize(joint3DTableLabel), joint3DTypeTableId);

    if (joint.type == Joint3DType::DISTANCE){
        propertyRow(RowPropertyType::Bool, cpType, "autoAnchors", "Auto Anchors", sceneProject, entities, settingsJointValue);
        if (!joint.autoAnchors){
            propertyRow(RowPropertyType::Vector3, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
            propertyRow(RowPropertyType::Vector3, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        }
    }else if (joint.type == Joint3DType::POINT){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::HINGE){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "axis", "Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "normal", "Normal", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::CONE){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "twistAxis", "Twist Axis", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::PRISMATIC){
        propertyRow(RowPropertyType::Vector3, cpType, "axis", "Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "limitsMin", "Limits Min", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "limitsMax", "Limits Max", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::SWINGTWIST){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "twistAxis", "Twist Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "planeAxis", "Plane Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "normalHalfConeAngle", "Normal HalfCone", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "planeHalfConeAngle", "Plane HalfCone", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "twistMinAngle", "Twist Min Angle", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "twistMaxAngle", "Twist Max Angle", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::SIXDOF){
        propertyRow(RowPropertyType::Vector3, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "axisX", "Axis X", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "axisY", "Axis Y", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::GEAR){
        propertyRow(RowPropertyType::LocalEntity, cpType, "hingeA", "Hinge A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::LocalEntity, cpType, "hingeB", "Hinge B", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethGearA", "Teeth Gear A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethGearB", "Teeth Gear B", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::RACKANDPINON){
        propertyRow(RowPropertyType::LocalEntity, cpType, "hinge", "Hinge", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::LocalEntity, cpType, "slider", "Slider", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethRack", "Teeth Rack", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethGear", "Teeth Gear", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "rackLength", "Rack Length", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::PULLEY){
        propertyRow(RowPropertyType::Vector3, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "fixedPointA", "Fixed Point A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "fixedPointB", "Fixed Point B", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::PATH){
        propertyRow(RowPropertyType::Vector3, cpType, "pathPosition", "Position", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Bool, cpType, "isLooping", "Looping", sceneProject, entities, settingsJointValue);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Points");
        ImGui::TableSetColumnIndex(1);

        if (ImGui::Button("Add Point")){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (Entity entity : entities){
                if (Joint3DComponent* jointComp = sceneProject->scene->findComponent<Joint3DComponent>(entity)){
                    std::vector<Vector3> newPoints = jointComp->pathPoints;
                    Vector3 newPoint = Vector3::ZERO;
                    if (!newPoints.empty()){
                        Vector3 lastPoint = newPoints.back();
                        newPoint = Vector3(lastPoint.x, lastPoint.y, lastPoint.z + 1.0f);
                    }
                    newPoints.push_back(newPoint);
                    multiCmd->addPropertyCmd<std::vector<Vector3>>(project, sceneProject->id, entity, cpType, "pathPoints", newPoints, settingsJointValue.onValueChanged);
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
        }

        bool removedPoint = false;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
        if (inputSize.x < 100.0f){
            inputSize.x = 100.0f;
        }

        for (size_t pointIndex = 0; pointIndex < joint.pathPoints.size(); pointIndex++){
            std::string pointId = "pathPoints[" + std::to_string(pointIndex) + "]";
            std::string pointLabel = "Point " + std::to_string(pointIndex);

            RowSettings settingsPathPoint = settingsJointValue;
            settingsPathPoint.secondColSize = inputSize.x;
            propertyRow(RowPropertyType::Vector3, cpType, pointId, pointLabel, sceneProject, entities, settingsPathPoint);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
            std::string removePointId = std::string(ICON_FA_TRASH_CAN) + "##remove_path_point_" + std::to_string(pointIndex);
            if (ImGui::Button(removePointId.c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Joint3DComponent* jointComp = sceneProject->scene->findComponent<Joint3DComponent>(entity)){
                        if (pointIndex < jointComp->pathPoints.size()){
                            std::vector<Vector3> newPoints = jointComp->pathPoints;
                            newPoints.erase(newPoints.begin() + (long int)pointIndex);
                            multiCmd->addPropertyCmd<std::vector<Vector3>>(project, sceneProject->id, entity, cpType, "pathPoints", newPoints, settingsJointValue.onValueChanged);
                        }
                    }
                }

                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                removedPoint = true;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                break;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove point");
            }
        }

        if (removedPoint){
            endTable();
            return;
        }
    }

    endTable();
}

void editor::Properties::startActionPreview(Entity entity, Scene* scene, SceneProject* sceneProject) {
    if (actionPreviewing) return;

    actionPreviewStates.clear();
    actionPreviewEntity = entity;
    actionPreviewSceneId = sceneProject->id;

    ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
    if (!actionComp) return;

    // Collect all entities that will be affected by the preview
    std::unordered_set<Entity> collected;

    std::function<void(Entity)> collectEntities = [&](Entity e) {
        if (e == NULL_ENTITY || !scene->isEntityCreated(e) || !collected.insert(e).second) return;

        // Save this entity's state
        ActionPreviewState state;
        state.entity = e;
        if (Transform* transform = scene->findComponent<Transform>(e)) {
            state.parent = transform->parent;
        }
        state.components = Stream::encodeComponents(e, scene, scene->getSignature(e));
        actionPreviewStates.push_back(state);

        // If entity has an ActionComponent with a target, save the target too
        if (ActionComponent* ac = scene->findComponent<ActionComponent>(e)) {
            if (ac->target != NULL_ENTITY && scene->isEntityCreated(ac->target)) {
                collectEntities(ac->target);
            }
        }

        // If entity has an AnimationComponent, collect child action entities and their targets
        if (AnimationComponent* anim = scene->findComponent<AnimationComponent>(e)) {
            for (const ActionFrame& frame : anim->actions) {
                if (frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)) {
                    collectEntities(frame.action);
                }
            }
        }
    };

    collectEntities(entity);

    actionComp->state = ActionState::Stopped;
    actionComp->timecount = 0;
    actionComp->stopTrigger = false;
    actionComp->pauseTrigger = false;
    actionComp->startTrigger = true;

    actionPreviewing = true;
}

void editor::Properties::stopActionPreview(Scene* scene, SceneProject* sceneProject) {
    if (!actionPreviewing) return;

    // Update snapshots with current user-editable config values before restoring
    for (ActionPreviewState& state : actionPreviewStates) {
        if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity) || !state.components || state.components.IsNull()) {
            continue;
        }
        if (auto* comp = scene->findComponent<TimedActionComponent>(state.entity)) {
            state.components[Catalog::getComponentName(ComponentType::TimedActionComponent, true)] = Stream::encodeTimedActionComponent(*comp);
        }
        if (auto* comp = scene->findComponent<PositionActionComponent>(state.entity)) {
            state.components[Catalog::getComponentName(ComponentType::PositionActionComponent, true)] = Stream::encodePositionActionComponent(*comp);
        }
        if (auto* comp = scene->findComponent<RotationActionComponent>(state.entity)) {
            state.components[Catalog::getComponentName(ComponentType::RotationActionComponent, true)] = Stream::encodeRotationActionComponent(*comp);
        }
        if (auto* comp = scene->findComponent<ScaleActionComponent>(state.entity)) {
            state.components[Catalog::getComponentName(ComponentType::ScaleActionComponent, true)] = Stream::encodeScaleActionComponent(*comp);
        }
        if (auto* comp = scene->findComponent<ColorActionComponent>(state.entity)) {
            state.components[Catalog::getComponentName(ComponentType::ColorActionComponent, true)] = Stream::encodeColorActionComponent(*comp);
        }
        if (auto* comp = scene->findComponent<AlphaActionComponent>(state.entity)) {
            state.components[Catalog::getComponentName(ComponentType::AlphaActionComponent, true)] = Stream::encodeAlphaActionComponent(*comp);
        }
    }

    for (const ActionPreviewState& state : actionPreviewStates) {
        if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity) || !state.components || state.components.IsNull()) {
            continue;
        }
        Stream::decodeComponents(state.entity, state.parent, scene, state.components);
    }

    // Remove InstancedMeshComponent dynamically added during preview (e.g. particle targets).
    for (const ActionPreviewState& state : actionPreviewStates) {
        ProjectUtils::removeDynamicInstmesh(state.entity, state.components, scene);
    }

    actionPreviewStates.clear();
    actionPreviewing = false;
    actionPreviewPlaying = false;
    if (sceneProject) {
        sceneProject->needUpdateRender = true;
    }
}

void editor::Properties::drawActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsState;
    settingsState.enumEntries = &entriesActionState;

    ActionComponent* actionComp = nullptr;
    Entity entity = NULL_ENTITY;
    Scene* scene = sceneProject->scene;
    bool sceneIsStopped = (sceneProject->playState == ScenePlayState::STOPPED);

    if (entities.size() == 1) {
        entity = entities[0];
        actionComp = scene->findComponent<ActionComponent>(entity);

        if (actionPreviewing && (actionPreviewEntity != entity || actionPreviewSceneId != sceneProject->id)) {
            SceneProject* prevSceneProject = project->getScene(actionPreviewSceneId);
            if (prevSceneProject && prevSceneProject->scene) {
                stopActionPreview(prevSceneProject->scene, prevSceneProject);
            } else {
                actionPreviewStates.clear();
                actionPreviewing = false;
                actionPreviewPlaying = false;
            }
        }

        if (actionPreviewing && !sceneIsStopped) {
            stopActionPreview(scene, sceneProject);
        }

        if (actionPreviewing && actionPreviewPlaying && sceneIsStopped) {
            float dt = ImGui::GetIO().DeltaTime;
            scene->getSystem<ActionSystem>()->updateActionPreview(dt, entity);
            sceneProject->needUpdateRender = true;

            actionComp = scene->findComponent<ActionComponent>(entity);
            if (actionComp && actionComp->state == ActionState::Stopped) {
                actionPreviewPlaying = false;
            }
        }
    }

    beginTable(cpType, getLabelSize("Owned target"));
    propertyRow(RowPropertyType::Enum, cpType, "state", "State", sceneProject, entities, settingsState);
    propertyRow(RowPropertyType::Float, cpType, "speed", "Speed", sceneProject, entities);
    propertyRow(RowPropertyType::LocalEntity, cpType, "target", "Target", sceneProject, entities);
    //propertyRow(RowPropertyType::Bool, cpType, "ownedTarget", "Owned target", sceneProject, entities);
    endTable();

    if (entities.size() != 1 || !actionComp) {
        return;
    }

    // Check if this entity also has AnimationComponent → sync with AnimationWindow
    AnimationWindow* animWindow = Backend::getApp().getAnimationWindow();
    bool hasAnimation = scene->findComponent<AnimationComponent>(entity) != nullptr;
    bool syncWithAnimWindow = hasAnimation && animWindow;

    ImGui::Separator();

    ImGui::BeginDisabled(!sceneIsStopped);

    if (syncWithAnimWindow) {
        // Delegate playback to AnimationWindow
        bool animIsPlaying = animWindow->getIsPlaying() && animWindow->isPreviewingEntity(entity, sceneProject->id);
        bool animIsPreviewing = animWindow->isPreviewingEntity(entity, sceneProject->id);

        if (animIsPlaying) {
            if (ImGui::Button(ICON_FA_PAUSE "##action_pause")) {
                animWindow->externalPause();
            }
        } else {
            if (ImGui::Button(ICON_FA_PLAY "##action_play")) {
                animWindow->externalPlay(entity, sceneProject->id);
            }
        }
        ImGui::SameLine();

        ImGui::BeginDisabled(!animIsPlaying && !animIsPreviewing);
        if (ImGui::Button(ICON_FA_STOP "##action_stop")) {
            animWindow->externalStop();
        }
        ImGui::EndDisabled();
    } else {
        // Standalone action preview (no AnimationComponent)
        if (actionPreviewPlaying) {
            if (ImGui::Button(ICON_FA_PAUSE "##action_pause")) {
                if (actionComp) {
                    actionComp->pauseTrigger = true;
                }
                actionPreviewPlaying = false;
            }
        } else {
            if (ImGui::Button(ICON_FA_PLAY "##action_play")) {
                if (!actionPreviewing) {
                    startActionPreview(entity, scene, sceneProject);
                    actionComp = scene->findComponent<ActionComponent>(entity);
                } else if (actionComp) {
                    if (actionComp->state == ActionState::Stopped) {
                        stopActionPreview(scene, sceneProject);
                        startActionPreview(entity, scene, sceneProject);
                        actionComp = scene->findComponent<ActionComponent>(entity);
                    } else {
                        actionComp->startTrigger = true;
                    }
                }
                actionPreviewPlaying = true;
            }
        }
        ImGui::SameLine();

        ImGui::BeginDisabled(!actionPreviewPlaying && !actionPreviewing);
        if (ImGui::Button(ICON_FA_STOP "##action_stop")) {
            if (actionPreviewing) {
                stopActionPreview(scene, sceneProject);
                actionComp = scene->findComponent<ActionComponent>(entity);
            }
            actionPreviewPlaying = false;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

    // Time display (fixed width so it doesn't jump around)
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.2fs", actionComp->timecount);
    ImGui::SameLine();

    // Timeline progress strip
    float duration = scene->getSystem<ActionSystem>()->getDuration(entity);

    float fraction = (duration > 0) ? std::clamp(actionComp->timecount / duration, 0.0f, 1.0f) : 0.0f;

    float timelineWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    float frameHeight = ImGui::GetFrameHeight();
    float timelineHeight = 6.0f;
    float rounding = timelineHeight * 0.5f;
    ImVec2 timelinePos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##action_timeline_strip", ImVec2(timelineWidth, frameHeight));
    bool timelineHovered = ImGui::IsItemHovered();
    bool timelineClicked = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (sceneIsStopped && duration > 0 && timelineClicked) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float clickFraction = std::clamp((mouseX - timelinePos.x) / timelineWidth, 0.0f, 1.0f);
        float seekTime = clickFraction * duration;

        if (syncWithAnimWindow) {
            animWindow->externalPlay(entity, sceneProject->id);
            animWindow->externalPause();
            animWindow->seekPreviewExternal(scene, sceneProject, seekTime);
            actionComp = scene->findComponent<ActionComponent>(entity);
        } else {
            if (!actionPreviewing) {
                startActionPreview(entity, scene, sceneProject);
                actionComp = scene->findComponent<ActionComponent>(entity);
            }
            if (actionPreviewing && actionComp) {
                // Restore state and re-evaluate at seek time
                for (const ActionPreviewState& state : actionPreviewStates) {
                    if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity) || !state.components || state.components.IsNull()) {
                        continue;
                    }
                    Stream::decodeComponents(state.entity, state.parent, scene, state.components);
                }

                actionComp = scene->findComponent<ActionComponent>(entity);
                if (actionComp) {
                    actionComp->state = ActionState::Stopped;
                    actionComp->timecount = 0;
                    actionComp->stopTrigger = false;
                    actionComp->pauseTrigger = false;
                    actionComp->startTrigger = true;
                }

                // Simulate to the seek time so particles and integrations match the state exactly
                float remainingTime = seekTime;
                float stepSize = 1.0f / 60.0f;
                while (remainingTime > 0.0f) {
                    float currentStep = std::min(stepSize, remainingTime);
                    scene->getSystem<ActionSystem>()->updateActionPreview(currentStep, entity);
                    remainingTime -= currentStep;
                }

                actionPreviewPlaying = false;

                actionComp = scene->findComponent<ActionComponent>(entity);
            }
        }
    }

    if (timelineHovered && duration > 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 trackColor = style.Colors[ImGuiCol_TextDisabled];
    trackColor.w = 0.22f;
    ImVec4 fillColor = style.Colors[ImGuiCol_PlotHistogram];

    ImVec2 barMin(timelinePos.x, timelinePos.y + (frameHeight - timelineHeight) * 0.5f);
    ImVec2 barMax(timelinePos.x + timelineWidth, barMin.y + timelineHeight);
    drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(trackColor), rounding);

    if (fraction > 0.0f) {
        ImVec2 fillMax(barMin.x + timelineWidth * fraction, barMax.y);
        drawList->AddRectFilled(barMin, fillMax, ImGui::GetColorU32(fillColor), rounding);
    }

    ImGui::EndDisabled();
}

void editor::Properties::drawTimedActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Duration"));
    propertyRow(RowPropertyType::FloatPositive, cpType, "duration", "Duration", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    endTable();
}

void editor::Properties::drawPositionActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start position"));

    // We compute a width that offsets the button
    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings vectorSettings;
    vectorSettings.secondColSize = reservedWidth;

    // Start Position Row
    propertyRow(RowPropertyType::Vector3, cpType, "startPosition", "Start position", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_pos")) {
            Vector3 pos = targetTransform->position;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "startPosition", pos);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current position from target");
        }
    }

    // End Position Row
    propertyRow(RowPropertyType::Vector3, cpType, "endPosition", "End position", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_pos")) {
            Vector3 pos = targetTransform->position;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "endPosition", pos);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current position from target");
        }
    }

    endTable();
}

void editor::Properties::drawRotationActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start rotation"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings quatSettings;
    quatSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Quat, cpType, "startRotation", "Start rotation", sceneProject, entities, quatSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_rot")) {
            Quaternion rot = targetTransform->rotation;
            auto* cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, "startRotation", rot);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current rotation from target");
        }
    }

    propertyRow(RowPropertyType::Quat, cpType, "endRotation", "End rotation", sceneProject, entities, quatSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_rot")) {
            Quaternion rot = targetTransform->rotation;
            auto* cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, "endRotation", rot);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current rotation from target");
        }
    }

    propertyRow(RowPropertyType::Bool, cpType, "shortestPath", "Shortest path", sceneProject, entities);
    endTable();
}

void editor::Properties::drawScaleActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start scale"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings vectorSettings;
    vectorSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Vector3, cpType, "startScale", "Start scale", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_scale")) {
            Vector3 scl = targetTransform->scale;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "startScale", scl);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current scale from target");
        }
    }

    propertyRow(RowPropertyType::Vector3, cpType, "endScale", "End scale", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_scale")) {
            Vector3 scl = targetTransform->scale;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "endScale", scl);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current scale from target");
        }
    }

    endTable();
}

void editor::Properties::drawColorActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start color"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings colorSettings;
    colorSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Color3L, cpType, "startColor", "Start color", sceneProject, entities, colorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        Vector3 targetColor;
        bool hasColor = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetColor = Vector3(ui->color.x, ui->color.y, ui->color.z); hasColor = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetColor = Vector3(mesh->submeshes[0].material.baseColorFactor.x, mesh->submeshes[0].material.baseColorFactor.y, mesh->submeshes[0].material.baseColorFactor.z); hasColor = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasColor);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_color")) {
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "startColor", targetColor);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current color from target");
        }
    }

    propertyRow(RowPropertyType::Color3L, cpType, "endColor", "End color", sceneProject, entities, colorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        Vector3 targetColor;
        bool hasColor = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetColor = Vector3(ui->color.x, ui->color.y, ui->color.z); hasColor = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetColor = Vector3(mesh->submeshes[0].material.baseColorFactor.x, mesh->submeshes[0].material.baseColorFactor.y, mesh->submeshes[0].material.baseColorFactor.z); hasColor = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasColor);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_color")) {
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "endColor", targetColor);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current color from target");
        }
    }

    propertyRow(RowPropertyType::Bool, cpType, "useSRGB", "Use sRGB", sceneProject, entities);
    endTable();
}

void editor::Properties::drawAlphaActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start alpha"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings alphaSettings;
    alphaSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Float, cpType, "startAlpha", "Start alpha", sceneProject, entities, alphaSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        float targetAlpha = 1.0f;
        bool hasAlpha = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetAlpha = ui->color.w; hasAlpha = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetAlpha = mesh->submeshes[0].material.baseColorFactor.w; hasAlpha = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasAlpha);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_alpha")) {
            auto* cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, "startAlpha", targetAlpha);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current alpha from target");
        }
    }

    propertyRow(RowPropertyType::Float, cpType, "endAlpha", "End alpha", sceneProject, entities, alphaSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        float targetAlpha = 1.0f;
        bool hasAlpha = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetAlpha = ui->color.w; hasAlpha = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetAlpha = mesh->submeshes[0].material.baseColorFactor.w; hasAlpha = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasAlpha);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_alpha")) {
            auto* cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, "endAlpha", targetAlpha);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current alpha from target");
        }
    }

    endTable();
}

void editor::Properties::drawSpriteAnimationComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Interval index"));
    propertyRow(RowPropertyType::String, cpType, "name", "Name", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    propertyRow(RowPropertyType::Label, cpType, "frameIndex", "Frame index", sceneProject, entities);
    propertyRow(RowPropertyType::Label, cpType, "frameTimeIndex", "Interval index", sceneProject, entities);
    propertyRow(RowPropertyType::Label, cpType, "spriteFrameCount", "Frame count", sceneProject, entities);
    endTable();

    if (entities.size() != 1) {
        ImGui::SeparatorText("Animation Frames");
        ImGui::TextDisabled("Select a single entity to edit sprite animation frames");
        return;
    }

    Entity entity = entities[0];
    SpriteAnimationComponent& spriteAnim = sceneProject->scene->getComponent<SpriteAnimationComponent>(entity);

    Entity previewEntity = NULL_ENTITY;
    if (ActionComponent* actionComp = sceneProject->scene->findComponent<ActionComponent>(entity)) {
        previewEntity = actionComp->target;
    }

    SpriteComponent* spriteComp = nullptr;
    MeshComponent* meshComp = nullptr;
    if (previewEntity != NULL_ENTITY) {
        spriteComp = sceneProject->scene->findComponent<SpriteComponent>(previewEntity);
        meshComp = sceneProject->scene->findComponent<MeshComponent>(previewEntity);
    }

    Texture previewTexture;
    if (meshComp && meshComp->numSubmeshes > 0) {
        previewTexture = meshComp->submeshes[0].material.baseColorTexture;
    }

    unsigned int visibleFrameCount = std::max(spriteAnim.framesSize, spriteAnim.framesTimeSize);
    bool countsMatch = spriteAnim.framesSize == spriteAnim.framesTimeSize;
    bool canAddFrame = spriteAnim.frames.validIndex(visibleFrameCount) && spriteAnim.framesTime.validIndex(visibleFrameCount);

    ImGui::SeparatorText("Animation Frames");

    ImGui::BeginDisabled(!canAddFrame);
    if (ImGui::Button(ICON_FA_PLUS " Add Frame", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (SpriteAnimationComponent* animComp = sceneProject->scene->findComponent<SpriteAnimationComponent>(selectedEntity)) {
                unsigned int nextIndex = std::max(animComp->framesSize, animComp->framesTimeSize);
                if (!animComp->frames.validIndex(nextIndex) || !animComp->framesTime.validIndex(nextIndex)) {
                    continue;
                }

                int defaultFrame = 0;
                if (animComp->framesSize > 0 && animComp->frames.validIndex(animComp->framesSize - 1)) {
                    defaultFrame = animComp->frames[animComp->framesSize - 1];
                }

                multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                    "frames[" + std::to_string(nextIndex) + "]", defaultFrame);
                multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                    "framesTime[" + std::to_string(nextIndex) + "]", 100);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                    "framesSize", nextIndex + 1);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                    "framesTimeSize", nextIndex + 1);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    if (ImGui::IsItemHovered() && !canAddFrame) {
        ImGui::SetTooltip("Sprite animation has reached the maximum number of frames");
    }
    ImGui::EndDisabled();

    beginTable(cpType, getLabelSize("Animation Frames"), "sprite_animation_frames_header");
    propertyHeader("Animation Frames", -1, false, false);
    ImGui::Text("%u", visibleFrameCount);
    if (!countsMatch) {
        ImGui::SameLine();
        ImGui::TextDisabled("(frames/intervals mismatch)");
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_all_sprite_animation_frames", spriteAnimationFramesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        spriteAnimationFramesExpanded = !spriteAnimationFramesExpanded;
    }
    endTable();

    if (!spriteAnimationFramesExpanded || visibleFrameCount == 0) {
        return;
    }

    RowSettings settingsFrame;
    settingsFrame.stepSize = 1.0f;
    settingsFrame.format = "%.0f";
    settingsFrame.showColors = false;

    RowSettings settingsInterval = settingsFrame;

    for (unsigned int i = 0; i < visibleFrameCount; i++) {
        ImGui::PushID((int)i);

        std::string frameGroupStr = "sprite_animation_frame_" + std::to_string(i);
        std::string framePropId = "frames[" + std::to_string(i) + "]";
        std::string intervalPropId = "framesTime[" + std::to_string(i) + "]";

        bool hasFrameValue = i < spriteAnim.framesSize;
        bool hasIntervalValue = i < spriteAnim.framesTimeSize;
        int frameId = hasFrameValue ? spriteAnim.frames[i] : -1;

        bool hasSpriteFrame = spriteComp && frameId >= 0 && (unsigned int)frameId < spriteComp->numFramesRect;
        Rect frameRect;
        std::string spriteFrameName = "Invalid";
        if (hasSpriteFrame) {
            frameRect = spriteComp->framesRect[frameId].rect;
            if (!spriteComp->framesRect[frameId].name.empty()) {
                spriteFrameName = spriteComp->framesRect[frameId].name;
            } else {
                spriteFrameName = "Frame " + std::to_string(frameId);
            }
        }

        std::string frameLabel = "[" + std::to_string(i) + "] ";
        if (hasFrameValue) {
            frameLabel += spriteFrameName;
        } else {
            frameLabel += "Missing frame";
        }

        ImGui::SeparatorText(frameLabel.c_str());

        beginTable(cpType, getLabelSize("Interval (ms)"), frameGroupStr);
        propertyHeader("Frame", -1, false, false);

        float previewSize = ImGui::GetFrameHeight() * 2.2f;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

        bool hasFramePreview = hasSpriteFrame
                && !previewTexture.empty()
                && frameRect.getWidth() > 0.0f
                && frameRect.getHeight() > 0.0f;
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonSize.x;
        if (hasFramePreview) {
            trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
        }

        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (hasFramePreview) {
            drawSpriteFramePreview(&previewTexture, frameRect, ImVec2(previewSize, previewSize), "##sprite_animation_frame_preview");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Sprite frame %d", frameId);
                if (spriteComp && !spriteComp->framesRect[frameId].name.empty()) {
                    ImGui::TextDisabled("%s", spriteComp->framesRect[frameId].name.c_str());
                }

                float tooltipMaxSize = 200.0f;
                float scale = std::min(tooltipMaxSize / std::max(1.0f, frameRect.getWidth()),
                                       tooltipMaxSize / std::max(1.0f, frameRect.getHeight()));
                scale = std::min(scale, 1.0f);
                ImVec2 tooltipSize(std::max(32.0f, frameRect.getWidth() * scale),
                                   std::max(32.0f, frameRect.getHeight() * scale));

                drawSpriteFramePreview(&previewTexture, frameRect, tooltipSize, "##sprite_animation_frame_preview_tooltip");
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
        }

        if (ImGui::ArrowButton("##toggle_sprite_animation_frame", spriteAnimationFramesButtonGroups[frameGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            spriteAnimationFramesButtonGroups[frameGroupStr] = !spriteAnimationFramesButtonGroups[frameGroupStr];
        }
        ImGui::SameLine();

        bool deleted = false;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_sprite_animation_frame", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (SpriteAnimationComponent* animComp = sceneProject->scene->findComponent<SpriteAnimationComponent>(selectedEntity)) {
                    if (i < animComp->framesSize) {
                        for (unsigned int j = i; j + 1 < animComp->framesSize; j++) {
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                                "frames[" + std::to_string(j) + "]", animComp->frames[j + 1]);
                        }
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                            "frames[" + std::to_string(animComp->framesSize - 1) + "]", 0);
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                            "framesSize", animComp->framesSize - 1);
                    }

                    if (i < animComp->framesTimeSize) {
                        for (unsigned int j = i; j + 1 < animComp->framesTimeSize; j++) {
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                                "framesTime[" + std::to_string(j) + "]", animComp->framesTime[j + 1]);
                        }
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                            "framesTime[" + std::to_string(animComp->framesTimeSize - 1) + "]", 0);
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                            "framesTimeSize", animComp->framesTimeSize - 1);
                    }

                    int nextFrameIndex = 0;
                    if (animComp->framesSize > 1) {
                        nextFrameIndex = std::min(animComp->frameIndex, (int)animComp->framesSize - 2);
                    }
                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                        "frameIndex", std::max(0, nextFrameIndex));

                    int nextFrameTimeIndex = 0;
                    if (animComp->framesTimeSize > 1) {
                        nextFrameTimeIndex = std::min(animComp->frameTimeIndex, (int)animComp->framesTimeSize - 2);
                    }
                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                        "frameTimeIndex", std::max(0, nextFrameTimeIndex));
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            deleted = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (!deleted && spriteAnimationFramesButtonGroups[frameGroupStr]) {
            propertyRow(RowPropertyType::Int, cpType, framePropId, "Sprite Frame", sceneProject, entities, settingsFrame);
            propertyRow(RowPropertyType::Int, cpType, intervalPropId, "Interval (ms)", sceneProject, entities, settingsInterval);

            if (previewEntity == NULL_ENTITY) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Assign Action target to preview animation frames");
            } else if (!spriteComp) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Target entity needs SpriteComponent to preview animation frames");
            } else if (!meshComp || meshComp->numSubmeshes == 0 || previewTexture.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Target entity needs MeshComponent with a valid texture for previews");
            } else if (!hasFrameValue) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Missing frame entry for this slot");
            } else if (frameId < 0 || (unsigned int)frameId >= spriteComp->numFramesRect) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Frame %d is outside SpriteComponent frame range", frameId);
            } else if (!hasIntervalValue) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Interval");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Missing interval entry for this slot");
            }
        }

        endTable();
        ImGui::PopID();
    }
}

void editor::Properties::drawAnimationComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Owned actions"));
    propertyRow(RowPropertyType::String, cpType, "name", "Name", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "duration", "Duration", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "ownedActions", "Owned actions", sceneProject, entities);
    endTable();

    AnimationComponent& anim = sceneProject->scene->getComponent<AnimationComponent>(entities[0]);

    ImGui::SeparatorText("Actions");

    if (ImGui::Button("Add Action")) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities) {
            if (AnimationComponent* animComp = sceneProject->scene->findComponent<AnimationComponent>(entity)) {
                std::vector<ActionFrame> newActions = animComp->actions;
                ActionFrame newFrame = {0.0f, 1.0f, NULL_ENTITY, 0};

                // Find non-overlapping track
                bool overlap;
                do {
                    overlap = false;
                    for (const auto& a : newActions) {
                        if (a.track == newFrame.track) {
                            float startA = a.startTime;
                            float endA = a.startTime + a.duration;
                            float startB = newFrame.startTime;
                            float endB = newFrame.startTime + newFrame.duration;
                            if (std::max(startA, startB) < std::min(endA, endB)) {
                                overlap = true;
                                newFrame.track++;
                                break;
                            }
                        }
                    }
                } while (overlap);

                newActions.push_back(newFrame);
                multiCmd->addPropertyCmd<std::vector<ActionFrame>>(project, sceneProject->id, entity, cpType, "actions", newActions, nullptr);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    if (!anim.actions.empty()) {
        beginTable(cpType, getLabelSize("Start Time"), "animation_actions_table");
        for (size_t i = 0; i < anim.actions.size(); i++) {
            ImGui::PushID((int)i);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Action %zu", i);
            ImGui::TableSetColumnIndex(1);

            if (ImGui::Button(ICON_FA_TRASH_CAN "##remove_action")) {
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities) {
                    if (AnimationComponent* animComp = sceneProject->scene->findComponent<AnimationComponent>(entity)) {
                        if (i < animComp->actions.size()) {
                            std::vector<ActionFrame> newActions = animComp->actions;
                            newActions.erase(newActions.begin() + i);
                            multiCmd->addPropertyCmd<std::vector<ActionFrame>>(project, sceneProject->id, entity, cpType, "actions", newActions, nullptr);
                        }
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                ImGui::PopID();
                break; // break early to reset next frame cleanly
            }

            std::string prefix = "actions[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::UInt, cpType, prefix + ".track", "Track", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, prefix + ".startTime", "Start Time", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, prefix + ".duration", "Duration", sceneProject, entities);
            RowSettings settingsAction;
            settingsAction.entityFilter.set(sceneProject->scene->getComponentId<ActionComponent>());
            propertyRow(RowPropertyType::LocalEntity, cpType, prefix + ".action", "Action", sceneProject, entities, settingsAction);

            if (i < anim.actions.size() - 1) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Separator();
                ImGui::TableSetColumnIndex(1);
                ImGui::Separator();
            }

            ImGui::PopID();
        }
        endTable();
    }
}

void editor::Properties::drawBundleComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Name"));
    propertyRow(RowPropertyType::Label, cpType, "name", "Name", sceneProject, entities);
    endTable();
}

void editor::Properties::drawBoneComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsModel;
    settingsModel.entityFilter.set(sceneProject->scene->getComponentId<ModelComponent>());
    beginTable(cpType, getLabelSize("Bind Rotation"));
    propertyRow(RowPropertyType::LocalEntity, cpType, "model", "Model", sceneProject, entities, settingsModel);
    propertyRow(RowPropertyType::Int, cpType, "index", "Index", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "bindPosition", "Bind Position", sceneProject, entities);
    propertyRow(RowPropertyType::Quat, cpType, "bindRotation", "Bind Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "bindScale", "Bind Scale", sceneProject, entities);
    endTable();
}

void editor::Properties::drawKeyframeTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    KeyframeTracksComponent& comp = sceneProject->scene->getComponent<KeyframeTracksComponent>(entities[0]);

    beginTable(cpType, getLabelSize("Interpolation"));
    propertyRow(RowPropertyType::Int, cpType, "index", "Index", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "interpolation", "Interpolation", sceneProject, entities);
    endTable();

    drawTrackValues<KeyframeTracksComponent, float>(cpType, sceneProject, entities, RowPropertyType::Float, 0.0f, "keyframe", &KeyframeTracksComponent::times, "times");
}

template<typename Component, typename ValueType>
void editor::Properties::drawTrackValues(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities, RowPropertyType rowType, const ValueType& defaultNewValue, const char* idPrefix, std::vector<ValueType> Component::*memberPtr, const char* propertyName){
    Component& comp = sceneProject->scene->getComponent<Component>(entities[0]);

    float firstColSize = getLabelSize("Value 000");
    beginTable(cpType, firstColSize, std::string(idPrefix) + "_values_header");

    RowSettings settingsValue;
    settingsValue.secondColSize = -1;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Values");
    ImGui::TableSetColumnIndex(1);

    ImGui::Text("%zu", (comp.*memberPtr).size());
    ImGui::SameLine();
    if (ImGui::Button("Add Value")){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities){
            if (Component* trackComp = sceneProject->scene->findComponent<Component>(entity)){
                std::vector<ValueType> newValues = trackComp->*memberPtr;
                ValueType newValue = defaultNewValue;
                if (!newValues.empty()){
                    newValue = newValues.back();
                }
                newValues.push_back(newValue);
                multiCmd->addPropertyCmd<std::vector<ValueType>>(project, sceneProject->id, entity, cpType, propertyName, newValues);
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    ImGui::SameLine();
    std::string arrowId = std::string("##toggle_") + idPrefix + "_values";
    if (ImGui::ArrowButton(arrowId.c_str(), trackValuesExpanded[idPrefix] ? ImGuiDir_Up : ImGuiDir_Down)){
        trackValuesExpanded[idPrefix] = !trackValuesExpanded[idPrefix];
    }

    bool removedValue = false;

    if (trackValuesExpanded[idPrefix]){
    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
    ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
    if (inputSize.x < 100.0f){
        inputSize.x = 100.0f;
    }

    for (size_t i = 0; i < (comp.*memberPtr).size(); i++){
        std::string valueId = std::string(propertyName) + "[" + std::to_string(i) + "]";
        std::string valueLabel = "Value " + std::to_string(i);

        RowSettings settingsEntry = settingsValue;
        settingsEntry.secondColSize = inputSize.x;
        propertyRow(rowType, cpType, valueId, valueLabel, sceneProject, entities, settingsEntry);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        std::string removeId = std::string(ICON_FA_TRASH_CAN) + "##remove_" + idPrefix + "_value_" + std::to_string(i);
        if (ImGui::Button(removeId.c_str())){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (Entity entity : entities){
                if (Component* trackComp = sceneProject->scene->findComponent<Component>(entity)){
                    if (i < (trackComp->*memberPtr).size()){
                        std::vector<ValueType> newValues = trackComp->*memberPtr;
                        newValues.erase(newValues.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<ValueType>>(project, sceneProject->id, entity, cpType, propertyName, newValues);
                    }
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

            removedValue = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove value");
        }
    }

    } // trackValuesExpanded

    if (removedValue){
        endTable();
        return;
    }

    endTable();
}

void editor::Properties::drawTranslateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    drawTrackValues<TranslateTracksComponent, Vector3>(cpType, sceneProject, entities, RowPropertyType::Vector3, Vector3::ZERO, "translate", &TranslateTracksComponent::values, "values");
}

void editor::Properties::drawRotateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    drawTrackValues<RotateTracksComponent, Quaternion>(cpType, sceneProject, entities, RowPropertyType::Quat, Quaternion::IDENTITY, "rotate", &RotateTracksComponent::values, "values");
}

void editor::Properties::drawScaleTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    drawTrackValues<ScaleTracksComponent, Vector3>(cpType, sceneProject, entities, RowPropertyType::Vector3, Vector3(1.0f, 1.0f, 1.0f), "scale", &ScaleTracksComponent::values, "values");
}

void editor::Properties::drawMorphTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    MorphTracksComponent& comp = sceneProject->scene->getComponent<MorphTracksComponent>(entities[0]);

    float firstColSize = getLabelSize("Value 000");
    beginTable(cpType, firstColSize, "morph_values_header");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Values");
    ImGui::TableSetColumnIndex(1);

    ImGui::Text("%zu", comp.values.size());
    ImGui::SameLine();
    if (ImGui::Button("Add Value")){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities){
            if (MorphTracksComponent* trackComp = sceneProject->scene->findComponent<MorphTracksComponent>(entity)){
                auto newValues = trackComp->values;
                std::vector<float> newValue;
                if (!newValues.empty()){
                    newValue = newValues.back();
                }
                newValues.push_back(newValue);
                multiCmd->addPropertyCmd<std::vector<std::vector<float>>>(project, sceneProject->id, entity, cpType, "values", newValues);
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_morph_values", trackValuesExpanded["morph"] ? ImGuiDir_Up : ImGuiDir_Down)){
        trackValuesExpanded["morph"] = !trackValuesExpanded["morph"];
    }

    bool removedValue = false;

    if (trackValuesExpanded["morph"]){
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;

        for (size_t i = 0; i < comp.values.size(); i++){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Value %zu", i);
            ImGui::TableSetColumnIndex(1);

            ImGui::Text("%zu weights", comp.values[i].size());

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
            std::string removeId = std::string(ICON_FA_TRASH_CAN) + "##remove_morph_value_" + std::to_string(i);
            if (ImGui::Button(removeId.c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (MorphTracksComponent* trackComp = sceneProject->scene->findComponent<MorphTracksComponent>(entity)){
                        if (i < trackComp->values.size()){
                            auto newValues = trackComp->values;
                            newValues.erase(newValues.begin() + (long int)i);
                            multiCmd->addPropertyCmd<std::vector<std::vector<float>>>(project, sceneProject->id, entity, cpType, "values", newValues);
                        }
                    }
                }

                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                removedValue = true;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                break;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove value");
            }

            for (size_t j = 0; j < comp.values[i].size(); j++){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  Weight %zu", j);
                ImGui::TableSetColumnIndex(1);

                float val = comp.values[i][j];
                std::string weightId = "##morph_w_" + std::to_string(i) + "_" + std::to_string(j);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat(weightId.c_str(), &val, 0.01f, 0.0f, 0.0f, "%.4f")){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (MorphTracksComponent* trackComp = sceneProject->scene->findComponent<MorphTracksComponent>(entity)){
                            if (i < trackComp->values.size() && j < trackComp->values[i].size()){
                                auto newValues = trackComp->values;
                                newValues[i][j] = val;
                                multiCmd->addPropertyCmd<std::vector<std::vector<float>>>(project, sceneProject->id, entity, cpType, "values", newValues);
                            }
                        }
                    }
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }
            }
        }
    }

    if (removedValue){
        endTable();
        return;
    }

    endTable();
}

void editor::Properties::show(){
    // Flush any debounced material file writes
    flushDirtyMaterials(project, ImGui::GetIO().DeltaTime);

    ImGui::Begin(Properties::WINDOW_NAME);

    uint32_t propertiesSceneId = project->getSelectedSceneForProperties();
    SceneProject* sceneProject = project->getScene(propertiesSceneId);
    if (!sceneProject) {
        sceneProject = project->getSelectedScene();
    }
    if (!sceneProject) {
        ImGui::End();
        return;
    }

    std::vector<Entity> entities = project->getSelectedEntities(sceneProject->id);

    std::vector<ComponentType> components;
    Scene* scene = sceneProject->scene;

    if (entities.size() > 0){

        // to change component view order, need change ComponentType
        std::filesystem::path bundlePath;
        std::string names;
        bool isFirstEntity = true;
        for (Entity& entity : entities){
            std::vector<ComponentType> newComponents = Catalog::findComponents(scene, entity);

            if (!std::is_sorted(newComponents.begin(), newComponents.end())) {
                std::sort(newComponents.begin(), newComponents.end());
            }

            std::filesystem::path newBundlePath = project->findEntityBundlePathFor(sceneProject->id, entity);

            if (isFirstEntity) {
                components = newComponents;
                bundlePath = newBundlePath;
                isFirstEntity = false;
            } else {
                std::vector<ComponentType> intersection;
                intersection.reserve(std::min(components.size(), newComponents.size()));

                std::set_intersection(
                    components.begin(), components.end(),
                    newComponents.begin(), newComponents.end(),
                    std::back_inserter(intersection));

                components = std::move(intersection);

                if (bundlePath != newBundlePath) {
                    bundlePath.clear(); // Different bundles, so no bundle
                }

                names += ", ";
            }

            names += scene->getEntityName(entity);
        }

        if (entities.size() == 1){
            ImGui::Text("Entity");
        }else{
            ImGui::Text("Entities (%lu)", entities.size());
        }
        ImGui::SetItemTooltip("%lu selected: %s", entities.size(), names.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        static char nameBuffer[128];
        strncpy(nameBuffer, names.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::BeginDisabled(entities.size() != 1);
        ImGui::InputText("##input_name", nameBuffer, IM_ARRAYSIZE(nameBuffer));
        ImGui::EndDisabled();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (entities.size() == 1){
                if (nameBuffer[0] != '\0' && strcmp(nameBuffer, scene->getEntityName(entities[0]).c_str()) != 0) {
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new EntityNameCmd(project, sceneProject->id, entities[0], nameBuffer));
                }
            }
        }

        ImGui::Separator();

        bool isReadOnlyComponents = false;

        float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        ImGui::BeginDisabled(isReadOnlyComponents);
        if (ImGui::Button(ICON_FA_SQUARE_PLUS " New component", ImVec2(buttonWidth, 0))) {
            componentAddDialog.open(
                [this, sceneProject, entities](ComponentType cpType) {
                    // Add component to all selected entities
                    for (const Entity& entity : entities) {
                        cmd = new AddComponentCmd(project, sceneProject->id, entity, cpType);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    cmd->setNoMerge();

                    // Mark scene as modified
                    sceneProject->isModified = true;
                },
                [this, sceneProject, entities](ComponentType cpType) {
                    // Check if component can be added to all selected entities
                    for (const Entity& entity : entities) {
                        if (!canAddComponent(sceneProject, entity, cpType)) {
                            return false;
                        }
                    }
                    return true;
                },
                [](){}
            );
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FILE_CIRCLE_PLUS " New Script", ImVec2(buttonWidth, 0))) {
            std::string defaultName = "NewScript";
            Entity firstEntity = entities.empty() ? NULL_ENTITY : entities[0];
            scriptCreateDialog.open(
                sceneProject->scene,
                firstEntity,
                project->getProjectPath(),
                defaultName,
                [this, sceneProject, entities](const std::filesystem::path& headerPath,
                                            const std::filesystem::path& sourcePath,
                                            const std::string& name,
                                            ScriptType type) {

                    editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                    for (const Entity& entity : entities) {
                        bool hasScriptComponent = false;
                        std::vector<ComponentType> existingComponents = Catalog::findComponents(sceneProject->scene, entity);
                        for (ComponentType ct : existingComponents) {
                            if (ct == ComponentType::ScriptComponent) {
                                hasScriptComponent = true;
                                break;
                            }
                        }

                        std::vector<ScriptEntry> newScripts;

                        if (!hasScriptComponent) {
                            auto addCmd = std::make_unique<editor::AddComponentCmd>(project, sceneProject->id, entity, ComponentType::ScriptComponent);
                            multiCmd->addCommand(std::move(addCmd));
                        } else {
                            ScriptComponent& scriptComp = sceneProject->scene->getComponent<ScriptComponent>(entity);
                            newScripts = scriptComp.scripts;
                        }

                        ScriptEntry entry;
                        entry.type = type;
                        entry.enabled = true;

                        if (type == ScriptType::SUBCLASS || type == ScriptType::SCRIPT_CLASS) {
                            entry.headerPath = headerPath.string();
                            entry.path = sourcePath.string();
                            entry.className = name;
                        } else if (type == ScriptType::SCRIPT_LUA) {
                            entry.headerPath.clear();
                            entry.path = sourcePath.string();
                            entry.className = name; // module (file base) name
                        }

                        newScripts.push_back(entry);

                        project->updateScriptProperties(sceneProject, entity, newScripts);

                        auto propCmd = std::make_unique<editor::PropertyCmd<std::vector<ScriptEntry>>>(
                            project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts
                        );
                        multiCmd->addCommand(std::move(propCmd));
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                },
                [](){}
            );
        }

        ImGui::EndDisabled();

        // Show the component add dialog
        componentAddDialog.show();

        bool isBundle = !bundlePath.empty();

        // Root entity of a bundle is always local, not part of the bundle template
        if (isBundle) {
            EntityBundle* bundle = project->getEntityBundle(bundlePath);
            if (bundle) {
                for (Entity& entity : entities) {
                    Entity root = bundle->getRootEntity(sceneProject->id, entity);
                    if (root == entity && bundle->getRegistryEntity(sceneProject->id, entity) == NULL_ENTITY) {
                        isBundle = false;
                        break;
                    }
                }
            }
        }

        for (ComponentType& cpType : components){

            // Check if this component is overridden for bundle entities
            bool isBundleOverridden = false;
            EntityBundle* entityBundle = nullptr;
            if (isBundle) {
                entityBundle = project->getEntityBundle(bundlePath);
                if (entityBundle) {
                    for (Entity& entity : entities) {
                        if (entityBundle->hasComponentOverride(sceneProject->id, entity, cpType)) {
                            isBundleOverridden = true;
                            break;
                        }
                        isBundleOverridden = false;
                    }
                }
            }

            // Create header with appropriate styling
            ImGui::PushID(static_cast<int>(cpType));

            // Build header text with icon
            std::string headerText;

            if (isBundle && !isBundleOverridden) {
                // Bundle components - light blue with cube icon (same as Structure window)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                headerText = ICON_FA_CUBE " ";
            }
            headerText += Catalog::getComponentName(cpType);

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            bool headerOpen = ImGui::CollapsingHeader(headerText.c_str());

            if (isBundle && !isBundleOverridden) {
                ImGui::PopStyleColor();
            }

            // Context menu disabled while playing
            bool compReadOnly = false;
            handleComponentMenu(sceneProject, entities, cpType, isBundle, isBundleOverridden, headerOpen, compReadOnly);

            // Add hover tooltip for bundle components
            if (isBundle && !isBundleOverridden && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), ICON_FA_CUBE " Bundle Component");
                ImGui::Text("This component comes from the bundle template.");
                ImGui::EndTooltip();
            }

            ImGui::PopID();

            if (headerOpen){
                // Disable non-script components while playing
                if (compReadOnly) ImGui::BeginDisabled(true);

                if (cpType == ComponentType::Transform){
                    drawTransform(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::MeshComponent){
                    drawMeshComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ModelComponent){
                    drawModelComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::UIComponent){
                    drawUIComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ButtonComponent){
                    drawButtonComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TextComponent){
                    drawTextComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::UILayoutComponent){
                    drawUILayoutComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::UIContainerComponent){
                    drawUIContainerComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ImageComponent){
                    drawImageComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SpriteComponent){
                    drawSpriteComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TilemapComponent){
                    drawTilemapComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TerrainComponent){
                    drawTerrainComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::LightComponent){
                    drawLightComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::CameraComponent){
                    drawCameraComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SkyComponent){
                    drawSkyComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ScriptComponent){
                    drawScriptComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Body2DComponent){
                    drawBody2DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Body3DComponent){
                    drawBody3DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Joint2DComponent){
                    drawJoint2DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Joint3DComponent){
                    drawJoint3DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ActionComponent){
                    drawActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TimedActionComponent){
                    drawTimedActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::PositionActionComponent){
                    drawPositionActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::RotationActionComponent){
                    drawRotationActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ScaleActionComponent){
                    drawScaleActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ColorActionComponent){
                    drawColorActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::AlphaActionComponent){
                    drawAlphaActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SpriteAnimationComponent){
                    drawSpriteAnimationComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::AnimationComponent){
                    drawAnimationComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::BundleComponent){
                    drawBundleComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::BoneComponent){
                    drawBoneComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::KeyframeTracksComponent){
                    drawKeyframeTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TranslateTracksComponent){
                    drawTranslateTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::RotateTracksComponent){
                    drawRotateTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ScaleTracksComponent){
                    drawScaleTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::MorphTracksComponent){
                    drawMorphTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ParticlesComponent){
                    drawParticlesComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::PointsComponent){
                    drawPointsComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::InstancedMeshComponent){
                    drawInstancedMeshComponent(cpType, sceneProject, entities);
                }

                if (compReadOnly) ImGui::EndDisabled();
            }
        }

    }else{
        ImGui::Text("Scene");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        static char nameBuffer[128];
        strncpy(nameBuffer, sceneProject->name.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        bool isMainScene = (sceneProject->id == project->getSelectedSceneId());
        ImGui::BeginDisabled(!isMainScene);
        ImGui::InputText("##input_scene_name", nameBuffer, IM_ARRAYSIZE(nameBuffer));
        ImGui::EndDisabled();

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (isMainScene && nameBuffer[0] != '\0' && strcmp(nameBuffer, sceneProject->name.c_str()) != 0) {
                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new SceneNameCmd(project, sceneProject->id, nameBuffer));
            }
        }
        ImGui::Separator();

        if (sceneProject->scene) {
            ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame;

            if (ImGui::BeginTable("scene_settings_table", 2, tableFlags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Background").x);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                drawScenePropertyRow<Vector4>(sceneProject, "background_color", "Background", ScenePropertyInputType::ColorRGBA);
                drawScenePropertyRow<LightState>(sceneProject, "light_state", "Lights", ScenePropertyInputType::Combo);

                ImGui::EndTable();
            }

            LightState currentLightState = doriax::editor::Catalog::getSceneProperty<LightState>(sceneProject->scene, "light_state");
            if (currentLightState != LightState::OFF) {
                ImGui::SeparatorText("Global Illumination");

                if (ImGui::BeginTable("scene_globalillum_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Intensity").x);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<Vector3>(sceneProject, "global_illumination_color", "Color", ScenePropertyInputType::ColorRGB);
                    drawScenePropertyRow<float>(sceneProject, "global_illumination_intensity", "Intensity", ScenePropertyInputType::SliderFloat, 0.0f, 1.0f);

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Shadows");

                if (ImGui::BeginTable("scene_shadow_settings_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Enable PCF").x);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<bool>(sceneProject, "shadows_pcf", "Enable PCF", ScenePropertyInputType::Checkbox);

                    ImGui::EndTable();
                }
            }
        }

        thumbnailTextures.clear();
    }

    // Clean up unused material renders
    for (auto it = materialRenders.begin(); it != materialRenders.end(); ) {
        if (usedPreviewIds.find(it->first) == usedPreviewIds.end()) {
            if (!Engine::isSceneRunning(it->second.getScene())){
                it = materialRenders.erase(it);
            }
        } else {
            ++it;
        }
    }

    // Clean up unused direction renders
    for (auto it = directionRenders.begin(); it != directionRenders.end(); ) {
        if (usedPreviewIds.find(it->first) == usedPreviewIds.end()) {
            if (!Engine::isSceneRunning(it->second.getScene())){
                it = directionRenders.erase(it);
            }
        } else {
            ++it;
        }
    }
    usedPreviewIds.clear();

    scriptCreateDialog.show();
    textureSlicerToolDialog.show();

    ImGui::End();
}