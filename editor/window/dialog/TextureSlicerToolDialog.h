#pragma once

#include <string>
#include <vector>
#include <functional>
#include "imgui.h"
#include "math/Rect.h"
#include "math/Vector2.h"
#include "Engine.h"
#include "texture/Texture.h"

namespace doriax {
namespace editor {

class TextureSlicerToolDialog {
public:
    enum class Mode {
        SPRITE,
        TILESET
    };

    enum class SliceMode {
        GRID,       // by columns x rows
        CELL_SIZE   // by cell width x height
    };

    struct SlicedRect {
        std::string name;
        Rect rect;
    };

    struct SubmeshInfo {
        std::string name;
        Texture texture;
    };

    struct SliceResult {
        std::vector<SlicedRect> rects;
        int submeshId = 0;
    };

private:
    bool m_isOpen = false;
    Mode m_mode = Mode::SPRITE;

    SliceMode m_sliceMode = SliceMode::GRID;

    int m_columns = 4;
    int m_rows = 4;
    int m_cellWidth = 32;
    int m_cellHeight = 32;
    int m_offsetX = 0;
    int m_offsetY = 0;
    int m_paddingX = 0;
    int m_paddingY = 0;

    int m_sheetWidth = 0;
    int m_sheetHeight = 0;

    Texture m_previewTexture;

    std::vector<SubmeshInfo> m_submeshes;
    int m_selectedSubmesh = 0;

    char m_prefixBuffer[64] = "frame_";

    bool m_previewDirty = true;
    std::vector<SlicedRect> m_previewRects;

    std::function<void(const SliceResult&)> m_onApply;
    std::function<void()> m_onCancel;

    void generatePreview();
    SliceResult buildResult();

public:
    TextureSlicerToolDialog() = default;
    ~TextureSlicerToolDialog() = default;

    void open(const Texture& previewTexture,
              int sheetWidth, int sheetHeight,
              std::function<void(const SliceResult&)> onApply,
              std::function<void()> onCancel = nullptr);

    void openTileset(const std::vector<SubmeshInfo>& submeshes,
                     int sheetWidth, int sheetHeight,
                     std::function<void(const SliceResult&)> onApply,
                     std::function<void()> onCancel = nullptr);

    void show();
    bool isOpen() const { return m_isOpen; }
    void close() { m_isOpen = false; }
};

} // namespace editor
} // namespace doriax
