// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef LIGHT_COMPONENT_H
#define LIGHT_COMPONENT_H

#include "math/Vector3.h"
#include "component/CameraComponent.h"
#include "component/Transform.h"
#include "render/FramebufferRender.h"
#include "texture/Texture.h"
#include "Engine.h"
#include <cmath>

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
        // directional cascade: view depth past nearFar.y where the next cascade fades in
        float splitBlend = 0.0f;
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

        // Optional projected grayscale/alpha mask. An empty mask keeps the default
        // circular angular attenuation; a loaded mask defines the complete spot shape.
        Texture spotMask;

        // Runtime projection state derived from spotMask and the entity transform.
        // These values are intentionally not serialized.
        Vector3 worldUp;
        float spotMaskAspect = 1.0f;
        bool spotMaskReady = false;

        Vector3 getLocalSpotProjectionUp() const {
            Vector3 localDirection = direction.normalized();
            if (localDirection == Vector3::ZERO){
                return Vector3(0, 1, 0);
            }

            Vector3 referenceUp = Vector3(0, 1, 0);
            if (std::abs(localDirection.dotProduct(referenceUp)) > 0.999f){
                referenceUp = Vector3(0, 0, 1);
            }

            Vector3 localRight = localDirection.crossProduct(referenceUp).normalized();
            return localRight.crossProduct(localDirection).normalized();
        }

        bool shadows = false;
        bool automaticShadowCamera = true;
        float shadowBias = 0.002f;
        float shadowNormalBias = 2.0f;
        unsigned int mapResolution = 2048;
        Vector2 shadowCameraNearFar = Vector2(0.1f, 10.0f); // when automatic it gets value from scene camera or light range
        unsigned int numShadowCascades = 3;

        LightCamera cameras[6];
        // base slot in the shadow atlas (projective for directional/spot, point atlas
        // for omni); -1 when the light has no shadow allocation this frame
        int shadowMapIndex = -1;

        bool needUpdateShadowCamera = false;
        bool needUpdateShadowMap = false; // framebuffers
    };
    
}

#endif //LIGHT_COMPONENT_H
