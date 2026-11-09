// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef CAMERA_COMPONENT_H
#define CAMERA_COMPONENT_H

#include "math/Vector3.h"
#include "math/Matrix4.h"
#include "math/Plane.h"
#include "texture/Framebuffer.h"
#include "render/CameraRender.h"
#include "shader/ShaderUniforms.h"

#define DEFAULT_ORTHO_NEAR          -10
#define DEFAULT_ORTHO_FAR           10
#define DEFAULT_PERSPECTIVE_NEAR    0.2
#define DEFAULT_PERSPECTIVE_FAR     200

namespace doriax{

    enum class CameraType{
        CAMERA_UI,
        CAMERA_ORTHO,
        CAMERA_PERSPECTIVE
    };

    enum FrustumPlane{
        FRUSTUM_PLANE_NEAR   = 0,
        FRUSTUM_PLANE_FAR    = 1,
        FRUSTUM_PLANE_LEFT   = 2,
        FRUSTUM_PLANE_RIGHT  = 3,
        FRUSTUM_PLANE_TOP    = 4,
        FRUSTUM_PLANE_BOTTOM = 5
    };

    struct DORIAX_API CameraComponent{

        CameraType type = CameraType::CAMERA_UI;
        
        Matrix4 projectionMatrix;
        Matrix4 viewMatrix;
        Matrix4 viewProjectionMatrix;

        Vector3 target = Vector3(0, 0, 0);
        Vector3 worldTarget;

        Vector3 up = Vector3(0, 1, 0);
        Vector3 direction = Vector3(0, 0, 1);
        Vector3 right = Vector3(1, 0, 0);

        Vector3 worldUp;
        Vector3 worldDirection;
        Vector3 worldRight;

        float leftClip = 0;
        float rightClip = 10;
        float bottomClip = 0;
        float topClip = 10;

        float yfov = 0.75;
        float aspect = 1.0;

        float nearClip = DEFAULT_ORTHO_NEAR;
        float farClip = DEFAULT_ORTHO_FAR;

        Plane frustumPlanes[6];

        CameraRender render;

        bool renderToTexture = false;
        // need to be a pointer to not lost reference when component changes position
        Framebuffer* framebuffer = new Framebuffer();

        // authored framebuffer settings (source of truth, serialized/editable);
        // RenderSystem syncs these into the Framebuffer above and recreates it when
        // they change. Defaults must match the Framebuffer() constructor defaults.
        unsigned int framebufferWidth = 512;
        unsigned int framebufferHeight = 512;
        TextureFilter framebufferFilter = TextureFilter::LINEAR;

        // draw transparent renderables back to front (needs depthTest)
        bool transparentSort = true;

        // false makes the camera ignore the depth buffer and composite in submission
        // order, like a 2D canvas. Instance and point batches follow the main camera
        bool depthTest = true;

        // when true (the default, and re-enabled by Camera::setTarget) updateCamera
        // builds the view from lookAt(position, target, up) and ignores the Transform
        // rotation; when false the rotation drives it (forward = rotation * -Z)
        bool useTarget = true;
        bool autoResize = true;

        // engine-driven override (planar reflection): when set, updateCamera uses
        // customViewMatrix instead of position/target, and invertCulling flips
        // triangle winding for the reflected (handedness-mirrored) pass
        bool hasCustomViewMatrix = false;
        Matrix4 customViewMatrix;
        bool invertCulling = false;

        // saved view shader parameters by "Collection.name"
        ShaderUniformValues shaderParameters;

        bool needUpdate = true;
    };

}

#endif //CAMERA_COMPONENT_H