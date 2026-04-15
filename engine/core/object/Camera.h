//
// (c) 2026 Eduardo Doria.
//

#ifndef CAMERA_H
#define CAMERA_H

#include "Object.h"
#include "component/CameraComponent.h"
#include "math/Ray.h"

namespace doriax{

    class DORIAX_API Camera: public Object{

    private:
        void applyOrthoDefaults(CameraComponent& cameraComponent);
        void applyPerspectiveDefaults(CameraComponent& cameraComponent);

    public:
        Camera(Scene* scene);
        Camera(Scene* scene, Entity entity);
        virtual ~Camera();

        void activate();

        void setOrtho(float left, float right, float bottom, float top, float nearValue, float farValue);
        void setPerspective(float yfov, float aspect, float nearValue, float farValue);

        void setAutoResize(bool autoResize);
        bool isAutoResize() const;

        void setNearClip(float nearValue);
        float getNearClip() const;

        void setFarClip(float farValue);
        float getFarClip() const;

        void setLeftClip(float left);
        float getLeftClip() const;

        void setRightClip(float right);
        float getRightClip() const;

        void setBottomClip(float bottom);
        float getBottomClip() const;

        void setTopClip(float top);
        float getTopClip() const;

        void setAspect(float aspect);
        float getAspect() const;

        void setYFov(float yfov);
        float getYFov() const;

        void setType(CameraType type);
        CameraType getType() const;

        void setTarget(Vector3 target);
        void setTarget(const float x, const float y, const float z);
        Vector3 getTarget() const;

        void disableTarget();
        bool isUsingTarget() const;

        void setUp(Vector3 up);
        void setUp(const float x, const float y, const float z);
        Vector3 getUp() const;

        Vector3 getDirection() const;
        Vector3 getRight() const;

        Vector3 getWorldTarget() const;

        Vector3 getWorldDirection() const;
        Vector3 getWorldUp() const;
        Vector3 getWorldRight() const;

        Matrix4 getViewMatrix() const;
        Matrix4 getProjectionMatrix() const;
        Matrix4 getViewProjectionMatrix() const;

        void rotateView(float angle);
        void rotatePosition(float angle);

        void elevateView(float angle);
        void elevatePosition(float angle);

        void walkForward(float distance);
        void zoom(float distance);

        void slide(float distance);
        void slideForward(float distance);
        void slideUp(float distance);

        void setRenderToTexture(bool renderToTexture);
        bool isRenderToTexture() const;

        Framebuffer* getFramebuffer();
        void setFramebufferSize(int width, int height);
        void setFramebufferFilter(TextureFilter filter);

        void setTransparentSort(bool transparentSort);
        bool isTransparentSort() const;

        Ray screenToRay(float x, float y);

        float getDistanceFromTarget() const;

        void updateCamera();
    };

}

#endif //CAMERA_H