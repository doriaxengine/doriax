#ifndef ROTATEGIZMO_H
#define ROTATEGIZMO_H

#include "object/Camera.h"
#include "object/Object.h"
#include "object/Shape.h"
#include "object/ui/Image.h"
#include "object/Lines.h"

#include "render/RenderUtil.h"

namespace doriax::editor{

    class RotateGizmo: public Object{
    private:
        bool use2DGizmo;

        Shape* maincircle;
        Shape* xcircle;
        Shape* ycircle;
        Shape* zcircle;

        Lines* line;

        std::vector<AABB> xcircleAABBs;
        std::vector<AABB> ycircleAABBs;
        std::vector<AABB> zcircleAABBs;

        static const Vector3 mainColor;
        static const Vector3 xaxisColor;
        static const Vector3 yaxisColor;
        static const Vector3 zaxisColor;
        static const Vector3 lineColor;
        static const Vector3 mainColorHightlight;
        static const Vector3 xaxisColorHightlight;
        static const Vector3 yaxisColorHightlight;
        static const Vector3 zaxisColorHightlight;
        static const float circleAlpha;

        std::vector<AABB> createHalfTorus(Entity entity, float radius, float ringRadius, unsigned int sides, unsigned int rings);

    public:
        RotateGizmo(Scene* scene, bool use2DGizmo);
        virtual ~RotateGizmo();

        void updateRotations(Camera* camera);
        void drawLine(Vector3 point);
        void removeLine();

        GizmoSideSelected checkHover(const Ray& ray);
    };

}

#endif /* ROTATEGIZMO_H */