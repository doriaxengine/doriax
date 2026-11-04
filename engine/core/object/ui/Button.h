// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef BUTTON_H
#define BUTTON_H

#include "Image.h"
#include "Text.h"

namespace doriax{
    class DORIAX_API Button: public Image{
    private:
        void ensureLabel();

    public:
        Button(Scene* scene);
        Button(Scene* scene, Entity entity);
        virtual ~Button();

        bool hasLabel() const;
        Text getLabelObject() const;

        void setLabel(const std::string& text);
        std::string getLabel() const;

        void setLabelColor(Vector4 color);
        void setLabelColor(const float red, const float green, const float blue, const float alpha);
        Vector4 getLabelColor() const;

        void setLabelFont(const std::string& font);
        std::string getLabelFont() const;

        void setLabelFontSize(unsigned int fontSize);
        unsigned int getLabelFontSize() const;

        void setTextureNormal(const std::string& path);
        void setTextureHovered(const std::string& path);
        void setTexturePressed(const std::string& path);
        void setTextureDisabled(const std::string& path);

        void setColorNormal(Vector4 color);
        void setColorNormal(const float red, const float green, const float blue, const float alpha);
        void setColorNormal(const float red, const float green, const float blue);
        Vector4 getColorNormal() const;

        void setColorHovered(Vector4 color);
        void setColorHovered(const float red, const float green, const float blue, const float alpha);
        void setColorHovered(const float red, const float green, const float blue);
        Vector4 getColorHovered() const;

        void setColorPressed(Vector4 color);
        void setColorPressed(const float red, const float green, const float blue, const float alpha);
        void setColorPressed(const float red, const float green, const float blue);
        Vector4 getColorPressed() const;

        void setColorDisabled(Vector4 color);
        void setColorDisabled(const float red, const float green, const float blue, const float alpha);
        void setColorDisabled(const float red, const float green, const float blue);
        Vector4 getColorDisabled() const;

        void setScaleHovered(float scaleHovered);
        float getScaleHovered() const;

        void setScalePressed(float scalePressed);
        float getScalePressed() const;

        void setTransitionTime(float transitionTime);
        float getTransitionTime() const;

        void setDisabled(bool disabled);
        bool getDisabled() const;

        void setFocusable(bool focusable);
        bool isFocusable() const;

        void setFocus();
        bool isFocused() const;
    };
}

#endif //BUTTON_H