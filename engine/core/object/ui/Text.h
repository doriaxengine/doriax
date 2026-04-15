//
// (c) 2026 Eduardo Doria.
//

#ifndef TEXT_H
#define TEXT_H

#include "UILayout.h"

namespace doriax{
    class DORIAX_API STBText;

    class Text: public UILayout{

    public:
        Text(Scene* scene);
        Text(Scene* scene, Entity entity);
        virtual ~Text();

        bool createText();
        bool load();

        void setFixedSize(bool fixedSize);

        void setFixedWidth(bool fixedWidth);
        bool isFixedWidth() const;

        void setFixedHeight(bool fixedHeight);
        bool isFixedHeight() const;

        void setMaxTextSize(unsigned int maxTextSize);
        unsigned int getMaxTextSize() const;

        void setText(const std::string& text);
        std::string getText() const;

        void setFont(const std::string& font);
        std::string getFont() const;

        void setFontSize(unsigned int fontSize);
        unsigned int getFontSize() const;

        void setMultiline(bool multiline);
        bool getMultiline() const;

        void setColor(Vector4 color);
        void setColor(const float red, const float green, const float blue, const float alpha);
        void setColor(const float red, const float green, const float blue);
        void setAlpha(const float alpha);
        Vector4 getColor() const;
        float getAlpha() const;

        float getAscent() const;
        float getDescent() const;
        float getLineGap() const;
        int getLineHeight() const;
        unsigned int getNumChars() const;
        Vector2 getCharPosition(unsigned int index) const;
        float getCharWidth(char c) const;

        void setFlipY(bool flipY);
        bool isFlipY() const;

        void setPivotBaseline(bool pivotBaseline);
        bool isPivotBaseline() const;

        void setPivotCentered(bool pivotCentered);
        bool isPivotCentered() const;

        AABB getAABB() const;
        AABB getWorldAABB() const;
    };
}

#endif //TEXT_H