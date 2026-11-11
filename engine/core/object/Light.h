// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef LIGHT_H
#define LIGHT_H

#include "Object.h"

namespace doriax{

    class DORIAX_API Light: public Object{
    public:
        Light(Scene* scene);
        Light(Scene* scene, Entity entity);
        virtual ~Light();

        void setType(LightType type);
        LightType getType() const;

        void setDirection(Vector3 direction);
        void setDirection(const float x, const float y, const float z);
        Vector3 getDirection() const;

        void setColor(Vector3 color);
        void setColor(const float r, const float g, const float b);
        Vector3 getColor() const;

        void setRange(float range);
        float getRange() const;

        void setIntensity(float intensity);
        float getIntensity() const;

        void setConeAngle(float inner, float outer);
        void setInnerConeAngle(float inner);
        float getInnerConeAngle() const;
        void setOuterConeAngle(float outer);
        float getOuterConeAngle() const;

        void setSpotMask(const std::string& texture);
        void setSpotMask(const std::string& id, TextureData data);
        void clearSpotMask();
        Texture getSpotMask() const;

        void setShadows(bool shadows);
        bool isShadows() const;

        void setBias(float bias);
        float getBias() const;

        void setShadowMapSize(unsigned int size);
        unsigned int getShadowMapSize() const;

        void setShadowCameraNearFar(float nearValue, float farValue);
        void setCameraNear(float nearValue);
        float getCameraNear() const;
        void setCameraFar(float farValue);
        float getCameraFar() const;

        void setAutomaticShadowCamera(bool automatic);
        bool isAutomaticShadowCamera() const;

        void setNumCascades(unsigned int numCascades);
        float getNumCascades() const;

        void setCascadeBlend(bool cascadeBlend);
        bool isCascadeBlend() const;
    };
}

#endif //LIGHT_H
