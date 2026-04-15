#include "ComponentAddDialog.h"
#include "external/IconsFontAwesome6.h"
#include "util/UIUtils.h"
#include <cctype>

namespace doriax {
namespace editor {

ComponentAddDialog::ComponentAddDialog() {
    initializeCategories();
}

void ComponentAddDialog::initializeCategories() {
    m_categories = {
        {"Core", ICON_FA_LOCATION_CROSSHAIRS, ImVec4(0.4f, 0.8f, 0.9f, 1.0f), {
            {ComponentType::Transform, ICON_FA_UP_DOWN_LEFT_RIGHT, "Transform", "Position, rotation, scale and hierarchy", ImVec4(0.4f, 0.8f, 0.9f, 1.0f)}
        }},
        {"Rendering", ICON_FA_CUBE, ImVec4(0.9f, 0.5f, 0.3f, 1.0f), {
            {ComponentType::MeshComponent, ICON_FA_SHAPES, "Mesh", "3D mesh renderer", ImVec4(0.9f, 0.5f, 0.3f, 1.0f)},
            {ComponentType::LightComponent, ICON_FA_LIGHTBULB, "Light", "Light source", ImVec4(1.0f, 0.9f, 0.3f, 1.0f)},
            {ComponentType::CameraComponent, ICON_FA_VIDEO, "Camera", "Camera view", ImVec4(0.5f, 0.7f, 0.9f, 1.0f)},
            {ComponentType::SkyComponent, ICON_FA_CLOUD_SUN, "Sky", "Skybox or skydome", ImVec4(0.6f, 0.8f, 1.0f, 1.0f)},
            {ComponentType::FogComponent, ICON_FA_SMOG, "Fog", "Fog effect", ImVec4(0.7f, 0.7f, 0.8f, 1.0f)},
            {ComponentType::ModelComponent, ICON_FA_CUBES, "Model", "3D model", ImVec4(0.8f, 0.4f, 0.4f, 1.0f)},
            {ComponentType::TerrainComponent, ICON_FA_MOUNTAIN, "Terrain", "Terrain mesh", ImVec4(0.4f, 0.7f, 0.3f, 1.0f)},
            {ComponentType::InstancedMeshComponent, ICON_FA_CLONE, "Instanced Mesh", "Instanced rendering", ImVec4(0.7f, 0.4f, 0.5f, 1.0f)}
        }},
        {"User Interface", ICON_FA_WINDOW_RESTORE, ImVec4(0.5f, 0.3f, 0.9f, 1.0f), {
            {ComponentType::UIComponent, ICON_FA_SQUARE, "UI Base", "Basic UI element", ImVec4(0.5f, 0.3f, 0.9f, 1.0f)},
            {ComponentType::UILayoutComponent, ICON_FA_TABLE_CELLS, "UI Layout", "Layout container", ImVec4(0.6f, 0.4f, 0.9f, 1.0f)},
            {ComponentType::UIContainerComponent, ICON_FA_BOX, "UI Container", "Container element", ImVec4(0.55f, 0.35f, 0.85f, 1.0f)},
            {ComponentType::ImageComponent, ICON_FA_IMAGE, "Image", "UI image display", ImVec4(0.7f, 0.5f, 0.9f, 1.0f)},
            {ComponentType::TextComponent, ICON_FA_FONT, "Text", "Text display", ImVec4(0.8f, 0.6f, 0.9f, 1.0f)},
            {ComponentType::ButtonComponent, ICON_FA_SQUARE_CHECK, "Button", "Interactive button", ImVec4(0.5f, 0.7f, 0.5f, 1.0f)},
            {ComponentType::PanelComponent, ICON_FA_WINDOW_MAXIMIZE, "Panel", "UI panel", ImVec4(0.4f, 0.5f, 0.7f, 1.0f)},
            {ComponentType::ScrollbarComponent, ICON_FA_ARROWS_UP_DOWN, "Scrollbar", "Scroll control", ImVec4(0.6f, 0.6f, 0.7f, 1.0f)},
            {ComponentType::TextEditComponent, ICON_FA_KEYBOARD, "Text Edit", "Text input field", ImVec4(0.7f, 0.7f, 0.8f, 1.0f)}
        }},
        {"2D Graphics", ICON_FA_LAYER_GROUP, ImVec4(0.3f, 0.8f, 0.5f, 1.0f), {
            {ComponentType::SpriteComponent, ICON_FA_IMAGE, "Sprite", "2D sprite", ImVec4(0.3f, 0.8f, 0.5f, 1.0f)},
            {ComponentType::SpriteAnimationComponent, ICON_FA_FILM, "Sprite Animation", "Animated sprite", ImVec4(0.4f, 0.8f, 0.6f, 1.0f)},
            {ComponentType::TilemapComponent, ICON_FA_BORDER_ALL, "Tilemap", "Tile-based map", ImVec4(0.5f, 0.7f, 0.6f, 1.0f)},
            {ComponentType::PolygonComponent, ICON_FA_DRAW_POLYGON, "Polygon", "2D polygon shape", ImVec4(0.6f, 0.8f, 0.4f, 1.0f)},
            {ComponentType::MeshPolygonComponent, ICON_FA_VECTOR_SQUARE, "Mesh Polygon", "Mesh-based polygon", ImVec4(0.5f, 0.75f, 0.45f, 1.0f)}
        }},
        {"Primitives", ICON_FA_SHAPES, ImVec4(0.7f, 0.4f, 0.7f, 1.0f), {
            {ComponentType::PointsComponent, ICON_FA_BRAILLE, "Points", "Point cloud", ImVec4(0.8f, 0.5f, 0.8f, 1.0f)},
            {ComponentType::LinesComponent, ICON_FA_GRIP_LINES, "Lines", "Line renderer", ImVec4(0.7f, 0.4f, 0.7f, 1.0f)},
            {ComponentType::ParticlesComponent, ICON_FA_FIRE, "Particles", "Particle system", ImVec4(0.9f, 0.3f, 0.5f, 1.0f)}
        }},
        {"Physics", ICON_FA_ATOM, ImVec4(0.8f, 0.3f, 0.3f, 1.0f), {
            {ComponentType::Body2DComponent, ICON_FA_CIRCLE, "2D Physics Body", "2D rigid body", ImVec4(0.8f, 0.3f, 0.3f, 1.0f)},
            {ComponentType::Body3DComponent, ICON_FA_CUBE, "3D Physics Body", "3D rigid body", ImVec4(0.9f, 0.4f, 0.4f, 1.0f)},
            {ComponentType::Joint2DComponent, ICON_FA_LINK, "2D Joint", "2D physics joint", ImVec4(0.7f, 0.5f, 0.3f, 1.0f)},
            {ComponentType::Joint3DComponent, ICON_FA_LINK, "3D Joint", "3D physics joint", ImVec4(0.8f, 0.6f, 0.4f, 1.0f)}
        }},
        {"Animation", ICON_FA_PLAY, ImVec4(0.9f, 0.6f, 0.2f, 1.0f), {
            {ComponentType::AnimationComponent, ICON_FA_FILM, "Animation", "Animation player", ImVec4(0.9f, 0.6f, 0.2f, 1.0f)},
            {ComponentType::ActionComponent, ICON_FA_PLAY, "Action", "Action sequence", ImVec4(0.8f, 0.7f, 0.3f, 1.0f)},
            {ComponentType::TimedActionComponent, ICON_FA_CLOCK, "Timed Action", "Time-based action", ImVec4(0.85f, 0.65f, 0.25f, 1.0f)},
            {ComponentType::AlphaActionComponent, ICON_FA_EYE, "Alpha Action", "Opacity animation", ImVec4(0.7f, 0.6f, 0.4f, 1.0f)},
            {ComponentType::ColorActionComponent, ICON_FA_PALETTE, "Color Action", "Color animation", ImVec4(0.8f, 0.5f, 0.3f, 1.0f)},
            {ComponentType::PositionActionComponent, ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Position Action", "Position animation", ImVec4(0.75f, 0.7f, 0.35f, 1.0f)},
            {ComponentType::RotationActionComponent, ICON_FA_ROTATE, "Rotation Action", "Rotation animation", ImVec4(0.85f, 0.75f, 0.3f, 1.0f)},
            {ComponentType::ScaleActionComponent, ICON_FA_EXPAND, "Scale Action", "Scale animation", ImVec4(0.9f, 0.65f, 0.35f, 1.0f)},
            {ComponentType::BoneComponent, ICON_FA_BONE, "Bone", "Skeletal bone", ImVec4(0.9f, 0.9f, 0.8f, 1.0f)},
            {ComponentType::KeyframeTracksComponent, ICON_FA_KEY, "Keyframe Tracks", "Keyframe animation", ImVec4(0.8f, 0.6f, 0.3f, 1.0f)},
            {ComponentType::MorphTracksComponent, ICON_FA_FACE_SMILE, "Morph Tracks", "Morph targets", ImVec4(0.7f, 0.65f, 0.4f, 1.0f)},
            {ComponentType::RotateTracksComponent, ICON_FA_CIRCLE_NOTCH, "Rotate Tracks", "Rotation tracks", ImVec4(0.75f, 0.6f, 0.35f, 1.0f)},
            {ComponentType::ScaleTracksComponent, ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, "Scale Tracks", "Scale tracks", ImVec4(0.8f, 0.55f, 0.3f, 1.0f)},
            {ComponentType::TranslateTracksComponent, ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Translate Tracks", "Translation tracks", ImVec4(0.85f, 0.6f, 0.25f, 1.0f)}
        }},
        {"Audio & Script", ICON_FA_CODE, ImVec4(0.6f, 0.6f, 0.6f, 1.0f), {
            {ComponentType::AudioComponent, ICON_FA_VOLUME_HIGH, "Audio", "Sound player", ImVec4(0.4f, 0.7f, 0.9f, 1.0f)},
            {ComponentType::ScriptComponent, ICON_FA_CODE, "Script", "C++ or Lua script", ImVec4(0.7f, 0.5f, 0.8f, 1.0f)}
        }}
    };
}

void ComponentAddDialog::open(
    std::function<void(ComponentType)> onAdd,
    std::function<bool(ComponentType)> canAddComponent,
    std::function<void()> onCancel
) {
    m_isOpen = true;
    m_justOpened = true;
    memset(m_searchBuffer, 0, sizeof(m_searchBuffer));
    m_hoveredIndex = -1;
    m_onAdd = onAdd;
    m_canAddComponent = canAddComponent;
    m_onCancel = onCancel;
}

void ComponentAddDialog::show() {
    if (!m_isOpen) return;

    ImGui::OpenPopup("Add new component##ComponentAddModal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0.0f), ImGuiCond_Once);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | 
                             ImGuiWindowFlags_Modal;

    if (!ImGui::BeginPopupModal("Add new component##ComponentAddModal", nullptr, flags)) {
        if (m_isOpen) {
            m_isOpen = false;
            if (m_onCancel) m_onCancel();
        }
        return;
    }

    // Search input
    UIUtils::searchInput("##search", "Search components...", m_searchBuffer, sizeof(m_searchBuffer), m_justOpened);
    m_justOpened = false;

    ImGui::Separator();

    // Filter components based on search
    std::string searchStr(m_searchBuffer);
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    // Scrollable component list
    ImGui::BeginChild("ComponentList", ImVec2(0, 320), false);

    int componentIndex = 0;
    bool foundAny = false;

    ImGui::Dummy(ImVec2(0, 5));

    for (const auto& category : m_categories) {
        std::vector<ComponentEntry> filteredEntries;

        // Filter entries based on search and availability
        for (const auto& entry : category.entries) {
            // Check search match
            std::string entryName(entry.name);
            std::transform(entryName.begin(), entryName.end(), entryName.begin(), ::tolower);
            if (!searchStr.empty() && entryName.find(searchStr) == std::string::npos) {
                continue;
            }

            // Check if component can be added
            if (m_canAddComponent && m_canAddComponent(entry.type)) {
                filteredEntries.push_back(entry);
            }
        }

        if (!filteredEntries.empty()) {
            foundAny = true;

            // Category header
            ImGui::PushStyleColor(ImGuiCol_Text, category.color);
            ImGui::SetCursorPosX(10);
            ImGui::Text("%s %s", category.icon, category.name);
            ImGui::PopStyleColor();
            ImGui::Separator();

            // Component entries
            for (const auto& entry : filteredEntries) {
                ImGui::PushID(componentIndex);

                ImVec2 cursorPos = ImGui::GetCursorPos();

                // Customize button colors to match your design
                ImVec4 bgColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent by default
                ImVec4 hoverColor = ImVec4(0.235f, 0.314f, 0.471f, 0.392f);  // Your hover color
                ImVec4 activeColor = ImVec4(0.314f, 0.392f, 0.549f, 0.588f);  // Slightly brighter when clicked

                ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.0f)); // Left align

                // Create button with empty label (we'll draw content manually)
                bool clicked = ImGui::Button("##component", ImVec2(ImGui::GetContentRegionAvail().x, 45));

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);

                // Check if hovered for cursor change
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }

                // Draw icon and text on top of the button
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 20, cursorPos.y + 5));

                ImGui::PushStyleColor(ImGuiCol_Text, entry.color);
                ImGui::Text("%s", entry.icon);
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::SetCursorPosX(cursorPos.x + 50);

                // Component name and description
                ImGui::BeginGroup();
                ImGui::Text("%s", entry.name);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);  // Tighten spacing
                ImGui::TextWrapped("%s", entry.description);
                ImGui::PopStyleColor();
                ImGui::EndGroup();

                // Handle button click
                if (clicked) {
                    if (m_onAdd) {
                        m_onAdd(entry.type);
                    }
                    
                    // Reset state and close
                    memset(m_searchBuffer, 0, sizeof(m_searchBuffer));
                    m_hoveredIndex = -1;
                    m_justOpened = false;
                    m_isOpen = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::PopID();
                componentIndex++;

                ImGui::Dummy(ImVec2(0, 5));

            }

        }
    }

    if (!foundAny) {
        // No components available message
        ImGui::Dummy(ImVec2(0, 100));

        ImVec2 textSize = ImGui::CalcTextSize("No components available");
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textSize.x) * 0.5f);
        ImGui::TextDisabled("No components available");

        if (searchStr.empty()) {
            textSize = ImGui::CalcTextSize("All components are already added");
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textSize.x) * 0.5f);
            ImGui::TextDisabled("All components are already added");
        } else {
            textSize = ImGui::CalcTextSize("Try a different search term");
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textSize.x) * 0.5f);
            ImGui::TextDisabled("Try a different search term");
        }

        ImGui::Dummy(ImVec2(0, 100));
    }

    ImGui::EndChild();

    // Footer with Cancel button
    ImGui::Separator();

    float buttonWidth = 100;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        memset(m_searchBuffer, 0, sizeof(m_searchBuffer));
        m_hoveredIndex = -1;
        m_justOpened = false;
        m_isOpen = false;
        if (m_onCancel) m_onCancel();
        ImGui::CloseCurrentPopup();
    }
    
    ImGui::PopStyleColor(2);

    ImGui::EndPopup();
}

} // namespace editor
} // namespace doriax