#pragma once

#include "object/Object.h"
#include "object/ui/Polygon.h"
#include "math/Rect.h"

namespace doriax::editor{

    class AnchorGizmo: public Object{
    private:
        Object* center;
        Polygon* points[4];

        Rect area;

        float anchorLeft;
        float anchorTop;
        float anchorRight;
        float anchorBottom;

        void updateVisual();

    public:
        AnchorGizmo(Scene* scene);
        virtual ~AnchorGizmo();

        void setArea(Rect area);
        void setAnchors(float left, float top, float right, float bottom);
    };

}
