#include "ProjectSettingsWindow.h"
#include "util/FileDialogs.h"
#include "Backend.h"
#include "window/Widgets.h"
#include "external/IconsFontAwesome6.h"

namespace doriax::editor {

static const char* scalingModeNames[] = { "Fit Width", "Fit Height", "Letterbox", "Crop", "Stretch", "Native" };
static const Scaling scalingModeValues[] = { Scaling::FITWIDTH, Scaling::FITHEIGHT, Scaling::LETTERBOX, Scaling::CROP, Scaling::STRETCH, Scaling::NATIVE };
static const int scalingModeCount = sizeof(scalingModeValues) / sizeof(scalingModeValues[0]);

static const char* textureStrategyNames[] = { "Fit", "Resize", "None" };
static const TextureStrategy textureStrategyValues[] = { TextureStrategy::FIT, TextureStrategy::RESIZE, TextureStrategy::NONE };
static const int textureStrategyCount = sizeof(textureStrategyValues) / sizeof(textureStrategyValues[0]);

static int findScalingIndex(Scaling mode) {
    for (int i = 0; i < scalingModeCount; i++) {
        if (scalingModeValues[i] == mode) return i;
    }
    return 0;
}

static int findTextureStrategyIndex(TextureStrategy strategy) {
    for (int i = 0; i < textureStrategyCount; i++) {
        if (textureStrategyValues[i] == strategy) return i;
    }
    return 0;
}

static void drawScalingPreview(Scaling mode, int canvasWidth, int canvasHeight) {
    if (canvasWidth <= 0 || canvasHeight <= 0) return;

    float canvasAspect = (float)canvasWidth / (float)canvasHeight;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Base canvas size for preview
    float baseH = 50.0f;
    float baseW = baseH * canvasAspect;
    if (baseW > 80.0f) { baseW = 80.0f; baseH = baseW / canvasAspect; }
    if (baseH > 70.0f) { baseH = 70.0f; baseW = baseH * canvasAspect; }
    if (baseW < 15.0f) { baseW = 15.0f; baseH = baseW / canvasAspect; }
    if (baseH < 15.0f) { baseH = 15.0f; baseW = baseH * canvasAspect; }

    // Three viewport configurations: reference, wider, taller
    float vpW[3] = { baseW * 1.15f, baseW * 1.8f,  baseW * 0.55f };
    float vpH[3] = { baseH * 1.15f, baseH * 0.7f,  baseH * 1.7f  };

    float spacing = 16.0f;
    float totalW = vpW[0] + vpW[1] + vpW[2] + spacing * 2;
    float maxH = 0;
    for (int i = 0; i < 3; i++) { if (vpH[i] > maxH) maxH = vpH[i]; }

    float availW = ImGui::GetContentRegionAvail().x;
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float offsetX = (availW - totalW) * 0.5f;
    if (offsetX < 0) offsetX = 0;
    float startX = cursor.x + offsetX;
    float startY = cursor.y;

    ImU32 colVpBg        = IM_COL32(215, 228, 243, 255);
    ImU32 colVpBorder    = IM_COL32(155, 165, 180, 255);
    ImU32 colCanvasFill  = IM_COL32(190, 210, 235, 255);
    ImU32 colCanvasBorder= IM_COL32(35, 50, 75, 255);
    ImU32 colTriangle    = IM_COL32(30, 80, 150, 255);
    ImU32 colBlack       = IM_COL32(15, 15, 20, 255);

    float baseTW = baseW * 0.35f;
    float baseTH = baseH * 0.4f;

    float curX = startX;

    for (int i = 0; i < 3; i++) {
        float vw = vpW[i];
        float vh = vpH[i];
        float vx = curX;
        float vy = startY + (maxH - vh) * 0.5f;

        ImVec2 vpMin(vx, vy);
        ImVec2 vpMax(vx + vw, vy + vh);

        // Compute scale factors based on scaling mode
        float scaleX, scaleY;
        switch (mode) {
        case Scaling::FITWIDTH:
            scaleX = scaleY = vw / baseW;
            break;
        case Scaling::FITHEIGHT:
            scaleX = scaleY = vh / baseH;
            break;
        case Scaling::LETTERBOX: {
            float s = (vw / baseW < vh / baseH) ? vw / baseW : vh / baseH;
            scaleX = scaleY = s;
            break;
        }
        case Scaling::CROP: {
            float s = (vw / baseW > vh / baseH) ? vw / baseW : vh / baseH;
            scaleX = scaleY = s;
            break;
        }
        case Scaling::STRETCH:
            scaleX = vw / baseW;
            scaleY = vh / baseH;
            break;
        case Scaling::NATIVE:
        default:
            scaleX = scaleY = 1.0f;
            break;
        }

        float dispW = baseW * scaleX;
        float dispH = baseH * scaleY;

        // Canvas centered in viewport
        float cX = vx + (vw - dispW) * 0.5f;
        float cY = vy + (vh - dispH) * 0.5f;
        ImVec2 cMin(cX, cY);
        ImVec2 cMax(cX + dispW, cY + dispH);

        // Clip drawing to viewport bounds
        dl->PushClipRect(vpMin, vpMax, true);

        // Viewport background
        if (mode == Scaling::LETTERBOX) {
            dl->AddRectFilled(vpMin, vpMax, colBlack);
        } else {
            dl->AddRectFilled(vpMin, vpMax, colVpBg);
        }

        // Canvas fill
        dl->AddRectFilled(cMin, cMax, colCanvasFill);

        // Triangle (content indicator)
        float triW = baseTW * scaleX;
        float triH = baseTH * scaleY;
        float triCX = cX + dispW * 0.5f;
        float triCY = cY + dispH * 0.5f;

        ImVec2 triP1(triCX, triCY - triH * 0.5f);
        ImVec2 triP2(triCX - triW * 0.5f, triCY + triH * 0.5f);
        ImVec2 triP3(triCX + triW * 0.5f, triCY + triH * 0.5f);
        dl->AddTriangleFilled(triP1, triP2, triP3, colTriangle);
        dl->AddTriangle(triP1, triP2, triP3, colCanvasBorder, 1.0f);

        // Canvas border
        dl->AddRect(cMin, cMax, colCanvasBorder, 0, 0, 1.5f);

        // Viewport border
        dl->AddRect(vpMin, vpMax, colVpBorder);

        dl->PopClipRect();

        curX += vw + spacing;
    }

    ImGui::Dummy(ImVec2(totalW, maxH));
}

void ProjectSettingsWindow::open(Project* project) {
    m_isOpen = true;
    m_project = project;
    m_canvasWidth = project->getCanvasWidth();
    m_canvasHeight = project->getCanvasHeight();
    m_scalingModeIndex = findScalingIndex(project->getScalingMode());
    m_textureStrategyIndex = findTextureStrategyIndex(project->getTextureStrategy());
    m_assetsDir = project->getAssetsDir();
    m_luaDir = project->getLuaDir();

    m_startSceneIndex = 0;
    uint32_t savedStartSceneId = project->getStartSceneId();
    const auto& scenes = project->getScenes();
    for (int i = 0; i < (int)scenes.size(); i++) {
        if (scenes[i].id == savedStartSceneId) {
            m_startSceneIndex = i;
            break;
        }
    }

    m_availableKits = Generator::detectAvailableKits();
    m_cmakeKitIndex = 0; // 0 = "Default"
    std::string currentCxx = project->getCMakeCxxCompiler();
    std::string currentGen = project->getCMakeGenerator();
    if (!currentCxx.empty() || !currentGen.empty()) {
        for (size_t i = 0; i < m_availableKits.size(); i++) {
            if (m_availableKits[i].cxxCompiler == currentCxx && m_availableKits[i].generator == currentGen) {
                m_cmakeKitIndex = static_cast<int>(i + 1);
                break;
            }
        }
    }
}

void ProjectSettingsWindow::show() {
    if (!m_isOpen) return;

    ImGui::OpenPopup("Project Settings##ProjectSettingsModal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(500, 0),
        ImVec2(500, ImGui::GetMainViewport()->WorkSize.y * 0.9f)
    );

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal |
                             ImGuiWindowFlags_AlwaysAutoResize;

    bool popupOpen = ImGui::BeginPopupModal("Project Settings##ProjectSettingsModal", &m_isOpen, flags);

    if (popupOpen) {
        if (!m_isOpen) {
            ImGui::CloseCurrentPopup();
        } else {
            drawSettings();
        }
        ImGui::EndPopup();
    }
}

void ProjectSettingsWindow::drawSettings() {
    ImGui::PushItemWidth(-1);
    ImGui::BeginTable("project_settings", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp);
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    // Canvas width row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Canvas Width");
    ImGui::TableNextColumn();
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##CanvasWidth", &m_canvasWidth);
        if (m_canvasWidth < 1) m_canvasWidth = 1;
    }

    // Canvas height row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Canvas Height");
    ImGui::TableNextColumn();
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##CanvasHeight", &m_canvasHeight);
        if (m_canvasHeight < 1) m_canvasHeight = 1;
    }

    // Scaling mode row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Scaling Mode");
    ImGui::TableNextColumn();
    {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##ScalingMode", scalingModeNames[m_scalingModeIndex])) {
            for (int i = 0; i < scalingModeCount; i++) {
                bool isSelected = (m_scalingModeIndex == i);
                if (ImGui::Selectable(scalingModeNames[i], isSelected)) {
                    m_scalingModeIndex = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();
        drawScalingPreview(scalingModeValues[m_scalingModeIndex], m_canvasWidth, m_canvasHeight);
    }

    // Texture strategy row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Texture Strategy");
    ImGui::TableNextColumn();
    {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##TextureStrategy", textureStrategyNames[m_textureStrategyIndex])) {
            for (int i = 0; i < textureStrategyCount; i++) {
                bool isSelected = (m_textureStrategyIndex == i);
                if (ImGui::Selectable(textureStrategyNames[i], isSelected)) {
                    m_textureStrategyIndex = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    // Start scene row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Start Scene");
    ImGui::TableNextColumn();
    {
        const auto& scenes = m_project->getScenes();
        if (!scenes.empty()) {
            if (m_startSceneIndex < 0 || m_startSceneIndex >= (int)scenes.size()) {
                m_startSceneIndex = 0;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##StartScene", scenes[m_startSceneIndex].name.c_str())) {
                for (int i = 0; i < (int)scenes.size(); i++) {
                    bool isSelected = (m_startSceneIndex == i);
                    if (ImGui::Selectable(scenes[i].name.c_str(), isSelected)) {
                        m_startSceneIndex = i;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    // Assets directory row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Assets Directory");
    ImGui::TableNextColumn();
    {
        float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2;
        float inputWidth = ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x;

        Vector2 pathSize = Vector2(inputWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
        fs::path assetsDisplay = (m_assetsDir.empty() || m_assetsDir == ".") ? fs::path("<Project root>") : m_assetsDir;
        Widgets::pathDisplay("##AssetsPath", assetsDisplay, pathSize);

        ImGui::SameLine();
        if (ImGui::Button("Browse##assets")) {
            std::string defaultPath = m_project ? m_project->getProjectPath().string() : "";
            std::string selectedPath = FileDialogs::openFileDialog(defaultPath, FILE_DIALOG_ALL, true);
            if (!selectedPath.empty()) {
                std::error_code ec;
                fs::path relPath = fs::relative(fs::path(selectedPath), m_project->getProjectPath(), ec);
                m_assetsDir = (ec || relPath.empty()) ? fs::path(".") : relPath;
            }
        }
    }

    // Lua directory row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Lua Directory");
    ImGui::TableNextColumn();
    {
        float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2;
        float inputWidth = ImGui::GetContentRegionAvail().x - browseWidth - ImGui::GetStyle().ItemSpacing.x;

        Vector2 pathSize = Vector2(inputWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
        fs::path luaDisplay = (m_luaDir.empty() || m_luaDir == ".") ? fs::path("<Project root>") : m_luaDir;
        Widgets::pathDisplay("##LuaPath", luaDisplay, pathSize);

        ImGui::SameLine();
        if (ImGui::Button("Browse##lua")) {
            std::string defaultPath = m_project ? m_project->getProjectPath().string() : "";
            std::string selectedPath = FileDialogs::openFileDialog(defaultPath, FILE_DIALOG_ALL, true);
            if (!selectedPath.empty()) {
                std::error_code ec;
                fs::path relPath = fs::relative(fs::path(selectedPath), m_project->getProjectPath(), ec);
                m_luaDir = (ec || relPath.empty()) ? fs::path(selectedPath) : relPath;
            }
        }
    }

    // CMake Kit row
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Compiler");
    ImGui::TableNextColumn();
    {
        ImGui::SetNextItemWidth(-1);
        const char* currentLabel = (m_cmakeKitIndex == 0) ? "Default" : m_availableKits[m_cmakeKitIndex - 1].displayName.c_str();
        if (ImGui::BeginCombo("##CMakeKit", currentLabel)) {
            bool isSelected = (m_cmakeKitIndex == 0);
            if (ImGui::Selectable("Default", isSelected)) {
                m_cmakeKitIndex = 0;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            for (size_t i = 0; i < m_availableKits.size(); i++) {
                isSelected = (m_cmakeKitIndex == static_cast<int>(i + 1));
                if (ImGui::Selectable(m_availableKits[i].displayName.c_str(), isSelected)) {
                    m_cmakeKitIndex = static_cast<int>(i + 1);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (m_cmakeKitIndex > 0) {
            const auto& kit = m_availableKits[m_cmakeKitIndex - 1];
            ImGui::TextDisabled("C: %s  CXX: %s", kit.cCompiler.c_str(), kit.cxxCompiler.c_str());
        }
    }

    ImGui::EndTable();
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Buttons ---
    float windowWidth = ImGui::GetWindowSize().x;
    float buttonsWidth = 250;
    ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

    if (ImGui::Button("OK", ImVec2(120, 0))) {
        m_project->setCanvasSize(m_canvasWidth, m_canvasHeight);
        m_project->setScalingMode(scalingModeValues[m_scalingModeIndex]);
        m_project->setTextureStrategy(textureStrategyValues[m_textureStrategyIndex]);
        m_project->setAssetsDir(m_assetsDir);
        m_project->setLuaDir(m_luaDir);
        const auto& scenes = m_project->getScenes();
        if (m_startSceneIndex >= 0 && m_startSceneIndex < (int)scenes.size()) {
            m_project->setStartSceneId(scenes[m_startSceneIndex].id);
        }
        if (m_cmakeKitIndex > 0) {
            const auto& kit = m_availableKits[m_cmakeKitIndex - 1];
            m_project->setCMakeKit(kit.cCompiler, kit.cxxCompiler, kit.generator);
        } else {
            m_project->setCMakeKit("", "", "");
        }
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }
}

} // namespace doriax::editor
