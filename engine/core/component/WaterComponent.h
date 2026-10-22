// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef WATER_COMPONENT_H
#define WATER_COMPONENT_H

#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"
#include "math/AABB.h"
#include "math/Matrix4.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "render/ObjectRender.h"
#include "render/ShaderRender.h"
#include "render/TextureRender.h"
#include "shader/ShaderUniforms.h"
#include "texture/Texture.h"
#include "util/Color.h"
#include <memory>
#include <string>

#define WATER_WAVE_COUNT 4
#define MAX_WATER_SUBDIVISIONS 512

namespace doriax{

    // Water surface at the entity height, with Gerstner waves in world space so the
    // transform never stretches them
    struct DORIAX_API WaterComponent{
        bool loaded = false;
        bool loadCalled = false;

        InterleavedBuffer buffer;
        IndexBuffer indices;
        unsigned int indexCount = 0;
        // size and cells the grid was built with
        Vector2 gridSize = Vector2(0.0f, 0.0f);
        unsigned int gridCells = 0;
        AABB aabb;
        AABB worldAABB;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shader;
        uint32_t shaderProperties = 0;
        // optional user-forked shader: project-relative base path (no extension),
        // empty = built-in. customShaderId is the resolved id for ShaderPool keys.
        std::string customShader;
        uint16_t customShaderId = 0;
        int slotVSParams = -1;
        int slotFSParams = -1;
        unsigned int fsParamsSize = 0; // as the shader declares it, shorter in forks of older versions
        int slotFSLighting = -1;
        int slotFSFog = -1;
        int slotVSShadows = -1;
        int slotFSShadows = -1;
        int slotFSShadowProjection = -1;
        int slotFSPointShadows = -1;
        // values of the fork's custom uniform blocks, by member name
        ShaderUniformValues shaderUniforms;
        bool needUpdateShaderUniforms = false;
        CustomUniformBlock customVSParams;
        CustomUniformBlock customFSParams;

        struct CustomTextureBinding
        {
            std::string name;
            std::pair<int, int> slot = {-1, -1};
            TextureRender* render = nullptr;
        };
        std::vector<CustomTextureBinding> customTextureBindings;
        std::vector<CustomTextureBinding> customUnderwaterTextureBindings;

        // the fullscreen fade seen from inside, loaded with the water while underwater is on
        bool underwaterLoaded = false;
        ObjectRender underwaterRender;
        std::shared_ptr<ShaderRender> underwaterShader;
        std::string customUnderwaterShader; // fork of underwater.frag, built-in when empty
        uint16_t customUnderwaterShaderId = 0;
        int slotUnderwaterParams = -1;
        CustomUniformBlock customUnderwaterParams;

        std::vector<std::pair<std::string, Texture>> customTextures;
        bool needUpdateCustomTextures = false;

        std::vector<std::pair<std::string, Texture>> customUnderwaterTextures;
        bool needUpdateCustomUnderwaterTextures = false;

        Vector2 size = Vector2(50.0f, 50.0f);
        unsigned int subdivisions = 128;

        Vector3 shallowColor = Color::sRGBToLinear(Vector3(0.20f, 0.62f, 0.62f)); // linear color
        Vector3 deepColor = Color::sRGBToLinear(Vector3(0.02f, 0.13f, 0.22f)); // linear color
        float depthFade = 3.0f; // depth where the deep color takes over

        float waveHeight = 0.2f;
        float waveLength = 6.0f; // of the longest wave
        float waveSpeed = 1.0f;
        float waveDirection = 0.0f; // around Y, 0 = +X
        float waveSteepness = 0.6f; // 0 = round, 1 = sharp crests

        Texture normalTexture; // built-in ripples when empty
        float normalScale = 4.0f; // world size of one tile
        float normalStrength = 0.4f;
        float rippleSpeed = 0.3f;

        float reflectivity = 1.0f;
        float specularIntensity = 1.0f;
        float roughness = 0.06f;

        Vector3 foamColor = Vector3(1.0f, 1.0f, 1.0f); // linear color
        float shoreFoam = 0.4f; // depth covered by shore foam
        float crestFoam = 0.15f;

        bool receiveShadows = true;
        bool depthEffects = true; // shore foam and depth tint from the scene depth
        bool planarReflection = false;
        float reflectionDistortion = 0.04f;
        bool refraction = true; // the scene behind is seen through, bent and absorbed
        float refractionDistortion = 0.03f;
        float caustics = 0.5f; // sunlight patterns on the floor seen through, 0 = off
        float causticsScale = 2.0f; // world size of one pattern cell
        bool underwater = true; // a camera inside sees the scene fade into the water
        bool buoyancy = true; // floats the dynamic bodies that have Body3D buoyancy
        float buoyancyDepth = 0.0f; // how far below the surface they still float, 0 = no bottom

        // advanced by RenderSystem::advanceWaters() at the end of each frame
        float wavePhase[WATER_WAVE_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};
        Vector2 rippleOffset[2] = {Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f)};
        float time = 0.0f;
        float pendingTime = 0.0f; // update time not applied yet

        Matrix4 reflectionViewProjection;
        std::shared_ptr<TextureRender> defaultNormalMap;

        bool needUpdateTexture = false;
        bool needReload = false;
    };

}

#endif //WATER_COMPONENT_H
