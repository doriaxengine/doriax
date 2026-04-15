#pragma once


#include "object/Object.h"
#include "object/ui/Polygon.h"
#include "object/Lines.h"

#include "render/RenderUtil.h"

namespace doriax::editor{

    class Object2DGizmo: public Object{
    private:
        Object* center;
        Polygon* rects[8];
        Lines* lines;

        float width;
        float height;
        bool showRects;

        static const float rectSize;
        static const float sizeOffset;

        void updateRects();
        void updateLines();

    public:
        Object2DGizmo(Scene* scene);
        virtual ~Object2DGizmo();

        void setCenter(Vector3 point);
        void setSize(float width, float height);

        void setShowRects(bool showRects);

        Gizmo2DSideSelected checkHover(const Ray& ray, const OBB& obb);
    };

}