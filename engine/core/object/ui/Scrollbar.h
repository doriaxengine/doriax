//
// (c) 2026 Eduardo Doria.
//

#ifndef SCROLLBAR_H
#define SCROLLBAR_H

#include "Image.h"
#include "Text.h"
#include "Container.h"

namespace doriax{
    class DORIAX_API Scrollbar: public Image{

    public:
        Scrollbar(Scene* scene);
        Scrollbar(Scene* scene, Entity entity);
        virtual ~Scrollbar();

        Image getBarObject() const;

        void setType(ScrollbarType type);
        ScrollbarType getType() const;

        void setBarSize(float size);
        float getBarSize() const;

        void setStep(float step);
        float getStep() const;

        void setBarTexture(const std::string& path);
        void setBarTexture(Framebuffer* framebuffer);

        void setBarColor(Vector4 color);
        void setBarColor(const float red, const float green, const float blue, const float alpha);
        void setBarColor(const float red, const float green, const float blue);
        void setBarAlpha(const float alpha);
        Vector4 getBarColor() const;
        float getBarAlpha() const;

        void setBarPatchMargin(int margin);
        void setBarPatchMargin(int marginLeft, int marginRight, int marginTop, int marginBottom);
    };
}

#endif //SCROLLBAR_H