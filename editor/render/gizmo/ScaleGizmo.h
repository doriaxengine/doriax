#ifndef SCALEGIZMO_H
#define SCALEGIZMO_H

#include "object/Camera.h"
#include "object/Object.h"
#include "object/Shape.h"
#include "object/ui/Image.h"

#include "render/RenderUtil.h"

namespace doriax::editor{

    class ScaleGizmo: public Object{
    private:
        bool use2DGizmo;

        Shape* centerbox;
        Shape* xaxis;
        Shape* yaxis;
        Shape* zaxis;
        Shape* xbox;
        Shape* ybox;
        Shape* zbox;
        Shape* xyrect;
        Shape* xzrect;
        Shape* yzrect;

        static const Vector3 centerColor;
        static const Vector3 xaxisColor;
        static const Vector3 yaxisColor;
        static const Vector3 zaxisColor;
        static const Vector3 centerColorHightlight;
        static const Vector3 xaxisColorHightlight;
        static const Vector3 yaxisColorHightlight;
        static const Vector3 zaxisColorHightlight;
        static const float rectAlpha;

    public:
        ScaleGizmo(Scene* scene, bool use2DGizmo);
        virtual ~ScaleGizmo();

        GizmoSideSelected checkHover(const Ray& ray);
    };

}

#endif /* SCALEGIZMO_H */