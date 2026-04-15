//
// (c) 2026 Eduardo Doria.
//

#ifndef UILAYOUT_H
#define UILAYOUT_H

#include "Object.h"

namespace doriax{
    class DORIAX_API UILayout: public Object{

    public:
        UILayout(Scene* scene);
        UILayout(Scene* scene, Entity entity);
        virtual ~UILayout();

        void setSize(unsigned int width, unsigned int height);
        void setWidth(unsigned int width);
        void setHeight(unsigned int height);

        unsigned int getWidth() const;
        unsigned int getHeight() const;

        void setAnchorPoints(float left, float top, float right, float bottom);
        void setAnchorPointLeft(float left);
        void setAnchorPointTop(float top);
        void setAnchorPointRight(float right);
        void setAnchorPointBottom(float bottom);

        float getAnchorPointLeft() const;
        float getAnchorPointTop() const;
        float getAnchorPointRight() const;
        float getAnchorPointBottom() const;

        void setAnchorOffsets(int left, int top, int right, int bottom);
        void setAnchorOffsetLeft(int left);
        void setAnchorOffsetTop(int top);
        void setAnchorOffsetRight(int right);
        void setAnchorOffsetBottom(int bottom);

        int getAnchorOffsetLeft() const;
        int getAnchorOffsetTop() const;
        int getAnchorOffsetRight() const;
        int getAnchorOffsetBottom() const;

        void setPositionXOffset(float xOffset);
        float getPositionXOffset() const;
        void setPositionYOffset(float yOffset);
        float getPositionYOffset() const;
        void setPositionOffset(Vector2 positionOffset);
        Vector2 getPositionOffset() const;

        void setAnchorPreset(AnchorPreset anchorPreset);
        AnchorPreset getAnchorPreset() const;

        void setUsingAnchors(bool usingAnchors);
        bool isUsingAnchors() const;

        void setIgnoreScissor(bool ignoreScissor);
        bool isIgnoreScissor() const;
    };
}

#endif //UILAYOUT_H