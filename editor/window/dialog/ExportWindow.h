#pragma once

#include "Exporter.h"
#include "imgui.h"

#include <string>
#include <filesystem>
#include <functional>
#include <set>
#include <vector>

namespace doriax::editor {

    namespace fs = std::filesystem;

    class ExportWindow {
    private:
        bool m_isOpen = false;
        bool m_exporting = false;
        Project* m_project = nullptr;

        // UI state
        char m_targetDirBuffer[512] = "";
        char m_assetsDirBuffer[512] = "";
        char m_luaDirBuffer[512] = "";
        fs::path m_targetDir;
        fs::path m_assetsDir;
        fs::path m_luaDir;

        // Start scene
        int m_startSceneIndex = 0;

        // Shader list: each entry is a shader to export
        struct ShaderEntry {
            ShaderKey key;
            ShaderType type;
            uint32_t properties;
            std::string displayName;
        };
        std::vector<ShaderEntry> m_shaderEntries;
        int m_selectedShaderIndex = -1;

        // Add Shader dialog state
        bool m_addShaderOpen = false;
        int m_addShaderTypeIndex = 0;
        bool m_addShaderProps[32] = {};

        // Platform selection
        struct PlatformEntry {
            Platform platform;
            std::string name;
            bool selected;
        };
        std::vector<PlatformEntry> m_platformEntries;

        Exporter m_exporter;

        void populateShaderList();
        void populatePlatformList();
        void drawSettings();
        void drawProgress();
        void drawAddShaderDialog();

    public:
        ExportWindow() = default;
        ~ExportWindow() = default;

        void open(Project* project);
        void show();
        bool isOpen() const { return m_isOpen; }
    };

}
