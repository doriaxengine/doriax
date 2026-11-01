// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ScriptCreateDialog.h"
#include "external/IconsFontAwesome6.h"
#include "Scene.h"
#include "Factory.h"
#include "Theme.h"
#include "util/FileDialogs.h"
#include "util/FileUtils.h"
#include "util/ProjectUtils.h"
#include "util/ScriptParser.h"
#include "util/UIUtils.h"
#include "util/Util.h"
#include <algorithm>
#include <fstream>
#include <initializer_list>

namespace doriax {
namespace editor {

namespace {

fs::path findCompanion(const fs::path& path, std::initializer_list<const char*> extensions) {
    std::error_code ec;
    for (const char* extension : extensions) {
        fs::path companion = path;
        companion.replace_extension(extension);
        ec.clear();
        if (fs::is_regular_file(companion, ec)) return companion;
    }
    return {};
}

}

// ImGui input filter: while typing, restrict the class/base name to characters
// that are legal in a C++ identifier - any other character (space, punctuation,
// ...) is converted to '_' immediately. Structural normalization that cannot be
// done per-character without fighting the typist - stripping leading/trailing
// underscores, prefixing a leading digit, escaping C++ keywords - is deferred to
// Factory::toIdentifier(), applied when editing finishes, so the committed name
// always equals the identifier the code generator emits.
int ScriptCreateDialog::classNameCharFilter(ImGuiInputTextCallbackData* data) {
    ImWchar c = data->EventChar;
    bool isValid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
    if (!isValid) {
        data->EventChar = '_';
    }
    return 0;
}

void ScriptCreateDialog::open(Scene* scene,
                              Entity entity,
                              const fs::path& projectPath,
                              const fs::path& luaPath,
                              const std::string& defaultBaseName,
                              std::function<void(const fs::path&, const fs::path&, const std::string&, ScriptType)> onCreate,
                              std::function<void()> onCancel) {
    m_isOpen = true;
    m_scene = scene;
    m_entity = entity;
    m_projectPath = projectPath;
    m_luaPath = luaPath;
    m_selectedPath = projectPath.string();
    m_creationKind = CreationKind::CPP_SUBCLASS;
    m_existingScriptType = ScriptType::CPP;
    m_attachExisting = false;
    m_existingHeaderPath.clear();
    m_existingSourcePath.clear();
    m_attachError.clear();
    m_onCreate = onCreate;
    m_onCancel = onCancel;
    std::string base = defaultBaseName.empty() ? "NewScript" : sanitizeClassName(defaultBaseName);
    strncpy(m_baseNameBuffer, base.c_str(), sizeof(m_baseNameBuffer) - 1);
    m_baseNameBuffer[sizeof(m_baseNameBuffer) - 1] = '\0';
}

std::string ScriptCreateDialog::sanitizeClassName(const std::string& in) const {
    if (in.empty()) return "NewScript";
    // Reuse the same identifier validation the code generator/loader rely on
    // (Factory::toIdentifier): collapses invalid characters to underscores,
    // strips leading/trailing underscores, ensures a valid identifier start and
    // avoids C++ keywords - so the class name always compiles.
    return Factory::toIdentifier(in);
}

std::string ScriptCreateDialog::inferClassName(const fs::path& headerPath,
                                               const std::string& fallback) const {
    if (!headerPath.empty()) {
        if (const auto className = ScriptParser::findScriptClassName(headerPath)) {
            return *className;
        }
    }
    return fallback;
}

fs::path ScriptCreateDialog::makeHeaderPath(const std::string& className) const {
    return fs::path(m_selectedPath) / (className + ".h");
}

fs::path ScriptCreateDialog::makeSourcePath(const std::string& className) const {
    return fs::path(m_selectedPath) / (className + ".cpp");
}

fs::path ScriptCreateDialog::makeLuaPath(const std::string& moduleName) const {
    return fs::path(m_selectedPath) / (moduleName + ".lua");
}

void ScriptCreateDialog::writeFiles(const fs::path& headerPath,
                                      const fs::path& sourcePath,
                                      const std::string& classOrModuleName,
                                      CreationKind kind) {
    bool isCppSubclass = kind == CreationKind::CPP_SUBCLASS;
    bool isCppScriptBase = kind == CreationKind::CPP_SCRIPT_CLASS;
    bool isLua = kind == CreationKind::LUA;

    if (isCppSubclass || isCppScriptBase) {
        fs::create_directories(headerPath.parent_path());

        if (isCppSubclass) {
            ProjectUtils::EntityClassInfo parentInfo = ProjectUtils::getEntityClassInfo(m_scene, m_entity);
            std::string parentClass = parentInfo.name;
            bool hasTransform = parentInfo.derivesObject;
            bool isMesh = parentInfo.derivesMesh;

            // Header
            {
                std::ofstream h(headerPath, std::ios::binary | std::ios::trunc);
                if (h) {
                    h << "#pragma once\n\n";
                    h << "#include \"Shape.h\"\n";
                    h << "#include \"" << parentClass << ".h\"\n";
                    h << "#include \"ScriptProperty.h\"\n\n";
                    h << "class " << classOrModuleName << " : public doriax::" << parentClass << " {\n";
                    h << "public:\n";
                    h << "    // Example properties\n";
                    h << "    DPROPERTY(\"Is Active\")\n";
                    h << "    bool isActive = true;\n\n";
                    if (hasTransform){
                        h << "    DPROPERTY(\"Speed\")\n";
                        h << "    float speed = 5.0f;\n\n";
                        h << "    DPROPERTY(\"Target Position\")\n";
                        h << "    doriax::Vector3 targetPosition = doriax::Vector3(0, 0, 0);\n\n";
                        if (isMesh){
                            h << "    DPROPERTY(\"Mesh Color\", Color4)\n";
                            h << "    doriax::Vector4 meshColor = doriax::Vector4(1, 1, 1, 1);\n\n";
                        }
                    }
                    h << "    " << classOrModuleName << "(doriax::Scene* scene, doriax::Entity entity);\n";
                    h << "    virtual ~" << classOrModuleName << "();\n\n";
                    h << "    void onUpdate();\n";
                    h << "};\n";
                }
            }

            // Source
            {
                std::ofstream c(sourcePath, std::ios::binary | std::ios::trunc);
                if (c) {
                    c << "#include \"" << headerPath.filename().string() << "\"\n\n";
                    c << "using namespace doriax;\n\n";
                    c << classOrModuleName << "::" << classOrModuleName << "(Scene* scene, Entity entity): " << parentClass << "(scene, entity) {\n";
                    c << "    REGISTER_ENGINE_EVENT(onUpdate);\n\n";
                    c << "}\n\n";
                    c << classOrModuleName << "::~" << classOrModuleName << "() {\n";
                    c << "\n";
                    c << "}\n\n";
                    c << "void " << classOrModuleName << "::onUpdate() {\n";
                    c << "    if (!isActive) return;\n\n";
                    if (hasTransform){
                        c << "    // Example: move toward the target at 'speed' units per second.\n";
                        c << "    // moveTowards() clamps the step to the remaining distance, so it\n";
                        c << "    // never overshoots - even on the first frame where deltaTime is large.\n";
                        c << "    float deltaTime = Engine::getDeltatime();\n";
                        c << "    Vector3 currentPos = getPosition();\n";
                        c << "    setPosition(currentPos.moveTowards(targetPosition, speed * deltaTime));\n";
                        if (isMesh){
                            c << "    setColor(meshColor);\n";
                        }
                    }
                    c << "}\n\n";
                }
            }
        } else if (isCppScriptBase) {
            // Script Class
            // Header
            {
                std::ofstream h(headerPath, std::ios::binary | std::ios::trunc);
                if (h) {
                    h << "#pragma once\n\n";
                    h << "#include \"ScriptBase.h\"\n";
                    h << "#include \"Engine.h\"\n";
                    h << "#include \"ScriptProperty.h\"\n\n";
                    h << "class " << classOrModuleName << " : public doriax::ScriptBase {\n";
                    h << "public:\n";
                    h << "    // Example properties - you can add more!\n";
                    h << "    DPROPERTY(\"Speed\")\n";
                    h << "    float speed = 5.0f;\n\n";
                    h << "    DPROPERTY(\"Is Active\")\n";
                    h << "    bool isActive = true;\n\n";
                    h << "    DPROPERTY(\"Counter\")\n";
                    h << "    int counter = 0;\n\n";
                    h << "    " << classOrModuleName << "(doriax::Scene* scene, doriax::Entity entity);\n";
                    h << "    ~" << classOrModuleName << "();\n\n";
                    h << "    void onUpdate();\n";
                    h << "};\n";
                }
            }

            // Source
            {
                std::ofstream c(sourcePath, std::ios::binary | std::ios::trunc);
                if (c) {
                    c << "#include \"" << headerPath.filename().string() << "\"\n\n";
                    c << "using namespace doriax;\n\n";
                    c << classOrModuleName << "::" << classOrModuleName << "(Scene* scene, Entity entity): ScriptBase(scene, entity) {\n";
                    c << "    REGISTER_ENGINE_EVENT(onUpdate);\n\n";
                    c << "}\n\n";
                    c << classOrModuleName << "::~" << classOrModuleName << "() {\n";
                    c << "\n";
                    c << "}\n\n";
                    c << "void " << classOrModuleName << "::onUpdate() {\n";
                    c << "    if (!isActive) return;\n\n";
                    c << "    // Example: Increment counter every frame\n";
                    c << "    counter++;\n";
                    c << "    if (counter % 60 == 0) {\n";
                    c << "        Log::print(\"Counter: %d\\n\", counter);\n";
                    c << "    }\n";
                    c << "}\n\n";
                }
            }
        }
    }

    // Lua generation
    if (isLua) {
        fs::create_directories(sourcePath.parent_path());
        std::ofstream f(sourcePath, std::ios::binary | std::ios::trunc);
        if (f) {
            f << "-- " << classOrModuleName << ".lua\n";
            f << "-- Auto-generated by Doriax Editor\n\n";

            // Script table used as the instance prototype
            f << "local " << classOrModuleName << " = {\n";
            f << "    -- Editor-exposed properties\n";
            f << "    properties = {\n";
            f << "        {\n";
            f << "            name = \"speed\",\n";
            f << "            displayName = \"Speed\",\n";
            f << "            type = \"float\",\n";
            f << "            default = 5.0\n";
            f << "        },\n";
            f << "        {\n";
            f << "            name = \"isActive\",\n";
            f << "            displayName = \"Is Active\",\n";
            f << "            type = \"bool\",\n";
            f << "            default = true\n";
            f << "        }\n";
            f << "    }\n";
            f << "}\n\n";

            // init(self) – called once by the engine (Project::initializeLuaScripts)
            f << "function " << classOrModuleName << ":init()\n";
            f << "    -- 'self.scene' and 'self.entity' are provided by the engine\n";
            f << "    RegisterEngineEvent(self, \"onUpdate\")\n";
            f << "    self.counter = 0\n";
            f << "end\n\n";

            // onUpdate(self) – optional; engine may call this every frame
            f << "function " << classOrModuleName << ":onUpdate()\n";
            f << "    if not self.isActive then return end\n\n";
            f << "    self.counter = self.counter + 1\n";
            f << "    if self.counter % 60 == 0 then\n";
            f << "        Log.print(\"[\" .. tostring(self.entity) .. \"] Counter: \" .. self.counter)\n";
            f << "    end\n";
            f << "end\n\n";

            f << "return " << classOrModuleName << "\n";
        }
    }
}

void ScriptCreateDialog::finalizeCreation(const fs::path& headerPath,
                                          const fs::path& sourcePath,
                                          const std::string& name) {
    const bool isLua = m_creationKind == CreationKind::LUA;
    const ScriptType type = isLua ? ScriptType::LUA : ScriptType::CPP;

    writeFiles(headerPath, sourcePath, name, m_creationKind);

    if (m_onCreate) {
        // Lua entries resolve through "lua://", C++ sources stay project-relative
        const fs::path sourceRoot = isLua ? m_luaPath : m_projectPath;

        fs::path relHeader, relSource;
        if (!headerPath.empty()) relHeader = fs::relative(headerPath, m_projectPath);
        if (!sourcePath.empty()) relSource = fs::relative(sourcePath, sourceRoot);

        m_onCreate(relHeader, relSource, name, type);
    }

    m_isOpen = false;
}

bool ScriptCreateDialog::finalizeAttachment(const fs::path& headerPath,
                                            const fs::path& sourcePath,
                                            const std::string& name,
                                            ScriptType type) {
    if (m_onCreate) {
        const fs::path sourceRoot = type == ScriptType::LUA ? m_luaPath : m_projectPath;
        std::error_code ec;
        fs::path relHeader;
        fs::path relSource;

        if (!headerPath.empty()) {
            relHeader = fs::relative(headerPath, m_projectPath, ec);
            if (ec) {
                m_attachError = "Could not resolve the header path: " + ec.message();
                return false;
            }
        }
        ec.clear();
        relSource = fs::relative(sourcePath, sourceRoot, ec);
        if (ec) {
            m_attachError = "Could not resolve the script path: " + ec.message();
            return false;
        }

        m_onCreate(relHeader, relSource, name, type);
    }

    m_isOpen = false;
    return true;
}

void ScriptCreateDialog::selectExistingFile(bool selectHeader) {
    fs::path currentPath = selectHeader ? m_existingHeaderPath : m_existingSourcePath;
    const std::string startDirectory = currentPath.empty()
        ? m_projectPath.string()
        : currentPath.parent_path().string();
    const std::string selected = FileDialogs::openFileDialog(startDirectory, FILE_DIALOG_SCRIPT);
    if (selected.empty()) return;

    const fs::path selectedPath = fs::path(selected).lexically_normal();
    if (Util::isLuaFile(selectedPath.string())) {
        if (selectHeader) {
            m_attachError = "Select a C++ header (.h, .hh, .hpp, or .hxx).";
            return;
        }
        if (!Util::isInsidePath(selectedPath, m_luaPath)) {
            m_attachError = "Lua scripts must be inside the configured Lua directory.";
            return;
        }
        m_existingScriptType = ScriptType::LUA;
        m_existingSourcePath = selectedPath;
        m_existingHeaderPath.clear();
    } else if (Util::isHeaderFile(selectedPath.string())) {
        if (!Util::isInsidePath(selectedPath, m_projectPath)) {
            m_attachError = "C++ scripts must be inside the project directory.";
            return;
        }
        m_existingScriptType = ScriptType::CPP;
        m_existingHeaderPath = selectedPath;
        m_existingSourcePath = findCompanion(selectedPath, {".cpp", ".cc", ".cxx"});
    } else if (Util::isSourceFile(selectedPath.string())) {
        if (selectHeader) {
            m_attachError = "Select a C++ header (.h, .hh, .hpp, or .hxx).";
            return;
        }
        if (!Util::isInsidePath(selectedPath, m_projectPath)) {
            m_attachError = "C++ scripts must be inside the project directory.";
            return;
        }
        m_existingScriptType = ScriptType::CPP;
        m_existingSourcePath = selectedPath;
        m_existingHeaderPath = findCompanion(selectedPath, {".h", ".hpp", ".hh", ".hxx"});
    } else {
        m_attachError = "Select a Lua or C++ script file.";
        return;
    }

    std::string inferredName = sanitizeClassName(selectedPath.stem().string());
    if (m_existingScriptType == ScriptType::CPP && !m_existingHeaderPath.empty()) {
        inferredName = inferClassName(m_existingHeaderPath, inferredName);
    }
    strncpy(m_baseNameBuffer, inferredName.c_str(), sizeof(m_baseNameBuffer) - 1);
    m_baseNameBuffer[sizeof(m_baseNameBuffer) - 1] = '\0';
    m_attachError.clear();
}

void ScriptCreateDialog::show() {
    if (!m_isOpen) return;

    const char* popupName = "Create Script##CreateScriptModal";
    ImGui::OpenPopup(popupName);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetWorkCenter();
    const float dialogWidth = std::min(Theme::dpi(360.0f), viewport->WorkSize.x * 0.9f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal;

    bool popupOpen = ImGui::BeginPopupModal(popupName, nullptr, flags);
    if (!popupOpen) {
        if (m_isOpen) {
            m_isOpen = false;
            if (m_onCancel) m_onCancel();
        }
        return;
    }

    if (ImGui::RadioButton("Create new", !m_attachExisting)) {
        m_attachExisting = false;
        m_attachError.clear();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Attach existing", m_attachExisting)) {
        m_attachExisting = true;
        m_attachError.clear();
    }

    if (!m_attachExisting) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Template:");
        if (ImGui::RadioButton("C++ Subclass", (int*)&m_creationKind, (int)CreationKind::CPP_SUBCLASS)) {}
        if (ImGui::RadioButton("C++ Script Class", (int*)&m_creationKind, (int)CreationKind::CPP_SCRIPT_CLASS)) {}
        if (ImGui::RadioButton("Lua Script", (int*)&m_creationKind, (int)CreationKind::LUA)) {}
    }

    bool isLua = m_attachExisting
        ? m_existingScriptType == ScriptType::LUA
        : m_creationKind == CreationKind::LUA;
    bool isCpp = !isLua;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    fs::path rootPath = isLua ? m_luaPath : m_projectPath;

    if (m_attachExisting) {
        ImGui::TextWrapped("Choose an existing script to attach. Its files will not be changed.");
        ImGui::Spacing();

        ImGui::TextUnformatted("Script File:");
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Browse Script...##existing_source")) {
            selectExistingFile(false);
            isLua = m_existingScriptType == ScriptType::LUA;
            isCpp = !isLua;
            rootPath = isLua ? m_luaPath : m_projectPath;
        }
        ImGui::SameLine();
        if (m_existingSourcePath.empty()) {
            ImGui::TextDisabled("No file selected");
        } else {
            std::error_code ec;
            const fs::path relative = fs::relative(m_existingSourcePath, rootPath, ec);
            const std::string label = ec ? m_existingSourcePath.string() : relative.generic_string();
            ImGui::TextUnformatted(label.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m_existingSourcePath.string().c_str());
        }

        if (isCpp && (!m_existingSourcePath.empty() || !m_existingHeaderPath.empty())) {
            ImGui::TextUnformatted("Header File:");
            if (ImGui::Button(ICON_FA_FOLDER_OPEN " Browse Header...##existing_header"))
                selectExistingFile(true);
            ImGui::SameLine();
            if (m_existingHeaderPath.empty()) {
                ImGui::TextDisabled("No file selected");
            } else {
                std::error_code ec;
                const fs::path relative = fs::relative(m_existingHeaderPath, m_projectPath, ec);
                const std::string label = ec ? m_existingHeaderPath.string() : relative.generic_string();
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", m_existingHeaderPath.string().c_str());
            }
        }

        if (!m_attachError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.5f, 0.f, 1.f));
            ImGui::TextWrapped("%s", m_attachError.c_str());
            ImGui::PopStyleColor();
        } else if (isCpp && (!m_existingSourcePath.empty() || !m_existingHeaderPath.empty()) &&
                   (m_existingSourcePath.empty() || m_existingHeaderPath.empty())) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Choose both files. A same-named companion is selected automatically when found.");
            ImGui::PopStyleColor();
        }

        if (!m_existingSourcePath.empty() || !m_existingHeaderPath.empty()) {
            const char* detectedType = m_existingScriptType == ScriptType::LUA
                ? "Lua Script"
                : "C++ Script";
            ImGui::TextDisabled("Detected type: %s", detectedType);
        }
    } else {
        // Browsing is limited to the root the new script is stored relative to: a folder
        // above it would be saved as an unresolvable "../" path.
        if (!Util::isInsidePath(fs::path(m_selectedPath), rootPath)) {
            m_selectedPath = rootPath.string();
        }

        if (ImGui::BeginChild("DirBrowser", ImVec2(0.0f, Theme::dpi(200.0f)), true)) {
            if (ImGui::BeginTable("DirTree", 1, ImGuiTableFlags_Resizable)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                bool isRootSelected = fs::path(m_selectedPath).lexically_normal() == rootPath.lexically_normal();

                if (ImGui::TreeNodeEx("##root",
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanFullWidth |
                    (isRootSelected ? ImGuiTreeNodeFlags_Selected : 0))) {

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "%s", ICON_FA_FOLDER_OPEN);
                    ImGui::SameLine();
                    ImGui::Text("%s", rootPath == m_projectPath ? "Project Root" : FileUtils::pathToUtf8(rootPath.filename()).c_str());
                    if (ImGui::IsItemClicked() ||
                        (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))) {
                        m_selectedPath = rootPath.string();
                    }

                    UIUtils::directoryTreeBrowser(rootPath, m_selectedPath);
                    ImGui::TreePop();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(m_attachExisting
        ? "Class / Module Name:"
        : (isLua ? "Module Name:" : "Class Name:"));
    ImGui::SetNextItemWidth(-1);

    bool enterPressed = ImGui::InputText(
        "##basename",
        m_baseNameBuffer,
        sizeof(m_baseNameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
        classNameCharFilter
    );

    // When editing finishes (Enter or focus loss), snap the field to the exact
    // identifier that will be generated - applying the leading/trailing '_' strip,
    // leading-digit prefix and keyword escaping that the per-char filter can't do
    // mid-typing - so the visible name stays identical to the created class name.
    // Skipped while empty so an intentionally cleared field isn't auto-refilled.
    if (ImGui::IsItemDeactivatedAfterEdit() && m_baseNameBuffer[0] != '\0') {
        std::string canonical = sanitizeClassName(m_baseNameBuffer);
        strncpy(m_baseNameBuffer, canonical.c_str(), sizeof(m_baseNameBuffer) - 1);
        m_baseNameBuffer[sizeof(m_baseNameBuffer) - 1] = '\0';
    }

    std::string baseName = m_baseNameBuffer;
    bool hasBase = !baseName.empty();
    std::string name = sanitizeClassName(baseName);

    fs::path headerPath;
    fs::path sourcePath;
    if (isCpp) {
        headerPath = makeHeaderPath(name);
        sourcePath = makeSourcePath(name);
    } else if (isLua) {
        sourcePath = makeLuaPath(name);
    }

    std::error_code existsEc;
    const bool headerExists = isCpp && fs::is_regular_file(headerPath, existsEc);
    existsEc.clear();
    const bool sourceExists = fs::is_regular_file(sourcePath, existsEc);
    const bool anyCreateFileExists = headerExists || sourceExists;
    const bool allCreateFilesExist = sourceExists && (!isCpp || headerExists);

    bool attachFilesValid = false;
    if (m_attachExisting) {
        std::error_code sourceEc;
        const bool validSource = fs::is_regular_file(m_existingSourcePath, sourceEc) &&
            (isLua ? Util::isLuaFile(m_existingSourcePath.string())
                   : Util::isSourceFile(m_existingSourcePath.string())) &&
            Util::isInsidePath(m_existingSourcePath, rootPath);
        std::error_code headerEc;
        const bool validHeader = !isCpp ||
            (fs::is_regular_file(m_existingHeaderPath, headerEc) &&
             Util::isHeaderFile(m_existingHeaderPath.string()) &&
             Util::isInsidePath(m_existingHeaderPath, m_projectPath));
        attachFilesValid = validSource && validHeader;
    } else {
        if (isCpp) {
            ImGui::TextWrapped("Will create:\n  %s\n  %s",
                FileUtils::pathToGenericUtf8(fs::relative(headerPath, m_projectPath)).c_str(),
                FileUtils::pathToGenericUtf8(fs::relative(sourcePath, m_projectPath)).c_str());
        } else {
            ImGui::TextWrapped("Will create:\n  %s",
                FileUtils::pathToGenericUtf8(fs::relative(sourcePath, rootPath)).c_str());
        }

        if (anyCreateFileExists) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.5f, 0.f, 1.f));
            if (allCreateFilesExist) {
                ImGui::TextWrapped(
                    "Files with this name already exist. Create will ask whether to attach or replace them.");
            } else {
                ImGui::TextWrapped(
                    "Only part of this script exists. Create will ask before replacing existing files.");
            }
            ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();

    const float windowWidth = ImGui::GetWindowSize().x;
    auto cancelDialog = [this]() {
        m_isOpen = false;
        if (m_onCancel) m_onCancel();
        ImGui::CloseCurrentPopup();
    };

    const float actionButtonWidth = Theme::dpi(120.0f);
    const float actionButtonsWidth = actionButtonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((windowWidth - actionButtonsWidth) * 0.5f);

    if (m_attachExisting) {
        ImGui::BeginDisabled(!hasBase || !attachFilesValid);
        if (ImGui::Button("Attach", ImVec2(actionButtonWidth, 0)) ||
                (enterPressed && hasBase && attachFilesValid)) {
            if (finalizeAttachment(m_existingHeaderPath, m_existingSourcePath, name, m_existingScriptType))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(actionButtonWidth, 0)))
            cancelDialog();
    } else {
        ImGui::BeginDisabled(!hasBase);
        if (ImGui::Button("Create", ImVec2(actionButtonWidth, 0)) || (enterPressed && hasBase)) {
            if (anyCreateFileExists) {
                m_attachError.clear();
                ImGui::OpenPopup("Script Files Already Exist");
            } else {
                finalizeCreation(headerPath, sourcePath, name);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(actionButtonWidth, 0)))
            cancelDialog();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0.0f), ImGuiCond_Always);
    ImGuiWindowFlags alertFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_Modal;
    if (ImGui::BeginPopupModal("Script Files Already Exist", nullptr, alertFlags)) {
        if (allCreateFilesExist) {
            ImGui::TextUnformatted("These script files already exist.\nAttach them unchanged or replace their contents?");
        } else {
            ImGui::TextUnformatted("Only part of this script exists.\nReplace the existing files and create the missing files?");
        }
        if (!m_attachError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.5f, 0.f, 1.f));
            ImGui::TextWrapped("%s", m_attachError.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        const float alertButtonWidth = Theme::dpi(105.0f);
        const int alertButtonCount = allCreateFilesExist ? 3 : 2;
        const float alertButtonsWidth = alertButtonWidth * alertButtonCount +
            ImGui::GetStyle().ItemSpacing.x * (alertButtonCount - 1);
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - alertButtonsWidth) * 0.5f);

        if (allCreateFilesExist) {
            if (ImGui::Button("Attach Existing", ImVec2(alertButtonWidth, 0))) {
                const ScriptType type = m_creationKind == CreationKind::LUA
                    ? ScriptType::LUA
                    : ScriptType::CPP;
                const std::string attachName = type == ScriptType::CPP
                    ? inferClassName(headerPath, name)
                    : name;
                if (finalizeAttachment(headerPath, sourcePath, attachName, type))
                    ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Replace Files", ImVec2(alertButtonWidth, 0))) {
            finalizeCreation(headerPath, sourcePath, name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(alertButtonWidth, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!m_isOpen && ImGui::IsPopupOpen(popupName)) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace editor
} // namespace doriax
