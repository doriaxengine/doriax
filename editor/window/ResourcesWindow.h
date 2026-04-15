#ifndef RESOURCESWINDOW_H
#define RESOURCESWINDOW_H

#include "Project.h"
#include "window/CodeEditor.h"
#include "util/EntityPayload.h"
#include "render/preview/ModelRender.h"

#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <unordered_map>
#include <condition_variable>

#include "imgui.h"

namespace fs = std::filesystem;

namespace doriax::editor {

    enum class FileType {
        NONE,
        IMAGE,
        MATERIAL,
        SCENE,
        BUNDLE,
        MODEL
    };

    struct FileEntry {
        std::string name;
        std::string extension;
        FileType type = FileType::NONE;
        bool isDirectory;
        intptr_t icon;
        bool hasThumbnail;
        std::string thumbnailPath;
    };

    struct ThumbnailRequest {
        fs::path path;
        FileType type = FileType::NONE;
    };

    class ResourcesWindow {
    private:
        Project* project;
        CodeEditor* codeEditor;

        enum class LayoutType {
            AUTO,  // Automatically switch between GRID and SPLIT based on window size
            GRID,  // Current single-column list view
            SPLIT   // New two-column tree view
        };
        LayoutType currentLayout = LayoutType::AUTO;

        enum class ItemViewStyle {
            CARD,
            CLASSIC
        };
        ItemViewStyle itemViewStyle = ItemViewStyle::CLASSIC;

        float layoutAutoThreshold = 600.0f;
        float leftPanelWidth = 200.0f;  // Width of the left panel in SPLIT layout

        bool firstOpen;
        bool requestSort;
        fs::path currentPath;
        std::vector<editor::FileEntry> files;

        Texture folderIcon;
        Texture fileIcon;
        Texture sceneIcon;
        Texture entityIcon;
        std::unordered_map<std::string, Texture> thumbnailTextures;

        int iconSize;
        float iconPadding;

        std::string lastSelectedFile;
        std::unordered_set<std::string> selectedFiles;
        bool ctrlPressed;
        bool shiftPressed;

        bool isDragging;
        ImVec2 dragStart;
        ImVec2 dragEnd;
        ImVec2 windowPos;
        ImVec2 scrollOffset;

        bool isDragDropTarget;
        bool isExternalDragHovering;

        std::vector<std::string> clipboardFiles;
        bool clipboardCut;

        char nameBuffer[256];

        bool isRenaming;
        std::string fileBeingRenamed;

        bool isCreatingNewDirectory;

        std::filesystem::file_time_type lastWriteTime;
        float timeSinceLastCheck;

        bool windowFocused;

        bool showDeleteConfirmation;

        MaterialRender materialRender;
        ModelRender modelRender;

        // Thumbnail generation
        std::thread thumbnailThread;
        std::mutex thumbnailMutex;
        std::queue<ThumbnailRequest> thumbnailQueue;
        std::atomic<bool> stopThumbnailThread;
        std::condition_variable thumbnailCondition;

        // Queue for completed thumbnails
        std::mutex completedThumbnailMutex;
        std::queue<ThumbnailRequest> completedThumbnailQueue;

        // Add these to the private section of the ResourcesWindow class
        fs::path pendingMaterialPath;
        bool hasPendingMaterialRender = false;
        std::mutex materialRenderMutex;

        fs::path pendingModelPath;
        bool hasPendingModelRender = false;
        std::mutex modelRenderMutex;

        ImU32 fileSeparatorColor(const FileEntry& fe) const;

        void renderHeader();
        void renderFileListing(bool showDirectories);
        void renderDirectoryTree(const fs::path& path);

        void scanDirectory(const fs::path& path);
        void sortWithSortSpecs(ImGuiTableSortSpecs* sortSpecs, std::vector<FileEntry>& files);
        void highlightDragAndDrop();
        void handleInternalDragAndDrop(const fs::path& targetDirectory);
        void handleNewDirectory();
        void handleRename();
        void copySelectedFiles(bool cut);
        void pasteFiles(const fs::path& targetDirectory);

        void queueThumbnailGeneration(const fs::path& filePath, FileType type, bool forceRegenerate = false);
        void thumbnailWorker();
        bool loadThumbnail(FileEntry& entry);

        void saveMaterialFile(const fs::path& directory, const char* materialContent, size_t contentLen, const MaterialPayload* sourceMaterial = nullptr);
        void saveEntityFile(const fs::path& directory, const char* entityContent, size_t contentLen);
        void saveBundleFile(const fs::path& directory, const char* bundleContent, size_t contentLen);

        fs::path uniqueRelativePath(const fs::path& directory, const std::string& baseName, const std::string& extension);

    public:
        static constexpr const char* WINDOW_NAME = "Resources";

        ResourcesWindow(Project* project, CodeEditor* codeEditor);
        ~ResourcesWindow();

        bool isFocused() const;

        void notifyProjectPathChange();

        void handleExternalDragEnter();
        void handleExternalDragLeave();

        // need to be after draw
        void processMaterialThumbnails();
        void processModelThumbnails();

        void notifyResourceFileChanged(const fs::path& filePath);

        void cleanupThumbnails();

        void refreshCurrentDirectory();

        void show();
    };

}

#endif /* RESOURCESWINDOW_H */