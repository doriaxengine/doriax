// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef WATER_H
#define WATER_H

#include "object/Object.h"
#include "component/WaterComponent.h"

namespace doriax{

    class DORIAX_API Water: public Object{

    public:
        Water(Scene* scene);
        Water(Scene* scene, Entity entity);
        virtual ~Water();

        bool load();

        void setSize(Vector2 size);
        void setSize(const float width, const float depth);
        Vector2 getSize() const;

        void setSubdivisions(unsigned int subdivisions);
        unsigned int getSubdivisions() const;

        void setShallowColor(Vector3 color);
        void setShallowColor(const float r, const float g, const float b);
        Vector3 getShallowColor() const;

        void setDeepColor(Vector3 color);
        void setDeepColor(const float r, const float g, const float b);
        Vector3 getDeepColor() const;

        void setDepthFade(float depthFade);
        float getDepthFade() const;

        void setWaveHeight(float waveHeight);
        float getWaveHeight() const;

        void setWaveLength(float waveLength);
        float getWaveLength() const;

        void setWaveSpeed(float waveSpeed);
        float getWaveSpeed() const;

        void setWaveDirection(float waveDirection);
        float getWaveDirection() const;

        void setWaveSteepness(float waveSteepness);
        float getWaveSteepness() const;

        // an empty path restores the built-in ripples
        void setNormalTexture(const std::string& path);

        void setNormalScale(float normalScale);
        float getNormalScale() const;

        void setNormalStrength(float normalStrength);
        float getNormalStrength() const;

        void setRippleSpeed(float rippleSpeed);
        float getRippleSpeed() const;

        void setReflectivity(float reflectivity);
        float getReflectivity() const;

        void setSpecularIntensity(float specularIntensity);
        float getSpecularIntensity() const;

        void setRoughness(float roughness);
        float getRoughness() const;

        void setFoamColor(Vector3 color);
        void setFoamColor(const float r, const float g, const float b);
        Vector3 getFoamColor() const;

        void setShoreFoam(float shoreFoam);
        float getShoreFoam() const;

        void setCrestFoam(float crestFoam);
        float getCrestFoam() const;

        void setReceiveShadows(bool receiveShadows);
        bool isReceiveShadows() const;

        void setDepthEffects(bool depthEffects);
        bool isDepthEffects() const;

        void setPlanarReflection(bool planarReflection);
        bool isPlanarReflection() const;

        void setReflectionDistortion(float reflectionDistortion);
        float getReflectionDistortion() const;

        void setRefraction(bool refraction);
        bool isRefraction() const;

        void setRefractionDistortion(float refractionDistortion);
        float getRefractionDistortion() const;

        // needs refraction and depth effects
        void setCaustics(float caustics);
        float getCaustics() const;

        void setCausticsScale(float causticsScale);
        float getCausticsScale() const;

        void setUnderwater(bool underwater);
        bool isUnderwater() const;

        void setBuoyancy(bool buoyancy);
        bool isBuoyancy() const;

        void setBuoyancyDepth(float buoyancyDepth);
        float getBuoyancyDepth() const;

        // world size of the wake map around the camera
        void setWakeArea(float wakeArea);
        float getWakeArea() const;

        void setWakeFade(float wakeFade);
        float getWakeFade() const;

        // shader hooks of the surface and the underwater fade
        void setShader(const std::string& path);
        std::string getShader() const;

        void setShaderUniform(const std::string& name, const Vector4& value);
        void setShaderUniform(const std::string& name, const Vector3& value);
        void setShaderUniform(const std::string& name, const Vector2& value);
        void setShaderUniform(const std::string& name, float value);
        Vector4 getShaderUniform(const std::string& name) const;
        bool removeShaderUniform(const std::string& name);

        void setShaderTexture(const std::string& name, const Texture& texture);
        Texture getShaderTexture(const std::string& name) const;

        // surface at the world point (x, z), waves included
        float getHeight(float x, float z) const;
        Vector3 getNormal(float x, float z) const;
    };
}

#endif //WATER_H
