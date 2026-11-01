// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderForkDialog.h"

#include "external/IconsFontAwesome6.h"
#include "util/FileUtils.h"
#include "util/ProjectUtils.h"
#include "util/UIUtils.h"
#include "util/Util.h"

#include <cstring>

namespace doriax::editor {

int ShaderForkDialog::shaderNameCharFilter(ImGuiInputTextCallbackData* data) {
    ImWchar c = data->EventChar;
    bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_';
    if (!valid)
        data->EventChar = '_';
    return 0;
}

const char* ShaderForkDialog::shaderTypeName(ShaderType shaderType) {
    switch (shaderType) {
        case ShaderType::MESH:   return "Mesh";
        case ShaderType::DEPTH:  return "Depth";
        case ShaderType::UI:     return "UI";
        case ShaderType::POINTS: return "Points";
        case ShaderType::LINES:  return "Lines";
        case ShaderType::SKYBOX: return "Sky";
        case ShaderType::POSTPROCESS: return "Post-process";
        case ShaderType::WATER:  return "Water";
        case ShaderType::UNDERWATER: return "Underwater";
        default:                 return "Unknown";
    }
}

void ShaderForkDialog::open(
        Project* project, ShaderType shaderType, const std::string& defaultBaseName,
        std::function<void(const std::filesystem::path&, const std::string&, bool)> onCreate,
        std::function<void()> onCancel) {
    m_isOpen = project != nullptr;
    m_project = project;
    m_shaderType = shaderType;
    m_onCreate = std::move(onCreate);
    m_onCancel = std::move(onCancel);
    m_forkIncludes = false;
    if (!project)
        return;

    m_projectPath = project->getProjectPath();
    std::filesystem::path defaultDir = m_projectPath / ProjectUtils::defaultShaderForkDir();
    std::error_code ec;
    std::filesystem::path selected = std::filesystem::is_directory(defaultDir, ec)
        ? defaultDir
        : m_projectPath;
    m_selectedPath = selected.lexically_normal().string();

    std::string name = ProjectUtils::makeUniqueShaderName(selected, shaderType, defaultBaseName);
    std::strncpy(m_nameBuffer, name.c_str(), sizeof(m_nameBuffer) - 1);
    m_nameBuffer[sizeof(m_nameBuffer) - 1] = '\0';
    m_previewDirty = true;
}

const ShaderForkDialog::ForkPreview& ShaderForkDialog::refreshPreview(const std::filesystem::path& directory,
                                                                      const std::string& name) {
    if (!m_previewDirty && directory == m_previewDirectory && name == m_previewName &&
            m_forkIncludes == m_previewForkIncludes) {
        return m_preview;
    }

    ProjectUtils::ShaderForkPlan plan =
        ProjectUtils::prepareShaderFork(m_project, m_shaderType, directory, name, m_forkIncludes);

    auto relative = [this](const std::filesystem::path& absolute) {
        std::error_code ec;
        return FileUtils::pathToGenericUtf8(std::filesystem::relative(absolute, m_projectPath, ec));
    };

    m_preview = {};
    m_preview.valid = plan.valid;
    m_preview.error = plan.error;
    if (plan.valid) {
        m_preview.vertPath = relative(plan.vertPath);
        m_preview.fragPath = relative(plan.fragPath);
        m_preview.forkDir = relative(plan.forkDirPath);
        for (const auto& include : plan.includeFiles)
            m_preview.includeFiles.push_back(relative(include.path));
    }

    m_previewDirectory = directory;
    m_previewName = name;
    m_previewForkIncludes = m_forkIncludes;
    m_previewDirty = false;
    return m_preview;
}

void ShaderForkDialog::show() {
    if (!m_isOpen || !m_project)
        return;

    const char* popupName = "Fork Shader##ForkShaderModal";
    ImGui::OpenPopup(popupName);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(380, 0), ImVec2(650, ImGui::GetMainViewport()->WorkSize.y * 0.9f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_Modal;
    if (!ImGui::BeginPopupModal(popupName, nullptr, flags)) {
        if (m_isOpen) {
            m_isOpen = false;
            if (m_onCancel)
                m_onCancel();
        }
        return;
    }

    ImGui::Text("Fork the built-in %s shader", shaderTypeName(m_shaderType));
    ImGui::Separator();

    if (!Util::isInsidePath(std::filesystem::path(m_selectedPath), m_projectPath))
        m_selectedPath = m_projectPath.string();

    if (ImGui::BeginChild("ShaderForkDirBrowser", ImVec2(360, 200), true)) {
        if (ImGui::BeginTable("ShaderForkDirTree", 1, ImGuiTableFlags_Resizable)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_SpanFullWidth;
            if (std::filesystem::path(m_selectedPath).lexically_normal() == m_projectPath.lexically_normal())
                rootFlags |= ImGuiTreeNodeFlags_Selected;
            if (ImGui::TreeNodeEx("##shader_fork_root", rootFlags)) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "%s", ICON_FA_FOLDER_OPEN);
                ImGui::SameLine();
                ImGui::TextUnformatted("Project Root");
                if (ImGui::IsItemClicked() ||
                        (ImGui::IsMouseClicked(0) &&
                         ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))) {
                    m_selectedPath = m_projectPath.string();
                }
                UIUtils::directoryTreeBrowser(m_projectPath, m_selectedPath);
                ImGui::TreePop();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::TextUnformatted("Shader Name:");
    ImGui::SetNextItemWidth(-1);
    bool enterPressed = ImGui::InputText(
        "##shader_fork_name", m_nameBuffer, sizeof(m_nameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
        shaderNameCharFilter);

    ImGui::Checkbox("Also fork shader includes", &m_forkIncludes);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy the built-in includes so you can edit them.\n"
                          "The fork is placed in a folder of its own so those copies\n"
                          "override the engine library only for this shader.");
    }

    std::error_code ec;
    std::filesystem::path directory = std::filesystem::relative(
        std::filesystem::path(m_selectedPath), m_projectPath, ec);
    if (ec || directory.empty())
        directory = ".";
    std::string name = m_nameBuffer;
    const ForkPreview& preview = refreshPreview(directory, name);

    if (preview.valid) {
        ImGui::TextUnformatted("Will create:");
        ImGui::TextWrapped("  %s\n  %s", preview.vertPath.c_str(), preview.fragPath.c_str());

        if (m_forkIncludes && preview.includeFiles.empty()) {
            ImGui::TextDisabled("  This shader uses no built-in .glsl includes");
        } else if (m_forkIncludes) {
            ImGui::TextDisabled("  %zu include files under %s/",
                preview.includeFiles.size(), preview.forkDir.c_str());
            if (ImGui::BeginChild("ShaderForkIncludePreview", ImVec2(0, 110), true)) {
                for (const std::string& include : preview.includeFiles)
                    ImGui::TextUnformatted(include.c_str());
            }
            ImGui::EndChild();
        }
    } else {
        ImGui::TextColored(ImVec4(1.f, 0.5f, 0.f, 1.f), "%s", preview.error.c_str());
    }

    ImGui::Separator();

    float windowWidth = ImGui::GetWindowSize().x;
    float buttonsWidth = 250;
    ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

    ImGui::BeginDisabled(!preview.valid);
    if (ImGui::Button("Create", ImVec2(120, 0)) || (enterPressed && preview.valid)) {
        if (m_onCreate)
            m_onCreate(directory, name, m_forkIncludes);
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        m_isOpen = false;
        if (m_onCancel)
            m_onCancel();
        ImGui::CloseCurrentPopup();
    }

    if (!m_isOpen && ImGui::IsPopupOpen(popupName))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace doriax::editor
