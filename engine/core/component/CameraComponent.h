//
// (c) 2026 Eduardo Doria.
//

#ifndef CAMERA_COMPONENT_H
#define CAMERA_COMPONENT_H

#include "math/Vector3.h"
#include "math/Matrix4.h"
#include "math/Plane.h"
#include "texture/Framebuffer.h"
#include "render/CameraRender.h"

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

        bool transparentSort = true;

        bool useTarget = true;
        bool autoResize = true;

        bool needUpdate = true;
    };

}

#endif //CAMERA_COMPONENT_H