//
// (c) 2026 Eduardo Doria.
//

#ifndef LIGHT_COMPONENT_H
#define LIGHT_COMPONENT_H

#include "math/Vector3.h"
#include "component/CameraComponent.h"
#include "component/Transform.h"
#include "render/FramebufferRender.h"
#include "Engine.h"

namespace doriax{

    enum class LightType{
        DIRECTIONAL,
        POINT,
        SPOT
    };

    struct LightCamera{
        CameraRender render;
        Matrix4 lightViewMatrix;
        Matrix4 lightProjectionMatrix;
        Matrix4 lightViewProjectionMatrix;
        Plane frustumPlanes[6];
        // for point light all cameras are same calculated value that is used by distanceToDepthValue in shader
        Vector2 nearFar = Vector2(0.0, 0.0);
    };

    struct DORIAX_API LightComponent{
        LightType type = LightType::DIRECTIONAL;

        Vector3 direction;
        Vector3 worldDirection;

        Vector3 color = Vector3(1.0, 1.0, 1.0);

        float range = 0.0;
        float intensity = 1.0;

        float innerConeCos = 0.766044438f; // cos(Angle::defaultToRad(80 / 2));
        float outerConeCos = 0.642787635f; // cos(Angle::defaultToRad(100 / 2));

        bool shadows = false;
        bool automaticShadowCamera = true;
        float shadowBias = 0.002f;
        float shadowNormalBias = 2.0f;
        unsigned int mapResolution = 2048;
        Vector2 shadowCameraNearFar = Vector2(0.1f, 10.0f); // when automatic it gets value from scene camera or light range
        unsigned int numShadowCascades = 3;

        LightCamera cameras[6];
        FramebufferRender framebuffer[MAX_SHADOWCASCADES];
        int shadowMapIndex;

        bool needUpdateShadowCamera = false;
        bool needUpdateShadowMap = false; // framebuffers
    };
    
}

#endif //LIGHT_COMPONENT_H
