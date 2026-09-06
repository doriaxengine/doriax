// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

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
#include <cstdint>

#include "imgui.h"

namespace fs = std::filesystem;

namespace doriax::editor {

    class ImageViewerWindow;

    enum class FileType {
        NONE,
        IMAGE,
        MATERIAL,
        SCENE,
        BUNDLE,
        MODEL,
        SOURCE,
        HEADER,
        LUA,
        AUDIO,
        FONT
    };

    struct FileEntry {
        std::string name;
        // name and name-without-extension reshaped for display, built with the entry
        // because ImGui cannot lay out right-to-left text and this list has no clipper
        std::string displayName;
        std::string displayBaseName;
        std::string extension;
        FileType type = FileType::NONE;
        bool isDirectory;
        TextureRender* icon = nullptr;
        bool hasThumbnail;
        std::string thumbnailPath;
    };

    struct ThumbnailRequest {
        fs::path path;
        FileType type = FileType::NONE;
        uint64_t generation = 0;
    };

    struct ThumbnailTexture {
        Texture texture;
        int lastUsedFrame = 0;
        bool failed = false;
    };

    class ResourcesWindow {
    private:
        Project* project;
        CodeEditor* codeEditor;
        ImageViewerWindow* imageViewerWindow;

        enum class LayoutType {
            AUTO,              // Automatically switch between GRID and SPLIT_FILES_ONLY based on window size
            GRID,              // Single panel, directories shown in the file listing
            SPLIT_FILES_ONLY,  // Directory tree sidebar, listing shows files only
            SPLIT              // Directory tree sidebar, directories included in the listing
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
        fs::path treeSelectedPath;
        std::vector<editor::FileEntry> files;

        Texture folderIcon;
        Texture fileIcon;
        Texture sceneIcon;
        Texture entityIcon;
        Texture sourceIcon;
        Texture headerIcon;
        Texture luaIcon;
        Texture audioIcon;
        Texture fontIcon;
        // Bounded cache of GPU thumbnail textures; must stay well below the
        // sokol image/sampler pool sizes (2048), which are shared with scenes
        static constexpr size_t MAX_THUMBNAIL_TEXTURES = 512;
        std::unordered_map<std::string, ThumbnailTexture> thumbnailTextures;
        std::unordered_set<std::string> pendingThumbnailRequests;

        int iconSize;

        std::string lastSelectedFile;
        std::unordered_set<std::string> selectedFiles;
        std::unordered_set<std::string> projectFiles;  // project-relative scene and built bundle paths
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
        bool renameSelectPending;
        std::string fileBeingRenamed;

        bool isCreatingNewDirectory;

        std::filesystem::file_time_type lastWriteTime;
        float timeSinceLastCheck;

        // Children of each folder-tree node: renderDirectoryTree() runs every frame,
        // and enumerating there was the tree's whole cost. Re-read only when the
        // folder's own write time changes, on the same 1 Hz check as the listing.
        struct DirTreeEntry {
            std::vector<fs::path> subDirs;
            std::filesystem::file_time_type writeTime{};
            double lastCheckTime = 0.0;
            uint64_t generation = 0;
        };
        std::unordered_map<std::string, DirTreeEntry> dirTreeCache;
        // Invalidated by bumping this rather than clearing the map: scanDirectory()
        // is reachable from inside the tree walk, which holds a reference into it.
        // Starts at 1 so a default-constructed entry is already stale.
        uint64_t dirTreeGeneration = 1;

        bool windowOpen;
        bool focusRequested;
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
        bool thumbnailWorkerBusy = false; // guarded by thumbnailMutex
        bool thumbnailWorkSuspended = false; // guarded by thumbnailMutex
        uint64_t thumbnailGeneration = 0; // guarded by thumbnailMutex
        size_t activeThumbnailSaves = 0; // guarded by thumbnailMutex

        // Queue for completed thumbnails
        std::mutex completedThumbnailMutex;
        std::queue<ThumbnailRequest> completedThumbnailQueue;

        // Add these to the private section of the ResourcesWindow class
        fs::path pendingMaterialPath;
        bool hasPendingMaterialRender = false; // guarded by thumbnailMutex
        uint64_t pendingMaterialGeneration = 0; // guarded by thumbnailMutex
        std::mutex materialRenderMutex;

        fs::path pendingModelPath;
        bool hasPendingModelRender = false; // guarded by thumbnailMutex
        uint64_t pendingModelGeneration = 0; // guarded by thumbnailMutex
        std::mutex modelRenderMutex;

        ImU32 fileSeparatorColor(const FileEntry& fe) const;
        void refreshProjectFiles();
        bool fileNotInProject(const FileEntry& fe) const;

        void renderHeader();
        void renderPathBreadcrumb(const ImVec2& size);
        void renderFileListing(bool showDirectories);
        void renderDirectoryTree(const fs::path& path);
        const std::vector<fs::path>& treeSubdirectories(const fs::path& path);

        void scanDirectory(const fs::path& path);
        void sortWithSortSpecs(ImGuiTableSortSpecs* sortSpecs, std::vector<FileEntry>& files);
        void highlightDragAndDrop();
        void handleInternalDragAndDrop(const fs::path& targetDirectory);
        void handleNewDirectory();
        void handleRename();
        void copySelectedFiles(bool cut);
        void pasteFiles(const fs::path& targetDirectory);
        void importExternalPaths(const std::vector<std::string>& sourcePaths);
        static FileType classifyThumbnailFileType(const fs::path& filePath);
        static std::string thumbnailRequestKey(const fs::path& filePath);
        static unsigned char textureByteAt(TextureData& data, size_t pixel, int channel, int bytesPerChannel);
        static bool writeImageThumbnail(const fs::path& sourcePath, const fs::path& thumbnailPath);

        void queueThumbnailGeneration(const fs::path& filePath, FileType type, bool forceRegenerate = false);
        void thumbnailWorker();
        ImTextureID getThumbnailTexture(const std::string& thumbnailPath, int& outWidth, int& outHeight);
        void evictThumbnailTexture(const std::string& thumbnailKey);
        void enforceThumbnailCacheLimit();
        void clearThumbnailTextures();

        void saveMaterialFile(const fs::path& directory, const char* materialContent, size_t contentLen, const MaterialPayload* sourceMaterial = nullptr);

        fs::path uniqueRelativePath(const fs::path& directory, const std::string& baseName, const std::string& extension);

    public:
        static constexpr const char* WINDOW_NAME = "Resources";

        ResourcesWindow(Project* project, CodeEditor* codeEditor, ImageViewerWindow* imageViewerWindow);
        ~ResourcesWindow();

        bool isFocused() const;

        void notifyProjectPathChange();
        void cancelThumbnailWork();
        void resumeThumbnailWork();

        void handleExternalDragEnter();
        void handleExternalDragLeave();

        // need to be after draw
        void processMaterialThumbnails();
        void processModelThumbnails();

        // True while any thumbnail is still generating; the render loop must keep
        // drawing while set (thumbnail scenes share systemDraw).
        bool hasPendingThumbnailWork();

        void notifyResourceFileChanged(const fs::path& filePath);
        void requestThumbnailGeneration(const fs::path& filePath, bool forceRegenerate = false);
        ImTextureID getAssetThumbnail(const fs::path& filePath, int& outWidth, int& outHeight);

        void cleanupThumbnails();

        void refreshCurrentDirectory();
        fs::path getCurrentPath() const { return currentPath; }
        void saveEntityFile(const fs::path& directory, const char* entityContent, size_t contentLen);

        void show();
        void setOpen(bool open);
        bool isOpen() const;
    };

}

