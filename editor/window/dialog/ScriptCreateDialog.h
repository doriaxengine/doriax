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
    bool m_isOpen = false;
    fs::path m_projectPath;
    std::string m_selectedPath;
    char m_baseNameBuffer[128] = "";
    ScriptType m_scriptType = ScriptType::SUBCLASS;
    Scene* m_scene = nullptr;
    Entity m_entity = NULL_ENTITY;

    std::function<void(const fs::path&, const fs::path&, const std::string&, ScriptType)> m_onCreate;
    std::function<void()> m_onCancel;

    void displayDirectoryTree(const fs::path& rootPath, const fs::path& currentPath);
    std::string sanitizeClassName(const std::string& in) const;

    fs::path makeHeaderPath(const std::string& className) const;
    fs::path makeSourcePath(const std::string& className) const;
    fs::path makeLuaPath(const std::string& moduleName) const;

    void writeFiles(const fs::path& headerPath,
                    const fs::path& sourcePath,
                    const std::string& classOrModuleName,
                    ScriptType type);

    void finalizeCreation(const fs::path& headerPath,
                          const fs::path& sourcePath,
                          const std::string& name);

public:
    ScriptCreateDialog() = default;
    ~ScriptCreateDialog() = default;

    void open(Scene* scene,
              Entity entity,
              const fs::path& projectPath,
              const std::string& defaultBaseName,
              std::function<void(const fs::path&, const fs::path&, const std::string&, ScriptType)> onCreate,
              std::function<void()> onCancel = nullptr);

    void show();
    bool isOpen() const { return m_isOpen; }
    void close() { m_isOpen = false; }
};

} // namespace editor
} // namespace doriax
