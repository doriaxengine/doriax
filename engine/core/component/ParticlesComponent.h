//
// (c) 2026 Eduardo Doria.
//

#ifndef PARTICLES_COMPONENT_H
#define PARTICLES_COMPONENT_H

#include "buffer/Buffer.h"
#include "texture/Texture.h"
#include "util/SpriteFrameData.h"
#include "Engine.h"
#include "buffer/ExternalBuffer.h"

namespace doriax{

    struct ParticleLifeInitializer{
        float minLife = 10;
        float maxLife = 10;
    };

    struct ParticlePositionInitializer{
        Vector3 minPosition = Vector3(0,0,0);
        Vector3 maxPosition = Vector3(0,0,0);
    };

    struct ParticlePositionModifier{
        float fromTime = 0;
        float toTime = 0;

        Vector3 fromPosition = Vector3(0,0,0);
        Vector3 toPosition = Vector3(0,0,0);

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

    struct ParticleVelocityInitializer{
        Vector3 minVelocity = Vector3(0,0,0);
        Vector3 maxVelocity = Vector3(0,0,0);
    };

    struct ParticleVelocityModifier{
        float fromTime = 0;
        float toTime = 0;

        Vector3 fromVelocity = Vector3(0,0,0);
        Vector3 toVelocity = Vector3(0,0,0);

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

    struct ParticleAccelerationInitializer{
        Vector3 minAcceleration = Vector3(0,0,0);
        Vector3 maxAcceleration = Vector3(0,0,0);
    };

    struct ParticleAccelerationModifier{
        float fromTime = 0;
        float toTime = 0;

        Vector3 fromAcceleration = Vector3(0,0,0);
        Vector3 toAcceleration = Vector3(0,0,0);

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

    struct ParticleColorInitializer{
        Vector3 minColor = Vector3(1,1,1);
        Vector3 maxColor = Vector3(1,1,1);

        bool useSRGB = true;
    };

    struct ParticleColorModifier{
        float fromTime = 0;
        float toTime = 0;

        Vector3 fromColor = Vector3(0,0,0);
        Vector3 toColor = Vector3(0,0,0);

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);

        bool useSRGB = true;
    };

    struct ParticleAlphaInitializer{
        float minAlpha = 1;
        float maxAlpha = 1;
    };

    struct ParticleAlphaModifier{
        float fromTime = 0;
        float toTime = 0;

        float fromAlpha = 0;
        float toAlpha= 0;

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

    struct ParticleSizeInitializer{
        float minSize = 0;
        float maxSize = 0;
    };

    struct ParticleSizeModifier{
        float fromTime = 0;
        float toTime = 0;

        float fromSize = 0;
        float toSize = 0;

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

    struct ParticleSpriteInitializer{
        std::vector<int> frames;
    };

    struct ParticleSpriteModifier{
        float fromTime = 0;
        float toTime = 0;

        std::vector<int> frames;

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

    struct ParticleRotationInitializer{
        Quaternion minRotation;
        Quaternion maxRotation;

        bool shortestPath = false;
    };

    struct ParticleRotationModifier{
        float fromTime = 0;
        float toTime = 0;

        Quaternion fromRotation;
        Quaternion toRotation;

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);

        bool shortestPath = false;
    };

    struct ParticleScaleInitializer{
        Vector3 minScale = Vector3(1,1,1);
        Vector3 maxScale = Vector3(1,1,1);

        bool linearSort = true; // only for initializer
    };

    struct ParticleScaleModifier{
        float fromTime = 0;
        float toTime = 0;

        Vector3 fromScale = Vector3(1,1,1);
        Vector3 toScale = Vector3(1,1,1);

        FunctionSubscribe<float(float)> function = std::function<float(float)>(Ease::linear);
    };

    struct ParticleData{
        float life = 1;
        Vector3 velocity = Vector3(0,0,0);
        Vector3 acceleration = Vector3(0,0,0);

        float time = 0;
    };

    struct DORIAX_API ParticlesComponent{
        std::vector<ParticleData> particles;

        unsigned int maxParticles = 100;

        // animation
        float newParticlesCount = 0;
        int lastUsedParticle = 0;
        bool emitter = false;

        bool loop = true;

        int rate = 5; //per second
        int maxPerUpdate = 100;

        ParticleLifeInitializer lifeInitializer;

        ParticlePositionInitializer positionInitializer;
        ParticlePositionModifier positionModifier;

        ParticleVelocityInitializer velocityInitializer;
        ParticleVelocityModifier velocityModifier;

        ParticleAccelerationInitializer accelerationInitializer;
        ParticleAccelerationModifier accelerationModifier;

        ParticleColorInitializer colorInitializer;
        ParticleColorModifier colorModifier;

        ParticleAlphaInitializer alphaInitializer;
        ParticleAlphaModifier alphaModifier;

        ParticleSizeInitializer sizeInitializer;
        ParticleSizeModifier sizeModifier;

        ParticleSpriteInitializer spriteInitializer;
        ParticleSpriteModifier spriteModifier;

        ParticleRotationInitializer rotationInitializer;
        ParticleRotationModifier rotationModifier;

        ParticleScaleInitializer scaleInitializer;
        ParticleScaleModifier scaleModifier;
    };
    
}

#endif //PARTICLES_COMPONENT_H