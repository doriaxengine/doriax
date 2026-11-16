// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ImageViewerWindow.h"

#include "Backend.h"
#include "Theme.h"
#include "external/IconsFontAwesome6.h"
#include "Out.h"
#include "Project.h"

#include <algorithm>
#include <cmath>

using namespace doriax;

editor::ImageViewerWindow::ImageViewerWindow(Project* project) : project(project) {
}

editor::ImageViewerWindow::~ImageViewerWindow() {
    closeAll();
    releaseRetiredTextures();
}

std::filesystem::path editor::ImageViewerWindow::resolveFilepath(const std::filesystem::path& path) const {
    if (path.is_absolute()) {
        return path;
    }
    return project->getProjectPath() / path;
}

std::string editor::ImageViewerWindow::toRelativePath(const std::filesystem::path& path) const {
    const std::filesystem::path normalizedPath = path.lexically_normal();
    if (normalizedPath.is_relative() || project->getProjectPath().empty()) {
        return normalizedPath.string();
    }

    std::error_code ec;
    std::filesystem::path relativePath = std::filesystem::relative(
        normalizedPath,
        project->getProjectPath(),
        ec
    );
    return (!ec && !relativePath.empty()) ? relativePath.lexically_normal().string()
                                          : normalizedPath.string();
}

std::string editor::ImageViewerWindow::getWindowId(const std::filesystem::path& relativePath) {
    return "###ImageViewer:" + relativePath.lexically_normal().string();
}

void editor::ImageViewerWindow::retireTexture(Texture& texture) {
    if (!texture.empty()) {
        // ImGui renders submitted draw lists after App::show().
        retiredTextures.push_back(texture);
    }
    texture = Texture();
}

void editor::ImageViewerWindow::releaseRetiredTextures() {
    for (Texture& texture : retiredTextures) {
        texture.destroy();
    }
    retiredTextures.clear();
}

void editor::ImageViewerWindow::loadImage(Instance& instance) {
    TextureData imageData;
    const std::filesystem::path fullPath = resolveFilepath(instance.filepath);
    const bool decoded = imageData.loadTextureFromFile(fullPath.string().c_str());
    if (!decoded || !imageData.getData() || imageData.getWidth() <= 0 || imageData.getHeight() <= 0) {
        imageData.releaseImageData();
        retireTexture(instance.texture);
        instance.loadError = "The image could not be decoded.";
        Out::error("Unable to preview image '%s'", fullPath.string().c_str());
        return;
    }

    Texture nextTexture;
    const std::string textureId = "editor:image-viewer:" + std::to_string(++textureGeneration);
    nextTexture.setData(textureId, imageData);

    // upload now, the check below would read the per-frame slice wait as a failure
    const bool asyncLoading = Engine::isAsyncLoading();
    Engine::setAsyncLoading(false);
    TextureRender* render = nextTexture.getRender();
    Engine::setAsyncLoading(asyncLoading);
    if (!render || !render->isCreated()) {
        nextTexture.destroy();
        imageData.releaseImageData();
        retireTexture(instance.texture);
        instance.loadError = "The image was decoded, but its GPU texture could not be created.";
        Out::error("Unable to create image viewer texture for '%s'", fullPath.string().c_str());
        return;
    }

    nextTexture.releaseData();

    retireTexture(instance.texture);
    instance.texture = nextTexture;
    instance.loadError.clear();
}

void editor::ImageViewerWindow::openFile(const std::filesystem::path& path, bool dockToCentral) {
    const std::string key = toRelativePath(path);
    if (key.empty()) {
        return;
    }

    auto existing = instances.find(key);
    if (existing != instances.end()) {
        existing->second.focusRequested = dockToCentral;
        loadImage(existing->second);
        Backend::getApp().addImageViewerWindowToDock(existing->second.filepath, dockToCentral);
        return;
    }

    Instance& instance = instances[key];
    instance.filepath = std::filesystem::path(key);
    instance.focusRequested = dockToCentral;

    loadImage(instance);

    project->addTab(TabType::IMAGE_VIEWER, key);
    project->saveWorkspaceFile();
    Backend::getApp().addImageViewerWindowToDock(instance.filepath, dockToCentral);
}

void editor::ImageViewerWindow::closeFile(const std::filesystem::path& path) {
    const std::string key = toRelativePath(path);

    auto instanceIt = instances.find(key);
    if (instanceIt != instances.end()) {
        retireTexture(instanceIt->second.texture);
        instances.erase(instanceIt);
    }

    if (project->hasTab(TabType::IMAGE_VIEWER, key)) {
        project->removeTab(TabType::IMAGE_VIEWER, key);
        project->saveWorkspaceFile();
    }
}

void editor::ImageViewerWindow::closeAll() {
    for (auto& entry : instances) {
        retireTexture(entry.second.texture);
    }
    instances.clear();
}

bool editor::ImageViewerWindow::isFileOpen(const std::filesystem::path& path) const {
    const std::string key = toRelativePath(path);
    return instances.find(key) != instances.end();
}

bool editor::ImageViewerWindow::handleFileRename(
    const std::filesystem::path& oldPath,
    const std::filesystem::path& newPath
) {
    const std::string oldKey = toRelativePath(oldPath);
    const std::string newKey = toRelativePath(newPath);
    auto oldIt = instances.find(oldKey);
    if (oldIt == instances.end() || newKey.empty()) {
        return false;
    }

    if (auto newIt = instances.find(newKey); newIt != instances.end()) {
        retireTexture(newIt->second.texture);
        instances.erase(newIt);
    }

    auto node = instances.extract(oldIt);
    node.key() = newKey;
    node.mapped().filepath = std::filesystem::path(newKey);
    node.mapped().focusRequested = true;
    instances.insert(std::move(node));

    project->removeTab(TabType::IMAGE_VIEWER, oldKey);
    project->addTab(TabType::IMAGE_VIEWER, newKey);
    project->saveWorkspaceFile();
    Backend::getApp().addImageViewerWindowToDock(std::filesystem::path(newKey));
    return true;
}

void editor::ImageViewerWindow::drawImage(Instance& instance) {
    if (!instance.loadError.empty()) {
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "Unable to display image");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", instance.loadError.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s", resolveFilepath(instance.filepath).string().c_str());
        return;
    }

    TextureRender* render = instance.texture.getRender();
    const float imageWidth = static_cast<float>(instance.texture.getWidth());
    const float imageHeight = static_cast<float>(instance.texture.getHeight());
    if (!render || !render->isCreated() || imageWidth <= 0.0f || imageHeight <= 0.0f) {
        ImGui::TextDisabled("The image texture is unavailable.");
        return;
    }

    const float currentScale = instance.fitToWindow ? instance.lastDisplayScale : instance.zoom;

    // Match SceneWindow's compact toolbar row.
    const float toolbarRowY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(
        toolbarRowY - (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f
    );

    if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS_MINUS "##ImageViewerZoomOut")) {
        instance.zoom = std::clamp(currentScale / 1.25f, MIN_ZOOM, MAX_ZOOM);
        instance.fitToWindow = false;
    }
    ImGui::SetItemTooltip("Zoom out");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS_PLUS "##ImageViewerZoomIn")) {
        instance.zoom = std::clamp(currentScale * 1.25f, MIN_ZOOM, MAX_ZOOM);
        instance.fitToWindow = false;
    }
    ImGui::SetItemTooltip("Zoom in");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_DOWN_LEFT_AND_UP_RIGHT_TO_CENTER "##ImageViewerFit")) {
        instance.fitToWindow = true;
        instance.pan = ImVec2(0.0f, 0.0f);
    }
    ImGui::SetItemTooltip("Fit image in window");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_MAXIMIZE "##ImageViewerActualSize")) {
        instance.fitToWindow = false;
        instance.zoom = 1.0f;
        instance.pan = ImVec2(0.0f, 0.0f);
    }
    ImGui::SetItemTooltip("Actual size (1:1)");

    ImGui::SameLine();
    ImGui::Text("%.0f%%", instance.lastDisplayScale * 100.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f x %.0f", imageWidth, imageHeight);

    ImGui::SetCursorPosY(toolbarRowY + ImGui::GetTextLineHeightWithSpacing());

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 1.0f);
    canvasSize.y = std::max(canvasSize.y, 1.0f);

    ImGui::InvisibleButton("##ImageViewerCanvas", canvasSize);
    const bool canvasHovered = ImGui::IsItemHovered();
    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    const ImVec2 canvasCenter(
        (canvasMin.x + canvasMax.x) * 0.5f,
        (canvasMin.y + canvasMax.y) * 0.5f
    );

    const float fitScale = std::max(
        MIN_ZOOM,
        std::min({canvasSize.x / imageWidth, canvasSize.y / imageHeight, 1.0f})
    );
    float displayScale = instance.fitToWindow
        ? fitScale
        : std::clamp(instance.zoom, MIN_ZOOM, MAX_ZOOM);

    ImGuiIO& io = ImGui::GetIO();
    if (canvasHovered && io.MouseWheel != 0.0f) {
        const ImVec2 oldImageSize(imageWidth * displayScale, imageHeight * displayScale);
        const ImVec2 oldImageMin(
            canvasCenter.x - oldImageSize.x * 0.5f + instance.pan.x,
            canvasCenter.y - oldImageSize.y * 0.5f + instance.pan.y
        );
        const ImVec2 mousePosition = io.MousePos;
        const ImVec2 imagePoint(
            (mousePosition.x - oldImageMin.x) / displayScale,
            (mousePosition.y - oldImageMin.y) / displayScale
        );

        instance.zoom = std::clamp(
            displayScale * std::pow(1.15f, io.MouseWheel),
            MIN_ZOOM,
            MAX_ZOOM
        );
        instance.fitToWindow = false;
        displayScale = instance.zoom;

        const ImVec2 newImageSize(imageWidth * displayScale, imageHeight * displayScale);
        const ImVec2 centeredNewImageMin(
            canvasCenter.x - newImageSize.x * 0.5f + instance.pan.x,
            canvasCenter.y - newImageSize.y * 0.5f + instance.pan.y
        );
        const ImVec2 anchoredNewImageMin(
            mousePosition.x - imagePoint.x * displayScale,
            mousePosition.y - imagePoint.y * displayScale
        );
        instance.pan.x += anchoredNewImageMin.x - centeredNewImageMin.x;
        instance.pan.y += anchoredNewImageMin.y - centeredNewImageMin.y;
    }

    if (canvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        instance.fitToWindow = true;
        instance.pan = ImVec2(0.0f, 0.0f);
        displayScale = fitScale;
    } else {
        const bool leftPanning = ImGui::IsItemActive() &&
                                 ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
        const bool middlePanning = canvasHovered &&
                                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
        if (leftPanning || middlePanning) {
            if (instance.fitToWindow) {
                instance.zoom = displayScale;
                instance.fitToWindow = false;
            }
            instance.pan.x += io.MouseDelta.x;
            instance.pan.y += io.MouseDelta.y;
        }
    }

    if (canvasHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    instance.lastDisplayScale = displayScale;
    const ImVec2 displaySize(imageWidth * displayScale, imageHeight * displayScale);
    const ImVec2 imageMin(
        canvasCenter.x - displaySize.x * 0.5f + instance.pan.x,
        canvasCenter.y - displaySize.y * 0.5f + instance.pan.y
    );
    const ImVec2 imageMax(imageMin.x + displaySize.x, imageMin.y + displaySize.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasMin, canvasMax, ImGui::GetColorU32(ImGuiCol_FrameBg));
    drawList->PushClipRect(canvasMin, canvasMax, true);

    const ImVec2 visibleImageMin(
        std::max(imageMin.x, canvasMin.x),
        std::max(imageMin.y, canvasMin.y)
    );
    const ImVec2 visibleImageMax(
        std::min(imageMax.x, canvasMax.x),
        std::min(imageMax.y, canvasMax.y)
    );

    if (visibleImageMin.x < visibleImageMax.x && visibleImageMin.y < visibleImageMax.y) {
        const float checkerSize = Theme::dpi(16.0f);
        const int startX = std::max(0, static_cast<int>(std::floor((visibleImageMin.x - imageMin.x) / checkerSize)));
        const int startY = std::max(0, static_cast<int>(std::floor((visibleImageMin.y - imageMin.y) / checkerSize)));
        const int endX = static_cast<int>(std::ceil((visibleImageMax.x - imageMin.x) / checkerSize));
        const int endY = static_cast<int>(std::ceil((visibleImageMax.y - imageMin.y) / checkerSize));

        drawList->AddRectFilled(imageMin, imageMax, IM_COL32(105, 105, 105, 255));
        for (int y = startY; y < endY; ++y) {
            for (int x = startX; x < endX; ++x) {
                if ((x + y) % 2 == 0) {
                    const ImVec2 tileMin(imageMin.x + x * checkerSize, imageMin.y + y * checkerSize);
                    const ImVec2 tileMax(
                        std::min(tileMin.x + checkerSize, imageMax.x),
                        std::min(tileMin.y + checkerSize, imageMax.y)
                    );
                    drawList->AddRectFilled(tileMin, tileMax, IM_COL32(135, 135, 135, 255));
                }
            }
        }

        drawList->AddImage(
            Backend::getImGuiTexture(render),
            imageMin,
            imageMax
        );
        drawList->AddRect(imageMin, imageMax, ImGui::GetColorU32(ImGuiCol_Border));
    }

    drawList->PopClipRect();
}

void editor::ImageViewerWindow::show() {
    releaseRetiredTextures();

    bool tabsChanged = false;
    for (auto it = instances.begin(); it != instances.end();) {
        Instance& instance = it->second;

        if (instance.focusRequested) {
            ImGui::SetNextWindowFocus();
            instance.focusRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);

        std::string visibleTitle = instance.filepath.filename().string();
        if (visibleTitle.empty()) {
            visibleTitle = "Image Viewer";
        }
        const std::string windowTitle = visibleTitle + getWindowId(instance.filepath);

        const bool visible = ImGui::Begin(windowTitle.c_str(), &instance.isOpen);
        if (visible) {
            drawImage(instance);
        }
        ImGui::End();

        if (!instance.isOpen) {
            project->removeTab(TabType::IMAGE_VIEWER, instance.filepath.string());
            tabsChanged = true;
            retireTexture(instance.texture);
            it = instances.erase(it);
        } else {
            ++it;
        }
    }

    if (tabsChanged) {
        project->saveWorkspaceFile();
    }
}
