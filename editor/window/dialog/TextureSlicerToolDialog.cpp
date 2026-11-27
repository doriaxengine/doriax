// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TextureSlicerToolDialog.h"
#include "App.h"
#include "Backend.h"
#include "Project.h"
#include "Theme.h"
#include "window/Widgets.h"
#include "util/UIUtils.h"
#include "external/IconsFontAwesome6.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <climits>
#include <filesystem>

namespace doriax {
namespace editor {

namespace {

// Cells are in original pixels, and the FIT/RESIZE texture strategies change the pool copy's size
bool hasSheetSpacePixels(TextureData& data) {
    return data.getData() && data.getWidth() > 0 && data.getHeight() > 0 &&
           data.getWidth() == data.getOriginalWidth() && data.getHeight() == data.getOriginalHeight();
}

}

void TextureSlicerToolDialog::open(const Texture& previewTexture,
                                    int sheetWidth, int sheetHeight,
                                    std::function<void(const SliceResult&)> onApply,
                                    std::function<void()> onCancel) {
    m_isOpen = true;
    m_mode = Mode::SPRITE;
    m_previewTexture = previewTexture;
    m_sheetWidth = sheetWidth;
    m_sheetHeight = sheetHeight;
    m_onApply = onApply;
    m_onCancel = onCancel;
    m_previewDirty = true;

    if (!m_previewTexture.empty()) {
        int textureWidth = static_cast<int>(m_previewTexture.getWidth());
        int textureHeight = static_cast<int>(m_previewTexture.getHeight());

        // If texture data not loaded yet, force load to get dimensions
        if (textureWidth <= 0 || textureHeight <= 0) {
            m_previewTexture.load();
            textureWidth = static_cast<int>(m_previewTexture.getWidth());
            textureHeight = static_cast<int>(m_previewTexture.getHeight());
        }

        if (textureWidth > 0) {
            m_sheetWidth = textureWidth;
        }
        if (textureHeight > 0) {
            m_sheetHeight = textureHeight;
        }
    }

    // Smart defaults based on sheet size
    if (m_sheetWidth > 0 && m_sheetHeight > 0) {
        m_columns = std::max(1, m_sheetWidth / 32);
        m_rows = std::max(1, m_sheetHeight / 32);
        m_cellWidth = m_sheetWidth / m_columns;
        m_cellHeight = m_sheetHeight / m_rows;

        // Clamp to reasonable defaults
        if (m_columns > 16) m_columns = 4;
        if (m_rows > 16) m_rows = 4;
        m_cellWidth = m_sheetWidth / m_columns;
        m_cellHeight = m_sheetHeight / m_rows;
    }

    m_offsetX = 0;
    m_offsetY = 0;
    m_paddingX = 0;
    m_paddingY = 0;
    m_skipEmptyCells = false;
    m_skippedEmptyCount = 0;
    releaseAlphaData();
    strncpy(m_prefixBuffer, "frame_", sizeof(m_prefixBuffer) - 1);
    m_prefixBuffer[sizeof(m_prefixBuffer) - 1] = '\0';
}

void TextureSlicerToolDialog::openTileset(const std::vector<SubmeshInfo>& submeshes,
                                           int sheetWidth, int sheetHeight,
                                           std::function<void(const SliceResult&)> onApply,
                                           std::function<void()> onCancel) {
    m_submeshes = submeshes;
    m_selectedSubmesh = 0;

    Texture previewTexture;
    if (!m_submeshes.empty()) {
        previewTexture = m_submeshes[0].texture;
    }

    // Initialize shared slicing state via the base open
    open(previewTexture, sheetWidth, sheetHeight, onApply, onCancel);

    // Override for tileset mode
    m_mode = Mode::TILESET;
    m_skipEmptyCells = true;
    strncpy(m_prefixBuffer, "tile_", sizeof(m_prefixBuffer) - 1);
    m_prefixBuffer[sizeof(m_prefixBuffer) - 1] = '\0';
}

void TextureSlicerToolDialog::releaseAlphaData() {
    m_alphaTextureData.releaseImageData();
    m_alphaLoadedPath.clear();
    m_alphaDataReady = false;
    m_alphaUsesLive = false;
}

void TextureSlicerToolDialog::close() {
    m_isOpen = false;
    releaseAlphaData();
}

void TextureSlicerToolDialog::refreshAlphaData() {
    std::string texPath = m_previewTexture.empty() ? std::string() : m_previewTexture.getPath(0);
    // A texture still loading, or that failed to load, has no data array yet
    const bool useLive = m_previewTexture.hasData() && hasSheetSpacePixels(m_previewTexture.getData());
    // Live pixels are freed once the texture uploads, so a cache built on them can go stale
    if (m_alphaDataReady && texPath == m_alphaLoadedPath && (!m_alphaUsesLive || useLive)) {
        return;
    }

    m_alphaTextureData.releaseImageData();
    m_alphaLoadedPath = texPath;
    m_alphaDataReady = true;
    m_alphaUsesLive = useLive;

    if (useLive || texPath.empty()) {
        return;
    }

    std::string cleanPath = TextureData::parseSvgScalePath(texPath);
    std::filesystem::path resolved = cleanPath;
    if (Project* proj = Backend::getApp().getProject()) {
        resolved = proj->resolveAssetPath(cleanPath);
    }

    // Rasterize SVGs at the texture's scale, so the image has the sheet's size
    m_alphaTextureData.setSVGScale(m_previewTexture.getSvgScale());
    if (m_alphaTextureData.loadTextureFromFile(resolved.string().c_str()) && m_alphaTextureData.getData()) {
        m_alphaTextureData.setDataOwned(true);
    } else {
        m_alphaTextureData.releaseImageData();
    }
}

bool TextureSlicerToolDialog::isCellEmpty(int x, int y, int w, int h) {
    TextureData* data = &m_alphaTextureData;
    if (m_alphaUsesLive && m_previewTexture.hasData()) {
        data = &m_previewTexture.getData();
    }
    if (!hasSheetSpacePixels(*data)) {
        return false;
    }

    const int channels = data->getChannels();
    // Only textures with an alpha channel (LA = 2, RGBA = 4) can have transparent cells
    if (channels != 4 && channels != 2) {
        return false;
    }

    const int imgW = data->getWidth();
    const int imgH = data->getHeight();
    const int bytesPerChannel = TextureData::getBytesPerChannel(data->getColorFormat());
    const int stride = imgW * channels * bytesPerChannel;
    const unsigned char* pixels = static_cast<const unsigned char*>(data->getData());
    if (!pixels || imgW <= 0 || imgH <= 0) {
        return false;
    }

    const int x0 = std::clamp(x, 0, imgW);
    const int y0 = std::clamp(y, 0, imgH);
    const int x1 = std::clamp(x + w, 0, imgW);
    const int y1 = std::clamp(y + h, 0, imgH);
    // Outside the image: keep the cell rather than drop one that may have pixels
    if (x0 >= x1 || y0 >= y1) {
        return false;
    }

    const int alphaOffset = (channels - 1) * bytesPerChannel;
    for (int py = y0; py < y1; py++) {
        const unsigned char* row = pixels + static_cast<size_t>(py) * stride;
        for (int px = x0; px < x1; px++) {
            const unsigned char* alphaPtr = row + static_cast<size_t>(px) * channels * bytesPerChannel + alphaOffset;
            for (int b = 0; b < bytesPerChannel; b++) {
                if (alphaPtr[b] != 0) {
                    return false;
                }
            }
        }
    }

    return true;
}

void TextureSlicerToolDialog::generatePreview() {
    m_previewRects.clear();
    m_skippedEmptyCount = 0;

    if (m_sheetWidth <= 0 || m_sheetHeight <= 0) return;

    if (m_skipEmptyCells) {
        refreshAlphaData();
    }

    int cols, rows, cw, ch;

    if (m_sliceMode == SliceMode::GRID) {
        cols = std::max(1, m_columns);
        rows = std::max(1, m_rows);
        int availW = m_sheetWidth - m_offsetX;
        int availH = m_sheetHeight - m_offsetY;
        cw = std::max(1, (availW - m_paddingX * (cols - 1)) / cols);
        ch = std::max(1, (availH - m_paddingY * (rows - 1)) / rows);
    } else {
        cw = std::max(1, m_cellWidth);
        ch = std::max(1, m_cellHeight);
        int availW = m_sheetWidth - m_offsetX;
        int availH = m_sheetHeight - m_offsetY;
        cols = std::max(1, (availW + m_paddingX) / (cw + m_paddingX));
        rows = std::max(1, (availH + m_paddingY) / (ch + m_paddingY));
    }

    std::string prefix(m_prefixBuffer);
    int rectIndex = 0;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            float fx = static_cast<float>(m_offsetX + c * (cw + m_paddingX));
            float fy = static_cast<float>(m_offsetY + r * (ch + m_paddingY));
            float fw = static_cast<float>(cw);
            float fh = static_cast<float>(ch);

            // Clip to sheet bounds
            if (fx + fw > m_sheetWidth) fw = m_sheetWidth - fx;
            if (fy + fh > m_sheetHeight) fh = m_sheetHeight - fy;
            if (fw <= 0 || fh <= 0) continue;

            if (m_skipEmptyCells && isCellEmpty(static_cast<int>(fx), static_cast<int>(fy), static_cast<int>(fw), static_cast<int>(fh))) {
                m_skippedEmptyCount++;
                continue;
            }

            SlicedRect slicedRect;
            slicedRect.name = prefix + std::to_string(rectIndex);
            slicedRect.rect = Rect(fx, fy, fw, fh);

            m_previewRects.push_back(slicedRect);
            rectIndex++;
        }
    }

    m_previewDirty = false;
}

TextureSlicerToolDialog::SliceResult TextureSlicerToolDialog::buildResult() {
    SliceResult result;
    result.rects = m_previewRects;
    result.submeshId = m_selectedSubmesh;
    return result;
}

void TextureSlicerToolDialog::show() {
    if (!m_isOpen) return;

    const bool isTileset = (m_mode == Mode::TILESET);
    const char* popupTitle = isTileset
        ? ICON_FA_TABLE_CELLS " Tileset Slicer Tool##TextureSlicerToolModal"
        : ICON_FA_GRIP " Sprite Slicer Tool##TextureSlicerToolModal";
    const char* rectLabel = isTileset ? "tile rects" : "frames";
    const char* namingLabel = isTileset ? "Rect Naming" : "Frame Naming";
    const char* detailsLabel = isTileset ? "Rect Details" : "Frame Details";

    ImGui::OpenPopup(popupTitle);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(isTileset ? 540.0f : 520.0f, 0), ImGuiCond_Once);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal;

    if (!ImGui::BeginPopupModal(popupTitle, &m_isOpen, flags)) {
        if (m_isOpen) {
            close();
            if (m_onCancel) m_onCancel();
        }
        return;
    }

    auto beginInputTable = [](const char* id, float firstColumnWidth = 130.0f) {
        if (ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, firstColumnWidth);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            return true;
        }
        return false;
    };

    auto beginInputRow = [](const char* label, float inputWidth = 0.0f) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(inputWidth > 0.0f ? inputWidth : 8.0f * ImGui::GetFontSize());
    };

    ImGui::Separator();

    if (m_sheetWidth > 0 && m_sheetHeight > 0) {
        ImGui::Text("%s size: %d x %d", isTileset ? "Texture" : "Sheet", m_sheetWidth, m_sheetHeight);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
            isTileset ? "Warning: Texture dimensions unknown. Assign a texture first."
                      : "Warning: Sheet dimensions unknown. Set Width/Height in sprite properties first.");
    }

    ImGui::Spacing();

    // --- Submesh selector (tileset only) ---
    if (isTileset && m_submeshes.size() > 1) {
        if (beginInputTable("SlicerSubmeshTable")) {
            beginInputRow("Submesh", 14.0f * ImGui::GetFontSize());
            const char* currentName = (m_selectedSubmesh >= 0 && m_selectedSubmesh < (int)m_submeshes.size())
                ? m_submeshes[m_selectedSubmesh].name.c_str() : "None";
            if (ImGui::BeginCombo("##Submesh", currentName)) {
                for (int i = 0; i < (int)m_submeshes.size(); i++) {
                    bool isSelected = (m_selectedSubmesh == i);
                    if (ImGui::Selectable(m_submeshes[i].name.c_str(), isSelected)) {
                        m_selectedSubmesh = i;
                        m_previewTexture = m_submeshes[i].texture;

                        if (!m_previewTexture.empty()) {
                            int textureWidth = static_cast<int>(m_previewTexture.getWidth());
                            int textureHeight = static_cast<int>(m_previewTexture.getHeight());
                            if (textureWidth <= 0 || textureHeight <= 0) {
                                m_previewTexture.load();
                                textureWidth = static_cast<int>(m_previewTexture.getWidth());
                                textureHeight = static_cast<int>(m_previewTexture.getHeight());
                            }
                            if (textureWidth > 0) m_sheetWidth = textureWidth;
                            if (textureHeight > 0) m_sheetHeight = textureHeight;
                        }
                        m_alphaDataReady = false;
                        m_previewDirty = true;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
    }

    // --- Slice Mode ---
    if (beginInputTable("SlicerSliceModeTable")) {
        beginInputRow("Slice Mode", 14.0f * ImGui::GetFontSize());
        if (ImGui::RadioButton("Grid (Columns x Rows)##SliceModeGrid", m_sliceMode == SliceMode::GRID)) {
            m_sliceMode = SliceMode::GRID;
            m_previewDirty = true;
        }
        if (ImGui::RadioButton("Cell Size (W x H)##SliceModeCell", m_sliceMode == SliceMode::CELL_SIZE)) {
            m_sliceMode = SliceMode::CELL_SIZE;
            m_previewDirty = true;
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_sliceMode == SliceMode::GRID) {
        ImGui::SeparatorText("Grid");
        if (beginInputTable("SlicerGridTable")) {
            beginInputRow("Columns", 6.0f * ImGui::GetFontSize());
            if (UIUtils::dragInt("##Columns", &m_columns, 0.1f, 1, 64)) {
                m_previewDirty = true;
            }
            beginInputRow("Rows", 6.0f * ImGui::GetFontSize());
            if (UIUtils::dragInt("##Rows", &m_rows, 0.1f, 1, 64)) {
                m_previewDirty = true;
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::SeparatorText("Cell Size");
        if (beginInputTable("SlicerCellSizeTable")) {
            beginInputRow("Cell Width", 6.0f * ImGui::GetFontSize());
            if (UIUtils::dragInt("##CellWidth", &m_cellWidth, 1.0f, 1, m_sheetWidth > 0 ? m_sheetWidth : 4096)) {
                m_previewDirty = true;
            }
            beginInputRow("Cell Height", 6.0f * ImGui::GetFontSize());
            if (UIUtils::dragInt("##CellHeight", &m_cellHeight, 1.0f, 1, m_sheetHeight > 0 ? m_sheetHeight : 4096)) {
                m_previewDirty = true;
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Offset & Padding");
    if (beginInputTable("SlicerOffsetPaddingTable")) {
        beginInputRow("Offset X", 6.0f * ImGui::GetFontSize());
        if (UIUtils::dragInt("##OffsetX", &m_offsetX, 1.0f, 0, INT_MAX)) {
            m_previewDirty = true;
        }
        beginInputRow("Offset Y", 6.0f * ImGui::GetFontSize());
        if (UIUtils::dragInt("##OffsetY", &m_offsetY, 1.0f, 0, INT_MAX)) {
            m_previewDirty = true;
        }
        beginInputRow("Padding X", 6.0f * ImGui::GetFontSize());
        if (UIUtils::dragInt("##PaddingX", &m_paddingX, 1.0f, 0, INT_MAX)) {
            m_previewDirty = true;
        }
        beginInputRow("Padding Y", 6.0f * ImGui::GetFontSize());
        if (UIUtils::dragInt("##PaddingY", &m_paddingY, 1.0f, 0, INT_MAX)) {
            m_previewDirty = true;
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText(namingLabel);
    if (beginInputTable("SlicerNamingTable")) {
        beginInputRow("Prefix", 10.0f * ImGui::GetFontSize());
        if (ImGui::InputText("##Prefix", m_prefixBuffer, sizeof(m_prefixBuffer))) {
            m_previewDirty = true;
        }
        beginInputRow("Skip empty cells");
        if (ImGui::Checkbox("##SkipEmptyCells", &m_skipEmptyCells)) {
            m_previewDirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Omit cells whose pixels are completely transparent.");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Generate preview
    if (m_previewDirty) {
        generatePreview();
    }

    // --- Visual preview ---
    if (m_skippedEmptyCount > 0) {
        ImGui::Text("Preview: %d %s (%d empty skipped)", (int)m_previewRects.size(), rectLabel, m_skippedEmptyCount);
    } else {
        ImGui::Text("Preview: %d %s", (int)m_previewRects.size(), rectLabel);
    }

    if (m_sheetWidth > 0 && m_sheetHeight > 0) {
        auto drawPreview = [this, isTileset](const ImVec2& canvasPos, const ImVec2& canvasSize, ImDrawList* drawList) {
            const float scale = std::min(canvasSize.x / static_cast<float>(m_sheetWidth),
                                         canvasSize.y / static_cast<float>(m_sheetHeight));
            const float previewW = m_sheetWidth * scale;
            const float previewH = m_sheetHeight * scale;
            const float offsetX = (canvasSize.x - previewW) * 0.5f;
            const float offsetY = (canvasSize.y - previewH) * 0.5f;

            ImVec2 imageMin(canvasPos.x + offsetX, canvasPos.y + offsetY);
            ImVec2 imageMax(imageMin.x + previewW, imageMin.y + previewH);

            drawList->AddRectFilled(canvasPos,
                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                IM_COL32(40, 40, 40, 255));

            if (!m_previewTexture.empty() && m_previewTexture.getRender()) {
                Widgets::addImage(drawList, Backend::getImGuiTexture(m_previewTexture.getRender()), imageMin, imageMax);
            }

            drawList->AddRect(imageMin, imageMax, IM_COL32(100, 100, 100, 255));

            ImU32 borderColor = isTileset ? IM_COL32(80, 180, 220, 220) : IM_COL32(100, 200, 100, 220);
            for (size_t i = 0; i < m_previewRects.size(); i++) {
                const SlicedRect& slicedRect = m_previewRects[i];
                float rx = imageMin.x + slicedRect.rect.getX() * scale;
                float ry = imageMin.y + slicedRect.rect.getY() * scale;
                float rw = slicedRect.rect.getWidth() * scale;
                float rh = slicedRect.rect.getHeight() * scale;

                drawList->AddRect(ImVec2(rx, ry), ImVec2(rx + rw, ry + rh), borderColor);

                if (rw > Theme::dpi(16.0f) && rh > Theme::dpi(12.0f)) {
                    char indexStr[8];
                    snprintf(indexStr, sizeof(indexStr), "%d", (int)i);
                    ImVec2 textSize = ImGui::CalcTextSize(indexStr);
                    float textX = rx + (rw - textSize.x) * 0.5f;
                    float textY = ry + (rh - textSize.y) * 0.5f;
                    drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 200), indexStr);
                }
            }

            drawList->AddRect(canvasPos,
                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                IM_COL32(80, 80, 80, 255));
        };

        const ImVec2 compactCanvasSize(Theme::dpi(100.0f), Theme::dpi(100.0f));
        ImVec2 compactCanvasPos = ImGui::GetCursorScreenPos();
        drawPreview(compactCanvasPos, compactCanvasSize, ImGui::GetWindowDrawList());
        ImGui::Dummy(compactCanvasSize);

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%d x %d", m_sheetWidth, m_sheetHeight);
            ImVec2 tooltipCanvasPos = ImGui::GetCursorScreenPos();
            ImVec2 tooltipCanvasSize(static_cast<float>(m_sheetWidth), static_cast<float>(m_sheetHeight));
            drawPreview(tooltipCanvasPos, tooltipCanvasSize, ImGui::GetWindowDrawList());
            ImGui::Dummy(tooltipCanvasSize);
            ImGui::EndTooltip();
        }
    }

    // Rect/frame list summary
    if (!m_previewRects.empty() && ImGui::TreeNode(detailsLabel)) {
        ImGui::BeginChild("RectList", ImVec2(0, Theme::dpi(isTileset ? 120.0f : 150.0f)), true);
        for (size_t i = 0; i < m_previewRects.size(); i++) {
            const SlicedRect& slicedRect = m_previewRects[i];
            ImGui::Text("[%d] %s: (%.0f, %.0f, %.0f, %.0f)",
                (int)i, slicedRect.name.c_str(),
                slicedRect.rect.getX(), slicedRect.rect.getY(),
                slicedRect.rect.getWidth(), slicedRect.rect.getHeight());
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Buttons
    float windowWidth = ImGui::GetWindowSize().x;
    float buttonsWidth = 260;
    ImGui::SetCursorPosX((windowWidth - buttonsWidth) * 0.5f);

    ImGui::BeginDisabled(m_previewRects.empty());
    if (ImGui::Button("Apply", ImVec2(120, 0))) {
        if (m_onApply) {
            m_onApply(buildResult());
        }
        close();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        close();
        if (m_onCancel) m_onCancel();
        ImGui::CloseCurrentPopup();
    }

    if (!m_isOpen) {
        releaseAlphaData();
    }

    ImGui::EndPopup();
}

} // namespace editor
} // namespace doriax
