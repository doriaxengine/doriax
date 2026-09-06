// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "ResourcesWindow.h"
#include "util/BidiText.h"

#include "ImageViewerWindow.h"

#include "AppSettings.h"
#include "external/IconsFontAwesome6.h"
#include "resources/icons/folder-icon_png.h"
#include "resources/icons/file-icon_png.h"
#include "resources/icons/entity-icon_png.h"
#include "resources/icons/scene-icon_png.h"
#include "resources/icons/source-icon_png.h"
#include "resources/icons/header-icon_png.h"
#include "resources/icons/lua-icon_png.h"
#include "resources/icons/audio-icon_png.h"
#include "resources/icons/font-icon_png.h"

#include "command/type/CopyFileCmd.h"
#include "command/type/CreateMaterialFileCmd.h"
#include "command/type/RenameFileCmd.h"
#include "command/type/CreateDirCmd.h"
#include "command/type/DeleteFileCmd.h"
#include "command/type/CreateEntityBundleCmd.h"

#include "Backend.h"
#include "App.h"
#include "Theme.h"
#include "Widgets.h"
#include "Stream.h"
#include "Log.h"
#include "subsystem/MeshSystem.h"
#include "util/FileDialogs.h"
#include "util/SHA1.h"
#include "util/GraphicUtils.h"
#include "util/EntityPayload.h"

#include "imgui_internal.h"

#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <algorithm>
#include "stb_image_write.h"
#include "stb_image_resize2.h"

using namespace doriax;

unsigned char editor::ResourcesWindow::textureByteAt(TextureData& data, size_t pixel, int channel, int bytesPerChannel) {
    if (bytesPerChannel == 2) {
        const unsigned short* src = static_cast<const unsigned short*>(data.getData());
        return static_cast<unsigned char>(src[pixel * data.getChannels() + channel] >> 8);
    }

    const unsigned char* src = static_cast<const unsigned char*>(data.getData());
    return src[pixel * data.getChannels() + channel];
}

bool editor::ResourcesWindow::writeImageThumbnail(const fs::path& sourcePath, const fs::path& thumbnailPath) {
    TextureData sourceData;
    if (!sourceData.loadTextureFromFile(sourcePath.string().c_str()) || !sourceData.getData()) {
        return false;
    }

    const int width = sourceData.getWidth();
    const int height = sourceData.getHeight();
    const int channels = sourceData.getChannels();
    const int bytesPerChannel = TextureData::getBytesPerChannel(sourceData.getColorFormat());
    if (width <= 0 || height <= 0 || channels <= 0) {
        sourceData.releaseImageData();
        return false;
    }

    std::vector<unsigned char> rgbaData(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const size_t srcPixel = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const size_t dstPixel = srcPixel * 4;

            if (channels == 1) {
                const unsigned char value = textureByteAt(sourceData, srcPixel, 0, bytesPerChannel);
                rgbaData[dstPixel + 0] = value;
                rgbaData[dstPixel + 1] = value;
                rgbaData[dstPixel + 2] = value;
                rgbaData[dstPixel + 3] = 255;
            } else if (channels == 2) {
                const unsigned char value = textureByteAt(sourceData, srcPixel, 0, bytesPerChannel);
                rgbaData[dstPixel + 0] = value;
                rgbaData[dstPixel + 1] = value;
                rgbaData[dstPixel + 2] = value;
                rgbaData[dstPixel + 3] = textureByteAt(sourceData, srcPixel, 1, bytesPerChannel);
            } else {
                rgbaData[dstPixel + 0] = textureByteAt(sourceData, srcPixel, 0, bytesPerChannel);
                rgbaData[dstPixel + 1] = textureByteAt(sourceData, srcPixel, 1, bytesPerChannel);
                rgbaData[dstPixel + 2] = textureByteAt(sourceData, srcPixel, 2, bytesPerChannel);
                rgbaData[dstPixel + 3] = channels >= 4 ? textureByteAt(sourceData, srcPixel, 3, bytesPerChannel) : 255;
            }
        }
    }

    int maxThumbSize = THUMBNAIL_SIZE;
    float scale = (width > height) ?
        static_cast<float>(maxThumbSize) / static_cast<float>(width) :
        static_cast<float>(maxThumbSize) / static_cast<float>(height);

    int newWidth = std::max(1, static_cast<int>(width * scale));
    int newHeight = std::max(1, static_cast<int>(height * scale));

    std::vector<unsigned char> thumbData(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight) * 4);
    stbir_resize_uint8_linear(
        rgbaData.data(),
        width,
        height,
        0,
        thumbData.data(),
        newWidth,
        newHeight,
        0,
        STBIR_RGBA
    );

    fs::create_directories(thumbnailPath.parent_path());
    const bool written = stbi_write_png(thumbnailPath.string().c_str(), newWidth, newHeight, 4, thumbData.data(), newWidth * 4) != 0;

    sourceData.releaseImageData();
    return written;
}

editor::FileType editor::ResourcesWindow::classifyThumbnailFileType(const fs::path& filePath) {
    const std::string extension = filePath.extension().string();

    if (editor::Util::isImageFile(extension)) {
        return editor::FileType::IMAGE;
    }
    if (editor::Util::isMaterialFile(extension)) {
        return editor::FileType::MATERIAL;
    }
    if (editor::Util::isModelFile(extension)) {
        return editor::FileType::MODEL;
    }

    return editor::FileType::NONE;
}

std::string editor::ResourcesWindow::thumbnailRequestKey(const fs::path& filePath) {
    return filePath.lexically_normal().string();
}

editor::ResourcesWindow::ResourcesWindow(Project* project, CodeEditor* codeEditor, ImageViewerWindow* imageViewerWindow) {
    this->project = project;
    this->codeEditor = codeEditor;
    this->imageViewerWindow = imageViewerWindow;
    this->firstOpen = true;
    this->requestSort = true;
    this->iconSize = 32;
    this->isExternalDragHovering = false;
    this->clipboardCut = false;
    this->isRenaming = false;
    this->renameSelectPending = false;
    this->isCreatingNewDirectory = false;
    this->timeSinceLastCheck = 0.0f;
    this->windowOpen = true;
    this->focusRequested = false;
    this->windowFocused = false;
    this->showDeleteConfirmation = false;
    this->stopThumbnailThread = false;
    memset(this->nameBuffer, 0, sizeof(this->nameBuffer));

    thumbnailThread = std::thread(&ResourcesWindow::thumbnailWorker, this);
}

editor::ResourcesWindow::~ResourcesWindow() {
    cancelThumbnailWork();
    stopThumbnailThread = true;
    thumbnailCondition.notify_one(); // Wake the worker thread to check stop condition
    if (thumbnailThread.joinable()) {
        thumbnailThread.join();
    }
}

bool editor::ResourcesWindow::isFocused() const{
    return windowFocused;
}

void editor::ResourcesWindow::setOpen(bool open){
    if (open){
        if (!windowOpen){
            focusRequested = true;
        }
        windowOpen = true;
        return;
    }

    windowOpen = false;
    focusRequested = false;
    windowFocused = false;
}

bool editor::ResourcesWindow::isOpen() const{
    return windowOpen;
}

void editor::ResourcesWindow::notifyProjectPathChange(){
    // This is also used by project moves, which may not go through the normal
    // project-open path. Quiesce the old path before scanning the new one.
    cancelThumbnailWork();

    // Clear thumbnail textures when changing projects
    clearThumbnailTextures();
    dirTreeCache.clear();

    resumeThumbnailWork();
    scanDirectory(project->getProjectPath());
}

void editor::ResourcesWindow::cancelThumbnailWork(){
    {
        std::unique_lock<std::mutex> lock(thumbnailMutex);
        thumbnailWorkSuspended = true;
        std::queue<ThumbnailRequest> empty;
        std::swap(thumbnailQueue, empty);
        pendingThumbnailRequests.clear();

        // The worker publishes pending preview state before clearing busy, so
        // after this wait all of its state can be invalidated atomically.
        thumbnailCondition.wait(lock, [this]() {
            return !thumbnailWorkerBusy && activeThumbnailSaves == 0;
        });

        ++thumbnailGeneration;
        hasPendingMaterialRender = false;
        pendingMaterialPath.clear();
        pendingMaterialGeneration = 0;
        hasPendingModelRender = false;
        pendingModelPath.clear();
        pendingModelGeneration = 0;
    }

    // removeScene() also removes one-shot bookkeeping, so the preview objects
    // can now release old-project resources without leaving dangling Scene*s.
    Engine::removeScene(materialRender.getScene());
    {
        std::lock_guard<std::mutex> lock(materialRenderMutex);
        materialRender.applyMaterial(Material());
    }

    Scene* modelScene = modelRender.getScene();
    if (modelScene) {
        Engine::removeScene(modelScene);
    }
    {
        std::lock_guard<std::mutex> lock(modelRenderMutex);
        modelRender.clearScene();
    }

    std::lock_guard<std::mutex> completedLock(completedThumbnailMutex);
    std::queue<ThumbnailRequest> empty;
    std::swap(completedThumbnailQueue, empty);
}

void editor::ResourcesWindow::resumeThumbnailWork(){
    {
        std::lock_guard<std::mutex> lock(thumbnailMutex);
        thumbnailWorkSuspended = false;
    }
    thumbnailCondition.notify_all();
}

void editor::ResourcesWindow::handleExternalDragEnter() {
    isExternalDragHovering = true;
}

void editor::ResourcesWindow::handleExternalDragLeave() {
    isExternalDragHovering = false;
}

void editor::ResourcesWindow::notifyResourceFileChanged(const fs::path& filePath) {
    std::error_code ec;
    if (!fs::exists(filePath, ec) || ec || fs::is_directory(filePath, ec) || ec) {
        return;
    }

    FileType type = classifyThumbnailFileType(filePath);

    if (type == FileType::NONE) {
        return;
    }

    fs::path thumbnailPath = project->getThumbnailPath(filePath);
    evictThumbnailTexture(thumbnailPath.string());

    for (auto& file : files) {
        if ((currentPath / file.name) == filePath) {
            file.hasThumbnail = false;
            file.thumbnailPath.clear();
            break;
        }
    }

    queueThumbnailGeneration(filePath, type, true);
}

void editor::ResourcesWindow::requestThumbnailGeneration(const fs::path& filePath, bool forceRegenerate) {
    FileType type = classifyThumbnailFileType(filePath);
    if (type == FileType::NONE) {
        return;
    }

    if (forceRegenerate) {
        evictThumbnailTexture(project->getThumbnailPath(filePath).string());
    }

    queueThumbnailGeneration(filePath, type, forceRegenerate);
}

bool editor::ResourcesWindow::hasPendingThumbnailWork() {
    std::lock_guard<std::mutex> lock(thumbnailMutex);
    // Any pending thumbnail work needs the shared systemDraw pass to keep running.
    return thumbnailWorkerBusy || hasPendingModelRender || hasPendingMaterialRender ||
           !thumbnailQueue.empty();
}

void editor::ResourcesWindow::processMaterialThumbnails() {
    ThumbnailRequest pending;
    {
        std::lock_guard<std::mutex> lock(thumbnailMutex);
        if (!hasPendingMaterialRender) {
            return;
        }
        pending = {pendingMaterialPath, FileType::MATERIAL, pendingMaterialGeneration};
    }

    if (!Engine::isSceneRunning(materialRender.getScene())) {
        std::lock_guard<std::mutex> lock(materialRenderMutex);

        fs::path thumbnailPath = project->getThumbnailPath(pending.path);
        fs::create_directories(thumbnailPath.parent_path());

        Framebuffer* framebuffer = materialRender.getFramebuffer();
        if (!framebuffer) {
            Log::warn("Thumbnail render produced no framebuffer for material: %s", pending.path.string().c_str());
            {
                std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
                if (pending.generation == thumbnailGeneration) {
                    pendingThumbnailRequests.erase(thumbnailRequestKey(pending.path));
                    hasPendingMaterialRender = false;
                    pendingMaterialPath.clear();
                    pendingMaterialGeneration = 0;
                }
            }
            thumbnailCondition.notify_one();
            return;
        }

        {
            std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
            ++activeThumbnailSaves;
        }
        const bool saveStarted = GraphicUtils::saveFramebufferImage(framebuffer, thumbnailPath, false,
            [this, pending]() {
                bool publish = false;
                {
                    std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
                    if (pending.generation == thumbnailGeneration) {
                        pendingThumbnailRequests.erase(thumbnailRequestKey(pending.path));
                        publish = true;
                    }
                }
                if (publish) {
                    std::lock_guard<std::mutex> completedLock(completedThumbnailMutex);
                    completedThumbnailQueue.push(pending);
                }
                {
                    std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
                    --activeThumbnailSaves;
                    // Once the count hits zero cancelThumbnailWork() may return and
                    // the object may be destroyed, so notify while still holding the
                    // lock and touch no members after this block.
                    thumbnailCondition.notify_all();
                }
            });
        if (!saveStarted) {
            std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
            --activeThumbnailSaves;
            pendingThumbnailRequests.erase(thumbnailRequestKey(pending.path));
            thumbnailCondition.notify_all();
        }

        {
            std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
            if (pending.generation == thumbnailGeneration &&
                pendingMaterialGeneration == pending.generation) {
                hasPendingMaterialRender = false;
                pendingMaterialPath.clear();
                pendingMaterialGeneration = 0;
            }
        }

        thumbnailCondition.notify_one();
    }
}

void editor::ResourcesWindow::processModelThumbnails() {
    ThumbnailRequest pending;
    {
        std::lock_guard<std::mutex> lock(thumbnailMutex);
        if (!hasPendingModelRender) {
            return;
        }
        pending = {pendingModelPath, FileType::MODEL, pendingModelGeneration};
    }

    if (!Engine::isSceneRunning(modelRender.getScene())) {
        std::lock_guard<std::mutex> lock(modelRenderMutex);

        fs::path thumbnailPath = project->getThumbnailPath(pending.path);
        fs::create_directories(thumbnailPath.parent_path());

        Framebuffer* framebuffer = modelRender.getFramebuffer();
        if (!framebuffer) {
            Log::warn("Thumbnail render produced no framebuffer for model: %s", pending.path.string().c_str());
            // Release the scene while hasPendingModelRender still parks the worker;
            // clearing the flags first would let a wakeup start a new load into the
            // scene being torn down here.
            Engine::removeScene(modelRender.getScene());
            modelRender.clearScene();
            {
                std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
                if (pending.generation == thumbnailGeneration) {
                    pendingThumbnailRequests.erase(thumbnailRequestKey(pending.path));
                    hasPendingModelRender = false;
                    pendingModelPath.clear();
                    pendingModelGeneration = 0;
                }
            }
            thumbnailCondition.notify_one();
            return;
        }

        {
            std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
            ++activeThumbnailSaves;
        }
        const bool saveStarted = GraphicUtils::saveFramebufferImage(framebuffer, thumbnailPath, false,
            [this, pending, thumbnailPath]() {
                std::error_code ec;
                if (!fs::exists(thumbnailPath, ec) || ec) {
                    Log::warn("Failed to save model thumbnail '%s': %s",
                              thumbnailPath.string().c_str(), ec ? ec.message().c_str() : "output file is missing");
                }
                bool publish = false;
                {
                    std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
                    if (pending.generation == thumbnailGeneration) {
                        pendingThumbnailRequests.erase(thumbnailRequestKey(pending.path));
                        publish = true;
                    }
                }
                if (publish) {
                    std::lock_guard<std::mutex> completedLock(completedThumbnailMutex);
                    completedThumbnailQueue.push(pending);
                }
                {
                    std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
                    --activeThumbnailSaves;
                    // Once the count hits zero cancelThumbnailWork() may return and
                    // the object may be destroyed, so notify while still holding the
                    // lock and touch no members after this block.
                    thumbnailCondition.notify_all();
                }
            });
        if (!saveStarted) {
            std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
            --activeThumbnailSaves;
            pendingThumbnailRequests.erase(thumbnailRequestKey(pending.path));
            thumbnailCondition.notify_all();
        }

        // Pixel readback above is synchronous; the detached PNG writer owns its
        // copy. Release this large preview scene now so its GPU buffers don't
        // overlap with the model being loaded into the user's scene.
        Engine::removeScene(modelRender.getScene());
        modelRender.clearScene();

        {
            std::lock_guard<std::mutex> thumbnailLock(thumbnailMutex);
            if (pending.generation == thumbnailGeneration &&
                pendingModelGeneration == pending.generation) {
                hasPendingModelRender = false;
                pendingModelPath.clear();
                pendingModelGeneration = 0;
            }
        }

        thumbnailCondition.notify_one();
    }
}

void editor::ResourcesWindow::refreshProjectFiles(){
    projectFiles.clear();

    for (const SceneProject& sceneProject : project->getScenes()){
        projectFiles.insert(sceneProject.filepath.generic_string());
        for (const BundleSceneInfo& info : sceneProject.bundles){
            projectFiles.insert(info.bundlePath.generic_string());
        }
    }

    for (const fs::path& bundlePath : project->getStandaloneBundles()){
        projectFiles.insert(bundlePath.generic_string());
    }
}

bool editor::ResourcesWindow::fileNotInProject(const FileEntry& fe) const{
    if (fe.type != FileType::SCENE && fe.type != FileType::BUNDLE)
        return false;

    const std::string relativePath = (currentPath / fe.name).lexically_relative(project->getProjectPath()).generic_string();

    return !projectFiles.count(relativePath);
}

ImU32 editor::ResourcesWindow::fileSeparatorColor(const FileEntry& fe) const{
    if (fe.isDirectory)
        return ImGui::GetColorU32(ImVec4(0.60f, 0.60f, 0.60f, 1.0f));

    switch (fe.type) {
        case FileType::IMAGE:
            return ImGui::GetColorU32(ImVec4(0.25f, 0.55f, 1.00f, 1.0f));
        case FileType::MATERIAL:
            return ImGui::GetColorU32(ImVec4(0.20f, 0.80f, 0.70f, 1.0f));
        case FileType::SCENE:
            return ImGui::GetColorU32(ImVec4(0.90f, 0.70f, 0.20f, 1.0f));
        case FileType::BUNDLE:
            return ImGui::GetColorU32(ImVec4(0.30f, 0.85f, 0.30f, 1.0f));
        case FileType::MODEL:
            return ImGui::GetColorU32(ImVec4(0.80f, 0.40f, 0.40f, 1.0f));
        case FileType::SOURCE:
            return ImGui::GetColorU32(ImVec4(0.17f, 0.42f, 0.75f, 1.0f));
        case FileType::HEADER:
            return ImGui::GetColorU32(ImVec4(0.49f, 0.25f, 0.75f, 1.0f));
        case FileType::LUA:
            return ImGui::GetColorU32(ImVec4(0.17f, 0.18f, 0.45f, 1.0f));
        case FileType::AUDIO:
            return ImGui::GetColorU32(ImVec4(0.12f, 0.62f, 0.39f, 1.0f));
        case FileType::FONT:
            return ImGui::GetColorU32(ImVec4(0.84f, 0.27f, 0.31f, 1.0f));
        case FileType::NONE:
        default:
            return ImGui::GetColorU32(ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
    }
}

void editor::ResourcesWindow::renderHeader() {
    ImGui::BeginDisabled(currentPath == project->getProjectPath());
    if (ImGui::Button(ICON_FA_HOUSE)) {
        scanDirectory(project->getProjectPath().string());
        selectedFiles.clear();
    }
    if (currentPath != project->getProjectPath() && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
            handleInternalDragAndDrop(project->getProjectPath());
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ANGLE_LEFT)) {
        if (!currentPath.empty() && currentPath != project->getProjectPath()) {
            fs::path parentPath = currentPath.parent_path();
            currentPath = parentPath;
            scanDirectory(currentPath);
            selectedFiles.clear();
        }
    }
    if (currentPath != project->getProjectPath() && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
            handleInternalDragAndDrop(currentPath.parent_path());
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImVec2 pathDisplaySize = ImVec2(-ImGui::CalcTextSize(ICON_FA_GEAR).x - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x * 2, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2);
    renderPathBreadcrumb(pathDisplaySize);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_GEAR)) {
        ImGui::OpenPopup("SettingsPopup");
    }

    ImGui::SetNextWindowSize(ImVec2(Theme::dpi(300.0f), 0.0f));
    if (ImGui::BeginPopup("SettingsPopup")) {
        ImGui::Text("Settings");
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Theme::dpi(2.0f), ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Button(ICON_FA_ROTATE_LEFT "##ResetSettings")) {
            iconSize = 32;
            currentLayout = LayoutType::AUTO;
            itemViewStyle = ItemViewStyle::CLASSIC;
            leftPanelWidth = 200.0f;
            AppSettings::setResourcesIconSize(iconSize);
            AppSettings::setResourcesLayout(static_cast<int>(currentLayout));
            AppSettings::setResourcesItemViewStyle(static_cast<int>(itemViewStyle));
            AppSettings::setResourcesLeftPanelWidth(leftPanelWidth);
            AppSettings::saveSettings();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset to defaults");
        }
        ImGui::Separator();

        // Same label | input layout as SceneWindow's scene settings popup, but the
        // value column stretches to fill the remaining popup width
        if (ImGui::BeginTable("resources_settings_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, Theme::dpi(100.0f));
            // Explicit stretch: tables inside auto-resizing popups default to
            // SizingFixedFit, which would shrink this column to its content
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Icon size");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##IconSize", &iconSize, 16.0f, THUMBNAIL_SIZE)) {
                AppSettings::setResourcesIconSize(iconSize);
                AppSettings::saveSettings();
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Layout");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            const char* layoutNames[] = { "Auto", "Grid", "Split (files only)", "Split" };
            int layoutIndex = static_cast<int>(currentLayout);
            if (ImGui::Combo("##Layout", &layoutIndex, layoutNames, IM_ARRAYSIZE(layoutNames))) {
                currentLayout = static_cast<LayoutType>(layoutIndex);
                AppSettings::setResourcesLayout(static_cast<int>(currentLayout));
                AppSettings::saveSettings();
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Item view");
            ImGui::TableSetColumnIndex(1);
            ItemViewStyle prevStyle = itemViewStyle;
            if (ImGui::RadioButton("Classic", itemViewStyle == ItemViewStyle::CLASSIC)) itemViewStyle = ItemViewStyle::CLASSIC;
            ImGui::SameLine();
            if (ImGui::RadioButton("Card", itemViewStyle == ItemViewStyle::CARD)) itemViewStyle = ItemViewStyle::CARD;
            if (itemViewStyle != prevStyle) {
                AppSettings::setResourcesItemViewStyle(static_cast<int>(itemViewStyle));
                AppSettings::saveSettings();
            }

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
}

// ImGui hides label text after "##", so breadcrumb items (directory names that
// may contain "##") draw their text manually over a label-less widget; callers
// wrap these in PushID for uniqueness
static bool verbatimTextButton(const std::string& label) {
    const char* begin = label.c_str();
    const char* end = begin + label.size();
    ImVec2 textSize = ImGui::CalcTextSize(begin, end);
    ImVec2 padding = ImGui::GetStyle().FramePadding;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::Button("##BreadcrumbItem", ImVec2(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f));
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y), ImGui::GetColorU32(ImGuiCol_Text), begin, end);
    return pressed;
}

static bool verbatimTextSelectable(const std::string& label) {
    const char* begin = label.c_str();
    const char* end = begin + label.size();
    ImVec2 textSize = ImGui::CalcTextSize(begin, end);
    bool pressed = ImGui::Selectable("##BreadcrumbHiddenItem", false, 0, ImVec2(textSize.x, 0.0f));
    ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_Text), begin, end);
    return pressed;
}

void editor::ResourcesWindow::renderPathBreadcrumb(const ImVec2& size) {
    fs::path rootPath = project->getProjectPath();

    // Segments between the project root and the current directory, each paired
    // with its absolute path so it can be clicked and used as a drop target
    std::vector<std::pair<std::string, fs::path>> segments;
    if (!currentPath.empty() && currentPath != rootPath && currentPath.string().find(rootPath.string()) == 0) {
        fs::path accumulated = rootPath;
        for (const auto& part : currentPath.lexically_relative(rootPath)) {
            if (part == "." || part.empty()) continue;
            accumulated /= part;
            segments.emplace_back(part.string(), accumulated);
        }
    }

    // Navigation and drops are deferred to after the child so scanDirectory and
    // handleInternalDragAndDrop don't mutate state mid-layout
    fs::path navigateTo;
    fs::path dropTarget;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(50, 50, 50, 255));
    ImGui::BeginChild("##ResourcesPath", size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float framePaddingY = ImGui::GetStyle().FramePadding.y;
    float separatorWidth = ImGui::CalcTextSize("/").x;
    auto segmentWidth = [](const std::string& name) {
        return ImGui::CalcTextSize(name.c_str()).x;
    };

    // Fit as many trailing segments as possible; earlier ones collapse into "..."
    size_t firstVisible = segments.size();
    float usedWidth = 0.0f;
    float availableWidth = ImGui::GetContentRegionAvail().x;
    for (size_t i = segments.size(); i-- > 0;) {
        float width = separatorWidth + segmentWidth(segments[i].first);
        float reserved = (i > 0) ? (separatorWidth + segmentWidth("...")) : 0.0f;
        if (firstVisible < segments.size() && (usedWidth + width + reserved) > availableWidth) {
            break;
        }
        usedWidth += width;
        firstVisible = i;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::SetCursorPosY(framePaddingY);

    if (segments.empty()) {
        ImGui::TextUnformatted("/");
    }

    if (firstVisible > 0) {
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        if (ImGui::Button("...##BreadcrumbMore")) {
            ImGui::OpenPopup("##BreadcrumbMorePopup");
        }
        ImGui::SameLine();
    }

    bool segmentTooltipShown = false;
    for (size_t i = firstVisible; i < segments.size(); i++) {
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        bool isCurrent = (segments[i].second == currentPath);
        if (verbatimTextButton(segments[i].first) && !isCurrent) {
            navigateTo = segments[i].second;
        }
        if (!isCurrent && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
                dropTarget = segments[i].second;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", segments[i].second.string().c_str());
            segmentTooltipShown = true;
        }
        ImGui::PopID();
        ImGui::SameLine();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (ImGui::BeginPopup("##BreadcrumbMorePopup")) {
        for (size_t i = 0; i < firstVisible; i++) {
            ImGui::PushID(static_cast<int>(i));
            if (verbatimTextSelectable(segments[i].first)) {
                navigateTo = segments[i].second;
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    if (!segmentTooltipShown && !currentPath.empty()) {
        ImGui::SetItemTooltip("%s", currentPath.string().c_str());
    }
    ImGui::PopStyleColor();

    if (!navigateTo.empty()) {
        scanDirectory(navigateTo);
        selectedFiles.clear();
    }
    if (!dropTarget.empty()) {
        handleInternalDragAndDrop(dropTarget);
    }
}

void editor::ResourcesWindow::renderFileListing(bool showDirectories){
    // Stored iconSize stays logical; layout/draw use the current window DPI.
    const float uiIconSize = static_cast<float>(iconSize) * Theme::dpiScale();
    const float uiIconPadding = 1.5f * uiIconSize;

    // --- Common grid sizing -------------------------------------------------
    float columnWidth = uiIconSize + uiIconPadding;
    float availableWidth = ImGui::GetContentRegionAvail().x;
    int columns = static_cast<int>(availableWidth / columnWidth);
    if (columns < 1) columns = 1;

    const bool useCardView = (itemViewStyle == ItemViewStyle::CARD);

    // Card view gets a bit more vertical padding than classic.
    ImVec2 cellPadding = Theme::dpi(useCardView ? ImVec2(6.0f, 8.0f) : ImVec2(8.0f, 8.0f));
    float  totalTableWidth = availableWidth;

    ImVec2 scrollRegionMin = ImGui::GetWindowPos();
    ImVec2 scrollRegionMax = ImVec2(
        scrollRegionMin.x + ImGui::GetWindowSize().x,
        scrollRegionMin.y + ImGui::GetWindowSize().y
    );

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);

    bool clickedInFile = false;

    // --- Start marquee selection on empty click -----------------------------
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()){
        isDragging = true;
        dragStart = ImGui::GetMousePos();
        dragStart.x -= scrollRegionMin.x;
        dragStart.y -= scrollRegionMin.y;
        dragStart.x += ImGui::GetScrollX();
        dragStart.y += ImGui::GetScrollY();
        selectedFiles.clear();
    }

    // --- Update marquee rectangle and auto-scroll ---------------------------
    if (isDragging){
        ImVec2 mousePos = ImGui::GetMousePos();
        float scrollMargin = Theme::dpi(20.0f);
        float currentScroll = ImGui::GetScrollY();

        if (mousePos.y > scrollRegionMax.y - scrollMargin){
            float scrollDelta = (mousePos.y - (scrollRegionMax.y - scrollMargin)) * 0.5f;
            ImGui::SetScrollY(currentScroll + scrollDelta);
        }else if (mousePos.y < scrollRegionMin.y + scrollMargin){
            float scrollDelta = (mousePos.y - (scrollRegionMin.y + scrollMargin)) * 0.5f;
            ImGui::SetScrollY(currentScroll + scrollDelta);
        }

        dragEnd = mousePos;
        dragEnd.x -= scrollRegionMin.x;
        dragEnd.y -= scrollRegionMin.y;
        dragEnd.x += ImGui::GetScrollX();
        dragEnd.y += ImGui::GetScrollY();

        if (!ImGui::IsMouseDown(0)){
            isDragging = false;
        }
    }

    // --- Card metrics (constants — only used in Card view) ------------------
    const float pad = Theme::dpi(8.0f);                     // Inner padding
    const float thumbHeight = uiIconSize + Theme::dpi(4.0f);
    const float lineThickness = Theme::dpi(2.0f);
    const int maxTextLines = 2;                         // At most 2 text lines
    const float fontScale = 0.80f;                     // Scale for card text
    const float lineHeight = ImGui::GetTextLineHeight();
    const float textAreaHeight = maxTextLines * (lineHeight * fontScale); // Tight fit for scaled text
    const float cardHeight = pad + thumbHeight
                             + pad * 0.5f + lineThickness
                             + pad * 0.5f + textAreaHeight
                             + pad;
    const float contentYOffset = pad * 0.75f; // Extra vertical offset so thumbnail, line, and text start a bit lower

    if (ImGui::BeginTable("FileTable", columns, ImGuiTableFlags_SizingStretchSame, ImVec2(totalTableWidth, 0))){
        for (auto& file : files){
            if (!showDirectories && file.isDirectory) continue;

            bool deferredDirectoryChange = false;
            fs::path dropTarget;

            ImGui::TableNextColumn();
            ImGui::PushID(file.name.c_str());

            float cellWidth = ImGui::GetContentRegionAvail().x;

            // --- Classic-only metrics --------------------------------------
            float itemSpacingY = 0.0f;
            ImVec2 textSizeClassic(0, 0);
            ImVec2 selectableSizeClassic(0, 0);

            // --- Choose item size per style --------------------------------
            ImVec2 itemSize;
            if (useCardView){
                itemSize = ImVec2(cellWidth, cardHeight);
            }else{
                itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
                textSizeClassic = ImGui::CalcTextSize(file.displayName.c_str(), nullptr, true, cellWidth);
                float cellHeight = uiIconSize + itemSpacingY + textSizeClassic.y;
                selectableSizeClassic = ImVec2(cellWidth, cellHeight);
                itemSize = selectableSizeClassic;
            }

            // --- Marquee overlap before item creation ----------------------
            if (isDragging){
                ImVec2 itemPos = ImGui::GetCursorScreenPos();
                itemPos.x -= scrollRegionMin.x;
                itemPos.y -= scrollRegionMin.y;
                itemPos.x += ImGui::GetScrollX();
                itemPos.y += ImGui::GetScrollY();

                ImRect itemRect(itemPos, ImVec2(itemPos.x + itemSize.x, itemPos.y + itemSize.y));

                ImRect selectionRect(
                    ImVec2(std::min(dragStart.x, dragEnd.x), std::min(dragStart.y, dragEnd.y)),
                    ImVec2(std::max(dragStart.x, dragEnd.x), std::max(dragStart.y, dragEnd.y))
                );

                bool isOverlapping = itemRect.Overlaps(selectionRect);

                if (!ctrlPressed){
                    if (isOverlapping)
                        selectedFiles.insert(file.name);
                    else
                        selectedFiles.erase(file.name);
                }else{
                    if (isOverlapping)
                        selectedFiles.insert(file.name);
                }
            }

            bool isSelected = selectedFiles.find(file.name) != selectedFiles.end();

            // --- Create the hit area ---------------------------------------
            bool itemPressed = false;
            bool hovered = false;

            if (useCardView){
                itemPressed = ImGui::InvisibleButton("##card", itemSize);
                hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            }else{
                ImGui::BeginGroup(); // classic uses a group
                itemPressed = ImGui::Selectable("",
                                                isSelected,
                                                ImGuiSelectableFlags_AllowDoubleClick,
                                                itemSize);
                hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            }

            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();

            const bool doubleClickedItem = hovered && ImGui::IsMouseDoubleClicked(0);

            // --- Double-click: open directory or file ----------------------
            if (doubleClickedItem){
                clickedInFile = true;

                selectedFiles.clear();
                selectedFiles.insert(file.name);
                lastSelectedFile = file.name;

                if (file.isDirectory){
                    deferredDirectoryChange = true;
                }else{
                    const fs::path filePath = currentPath / file.name;
                    if (file.type == FileType::IMAGE)
                        imageViewerWindow->openFile(filePath);
                    else if (file.type == FileType::SCENE)
                        project->openScene(filePath);
                    else
                        codeEditor->openFile(filePath.string(), true);
                }
            }

            // --- Resolve icon/thumbnail common path ------------------------
            ImTextureID fileIconImage = Backend::getImGuiTexture(file.icon);
            float dispW = uiIconSize;
            float dispH = uiIconSize;

            // Only resolve thumbnails for items inside the visible region, so
            // the GPU texture cache stays bounded with large directories
            // (clipped items never draw the image anyway)
            if (file.hasThumbnail && ImGui::IsItemVisible()){
                int tw = 0;
                int th = 0;
                if (ImTextureID thumbTex = getThumbnailTexture(file.thumbnailPath, tw, th)){
                    if (tw > 0 && th > 0){
                        float scale = std::min(uiIconSize / static_cast<float>(tw),
                                               uiIconSize / static_cast<float>(th));
                        dispW = std::max(1.0f, tw * scale);
                        dispH = std::max(1.0f, th * scale);
                    }
                    fileIconImage = thumbTex;
                }
            }

            // --- Drag source (unified) -------------------------------------
            bool itemActive   = ImGui::IsItemActive();
            float dragThreshold = ImGui::GetIO().MouseDragThreshold;
            bool wantDrag = itemActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, dragThreshold);

            if (wantDrag){
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){
                    // Ensure selection contains this file if dragging an unselected item
                    if (selectedFiles.find(file.name) == selectedFiles.end()){
                        selectedFiles.clear();
                        selectedFiles.insert(file.name);
                        lastSelectedFile = file.name;
                    }

                    // Payload: list of paths separated by '\0'
                    std::vector<char> buffer;
                    buffer.reserve(256);

                    for (const auto& selectedFile : selectedFiles){
                        std::string fullPath = (currentPath / selectedFile).string();
                        buffer.insert(buffer.end(), fullPath.begin(), fullPath.end());
                        buffer.push_back('\0');
                    }

                    ImGui::SetDragDropPayload("resource_files", buffer.data(), buffer.size());

                    ImGui::Text("Moving %zu file(s)", selectedFiles.size());

                    if (selectedFiles.size() == 1){
                        float imageDragSize = Theme::dpi(32.0f);
                        float scale = std::min(imageDragSize / dispW, imageDragSize / dispH);
                        float previewW = dispW * scale;
                        float previewH = dispH * scale;
                        float availWidth = ImGui::GetCurrentWindow()->Size.x;
                        float xPos = (availWidth - previewW) * 0.5f;
                        ImGui::SetCursorPosX(xPos);
                        Widgets::image(fileIconImage, ImVec2(previewW, previewH));
                    }

                    ImGui::EndDragDropSource();
                }
            }

            // --- Directory as drop target ----------------------------------
            if (file.isDirectory){
                if (ImGui::BeginDragDropTarget()){
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")){
                        dropTarget = currentPath / file.name;
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            // =================================================================
            // Draw content per style
            // =================================================================
            const bool notInProject = fileNotInProject(file);

            if (useCardView){
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImGuiStyle& style = ImGui::GetStyle();

                ImVec4 header = style.Colors[ImGuiCol_Header];
                ImVec4 headerHovered = style.Colors[ImGuiCol_HeaderHovered];
                ImVec4 headerActive = style.Colors[ImGuiCol_HeaderActive];
                ImVec4 borderV = style.Colors[ImGuiCol_Border];
                ImVec4 bgV = Theme::Colors::FileCardBackground;

                if (hovered) bgV = Theme::Colors::FileCardBackgroundHovered;
                if (isSelected) bgV = header;

                ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(bgV);

                ImVec4 bColV = borderV;
                if (hovered) bColV = headerHovered;
                if (isSelected) bColV = headerActive;
                ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(bColV);

                float rounding = style.FrameRounding;

                // Outer card
                drawList->AddRectFilled(itemMin, itemMax, bgCol, rounding);
                drawList->AddRect(itemMin, itemMax, borderCol, rounding, 0, Theme::dpi(1.0f));

                // Slight inner shadow on top for depth
                ImU32 shadowCol = IM_COL32(0, 0, 0, 40);
                ImVec2 shadowMin(itemMin.x, itemMin.y + Theme::dpi(1.0f));
                ImVec2 shadowMax(itemMax.x, itemMax.y);
                drawList->AddRect(shadowMin, shadowMax, shadowCol, rounding, 0, Theme::dpi(1.0f));

                // Content rect inside padding
                ImVec2 contentMin(itemMin.x + pad, itemMin.y + pad);
                float  contentWidth = itemMax.x - itemMin.x - (pad * 2.0f);

                // --- Name / extension split ---------------------------------
                std::string extensionStr = file.extension;

                // Remove leading '.' for badge text
                std::string extLabel;
                if (!file.isDirectory && !extensionStr.empty()){
                    if (extensionStr[0] == '.')
                        extLabel = extensionStr.substr(1);
                    else
                        extLabel = extensionStr;
                }

                // --- Thumbnail centered -------------------------------------
                float imageX = contentMin.x + (contentWidth - dispW) * 0.5f;
                float imageY = contentMin.y + contentYOffset + (thumbHeight - dispH) * 0.5f;
                ImGui::SetCursorScreenPos(ImVec2(imageX, imageY));
                if (notInProject) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * 0.4f);
                Widgets::image(fileIconImage, ImVec2(dispW, dispH));

                // --- Extension badge over thumbnail -------------------------
                if (!extLabel.empty()){
                    ImU32 badgeBgCol = fileSeparatorColor(file);
                    ImVec4 badgeBgV = ImGui::ColorConvertU32ToFloat4(badgeBgCol);

                    float luminance = badgeBgV.x * 0.2126f +
                                    badgeBgV.y * 0.7152f +
                                    badgeBgV.z * 0.0722f;

                    ImVec4 txtColV;
                    if (luminance > 0.6f)
                        txtColV = ImVec4(0.10f, 0.10f, 0.10f, 0.98f);   // dark text on bright bg
                    else
                        txtColV = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);   // light text on dark bg

                    ImU32 badgeTextCol = ImGui::ColorConvertFloat4ToU32(txtColV);

                    // Make the extension text smaller (use ImGui::GetFontSize, not font->FontSize)
                    ImFont* font = ImGui::GetFont();
                    float baseSize = ImGui::GetFontSize();
                    float fontScale = 0.80f;                         // 80% of normal
                    float smallSize = baseSize * fontScale;

                    ImVec2 extSize = ImGui::CalcTextSize(extLabel.c_str());
                    extSize.x *= fontScale;
                    extSize.y *= fontScale;

                    float badgePadX = Theme::dpi(4.0f);
                    float badgePadY = Theme::dpi(1.0f);

                    ImVec2 badgeMax(
                        contentMin.x + contentWidth - Theme::dpi(2.0f),
                        contentMin.y + 2.0f + extSize.y + badgePadY * 2.0f
                    );
                    ImVec2 badgeMin(
                        badgeMax.x - extSize.x - badgePadX * 2.0f,
                        badgeMax.y - extSize.y - badgePadY * 2.0f
                    );

                    ImDrawList* drawList   = ImGui::GetWindowDrawList();
                    float badgeRound = ImGui::GetStyle().FrameRounding * 0.5f;

                    drawList->AddRectFilled(badgeMin, badgeMax, badgeBgCol, badgeRound);

                    ImVec2 textPos(badgeMin.x + badgePadX, badgeMin.y + badgePadY);
                    drawList->AddText(font, smallSize, textPos, badgeTextCol, extLabel.c_str());
                }

                // --- Separator line -----------------------------------------
                ImU32 sepCol = fileSeparatorColor(file);
                float lineY  = contentMin.y + contentYOffset + thumbHeight + pad * 0.5f;
                drawList->AddLine(
                    ImVec2(contentMin.x, lineY),
                    ImVec2(contentMin.x + contentWidth, lineY),
                    sepCol,
                    lineThickness
                );

                // --- Name text, clipped to two lines ------------------------
                float textY = lineY + lineThickness + pad * 0.4f;
                ImVec2 textMin(contentMin.x, textY);
                ImVec2 textMax(contentMin.x + contentWidth, textY + textAreaHeight);
                ImGui::PushClipRect(textMin, textMax, true);
                ImGui::SetCursorScreenPos(textMin);
                ImGui::PushTextWrapPos(textMax.x - ImGui::GetWindowPos().x);
                ImGui::SetWindowFontScale(fontScale);
                ImGui::TextUnformatted(file.displayBaseName.c_str());
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopTextWrapPos();
                ImGui::PopClipRect();
                if (notInProject) ImGui::PopStyleVar();

                if (hovered){
                    ImGui::SetTooltip("%s", file.displayName.c_str());
                }

            }else{
                // ------------------------------------------------------------
                // Classic layout: icon + wrapped text centered
                // ------------------------------------------------------------
                float iconOffsetX = (cellWidth - uiIconSize) * 0.5f;
                float iconOffsetY = itemSize.y + itemSpacingY;

                // Move back up into the selectable rect
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconOffsetX);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - iconOffsetY);

                // Center the scaled thumbnail inside the icon box
                float offsetX = (uiIconSize - dispW) * 0.5f;
                float offsetY = (uiIconSize - dispH) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
                if (notInProject) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
                Widgets::image(fileIconImage, ImVec2(dispW, dispH));

                float textOffsetX = (cellWidth * 0.5f) - (textSizeClassic.x * 0.5f);
                if (textOffsetX < 0) textOffsetX = 0;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffsetX);
                ImGui::TextWrapped("%s", file.displayName.c_str());
                if (notInProject) ImGui::PopStyleVar();

                ImGui::EndGroup(); // classic group
            }

            // --- Selection behavior (unified) ------------------------------
            if (itemPressed && !doubleClickedItem){
                clickedInFile = true;

                if (ctrlPressed){
                    if (isSelected)
                        selectedFiles.erase(file.name);
                    else
                        selectedFiles.insert(file.name);
                    lastSelectedFile = file.name;
                }else if (shiftPressed){
                    if (!lastSelectedFile.empty()){
                        auto itStart = std::find_if(files.begin(), files.end(),
                            [&](const FileEntry& entry) { return entry.name == lastSelectedFile; });
                        auto itEnd = std::find_if(files.begin(), files.end(),
                            [&](const FileEntry& entry) { return entry.name == file.name; });

                        if (itStart != files.end() && itEnd != files.end()){
                            if (itStart > itEnd) std::swap(itStart, itEnd);
                            for (auto it = itStart; it <= itEnd; ++it){
                                if (showDirectories || !it->isDirectory)
                                    selectedFiles.insert(it->name);
                            }
                        }
                    }else{
                        selectedFiles.insert(file.name);
                    }
                }else{
                    selectedFiles.clear();
                    selectedFiles.insert(file.name);
                    lastSelectedFile = file.name;
                }
            }

            // --- Right-click context menu on the item rect -----------------
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(itemMin, itemMax, true)){
                clickedInFile = true;
                if (!ctrlPressed && !isSelected){
                    selectedFiles.clear();
                }
                selectedFiles.insert(file.name);
                lastSelectedFile = file.name;
                ImGui::OpenPopup("FileContextMenu");
            }

            if (ImGui::BeginPopup("FileContextMenu")){
                if (file.type == FileType::SCENE){
                    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS " Open (Add)")) {
                        project->openScene(currentPath / file.name, false);
                    }
                    ImGui::Separator();
                }

                if (ImGui::MenuItem(ICON_FA_COPY " Copy")) copySelectedFiles(false);
                if (ImGui::MenuItem(ICON_FA_SCISSORS " Cut")) copySelectedFiles(true);
                if (ImGui::MenuItem(ICON_FA_PASTE " Paste", nullptr, false, !clipboardFiles.empty())){
                    pasteFiles(currentPath / lastSelectedFile);
                }

                ImGui::Separator();

                if (ImGui::MenuItem(ICON_FA_TRASH " Delete")) showDeleteConfirmation = true;
                if (ImGui::MenuItem(ICON_FA_I_CURSOR " Rename")){
                    isRenaming = true;
                    renameSelectPending = true;
                    fileBeingRenamed = file.name;
                    strncpy(nameBuffer, file.name.c_str(), sizeof(nameBuffer) - 1);
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();

            if (deferredDirectoryChange){
                scanDirectory(currentPath / file.name);
                selectedFiles.clear();
                break;
            }

            // Rebuilds files, so the entry this iteration holds is gone from here on
            if (!dropTarget.empty()){
                handleInternalDragAndDrop(dropTarget);
                break;
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();

    // --- Right-click on empty space ----------------------------------------
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsWindowHovered() && !clickedInFile){
        ImGui::OpenPopup("ResourcesContextMenu");
    }

    if (ImGui::BeginPopup("ResourcesContextMenu")){
        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Import Files")){
            importExternalPaths(editor::FileDialogs::openFileDialogMultiple());
        }

        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Import Folders")){
            importExternalPaths(editor::FileDialogs::pickFolderDialogMultiple());
        }

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_FA_FOLDER " New Folder")){
            isCreatingNewDirectory = true;
            memset(nameBuffer, 0, sizeof(nameBuffer));
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem(ICON_FA_PASTE " Paste", nullptr, false, !clipboardFiles.empty())){
            pasteFiles(currentPath);
        }

        ImGui::EndPopup();
    }

    // --- Clear selection when clicking empty space -------------------------
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !clickedInFile){
        selectedFiles.clear();
    }

    // --- Draw marquee rectangle --------------------------------------------
    if (isDragging){
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec2 rectMin(
            scrollRegionMin.x + std::min(dragStart.x, dragEnd.x) - ImGui::GetScrollX(),
            scrollRegionMin.y + std::min(dragStart.y, dragEnd.y) - ImGui::GetScrollY()
        );
        ImVec2 rectMax(
            scrollRegionMin.x + std::max(dragStart.x, dragEnd.x) - ImGui::GetScrollX(),
            scrollRegionMin.y + std::max(dragStart.y, dragEnd.y) - ImGui::GetScrollY()
        );

        rectMin.x = ImClamp(rectMin.x, scrollRegionMin.x, scrollRegionMax.x);
        rectMin.y = ImClamp(rectMin.y, scrollRegionMin.y, scrollRegionMax.y);
        rectMax.x = ImClamp(rectMax.x, scrollRegionMin.x, scrollRegionMax.x);
        rectMax.y = ImClamp(rectMax.y, scrollRegionMin.y, scrollRegionMax.y);

        drawList->AddRect(rectMin, rectMax, IM_COL32(100, 150, 255, 255));
        drawList->AddRectFilled(rectMin, rectMax, IM_COL32(100, 150, 255, 50));
    }

    // --- Delete confirmation modal (unchanged) -----------------------------
    if (showDeleteConfirmation){
        ImGui::OpenPopup("Delete Confirmation");
    }

    if (ImGui::BeginPopupModal("Delete Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Are you sure you want to delete the following items?");
        ImGui::Separator();
        int fileCount = 0;
        for (const auto& fileName : selectedFiles){
            if (fileCount < 10){
                ImGui::BulletText("%s", fileName.c_str());
            }
            fileCount++;
        }
        if (fileCount > 10){
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "And %d more items...", fileCount - 10);
        }

        ImGui::Separator();
        const float buttonWidth = 120.0f;
        const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float totalWidth = (buttonWidth * 2) + buttonSpacing;
        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - totalWidth) / 2.0f);

        if (ImGui::Button("Yes", ImVec2(buttonWidth, 0))){
            std::vector<fs::path> pathsToDelete;
            for (const auto& fileName : selectedFiles){
                fs::path filePath = currentPath / fileName;
                pathsToDelete.push_back(filePath);
                codeEditor->closeFile(filePath.string());
                imageViewerWindow->closeFile(filePath);
            }

            project->getProjectCommandHistory()->addCommand(new DeleteFileCmd(project, pathsToDelete, project->getProjectPath()));
            selectedFiles.clear();
            scanDirectory(currentPath);
            showDeleteConfirmation = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("No", ImVec2(buttonWidth, 0))){
            showDeleteConfirmation = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// Subdirectories the tree can render under path, hidden ones already filtered out.
const std::vector<fs::path>& editor::ResourcesWindow::treeSubdirectories(const fs::path& path) {
    const double now = ImGui::GetTime();
    DirTreeEntry& cached = dirTreeCache[path.string()];

    // An invalidation has to take effect at once, so it skips the interval
    const bool invalidated = cached.generation != dirTreeGeneration;
    if (!invalidated && (now - cached.lastCheckTime) < 1.0) {
        return cached.subDirs;
    }
    cached.lastCheckTime = now;

    // Unreadable this tick: keep what we have, the next one picks up the truth
    std::error_code ec;
    const auto writeTime = fs::last_write_time(path, ec);
    if (!invalidated && (ec || writeTime == cached.writeTime)) {
        return cached.subDirs;
    }
    // Marked read even if the listing below fails, so a folder that cannot be opened
    // is retried on the next check instead of on every frame
    cached.generation = dirTreeGeneration;

    std::vector<fs::path> subDirs;
    ec.clear();
    fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    while (!ec && it != end) {
        const auto entry = *it;
        it.increment(ec);

        // Skip hidden directories (starting with '.')
        if (entry.path().filename().string()[0] == '.') {
            continue;
        }

        std::error_code entryEc;
        if (entry.is_directory(entryEc) && !entryEc) {
            subDirs.push_back(entry.path());
        }
    }

    // A failed or truncated listing keeps the previous children, and leaves writeTime
    // stale so the next check retries instead of the node staying empty for good
    if (!ec) {
        cached.writeTime = writeTime;
        cached.subDirs = std::move(subDirs);
    }

    return cached.subDirs;
}

void editor::ResourcesWindow::renderDirectoryTree(const fs::path& rootPath) {
    std::string rootFullPath = rootPath.string();
    std::string rootDisplayName = rootPath.filename().string();
    if (rootDisplayName.empty()) rootDisplayName = rootPath.string(); // Handle root paths like C:/ or /

    bool isRootSelected = (treeSelectedPath == rootPath);
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (isRootSelected) rootFlags |= ImGuiTreeNodeFlags_Selected;

    // Check if this directory is in the path to current directory
    bool isInCurrentPath = false;
    if (currentPath.string().find(rootPath.string()) == 0 && rootPath != currentPath) {
        // This directory is a parent of the current directory
        isInCurrentPath = true;
    }

    // For root directory of the project or directories in path to current directory, expand initially
    if (rootPath == project->getProjectPath() || isInCurrentPath) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }

    // Auto-expand the parent of the current directory
    if (rootPath == currentPath.parent_path()) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    // Use unique ID to prevent ImGui tree node confusion
    ImGui::PushID(rootFullPath.c_str());

    // Get the unique ID for this tree node
    ImGuiID node_id = ImGui::GetID("##node");

    // Query the current open state from ImGui's state storage
    bool is_open = ImGui::GetStateStorage()->GetInt(node_id, 0) != 0;

    // Select the appropriate icon: open folder if node is open OR selected, closed folder otherwise
    const char* icon = (is_open || isRootSelected) ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;

    // Same list drives the leaf test and the recursion, so a folder holding only
    // dot-directories no longer gets an expander that opens onto nothing
    const std::vector<fs::path>& subDirs = treeSubdirectories(rootPath);

    // If no subdirectories, mark as leaf node (no arrow)
    if (subDirs.empty()) {
        rootFlags |= ImGuiTreeNodeFlags_Leaf;
    }

    // Render the current directory node with the selected icon
    bool nodeOpen = ImGui::TreeNodeEx("##node", rootFlags, "%s %s", icon, rootDisplayName.c_str());

    // Single click only selects the node; double click navigates to it
    if (ImGui::IsItemClicked()) {
        treeSelectedPath = rootPath;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        scanDirectory(rootPath);
    }

    // Handle drag and drop target
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
            handleInternalDragAndDrop(rootPath);
        }
        ImGui::EndDragDropTarget();
    }

    if (nodeOpen) {
        // Recursing inserts into dirTreeCache, which leaves subDirs valid: rehashing
        // an unordered_map does not invalidate references to elements already in it
        for (const fs::path& subDir : subDirs) {
            renderDirectoryTree(subDir);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void editor::ResourcesWindow::scanDirectory(const fs::path& path) {
    currentPath = path;

    std::error_code ec;
    if (!std::filesystem::is_directory(currentPath, ec) || ec) {
        currentPath = project->getProjectPath();
    }

    // Keep the directory tree highlight in sync with any navigation
    treeSelectedPath = currentPath;

    // Every create, rename, move and delete ends here, so the tree cache only has to
    // be invalidated in this one place
    dirTreeGeneration++;

    refreshProjectFiles();

    requestSort = true;

    // Update last write time
    ec.clear();
    lastWriteTime = fs::last_write_time(currentPath, ec);
    if (ec) {
        lastWriteTime = {};
    }

    TextureRender* folderIconH = folderIcon.getRender();
    TextureRender* fileIconH = fileIcon.getRender();
    TextureRender* sceneIconH = sceneIcon.getRender();
    TextureRender* entityIconH = entityIcon.getRender();
    TextureRender* sourceIconH = sourceIcon.getRender();
    TextureRender* headerIconH = headerIcon.getRender();
    TextureRender* luaIconH = luaIcon.getRender();
    TextureRender* audioIconH = audioIcon.getRender();
    TextureRender* fontIconH = fontIcon.getRender();

    files.clear();

    ec.clear();
    fs::directory_iterator it(currentPath, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    while (!ec && it != end) {
        const auto entry = *it;
        it.increment(ec);

        // Skip hidden files and directories (starting with '.')
        if (entry.path().filename().string()[0] == '.') {
            continue;
        }

        // Skip project.yaml file
        if (entry.path().filename() == "project.yaml") {
            continue;
        }

        FileEntry fileEntry;
        fileEntry.name = entry.path().filename().string();
        std::error_code entryEc;
        fileEntry.isDirectory = entry.is_directory(entryEc) && !entryEc;

        fileEntry.displayName = BidiText::toVisual(fileEntry.name);
        std::string baseName = fileEntry.name;
        if (!fileEntry.isDirectory){
            size_t dotPos = baseName.rfind('.');
            if (dotPos != std::string::npos)
                baseName = baseName.substr(0, dotPos);
        }
        fileEntry.displayBaseName = BidiText::toVisual(baseName);
        fileEntry.icon = fileEntry.isDirectory ? folderIconH : fileIconH;
        fileEntry.hasThumbnail = false;

        if (!fileEntry.isDirectory) {
            fileEntry.extension = entry.path().extension().string();
            if (Util::isImageFile(fileEntry.extension)){
                fileEntry.type = FileType::IMAGE;
            }else if (Util::isSceneFile(fileEntry.extension)){
                fileEntry.type = FileType::SCENE;
                fileEntry.icon = sceneIconH;
            }else if (Util::isMaterialFile(fileEntry.extension)){
                fileEntry.type = FileType::MATERIAL;
            }else if (Util::isBundleFile(fileEntry.extension)){
                fileEntry.type = FileType::BUNDLE;
                fileEntry.icon = entityIconH;
            }else if (Util::isModelFile(fileEntry.extension)){
                fileEntry.type = FileType::MODEL;
            }else if (Util::isLuaFile(fileEntry.extension)){
                fileEntry.type = FileType::LUA;
                fileEntry.icon = luaIconH;
            }else if (Util::isHeaderFile(fileEntry.extension)){
                fileEntry.type = FileType::HEADER;
                fileEntry.icon = headerIconH;
            }else if (Util::isSourceFile(fileEntry.extension)){
                fileEntry.type = FileType::SOURCE;
                fileEntry.icon = sourceIconH;
            }else if (Util::isAudioFile(fileEntry.extension)){
                fileEntry.type = FileType::AUDIO;
                fileEntry.icon = audioIconH;
            }else if (Util::isFontFile(fileEntry.extension)){
                fileEntry.type = FileType::FONT;
                fileEntry.icon = fontIconH;
            }

            if (fileEntry.type == FileType::IMAGE || fileEntry.type == FileType::MATERIAL || fileEntry.type == FileType::MODEL) {
                queueThumbnailGeneration(entry.path(), fileEntry.type);
            }
        } else {
            fileEntry.extension = "";
        }

        files.push_back(fileEntry);
    }
}

void editor::ResourcesWindow::sortWithSortSpecs(ImGuiTableSortSpecs* sortSpecs, std::vector<FileEntry>& files) {
    if (!sortSpecs || sortSpecs->SpecsCount == 0) {
        // Default behavior: Sort directories first, then by name
        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.isDirectory != b.isDirectory) {
                return a.isDirectory; // Directories come first
            }
            return a.name < b.name;
        });
        return;
    }

    auto comparator = [&](const FileEntry& a, const FileEntry& b) -> bool {
        // Always sort directories first
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory; // Directories come first
        }

        for (int i = 0; i < sortSpecs->SpecsCount; i++) {
            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[i];
            bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

            switch (spec.ColumnIndex) {
                case 0: // Column 0: "Name"
                    if (a.name != b.name) {
                        return ascending ? (a.name < b.name) : (a.name > b.name);
                    }
                    break;

                case 1: // Column 1: "Extension"
                    if (a.extension != b.extension) {
                        return ascending ? (a.extension < b.extension) : (a.extension > b.extension);
                    }
                    break;

                default:
                    return false;
            }
        }

        return false;
    };

    std::sort(files.begin(), files.end(), comparator);
}

void editor::ResourcesWindow::highlightDragAndDrop(){
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 maxPos = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);

    ImGui::GetWindowDrawList()->AddRect(
        windowPos,
        maxPos,
        ImGui::GetColorU32(ImGuiCol_DragDropTarget),
        0.0f, 0, 2.0f);
}

void editor::ResourcesWindow::handleInternalDragAndDrop(const fs::path& targetDirectory) {
    std::vector<std::string> filesVector(selectedFiles.begin(), selectedFiles.end());
    project->getProjectCommandHistory()->addCommand(new CopyFileCmd(project, filesVector, currentPath.string(), targetDirectory.string(), false));

    selectedFiles.clear();
    scanDirectory(currentPath);
}

void editor::ResourcesWindow::handleNewDirectory(){
    // Handle new directory creation popup
    if (isCreatingNewDirectory) {
        ImGui::OpenPopup("Create New Directory");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    if (ImGui::BeginPopupModal("Create New Directory", &isCreatingNewDirectory, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter directory name:");

        // Auto-focus the input field when the popup opens
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        bool enterPressed = ImGui::InputText("##newdir", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

        // Calculate total width of the buttons
        float buttonWidth = 120.0f;
        float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = (buttonWidth * 2) + buttonSpacing;

        // Center the buttons
        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - totalWidth) * 0.5f);

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            isCreatingNewDirectory = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool confirmed = ImGui::Button("Create", ImVec2(buttonWidth, 0)) || enterPressed;
        if (confirmed) {
            std::string dirName = nameBuffer;
            if (!dirName.empty()) {
                try {
                    fs::path newDirPath = currentPath / dirName;

                    // Check if directory already exists
                    if (fs::exists(newDirPath)) {
                        ImGui::OpenPopup("Directory Already Exists");
                    } else {
                        project->getProjectCommandHistory()->addCommand(new CreateDirCmd(dirName, currentPath.string()));
                        scanDirectory(currentPath);
                        isCreatingNewDirectory = false;
                        ImGui::CloseCurrentPopup();
                    }
                } catch (const fs::filesystem_error& e) {
                    ImGui::OpenPopup("Creation Error");
                }
            }
            if (dirName.empty()) {
                ImGui::OpenPopup("Invalid Name");
            }
        }

        // Error popup for existing directory
        if (ImGui::BeginPopupModal("Directory Already Exists", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("A directory with this name already exists.");
            ImGui::Separator();

            float popupWidth = ImGui::GetWindowSize().x;
            float buttonWidth = 120.0f;
            ImGui::SetCursorPosX((popupWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Error popup for creation failure
        if (ImGui::BeginPopupModal("Creation Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to create the directory.");
            ImGui::Separator();

            float popupWidth = ImGui::GetWindowSize().x;
            float buttonWidth = 120.0f;
            ImGui::SetCursorPosX((popupWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Error popup for invalid name
        if (ImGui::BeginPopupModal("Invalid Name", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Please enter a valid directory name.");
            ImGui::Separator();

            float popupWidth = ImGui::GetWindowSize().x;
            float buttonWidth = 120.0f;
            ImGui::SetCursorPosX((popupWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }
}

void editor::ResourcesWindow::handleRename(){
    // Handle rename popup
    if (isRenaming) {
        ImGui::OpenPopup("Rename File");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    if (ImGui::BeginPopupModal("Rename File", &isRenaming, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Rename '%s' to:", fileBeingRenamed.c_str());

        // Auto-focus the input field when the popup opens
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(-1);

        // On the first frame, preselect the file name without its extension.
        auto selectNameCallback = [](ImGuiInputTextCallbackData* data) -> int {
            bool& pending = *static_cast<bool*>(data->UserData);
            if (!pending) return 0;

            std::string_view name(data->Buf, data->BufTextLen);
            size_t dotPos = name.find_last_of('.');
            // A leading dot means a hidden file with no extension: select everything.
            int selEnd = (dotPos != std::string_view::npos && dotPos > 0) ? (int)dotPos : data->BufTextLen;

            data->CursorPos = selEnd;
            data->SelectionStart = 0;
            data->SelectionEnd = selEnd;
            pending = false;
            return 0;
        };

        bool enterPressed = ImGui::InputText("##rename", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways, selectNameCallback, &renameSelectPending);

        // Calculate total width of the buttons
        float buttonWidth = 120.0f;
        float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
        float totalWidth = (buttonWidth * 2) + buttonSpacing;

        // Center the buttons
        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - totalWidth) * 0.5f);

        bool confirmed = ImGui::Button("OK", ImVec2(buttonWidth, 0)) || enterPressed;
        if (confirmed) {
            std::string newName = nameBuffer;
            if (!newName.empty() && newName != fileBeingRenamed) {
                try {
                    fs::path oldPath = currentPath / fileBeingRenamed;
                    fs::path newPath = currentPath / newName;

                    // Check if the target file already exists
                    if (fs::exists(newPath)) {
                        ImGui::OpenPopup("File Already Exists");
                    } else {
                        project->getProjectCommandHistory()->addCommand(new RenameFileCmd(project, fileBeingRenamed, newName, currentPath.string()));
                        codeEditor->handleFileRename(oldPath, newPath);
                        imageViewerWindow->handleFileRename(oldPath, newPath);
                        scanDirectory(currentPath);
                        isRenaming = false;
                        ImGui::CloseCurrentPopup();
                    }
                } catch (const fs::filesystem_error& e) {
                    ImGui::OpenPopup("Rename Error");
                }
            }
            if (newName.empty()) {
                ImGui::OpenPopup("Invalid Name");
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            isRenaming = false;
            ImGui::CloseCurrentPopup();
        }

        // Error popup for existing file
        if (ImGui::BeginPopupModal("File Already Exists", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("A file with this name already exists.");
            ImGui::Separator();

            float popupWidth = ImGui::GetWindowSize().x;
            float buttonWidth = 120.0f;
            ImGui::SetCursorPosX((popupWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Error popup for rename failure
        if (ImGui::BeginPopupModal("Rename Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to rename the file.");
            ImGui::Separator();

            float popupWidth = ImGui::GetWindowSize().x;
            float buttonWidth = 120.0f;
            ImGui::SetCursorPosX((popupWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Error popup for invalid name
        if (ImGui::BeginPopupModal("Invalid Name", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Please enter a valid file name.");
            ImGui::Separator();

            float popupWidth = ImGui::GetWindowSize().x;
            float buttonWidth = 120.0f;
            ImGui::SetCursorPosX((popupWidth - buttonWidth) * 0.5f);

            if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }
}

void editor::ResourcesWindow::copySelectedFiles(bool cut) {
    clipboardFiles.clear();
    clipboardCut = cut;

    for (const auto& fileName : selectedFiles) {
        clipboardFiles.push_back((currentPath / fileName).string());
    }
}

void editor::ResourcesWindow::pasteFiles(const fs::path& targetDirectory) {
    project->getProjectCommandHistory()->addCommand(new CopyFileCmd(project, clipboardFiles, targetDirectory.string(), !clipboardCut));

    // Clear clipboard if it was a cut operation
    if (clipboardCut) {
        clipboardFiles.clear();
    }

    scanDirectory(currentPath);
}

void editor::ResourcesWindow::importExternalPaths(const std::vector<std::string>& sourcePaths) {
    std::vector<std::string> validPaths;

    for (const auto& sourcePath : sourcePaths) {
        fs::path source = fs::path(sourcePath).lexically_normal();

        std::error_code ec;
        if (fs::is_directory(source, ec) && !ec) {
            // copying a directory into itself would recurse forever
            fs::path relative = currentPath.lexically_normal().lexically_relative(source);
            if (!relative.empty() && *relative.begin() != "..") {
                Log::error("Cannot import '%s': destination is inside it", source.string().c_str());
                continue;
            }
        }

        validPaths.push_back(sourcePath);
    }

    if (validPaths.empty()) {
        return;
    }

    project->getProjectCommandHistory()->addCommand(new CopyFileCmd(project, validPaths, currentPath.string(), true));
    scanDirectory(currentPath);
}

void editor::ResourcesWindow::queueThumbnailGeneration(const fs::path& filePath, FileType type, bool forceRegenerate) {
    std::error_code ec;
    if (!fs::exists(filePath, ec) || ec) {
        return;
    }

    fs::path normalizedPath = filePath;
    if (normalizedPath.is_relative() && !project->getProjectPath().empty()) {
        normalizedPath = project->getProjectPath() / normalizedPath;
    }
    normalizedPath = normalizedPath.lexically_normal();

    const std::string requestKey = thumbnailRequestKey(normalizedPath);

    fs::path thumbnailPath = project->getThumbnailPath(normalizedPath);

    ThumbnailRequest thumbFile = {normalizedPath, type, 0};
    ec.clear();
    if (!forceRegenerate && fs::exists(thumbnailPath, ec) && !ec) {
        std::error_code imageTimeEc;
        std::error_code thumbTimeEc;
        auto imageTime = fs::last_write_time(normalizedPath, imageTimeEc);
        auto thumbTime = fs::last_write_time(thumbnailPath, thumbTimeEc);
        if (!imageTimeEc && !thumbTimeEc && thumbTime >= imageTime) {
            {
                std::lock_guard<std::mutex> lock(thumbnailMutex);
                if (thumbnailWorkSuspended) {
                    return;
                }
                thumbFile.generation = thumbnailGeneration;
            }
            // Thumbnail is up-to-date, queue it for loading. Consumers verify
            // the generation in case a project switch races this push.
            std::lock_guard<std::mutex> lock(completedThumbnailMutex);
            completedThumbnailQueue.push(thumbFile);
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lock(thumbnailMutex);
        if (thumbnailWorkSuspended) {
            return;
        }
        if (!pendingThumbnailRequests.insert(requestKey).second) {
            return;
        }
        thumbFile.generation = thumbnailGeneration;
        thumbnailQueue.push(thumbFile);
    }
    thumbnailCondition.notify_one();
}

void editor::ResourcesWindow::thumbnailWorker() {
    // warm shared preview resources in background: rendering the material preview
    // scene once generates the IBL environment maps, which are pooled by texture id
    // (TexturePool), so the first material preview in Properties opens without lag
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!stopThumbnailThread && !Engine::isViewLoaded() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        bool runWarmup = false;
        if (!stopThumbnailThread && Engine::isViewLoaded()) {
            std::lock_guard<std::mutex> lock(thumbnailMutex);
            if (!thumbnailWorkSuspended) {
                thumbnailWorkerBusy = true;
                runWarmup = true;
            }
        }
        if (runWarmup) {
            try {
                Engine::AsyncThreadScope asyncThreadScope;
                Engine::executeSceneOnce(materialRender.getScene());
            } catch (const std::exception& e) {
                Log::error("Failed to warm thumbnail preview: %s", e.what());
            } catch (...) {
                Log::error("Failed to warm thumbnail preview: unknown error");
            }
            {
                std::lock_guard<std::mutex> lock(thumbnailMutex);
                thumbnailWorkerBusy = false;
            }
            thumbnailCondition.notify_all();
        }
    }

    while (!stopThumbnailThread) {
        ThumbnailRequest thumbFile;
        {
            std::unique_lock<std::mutex> lock(thumbnailMutex);
            // Wait until there is work or the thread is stopped
            thumbnailCondition.wait(lock, [this]() {
                return (!thumbnailWorkSuspended && !thumbnailQueue.empty() &&
                        !hasPendingMaterialRender && !hasPendingModelRender) || stopThumbnailThread;
            });

            // If we're stopping, exit
            if (stopThumbnailThread) {
                return;
            }

            // Get the next file path
            thumbFile = thumbnailQueue.front();
            thumbnailQueue.pop();
            thumbnailWorkerBusy = true;
        }

        try {
        // Generate thumbnail
        if (thumbFile.type == FileType::IMAGE) {
            fs::path thumbnailPath = project->getThumbnailPath(thumbFile.path);
            bool thumbnailWritten = writeImageThumbnail(thumbFile.path, thumbnailPath);

            if (thumbnailWritten) {
                {
                    std::lock_guard<std::mutex> lock(completedThumbnailMutex);
                    completedThumbnailQueue.push(thumbFile);
                }
            } else {
                std::cerr << "Failed to generate image thumbnail: " << thumbFile.path.string() << std::endl;
            }

            {
                std::lock_guard<std::mutex> lock(thumbnailMutex);
                pendingThumbnailRequests.erase(thumbnailRequestKey(thumbFile.path));
            }
        } else if (thumbFile.type == FileType::MATERIAL) {
            try {
                if (YAML::Node materialNode = YAML::LoadFile(thumbFile.path.string())) {
                    Material material = Stream::decodeMaterial(materialNode);
                    materialRender.applyMaterial(material);

                    {
                        Engine::AsyncThreadScope asyncThreadScope;
                        Engine::executeSceneOnce(materialRender.getScene());
                    }

                    // Set the pending flag before executing the scene
                    {
                        std::lock_guard<std::mutex> lock(thumbnailMutex);
                        if (!thumbnailWorkSuspended && thumbFile.generation == thumbnailGeneration) {
                            pendingMaterialPath = thumbFile.path;
                            pendingMaterialGeneration = thumbFile.generation;
                            hasPendingMaterialRender = true;
                        }
                    }

                    // The processMaterialThumbnails method will handle the rest
                    // and set hasPendingMaterialRender to false when done
                } else {
                    std::lock_guard<std::mutex> lock(thumbnailMutex);
                    pendingThumbnailRequests.erase(thumbnailRequestKey(thumbFile.path));
                }
            } catch (const std::exception& e) {
                // Log the error and continue with other thumbnails
                std::cerr << "Error generating thumbnail for material: " << thumbFile.path.string() << " - " << e.what() << std::endl;

                // Make sure we reset the pending flag if there was an error
                {
                    std::lock_guard<std::mutex> lock(thumbnailMutex);
                    if (pendingMaterialGeneration == thumbFile.generation) {
                        hasPendingMaterialRender = false;
                        pendingMaterialPath.clear();
                        pendingMaterialGeneration = 0;
                    }
                }
                std::lock_guard<std::mutex> lock(thumbnailMutex);
                pendingThumbnailRequests.erase(thumbnailRequestKey(thumbFile.path));
            }

        } else if (thumbFile.type == FileType::MODEL) {
            try {
                // Run the load under the async-thread flag: building the multi-node child-entity
                // hierarchy mutates the ECS, which is only safe on the main thread, so off-thread the
                // loader bakes node transforms into one static preview mesh instead.
                // GPU work is deferred to the main thread; ModelRender frames whatever entities exist.
                // Cap texture decode resolution too: a 128px preview never needs full 4K maps.
                Engine::AsyncThreadScope loadAsyncScope;
                MeshSystem::setImageDecodeMaxDimension(256);
                const bool modelLoaded = modelRender.loadModel(thumbFile.path.string());
                MeshSystem::setImageDecodeMaxDimension(0);
                if (modelLoaded) {
                    modelRender.fixDarkMaterials();
                    modelRender.positionCameraForModel();

                    {
                        Engine::AsyncThreadScope asyncThreadScope;
                        Engine::executeSceneOnce(modelRender.getScene());
                    }

                    {
                        std::lock_guard<std::mutex> lock(thumbnailMutex);
                        if (!thumbnailWorkSuspended && thumbFile.generation == thumbnailGeneration) {
                            pendingModelPath = thumbFile.path;
                            pendingModelGeneration = thumbFile.generation;
                            hasPendingModelRender = true;
                        }
                    }
                } else {
                    Log::error("Failed to load model for thumbnail: %s", thumbFile.path.string().c_str());
                    std::lock_guard<std::mutex> lock(thumbnailMutex);
                    pendingThumbnailRequests.erase(thumbnailRequestKey(thumbFile.path));
                }
            } catch (const std::exception& e) {
                MeshSystem::setImageDecodeMaxDimension(0);
                std::cerr << "Error generating thumbnail for model: " << thumbFile.path.string() << " - " << e.what() << std::endl;

                {
                    std::lock_guard<std::mutex> lock(thumbnailMutex);
                    if (pendingModelGeneration == thumbFile.generation) {
                        hasPendingModelRender = false;
                        pendingModelPath.clear();
                        pendingModelGeneration = 0;
                    }
                }
                std::lock_guard<std::mutex> lock(thumbnailMutex);
                pendingThumbnailRequests.erase(thumbnailRequestKey(thumbFile.path));
            }
        }
        } catch (const std::exception& e) {
            MeshSystem::setImageDecodeMaxDimension(0);
            Log::error("Unhandled thumbnail error for '%s': %s", thumbFile.path.string().c_str(), e.what());
            std::lock_guard<std::mutex> lock(thumbnailMutex);
            if (thumbFile.generation == thumbnailGeneration) {
                pendingThumbnailRequests.erase(thumbnailRequestKey(thumbFile.path));
            }
        } catch (...) {
            MeshSystem::setImageDecodeMaxDimension(0);
            Log::error("Unhandled thumbnail error for '%s'", thumbFile.path.string().c_str());
            std::lock_guard<std::mutex> lock(thumbnailMutex);
            if (thumbFile.generation == thumbnailGeneration) {
                pendingThumbnailRequests.erase(thumbnailRequestKey(thumbFile.path));
            }
        }

        {
            std::lock_guard<std::mutex> lock(thumbnailMutex);
            thumbnailWorkerBusy = false;
        }
        thumbnailCondition.notify_all();
    }
}

ImTextureID editor::ResourcesWindow::getThumbnailTexture(const std::string& thumbnailPath, int& outWidth, int& outHeight) {
    auto it = thumbnailTextures.find(thumbnailPath);
    if (it == thumbnailTextures.end()) {
        ThumbnailTexture cached;
        cached.texture = Texture(thumbnailPath, TextureData(thumbnailPath.c_str()));
        cached.failed = !cached.texture.load();
        it = thumbnailTextures.emplace(thumbnailPath, std::move(cached)).first;
    }

    ThumbnailTexture& cached = it->second;
    cached.lastUsedFrame = ImGui::GetFrameCount();

    if (cached.failed) {
        return ImTextureID{};
    }

    TextureRender* render = cached.texture.getRender();
    if (!render || !render->isCreated()) {
        // GPU creation failed (e.g. texture pool exhausted); keep the entry
        // marked as failed so we don't retry every frame. LRU eviction will
        // drop it eventually, allowing a retry on a later visit.
        cached.failed = true;
        return ImTextureID{};
    }

    outWidth = static_cast<int>(cached.texture.getWidth());
    outHeight = static_cast<int>(cached.texture.getHeight());

    return Backend::getImGuiTexture(render);
}

ImTextureID editor::ResourcesWindow::getAssetThumbnail(const fs::path& filePath, int& outWidth, int& outHeight) {
    outWidth = outHeight = 0;
    if (filePath.empty() || !Engine::isViewLoaded()) {
        return ImTextureID{};
    }

    const fs::path assetPath = project->resolveAssetPath(filePath);
    std::error_code ec;
    if (!fs::is_regular_file(assetPath, ec)) {
        return ImTextureID{};
    }

    const fs::path thumbnailPath = project->getThumbnailPath(assetPath);
    if (!fs::is_regular_file(thumbnailPath, ec)) {
        requestThumbnailGeneration(assetPath);
        return ImTextureID{};
    }

    ImTextureID texture = getThumbnailTexture(thumbnailPath.string(), outWidth, outHeight);
    enforceThumbnailCacheLimit();
    return texture;
}

void editor::ResourcesWindow::evictThumbnailTexture(const std::string& thumbnailKey) {
    auto it = thumbnailTextures.find(thumbnailKey);
    if (it != thumbnailTextures.end()) {
        // destroy() also removes the entries from TexturePool/TextureDataPool,
        // otherwise the GPU texture would outlive the cache (and a later
        // reload would pick up the stale pooled texture)
        it->second.texture.destroy();
        thumbnailTextures.erase(it);
    }
}

void editor::ResourcesWindow::enforceThumbnailCacheLimit() {
    const int currentFrame = ImGui::GetFrameCount();

    while (thumbnailTextures.size() > MAX_THUMBNAIL_TEXTURES) {
        auto oldest = thumbnailTextures.end();
        for (auto it = thumbnailTextures.begin(); it != thumbnailTextures.end(); ++it) {
            if (it->second.lastUsedFrame == currentFrame) {
                continue; // used this frame, keep it
            }
            if (oldest == thumbnailTextures.end() || it->second.lastUsedFrame < oldest->second.lastUsedFrame) {
                oldest = it;
            }
        }
        if (oldest == thumbnailTextures.end()) {
            break; // everything in the cache is visible this frame
        }
        oldest->second.texture.destroy();
        thumbnailTextures.erase(oldest);
    }
}

void editor::ResourcesWindow::clearThumbnailTextures() {
    for (auto& [key, cached] : thumbnailTextures) {
        cached.texture.destroy();
    }
    thumbnailTextures.clear();
}

fs::path editor::ResourcesWindow::uniqueRelativePath(const fs::path& directory, const std::string& baseName, const std::string& extension) {
    std::string sanitized = baseName;
    for (char& c : sanitized) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    std::string fileName = sanitized + extension;
    fs::path targetFile = directory / fileName;
    int counter = 1;
    while (fs::exists(targetFile)) {
        fileName = sanitized + "_" + std::to_string(counter) + extension;
        targetFile = directory / fileName;
        counter++;
    }
    return fs::relative(targetFile, project->getProjectPath());
}

void editor::ResourcesWindow::saveMaterialFile(const fs::path& directory, const char* materialContent, size_t contentLen, const MaterialPayload* sourceMaterial) {
    project->getProjectCommandHistory()->addCommandNoMerge(new CreateMaterialFileCmd(project, directory, materialContent, contentLen, sourceMaterial));
    scanDirectory(currentPath);
}

void editor::ResourcesWindow::saveEntityFile(const fs::path& directory, const char* entityContent, size_t contentLen) {
    std::string yamlString;
    if (contentLen >= sizeof(EntityPayload)) {
        yamlString = std::string(entityContent + sizeof(EntityPayload), contentLen - sizeof(EntityPayload));
    }

    YAML::Node entityNode = YAML::Load(yamlString);

    std::string entityName = "Bundle";
    if (entityNode["name"]) {
        entityName = entityNode["name"].as<std::string>();
    } else if (entityNode["members"] && entityNode["members"].IsSequence() && entityNode["members"].size() > 0) {
        YAML::Node firstMember = entityNode["members"][0];
        if (firstMember["name"]) {
            entityName = firstMember["name"].as<std::string>();
        }
    }

    std::string baseName = entityName.empty() ? "Bundle" : entityName;
    fs::path relativePath = uniqueRelativePath(directory, baseName, ".bundle");

    CreateEntityBundleCmd* createBundleCmd = new CreateEntityBundleCmd(project, project->getSelectedSceneId(), relativePath, entityNode);
    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(createBundleCmd);

    scanDirectory(currentPath);
}

void editor::ResourcesWindow::cleanupThumbnails() {
    // 1. Collect all valid thumbnail paths of existing files
    std::unordered_set<std::string> validThumbPaths;

    // Recursively scan project directory for files that have thumbnails
    for (auto& p : fs::recursive_directory_iterator(project->getProjectPath())) {
        if (!fs::is_regular_file(p.status()))
            continue;

        std::string ext = p.path().extension().string();
        if (Util::isImageFile(ext) || Util::isMaterialFile(ext) || Util::isModelFile(ext)) {
            fs::path thumbnailPath = project->getThumbnailPath(p.path());
            validThumbPaths.insert(thumbnailPath.string());
        }
    }

    // 2. Walk .doriax/thumbs/ and remove any orphaned thumbnails
    fs::path thumbsDir = project->getThumbsDir();
    if (!fs::exists(thumbsDir)) return;

    // Recursively iterate through the thumbnails directory to handle subdirectories
    for (auto& p : fs::recursive_directory_iterator(thumbsDir)) {
        if (!fs::is_regular_file(p.status())) continue;

        std::string thumbnailPath = p.path().string();
        if (validThumbPaths.find(thumbnailPath) == validThumbPaths.end()) {
            // This thumbnail file doesn't correspond to any existing image/material file
            std::error_code ec;
            fs::remove(p, ec);

            // Also remove from the loaded thumbnails cache
            evictThumbnailTexture(thumbnailPath);
        }
    }

    // 3. Remove empty directories in the thumbnails folder
    for (auto& p : fs::recursive_directory_iterator(thumbsDir)) {
        if (fs::is_directory(p.status()) && fs::is_empty(p.path())) {
            std::error_code ec;
            fs::remove(p, ec);
        }
    }
}

void editor::ResourcesWindow::refreshCurrentDirectory() {
    scanDirectory(currentPath);
}

void editor::ResourcesWindow::show() {
    if (firstOpen) {
        // Load saved settings (AppSettings is initialized by now)
        iconSize = AppSettings::getResourcesIconSize();
        currentLayout = static_cast<LayoutType>(AppSettings::getResourcesLayout());
        itemViewStyle = static_cast<ItemViewStyle>(AppSettings::getResourcesItemViewStyle());
        leftPanelWidth = AppSettings::getResourcesLeftPanelWidth();

        int iconWidth, iconHeight;

        TextureData data;

        data.loadTextureFromMemory(folder_icon_png, folder_icon_png_len);
        folderIcon.setData("editor:resources:folder_icon", data);
        folderIcon.load();

        data.loadTextureFromMemory(file_icon_png, file_icon_png_len);
        fileIcon.setData("editor:resources:file_icon", data);
        fileIcon.load();

        data.loadTextureFromMemory(scene_icon_png, scene_icon_png_len);
        sceneIcon.setData("editor:resources:scene_icon", data);
        sceneIcon.load();

        data.loadTextureFromMemory(entity_icon_png, entity_icon_png_len);
        entityIcon.setData("editor:resources:entity_icon", data);
        entityIcon.load();

        data.loadTextureFromMemory(source_icon_png, source_icon_png_len);
        sourceIcon.setData("editor:resources:source_icon", data);
        sourceIcon.load();

        data.loadTextureFromMemory(header_icon_png, header_icon_png_len);
        headerIcon.setData("editor:resources:header_icon", data);
        headerIcon.load();

        data.loadTextureFromMemory(lua_icon_png, lua_icon_png_len);
        luaIcon.setData("editor:resources:lua_icon", data);
        luaIcon.load();

        data.loadTextureFromMemory(audio_icon_png, audio_icon_png_len);
        audioIcon.setData("editor:resources:audio_icon", data);
        audioIcon.load();

        data.loadTextureFromMemory(font_icon_png, font_icon_png_len);
        fontIcon.setData("editor:resources:font_icon", data);
        fontIcon.load();

        scanDirectory(project->getProjectPath().string());

        firstOpen = false;
    }

    // Process completed thumbnails
    while (true) {
        ThumbnailRequest thumbFile;
        {
            std::lock_guard<std::mutex> lock(completedThumbnailMutex);
            if (completedThumbnailQueue.empty()) {
                break;
            }
            thumbFile = completedThumbnailQueue.front();
            completedThumbnailQueue.pop();
        }
        {
            std::lock_guard<std::mutex> lock(thumbnailMutex);
            if (thumbFile.generation != thumbnailGeneration || thumbnailWorkSuspended) {
                continue;
            }
        }
        // Find and mark the corresponding file entry; the texture itself is
        // created on demand when the item becomes visible
        for (auto& file : files) {
            if ((currentPath / file.name) == thumbFile.path) {
                fs::path thumbnailPath = project->getThumbnailPath(thumbFile.path);
                std::error_code ec;
                if (fs::exists(thumbnailPath, ec) && !ec) {
                    file.hasThumbnail = true;
                    file.thumbnailPath = thumbnailPath.string();
                } else {
                    file.hasThumbnail = false;
                    file.thumbnailPath.clear();
                }
                break;
            }
        }
    }

    timeSinceLastCheck += ImGui::GetIO().DeltaTime;
    if (timeSinceLastCheck >= 1.0f) {
        try {
            auto currentWriteTime = fs::last_write_time(currentPath);
            if (currentWriteTime != lastWriteTime) {
                scanDirectory(currentPath);
            } else {
                // The project can change without touching this directory
                refreshProjectFiles();
            }
        } catch (const fs::filesystem_error& e) {
            // Handle potential filesystem errors silently
        }
        timeSinceLastCheck = 0.0f;
    }

    if (!windowOpen) {
        windowFocused = false;
        return;
    }

    ctrlPressed = ImGui::GetIO().KeyCtrl;
    shiftPressed = ImGui::GetIO().KeyShift;

    windowPos = ImGui::GetWindowPos();
    scrollOffset = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());

    if (focusRequested) {
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }

    bool wasOpen = windowOpen;

    if (!ImGui::Begin(ResourcesWindow::WINDOW_NAME, &windowOpen)) {
        windowFocused = false;
        ImGui::End();
        if (wasOpen && !windowOpen) {
            setOpen(false);
        }
        return;
    }

    windowPos = ImGui::GetWindowPos();
    scrollOffset = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());

    // Determine effective layout when in AUTO mode
    LayoutType effectiveLayout = currentLayout;
    if (currentLayout == LayoutType::AUTO) {
        float windowWidth = ImGui::GetWindowWidth();
        effectiveLayout = (windowWidth < Theme::dpi(layoutAutoThreshold)) ? LayoutType::GRID : LayoutType::SPLIT_FILES_ONLY;
    }

    ImGuiTableFlags table_flags_for_sort_specs = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders;

    // Full-height header vertically centered on a text-height row, overlapping the
    // spacing above/below (same layout pattern as the scene window toolbar)
    float headerRowY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(headerRowY - (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
    renderHeader();
    ImGui::SetCursorPosY(headerRowY + ImGui::GetTextLineHeightWithSpacing());
    ImGui::Separator();

    if (effectiveLayout == LayoutType::GRID) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (ImGui::BeginTable("for_sort_specs_only", 2, table_flags_for_sort_specs, ImVec2(0.0f, ImGui::GetFrameHeight()))) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Type");
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty || requestSort) {
                    sortWithSortSpecs(sort_specs, files);
                    sort_specs->SpecsDirty = requestSort = false;
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::BeginChild("FileTableScrollRegion", ImVec2(0, 0), true);
        renderFileListing(true);
        ImGui::EndChild();
    } else if (effectiveLayout == LayoutType::SPLIT_FILES_ONLY || effectiveLayout == LayoutType::SPLIT) {
        float splitterWidth = Theme::dpi(4.0f);
        ImGui::BeginChild("LeftPanel", ImVec2(Theme::dpi(leftPanelWidth), 0), true);
        renderDirectoryTree(project->getProjectPath());
        ImGui::EndChild();
        ImGui::SameLine();
        float splitterX = ImGui::GetCursorPosX();
        ImGui::InvisibleButton("splitter", ImVec2(splitterWidth, -1));

        // Handle visual appearance of the splitter
        ImVec2 splitterMin = ImGui::GetItemRectMin();
        ImVec2 splitterMax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            splitterMin, 
            splitterMax, 
            ImGui::GetColorU32(ImGui::IsItemHovered() || ImGui::IsItemActive() 
                ? ImGuiCol_SliderGrabActive 
                : ImGuiCol_SliderGrab)
        );

        // Handle splitter interaction
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            const float dpi = Theme::dpiScale();
            leftPanelWidth += ImGui::GetIO().MouseDelta.x / dpi;
            const float maxLogical = std::max(100.0f, ImGui::GetWindowWidth() / dpi - 100.0f);
            leftPanelWidth = ImClamp(leftPanelWidth, 100.0f, maxLogical);
        }
        if (ImGui::IsItemDeactivated()) {
            AppSettings::setResourcesLeftPanelWidth(leftPanelWidth);
            AppSettings::saveSettings();
        }
        ImGui::SameLine();
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (ImGui::BeginTable("for_sort_specs_only", 2, table_flags_for_sort_specs, ImVec2(0.0f, ImGui::GetFrameHeight()))) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Type");
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty || requestSort) {
                    sortWithSortSpecs(sort_specs, files);
                    sort_specs->SpecsDirty = requestSort = false;
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::BeginChild("FileTableScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        renderFileListing(effectiveLayout == LayoutType::SPLIT);
        ImGui::EndChild();
        ImGui::EndChild();
    }

    // Remaining code (drag and drop, context menus, keyboard shortcuts, etc.) remains unchanged
    if (ImGui::BeginDragDropTarget()) {
        isDragDropTarget = true;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("external_files")) {
            importExternalPaths(Util::getStringsFromPayload(payload));
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("material")) {
            const char* materialContent = static_cast<const char*>(payload->Data);
            size_t contentLen = payload->DataSize;

            MaterialPayload materialSource{0, NULL_PROJECT_SCENE, NULL_ENTITY, 0};
            const MaterialPayload* sourcePtr = nullptr;

            if (contentLen > sizeof(MaterialPayload)) {
                const MaterialPayload* candidate = reinterpret_cast<const MaterialPayload*>(materialContent);
                const char* yamlData = materialContent + sizeof(MaterialPayload);

                // New payload format: [MaterialPayload header][YAML string]
                if (candidate->magic == 0x4D54524C) {
                    materialSource = *candidate;
                    sourcePtr = &materialSource;
                    materialContent = yamlData;
                    contentLen -= sizeof(MaterialPayload);
                }
            }

            saveMaterialFile(currentPath, materialContent, contentLen, sourcePtr);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity")) {
            const char* entityContent = (const char*)payload->Data;
            size_t contentLen = payload->DataSize;

            saveEntityFile(currentPath, entityContent, contentLen);
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImGuiDragDropFlags target_flags = ImGuiDragDropFlags_AcceptBeforeDelivery;
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload()) {
            if (payload->IsDataType("external_files")) highlightDragAndDrop();
        }
    }

    if (isExternalDragHovering) highlightDragAndDrop();

    windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (windowFocused) {
        if (!selectedFiles.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            // Trigger delete confirmation (handled in renderFileListing)
            showDeleteConfirmation = true;
        }
        if (ctrlPressed) {
            if (ImGui::IsKeyPressed(ImGuiKey_C)) copySelectedFiles(false);
            else if (ImGui::IsKeyPressed(ImGuiKey_X)) copySelectedFiles(true);
            else if (ImGui::IsKeyPressed(ImGuiKey_V) && !clipboardFiles.empty()) pasteFiles(currentPath);
            else if (ImGui::IsKeyPressed(ImGuiKey_A)) {
                // Directories are only selectable when the listing shows them
                bool listingShowsDirectories = (effectiveLayout != LayoutType::SPLIT_FILES_ONLY);
                selectedFiles.clear();
                for (const auto& file : files) {
                    if (listingShowsDirectories || !file.isDirectory) selectedFiles.insert(file.name);
                }
                if (!files.empty()) lastSelectedFile = files.back().name;
            }
        }
    }

    ImGui::End();

    // Evict least-recently-used thumbnail textures (entries used this frame
    // are never evicted)
    enforceThumbnailCacheLimit();

    if (wasOpen && !windowOpen) {
        setOpen(false);
    }

    handleNewDirectory();
    handleRename();
}
