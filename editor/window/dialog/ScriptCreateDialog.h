// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <filesystem>
#include <string>
#include "imgui.h"
#include "component/ScriptComponent.h"
#include "Entity.h"
#include "Scene.h"

namespace doriax {
namespace editor {

namespace fs = std::filesystem;

class ScriptCreateDialog {
private:
    enum class CreationKind {
        CPP_SUBCLASS,
        CPP_SCRIPT_CLASS,
        LUA,
        LOGIC,
    };

    bool m_isOpen = false;
    fs::path m_projectPath;
    fs::path m_luaPath;
    std::string m_selectedPath;
    char m_baseNameBuffer[128] = "";
    CreationKind m_creationKind = CreationKind::CPP_SUBCLASS;
    ScriptType m_existingScriptType = ScriptType::CPP;
    bool m_attachExisting = false;
    fs::path m_existingHeaderPath;
    fs::path m_existingSourcePath;
    std::string m_attachError;
    Scene* m_scene = nullptr;
    Entity m_entity = NULL_ENTITY;

    std::function<void(const fs::path&, const fs::path&, const std::string&, ScriptType)> m_onCreate;
    std::function<void()> m_onCancel;

    std::string sanitizeClassName(const std::string& in) const;
    std::string inferClassName(const fs::path& headerPath, const std::string& fallback) const;

    fs::path makeHeaderPath(const std::string& className) const;
    fs::path makeSourcePath(const std::string& className) const;
    fs::path makeLuaPath(const std::string& moduleName) const;

    void writeFiles(const fs::path& headerPath,
                    const fs::path& sourcePath,
                    const std::string& classOrModuleName,
                    CreationKind kind);

    void finalizeCreation(const fs::path& headerPath,
                          const fs::path& sourcePath,
                          const std::string& name);

    bool finalizeAttachment(const fs::path& headerPath,
                            const fs::path& sourcePath,
                            const std::string& name,
                            ScriptType type);

    void selectExistingFile(bool selectHeader);

public:
    ScriptCreateDialog() = default;
    ~ScriptCreateDialog() = default;

    void open(Scene* scene,
              Entity entity,
              const fs::path& projectPath,
              const fs::path& luaPath,
              const std::string& defaultBaseName,
              std::function<void(const fs::path&, const fs::path&, const std::string&, ScriptType)> onCreate,
              std::function<void()> onCancel = nullptr);

    void show();
    bool isOpen() const { return m_isOpen; }
    void close() { m_isOpen = false; }

    // ImGui CallbackCharFilter that keeps a class/base name field typeable only
    // as valid C++ identifier characters (invalid chars -> '_'). Shared so other
    // class-name inputs (e.g. Properties' "Edit Script Details") behave the same.
    static int classNameCharFilter(ImGuiInputTextCallbackData* data);
};

} // namespace editor
} // namespace doriax
