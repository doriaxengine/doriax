// ProjectSaveDialog.h
#pragma once

#include <string>
#include <filesystem>
#include <functional>
#include "imgui.h"

namespace doriax {
namespace editor {

    namespace fs = std::filesystem;

    class ProjectSaveDialog {
    private:
        bool m_isOpen = false;
        std::string m_projectName;
        fs::path m_projectPath;
        char m_projectNameBuffer[256] = "";

        std::function<void(const std::string&, const fs::path&)> m_onSave;
        std::function<void()> m_onCancel;

    public:
        ProjectSaveDialog() = default;
        ~ProjectSaveDialog() = default;

        void open(const std::string& defaultName, 
            std::function<void(const std::string&, const fs::path&)> onSave,
            std::function<void()> onCancel = nullptr);
        void show();
        bool isOpen() const { return m_isOpen; }
        void close() { m_isOpen = false; }
    };

} // namespace editor
} // namespace doriax