//
// (c) 2026 Eduardo Doria.
//

#ifndef PANEL_H
#define PANEL_H

#include "Image.h"
#include "Text.h"
#include "Container.h"

namespace doriax{
    class DORIAX_API Panel: public Image{

    public:
        Panel(Scene* scene);
        Panel(Scene* scene, Entity entity);
        virtual ~Panel();

        Image getHeaderImageObject() const;
        Container getHeaderContainerObject() const;
        Text getHeaderTextObject() const;

        void setTitle(const std::string& text);
        std::string getTitle() const;

        void setTitleAnchorPreset(AnchorPreset titleAnchorPreset);
        AnchorPreset getTitleAnchorPreset() const;

        void setTitleColor(Vector4 color);
        void setTitleColor(const float red, const float green, const float blue, const float alpha);
        Vector4 getTitleColor() const;

        void setTitleFont(const std::string& font);
        std::string getTitleFont() const;

        void setTitleFontSize(unsigned int fontSize);
        unsigned int getTitleFontSize() const;

        void setHeaderColor(Vector4 color);
        void setHeaderColor(const float red, const float green, const float blue, const float alpha);
        Vector4 getHeaderColor() const;

        void setHeaderPatchMargin(int margin);
        void setHeaderTexture(const std::string& path);

        void setHeaderMargin(int left, int top, int right, int bottom);

        void setHeaderMarginLeft(int left);
        int getHeaderMarginLeft() const;

        void setHeaderMarginTop(int top);
        int getHeaderMarginTop() const;

        void setHeaderMarginRight(int right);
        int getHeaderMarginRight() const;

        void setHeaderMarginBottom(int bottom);
        int getHeaderMarginBottom() const;

        void setMinSize(int minWidth, int minHeight);
        void setMinWidth(int minWidth);
        void setMinHeight(int minHeight);

        int getMinWidth() const;
        int getMinHeight() const;

        void setResizeMargin(int resizeMargin);
        int getResizeMargin() const;

        void setWindowProperties(const bool canMove, const bool canResize, const bool canBringToFront);

        void setCanMove(const bool canMove);
        bool isCanMove() const;

        void setCanResize(const bool canResize);
        bool isCanResize() const;

        void setCanBringToFront(const bool canBringToFront);
        bool isCanBringToFront() const;
    };
}

#endif //PANEL_H