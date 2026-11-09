// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef CAMERA_H
#define CAMERA_H

#include "Object.h"
#include "component/CameraComponent.h"
#include "math/Ray.h"

namespace doriax{

    class DORIAX_API Camera: public Object{

    private:
        static constexpr float ELEVATION_DOT_LIMIT = 0.9998477f; // sin(89 degrees)

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

        // Target mode is ON by default and setTarget() turns it back on. While it is
        // on the view matrix is lookAt(worldPosition, worldTarget, worldUp) and the
        // entity's Transform rotation is ignored, so setRotation() followed by
        // setTarget() leaves the rotation with no effect on the view.
        void setTarget(Vector3 target);
        void setTarget(const float x, const float y, const float z);
        Vector3 getTarget() const;

        // Orient the camera from its Transform rotation instead of a target point:
        // forward becomes rotation * Vector3(0, 0, -1).
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

        void setDepthTest(bool depthTest);
        bool isDepthTest() const;

        Ray screenToRay(float x, float y);

        float getDistanceFromTarget() const;

        // runtime view shader parameters, as Scene::setViewShaderParameter
        void setShaderParameter(const std::string& collection, const std::string& name, const Vector4& value);
        void setShaderParameter(const std::string& collection, const std::string& name, const Vector3& value);
        void setShaderParameter(const std::string& collection, const std::string& name, const Vector2& value);
        void setShaderParameter(const std::string& collection, const std::string& name, float value);
        Vector4 getShaderParameter(const std::string& collection, const std::string& name) const;
        bool clearShaderParameter(const std::string& collection, const std::string& name);

        void updateCamera();
    };

}

#endif //CAMERA_H
