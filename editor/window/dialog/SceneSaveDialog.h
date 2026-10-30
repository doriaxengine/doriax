// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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
    void cancel();

    bool m_isOpen = false;
    fs::path m_projectPath;
    fs::path m_selectedPath;
    char m_fileNameBuffer[256] = "";

    std::function<void(const fs::path&)> m_onSave;
    std::function<void()> m_onCancel;
    std::function<std::string(const fs::path&)> m_fileOwner;

public:
    SceneSaveDialog() = default;
    ~SceneSaveDialog() = default;

    // fileOwner returns the name of the scene already saved at a path
    void open(const fs::path& projectPath, const fs::path& initialDirectory, const std::string& defaultName,
        std::function<void(const fs::path&)> onSave,
        std::function<void()> onCancel = nullptr,
        std::function<std::string(const fs::path&)> fileOwner = nullptr);
    void show();
    bool isOpen() const { return m_isOpen; }
    void close(); // same as Cancel
};

} // namespace editor
} // namespace doriax