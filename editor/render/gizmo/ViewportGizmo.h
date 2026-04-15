#ifndef VIEWPORTGIZMO_H
#define VIEWPORTGIZMO_H

#include "Scene.h"
#include "object/Camera.h"
#include "object/Object.h"
#include "object/Shape.h"
#include "math/Vector3.h"

namespace doriax::editor{

    enum class ViewportGizmoAxis {
        NONE,
        POSITIVE_X,
        NEGATIVE_X,
        POSITIVE_Y,
        NEGATIVE_Y,
        POSITIVE_Z,
        NEGATIVE_Z
    };

    class ViewportGizmo{
    private:
        Scene* scene;
        Camera* camera;

        Object* mainObject;

        Shape* cube;
        Shape* xaxis;
        Shape* yaxis;
        Shape* zaxis;
        Shape* xarrow;
        Shape* yarrow;
        Shape* zarrow;
    public:
        ViewportGizmo();
        virtual ~ViewportGizmo();

        void applyRotation(Camera* sceneCam);

        ViewportGizmoAxis hitTest(float normalizedX, float normalizedY) const;

        Framebuffer* getFramebuffer();
        TextureRender& getTexture();
        Scene* getScene();
        Object* getObject();
    };

}

#endif /* VIEWPORTGIZMO_H */