// SceneSaveDialog.h
#pragma once

#include <string>
#include <filesystem>
#include <functional>
#include "imgui.h"

namespace doriax {
namespace editor {

namespace fs = std::filesystem;

class SceneSaveDialog {
private:
    void displayDirectoryTree(const fs::path& rootPath, const fs::path& currentPath);

    bool m_isOpen = false;
    fs::path m_projectPath;
    std::string m_selectedPath;
    char m_fileNameBuffer[256] = "";

    std::function<void(const fs::path&)> m_onSave;
    std::function<void()> m_onCancel;

public:
    SceneSaveDialog() = default;
    ~SceneSaveDialog() = default;

    void open(const fs::path& projectPath, const std::string& defaultName, 
        std::function<void(const fs::path&)> onSave,
        std::function<void()> onCancel = nullptr);
    void show();
    bool isOpen() const { return m_isOpen; }
    void close() { m_isOpen = false; }
};

} // namespace editor
} // namespace doriax