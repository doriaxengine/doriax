// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "RenderSystem.h"
#include "object/Camera.h"
#include "math/Quaternion.h"
#include "Scene.h"
#include "Engine.h"
#include "System.h"
#include "render/Render.h"
#include "render/ObjectRender.h"
#include "render/SystemRender.h"
#include "pool/ShaderPool.h"
#include "pool/TexturePool.h"
#include "math/Vector3.h"
#include "texture/IBLEnvironment.h"
#include "util/Angle.h"
#include "util/Color.h"
#include "buffer/ExternalBuffer.h"
#include "math/AABB.h"
#include "pool/TextureDataPool.h"
#include <memory>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <cstdint>
#include <cstring>

using namespace doriax;

namespace {
    constexpr int SPOT_MASK_TILE_SIZE = 256;

    static_assert(sizeof(Matrix4) == sizeof(float) * 16);

    bool bindSkinningResource(ObjectRender& render, ShaderData& shader, MeshComponent& mesh){
        const size_t boneCount = mesh.bonesMatrix.size();
        const unsigned int size = static_cast<unsigned int>(boneCount * sizeof(Matrix4));
        const int storageSlot = shader.getStorageBufferIndex(StorageBufferType::VS_SKINNING);
        if (storageSlot != -1){
            if (!mesh.bonesBuffer.isCreated() &&
                    !mesh.bonesBuffer.createBuffer(size, nullptr, BufferType::STORAGE_BUFFER, BufferUsage::STREAM))
                return false;
            render.addStorageBuffer(storageSlot, ShaderStageType::VERTEX, &mesh.bonesBuffer);
        }else{
            const auto textureSlot = shader.getTextureIndex(TextureShaderType::BONES);
            if (textureSlot.first == -1 || (!mesh.bonesTexture.isCreated() &&
                    !mesh.bonesTexture.createDynamicTexture("mesh-bones", 4,
                        static_cast<int>(boneCount))))
                return false;
            render.addTexture(textureSlot, ShaderStageType::VERTEX, &mesh.bonesTexture);
        }
        mesh.needUpdateBones = true;
        return true;
    }

    void applySkinningUniform(ObjectRender& render, int slot, const MeshComponent& mesh,
                              const Submesh& submesh){
        Vector4 normAdjust(
            submesh.hasSkinningNormalization ? submesh.normAdjustJoint : mesh.normAdjustJoint,
            submesh.hasSkinningNormalization ? submesh.normAdjustWeight : mesh.normAdjustWeight,
            0.0f, 0.0f);
        render.applyUniformBlock(slot, sizeof(normAdjust), &normAdjust);
    }

    bool usesAlphaMask(const Material& material, bool textureShadow){
        return material.alphaMode == MaterialAlphaMode::MASK ||
            (material.alphaMode == MaterialAlphaMode::AUTO && textureShadow);
    }

    bool usesAlphaBlend(const Material& material){
        return material.alphaMode == MaterialAlphaMode::BLEND ||
            (material.alphaMode == MaterialAlphaMode::AUTO &&
             (material.baseColorTexture.isTransparent() || material.baseColorFactor.w != 1.0f));
    }

    float readSpotMaskTexel(TextureData& source, int x, int y, bool useAlpha){
        x = std::clamp(x, 0, source.getWidth() - 1);
        y = std::clamp(y, 0, source.getHeight() - 1);

        const int channels = source.getChannels();
        const int bytesPerChannel = TextureData::getBytesPerChannel(source.getColorFormat());
        const size_t pixelOffset =
            (static_cast<size_t>(y) * static_cast<size_t>(source.getWidth()) + static_cast<size_t>(x)) *
            static_cast<size_t>(channels) * static_cast<size_t>(bytesPerChannel);
        const unsigned char* pixels = static_cast<const unsigned char*>(source.getData());

        if (source.getColorFormat() == ColorFormat::RED16){
            uint16_t value = 0;
            std::memcpy(&value, pixels + pixelOffset, sizeof(value));
            return static_cast<float>(value) / 65535.0f;
        }

        if (useAlpha && channels >= 4){
            return static_cast<float>(pixels[pixelOffset + 3]) / 255.0f;
        }

        if (channels >= 3){
            Vector3 color = Color::sRGBToLinear(
                static_cast<float>(pixels[pixelOffset]) / 255.0f,
                static_cast<float>(pixels[pixelOffset + 1]) / 255.0f,
                static_cast<float>(pixels[pixelOffset + 2]) / 255.0f);
            return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
        }

        return static_cast<float>(pixels[pixelOffset]) / 255.0f;
    }

    void rasterizeSpotMaskTile(TextureData& source, int tileIndex, int atlasWidth, std::vector<unsigned char>& atlas){
        const bool useAlpha = source.getColorFormat() == ColorFormat::RGBA && source.hasAlpha();
        const float sourceWidth = static_cast<float>(source.getWidth());
        const float sourceHeight = static_cast<float>(source.getHeight());

        for (int y = 0; y < SPOT_MASK_TILE_SIZE; y++){
            const float sourceY = ((static_cast<float>(y) + 0.5f) / static_cast<float>(SPOT_MASK_TILE_SIZE)) * sourceHeight - 0.5f;
            const int y0 = static_cast<int>(std::floor(sourceY));
            const int y1 = y0 + 1;
            const float fy = sourceY - static_cast<float>(y0);

            for (int x = 0; x < SPOT_MASK_TILE_SIZE; x++){
                const float sourceX = ((static_cast<float>(x) + 0.5f) / static_cast<float>(SPOT_MASK_TILE_SIZE)) * sourceWidth - 0.5f;
                const int x0 = static_cast<int>(std::floor(sourceX));
                const int x1 = x0 + 1;
                const float fx = sourceX - static_cast<float>(x0);

                const float top = readSpotMaskTexel(source, x0, y0, useAlpha) * (1.0f - fx) +
                                  readSpotMaskTexel(source, x1, y0, useAlpha) * fx;
                const float bottom = readSpotMaskTexel(source, x0, y1, useAlpha) * (1.0f - fx) +
                                     readSpotMaskTexel(source, x1, y1, useAlpha) * fx;
                const float value = top * (1.0f - fy) + bottom * fy;

                const int atlasX = tileIndex * SPOT_MASK_TILE_SIZE + x;
                atlas[static_cast<size_t>(y) * static_cast<size_t>(atlasWidth) + static_cast<size_t>(atlasX)] =
                    static_cast<unsigned char>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            }
        }
    }
}

uint32_t RenderSystem::pixelsWhite[64];
uint32_t RenderSystem::pixelsBlack[64];
uint32_t RenderSystem::pixelsNormal[64];

TextureRender RenderSystem::emptyWhite;
TextureRender RenderSystem::emptyBlack;
TextureRender RenderSystem::emptyCubeBlack;
TextureRender RenderSystem::emptyCubeWhite;
TextureRender RenderSystem::emptyNormal;
TextureRender RenderSystem::emptyShadowDepth;

bool RenderSystem::emptyTexturesCreated = false;

RenderSystem::RenderSystem(Scene* scene): SubSystem(scene){
    signature.set(scene->getComponentId<Transform>());

    this->scene = scene;

    ssaoLoaded = false;
    ssaoWidth = 0;
    ssaoHeight = 0;
    ssaoSlotParams = -1;
    ssaoBlurSlotParams = -1;
    currentSSAOTexture = NULL;

    ssrLoaded = false;
    ssrWidth = 0;
    ssrHeight = 0;
    ssrSlotParams = -1;
    ssrBlurSlotParams = -1;
    compositeSlotParams = -1;
    swapchainRedirect = false;

    blitLoaded = false;
    fixedResWidth = 0;
    fixedResHeight = 0;
    blitSlotParams = -1;

    postProcessLoaded = false;
    postProcessNeedReload = false;
    postProcessWidth = 0;
    postProcessHeight = 0;
    postProcessNeedsDepth = false;
    postProcessNeedsGBuffer = false;

    shadowAtlasSlotResolution = 0;
    shadowAtlasCols = 0;
    shadowAtlasRows = 0;
    shadowAtlasUsedSlots = 0;
    needUpdateShadowAtlas = true;
    shadowPointAtlasSlotResolution = 0;
    needUpdateShadowPointAtlas = true;
    needUpdateShadowBindings = false;
    hasShadowAtlas = false;
    hasShadowPointAtlas = false;
    spotMaskAtlasCreated = false;
    initShadowAtlasRects();
    initShadowPointAtlasRects();

    shadow2DAtlasWidth = 0;
    hasShadow2DAtlas = false;
    occluder2DLoaded = false;
    shadow2DSlotParams = -1;
    occluder2DBufferCapacity = 0;
    occluder2DVertexCount = 0;
    occluder2DNeedUpdateBuffer = false;
    hasReflectionProbes = false;
    capturingReflectionProbe = false;
}

RenderSystem::~RenderSystem(){
}

void RenderSystem::load(){
    hasLights = false;
    hasShadows = false;
    hasFog = false;
    hasIBL = false;
    hasReflectionProbes = false;
    hasMultipleCameras = false;
    lastMultiCameraDraw = false;
    capturingReflectionProbe = false;
    loadedPipelines = 0;
    hasLights2D = false;
    hasShadows2D = false;
    numLights2D = 0;

    createEmptyTextures();

    update(0); // first update
}

void RenderSystem::destroy(){
    // Before the transform traversal below, which the camera entities are part of.
    while (!mirrorCameras.empty()){
        destroyMirrorCamera(mirrorCameras.begin()->first);
    }

    // cannot destroy static textures because it affects other scenes
    //emptyWhite.destroyTexture();
    //emptyBlack.destroyTexture();
    //emptyCubeBlack.destroyTexture();
    //emptyNormal.destroyTexture();

    emptyTexturesCreated = false;

    destroySSAO();
    destroySSR();
    destroyBlit();
    destroyPostProcess();
    shadowAtlasFramebuffer.destroyFramebuffer();
    shadowAtlasSlotResolution = 0;
    shadowAtlasCols = 0;
    shadowAtlasRows = 0;
    shadowAtlasUsedSlots = 0;
    hasShadowAtlas = false;
    shadowPointAtlasFramebuffer.destroyFramebuffer();
    shadowPointAtlasSlotResolution = 0;
    hasShadowPointAtlas = false;
    shadow2DAtlasFramebuffer.destroyFramebuffer();
    shadow2DAtlasWidth = 0;
    hasShadow2DAtlas = false;
    destroyOccluder2DPass();
    if (spotMaskAtlasCreated){
        spotMaskAtlas.destroyTexture();
        spotMaskAtlasCreated = false;
    }
    spotMaskAtlasPixels.clear();
    spotMaskAtlasEntries.fill({});

    auto skys = scene->getComponentArray<SkyComponent>();
    if (skys->size() > 0){
        SkyComponent& sky = skys->getComponentFromIndex(0);
        Entity entity = skys->getEntity(0);
        if (sky.loaded){
            destroySky(entity, sky);
        }
    }

    auto probes = scene->getComponentArray<ReflectionProbeComponent>();
    for (int i = 0; i < probes->size(); i++){
        ReflectionProbeComponent& probe = probes->getComponentFromIndex(i);
        destroyReflectionProbe(probes->getEntity(i), probe);
    }
    // defensive sweep: a runtime can outlive its component (e.g. the probe lost
    // its Transform and updateReflectionProbes never revisited it)
    for (auto& entry : reflectionProbeRuntimes){
        entry.second->captureFramebuffer.destroyFramebuffer();
        releaseReflectionProbeMaps(*entry.second);
    }
    reflectionProbeRuntimes.clear();

    auto transforms = scene->getComponentArray<Transform>();
    for (int i = 0; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);
        Entity entity = transforms->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<MeshComponent>())){
            MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
            if (mesh.loaded){
                destroyMesh(entity, mesh);
            }
        }else if (signature.test(scene->getComponentId<UIComponent>())){
            UIComponent& ui = scene->getComponent<UIComponent>(entity);
            if (ui.loaded){
                destroyUI(entity, ui);
            }
        }else if (signature.test(scene->getComponentId<PointsComponent>())){
            PointsComponent& points = scene->getComponent<PointsComponent>(entity);
            if (points.loaded){
                destroyPoints(entity, points);
            }
        }else if (signature.test(scene->getComponentId<LinesComponent>())){
            LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
            if (lines.loaded){
                destroyLines(entity, lines);
            }
        }else if (signature.test(scene->getComponentId<LightComponent>())){
            LightComponent& light = scene->getComponent<LightComponent>(entity);
            destroyLight(light);
        }else if (signature.test(scene->getComponentId<CameraComponent>())){
            CameraComponent& camera = scene->getComponent<CameraComponent>(entity);
            destroyCamera(camera, false);
        }
    }
}

void RenderSystem::updateFramebuffer(CameraComponent& camera){
    if (camera.framebuffer->isCreated()){
        camera.framebuffer->destroy();
        camera.framebuffer->create();
    }
}

void RenderSystem::createEmptyTextures(){
    if (!emptyTexturesCreated){
        void* data_array[6];
        size_t size_array[6];

        for (int i = 0; i < 64; i++) {
            pixelsNormal[i] = 0xFF808080;
        }

        for (int i = 0; i < 6; i++){
            data_array[i]  = (void*)pixelsNormal;
            size_array[i] = 8 * 8 * 4;
        }

        emptyNormal.createTexture(
                "empty|normal", 8, 8, ColorFormat::RGBA, TextureType::TEXTURE_2D, 1, data_array, size_array, 
                TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::REPEAT, TextureWrap::REPEAT);

        for (int i = 0; i < 64; i++) {
            pixelsWhite[i] = 0xFFFFFFFF;
        }

        for (int i = 0; i < 6; i++){
            data_array[i]  = (void*)pixelsWhite;
            size_array[i] = 8 * 8 * 4;
        }

        emptyWhite.createTexture(
                "empty|white", 8, 8, ColorFormat::RGBA, TextureType::TEXTURE_2D, 1, data_array, size_array,
                TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::REPEAT, TextureWrap::REPEAT);

        // white cube fallback for the skybox cube sampler: a color-only sky (no
        // texture) multiplies skyParams.color by this, so white keeps the color intact
        emptyCubeWhite.createTexture(
                "empty|cube|white", 8, 8, ColorFormat::RGBA, TextureType::TEXTURE_CUBE, 6, data_array, size_array,
                TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::REPEAT, TextureWrap::REPEAT);

        for (int i = 0; i < 64; i++) {
            pixelsBlack[i] = 0xFF000000;
        }

        for (int i = 0; i < 6; i++){
            data_array[i]  = (void*)pixelsBlack;
            size_array[i] = 8 * 8 * 4;
        }

        emptyBlack.createTexture(
                "empty|black", 8, 8, ColorFormat::RGBA, TextureType::TEXTURE_2D, 1, data_array, size_array, 
                TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::REPEAT, TextureWrap::REPEAT);
        emptyCubeBlack.createTexture(
                "empty|cube|black", 8, 8, ColorFormat::RGBA, TextureType::TEXTURE_CUBE, 6, data_array, size_array, 
                TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::REPEAT, TextureWrap::REPEAT);

        emptyTexturesCreated = true;
    }

    // Point-only scenes still declare the projective sampler, so keep a
    // type-compatible binding available even if it is never sampled.
    if (!emptyShadowDepth.isCreated()){
        emptyShadowDepth.createFramebufferTexture(
            TextureType::TEXTURE_2D, true, true, 1, 1,
            TextureFilter::NEAREST, TextureFilter::NEAREST,
            TextureWrap::CLAMP_TO_BORDER, TextureWrap::CLAMP_TO_BORDER);
    }
}

int RenderSystem::checkLightsAndShadow(){
    hasLights = false;
    hasShadows = false;

    auto lights = scene->getComponentArray<LightComponent>();

    int numLights = lights->size();
    if (numLights > MAX_LIGHTS)
        numLights = MAX_LIGHTS;

    if (scene->getLightState() == LightState::AUTO){
        if (numLights > 0)
            hasLights = true;
    }else if (scene->getLightState() == LightState::ON){
        hasLights = true;
    }
    
    for (int i = 0; i < numLights; i++){
        LightComponent& light = lights->getComponentFromIndex(i);
        if (light.shadows){
            hasShadows = true;
        }
    }

    // 2D lights are gated only by their own component count, NOT by
    // scene->getLightState(): the editor forces LightState::OFF in 2D scenes
    // to keep 3D lights out of them, but 2D lights must still work there.
    hasLights2D = false;
    hasShadows2D = false;

    auto lights2d = scene->getComponentArray<Light2DComponent>();
    numLights2D = lights2d->size();
    if (numLights2D > MAX_LIGHTS_2D)
        numLights2D = MAX_LIGHTS_2D;

    if (numLights2D > 0)
        hasLights2D = true;

    return numLights;
}

bool RenderSystem::loadLights(int numLights){
    int freeProjectiveSlot = 0;
    int freePointSlot = 0;
    unsigned int maxProjectiveSlotResolution = 0;
    unsigned int maxPointSlotResolution = 0;

    auto lights = scene->getComponentArray<LightComponent>();
    
    for (int i = 0; i < numLights; i++){
        LightComponent& light = lights->getComponentFromIndex(i);

        light.shadowMapIndex = -1;
        
        if (light.shadows){
            if (light.type == LightType::DIRECTIONAL &&
                (light.numShadowCascades < 1 || light.numShadowCascades > MAX_SHADOWCASCADES)){
                unsigned int requestedCascades = light.numShadowCascades;
                light.numShadowCascades = std::clamp(
                    light.numShadowCascades, 1u, (unsigned int)MAX_SHADOWCASCADES);
                Log::warn("Shadow cascades clamped from %u to %u", requestedCascades, light.numShadowCascades);
            }

            if (light.needUpdateShadowMap){
                needUpdateShadowAtlas = true;
                needUpdateShadowPointAtlas = true;
                needUpdateShadowBindings = true;
                light.needUpdateShadowMap = false;
            }
        }
    }

    for (int i = 0; i < numLights; i++){
        LightComponent& light = lights->getComponentFromIndex(i);
        if (!light.shadows) continue;

        if (light.type == LightType::SPOT){
            if (freeProjectiveSlot < MAX_SHADOW_ATLAS_SLOTS){
                light.shadowMapIndex = freeProjectiveSlot;
                freeProjectiveSlot++;
                maxProjectiveSlotResolution = std::max(maxProjectiveSlotResolution, light.mapResolution);
            }
        }else if (light.type == LightType::DIRECTIONAL){
            if ((freeProjectiveSlot + light.numShadowCascades) <= MAX_SHADOW_ATLAS_SLOTS){
                light.shadowMapIndex = freeProjectiveSlot;
                freeProjectiveSlot += light.numShadowCascades;
                maxProjectiveSlotResolution = std::max(maxProjectiveSlotResolution, light.mapResolution);
            }
        }
    }

    for (int i = 0; i < numLights; i++){
        LightComponent& light = lights->getComponentFromIndex(i);
        if (!light.shadows || light.type != LightType::POINT) continue;

        if ((freePointSlot + SHADOW_CUBE_FACES) <= MAX_POINT_SHADOW_ATLAS_SLOTS){
            light.shadowMapIndex = freePointSlot;
            freePointSlot += SHADOW_CUBE_FACES;
            maxPointSlotResolution = std::max(maxPointSlotResolution, light.mapResolution);
        }
    }

    if (freeProjectiveSlot > 0){
        if (!ensureShadowAtlas(maxProjectiveSlotResolution, freeProjectiveSlot)){
            Log::warn("Failed to create projective shadow atlas");
            for (int i = 0; i < numLights; i++){
                LightComponent& light = lights->getComponentFromIndex(i);
                if (light.shadows && light.type != LightType::POINT){
                    light.shadowMapIndex = -1;
                }
            }
        }
    }else{
        if (hasShadowAtlas || shadowAtlasFramebuffer.isCreated()){
            needUpdateShadowBindings = true;
        }
        shadowAtlasFramebuffer.destroyFramebuffer();
        shadowAtlasSlotResolution = 0;
        shadowAtlasCols = 0;
        shadowAtlasRows = 0;
        shadowAtlasUsedSlots = 0;
        initShadowAtlasRects();
        hasShadowAtlas = false;
    }

    if (freePointSlot > 0){
        if (!ensureShadowPointAtlas(maxPointSlotResolution)){
            Log::warn("Failed to create point shadow atlas");
            for (int i = 0; i < numLights; i++){
                LightComponent& light = lights->getComponentFromIndex(i);
                if (light.shadows && light.type == LightType::POINT){
                    light.shadowMapIndex = -1;
                }
            }
        }
    }else{
        if (hasShadowPointAtlas || shadowPointAtlasFramebuffer.isCreated()){
            needUpdateShadowBindings = true;
        }
        shadowPointAtlasFramebuffer.destroyFramebuffer();
        shadowPointAtlasSlotResolution = 0;
        hasShadowPointAtlas = false;
    }

    return true;
}

void RenderSystem::updateSpotMaskAtlas(int numLights){
    auto lights = scene->getComponentArray<LightComponent>();
    std::array<SpotMaskAtlasEntry, MAX_LIGHTS> newEntries;
    std::array<TextureData*, MAX_LIGHTS> sources{};
    std::array<std::shared_ptr<std::array<TextureData, 6>>, MAX_LIGHTS> sourceOwners;
    std::array<bool, MAX_LIGHTS> previousReady{};
    std::array<float, MAX_LIGHTS> previousAspects{};

    for (int i = 0; i < MAX_LIGHTS; i++){
        if (i >= numLights){
            continue;
        }

        LightComponent& light = lights->getComponentFromIndex(i);
        previousReady[i] = light.spotMaskReady;
        previousAspects[i] = light.spotMaskAspect;

        if (light.type == LightType::SPOT && !light.spotMask.empty() && !light.spotMask.isFramebuffer()){
            const std::string sourceId = light.spotMask.getId();

            auto acceptSource = [&](const std::shared_ptr<std::array<TextureData, 6>>& owner){
                if (!owner){
                    return false;
                }

                TextureData& source = owner->at(0);
                if (!source.getData() || source.getWidth() <= 0 || source.getHeight() <= 0 || source.getChannels() <= 0){
                    return false;
                }

                sourceOwners[i] = owner;
                sources[i] = &sourceOwners[i]->at(0);
                newEntries[i].sourceId = sourceId;
                newEntries[i].sourceOwner = owner.get();
                newEntries[i].sourcePixels = source.getData();
                newEntries[i].width = source.getWidth();
                newEntries[i].height = source.getHeight();
                newEntries[i].aspect = std::clamp(
                    static_cast<float>(source.getWidth()) / static_cast<float>(source.getHeight()),
                    0.1f, 10.0f);
                return true;
            };

            std::shared_ptr<std::array<TextureData, 6>> pooledSource = TextureDataPool::get(sourceId);
            bool hasSource = acceptSource(pooledSource);

            if (!hasSource){
                const SpotMaskAtlasEntry& committedEntry = spotMaskAtlasEntries[i];
                const bool canReuseCommittedTile =
                    spotMaskAtlasCreated &&
                    pooledSource &&
                    pooledSource.get() == committedEntry.sourceOwner &&
                    sourceId == committedEntry.sourceId &&
                    !committedEntry.empty();

                if (canReuseCommittedTile){
                    newEntries[i] = committedEntry;
                }else{
                    TextureLoadResult result = light.spotMask.load();
                    if (result.state == ResourceLoadState::Finished){
                        acceptSource(result.data);
                    }
                }
            }
        }
    }

    auto applyCommittedReadiness = [&](){
        for (int i = 0; i < numLights; i++){
            LightComponent& light = lights->getComponentFromIndex(i);
            const bool ready =
                spotMaskAtlasCreated &&
                !newEntries[i].empty() &&
                newEntries[i] == spotMaskAtlasEntries[i];

            light.spotMaskReady = ready;
            light.spotMaskAspect = ready ? spotMaskAtlasEntries[i].aspect : 1.0f;

            if (previousReady[i] != light.spotMaskReady ||
                std::abs(previousAspects[i] - light.spotMaskAspect) > 0.0001f){
                light.needUpdateShadowCamera = true;
            }
        }

        for (int i = numLights; i < static_cast<int>(lights->size()); i++){
            LightComponent& light = lights->getComponentFromIndex(i);
            if (light.spotMaskReady || std::abs(light.spotMaskAspect - 1.0f) > 0.0001f){
                light.spotMaskReady = false;
                light.spotMaskAspect = 1.0f;
                light.needUpdateShadowCamera = true;
            }
        }
    };

    if (spotMaskAtlasCreated && newEntries == spotMaskAtlasEntries){
        applyCommittedReadiness();
        return;
    }
    if (!Engine::isViewLoaded()){
        applyCommittedReadiness();
        return;
    }

    const bool hasSpotMasks = std::any_of(
        newEntries.begin(),
        newEntries.end(),
        [](const SpotMaskAtlasEntry& entry){ return !entry.empty(); });
    if (!hasSpotMasks){
        if (spotMaskAtlasCreated){
            spotMaskAtlas.destroyTexture();
            spotMaskAtlasCreated = false;
        }
        spotMaskAtlasEntries.fill({});
        spotMaskAtlasPixels.clear();
        applyCommittedReadiness();
        return;
    }

    const int atlasWidth = SPOT_MASK_TILE_SIZE * MAX_LIGHTS;
    const size_t atlasSize =
        static_cast<size_t>(atlasWidth) * static_cast<size_t>(SPOT_MASK_TILE_SIZE);
    std::vector<unsigned char> newPixels = spotMaskAtlasPixels;
    const bool rebuildAllTiles = newPixels.size() != atlasSize;
    if (rebuildAllTiles){
        newPixels.assign(atlasSize, 255);
    }

    for (int i = 0; i < MAX_LIGHTS; i++){
        if (sources[i] &&
            (rebuildAllTiles || !spotMaskAtlasCreated || !(newEntries[i] == spotMaskAtlasEntries[i]))){
            rasterizeSpotMaskTile(*sources[i], i, atlasWidth, newPixels);
        }else if (newEntries[i].empty() &&
                  (rebuildAllTiles || !spotMaskAtlasEntries[i].empty())){
            const int tileStart = i * SPOT_MASK_TILE_SIZE;
            for (int y = 0; y < SPOT_MASK_TILE_SIZE; y++){
                auto row = newPixels.begin() +
                    static_cast<size_t>(y) * static_cast<size_t>(atlasWidth) +
                    static_cast<size_t>(tileStart);
                std::fill(row, row + SPOT_MASK_TILE_SIZE, 255);
            }
        }
    }

    void* data[6] = {};
    size_t size[6] = {};
    data[0] = newPixels.data();
    size[0] = newPixels.size();

    TextureRender newAtlas;
    const bool newAtlasCreated = newAtlas.createTexture(
        "spot-mask-atlas",
        atlasWidth,
        SPOT_MASK_TILE_SIZE,
        ColorFormat::RED,
        TextureType::TEXTURE_2D,
        1,
        data,
        size,
        TextureFilter::LINEAR,
        TextureFilter::LINEAR,
        TextureWrap::CLAMP_TO_EDGE,
        TextureWrap::CLAMP_TO_EDGE);

    if (!newAtlasCreated){
        newAtlas.destroyTexture();
        applyCommittedReadiness();
        return;
    }

    if (spotMaskAtlasCreated){
        spotMaskAtlas.destroyTexture();
    }
    spotMaskAtlas = newAtlas;
    spotMaskAtlasCreated = true;
    spotMaskAtlasPixels = std::move(newPixels);
    spotMaskAtlasEntries = newEntries;
    applyCommittedReadiness();

    // The atlas owns its resampled copy, so honor the source Texture's normal
    // release-after-upload policy instead of retaining full-resolution pixels.
    for (int i = 0; i < numLights; i++){
        LightComponent& light = lights->getComponentFromIndex(i);
        if (sourceOwners[i] && light.spotMask.isReleaseDataAfterLoad()){
            sourceOwners[i]->at(0).releaseImageData();
            spotMaskAtlasEntries[i].sourcePixels = nullptr;
        }
    }
}

void RenderSystem::loadSpotMaskTexture(ShaderData& shaderData, ObjectRender& render){
    render.addTexture(
        shaderData.getTextureIndex(TextureShaderType::SPOTMASKATLAS),
        ShaderStageType::FRAGMENT,
        spotMaskAtlasCreated ? &spotMaskAtlas : &emptyWhite);
}

void RenderSystem::processLights(int numLights, CameraComponent& camera, Transform& cameraTransform){
    auto lights = scene->getComponentArray<LightComponent>();

    for (int i = 0; i < numLights; i++){
        LightComponent& light = lights->getComponentFromIndex(i);
        Entity entity = lights->getEntity(i);
        Transform* transform = scene->findComponent<Transform>(entity);

        Vector3 worldPosition;
        if (transform){
            worldPosition = Vector3(transform->worldPosition);
        }

        int type = 0;
        if (light.type == LightType::POINT)
            type = 1;
        if (light.type == LightType::SPOT)
            type = 2;

        if (light.shadows){
            if (light.shadowMapIndex >= 0){
                size_t numMaps = 1;
                if (light.type == LightType::DIRECTIONAL){
                    numMaps = light.numShadowCascades;
                }else if (light.type == LightType::POINT){
                    numMaps = SHADOW_CUBE_FACES;
                }

                for (int c = 0; c < (int)numMaps; c++){
                    if (light.type == LightType::POINT){
                        fs_point_shadows.bias_texSize_nearFar[light.shadowMapIndex+c] = Vector4(
                            light.shadowBias,
                            (float)shadowPointAtlasSlotResolution,
                            light.cameras[c].nearFar.x,
                            light.cameras[c].nearFar.y);
                    }else{
                        vs_shadows.lightViewProjectionMatrix[light.shadowMapIndex+c] = light.cameras[c].lightViewProjectionMatrix;

                        float projScale = std::max(std::abs(light.cameras[c].lightProjectionMatrix[0][0]),
                                                   std::abs(light.cameras[c].lightProjectionMatrix[1][1]));
                        float texelSizeWorld = (projScale > 0.0f) ? 2.0f / (projScale * (float)shadowAtlasSlotResolution) : 0.0f;
                        vs_shadows.shadowParams[light.shadowMapIndex+c] = Vector4(light.shadowNormalBias * texelSizeWorld, 0.0, 0.0, 0.0);

                        fs_shadows.bias_texSize_nearFar[light.shadowMapIndex+c] = Vector4(
                            light.shadowBias,
                            (float)shadowAtlasSlotResolution,
                            light.cameras[c].nearFar.x,
                            light.cameras[c].nearFar.y);
                    }
                }
            }
            // shadowMapIndex < 0 means the atlas had no free slot for this light this
            // frame: it is passed to the shader (inCon_ouCon_shadows_cascades.z) which
            // treats a negative index as "no shadow". The light keeps its shadow intent
            // and reclaims a slot automatically once one frees up.
        }

        fs_lighting.direction_range[i] = Vector4(light.worldDirection.x, light.worldDirection.y, light.worldDirection.z, light.range);
        fs_lighting.color_intensity[i] = Vector4(light.color.x, light.color.y, light.color.z, light.intensity);
        fs_lighting.position_type[i] = Vector4(worldPosition.x, worldPosition.y, worldPosition.z, (float)type);
        fs_lighting.inCon_ouCon_shadows_cascades[i] = Vector4(light.innerConeCos, light.outerConeCos, light.shadowMapIndex, light.numShadowCascades);
        fs_lighting.spotUp_maskAspect[i] = Vector4(
            light.worldUp.x,
            light.worldUp.y,
            light.worldUp.z,
            (light.type == LightType::SPOT && light.spotMaskReady) ? light.spotMaskAspect : 0.0f);
    }

    // PCF tap radius of the 3D shadow filter, packed into cameraDir.w
    // (kernel = (2*radius+1)^2 taps; uniform-driven, no shader variant)
    float pcfRadius = 1.0f;
    switch (scene->getShadowQuality()){
        case ShadowQuality::NONE:   pcfRadius = 0.0f; break;
        case ShadowQuality::LOW:    pcfRadius = 1.0f; break;
        case ShadowQuality::MEDIUM: pcfRadius = 2.0f; break;
        case ShadowQuality::HIGH:   pcfRadius = 3.0f; break;
    }

    fs_lighting.eyePos = Vector4(cameraTransform.worldPosition.x, cameraTransform.worldPosition.y, cameraTransform.worldPosition.z, 0.0);
    fs_lighting.cameraDir = Vector4(camera.worldDirection.x, camera.worldDirection.y, camera.worldDirection.z, pcfRadius);
    fs_lighting.globalIllum = Vector4(scene->getGlobalIlluminationColorLinear(), scene->getGlobalIlluminationIntensity());

    fs_lighting.envColor = Vector4(1.0, 1.0, 1.0, 0.0);
    if (hasIBL){
        auto skys = scene->getComponentArray<SkyComponent>();
        if (skys->size() > 0){
            SkyComponent& sky = skys->getComponentFromIndex(0);
            Vector3 linColor = Color::sRGBToLinear(Vector3(sky.color.x, sky.color.y, sky.color.z));
            fs_lighting.envColor = Vector4(linColor.x, linColor.y, linColor.z, Angle::defaultToRad(sky.rotation));
        }
    }

    // inverse viewport size, used by USE_SSAO to convert gl_FragCoord to a screen UV.
    // .z = SSAO debug flag (output raw AO); .w = rendering-flipped flag (the AO buffer
    // is in logical orientation, so flip Y on sample when the color pass is flipped).
    float vpW = Engine::getViewRect().getWidth();
    float vpH = Engine::getViewRect().getHeight();
    if (isFixedResolutionActive() && !camera.renderToTexture){
        // the main color pass renders into the fixed-resolution target
        vpW = (float)scene->getFixedResolutionWidth();
        vpH = (float)scene->getFixedResolutionHeight();
    }
    fs_lighting.viewportInfo = Vector4(
        (vpW > 0.0f) ? 1.0f / vpW : 0.0f,
        (vpH > 0.0f) ? 1.0f / vpH : 0.0f,
        scene->isSSAODebug() ? 1.0f : 0.0f,
        isRenderingFlipped(camera) ? 1.0f : 0.0f);

    // Setting intensity of other lights to zero
    for (int i = numLights; i < MAX_LIGHTS; i++){
        fs_lighting.color_intensity[i].w = 0.0;
        fs_lighting.spotUp_maskAspect[i].w = 0.0;
    }
}

bool RenderSystem::loadLights2D(){
    bool prevHasShadows2D = hasShadows2D;
    hasShadows2D = false;

    auto lights2d = scene->getComponentArray<Light2DComponent>();
    auto occluders = scene->getComponentArray<Occluder2DComponent>();

    bool anyOccluder = false;
    for (int i = 0; i < occluders->size(); i++){
        if (occluders->getComponentFromIndex(i).enabled){
            anyOccluder = true;
            break;
        }
    }

    int freeRow = 0;
    unsigned int maxResolution = 0;
    for (int i = 0; i < numLights2D; i++){
        Light2DComponent& light = lights2d->getComponentFromIndex(i);

        light.shadowMapIndex = -1;

        if (light.shadows && anyOccluder){
            light.shadowMapIndex = freeRow++;
            maxResolution = std::max(maxResolution, light.mapResolution);
        }
    }

    hasShadows2D = (freeRow > 0);

    if (hasShadows2D){
        if (!ensureShadow2DAtlas(maxResolution)){
            Log::warn("Failed to create 2D shadow atlas");
            for (int i = 0; i < numLights2D; i++){
                lights2d->getComponentFromIndex(i).shadowMapIndex = -1;
            }
            hasShadows2D = false;
        }
    }

    if (!hasShadows2D){
        if (hasShadow2DAtlas){
            needUpdateShadowBindings = true;
        }
        hasShadow2DAtlas = false;
    }

    // USE_SHADOWS_2D is baked into mesh shader variants, so a flip (first shadow
    // light + occluder appearing, or the last one going away) rebuilds meshes
    if (hasShadows2D != prevHasShadows2D){
        needReloadMeshes();
    }

    return true;
}

bool RenderSystem::ensureShadow2DAtlas(unsigned int width){
    int maxTextureSize = sg_query_limits().max_image_size_2d;
    if (maxTextureSize < 1){
        maxTextureSize = 4096;
    }
    if (width < 1){
        width = 1;
    }
    if ((int)width > maxTextureSize){
        Log::warn("2D shadow atlas width clamped from %u to %d (GPU max texture size)", width, maxTextureSize);
        width = (unsigned int)maxTextureSize;
    }

    if (shadow2DAtlasFramebuffer.isCreated() && shadow2DAtlasWidth == width){
        if (!hasShadow2DAtlas){
            needUpdateShadowBindings = true;
        }
        hasShadow2DAtlas = true;
        return true;
    }

    shadow2DAtlasFramebuffer.destroyFramebuffer();
    if (!shadow2DAtlasFramebuffer.createFramebuffer(
            TextureType::TEXTURE_2D, (int)width, MAX_LIGHTS_2D,
            TextureFilter::NEAREST, TextureFilter::NEAREST,
            TextureWrap::CLAMP_TO_EDGE, TextureWrap::CLAMP_TO_EDGE, true)){
        hasShadow2DAtlas = false;
        return false;
    }

    shadow2DAtlasWidth = width;
    hasShadow2DAtlas = true;
    needUpdateShadowBindings = true;
    return true;
}

void RenderSystem::processLights2D(){
    auto lights2d = scene->getComponentArray<Light2DComponent>();

    for (int i = 0; i < numLights2D; i++){
        Light2DComponent& light = lights2d->getComponentFromIndex(i);
        Entity entity = lights2d->getEntity(i);
        Transform* transform = scene->findComponent<Transform>(entity);

        Vector3 worldPosition;
        if (transform){
            worldPosition = Vector3(transform->worldPosition);
        }

        fs_lighting2d.position_range[i] = Vector4(worldPosition.x, worldPosition.y, light.height, std::max(light.range, 0.001f));
        fs_lighting2d.color_intensity[i] = Vector4(light.color.x, light.color.y, light.color.z, light.intensity);
        fs_lighting2d.falloff_shadow[i] = Vector4(light.falloff, (float)light.shadowMapIndex, light.shadowSoftness, light.shadowBias);
    }

    Vector3 ambient = scene->getAmbientLight2DColorLinear() * scene->getAmbientLight2DIntensity();
    fs_lighting2d.ambient = Vector4(ambient.x, ambient.y, ambient.z, (float)numLights2D);

    // PCF tap radius from the scene's 2D shadow quality (taps = 2*radius + 1)
    float pcfRadius = 2.0f;
    switch (scene->getShadowQuality()){
        case ShadowQuality::NONE:   pcfRadius = 0.0f; break;
        case ShadowQuality::LOW:    pcfRadius = 2.0f; break;
        case ShadowQuality::MEDIUM: pcfRadius = 4.0f; break;
        case ShadowQuality::HIGH:   pcfRadius = 6.0f; break;
    }

    // rows without shadows keep shadowMapIndex -1 and never sample the atlas
    float atlasW = (hasShadow2DAtlas && shadow2DAtlasWidth > 0) ? (float)shadow2DAtlasWidth : 1.0f;
    fs_lighting2d.atlasInfo = Vector4(1.0f / atlasW, 1.0f / (float)MAX_LIGHTS_2D, atlasW, pcfRadius);
}

bool RenderSystem::loadAndProcessFog(){

    FogComponent* fog = scene->findComponentFromIndex<FogComponent>(0);
    hasFog = false;
    if (fog){
        hasFog = true;

        int fogTypeI;
        if (fog->type == FogType::LINEAR){
            fogTypeI = 0;
        }else if (fog->type == FogType::EXPONENTIAL){
            fogTypeI = 1;
        }else if (fog->type == FogType::EXPONENTIALSQUARED){
            fogTypeI = 2;
        }

        fs_fog.color_type = Vector4(fog->color.x, fog->color.y, fog->color.z, fogTypeI);
        fs_fog.density_start_end = Vector4(fog->density, 0.0, fog->linearStart, fog->linearEnd);
    }

    return hasFog;
}

// pooled maps stay alive in TexturePool for reuse; only unpooled ones are destroyed
void RenderSystem::releaseSkyEnvironment(SkyComponent& sky){
    if (sky.irradianceMap && sky.irradianceMap.use_count() == 1){
        sky.irradianceMap->destroyTexture();
    }
    if (sky.prefilteredMap && sky.prefilteredMap.use_count() == 1){
        sky.prefilteredMap->destroyTexture();
    }
    sky.irradianceMap.reset();
    sky.prefilteredMap.reset();
    sky.envMapsLoaded = false;
}

// generates (or regenerates) the IBL environment maps from the sky texture,
// must run while the texture pixels are still on CPU (before the sky GPU upload)
void RenderSystem::updateSkyEnvironment(SkyComponent& sky){
    if (!sky.needUpdateEnvironment)
        return;

    if (sky.texture.empty() || sky.texture.getNumFaces() != 6){
        releaseSkyEnvironment(sky);
        sky.needUpdateEnvironment = false;
        return;
    }

    // generation is expensive (cosine + GGX convolution), reuse cached maps
    const std::string texId = sky.texture.getId();
    const std::string irradianceId = texId + "|ibl|irradiance";
    const std::string prefilteredId = texId + "|ibl|prefiltered";

    if (!texId.empty()){
        std::shared_ptr<TextureRender> irradiance = TexturePool::get(irradianceId);
        std::shared_ptr<TextureRender> prefiltered = TexturePool::get(prefilteredId);
        if (irradiance && prefiltered){
            releaseSkyEnvironment(sky);
            sky.irradianceMap = irradiance;
            sky.prefilteredMap = prefiltered;
            sky.envMapsLoaded = true;
            needReloadMeshes();
            sky.needUpdateEnvironment = false;
            return;
        }
    }

    TextureLoadResult texResult = sky.texture.load();

    if (texResult.state == ResourceLoadState::Loading){
        return; // try again next frame
    }

    if (texResult.state == ResourceLoadState::Finished && texResult.data){
        releaseSkyEnvironment(sky);

        std::shared_ptr<TextureRender> irradiance = std::make_shared<TextureRender>();
        std::shared_ptr<TextureRender> prefiltered = std::make_shared<TextureRender>();

        if (IBLEnvironment::generate(texId, *(texResult.data), *irradiance, *prefiltered)){
            sky.irradianceMap = irradiance;
            sky.prefilteredMap = prefiltered;
            sky.envMapsLoaded = true;
            TexturePool::add(irradianceId, irradiance);
            TexturePool::add(prefilteredId, prefiltered);
            needReloadMeshes();
        }
    }

    sky.needUpdateEnvironment = false;
}

void RenderSystem::releaseReflectionProbeMaps(ReflectionProbeRuntime& runtime){
    if (runtime.irradianceMap && runtime.irradianceMap.use_count() == 1)
        runtime.irradianceMap->destroyTexture();
    if (runtime.prefilteredMap && runtime.prefilteredMap.use_count() == 1)
        runtime.prefilteredMap->destroyTexture();
    runtime.irradianceMap.reset();
    runtime.prefilteredMap.reset();
}

void RenderSystem::destroyReflectionProbe(Entity entity, ReflectionProbeComponent& probe){
    auto it = reflectionProbeRuntimes.find(entity);
    if (it != reflectionProbeRuntimes.end()){
        it->second->captureFramebuffer.destroyFramebuffer();
        releaseReflectionProbeMaps(*it->second);
        reflectionProbeRuntimes.erase(it);
    }
    probe.texture.destroy();
    if (activeReflectionProbe == entity)
        activeReflectionProbe = NULL_ENTITY;
}

void RenderSystem::updateReflectionProbes(double dt){
    auto probes = scene->getComponentArray<ReflectionProbeComponent>();
    bool newHasReflectionProbes = probes->size() > 0;
    if (newHasReflectionProbes != hasReflectionProbes){
        hasReflectionProbes = newHasReflectionProbes;
        needReloadMeshes();
    }

    activeReflectionProbe = NULL_ENTITY;

    // Finish an already-started cube before scheduling another probe. This keeps
    // the one-face-per-frame budget without interleaving two partial cubemaps.
    for (int i = 0; i < probes->size(); i++){
        Entity entity = probes->getEntity(i);
        auto runtimeIt = reflectionProbeRuntimes.find(entity);
        if (runtimeIt != reflectionProbeRuntimes.end() && runtimeIt->second->captureInProgress &&
            probes->getComponentFromIndex(i).needUpdate && scene->findComponent<Transform>(entity)){
            activeReflectionProbe = entity;
            break;
        }
    }

    for (int i = 0; i < probes->size(); i++){
        Entity entity = probes->getEntity(i);
        ReflectionProbeComponent& probe = probes->getComponentFromIndex(i);
        Transform* transform = scene->findComponent<Transform>(entity);
        if (!transform)
            continue;

        auto& runtimePtr = reflectionProbeRuntimes[entity];
        if (!runtimePtr)
            runtimePtr = std::make_unique<ReflectionProbeRuntime>();
        ReflectionProbeRuntime& runtime = *runtimePtr;

        if (!runtime.modeInitialized){
            runtime.lastMode = probe.mode;
            runtime.modeInitialized = true;
        }else if (runtime.lastMode != probe.mode){
            if (activeReflectionProbe == entity)
                activeReflectionProbe = NULL_ENTITY;
            runtime.lastMode = probe.mode;
            runtime.captureInProgress = false;
            runtime.nextFace = 0;
            runtime.ready = false;
            runtime.elapsed = 0.0f;
            runtime.retryDelay = 0.0f;
            probe.needUpdate = true;
        }

        // Capture-affecting editor changes can arrive while a six-face job is
        // already running. Restart that job so no face uses the old settings.
        if (runtime.observedCaptureRevision != probe.captureRevision){
            if (activeReflectionProbe == entity)
                activeReflectionProbe = NULL_ENTITY;
            runtime.observedCaptureRevision = probe.captureRevision;
            runtime.captureInProgress = false;
            runtime.nextFace = 0;
        }

        auto scheduleCapture = [&](){
            if (activeReflectionProbe != NULL_ENTITY && activeReflectionProbe != entity)
                return;
            if (!runtime.captureInProgress){
                runtime.captureInProgress = true;
                // A finished cube keeps being sampled while its faces are
                // rewritten. Only the first capture leaves the probe unselectable.
                if (!runtime.captureFramebuffer.isCreated())
                    runtime.ready = false;
                runtime.nextFace = 0;
                // Freeze the entity origin for all six faces. boxOffset controls
                // only the influence volume and is intentionally not a capture offset.
                runtime.capturePosition = transform->worldPosition;
            }
            activeReflectionProbe = entity;
        };

        if (probe.mode == ReflectionProbeMode::STATIC){
            bool hasAuthoredCubemap = !probe.texture.empty() && probe.texture.getNumFaces() == 6;
            if (hasAuthoredCubemap && runtime.ready)
                runtime.capturedPosition = transform->worldPosition;

            if (!probe.needUpdate)
                continue;

            if (!Engine::isViewLoaded())
                continue;

            // An authored cubemap is the preferred static path because it can be
            // GGX-prefiltered once and cached. With no cubemap, capture the scene
            // once on load (or when Refresh is pressed) and then keep that result.
            if (!hasAuthoredCubemap){
                runtime.retryDelay = 0.0f;
                releaseReflectionProbeMaps(runtime);
                scheduleCapture();
                continue;
            }

            if (activeReflectionProbe == entity)
                activeReflectionProbe = NULL_ENTITY;
            runtime.captureInProgress = false;
            runtime.captureFramebuffer.destroyFramebuffer();
            runtime.resolution = 0;
            runtime.nextFace = 0;
            runtime.ready = false;
            releaseReflectionProbeMaps(runtime);

            const std::string textureId = probe.texture.getId();
            const std::string irradianceId = textureId + "|probe|irradiance";
            const std::string prefilteredId = textureId + "|probe|prefiltered";
            if (!textureId.empty()){
                runtime.irradianceMap = TexturePool::get(irradianceId);
                runtime.prefilteredMap = TexturePool::get(prefilteredId);
                if (runtime.irradianceMap && runtime.prefilteredMap){
                    runtime.ready = true;
                    runtime.capturedPosition = transform->worldPosition;
                    runtime.retryDelay = 0.0f;
                    probe.needUpdate = false;
                    continue;
                }
            }

            runtime.retryDelay = std::max(0.0f, runtime.retryDelay - (float)dt);
            if (runtime.retryDelay > 0.0f)
                continue;

            TextureLoadResult loadResult = probe.texture.load();
            if (loadResult.state == ResourceLoadState::Loading)
                continue;

            if (loadResult.state == ResourceLoadState::Finished && loadResult.data){
                auto irradiance = std::make_shared<TextureRender>();
                auto prefiltered = std::make_shared<TextureRender>();
                if (IBLEnvironment::generate(textureId, *loadResult.data, *irradiance, *prefiltered)){
                    runtime.irradianceMap = irradiance;
                    runtime.prefilteredMap = prefiltered;
                    runtime.ready = true;
                    runtime.capturedPosition = transform->worldPosition;
                    runtime.retryDelay = 0.0f;
                    probe.needUpdate = false;
                    if (!textureId.empty()){
                        TexturePool::add(irradianceId, irradiance);
                        TexturePool::add(prefilteredId, prefiltered);
                    }
                    continue;
                }
            }

            // Texture::load disables its own load latch after a failure. Re-arm
            // it and back off so a transient authored-bake failure is recoverable
            // without logging/retrying on every frame.
            if (loadResult.state == ResourceLoadState::Failed)
                probe.texture.retryLoad();
            runtime.retryDelay = 1.0f;
            continue;
        }

        // A component switched from an authored static probe to dynamic must not
        // keep selecting the stale prefiltered map while its live capture updates.
        releaseReflectionProbeMaps(runtime);
        runtime.retryDelay = 0.0f;
        runtime.elapsed += (float)dt;
        Vector3 capturePosition = transform->worldPosition;
        if (probe.updateMode == ReflectionProbeUpdateMode::ON_MOVE && runtime.ready){
            if ((capturePosition - runtime.capturedPosition).length() > 0.0001f)
                probe.needUpdate = true;
        }else if (probe.updateMode == ReflectionProbeUpdateMode::INTERVAL && runtime.ready){
            if (runtime.elapsed >= std::max(probe.updateInterval, 0.01f))
                probe.needUpdate = true;
        }

        if (probe.needUpdate)
            scheduleCapture();
    }
}

bool RenderSystem::selectReflectionProbe(const Vector3& worldPosition, fs_reflection_probe_t& params, TextureRender*& texture){
    params.position_weight = Vector4(0.0, 0.0, 0.0, 0.0);
    params.boxMin_intensity = Vector4(-1.0, -1.0, -1.0, 1.0);
    params.boxMax_lod = Vector4(1.0, 1.0, 1.0, 0.0);
    texture = &emptyCubeBlack;

    if (capturingReflectionProbe)
        return false;

    auto probes = scene->getComponentArray<ReflectionProbeComponent>();
    ReflectionProbeComponent* selected = nullptr;
    ReflectionProbeRuntime* selectedRuntime = nullptr;
    Vector3 selectedMin;
    Vector3 selectedMax;
    float selectedDistance = 0.0f;

    for (int i = 0; i < probes->size(); i++){
        Entity entity = probes->getEntity(i);
        ReflectionProbeComponent& probe = probes->getComponentFromIndex(i);
        Transform* transform = scene->findComponent<Transform>(entity);
        auto runtimeIt = reflectionProbeRuntimes.find(entity);
        if (!transform || runtimeIt == reflectionProbeRuntimes.end() || !runtimeIt->second->ready)
            continue;

        Vector3 center = transform->modelMatrix * probe.boxOffset;
        Vector3 half(
            std::abs(probe.boxSize.x * transform->worldScale.x) * 0.5f,
            std::abs(probe.boxSize.y * transform->worldScale.y) * 0.5f,
            std::abs(probe.boxSize.z * transform->worldScale.z) * 0.5f);
        Vector3 boxMin = center - half;
        Vector3 boxMax = center + half;
        if (worldPosition.x < boxMin.x || worldPosition.y < boxMin.y || worldPosition.z < boxMin.z ||
            worldPosition.x > boxMax.x || worldPosition.y > boxMax.y || worldPosition.z > boxMax.z)
            continue;

        float distance = (worldPosition - center).length();
        if (!selected || probe.priority > selected->priority ||
            (probe.priority == selected->priority && distance < selectedDistance)){
            selected = &probe;
            selectedRuntime = runtimeIt->second.get();
            selectedMin = boxMin;
            selectedMax = boxMax;
            selectedDistance = distance;
        }
    }

    if (!selected || !selectedRuntime)
        return false;

    bool hasPrefilteredMap = selectedRuntime->prefilteredMap && selectedRuntime->prefilteredMap->isCreated();
    if (hasPrefilteredMap){
        texture = selectedRuntime->prefilteredMap.get();
    }else{
        if (!selectedRuntime->captureFramebuffer.isCreated())
            return false;
        texture = &selectedRuntime->captureFramebuffer.getColorTexture();
    }

    float edgeDistance = std::min(
        std::min(worldPosition.x - selectedMin.x, selectedMax.x - worldPosition.x),
        std::min(std::min(worldPosition.y - selectedMin.y, selectedMax.y - worldPosition.y),
                 std::min(worldPosition.z - selectedMin.z, selectedMax.z - worldPosition.z)));
    Vector3 selectedHalf = (selectedMax - selectedMin) * 0.5f;
    float maxBlendDistance = std::max(0.0f, std::min(selectedHalf.x, std::min(selectedHalf.y, selectedHalf.z)));
    float blendDistance = std::min(std::max(selected->blendDistance, 0.0f), maxBlendDistance);
    float weight = blendDistance > 0.0001f
        ? std::max(0.0f, std::min(1.0f, edgeDistance / blendDistance))
        : 1.0f;

    params.position_weight = Vector4(selectedRuntime->capturedPosition.x, selectedRuntime->capturedPosition.y,
                                     selectedRuntime->capturedPosition.z, weight);
    params.boxMin_intensity = Vector4(selectedMin.x, selectedMin.y, selectedMin.z, std::max(selected->intensity, 0.0f));
    params.boxMax_lod = Vector4(selectedMax.x, selectedMax.y, selectedMax.z,
        hasPrefilteredMap ? (float)(IBLEnvironment::SPECULAR_MIPS - 1) : 0.0f);
    return true;
}

void RenderSystem::renderReflectionProbeCapture(){
    if (activeReflectionProbe == NULL_ENTITY || !scene->isEntityCreated(activeReflectionProbe))
        return;

    ReflectionProbeComponent* probe = scene->findComponent<ReflectionProbeComponent>(activeReflectionProbe);
    Transform* probeTransform = scene->findComponent<Transform>(activeReflectionProbe);
    auto runtimeIt = reflectionProbeRuntimes.find(activeReflectionProbe);
    if (!probe || !probeTransform || runtimeIt == reflectionProbeRuntimes.end())
        return;

    ReflectionProbeRuntime& runtime = *runtimeIt->second;
    if (!runtime.captureInProgress || !probe->needUpdate)
        return;
    unsigned int resolution = std::max(16u, std::min(probe->resolution, 1024u));
    if (!runtime.captureFramebuffer.isCreated() || runtime.resolution != resolution){
        runtime.captureFramebuffer.destroyFramebuffer();
        if (!runtime.captureFramebuffer.createFramebuffer(TextureType::TEXTURE_CUBE, (int)resolution, (int)resolution,
                TextureFilter::LINEAR, TextureFilter::LINEAR, TextureWrap::CLAMP_TO_EDGE, TextureWrap::CLAMP_TO_EDGE, false)){
            return;
        }
        runtime.resolution = resolution;
        runtime.nextFace = 0;
        runtime.ready = false;
    }

    static const Vector3 directions[6] = {
        Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0),
        Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)
    };
    static const Vector3 ups[6] = {
        Vector3(0, -1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1),
        Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, -1, 0)
    };

    int face = runtime.nextFace;
    Vector3 capturePosition = runtime.capturePosition;

    Transform captureTransform;
    captureTransform.position = capturePosition;
    captureTransform.worldPosition = capturePosition;
    captureTransform.worldScale = Vector3(1.0f, 1.0f, 1.0f);
    captureTransform.parent = NULL_ENTITY;

    CameraComponent captureCamera;
    captureCamera.type = CameraType::CAMERA_PERSPECTIVE;
    captureCamera.renderToTexture = true;
    // cube faces flip on the opposite backends from 2D RTT (see
    // isRenderingFlipped), so the matching winding is PIP_RTT_INVERT
    captureCamera.invertCulling = true;
    captureCamera.autoResize = false;
    captureCamera.useTarget = true;
    captureCamera.target = capturePosition + directions[face];
    captureCamera.up = ups[face];
    captureCamera.yfov = 1.57079632679f;
    captureCamera.aspect = 1.0f;
    captureCamera.nearClip = std::max(probe->nearClip, 0.001f);
    captureCamera.farClip = std::max(probe->farClip, captureCamera.nearClip + 0.001f);
    updateCamera(captureCamera, captureTransform);

    Vector4 previousEye = fs_lighting.eyePos;
    Vector4 previousCameraDir = fs_lighting.cameraDir;
    fs_lighting.eyePos = Vector4(capturePosition.x, capturePosition.y, capturePosition.z, 0.0f);
    fs_lighting.cameraDir = Vector4(captureCamera.worldDirection.x, captureCamera.worldDirection.y,
                                    captureCamera.worldDirection.z, previousCameraDir.w);

    capturingReflectionProbe = true;
    runtime.capturePass.setClearColor(probe->includeSky ? scene->getBackgroundColor() : Vector4(0.0f, 0.0f, 0.0f, 1.0f));
    runtime.capturePass.startRenderPass(&runtime.captureFramebuffer, (size_t)face);

    auto skys = scene->getComponentArray<SkyComponent>();
    if (probe->includeSky && skys->size() > 0){
        SkyComponent& sky = skys->getComponentFromIndex(0);
        if (sky.visible){
            updateSkyViewProjection(sky, captureCamera);
            drawSky(sky, true, true);
        }
    }

    auto transforms = scene->getComponentArray<Transform>();
    // Point/spot shadows are light-space and can be reused here. Directional
    // cascades are fitted to the main camera, so their coverage is a known
    // limitation until probe-specific directional shadow rendering is added.
    for (int i = 0; i < transforms->size(); i++){
        Entity entity = transforms->getEntity(i);
        if (entity == activeReflectionProbe)
            continue;
        Transform& transform = transforms->getComponentFromIndex(i);
        MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
        if (!mesh || !transform.visible || mesh->transparent || !mesh->renderInReflectionProbes)
            continue;

        updateMVP(i, transform, captureCamera, captureTransform);
        InstancedMeshComponent* instanced = scene->findComponent<InstancedMeshComponent>(entity);
        TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
        TilemapComponent* tilemap = scene->findComponent<TilemapComponent>(entity);
        drawMesh(*mesh, transform, captureCamera, captureTransform, true, instanced, terrain, tilemap, 0);
    }

    runtime.capturePass.endRenderPass();
    capturingReflectionProbe = false;
    fs_lighting.eyePos = previousEye;
    fs_lighting.cameraDir = previousCameraDir;

    delete captureCamera.framebuffer;
    captureCamera.framebuffer = nullptr;

    runtime.nextFace++;
    if (runtime.nextFace >= 6){
        runtime.nextFace = 0;
        runtime.ready = true;
        runtime.captureInProgress = false;
        runtime.elapsed = 0.0f;
        runtime.capturedPosition = capturePosition;
        probe->needUpdate = false;
        activeReflectionProbe = NULL_ENTITY;
    }
}

void RenderSystem::initShadowAtlasRects(){
    for (int s = 0; s < MAX_SHADOW_ATLAS_SLOTS; s++){
        if (s >= shadowAtlasUsedSlots || shadowAtlasCols < 1 || shadowAtlasRows < 1){
            fs_shadows.atlasRect[s] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
            continue;
        }

        int col = s % shadowAtlasCols;
        int row = s / shadowAtlasCols;
        fs_shadows.atlasRect[s] = Vector4(
            (float)col / (float)shadowAtlasCols,
            (float)row / (float)shadowAtlasRows,
            1.0f / (float)shadowAtlasCols,
            1.0f / (float)shadowAtlasRows);
    }
}

void RenderSystem::initShadowPointAtlasRects(){
    for (int s = 0; s < MAX_POINT_SHADOW_ATLAS_SLOTS; s++){
        int col = s % SHADOW_POINT_ATLAS_COLS;
        int row = s / SHADOW_POINT_ATLAS_COLS;
        fs_point_shadows.atlasRect[s] = Vector4(
            (float)col / (float)SHADOW_POINT_ATLAS_COLS,
            (float)row / (float)SHADOW_POINT_ATLAS_ROWS,
            1.0f / (float)SHADOW_POINT_ATLAS_COLS,
            1.0f / (float)SHADOW_POINT_ATLAS_ROWS);
    }
}

// Clamps a per-slot shadow resolution so the whole atlas (slotResolution * grid) stays
// within the GPU's max 2D texture size (sg_query_limits().max_image_size_2d). The atlas
// is square-tiled, so the limiting dimension is the larger grid axis. Returns the usable
// per-slot resolution (possibly reduced, logged once when clamped), or 0 when even a
// 1px slot cannot fit. This keeps shadow allocation succeeding at lower quality instead
// of failing outright on GPUs with small texture caps (e.g. 4096/8192).
unsigned int RenderSystem::clampShadowAtlasSlotResolution(unsigned int requestedResolution, int atlasCols, int atlasRows) const{
    if (requestedResolution < 1 || atlasCols < 1 || atlasRows < 1){
        return 0;
    }

    int maxTextureSize = sg_query_limits().max_image_size_2d;
    if (maxTextureSize < 1){
        maxTextureSize = 4096;
    }

    int gridMax = std::max(atlasCols, atlasRows);
    unsigned int maxSlotResolution = (unsigned int)(maxTextureSize / gridMax);
    if (maxSlotResolution < 1){
        Log::warn("Shadow atlas grid %dx%d does not fit in GPU max texture size %d", atlasCols, atlasRows, maxTextureSize);
        return 0;
    }

    if (requestedResolution > maxSlotResolution){
        Log::warn("Shadow atlas slot resolution clamped from %u to %u (GPU max texture %d, %dx%d grid)",
                  requestedResolution, maxSlotResolution, maxTextureSize, atlasCols, atlasRows);
        return maxSlotResolution;
    }

    return requestedResolution;
}

bool RenderSystem::ensureShadowAtlas(unsigned int slotResolution, int usedSlots){
    usedSlots = std::clamp(usedSlots, 1, MAX_SHADOW_ATLAS_SLOTS);

    // Smallest grid within the 3x3 capacity; equal-area layouts prefer more
    // columns so three cascades pack into a 3x1 row.
    int atlasCols = 1;
    int atlasRows = usedSlots;
    int bestArea = MAX_SHADOW_ATLAS_SLOTS + 1;
    for (int cols = 1; cols <= SHADOW_ATLAS_COLS; cols++){
        int rows = (usedSlots + cols - 1) / cols;
        if (rows > SHADOW_ATLAS_ROWS){
            continue;
        }

        int area = cols * rows;
        if (area < bestArea || (area == bestArea && cols > atlasCols)){
            atlasCols = cols;
            atlasRows = rows;
            bestArea = area;
        }
    }

    slotResolution = clampShadowAtlasSlotResolution(slotResolution, atlasCols, atlasRows);
    if (slotResolution < 1){
        return false;
    }

    int atlasWidth = (int)slotResolution * atlasCols;
    int atlasHeight = (int)slotResolution * atlasRows;

    if (shadowAtlasFramebuffer.isCreated() &&
        shadowAtlasSlotResolution == slotResolution &&
        shadowAtlasCols == atlasCols &&
        shadowAtlasRows == atlasRows &&
        shadowAtlasUsedSlots == usedSlots &&
        !needUpdateShadowAtlas){
        if (!hasShadowAtlas){
            needUpdateShadowBindings = true;
        }
        hasShadowAtlas = true;
        return true;
    }

    shadowAtlasFramebuffer.destroyFramebuffer();
    // LINEAR on a comparison sampler is hardware PCF: each tap is bilinearly filtered
    // over 2x2 comparison results, so every quality level gets softer edges for free.
    if (!shadowAtlasFramebuffer.createDepthOnlyFramebuffer(
            atlasWidth, atlasHeight,
            TextureFilter::LINEAR, TextureFilter::LINEAR,
            TextureWrap::CLAMP_TO_BORDER, TextureWrap::CLAMP_TO_BORDER, true)){
        shadowAtlasSlotResolution = 0;
        shadowAtlasCols = 0;
        shadowAtlasRows = 0;
        shadowAtlasUsedSlots = 0;
        initShadowAtlasRects();
        hasShadowAtlas = false;
        needUpdateShadowBindings = true;
        return false;
    }

    shadowAtlasSlotResolution = slotResolution;
    shadowAtlasCols = atlasCols;
    shadowAtlasRows = atlasRows;
    shadowAtlasUsedSlots = usedSlots;
    initShadowAtlasRects();
    needUpdateShadowAtlas = false;
    hasShadowAtlas = true;
    needUpdateShadowBindings = true;
    return true;
}

bool RenderSystem::ensureShadowPointAtlas(unsigned int slotResolution){
    slotResolution = clampShadowAtlasSlotResolution(slotResolution, SHADOW_POINT_ATLAS_COLS, SHADOW_POINT_ATLAS_ROWS);
    if (slotResolution < 1){
        return false;
    }

    int atlasWidth = (int)slotResolution * SHADOW_POINT_ATLAS_COLS;
    int atlasHeight = (int)slotResolution * SHADOW_POINT_ATLAS_ROWS;

    if (shadowPointAtlasFramebuffer.isCreated() && shadowPointAtlasSlotResolution == slotResolution && !needUpdateShadowPointAtlas){
        if (!hasShadowPointAtlas){
            needUpdateShadowBindings = true;
        }
        hasShadowPointAtlas = true;
        return true;
    }

    shadowPointAtlasFramebuffer.destroyFramebuffer();
    if (!shadowPointAtlasFramebuffer.createFramebuffer(
            TextureType::TEXTURE_2D, atlasWidth, atlasHeight,
            TextureFilter::NEAREST, TextureFilter::NEAREST,
            TextureWrap::CLAMP_TO_BORDER, TextureWrap::CLAMP_TO_BORDER, true)){
        hasShadowPointAtlas = false;
        return false;
    }

    shadowPointAtlasSlotResolution = slotResolution;
    needUpdateShadowPointAtlas = false;
    hasShadowPointAtlas = true;
    needUpdateShadowBindings = true;
    return true;
}

Rect RenderSystem::getShadowAtlasSlotRect(int slotIndex) const{
    if (shadowAtlasCols < 1 || slotIndex < 0 || slotIndex >= shadowAtlasUsedSlots){
        return Rect();
    }

    int col = slotIndex % shadowAtlasCols;
    int row = slotIndex / shadowAtlasCols;
    float x = (float)(col * (int)shadowAtlasSlotResolution);
    float y = (float)(row * (int)shadowAtlasSlotResolution);
    return Rect(x, y, (float)shadowAtlasSlotResolution, (float)shadowAtlasSlotResolution);
}

Rect RenderSystem::getShadowPointAtlasSlotRect(int slotIndex) const{
    int col = slotIndex % SHADOW_POINT_ATLAS_COLS;
    int row = slotIndex / SHADOW_POINT_ATLAS_COLS;
    float x = (float)(col * (int)shadowPointAtlasSlotResolution);
    float y = (float)(row * (int)shadowPointAtlasSlotResolution);
    return Rect(x, y, (float)shadowPointAtlasSlotResolution, (float)shadowPointAtlasSlotResolution);
}

bool RenderSystem::checkPBRFrabebufferUpdate(Material& material){
    return (
        material.baseColorTexture.isFramebufferOutdated() ||
        material.metallicRoughnessTexture.isFramebufferOutdated() ||
        material.normalTexture.isFramebufferOutdated() ||
        material.occlusionTexture.isFramebufferOutdated() ||
        material.emissiveTexture.isFramebufferOutdated() );
}

bool RenderSystem::checkPBRTextures(Material& material, bool receiveLights){
    bool hasBaseColor = !material.baseColorTexture.empty();

    bool hasOtherPBRTextures = false;
    if (hasLights && receiveLights) {
        hasOtherPBRTextures = !material.metallicRoughnessTexture.empty() ||
                              !material.normalTexture.empty() ||
                              !material.occlusionTexture.empty() ||
                              !material.emissiveTexture.empty();
    } else if (hasLights2D && receiveLights) {
        // the 2D light path samples only the normal map from the PBR set
        hasOtherPBRTextures = !material.normalTexture.empty();
    }

    return hasBaseColor || hasOtherPBRTextures;
}

bool RenderSystem::loadPBRTextures(Material& material, ShaderData& shaderData, ObjectRender& render, bool receiveLights){
    TextureRender* textureRender = NULL;
    std::pair<int, int> slotTex(-1, -1);

    textureRender = material.baseColorTexture.getRender(&emptyWhite);
    slotTex = shaderData.getTextureIndex(TextureShaderType::BASECOLOR);
    if (textureRender){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
    }else{
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
    }

    if (hasLights && receiveLights){
        textureRender = material.metallicRoughnessTexture.getRender(&emptyWhite);
        slotTex = shaderData.getTextureIndex(TextureShaderType::METALLICROUGHNESS);
        if (textureRender){
            if (!textureRender->isCreated()){
                return false;
            }
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
        }else{
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
        }

        textureRender = material.occlusionTexture.getRender(&emptyWhite);
        slotTex = shaderData.getTextureIndex(TextureShaderType::OCCULSION);
        if (textureRender){
            if (!textureRender->isCreated()){
                return false;
            }
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
        }else{
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
        }

        // A missing emissive texture must default to white (glTF: emissive =
        // emissiveFactor * emissiveTexture, absent texture = 1,1,1,1). The shader
        // unconditionally multiplies emissiveFactor by this sample for UV meshes, so a
        // black fallback would silently zero out emissiveFactor when no texture is set.
        textureRender = material.emissiveTexture.getRender(&emptyWhite);
        slotTex = shaderData.getTextureIndex(TextureShaderType::EMISSIVE);
        if (textureRender){
            if (!textureRender->isCreated()){
                return false;
            }
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
        }else{
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
        }
    }

    // normal map is sampled by both the 3D lit path and the 2D light path (which
    // otherwise keeps the unlit shader); addTexture no-ops when the compiled
    // variant has no u_normalTexture slot
    if ((hasLights || hasLights2D) && receiveLights){
        textureRender = material.normalTexture.getRender(&emptyNormal);
        slotTex = shaderData.getTextureIndex(TextureShaderType::NORMAL);
        if (textureRender){
            if (!textureRender->isCreated()){
                return false;
            }
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
        }else{
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyNormal);
        }
    }

    if (hasLights && receiveLights){
        loadSpotMaskTexture(shaderData, render);
    }

    return true;
}

void RenderSystem::updateShadowBindings(){
    if (!needUpdateShadowBindings){
        return;
    }

    auto meshes = scene->getComponentArray<MeshComponent>();
    for (int i = 0; i < meshes->size(); i++){
        MeshComponent& mesh = meshes->getComponentFromIndex(i);
        if (!mesh.loaded || mesh.needReload){
            continue;
        }

        for (int s = 0; s < mesh.numSubmeshes; s++){
            bool has3DShadows = mesh.submeshes[s].shaderProperties & (1u << 4);  // 'Shw'
            bool has2DShadows = mesh.submeshes[s].shaderProperties & (1u << 23); // 'S2d'
            if (!has3DShadows && !has2DShadows){
                continue;
            }
            if (!mesh.submeshes[s].shader){
                continue;
            }

            ShaderData& shaderData = mesh.submeshes[s].shader.get()->shaderData;
            if (has3DShadows){
                loadShadowTextures(shaderData, mesh.submeshes[s].render, mesh.receiveLights, true);
            }
            if (has2DShadows){
                loadShadow2DTexture(shaderData, mesh.submeshes[s].render, true);
            }
        }
    }

    needUpdateShadowBindings = false;
}

void RenderSystem::loadShadowTextures(ShaderData& shaderData, ObjectRender& render, bool receiveLights, bool receiveShadows){
    std::pair<int, int> slotTex(-1, -1);

    if (hasLights && receiveLights && hasShadows && receiveShadows){
        // Bind a depth/comparison fallback when the projective atlas is absent.
        slotTex = shaderData.getTextureIndex(TextureShaderType::SHADOWATLAS);
        render.addTexture(slotTex, ShaderStageType::FRAGMENT,
                          hasShadowAtlas ? &shadowAtlasFramebuffer.getDepthTexture() : &emptyShadowDepth);

        slotTex = shaderData.getTextureIndex(TextureShaderType::SHADOWPOINTATLAS);
        render.addTexture(slotTex, ShaderStageType::FRAGMENT,
                          hasShadowPointAtlas ? &shadowPointAtlasFramebuffer.getColorTexture() : &emptyWhite);
    }
}

void RenderSystem::loadShadow2DTexture(ShaderData& shaderData, ObjectRender& render, bool receiveShadows2D){
    if (receiveShadows2D){
        // same white fallback as the 3D atlases: decoded depth ~1.0 = unoccluded
        std::pair<int, int> slotTex = shaderData.getTextureIndex(TextureShaderType::SHADOW2DATLAS);
        render.addTexture(slotTex, ShaderStageType::FRAGMENT,
                          hasShadow2DAtlas ? &shadow2DAtlasFramebuffer.getColorTexture() : &emptyWhite);
    }
}

// Merges every enabled Occluder2D into occluder2DSegments as a world-space line
// list: each vertex carries its own endpoint (POSITION) plus the segment's other
// endpoint (TEXCOORD1), which the shadow2d vertex shader needs to unwrap segments
// crossing the polar seam. Returns the vertex count.
unsigned int RenderSystem::buildOccluder2DSegments(){
    occluder2DSegments.clear();

    auto occluders = scene->getComponentArray<Occluder2DComponent>();

    std::vector<Vector2> worldPoints;
    for (int i = 0; i < occluders->size(); i++){
        Occluder2DComponent& occluder = occluders->getComponentFromIndex(i);
        if (!occluder.enabled){
            continue;
        }
        Entity entity = occluders->getEntity(i);
        Transform* transform = scene->findComponent<Transform>(entity);
        if (!transform || !transform->visible){
            continue;
        }

        worldPoints.clear();
        bool closed = true;

        if (occluder.shape == Occluder2DShape::AUTO_QUAD){
            MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
            if (!mesh || mesh->aabb == AABB::ZERO){
                continue;
            }
            Vector3 mn = mesh->aabb.getMinimum();
            Vector3 mx = mesh->aabb.getMaximum();
            worldPoints.push_back(Vector2(mn.x, mn.y));
            worldPoints.push_back(Vector2(mx.x, mn.y));
            worldPoints.push_back(Vector2(mx.x, mx.y));
            worldPoints.push_back(Vector2(mn.x, mx.y));
        }else{
            worldPoints.assign(occluder.points.begin(), occluder.points.end());
            closed = occluder.closed;
        }

        size_t numPoints = worldPoints.size();
        if (numPoints < 2){
            continue;
        }

        for (size_t p = 0; p < numPoints; p++){
            Vector3 world = transform->modelMatrix * Vector3(worldPoints[p].x, worldPoints[p].y, 0.0f);
            worldPoints[p] = Vector2(world.x, world.y);
        }

        size_t numSegments = closed ? numPoints : (numPoints - 1);
        for (size_t s = 0; s < numSegments; s++){
            const Vector2& a = worldPoints[s];
            const Vector2& b = worldPoints[(s + 1) % numPoints];

            occluder2DSegments.push_back(a.x);
            occluder2DSegments.push_back(a.y);
            occluder2DSegments.push_back(b.x);
            occluder2DSegments.push_back(b.y);

            occluder2DSegments.push_back(b.x);
            occluder2DSegments.push_back(b.y);
            occluder2DSegments.push_back(a.x);
            occluder2DSegments.push_back(a.y);
        }
    }

    return (unsigned int)(occluder2DSegments.size() / 4);
}

bool RenderSystem::loadOccluder2DPass(unsigned int vertexCapacity){
    if (!Engine::isViewLoaded())
        return false;

    destroyOccluder2DPass(); // also called when growing the buffer capacity

    ObjectRender& render = occluder2DRender;

    render.beginLoad(PrimitiveType::LINES);

    shadow2DShader = ShaderPool::get(ShaderType::SHADOW2D, 0);
    if (!shadow2DShader->isCreated())
        return false;
    render.setShader(shadow2DShader.get());
    ShaderData& shaderData = shadow2DShader.get()->shaderData;

    shadow2DSlotParams = shaderData.getUniformBlockIndex(UniformBlockType::SHADOW2D_VS_PARAMS);

    occluder2DBuffer.clear();
    occluder2DBuffer.addAttribute(AttributeType::POSITION, 2, 0);
    occluder2DBuffer.addAttribute(AttributeType::TEXCOORD1, 2, 2 * sizeof(float));
    occluder2DBuffer.setStride(4 * sizeof(float));
    occluder2DBuffer.setRenderAttributes(true);
    occluder2DBuffer.setUsage(BufferUsage::STREAM);

    size_t bufferSize = vertexCapacity * occluder2DBuffer.getStride();
    if (bufferSize == 0)
        return false;

    occluder2DBuffer.getRender()->createBuffer(bufferSize, occluder2DBuffer.getData(), occluder2DBuffer.getType(), occluder2DBuffer.getUsage());
    for (auto const &attr : occluder2DBuffer.getAttributes()) {
        render.addAttribute(shaderData.getAttrIndex(attr.first), occluder2DBuffer.getRender(), attr.second.getElements(), attr.second.getDataType(), occluder2DBuffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
    }

    if (!render.endLoad(PIP_DEPTH, false, true, CullingMode::BACK, WindingOrder::CCW)){
        return false;
    }

    occluder2DBufferCapacity = vertexCapacity;
    occluder2DLoaded = true;
    return true;
}

void RenderSystem::destroyOccluder2DPass(){
    if (!occluder2DLoaded)
        return;

    if (shadow2DShader){
        shadow2DShader.reset();
        ShaderPool::remove(ShaderType::SHADOW2D, 0);
    }

    occluder2DRender.destroy();

    occluder2DBuffer.clearAll();
    occluder2DBuffer.getRender()->destroyBuffer();

    shadow2DSlotParams = -1;
    occluder2DBufferCapacity = 0;
    occluder2DLoaded = false;
}

bool RenderSystem::loadDepthTexture(Material& material, ShaderData& shaderData, ObjectRender& render){
    TextureRender* textureDepthRender = material.baseColorTexture.getRender(&emptyWhite);
    std::pair<int, int> slotTex = shaderData.getTextureIndex(TextureShaderType::DEPTHTEXTURE);
    if (textureDepthRender){
        if (!textureDepthRender->isCreated()){
            return false;
        }
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureDepthRender);
    }else{
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
    }

    return true;
}

bool RenderSystem::loadGBufferTextures(Material& material, ShaderData& shaderData, ObjectRender& render){
    // base color (albedo + alpha cutout); bound only when the gbuffer variant declares it
    std::pair<int, int> slotBase = shaderData.getTextureIndex(TextureShaderType::BASECOLOR);
    if (slotBase.first != -1){
        TextureRender* t = material.baseColorTexture.getRender(&emptyWhite);
        if (!t || !t->isCreated()){
            return false;
        }
        render.addTexture(slotBase, ShaderStageType::FRAGMENT, t);
    }

    // metallic-roughness (per-pixel roughness/metallic)
    std::pair<int, int> slotMR = shaderData.getTextureIndex(TextureShaderType::METALLICROUGHNESS);
    if (slotMR.first != -1){
        TextureRender* t = material.metallicRoughnessTexture.getRender(&emptyWhite);
        if (!t || !t->isCreated()){
            return false;
        }
        render.addTexture(slotMR, ShaderStageType::FRAGMENT, t);
    }

    return true;
}

bool RenderSystem::loadTerrainTextures(TerrainComponent& terrain, ObjectRender& render, ShaderData& shaderData){
    TextureRender* textureRender = NULL;
    std::pair<int, int> slotTex(-1, -1);

    // The heightmap and the blend map span the whole 0..1 range, so their borders must read
    // the border texels, not the REPEAT blend with the opposite edge (detail maps are tiled).
    terrain.heightMap.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    terrain.heightMap.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    terrain.blendMap.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    terrain.blendMap.setWrapV(TextureWrap::CLAMP_TO_EDGE);

    textureRender = terrain.heightMap.getRender(&emptyWhite);
    slotTex = shaderData.getTextureIndex(TextureShaderType::HEIGHTMAP);
    if (textureRender){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(slotTex, ShaderStageType::VERTEX, textureRender);
    }else{
        render.addTexture(slotTex, ShaderStageType::VERTEX, &emptyWhite);
    }

    textureRender = terrain.blendMap.getRender(&emptyBlack);
    slotTex = shaderData.getTextureIndex(TextureShaderType::BLENDMAP);
    if (textureRender){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
    }else{
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyBlack);
    }

    textureRender = terrain.textureDetailRed.getRender(&emptyWhite);
    slotTex = shaderData.getTextureIndex(TextureShaderType::TERRAINDETAIL_RED);
    if (textureRender){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
    }else{
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
    }

    textureRender = terrain.textureDetailGreen.getRender(&emptyWhite);
    slotTex = shaderData.getTextureIndex(TextureShaderType::TERRAINDETAIL_GREEN);
    if (textureRender){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
    }else{
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
    }

    textureRender = terrain.textureDetailBlue.getRender(&emptyWhite);
    slotTex = shaderData.getTextureIndex(TextureShaderType::TERRAINDETAIL_BLUE);
    if (textureRender){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
    }else{
        render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyWhite);
    }

    return true;
}

bool RenderSystem::loadTerrainHeightTexture(TerrainComponent& terrain, ObjectRender& render, ShaderData& shaderData){
    std::pair<int, int> slotTex = shaderData.getTextureIndex(TextureShaderType::HEIGHTMAP);
    if (slotTex.first == -1){
        return true;
    }

    TextureRender* textureRender = terrain.heightMap.getRender(&emptyWhite);
    if (!textureRender || !textureRender->isCreated()){
        return false;
    }

    render.addTexture(slotTex, ShaderStageType::VERTEX, textureRender);
    return true;
}

bool RenderSystem::updateTerrainRenderTextures(TerrainComponent& terrain, MeshComponent& mesh){
    if (!terrain.needUpdateTexture){
        return true;
    }

    bool texLoaded = true;
    for (int s = 0; s < 2; s++){
        if (s >= mesh.numSubmeshes){
            break;
        }
        if (!mesh.submeshes[s].shader){
            texLoaded = false;
            continue;
        }
        ShaderData& shaderData = mesh.submeshes[s].shader.get()->shaderData;
        if (!loadTerrainTextures(terrain, mesh.submeshes[s].render, shaderData)){
            texLoaded = false;
        }
        if (mesh.submeshes[s].depthShader){
            ShaderData& depthShaderData = mesh.submeshes[s].depthShader.get()->shaderData;
            if (!loadTerrainHeightTexture(terrain, mesh.submeshes[s].depthRender, depthShaderData)){
                texLoaded = false;
            }
        }
        if (mesh.submeshes[s].gbufferShader){
            ShaderData& gbufferShaderData = mesh.submeshes[s].gbufferShader.get()->shaderData;
            if (!loadTerrainHeightTexture(terrain, mesh.submeshes[s].gbufferRender, gbufferShaderData)){
                texLoaded = false;
            }
        }
    }

    if (texLoaded){
        terrain.needUpdateTexture = false;
    }

    return texLoaded;
}

void RenderSystem::updateAllTerrainRenderTextures(){
    auto meshes = scene->getComponentArray<MeshComponent>();
    for (int i = 0; i < meshes->size(); i++){
        MeshComponent& mesh = meshes->getComponentFromIndex(i);
        if (!mesh.loaded || mesh.needReload){
            continue;
        }

        Entity entity = meshes->getEntity(i);
        TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
        if (terrain){
            updateTerrainRenderTextures(*terrain, mesh);
        }
    }
}

void RenderSystem::setDisableFaceCulling(bool disableFaceCulling){
    if (this->disableFaceCulling == disableFaceCulling){
        return;
    }
    this->disableFaceCulling = disableFaceCulling;

    // Cull mode is baked into the pipeline at creation time, so every mesh must be
    // reloaded for the override to take effect (editor-only debug view).
    needReloadMeshes();
}

void RenderSystem::setDisableFixedResolution(bool disableFixedResolution){
    // No reload needed: the editor always renders through Engine::getFramebuffer(),
    // so the PIP_RTT pipeline is already baked whether fixed resolution is on or off.
    this->disableFixedResolution = disableFixedResolution;
}

bool RenderSystem::loadMesh(Entity entity, MeshComponent& mesh, uint8_t pipelines, InstancedMeshComponent* instmesh, TerrainComponent* terrain){

    if (!Engine::isViewLoaded()) 
        return false;

    if (terrain && !terrain->heightMapLoaded)
        return false;

    if (terrain && instmesh){
        Log::warn("Terrain cannot be an InstancedMesh");
    }

    if (mesh.autoTransparency){
        // Recompute from every current submesh below. This also clears a stale
        // transparent flag when a model is reloaded with OPAQUE/MASK materials.
        mesh.transparent = false;
    }

    std::map<std::string, Buffer*> buffers;
    std::map<std::string, BufferRender*> bufferNameToRender;
    bool allBuffersEmpty = true;

    if (mesh.buffer.getSize() > 0){
        buffers["vertices"] = &mesh.buffer;
        allBuffersEmpty = false;
        if (mesh.buffer.getUsage() != BufferUsage::IMMUTABLE){
            mesh.needUpdateBuffer = true;
        }
    }
    if (mesh.indices.getSize() > 0){
        buffers["indices"] = &mesh.indices;
        allBuffersEmpty = false;
        if (mesh.indices.getUsage() != BufferUsage::IMMUTABLE){
            mesh.needUpdateBuffer = true;
        }
    }
    for (int i = 0; i < mesh.numExternalBuffers; i++){
        buffers[mesh.eBuffers[i].getName()] = &mesh.eBuffers[i];
        allBuffersEmpty = false;
        if (mesh.eBuffers[i].getUsage() != BufferUsage::IMMUTABLE){
            mesh.needUpdateBuffer = true;
        }
    }

    if (mesh.vertexCount == 0){
        mesh.vertexCount = mesh.buffer.getCount();
    }

    if (allBuffersEmpty) {
        // Multi-node glTF models use their root entity only as a hierarchy
        // container; renderable geometry lives in child MeshComponents. Treat
        // that empty root as successfully loaded so one-time preview scenes can
        // complete instead of waiting forever in RenderSystem::isAllLoaded().
        ModelComponent* model = scene->findComponent<ModelComponent>(entity);
        if (model && !model->meshNodesMapping.empty() && mesh.numSubmeshes == 0) {
            mesh.needReload = false;
            mesh.needUpdateAABB = true;
            mesh.loadCalled = true;
            SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});
            return true;
        }
        return false;
    }

    std::map<std::string, unsigned int> bufferStride;

    std::vector<BufferRender*> buffersCreatedThisLoad;
    for (auto const& buf : buffers){
        BufferRender* bufferRender = buf.second->getRender();

        // A GPU buffer keeps for life the usage it was created with, so a generator
        // that turns its buffer dynamic after the mesh was first loaded from
        // serialized data (a tilemap rebuilding its indices) needs the buffer
        // recreated — updating an immutable one is rejected by the backend.
        if (bufferRender->isCreated() && bufferRender->getCreatedUsage() != buf.second->getUsage()) {
            bufferRender->destroyBuffer();
        }

        if (!bufferRender->isCreated()) {
            if (!bufferRender->createBuffer(buf.second->getSize(), buf.second->getData(), buf.second->getType(), buf.second->getUsage())) {
                Log::error("Cannot create GPU buffer '%s' (%zu bytes) for mesh entity %lu; resource pool may be exhausted",
                           buf.first.c_str(), buf.second->getSize(), entity);
                for (BufferRender* created : buffersCreatedThisLoad) {
                    created->destroyBuffer();
                }
                return false;
            }
            buffersCreatedThisLoad.push_back(bufferRender);
        }
        bufferNameToRender[buf.first] = bufferRender;
        bufferStride[buf.first] = buf.second->getStride();
    }

    if (instmesh){
        // Now buffer size is zero than it needed to be calculated
        size_t bufferSize = instmesh->maxInstances * instmesh->buffer.getStride();
        instmesh->buffer.getRender()->createBuffer(bufferSize, instmesh->buffer.getData(), instmesh->buffer.getType(), instmesh->buffer.getUsage());

        instmesh->needUpdateBuffer = true;
    }

    if (terrain){
        for (int v = 0; v < MAX_TERRAIN_VIEWS; v++){
            for (int s = 0; s < 2; s++){
                size_t bufferSize = terrain->nodes.size() * terrain->views[v].nodesbuffer[s].getStride();
                terrain->views[v].nodesbuffer[s].getRender()->createBuffer(bufferSize, terrain->views[v].nodesbuffer[s].getData(), terrain->views[v].nodesbuffer[s].getType(), terrain->views[v].nodesbuffer[s].getUsage());
            }
        }

        // using same material in both terrain submeshes
        mesh.submeshes[1].material = mesh.submeshes[0].material;
    }

    for (int i = 0; i < mesh.numSubmeshes; i++){

        ObjectRender& render = mesh.submeshes[i].render;

        // These flags are derived from the current buffers/attributes below. Reset all
        // of them before rescanning: model reloads can replace and reorder submeshes,
        // so retaining a flag from the previous slot may select a shader attribute
        // that the rebuilt submesh does not provide.
        mesh.submeshes[i].hasTexCoord1 = false;
        mesh.submeshes[i].hasTexCoord2 = false;
        mesh.submeshes[i].hasNormal = false;
        mesh.submeshes[i].hasNormalMap = false;
        mesh.submeshes[i].hasTangent = false;
        mesh.submeshes[i].hasVertexColor3 = false;
        mesh.submeshes[i].hasVertexColor4 = false;
        mesh.submeshes[i].hasSkinning = false;
        mesh.submeshes[i].hasMorphTarget = false;
        mesh.submeshes[i].hasMorphNormal = false;
        mesh.submeshes[i].hasMorphTangent = false;

        render.beginLoad(mesh.submeshes[i].primitiveType);

        bool hasPBRTextures = checkPBRTextures(mesh.submeshes[i].material, mesh.receiveLights);

        for (auto const& buf : buffers){
            if (buf.second->isRenderAttributes()) {
                for (auto const &attr : buf.second->getAttributes()) {
                    if (attr.first == AttributeType::TEXCOORD1){
                        mesh.submeshes[i].hasTexCoord1 = true;
                    }
                    if (attr.first == AttributeType::TEXCOORD2){
                        mesh.submeshes[i].hasTexCoord2 = true;
                    }
                    if (attr.first == AttributeType::NORMAL){
                        mesh.submeshes[i].hasNormal = true;
                    }
                    if (attr.first == AttributeType::TANGENT){
                        mesh.submeshes[i].hasTangent = true;
                    }
                    if (attr.first == AttributeType::COLOR){
                        if (attr.second.getElements() == 3){
                            mesh.submeshes[i].hasVertexColor3 = true;
                        }else{
                            mesh.submeshes[i].hasVertexColor4 = true;
                        }
                    }
                    if (attr.first == AttributeType::BONEIDS || attr.first == AttributeType::BONEWEIGHTS){
                        mesh.submeshes[i].hasSkinning = true;
                    }
                    if (attr.first == AttributeType::MORPHTARGET0){
                        mesh.submeshes[i].hasMorphTarget = true;
                    }
                    if (attr.first == AttributeType::MORPHNORMAL0){
                        mesh.submeshes[i].hasMorphNormal = true;
                    }
                    if (attr.first == AttributeType::MORPHTANGENT0){
                        mesh.submeshes[i].hasMorphTangent = true;
                    }
                }
            }
        }
        for (auto const& attr : mesh.submeshes[i].attributes){
            if (attr.first == AttributeType::TEXCOORD1){
                mesh.submeshes[i].hasTexCoord1 = true;
            }
            if (attr.first == AttributeType::TEXCOORD2){
                mesh.submeshes[i].hasTexCoord2 = true;
            }
            if (attr.first == AttributeType::NORMAL){
                mesh.submeshes[i].hasNormal = true;
            }
            if (attr.first == AttributeType::TANGENT){
                mesh.submeshes[i].hasTangent = true;
            }
            if (attr.first == AttributeType::COLOR){
                if (attr.second.getElements() == 3){
                    mesh.submeshes[i].hasVertexColor3 = true;
                }else{
                    mesh.submeshes[i].hasVertexColor4 = true;
                }
            }
            if (attr.first == AttributeType::BONEIDS || attr.first == AttributeType::BONEWEIGHTS){
                mesh.submeshes[i].hasSkinning = true;
            }
            if (attr.first == AttributeType::MORPHTARGET0){
                mesh.submeshes[i].hasMorphTarget = true;
            }
            if (attr.first == AttributeType::MORPHNORMAL0){
                mesh.submeshes[i].hasMorphNormal = true;
            }
            if (attr.first == AttributeType::MORPHTANGENT0){
                mesh.submeshes[i].hasMorphTangent = true;
            }
        }

        if (!mesh.submeshes[i].material.normalTexture.empty()){
            mesh.submeshes[i].hasNormalMap = true;
        }

        bool p_unlit = false;
        bool p_punctual = false;
        bool p_ibl = false;
        bool p_mirror = (scene->findComponent<MirrorComponent>(entity) != NULL);
        bool p_hasTexture1 = false;
        bool p_hasTexture2 = false;
        bool p_hasNormalMap = false;
        bool p_hasNormal = false;
        bool p_hasTangent = false;
        bool p_receiveShadows = false;

        if (mesh.submeshes[i].hasTexCoord1 && hasPBRTextures){
            p_hasTexture1 = true;
        }
        // second UV set: only when both UV attributes and PBR textures are present
        // (never for terrain, which generates its UVs in the shader)
        if (mesh.submeshes[i].hasTexCoord1 && mesh.submeshes[i].hasTexCoord2 && hasPBRTextures){
            p_hasTexture2 = true;
        }
        if (terrain && (!terrain->blendMap.empty() || hasPBRTextures)){
            p_hasTexture1 = true;
        }
        bool useIBL = (hasIBL || hasReflectionProbes) && mesh.receiveIBL;
        if ((hasLights || useIBL) && mesh.receiveLights){
            p_punctual = hasLights;
            p_ibl = useIBL;

            p_hasNormal = true;
            if (mesh.submeshes[i].hasTangent){
                p_hasTangent = true;
            }
            if (mesh.submeshes[i].hasNormalMap){
                p_hasNormalMap = true;
            }
            if (hasShadows && mesh.receiveShadows){
                p_receiveShadows = true;
            }
        }else{
            p_unlit = true;
        }

        // dedicated 2D light path: composes with MATERIAL_UNLIT (sprites keep the
        // cheap unlit shader; accumulated 2D light multiplies the base color) and
        // with the lit path in mixed scenes (contributions add)
        bool useLight2D = hasLights2D && mesh.receiveLights;
        bool p_light2d = useLight2D;
        bool p_shadows2d = useLight2D && hasShadows2D && mesh.receiveShadows;
        if (useLight2D){
            p_hasNormal = true;
            if (mesh.submeshes[i].hasTangent){
                p_hasTangent = true;
            }
            if (mesh.submeshes[i].hasNormalMap){
                p_hasNormalMap = true;
            }
        }

        // remember whether this submesh's lit shader applies IBL specular, so the SSR
        // G-buffer can flag those pixels for the energy-conserving composite
        mesh.submeshes[i].hasIBL = p_ibl;

        // screen-space AO modulates ambient/indirect light on lit submeshes.
        bool p_ssao = scene->isSSAOEnabled() && !p_unlit && (p_punctual || p_ibl);
        bool p_alphaMask = mesh.submeshes[i].material.alphaMode == MaterialAlphaMode::MASK;
        bool p_alphaOpaque = mesh.submeshes[i].material.alphaMode == MaterialAlphaMode::ALPHA_OPAQUE;
        // AUTO + textureShadow preserves the legacy opt-in for editor-created
        // materials; explicit glTF modes derive the depth behavior from alphaMode.
        bool p_depthAlphaMask = usesAlphaMask(mesh.submeshes[i].material, mesh.submeshes[i].textureShadow);
        bool p_depthTexture = p_depthAlphaMask && mesh.submeshes[i].hasTexCoord1 &&
            !mesh.submeshes[i].material.baseColorTexture.empty();

        mesh.submeshes[i].shaderProperties = ShaderPool::getMeshProperties(
                        p_unlit, p_hasTexture1, p_hasTexture2, p_punctual,
                        p_receiveShadows, p_hasNormal, p_hasNormalMap,
                        p_hasTangent, mesh.submeshes[i].hasVertexColor3, mesh.submeshes[i].hasVertexColor4, mesh.submeshes[i].hasTextureRect,
                        hasFog, mesh.submeshes[i].hasSkinning, mesh.submeshes[i].hasMorphTarget, mesh.submeshes[i].hasMorphNormal, mesh.submeshes[i].hasMorphTangent,
                        (terrain)?true:false, (instmesh)?true:false, p_ibl, p_mirror, p_ssao, p_light2d, p_shadows2d,
                        p_alphaMask, p_alphaOpaque);
        // a user-forked main shader overrides the built-in Mesh shader; the variant
        // (#define) system, depth/gbuffer passes and bind-slots are unchanged.
        // Priority: component customShader > scene default shader > built-in (empty)
        const std::string& meshShaderSrc = mesh.customShader.empty() ? scene->getDefaultCustomShader(ShaderType::MESH) : mesh.customShader;
        mesh.submeshes[i].customShaderId = ShaderPool::registerCustomShader(meshShaderSrc);
        mesh.submeshes[i].shader = ShaderPool::get(ShaderType::MESH, mesh.submeshes[i].shaderProperties, mesh.submeshes[i].customShaderId);
        // the depth shader feeds shadow maps (casters) and the SSAO depth pre-pass
        // (SSR uses its own G-buffer shader, built below)
        if ((hasShadows && mesh.castShadows) || scene->isSSAOEnabled()){
            mesh.submeshes[i].depthShaderProperties = ShaderPool::getDepthMeshProperties(
                p_depthTexture, mesh.submeshes[i].hasSkinning, mesh.submeshes[i].hasMorphTarget,
                mesh.submeshes[i].hasMorphNormal, mesh.submeshes[i].hasMorphTangent, (terrain)?true:false, (instmesh)?true:false,
                p_depthAlphaMask);
            mesh.submeshes[i].depthShader = ShaderPool::get(ShaderType::DEPTH, mesh.submeshes[i].depthShaderProperties);
            if (!mesh.submeshes[i].depthShader->isCreated())
                return false;
        }
        // the G-buffer shader feeds the SSR geometry pass (MRT: packed depth + view-space
        // normal/roughness/metallic). Built only when SSR is enabled (it needs normals).
        if (scene->isSSREnabled()){
            bool gbufferHasBaseColorTex = !mesh.submeshes[i].material.baseColorTexture.empty();
            bool gbufferHasMRTex = !mesh.submeshes[i].material.metallicRoughnessTexture.empty();
            mesh.submeshes[i].gbufferShaderProperties = ShaderPool::getGBufferMeshProperties(
                gbufferHasBaseColorTex, mesh.submeshes[i].hasNormal, mesh.submeshes[i].hasSkinning,
                mesh.submeshes[i].hasMorphTarget, mesh.submeshes[i].hasMorphNormal, mesh.submeshes[i].hasMorphTangent,
                (terrain)?true:false, (instmesh)?true:false, gbufferHasMRTex);
            mesh.submeshes[i].gbufferShader = ShaderPool::get(ShaderType::GBUFFER, mesh.submeshes[i].gbufferShaderProperties);
            if (!mesh.submeshes[i].gbufferShader->isCreated())
                return false;
        }
        if (!mesh.submeshes[i].shader->isCreated()){
            // A custom shader that failed to compile falls back to the built-in shader so
            // the object stays visible and the mesh finishes loading (otherwise needReload
            // stays set and we retry forever). The fork is retried after its source changes
            // (invalidateCustomShaders clears the failure and re-flags needReload).
            if (mesh.submeshes[i].customShaderId != 0 &&
                ShaderPool::isShaderBuildFailed(ShaderType::MESH, mesh.submeshes[i].shaderProperties, mesh.submeshes[i].customShaderId)){
                Log::error("Custom shader '%s' failed to compile; rendering '%s' with the built-in shader",
                           meshShaderSrc.c_str(), scene->getEntityName(entity).c_str());
                mesh.submeshes[i].customShaderId = 0;
                mesh.submeshes[i].shader = ShaderPool::get(ShaderType::MESH, mesh.submeshes[i].shaderProperties, 0);
            }
            if (!mesh.submeshes[i].shader->isCreated())
                return false;
        }
        render.setShader(mesh.submeshes[i].shader.get());
        ShaderData& shaderData = mesh.submeshes[i].shader.get()->shaderData;

        mesh.submeshes[i].slotVSParams = shaderData.getUniformBlockIndex(UniformBlockType::PBR_VS_PARAMS);
        mesh.submeshes[i].slotFSParams = shaderData.getUniformBlockIndex(UniformBlockType::PBR_FS_PARAMS);
        if (p_hasTexture2){
            mesh.submeshes[i].slotFSTexCoordSets = shaderData.getUniformBlockIndex(UniformBlockType::PBR_FS_TEXCOORDSETS);
        }
        if (hasFog){
            mesh.submeshes[i].slotFSFog = shaderData.getUniformBlockIndex(UniformBlockType::FS_FOG);
        }
        if (p_mirror){
            mesh.submeshes[i].slotFSMirror = shaderData.getUniformBlockIndex(UniformBlockType::FS_MIRROR);
        }
        if ((hasLights || useIBL) && mesh.receiveLights){
            mesh.submeshes[i].slotFSLighting = shaderData.getUniformBlockIndex(UniformBlockType::FS_LIGHTING);
            if (p_ibl){
                mesh.submeshes[i].slotFSReflectionProbe = shaderData.getUniformBlockIndex(UniformBlockType::FS_REFLECTION_PROBE);
            }
            if (p_receiveShadows){
                mesh.submeshes[i].slotVSShadows = shaderData.getUniformBlockIndex(UniformBlockType::VS_SHADOWS);
                mesh.submeshes[i].slotFSShadows = shaderData.getUniformBlockIndex(UniformBlockType::FS_SHADOWS);
                mesh.submeshes[i].slotFSPointShadows = shaderData.getUniformBlockIndex(UniformBlockType::FS_POINT_SHADOWS);
            }
        }
        if (p_light2d){
            mesh.submeshes[i].slotFSLighting2D = shaderData.getUniformBlockIndex(UniformBlockType::FS_LIGHTING2D);
        }
        if (mesh.submeshes[i].hasTextureRect){
            mesh.submeshes[i].slotVSSprite = shaderData.getUniformBlockIndex(UniformBlockType::SPRITE_VS_PARAMS);
        }
        if (mesh.submeshes[i].hasSkinning){
            mesh.submeshes[i].slotVSSkinning = shaderData.getUniformBlockIndex(UniformBlockType::VS_SKINNING);
            if (!bindSkinningResource(render, shaderData, mesh))
                return false;
        }
        if (mesh.submeshes[i].hasMorphTarget){
            mesh.submeshes[i].slotVSMorphTarget = shaderData.getUniformBlockIndex(UniformBlockType::VS_MORPHTARGET);
        }

        if (!loadPBRTextures(mesh.submeshes[i].material, shaderData, mesh.submeshes[i].render, mesh.receiveLights)){
            return false;
        }
        loadShadowTextures(shaderData, mesh.submeshes[i].render, mesh.receiveLights, p_receiveShadows);
        loadShadow2DTexture(shaderData, mesh.submeshes[i].render, p_shadows2d);

        if (p_ibl){
            auto skyArray = scene->getComponentArray<SkyComponent>();
            TextureRender* irradiance = &emptyCubeBlack;
            TextureRender* prefiltered = &emptyCubeBlack;
            if (skyArray->size() > 0){
                SkyComponent& sky = skyArray->getComponentFromIndex(0);
                if (sky.irradianceMap && sky.prefilteredMap){
                    irradiance = sky.irradianceMap.get();
                    prefiltered = sky.prefilteredMap.get();
                }
            }
            render.addTexture(shaderData.getTextureIndex(TextureShaderType::IRRADIANCEMAP), ShaderStageType::FRAGMENT, irradiance);
            render.addTexture(shaderData.getTextureIndex(TextureShaderType::PREFILTEREDMAP), ShaderStageType::FRAGMENT, prefiltered);
            render.addTexture(shaderData.getTextureIndex(TextureShaderType::REFLECTIONPROBE), ShaderStageType::FRAGMENT, &emptyCubeBlack);
        }

        if (terrain){
            mesh.submeshes[i].slotVSTerrain = shaderData.getUniformBlockIndex(UniformBlockType::TERRAIN_VS_PARAMS);

            if (!loadTerrainTextures(*terrain, mesh.submeshes[i].render, shaderData)){
                return false;
            }

            terrain->needUpdateTexture = false;

            // bind view 0 as the load-time default; per-view buffers (same layout) are
            // swapped in at draw via ObjectRender::replaceVertexBuffer
            for (auto const &attr : terrain->views[0].nodesbuffer[i].getAttributes()) {
                render.addAttribute(shaderData.getAttrIndex(attr.first), terrain->views[0].nodesbuffer[i].getRender(), attr.second.getElements(), attr.second.getDataType(), terrain->views[0].nodesbuffer[i].getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
            }
        }

        mesh.submeshes[i].needUpdateTexture = false;
        mesh.submeshes[i].needUpdateDepthTexture = false;
        mesh.submeshes[i].needUpdateGBufferTexture = false;

        const bool alphaBlend = usesAlphaBlend(mesh.submeshes[i].material);
        mesh.submeshes[i].alphaBlend = alphaBlend;
        if (mesh.autoTransparency && !mesh.transparent && alphaBlend){
            mesh.transparent = true;
        }
    
        unsigned int indexCount = 0;
        // an indexed submesh draws indexCount elements even when that count is zero;
        // only a submesh without any index data falls back to the whole vertex buffer
        bool indexed = false;

        for (auto const& buf : buffers){
            if (buf.second->isRenderAttributes()) {
                if (buf.second->getType() == BufferType::INDEX_BUFFER){
                    indexed = true;
                    indexCount = buf.second->getCount();
                    Attribute indexattr = buf.second->getAttributes()[AttributeType::INDEX];
                    render.setIndex(buf.second->getRender(), indexattr.getDataType(), indexattr.getOffset());
                }else{
                    for (auto const &attr : buf.second->getAttributes()) {
                        render.addAttribute(shaderData.getAttrIndex(attr.first), buf.second->getRender(), attr.second.getElements(), attr.second.getDataType(), buf.second->getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                    }
                }
            }
        }

        for (auto const& attr : mesh.submeshes[i].attributes){
            if (bufferNameToRender.count(attr.second.getBufferName())){
                if (attr.first == AttributeType::INDEX){
                    indexed = true;
                    indexCount = attr.second.getCount();
                    render.setIndex(bufferNameToRender[attr.second.getBufferName()], attr.second.getDataType(), attr.second.getOffset());
                }else{
                    render.addAttribute(shaderData.getAttrIndex(attr.first), bufferNameToRender[attr.second.getBufferName()], attr.second.getElements(), attr.second.getDataType(), bufferStride[attr.second.getBufferName()], attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                }
            }else{
                Log::error("Cannot load submesh attribute from buffer name: %s", attr.second.getBufferName().c_str());
            }
        }

        if (instmesh){
            for (auto const &attr : instmesh->buffer.getAttributes()) {
                render.addAttribute(shaderData.getAttrIndex(attr.first), instmesh->buffer.getRender(), attr.second.getElements(), attr.second.getDataType(), instmesh->buffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
            }
        }

        if (indexed){
            // An empty indexed submesh (a tilemap rect with no tile placed on it)
            // must draw nothing: falling back to the mesh vertex count would read
            // indices belonging to the other submeshes, and past the index buffer.
            mesh.submeshes[i].vertexCount = indexCount;
        }else{
            mesh.submeshes[i].vertexCount = mesh.vertexCount;
        }

        bool faceCulling = disableFaceCulling ? false : mesh.submeshes[i].faceCulling;
        if (!render.endLoad(pipelines, faceCulling, !alphaBlend,
                            mesh.cullingMode, mesh.windingOrder)){
            return false;
        }

        //----------Start depth shader---------------
        if ((hasShadows && mesh.castShadows) || scene->isSSAOEnabled()){
            ObjectRender& depthRender = mesh.submeshes[i].depthRender;

            depthRender.beginLoad(mesh.submeshes[i].primitiveType);

            depthRender.setShader(mesh.submeshes[i].depthShader.get());
            ShaderData& depthShaderData = mesh.submeshes[i].depthShader.get()->shaderData;

            mesh.submeshes[i].slotVSDepthParams = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_PARAMS);
            if (p_depthAlphaMask){
                mesh.submeshes[i].slotFSDepthMaterial = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_FS_MATERIAL);
            }

            if (mesh.submeshes[i].hasSkinning){
                mesh.submeshes[i].slotVSDepthSkinning = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_SKINNING);
                if (!bindSkinningResource(depthRender, depthShaderData, mesh))
                    return false;
            }
            if (mesh.submeshes[i].hasMorphTarget){
                mesh.submeshes[i].slotVSDepthMorphTarget = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_MORPHTARGET);
            }

            if (p_depthTexture){
                if (!loadDepthTexture(mesh.submeshes[i].material, depthShaderData, depthRender)){
                    return false;
                }
            }

            if (terrain){
                mesh.submeshes[i].slotVSDepthTerrain = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_TERRAIN_VS_PARAMS);

                if (!loadTerrainHeightTexture(*terrain, depthRender, depthShaderData)){
                    return false;
                }

                // shadow depth pass always renders the main camera's selection (view 0)
                for (auto const &attr : terrain->views[0].nodesbuffer[i].getAttributes()) {
                    depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), terrain->views[0].nodesbuffer[i].getRender(), attr.second.getElements(), attr.second.getDataType(), terrain->views[0].nodesbuffer[i].getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                }
            }

            for (auto const& buf : buffers){
                if (buf.second->isRenderAttributes()) {

                    if (buf.second->getType() == BufferType::INDEX_BUFFER){
                        indexCount = buf.second->getCount();
                        Attribute indexattr = buf.second->getAttributes()[AttributeType::INDEX];
                        depthRender.setIndex(buf.second->getRender(), indexattr.getDataType(), indexattr.getOffset());
                    }else{
                        for (auto const &attr : buf.second->getAttributes()){
                            if (attr.first == AttributeType::POSITION || attr.first == AttributeType::TEXCOORD1){
                                depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), buf.second->getRender(), attr.second.getElements(), attr.second.getDataType(), buf.second->getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                            }
                            if (mesh.submeshes[i].hasSkinning){
                                if (attr.first == AttributeType::BONEIDS || attr.first == AttributeType::BONEWEIGHTS){
                                    depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), buf.second->getRender(), attr.second.getElements(), attr.second.getDataType(), buf.second->getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                                }
                            }
                            if (mesh.submeshes[i].hasMorphTarget){
                                if (attr.first == AttributeType::MORPHTARGET0 || attr.first == AttributeType::MORPHTARGET1 ||
                                    attr.first == AttributeType::MORPHTARGET2 || attr.first == AttributeType::MORPHTARGET3 ||
                                    attr.first == AttributeType::MORPHTARGET4 || attr.first == AttributeType::MORPHTARGET5 ||
                                    attr.first == AttributeType::MORPHTARGET6 || attr.first == AttributeType::MORPHTARGET7 ||
                                    attr.first == AttributeType::MORPHNORMAL0 || attr.first == AttributeType::MORPHNORMAL1 ||
                                    attr.first == AttributeType::MORPHNORMAL2 || attr.first == AttributeType::MORPHNORMAL3 ||
                                    attr.first == AttributeType::MORPHTANGENT0 || attr.first == AttributeType::MORPHTANGENT1){
                                    depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), buf.second->getRender(), attr.second.getElements(), attr.second.getDataType(), buf.second->getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                                }
                            }
                        }
                    }
                }
            }

            for (auto const& attr : mesh.submeshes[i].attributes){
                if (bufferNameToRender.count(attr.second.getBufferName())){
                    if (attr.first == AttributeType::INDEX){
                        depthRender.setIndex(bufferNameToRender[attr.second.getBufferName()], attr.second.getDataType(), attr.second.getOffset());
                    }else if (attr.first == AttributeType::POSITION || attr.first == AttributeType::TEXCOORD1){
                        depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), bufferNameToRender[attr.second.getBufferName()], attr.second.getElements(), attr.second.getDataType(), bufferStride[attr.second.getBufferName()], attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                    }
                    if (mesh.submeshes[i].hasSkinning){
                        if (attr.first == AttributeType::BONEIDS || attr.first == AttributeType::BONEWEIGHTS){
                            depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), bufferNameToRender[attr.second.getBufferName()], attr.second.getElements(), attr.second.getDataType(), bufferStride[attr.second.getBufferName()], attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                        }
                    }
                    if (mesh.submeshes[i].hasMorphTarget){
                        if (attr.first == AttributeType::MORPHTARGET0 || attr.first == AttributeType::MORPHTARGET1 ||
                            attr.first == AttributeType::MORPHTARGET2 || attr.first == AttributeType::MORPHTARGET3 ||
                            attr.first == AttributeType::MORPHTARGET4 || attr.first == AttributeType::MORPHTARGET5 ||
                            attr.first == AttributeType::MORPHTARGET6 || attr.first == AttributeType::MORPHTARGET7 ||
                            attr.first == AttributeType::MORPHNORMAL0 || attr.first == AttributeType::MORPHNORMAL1 ||
                            attr.first == AttributeType::MORPHNORMAL2 || attr.first == AttributeType::MORPHNORMAL3 ||
                            attr.first == AttributeType::MORPHTANGENT0 || attr.first == AttributeType::MORPHTANGENT1){
                            depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), bufferNameToRender[attr.second.getBufferName()], attr.second.getElements(), attr.second.getDataType(), bufferStride[attr.second.getBufferName()], attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                        }
                    }
                }else{
                    Log::error("Cannot load (depth) submesh attribute from buffer name: %s", attr.second.getBufferName().c_str());
                }
            }

            if (instmesh){
                for (auto const &attr : instmesh->buffer.getAttributes()) {
                    depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), instmesh->buffer.getRender(), attr.second.getElements(), attr.second.getDataType(), instmesh->buffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                }
            }

            CullingMode depthCullingMode = mesh.cullingMode;
            bool depthFaceCulling = (mesh.submeshes[i].textureShadow)? false : mesh.submeshes[i].faceCulling;

            if (!depthRender.endLoad(PIP_DEPTH | PIP_SHADOW_DEPTH, depthFaceCulling, true, depthCullingMode, mesh.windingOrder)){
                return false;
            }
        }
        //----------End depth shader---------------

        //----------Start G-buffer shader---------------
        if (scene->isSSREnabled()){
            ObjectRender& gbufferRender = mesh.submeshes[i].gbufferRender;

            gbufferRender.beginLoad(mesh.submeshes[i].primitiveType);

            gbufferRender.setShader(mesh.submeshes[i].gbufferShader.get());
            ShaderData& gbufferShaderData = mesh.submeshes[i].gbufferShader.get()->shaderData;

            mesh.submeshes[i].slotVSGBufferParams = gbufferShaderData.getUniformBlockIndex(UniformBlockType::GBUFFER_VS_PARAMS);
            mesh.submeshes[i].slotFSGBufferMaterial = gbufferShaderData.getUniformBlockIndex(UniformBlockType::GBUFFER_FS_MATERIAL);

            if (mesh.submeshes[i].hasSkinning){
                mesh.submeshes[i].slotVSGBufferSkinning = gbufferShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_SKINNING);
                if (!bindSkinningResource(gbufferRender, gbufferShaderData, mesh))
                    return false;
            }
            if (mesh.submeshes[i].hasMorphTarget){
                mesh.submeshes[i].slotVSGBufferMorphTarget = gbufferShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_MORPHTARGET);
            }

            if (!loadGBufferTextures(mesh.submeshes[i].material, gbufferShaderData, gbufferRender)){
                return false;
            }

            if (terrain){
                mesh.submeshes[i].slotVSGBufferTerrain = gbufferShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_TERRAIN_VS_PARAMS);

                if (!loadTerrainHeightTexture(*terrain, gbufferRender, gbufferShaderData)){
                    return false;
                }

                for (auto const &attr : terrain->views[0].nodesbuffer[i].getAttributes()) {
                    gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), terrain->views[0].nodesbuffer[i].getRender(), attr.second.getElements(), attr.second.getDataType(), terrain->views[0].nodesbuffer[i].getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                }
            }

            unsigned int gbufferIndexCount = 0;
            for (auto const& buf : buffers){
                if (buf.second->isRenderAttributes()) {
                    if (buf.second->getType() == BufferType::INDEX_BUFFER){
                        gbufferIndexCount = buf.second->getCount();
                        Attribute indexattr = buf.second->getAttributes()[AttributeType::INDEX];
                        gbufferRender.setIndex(buf.second->getRender(), indexattr.getDataType(), indexattr.getOffset());
                    }else{
                        for (auto const &attr : buf.second->getAttributes()){
                            if (attr.first == AttributeType::POSITION || attr.first == AttributeType::TEXCOORD1 || attr.first == AttributeType::NORMAL){
                                gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), buf.second->getRender(), attr.second.getElements(), attr.second.getDataType(), buf.second->getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                            }
                            if (mesh.submeshes[i].hasSkinning){
                                if (attr.first == AttributeType::BONEIDS || attr.first == AttributeType::BONEWEIGHTS){
                                    gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), buf.second->getRender(), attr.second.getElements(), attr.second.getDataType(), buf.second->getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                                }
                            }
                            if (mesh.submeshes[i].hasMorphTarget){
                                if (attr.first == AttributeType::MORPHTARGET0 || attr.first == AttributeType::MORPHTARGET1 ||
                                    attr.first == AttributeType::MORPHTARGET2 || attr.first == AttributeType::MORPHTARGET3 ||
                                    attr.first == AttributeType::MORPHTARGET4 || attr.first == AttributeType::MORPHTARGET5 ||
                                    attr.first == AttributeType::MORPHTARGET6 || attr.first == AttributeType::MORPHTARGET7 ||
                                    attr.first == AttributeType::MORPHNORMAL0 || attr.first == AttributeType::MORPHNORMAL1 ||
                                    attr.first == AttributeType::MORPHNORMAL2 || attr.first == AttributeType::MORPHNORMAL3 ||
                                    attr.first == AttributeType::MORPHTANGENT0 || attr.first == AttributeType::MORPHTANGENT1){
                                    gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), buf.second->getRender(), attr.second.getElements(), attr.second.getDataType(), buf.second->getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                                }
                            }
                        }
                    }
                }
            }

            for (auto const& attr : mesh.submeshes[i].attributes){
                if (bufferNameToRender.count(attr.second.getBufferName())){
                    if (attr.first == AttributeType::INDEX){
                        gbufferRender.setIndex(bufferNameToRender[attr.second.getBufferName()], attr.second.getDataType(), attr.second.getOffset());
                    }else if (attr.first == AttributeType::POSITION || attr.first == AttributeType::TEXCOORD1 || attr.first == AttributeType::NORMAL){
                        gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), bufferNameToRender[attr.second.getBufferName()], attr.second.getElements(), attr.second.getDataType(), bufferStride[attr.second.getBufferName()], attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                    }
                    if (mesh.submeshes[i].hasSkinning){
                        if (attr.first == AttributeType::BONEIDS || attr.first == AttributeType::BONEWEIGHTS){
                            gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), bufferNameToRender[attr.second.getBufferName()], attr.second.getElements(), attr.second.getDataType(), bufferStride[attr.second.getBufferName()], attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                        }
                    }
                    if (mesh.submeshes[i].hasMorphTarget){
                        if (attr.first == AttributeType::MORPHTARGET0 || attr.first == AttributeType::MORPHTARGET1 ||
                            attr.first == AttributeType::MORPHTARGET2 || attr.first == AttributeType::MORPHTARGET3 ||
                            attr.first == AttributeType::MORPHTARGET4 || attr.first == AttributeType::MORPHTARGET5 ||
                            attr.first == AttributeType::MORPHTARGET6 || attr.first == AttributeType::MORPHTARGET7 ||
                            attr.first == AttributeType::MORPHNORMAL0 || attr.first == AttributeType::MORPHNORMAL1 ||
                            attr.first == AttributeType::MORPHNORMAL2 || attr.first == AttributeType::MORPHNORMAL3 ||
                            attr.first == AttributeType::MORPHTANGENT0 || attr.first == AttributeType::MORPHTANGENT1){
                            gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), bufferNameToRender[attr.second.getBufferName()], attr.second.getElements(), attr.second.getDataType(), bufferStride[attr.second.getBufferName()], attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                        }
                    }
                }
            }

            if (instmesh){
                for (auto const &attr : instmesh->buffer.getAttributes()) {
                    gbufferRender.addAttribute(gbufferShaderData.getAttrIndex(attr.first), instmesh->buffer.getRender(), attr.second.getElements(), attr.second.getDataType(), instmesh->buffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
                }
            }

            CullingMode gbufferCullingMode = mesh.cullingMode;
            bool gbufferFaceCulling = (mesh.submeshes[i].textureShadow)? false : mesh.submeshes[i].faceCulling;

            if (!gbufferRender.endLoad(PIP_GBUFFER, gbufferFaceCulling, true, gbufferCullingMode, mesh.windingOrder)){
                return false;
            }
        }
        //----------End G-buffer shader---------------
    }

    mesh.needReload = false;
    mesh.needUpdateAABB = true;
    mesh.loadCalled = true;

    // A reload recreates the terrain's per-view CDLOD node buffers empty; force a
    // re-selection next frame (the main-camera cut at view 0 is otherwise only
    // re-run on camera/transform movement), so the terrain doesn't vanish until
    // the camera moves after a settings change such as toggling SSAO.
    if (terrain){
        if (Transform* terrainTransform = scene->findComponent<Transform>(entity)){
            terrainTransform->needUpdate = true;
        }
    }

    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

// Uploads the dirty CPU-side buffers of a mesh, before the first pass of the frame
// that draws it. Every pass needs this, not only the color one that comes last: a
// rebuilt tilemap reorders its indices, so a shadow or G-buffer pass drawing chunk
// ranges would slice the previous upload with the new offsets. Sokol allows a single
// update per buffer per frame, which the flag already guarantees.
void RenderSystem::updateMeshBuffers(MeshComponent& mesh){
    if (mesh.needUpdateBones){
        const unsigned int size = static_cast<unsigned int>(
            mesh.bonesMatrix.size() * sizeof(Matrix4));
        if (mesh.bonesBuffer.isCreated())
            mesh.bonesBuffer.updateBuffer(size, mesh.bonesMatrix.data());
        if (mesh.bonesTexture.isCreated())
            mesh.bonesTexture.updateTexture(mesh.bonesMatrix.data(), size);
        mesh.needUpdateBones = false;
    }

    if (!mesh.needUpdateBuffer)
        return;

    if (mesh.buffer.getUsage() != BufferUsage::IMMUTABLE)
        mesh.buffer.getRender()->updateBuffer(mesh.buffer.getSize(), mesh.buffer.getData());
    if (mesh.indices.getUsage() != BufferUsage::IMMUTABLE)
        mesh.indices.getRender()->updateBuffer(mesh.indices.getSize(), mesh.indices.getData());
    for (int i = 0; i < mesh.numExternalBuffers; i++){
        if (mesh.eBuffers[i].getUsage() != BufferUsage::IMMUTABLE)
            mesh.eBuffers[i].getRender()->updateBuffer(mesh.eBuffers[i].getSize(), mesh.eBuffers[i].getData());
    }

    mesh.needUpdateBuffer = false;
}

// Once per frame, before any pass: the depth passes take their instance count from here.
void RenderSystem::updateInstanceBuffers(){
    auto instmeshes = scene->getComponentArray<InstancedMeshComponent>();

    for (int i = 0; i < instmeshes->size(); i++){
        InstancedMeshComponent& instmesh = instmeshes->getComponentFromIndex(i);

        if (!instmesh.needUpdateBuffer)
            continue;

        // setData here because component can change order and lose reference
        if (instmesh.numVisible > 0 && !instmesh.renderInstances.empty()){
            instmesh.buffer.setData((unsigned char*)(&instmesh.renderInstances[0]), sizeof(InstanceRenderData)*instmesh.numVisible);
            instmesh.buffer.getRender()->updateBuffer(instmesh.buffer.getSize(), instmesh.buffer.getData());
        }

        instmesh.needUpdateBuffer = false;
    }
}

// Uploads a terrain view's CDLOD node selection, before the first pass of the frame
// that draws it — the same rule as updateMeshBuffers. The depth and G-buffer passes
// draw the main camera's selection and take their instance count from the CPU-side
// buffer, so without this they would draw this frame's node count out of the previous
// frame's node data.
void RenderSystem::updateTerrainNodesBuffer(TerrainComponent& terrain, int viewIndex){
    if (!terrain.views[viewIndex].needUpdateNodesBuffer)
        return;

    for (int s = 0; s < 2; s++){
        terrain.views[viewIndex].nodesbuffer[s].getRender()->updateBuffer(terrain.views[viewIndex].nodesbuffer[s].getSize(), terrain.views[viewIndex].nodesbuffer[s].getData());
    }

    terrain.views[viewIndex].needUpdateNodesBuffer = false;
}

bool RenderSystem::drawMesh(MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform, bool renderToTexture, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap, int terrainView){
    if (mesh.loaded && !mesh.needReload){

        if (terrain && terrain->needUpdateTexture){
            return false;
        }

        if (mesh.worldAABB != AABB::ZERO && !isInsideCamera(camera, mesh.worldAABB)) {
            return false;
        }

        // unpainted foliage chunks make this common
        if (instmesh && instmesh->numVisible == 0){
            return false;
        }

        updateMeshBuffers(mesh);

        // Buffer already uploaded this frame by updateInstanceBuffers().
        unsigned int instanceCount = 1;
        if (instmesh){
            instanceCount = instmesh->numVisible;
        }

        if (terrain){
            updateTerrainNodesBuffer(*terrain, terrainView);
        }

        for (int i = 0; i < mesh.numSubmeshes; i++){
            // an indexed submesh without indices has nothing to draw (a tilemap rect
            // with no tile placed on it). Its texture state is cleared here because
            // gaining indices always reloads the mesh, which reloads the textures.
            if (mesh.submeshes[i].vertexCount == 0){
                mesh.submeshes[i].needUpdateTexture = false;
                continue;
            }

            // per-chunk culling: skip the submesh entirely when no chunk is visible
            if (tilemap && !selectTilemapChunks(*tilemap, i, camera.farClip, camera.frustumPlanes))
                continue;

            ObjectRender& render = mesh.submeshes[i].render;

            if (terrain){
                instanceCount = terrain->views[terrainView].nodesbuffer[i].getCount();
                // swap in this view's node instance buffer (no-op for view 0)
                render.replaceVertexBuffer(terrain->views[0].nodesbuffer[i].getRender(), terrain->views[terrainView].nodesbuffer[i].getRender());
            }

            bool needUpdateFramebuffer = checkPBRFrabebufferUpdate(mesh.submeshes[i].material);

            if (mesh.submeshes[i].needUpdateTexture || needUpdateFramebuffer){
                ShaderData& shaderData = mesh.submeshes[i].shader.get()->shaderData;
                if (loadPBRTextures(mesh.submeshes[i].material, shaderData, mesh.submeshes[i].render, mesh.receiveLights)){
                    mesh.submeshes[i].needUpdateTexture = false;
                }
            }

            PipelineType pipType = PIP_DEFAULT;
            if (renderToTexture){
                // reflection cameras flip winding to keep front faces visible
                pipType = camera.invertCulling ? PIP_RTT_INVERT : PIP_RTT;
            }
            if (!render.beginDraw(pipType)){
                mesh.needReload = true;
                return false;
            }

            if (mesh.submeshes[i].hasIBL){
                TextureRender* probeTexture = &emptyCubeBlack;
                // Probe assignment is per renderable. Its bounds center is a
                // better representative point than the entity origin for meshes
                // whose geometry is offset in local space.
                Vector3 probeSamplePosition = mesh.worldAABB != AABB::ZERO
                    ? mesh.worldAABB.getCenter()
                    : transform.worldPosition;
                selectReflectionProbe(probeSamplePosition, fs_reflection_probe, probeTexture);
                ShaderData& probeShaderData = mesh.submeshes[i].shader.get()->shaderData;
                render.addTexture(probeShaderData.getTextureIndex(TextureShaderType::REFLECTIONPROBE), ShaderStageType::FRAGMENT, probeTexture);
                if (mesh.submeshes[i].slotFSReflectionProbe != -1){
                    render.applyUniformBlock(mesh.submeshes[i].slotFSReflectionProbe, sizeof(fs_reflection_probe_t), &fs_reflection_probe);
                }
            }

            if (hasFog){
                render.applyUniformBlock(mesh.submeshes[i].slotFSFog, sizeof(float) * 8, &fs_fog);
            }

            if (mesh.submeshes[i].slotFSMirror != -1){
                render.applyUniformBlock(mesh.submeshes[i].slotFSMirror, sizeof(float) * 16, &mesh.mirrorViewProjection);
            }

            // a submesh is lit (uses the lighting block + full PBR params) unless it
            // was compiled as MATERIAL_UNLIT (property bit 0)
            bool submeshLit = !(mesh.submeshes[i].shaderProperties & (1u << 0));

            if (submeshLit){
                ShaderData& lightingShaderData = mesh.submeshes[i].shader.get()->shaderData;
                loadSpotMaskTexture(lightingShaderData, render);
                render.applyUniformBlock(mesh.submeshes[i].slotFSLighting, sizeof(fs_lighting_t), &fs_lighting);
                if (hasShadows && (mesh.submeshes[i].shaderProperties & (1u << 4))){
                    render.applyUniformBlock(mesh.submeshes[i].slotVSShadows, sizeof(float) * (20 * MAX_SHADOW_ATLAS_SLOTS), &vs_shadows);
                    render.applyUniformBlock(mesh.submeshes[i].slotFSShadows, sizeof(fs_shadows_t), &fs_shadows);
                    render.applyUniformBlock(mesh.submeshes[i].slotFSPointShadows, sizeof(fs_point_shadows_t), &fs_point_shadows);
                }
                // USE_SSAO (property bit 21): bind the screen-space AO texture for
                // this view (blurred AO for the main camera, empty white otherwise)
                if (mesh.submeshes[i].shaderProperties & (1u << 21)){
                    ShaderData& ssaoShaderData = mesh.submeshes[i].shader.get()->shaderData;
                    render.addTexture(ssaoShaderData.getTextureIndex(TextureShaderType::SSAOTEXTURE), ShaderStageType::FRAGMENT,
                                      currentSSAOTexture ? currentSSAOTexture : &emptyWhite);
                }
            }

            // USE_LIGHT2D (property bit 22): dedicated 2D light path; composes with
            // both unlit (sprites) and lit submeshes, so it is applied independently
            if (mesh.submeshes[i].shaderProperties & (1u << 22)){
                render.applyUniformBlock(mesh.submeshes[i].slotFSLighting2D, sizeof(fs_lighting2d_t), &fs_lighting2d);
            }

            if (mesh.submeshes[i].hasTextureRect){
                render.applyUniformBlock(mesh.submeshes[i].slotVSSprite, sizeof(float) * 4, &mesh.submeshes[i].textureRect);
            }

            if (mesh.submeshes[i].hasSkinning){
                applySkinningUniform(render, mesh.submeshes[i].slotVSSkinning, mesh, mesh.submeshes[i]);
            }

            if (mesh.submeshes[i].hasMorphTarget){
                if (!mesh.submeshes[i].hasMorphNormal && !mesh.submeshes[i].hasMorphTangent){
                    render.applyUniformBlock(mesh.submeshes[i].slotVSMorphTarget, sizeof(float) * MAX_MORPHTARGETS, &mesh.morphWeights);
                }else{
                    render.applyUniformBlock(mesh.submeshes[i].slotVSMorphTarget, sizeof(float) * MAX_MORPHTARGETS / 2, &mesh.morphWeights);
                }
            }

            if (submeshLit){
                render.applyUniformBlock(mesh.submeshes[i].slotFSParams, sizeof(float) * 12, &mesh.submeshes[i].material);
            }else if (mesh.submeshes[i].shaderProperties & (1u << 24)){
                // Unlit alpha-mask variants include the metallic/roughness padding
                // and alphaCutoff from the shared CPU material prefix.
                render.applyUniformBlock(mesh.submeshes[i].slotFSParams, sizeof(float) * 8, &mesh.submeshes[i].material);
            }else{
                render.applyUniformBlock(mesh.submeshes[i].slotFSParams, sizeof(float) * 4, &mesh.submeshes[i].material);
            }

            if (mesh.submeshes[i].slotFSTexCoordSets != -1){
                // per-texture UV set selector (0 = a_texcoord1, 1 = a_texcoord2):
                // set0 = baseColor, metallicRoughness, occlusion, emissive; set1.x = normal
                const Material& mat = mesh.submeshes[i].material;
                int32_t texCoordSets[8] = {
                    mat.baseColorTexCoord, mat.metallicRoughnessTexCoord, mat.occlusionTexCoord, mat.emissiveTexCoord,
                    mat.normalTexCoord, 0, 0, 0
                };
                render.applyUniformBlock(mesh.submeshes[i].slotFSTexCoordSets, sizeof(texCoordSets), &texCoordSets);
            }

            if (terrain){
                // morph from this view's eye position (paired with its node selection)
                terrain->eyePos = terrain->views[terrainView].nodesEyePos;
                render.applyUniformBlock(mesh.submeshes[i].slotVSTerrain, sizeof(float) * 8, &(terrain->eyePos));
            }

            //model, normal and mvp matrix
            render.applyUniformBlock(mesh.submeshes[i].slotVSParams, sizeof(float) * 48, &transform.modelMatrix);

            if (tilemap && !tilemapDrawRanges.empty()){
                for (const TilemapDrawRange& range : tilemapDrawRanges){
                    render.draw(range.offset, range.count, instanceCount);
                }
            }else{
                render.draw(0, mesh.submeshes[i].vertexCount, instanceCount);
            }
        }
    }

    return true;
}

bool RenderSystem::drawMeshDepth(MeshComponent& mesh, const float cameraFar, const Plane frustumPlanes[6], vs_depth_t vsDepthParams, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap, bool forSSAO, PipelineType pipelineType){
    // shadow passes only draw casters; the SSAO depth pre-pass draws every opaque mesh
    if (mesh.loaded && !mesh.needReload && (mesh.castShadows || forSSAO)){

        if (terrain && terrain->needUpdateTexture){
            return false;
        }

        if (mesh.worldAABB != AABB::ZERO && !isInsideCamera(cameraFar, frustumPlanes, mesh.worldAABB)) {
            return false;
        }

        // unpainted foliage chunks make this common
        if (instmesh && instmesh->numVisible == 0){
            return false;
        }

        updateMeshBuffers(mesh);

        // the depth pass renders the main camera's selection (view 0)
        if (terrain){
            updateTerrainNodesBuffer(*terrain, 0);
        }

        for (int i = 0; i < mesh.numSubmeshes; i++){
            if (mesh.submeshes[i].vertexCount == 0){
                mesh.submeshes[i].needUpdateDepthTexture = false;
                continue;
            }

            if (tilemap && !selectTilemapChunks(*tilemap, i, cameraFar, frustumPlanes))
                continue;

            ObjectRender& depthRender = mesh.submeshes[i].depthRender;

            if (mesh.submeshes[i].needUpdateDepthTexture &&
                (mesh.submeshes[i].depthShaderProperties & (1u << 0))){
                ShaderData& depthShaderData = mesh.submeshes[i].depthShader.get()->shaderData;
                if (loadDepthTexture(mesh.submeshes[i].material, depthShaderData, mesh.submeshes[i].depthRender)){
                    mesh.submeshes[i].needUpdateDepthTexture = false;
                }
            }else if (mesh.submeshes[i].needUpdateDepthTexture){
                mesh.submeshes[i].needUpdateDepthTexture = false;
            }

            if (!depthRender.beginDraw(pipelineType)){
                mesh.needReload = true;
                return false;
            }

            unsigned int instanceCount = 1;
            if (instmesh){
                instanceCount = instmesh->numVisible;
            }
            if (terrain){
                instanceCount = terrain->views[0].nodesbuffer[i].getCount();
                depthRender.replaceVertexBuffer(terrain->views[0].nodesbuffer[i].getRender(), terrain->views[0].nodesbuffer[i].getRender());
            }

            //model, mvp matrix
            depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthParams, sizeof(float) * 32, &vsDepthParams);

            if (mesh.submeshes[i].slotFSDepthMaterial != -1){
                const Material& material = mesh.submeshes[i].material;
                Vector4 alphaParams(material.baseColorFactor.w, material.alphaCutoff, 0.0f, 0.0f);
                depthRender.applyUniformBlock(mesh.submeshes[i].slotFSDepthMaterial, sizeof(Vector4), &alphaParams);
            }

            if (mesh.submeshes[i].hasSkinning){
                applySkinningUniform(depthRender, mesh.submeshes[i].slotVSDepthSkinning, mesh, mesh.submeshes[i]);
            }
            if (mesh.submeshes[i].hasMorphTarget){
                if (!mesh.submeshes[i].hasMorphNormal && !mesh.submeshes[i].hasMorphTangent){
                    depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthMorphTarget, sizeof(float) * MAX_MORPHTARGETS, &mesh.morphWeights);
                }else{
                    depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthMorphTarget, sizeof(float) * MAX_MORPHTARGETS / 2, &mesh.morphWeights);
                }
            }

            if (terrain){
                // shadow depth renders the main camera's selection (view 0)
                terrain->eyePos = terrain->views[0].nodesEyePos;
                depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthTerrain, sizeof(float) * 8, &(terrain->eyePos));
            }

            if (tilemap && !tilemapDrawRanges.empty()){
                for (const TilemapDrawRange& range : tilemapDrawRanges){
                    depthRender.draw(range.offset, range.count, instanceCount);
                }
            }else{
                depthRender.draw(0, mesh.submeshes[i].vertexCount, instanceCount);
            }
        }
    }

    return true;
}

void RenderSystem::loadSSAO(){
    if (ssaoLoaded)
        return;

    // the SSAO fullscreen shaders may still be building (async in the editor)
    ssaoShader = ShaderPool::get(ShaderType::SSAO, 0);
    ssaoBlurShader = ShaderPool::get(ShaderType::SSAO_BLUR, 0);
    if (!ssaoShader || !ssaoShader->isCreated() || !ssaoBlurShader || !ssaoBlurShader->isCreated())
        return;

    // hemisphere sample kernel, weighted toward the origin (more near-field samples).
    // It is constant, so write it straight into the uniform block once here.
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distm11(-1.0f, 1.0f);

    for (int i = 0; i < SSAO_KERNEL_SIZE; i++){
        Vector3 sample(distm11(rng), distm11(rng), dist01(rng)); // +Z hemisphere
        sample.normalize();
        sample *= dist01(rng);
        float scale = (float)i / (float)SSAO_KERNEL_SIZE;
        scale = 0.1f + (scale * scale) * (1.0f - 0.1f); // lerp(0.1, 1.0, scale^2)
        sample *= scale;
        fs_ssao.kernel[i] = Vector4(sample.x, sample.y, sample.z, 0.0f);
    }

    // 4x4 rotation-noise texture: random vectors in the tangent XY plane (z=0).
    // Pixel layout matches the empty textures: 0xAABBGGRR in memory.
    const int noiseDim = 4;
    uint32_t noisePixels[noiseDim * noiseDim];
    for (int i = 0; i < noiseDim * noiseDim; i++){
        uint8_t r = (uint8_t)((distm11(rng) * 0.5f + 0.5f) * 255.0f);
        uint8_t g = (uint8_t)((distm11(rng) * 0.5f + 0.5f) * 255.0f);
        noisePixels[i] = (0xFFu << 24) | (0x80u << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }

    void* ndata[6]; size_t nsize[6];
    ndata[0] = (void*)noisePixels; nsize[0] = noiseDim * noiseDim * 4;
    ssaoNoiseTexture.createTexture(
            "ssao|noise", noiseDim, noiseDim, ColorFormat::RGBA, TextureType::TEXTURE_2D, 1, ndata, nsize,
            TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::REPEAT, TextureWrap::REPEAT);

    // fullscreen ssao pass (vertexless: no attributes added)
    ssaoRender.beginLoad(PrimitiveType::TRIANGLES);
    ssaoRender.setShader(ssaoShader.get());
    ssaoSlotParams = ssaoShader.get()->shaderData.getUniformBlockIndex(UniformBlockType::SSAO_FS_PARAMS);
    if (!ssaoRender.endLoad(PIP_RTT, false, true, CullingMode::BACK, WindingOrder::CCW))
        return;

    // fullscreen blur pass
    ssaoBlurRender.beginLoad(PrimitiveType::TRIANGLES);
    ssaoBlurRender.setShader(ssaoBlurShader.get());
    ssaoBlurSlotParams = ssaoBlurShader.get()->shaderData.getUniformBlockIndex(UniformBlockType::SSAO_BLUR_FS_PARAMS);
    if (!ssaoBlurRender.endLoad(PIP_RTT, false, true, CullingMode::BACK, WindingOrder::CCW))
        return;

    ssaoLoaded = true;
}

void RenderSystem::destroySSAO(){
    if (ssaoLoaded){
        ssaoRender.destroy();
        ssaoBlurRender.destroy();
        ssaoNoiseTexture.destroyTexture();
    }
    if (ssaoShader){
        ssaoShader.reset();
        ShaderPool::remove(ShaderType::SSAO, 0);
    }
    if (ssaoBlurShader){
        ssaoBlurShader.reset();
        ShaderPool::remove(ShaderType::SSAO_BLUR, 0);
    }

    ssaoDepthFramebuffer.destroy();
    ssaoFramebuffer.destroy();
    ssaoBlurFramebuffer.destroy();

    ssaoWidth = 0;
    ssaoHeight = 0;
    ssaoSlotParams = -1;
    ssaoBlurSlotParams = -1;
    ssaoLoaded = false;
    currentSSAOTexture = NULL;
}

bool RenderSystem::ensureSSAOFramebuffers(unsigned int width, unsigned int height){
    if (width == 0 || height == 0)
        return false;

    if (ssaoDepthFramebuffer.isCreated() && ssaoWidth == width && ssaoHeight == height)
        return true;

    ssaoDepthFramebuffer.destroy();
    ssaoFramebuffer.destroy();
    ssaoBlurFramebuffer.destroy();

    // depth pre-pass target: packed depth in color, sampled point-exact
    ssaoDepthFramebuffer.setWidth(width);
    ssaoDepthFramebuffer.setHeight(height);
    ssaoDepthFramebuffer.setMinFilter(TextureFilter::NEAREST);
    ssaoDepthFramebuffer.setMagFilter(TextureFilter::NEAREST);
    ssaoDepthFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    ssaoDepthFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    ssaoDepthFramebuffer.create();

    ssaoFramebuffer.setWidth(width);
    ssaoFramebuffer.setHeight(height);
    ssaoFramebuffer.setMinFilter(TextureFilter::NEAREST);
    ssaoFramebuffer.setMagFilter(TextureFilter::NEAREST);
    ssaoFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    ssaoFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    ssaoFramebuffer.create();

    // blurred AO is sampled bilinearly by the lit meshes
    ssaoBlurFramebuffer.setWidth(width);
    ssaoBlurFramebuffer.setHeight(height);
    ssaoBlurFramebuffer.setMinFilter(TextureFilter::LINEAR);
    ssaoBlurFramebuffer.setMagFilter(TextureFilter::LINEAR);
    ssaoBlurFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    ssaoBlurFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    ssaoBlurFramebuffer.create();

    ssaoWidth = width;
    ssaoHeight = height;

    return ssaoDepthFramebuffer.isCreated() && ssaoFramebuffer.isCreated() && ssaoBlurFramebuffer.isCreated();
}

void RenderSystem::renderSSAO(CameraComponent& camera, TextureRender* sharedDepth){
    loadSSAO();
    if (!ssaoLoaded)
        return;

    unsigned int w = (unsigned int)Engine::getViewRect().getWidth();
    unsigned int h = (unsigned int)Engine::getViewRect().getHeight();
    if (isFixedResolutionActive() && !camera.renderToTexture){
        // the main color pass renders into the fixed-resolution target, so the
        // AO buffer must match it (meshes convert gl_FragCoord with these sizes)
        w = scene->getFixedResolutionWidth();
        h = scene->getFixedResolutionHeight();
    }
    if (!ensureSSAOFramebuffers(w, h))
        return;

    Matrix4 renderProj = camera.projectionMatrix;

    // --- 1. depth + normal source: reuse the SSR G-buffer (packed depth in color[0],
    // view-space normal in color[1]) when available, otherwise run the SSAO depth
    // pre-pass and reconstruct the normal from the depth gradient in the shader ---
    bool useGBufferNormal = (sharedDepth != nullptr);
    TextureRender* depthTexture = sharedDepth;
    TextureRender* normalTexture; // G-buffer normal when shared, else an unused placeholder
    if (!depthTexture){
        renderDepthPrePass(camera);
        depthTexture = &ssaoDepthFramebuffer.getRender().getColorTexture();
        normalTexture = depthTexture; // placeholder binding; not sampled (params.w = 0)
    }else{
        normalTexture = &gbufferFramebuffer.getRender().getColorAttachmentTexture(1);
    }

    // --- 2. ssao pass (kernel was filled once in loadSSAO) ---
    fs_ssao.projection = renderProj;
    fs_ssao.invProjection = renderProj.inverse();
    fs_ssao.params = Vector4(scene->getSSAORadius(), scene->getSSAOBias(), scene->getSSAOIntensity(), useGBufferNormal ? 1.0f : 0.0f);
    // xy = noise tiling (4x4), zw = inverse depth-texture size (neighbour taps for normals)
    fs_ssao.noiseScale = Vector4((float)w / 4.0f, (float)h / 4.0f, 1.0f / (float)w, 1.0f / (float)h);

    ssaoPassRender.setClearColor(Vector4(1.0, 1.0, 1.0, 1.0));
    ssaoPassRender.startRenderPass(&ssaoFramebuffer.getRender());
    if (ssaoRender.beginDraw(PIP_RTT)){
        ShaderData& sd = ssaoShader.get()->shaderData;
        ssaoRender.addTexture(sd.getTextureIndex(TextureShaderType::DEPTHTEXTURE), ShaderStageType::FRAGMENT, depthTexture);
        ssaoRender.addTexture(sd.getTextureIndex(TextureShaderType::GBUFFERTEXTURE), ShaderStageType::FRAGMENT, normalTexture);
        ssaoRender.addTexture(sd.getTextureIndex(TextureShaderType::NOISETEXTURE), ShaderStageType::FRAGMENT, &ssaoNoiseTexture);
        ssaoRender.applyUniformBlock(ssaoSlotParams, sizeof(fs_ssao_t), &fs_ssao);
        ssaoRender.draw(0, 3, 1);
    }
    ssaoPassRender.endRenderPass();

    // --- 3. blur pass ---
    fs_ssao_blur.texelSize = Vector4(1.0f / (float)w, 1.0f / (float)h, 0.0, 0.0);

    ssaoPassRender.setClearColor(Vector4(1.0, 1.0, 1.0, 1.0));
    ssaoPassRender.startRenderPass(&ssaoBlurFramebuffer.getRender());
    if (ssaoBlurRender.beginDraw(PIP_RTT)){
        ShaderData& bd = ssaoBlurShader.get()->shaderData;
        ssaoBlurRender.addTexture(bd.getTextureIndex(TextureShaderType::SSAOTEXTURE), ShaderStageType::FRAGMENT, &ssaoFramebuffer.getRender().getColorTexture());
        ssaoBlurRender.applyUniformBlock(ssaoBlurSlotParams, sizeof(fs_ssao_blur_t), &fs_ssao_blur);
        ssaoBlurRender.draw(0, 3, 1);
    }
    ssaoPassRender.endRenderPass();

    currentSSAOTexture = &ssaoBlurFramebuffer.getRender().getColorTexture();
}

void RenderSystem::renderDepthPrePass(CameraComponent& camera){
    // Logical (un-flipped) matrices: flipY reverses winding but PIP_DEPTH does not
    // compensate (only PIP_RTT reverses winding on GL). The depth buffer therefore
    // stays in logical orientation; SSAO/SSR consumers flip Y on GL when sampling.
    Matrix4 renderVP = camera.viewProjectionMatrix;

    ssaoPassRender.setClearColor(Vector4(1.0, 1.0, 1.0, 1.0)); // background -> depth ~1.0
    ssaoPassRender.startRenderPass(&ssaoDepthFramebuffer.getRender());

    auto transforms = scene->getComponentArray<Transform>();
    for (int i = 0; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);
        Entity entity = transforms->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (!signature.test(scene->getComponentId<MeshComponent>()))
            continue;

        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        if (!transform.visible || mesh.transparent)
            continue;

        InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
        TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
        TilemapComponent* tilemap = scene->findComponent<TilemapComponent>(entity);

        vs_depth_t params = {transform.modelMatrix, renderVP};
        drawMeshDepth(mesh, camera.farClip, camera.frustumPlanes, params, instmesh, terrain, tilemap, true);
    }
    ssaoPassRender.endRenderPass();
}

bool RenderSystem::ensureGBufferFramebuffer(unsigned int width, unsigned int height){
    if (width == 0 || height == 0)
        return false;

    if (gbufferFramebuffer.isCreated() && gbufferFramebuffer.getWidth() == width && gbufferFramebuffer.getHeight() == height)
        return true;

    gbufferFramebuffer.destroy();

    // MRT: color[0] = packed depth (point-sampled like the depth pre-pass),
    //      color[1] = view-space normal (oct .rg) + roughness (.b) + metallic (.a),
    //      color[2] = linear base color (.rgb) + hasIBL flag (.a)
    ColorFormat formats[3] = { ColorFormat::RGBA, ColorFormat::RGBA, ColorFormat::RGBA };
    gbufferFramebuffer.setColorAttachments(3, formats);
    gbufferFramebuffer.setWidth(width);
    gbufferFramebuffer.setHeight(height);
    gbufferFramebuffer.setMinFilter(TextureFilter::NEAREST);
    gbufferFramebuffer.setMagFilter(TextureFilter::NEAREST);
    gbufferFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    gbufferFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    gbufferFramebuffer.create();

    return gbufferFramebuffer.isCreated();
}

bool RenderSystem::drawMeshGBuffer(MeshComponent& mesh, const float cameraFar, const Plane frustumPlanes[6], vs_gbuffer_t vsGBufferParams, bool hasLocalProbe, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap){
    if (!mesh.loaded || mesh.needReload)
        return true;

    if (terrain && terrain->needUpdateTexture){
        return false;
    }

    if (mesh.worldAABB != AABB::ZERO && !isInsideCamera(cameraFar, frustumPlanes, mesh.worldAABB)) {
        return false;
    }

    // unpainted foliage chunks make this common
    if (instmesh && instmesh->numVisible == 0){
        return false;
    }

    updateMeshBuffers(mesh);

    // the G-buffer pass renders the main camera's selection (view 0)
    if (terrain){
        updateTerrainNodesBuffer(*terrain, 0);
    }

    for (int i = 0; i < mesh.numSubmeshes; i++){
        // the G-buffer shader/render only exist when SSR was enabled at load time; a
        // mesh loaded before SSR was toggled on is skipped until it reloads
        if (!mesh.submeshes[i].gbufferShader)
            continue;

        if (mesh.submeshes[i].vertexCount == 0){
            mesh.submeshes[i].needUpdateGBufferTexture = false;
            continue;
        }

        if (tilemap && !selectTilemapChunks(*tilemap, i, cameraFar, frustumPlanes))
            continue;

        ObjectRender& gbufferRender = mesh.submeshes[i].gbufferRender;

        if (mesh.submeshes[i].needUpdateGBufferTexture){
            ShaderData& gbufferShaderData = mesh.submeshes[i].gbufferShader.get()->shaderData;
            if (loadGBufferTextures(mesh.submeshes[i].material, gbufferShaderData, mesh.submeshes[i].gbufferRender)){
                mesh.submeshes[i].needUpdateGBufferTexture = false;
            }
        }

        if (!gbufferRender.beginDraw(PIP_GBUFFER)){
            mesh.needReload = true;
            return false;
        }

        unsigned int instanceCount = 1;
        if (instmesh){
            instanceCount = instmesh->numVisible;
        }

        gbufferRender.applyUniformBlock(mesh.submeshes[i].slotVSGBufferParams, sizeof(vs_gbuffer_t), &vsGBufferParams);

        fs_gbuffer_material_t mat;
        // IBL source encoding: 0 = none, 0.5 = sky, 1 = local probe. The SSR
        // composite can exactly replace sky IBL; local probes need a deferred
        // cubemap array to be reconstructed per pixel, so they use additive SSR.
        float iblSource = mesh.submeshes[i].hasIBL ? (hasLocalProbe ? 1.0f : 0.5f) : 0.0f;
        const Material& material = mesh.submeshes[i].material;
        const float alphaCutoff = usesAlphaMask(material, mesh.submeshes[i].textureShadow)
            ? material.alphaCutoff
            : -1.0f;
        mat.params = Vector4(material.roughnessFactor, material.metallicFactor, iblSource, alphaCutoff);
        mat.baseColorFactor = mesh.submeshes[i].material.baseColorFactor;
        gbufferRender.applyUniformBlock(mesh.submeshes[i].slotFSGBufferMaterial, sizeof(fs_gbuffer_material_t), &mat);

        if (mesh.submeshes[i].hasSkinning){
            applySkinningUniform(gbufferRender, mesh.submeshes[i].slotVSGBufferSkinning, mesh, mesh.submeshes[i]);
        }
        if (mesh.submeshes[i].hasMorphTarget){
            if (!mesh.submeshes[i].hasMorphNormal && !mesh.submeshes[i].hasMorphTangent){
                gbufferRender.applyUniformBlock(mesh.submeshes[i].slotVSGBufferMorphTarget, sizeof(float) * MAX_MORPHTARGETS, &mesh.morphWeights);
            }else{
                gbufferRender.applyUniformBlock(mesh.submeshes[i].slotVSGBufferMorphTarget, sizeof(float) * MAX_MORPHTARGETS / 2, &mesh.morphWeights);
            }
        }

        if (terrain){
            terrain->eyePos = terrain->views[0].nodesEyePos;
            gbufferRender.applyUniformBlock(mesh.submeshes[i].slotVSGBufferTerrain, sizeof(float) * 8, &(terrain->eyePos));
            instanceCount = terrain->views[0].nodesbuffer[i].getCount();
            gbufferRender.replaceVertexBuffer(terrain->views[0].nodesbuffer[i].getRender(), terrain->views[0].nodesbuffer[i].getRender());
        }

        if (tilemap && !tilemapDrawRanges.empty()){
            for (const TilemapDrawRange& range : tilemapDrawRanges){
                gbufferRender.draw(range.offset, range.count, instanceCount);
            }
        }else{
            gbufferRender.draw(0, mesh.submeshes[i].vertexCount, instanceCount);
        }
    }

    return true;
}

void RenderSystem::renderGBufferPass(CameraComponent& camera){
    // Same logical orientation as renderDepthPrePass: PIP_GBUFFER (like PIP_DEPTH)
    // does not reverse winding on GL, so color[0] depth matches the depth pre-pass
    // and SSR's existing Y-flip logic applies unchanged. color[1] carries the
    // view-space normal (octahedral) + roughness + metallic at the same texel layout.
    Matrix4 renderVP = camera.viewProjectionMatrix;
    Matrix4 viewMatrix = camera.viewMatrix;

    gbufferPassRender.setClearColor(Vector4(1.0, 1.0, 1.0, 1.0)); // color[0] background -> depth ~1.0
    gbufferPassRender.startRenderPass(&gbufferFramebuffer.getRender());

    auto transforms = scene->getComponentArray<Transform>();
    for (int i = 0; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);
        Entity entity = transforms->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (!signature.test(scene->getComponentId<MeshComponent>()))
            continue;

        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        if (!transform.visible || mesh.transparent)
            continue;

        InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
        TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
        TilemapComponent* tilemap = scene->findComponent<TilemapComponent>(entity);

        fs_reflection_probe_t probeParams;
        TextureRender* probeTexture = nullptr;
        Vector3 probeSamplePosition = mesh.worldAABB != AABB::ZERO
            ? mesh.worldAABB.getCenter()
            : transform.worldPosition;
        bool hasLocalProbe = selectReflectionProbe(probeSamplePosition, probeParams, probeTexture)
            && probeParams.position_weight.w > 0.001f;

        // view-space normal matrix = transpose(inverse(view * model))
        Matrix4 viewModel = viewMatrix * transform.modelMatrix;
        Matrix4 normalMatrix = viewModel.inverse().transpose();

        vs_gbuffer_t params = {transform.modelMatrix, renderVP, normalMatrix};
        drawMeshGBuffer(mesh, camera.farClip, camera.frustumPlanes, params, hasLocalProbe, instmesh, terrain, tilemap);
    }
    gbufferPassRender.endRenderPass();
}

void RenderSystem::loadSSR(){
    if (ssrLoaded)
        return;

    // fullscreen SSR shaders may still be building (async in the editor)
    ssrShader = ShaderPool::get(ShaderType::SSR, 0);
    ssrBlurShader = ShaderPool::get(ShaderType::SSR_BLUR, 0);
    compositeShader = ShaderPool::get(ShaderType::COMPOSITE, 0);
    if (!ssrShader || !ssrShader->isCreated() || !ssrBlurShader || !ssrBlurShader->isCreated() || !compositeShader || !compositeShader->isCreated())
        return;

    // fullscreen ssr pass (vertexless: no attributes added)
    ssrRender.beginLoad(PrimitiveType::TRIANGLES);
    ssrRender.setShader(ssrShader.get());
    ssrSlotParams = ssrShader.get()->shaderData.getUniformBlockIndex(UniformBlockType::SSR_FS_PARAMS);
    if (!ssrRender.endLoad(PIP_RTT, false, true, CullingMode::BACK, WindingOrder::CCW))
        return;

    // fullscreen glossy blur pass
    ssrBlurRender.beginLoad(PrimitiveType::TRIANGLES);
    ssrBlurRender.setShader(ssrBlurShader.get());
    ssrBlurSlotParams = ssrBlurShader.get()->shaderData.getUniformBlockIndex(UniformBlockType::SSR_BLUR_FS_PARAMS);
    if (!ssrBlurRender.endLoad(PIP_RTT, false, true, CullingMode::BACK, WindingOrder::CCW))
        return;

    // composite targets either an offscreen framebuffer (PIP_RTT) or the swapchain
    // (PIP_DEFAULT), so build both pipeline variants.
    compositeRender.beginLoad(PrimitiveType::TRIANGLES);
    compositeRender.setShader(compositeShader.get());
    compositeSlotParams = compositeShader.get()->shaderData.getUniformBlockIndex(UniformBlockType::COMPOSITE_FS_PARAMS);
    if (!compositeRender.endLoad(PIP_RTT | PIP_DEFAULT, false, true, CullingMode::BACK, WindingOrder::CCW))
        return;

    ssrLoaded = true;
}

void RenderSystem::destroySSR(){
    if (ssrLoaded){
        ssrRender.destroy();
        ssrBlurRender.destroy();
        compositeRender.destroy();
    }
    if (ssrShader){
        ssrShader.reset();
        ShaderPool::remove(ShaderType::SSR, 0);
    }
    if (ssrBlurShader){
        ssrBlurShader.reset();
        ShaderPool::remove(ShaderType::SSR_BLUR, 0);
    }
    if (compositeShader){
        compositeShader.reset();
        ShaderPool::remove(ShaderType::COMPOSITE, 0);
    }

    gbufferFramebuffer.destroy();
    sceneColorFramebuffer.destroy();
    ssrFramebuffer.destroy();
    ssrBlurFramebuffer.destroy();

    ssrWidth = 0;
    ssrHeight = 0;
    ssrSlotParams = -1;
    ssrBlurSlotParams = -1;
    compositeSlotParams = -1;
    ssrLoaded = false;
}

bool RenderSystem::ensureSSRFramebuffers(unsigned int width, unsigned int height){
    if (width == 0 || height == 0)
        return false;

    if (sceneColorFramebuffer.isCreated() && ssrWidth == width && ssrHeight == height)
        return true;

    sceneColorFramebuffer.destroy();
    ssrFramebuffer.destroy();
    ssrBlurFramebuffer.destroy();

    // offscreen opaque scene color (sampled bilinearly by ssr.frag); has a depth
    // attachment so the redirected main color pass can depth-test normally.
    sceneColorFramebuffer.setWidth(width);
    sceneColorFramebuffer.setHeight(height);
    sceneColorFramebuffer.setMinFilter(TextureFilter::LINEAR);
    sceneColorFramebuffer.setMagFilter(TextureFilter::LINEAR);
    sceneColorFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    sceneColorFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    sceneColorFramebuffer.create();

    // reflection color + mask
    ssrFramebuffer.setWidth(width);
    ssrFramebuffer.setHeight(height);
    ssrFramebuffer.setMinFilter(TextureFilter::LINEAR);
    ssrFramebuffer.setMagFilter(TextureFilter::LINEAR);
    ssrFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    ssrFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    ssrFramebuffer.create();

    // glossy-blurred reflection (sampled by the composite when blur > 0)
    ssrBlurFramebuffer.setWidth(width);
    ssrBlurFramebuffer.setHeight(height);
    ssrBlurFramebuffer.setMinFilter(TextureFilter::LINEAR);
    ssrBlurFramebuffer.setMagFilter(TextureFilter::LINEAR);
    ssrBlurFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    ssrBlurFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    ssrBlurFramebuffer.create();

    ssrWidth = width;
    ssrHeight = height;

    return sceneColorFramebuffer.isCreated() && ssrFramebuffer.isCreated() && ssrBlurFramebuffer.isCreated();
}

void RenderSystem::renderSSR(CameraComponent& camera, FramebufferRender* destination){
    unsigned int w = ssrWidth;
    unsigned int h = ssrHeight;
    if (w == 0 || h == 0)
        return;

    // The depth buffer is in logical orientation; the offscreen scene color was
    // rendered with the flip an offscreen target uses, so on GL the two differ by a
    // Y flip (isRenderingFlipped, always true on GL here).
    float flipGL = isRenderingFlipped(camera) ? 1.0f : 0.0f;
    Matrix4 renderProj = camera.projectionMatrix;

    // --- 1. ssr pass: march the depth buffer, sample offscreen scene color ---
    fs_ssr.projection = renderProj;
    fs_ssr.invProjection = renderProj.inverse();
    fs_ssr.params = Vector4(scene->getSSRMaxDistance(), scene->getSSRThickness(), scene->getSSRIntensity(), (float)scene->getSSRMaxSteps());
    // misc.w = glossy-blur amount, used only to scale the march jitter (0 => sharp, no jitter)
    fs_ssr.misc = Vector4(1.0f / (float)w, 1.0f / (float)h, flipGL, scene->getSSRBlur());

    ssrPassRender.setClearColor(Vector4(0.0, 0.0, 0.0, 0.0));
    ssrPassRender.startRenderPass(&ssrFramebuffer.getRender());
    if (ssrRender.beginDraw(PIP_RTT)){
        ShaderData& sd = ssrShader.get()->shaderData;
        ssrRender.addTexture(sd.getTextureIndex(TextureShaderType::DEPTHTEXTURE), ShaderStageType::FRAGMENT, &gbufferFramebuffer.getRender().getColorAttachmentTexture(0));
        ssrRender.addTexture(sd.getTextureIndex(TextureShaderType::GBUFFERTEXTURE), ShaderStageType::FRAGMENT, &gbufferFramebuffer.getRender().getColorAttachmentTexture(1));
        ssrRender.addTexture(sd.getTextureIndex(TextureShaderType::SCENECOLORTEXTURE), ShaderStageType::FRAGMENT, &sceneColorFramebuffer.getRender().getColorTexture());
        ssrRender.applyUniformBlock(ssrSlotParams, sizeof(fs_ssr_t), &fs_ssr);
        ssrRender.draw(0, 3, 1);
    }
    ssrPassRender.endRenderPass();

    // --- 2. optional glossy blur pass (premultiplied) ---
    // ssrBlur in [0..1] maps to a pixel spread radius; 0 keeps sharp mirror
    // reflections and skips the pass entirely. The blur ramps smoothly with the
    // radius (the shader scales its tap offsets by it), so there is no hard step.
    const float ssrBlurMaxRadius = 24.0f;
    float blurRadius = scene->getSSRBlur() * ssrBlurMaxRadius;
    TextureRender* reflectionTexture = &ssrFramebuffer.getRender().getColorTexture();
    if (scene->getSSRBlur() > 0.001f && scene->getSSRDebugMode() == 0){
        // blurRadius is the MAX radius (full roughness); the blur shader scales it
        // per-pixel by the G-buffer roughness. w = flip (gbuffer is in depth space).
        fs_ssr_blur.params = Vector4(1.0f / (float)w, 1.0f / (float)h, blurRadius, flipGL);

        ssrPassRender.setClearColor(Vector4(0.0, 0.0, 0.0, 0.0));
        ssrPassRender.startRenderPass(&ssrBlurFramebuffer.getRender());
        if (ssrBlurRender.beginDraw(PIP_RTT)){
            ShaderData& bd = ssrBlurShader.get()->shaderData;
            ssrBlurRender.addTexture(bd.getTextureIndex(TextureShaderType::SSRTEXTURE), ShaderStageType::FRAGMENT, &ssrFramebuffer.getRender().getColorTexture());
            ssrBlurRender.addTexture(bd.getTextureIndex(TextureShaderType::GBUFFERTEXTURE), ShaderStageType::FRAGMENT, &gbufferFramebuffer.getRender().getColorAttachmentTexture(1));
            ssrBlurRender.applyUniformBlock(ssrBlurSlotParams, sizeof(fs_ssr_blur_t), &fs_ssr_blur);
            ssrBlurRender.draw(0, 3, 1);
        }
        ssrPassRender.endRenderPass();

        reflectionTexture = &ssrBlurFramebuffer.getRender().getColorTexture();
    }

    // --- 3. composite pass: energy-conserving SSR-over-IBL -> real destination ---
    // The composite recomputes the IBL specular term per pixel (for surfaces flagged
    // hasIBL in the G-buffer) and replaces it with the SSR reflection, so the env
    // reflection already in the scene is not double-counted.
    fs_composite.invProjection = renderProj.inverse();
    fs_composite.invView = camera.viewMatrix.inverse();

    // the GL swapchain keeps row 0 at the bottom while the offscreen sources do not,
    // so a swapchain destination samples them flipped (renderBlit does the same)
    float flipDest = (!destination && Engine::isOpenGL()) ? 1.0f : 0.0f;
    fs_composite.params = Vector4(scene->getSSRIntensity(), flipGL, (float)scene->getSSRDebugMode(), flipDest);

    // environment color/rotation, matching mesh.frag's lighting.envColor; the prefiltered
    // GGX map comes from the sky (fall back to a black cube when there is no IBL sky)
    fs_composite.envColor = Vector4(0.0, 0.0, 0.0, 0.0);
    TextureRender* prefilteredTexture = &emptyCubeBlack;
    {
        auto skys = scene->getComponentArray<SkyComponent>();
        if (skys->size() > 0){
            SkyComponent& sky = skys->getComponentFromIndex(0);
            if (sky.prefilteredMap){
                prefilteredTexture = sky.prefilteredMap.get();
                Vector3 linColor = Color::sRGBToLinear(Vector3(sky.color.x, sky.color.y, sky.color.z));
                fs_composite.envColor = Vector4(linColor.x, linColor.y, linColor.z, Angle::defaultToRad(sky.rotation));
            }
        }
    }

    ssrPassRender.setClearColor(scene->getBackgroundColor());
    PipelineType compositePip;
    if (destination){
        ssrPassRender.startRenderPass(destination);
        compositePip = PIP_RTT;
    }else{
        ssrPassRender.startRenderPass();
        ssrPassRender.applyViewport(Engine::getViewRect());
        compositePip = PIP_DEFAULT;
    }
    if (compositeRender.beginDraw(compositePip)){
        ShaderData& cd = compositeShader.get()->shaderData;
        compositeRender.addTexture(cd.getTextureIndex(TextureShaderType::SCENECOLORTEXTURE), ShaderStageType::FRAGMENT, &sceneColorFramebuffer.getRender().getColorTexture());
        compositeRender.addTexture(cd.getTextureIndex(TextureShaderType::SSRTEXTURE), ShaderStageType::FRAGMENT, reflectionTexture);
        compositeRender.addTexture(cd.getTextureIndex(TextureShaderType::DEPTHTEXTURE), ShaderStageType::FRAGMENT, &gbufferFramebuffer.getRender().getColorAttachmentTexture(0));
        compositeRender.addTexture(cd.getTextureIndex(TextureShaderType::GBUFFERTEXTURE), ShaderStageType::FRAGMENT, &gbufferFramebuffer.getRender().getColorAttachmentTexture(1));
        compositeRender.addTexture(cd.getTextureIndex(TextureShaderType::GBUFFERALBEDOTEXTURE), ShaderStageType::FRAGMENT, &gbufferFramebuffer.getRender().getColorAttachmentTexture(2));
        compositeRender.addTexture(cd.getTextureIndex(TextureShaderType::PREFILTEREDMAP), ShaderStageType::FRAGMENT, prefilteredTexture);
        compositeRender.applyUniformBlock(compositeSlotParams, sizeof(fs_composite_t), &fs_composite);
        compositeRender.draw(0, 3, 1);
    }
    ssrPassRender.endRenderPass();
}

void RenderSystem::loadBlit(){
    if (blitLoaded)
        return;

    // the fullscreen blit shader may still be building (async in the editor)
    blitShader = ShaderPool::get(ShaderType::BLIT, 0);
    if (!blitShader || !blitShader->isCreated())
        return;

    // the blit targets either the Engine framebuffer (PIP_RTT, editor) or the
    // swapchain (PIP_DEFAULT, exported builds), so build both pipeline variants.
    blitRender.beginLoad(PrimitiveType::TRIANGLES);
    blitRender.setShader(blitShader.get());
    blitSlotParams = blitShader.get()->shaderData.getUniformBlockIndex(UniformBlockType::BLIT_FS_PARAMS);
    if (!blitRender.endLoad(PIP_RTT | PIP_DEFAULT, false, true, CullingMode::BACK, WindingOrder::CCW))
        return;

    blitLoaded = true;
}

void RenderSystem::destroyBlit(){
    if (blitLoaded){
        blitRender.destroy();
    }
    if (blitShader){
        blitShader.reset();
        ShaderPool::remove(ShaderType::BLIT, 0);
    }

    fixedResFramebuffer.destroy();

    fixedResWidth = 0;
    fixedResHeight = 0;
    blitSlotParams = -1;
    blitLoaded = false;
}

bool RenderSystem::ensureFixedResFramebuffer(unsigned int width, unsigned int height, TextureFilter filter){
    if (width == 0 || height == 0)
        return false;

    if (fixedResFramebuffer.isCreated() && fixedResWidth == width && fixedResHeight == height
            && fixedResFramebuffer.getMinFilter() == filter)
        return true;

    fixedResFramebuffer.destroy();

    // offscreen scene color at the fixed game resolution; the min/mag filter is
    // what the upscale blit samples with (nearest = crisp pixels, linear = smooth)
    fixedResFramebuffer.setWidth(width);
    fixedResFramebuffer.setHeight(height);
    fixedResFramebuffer.setMinFilter(filter);
    fixedResFramebuffer.setMagFilter(filter);
    fixedResFramebuffer.setWrapU(TextureWrap::CLAMP_TO_EDGE);
    fixedResFramebuffer.setWrapV(TextureWrap::CLAMP_TO_EDGE);
    fixedResFramebuffer.create();

    fixedResWidth = width;
    fixedResHeight = height;

    return fixedResFramebuffer.isCreated();
}

void RenderSystem::renderBlit(TextureRender* source, Rect viewport){
    // bars outside the viewport keep the background color (same as the direct
    // path, which clears the whole destination before applying the viewport)
    fixedResPassRender.setClearColor(scene->getBackgroundColor());

    PipelineType blitPip;
    float flipGL = 0.0f;
    if (Engine::getFramebuffer()){
        if (!Engine::getFramebuffer()->isCreated()){
            Engine::getFramebuffer()->create();
        }
        // source and destination are both flipped on GL, so no un-flip needed
        fixedResPassRender.startRenderPass(&Engine::getFramebuffer()->getRender());
        blitPip = PIP_RTT;
    }else{
        fixedResPassRender.startRenderPass();
        blitPip = PIP_DEFAULT;
        // the source was rendered flipped (PIP_RTT) but the GL swapchain is not
        flipGL = Engine::isOpenGL() ? 1.0f : 0.0f;
    }
    fixedResPassRender.applyViewport(viewport);

    if (blitRender.beginDraw(blitPip)){
        ShaderData& bd = blitShader.get()->shaderData;
        blitRender.addTexture(bd.getTextureIndex(TextureShaderType::SCENECOLORTEXTURE), ShaderStageType::FRAGMENT, source);
        fs_blit.params = Vector4(flipGL, 0.0f, 0.0f, 0.0f);
        blitRender.applyUniformBlock(blitSlotParams, sizeof(fs_blit_t), &fs_blit);
        blitRender.draw(0, 3, 1);
    }
    fixedResPassRender.endRenderPass();
}

void RenderSystem::renderFixedResolutionBlit(){
    renderBlit(&fixedResFramebuffer.getRender().getColorTexture(), Engine::getViewRect());
}

// writes one post-process uniform at its reflected offset in the block
static void writePostProcessUniform(std::vector<uint8_t>& block, const ShaderUniform& uniform, const Vector4& value){
    float components[4] = {value.x, value.y, value.z, value.w};

    int count = 0;
    bool isInt = false;
    switch (uniform.type){
        case ShaderUniformType::FLOAT:  count = 1; break;
        case ShaderUniformType::FLOAT2: count = 2; break;
        case ShaderUniformType::FLOAT3: count = 3; break;
        case ShaderUniformType::FLOAT4: count = 4; break;
        case ShaderUniformType::INT:    count = 1; isInt = true; break;
        case ShaderUniformType::INT2:   count = 2; isInt = true; break;
        case ShaderUniformType::INT3:   count = 3; isInt = true; break;
        case ShaderUniformType::INT4:   count = 4; isInt = true; break;
        default: return; // matrices are not editable
    }

    if ((size_t)(uniform.offset + count * 4) > block.size())
        return;

    for (int i = 0; i < count; i++){
        if (isInt){
            int intValue = (int)components[i];
            memcpy(block.data() + uniform.offset + i * 4, &intValue, 4);
        }else{
            memcpy(block.data() + uniform.offset + i * 4, &components[i], 4);
        }
    }
}

void RenderSystem::needReloadPostProcess(){
    postProcessNeedReload = true;
}

void RenderSystem::loadPostProcess(){
    if (postProcessLoaded && !postProcessNeedReload)
        return;

    // a pass whose shader is still building aborts the attempt, so the partial
    // chain is always dropped before rebuilding
    for (int i = 0; i < (int)postProcessPasses.size(); i++){
        postProcessPasses[i].render.destroy();
        postProcessPasses[i].shader.reset();
    }
    postProcessPasses.clear();
    postProcessNeedReload = false;
    postProcessLoaded = false;
    postProcessNeedsDepth = false;
    postProcessNeedsGBuffer = false;

    const std::vector<PostProcessPass>& passes = scene->getPostProcessPasses();

    for (int i = 0; i < (int)passes.size(); i++){
        if (!passes[i].enabled)
            continue;

        PostProcessRuntime pass;
        pass.slotParams = -1;
        pass.hasResolution = false;
        pass.hasTime = false;

        // an empty path is the built-in passthrough, also the fallback of a failed fork
        pass.customId = ShaderPool::registerCustomShader(passes[i].shader);
        pass.shader = ShaderPool::get(ShaderType::POSTPROCESS, 0, pass.customId);
        if (pass.customId != 0 && ShaderPool::isShaderBuildFailed(ShaderType::POSTPROCESS, 0, pass.customId)){
            // the pool already logged the failure; the panel marks the pass
            pass.customId = 0;
            pass.shader = ShaderPool::get(ShaderType::POSTPROCESS, 0);
        }
        // shaders may still be building (async in the editor)
        if (!pass.shader || !pass.shader->isCreated())
            return;

        pass.render.beginLoad(PrimitiveType::TRIANGLES);
        pass.render.setShader(pass.shader.get());
        // the chain never targets the swapchain directly, renderPostProcess blits for that
        if (!pass.render.endLoad(PIP_RTT, false, true, CullingMode::BACK, WindingOrder::CCW))
            return;

        ShaderData& sd = pass.shader.get()->shaderData;
        pass.slotSceneColor = sd.getTextureIndexByName("u_sceneColorTexture");
        pass.slotDepth = sd.getTextureIndexByName("u_depthTexture");
        pass.slotGBuffer = sd.getTextureIndexByName("u_gbufferTexture");
        pass.slotSSAO = sd.getTextureIndexByName("u_ssaoTexture");

        if (pass.slotDepth.first != -1)
            postProcessNeedsDepth = true;
        if (pass.slotGBuffer.first != -1)
            postProcessNeedsGBuffer = true;

        unsigned int sizeBytes = 0;
        const std::vector<ShaderUniform>* members = sd.getUniformBlockMembers("u_fs_postParams", sizeBytes);
        if (members && sizeBytes > 0){
            pass.slotParams = sd.getUniformBlockIndexByName("u_fs_postParams");
            // the backend declares the block rounded up to the std140 vec4 stride
            pass.params.assign(((sizeBytes + 15) / 16) * 16, 0);

            for (int m = 0; m < (int)members->size(); m++){
                const ShaderUniform& uniform = (*members)[m];
                std::string name = ShaderData::getUniformShortName(uniform.name);

                if (name == "resolution"){
                    pass.hasResolution = true;
                    pass.resolutionUniform = uniform;
                }else if (name == "time"){
                    pass.hasTime = true;
                    pass.timeUniform = uniform;
                }else{
                    // stored by name, so a renamed or removed member just drops
                    for (int v = 0; v < (int)passes[i].uniforms.size(); v++){
                        if (passes[i].uniforms[v].first == name){
                            writePostProcessUniform(pass.params, uniform, passes[i].uniforms[v].second);
                            break;
                        }
                    }
                }
            }
        }

        postProcessPasses.push_back(std::move(pass));
    }

    postProcessLoaded = true;
}

void RenderSystem::destroyPostProcess(){
    for (int i = 0; i < (int)postProcessPasses.size(); i++){
        postProcessPasses[i].render.destroy();
        postProcessPasses[i].shader.reset();
        ShaderPool::remove(ShaderType::POSTPROCESS, 0, postProcessPasses[i].customId);
    }
    postProcessPasses.clear();

    postProcessFramebuffer[0].destroy();
    postProcessFramebuffer[1].destroy();

    postProcessWidth = 0;
    postProcessHeight = 0;
    postProcessNeedsDepth = false;
    postProcessNeedsGBuffer = false;
    postProcessNeedReload = false;
    postProcessLoaded = false;
}

bool RenderSystem::ensurePostProcessFramebuffers(unsigned int width, unsigned int height){
    if (width == 0 || height == 0)
        return false;

    if (postProcessFramebuffer[0].isCreated() && postProcessWidth == width && postProcessHeight == height)
        return true;

    // the chain ping-pongs between the two; its last pass writes the real destination
    for (int i = 0; i < 2; i++){
        postProcessFramebuffer[i].destroy();
        postProcessFramebuffer[i].setWidth(width);
        postProcessFramebuffer[i].setHeight(height);
        postProcessFramebuffer[i].setMinFilter(TextureFilter::LINEAR);
        postProcessFramebuffer[i].setMagFilter(TextureFilter::LINEAR);
        postProcessFramebuffer[i].setWrapU(TextureWrap::CLAMP_TO_EDGE);
        postProcessFramebuffer[i].setWrapV(TextureWrap::CLAMP_TO_EDGE);
        postProcessFramebuffer[i].create();
    }

    postProcessWidth = width;
    postProcessHeight = height;

    return postProcessFramebuffer[0].isCreated() && postProcessFramebuffer[1].isCreated();
}

void RenderSystem::renderPostProcess(FramebufferRender* destination){
    // depth is the SSR G-buffer or the chain's own pre-pass; the G-buffer
    // normal/roughness only exists while SSR is on
    TextureRender* depthTexture = &emptyBlack;
    TextureRender* gbufferTexture = &emptyBlack;
    if (postProcessNeedsGBuffer && gbufferFramebuffer.isCreated()){
        gbufferTexture = &gbufferFramebuffer.getRender().getColorAttachmentTexture(1);
    }
    if (postProcessNeedsDepth){
        if (gbufferFramebuffer.isCreated()){
            depthTexture = &gbufferFramebuffer.getRender().getColorAttachmentTexture(0);
        }else if (ssaoDepthFramebuffer.isCreated()){
            depthTexture = &ssaoDepthFramebuffer.getRender().getColorTexture();
        }
    }
    TextureRender* ssaoTexture = currentSSAOTexture ? currentSSAOTexture : &emptyWhite;

    // the color pass (or the SSR composite) was redirected into buffer 0
    TextureRender* input = &postProcessFramebuffer[0].getRender().getColorTexture();
    int pingPong = 1;

    for (int i = 0; i < (int)postProcessPasses.size(); i++){
        PostProcessRuntime& pass = postProcessPasses[i];
        bool writeDestination = destination && (i + 1 == (int)postProcessPasses.size());

        // rewritten every frame so they survive a resize and can animate
        if (pass.hasResolution){
            float w = (float)postProcessWidth;
            float h = (float)postProcessHeight;
            writePostProcessUniform(pass.params, pass.resolutionUniform, Vector4(w, h, 1.0f / w, 1.0f / h));
        }
        if (pass.hasTime){
            float seconds = (float)Engine::getSystemTime();
            writePostProcessUniform(pass.params, pass.timeUniform, Vector4(seconds, seconds, seconds, seconds));
        }

        if (writeDestination){
            postProcessPassRender.startRenderPass(destination);
        }else{
            postProcessPassRender.startRenderPass(&postProcessFramebuffer[pingPong].getRender());
        }

        if (pass.render.beginDraw(PIP_RTT)){
            pass.render.addTexture(pass.slotSceneColor, ShaderStageType::FRAGMENT, input);
            pass.render.addTexture(pass.slotDepth, ShaderStageType::FRAGMENT, depthTexture);
            pass.render.addTexture(pass.slotGBuffer, ShaderStageType::FRAGMENT, gbufferTexture);
            pass.render.addTexture(pass.slotSSAO, ShaderStageType::FRAGMENT, ssaoTexture);
            pass.render.applyUniformBlock(pass.slotParams, (unsigned int)pass.params.size(), pass.params.data());
            pass.render.draw(0, 3, 1);
        }
        postProcessPassRender.endRenderPass();

        if (!writeDestination){
            input = &postProcessFramebuffer[pingPong].getRender().getColorTexture();
            pingPong = 1 - pingPong;
        }
    }

    // the swapchain is written by the blit pass, which handles the GL orientation flip
    if (!destination){
        renderBlit(input, Engine::getViewRect());
    }
}

void RenderSystem::presentFramebufferToSwapchain(Framebuffer* source){
    if (!source || !source->isCreated())
        return;

    loadBlit();
    if (!blitLoaded)
        return;

    // the scene passes already letterboxed into the composite, so copy it 1:1
    renderBlit(&source->getRender().getColorTexture(), Rect(0, 0,
            (float)System::instance().getScreenWidth(), (float)System::instance().getScreenHeight()));
}

void RenderSystem::destroyMesh(Entity entity, MeshComponent& mesh, bool clearAssets){
    InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
    TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
    const bool preserveAssets = mesh.needReload && !clearAssets;

    if (!mesh.loaded)
        return;

    for (int i = 0; i < mesh.numSubmeshes; i++){

        Submesh& submesh = mesh.submeshes[i];
        if (!preserveAssets){
            //Destroy shader
            if (submesh.shader){
                submesh.shader.reset();
                ShaderPool::remove(ShaderType::MESH, submesh.shaderProperties, submesh.customShaderId);
            }
            // depth shader may have been loaded for shadows and/or SSAO
            if (submesh.depthShader){
                submesh.depthShader.reset();
                ShaderPool::remove(ShaderType::DEPTH, submesh.depthShaderProperties);
            }
            // G-buffer shader may have been loaded for SSR
            if (submesh.gbufferShader){
                submesh.gbufferShader.reset();
                ShaderPool::remove(ShaderType::GBUFFER, submesh.gbufferShaderProperties);
            }

            //Destroy texture
            submesh.material.baseColorTexture.destroy();
            submesh.material.metallicRoughnessTexture.destroy();
            submesh.material.normalTexture.destroy();
            submesh.material.occlusionTexture.destroy();
            submesh.material.emissiveTexture.destroy();
        }

        if (terrain){
            //Destroy terrain texture
            if (!preserveAssets){
                terrain->heightMap.destroy();
                terrain->blendMap.destroy();
                terrain->textureDetailRed.destroy();
                terrain->textureDetailGreen.destroy();
                terrain->textureDetailBlue.destroy();
            }

            //Destroy terrain buffer
            for (int v = 0; v < MAX_TERRAIN_VIEWS; v++){
                for (int s = 0; s < 2; s++){
                    terrain->views[v].nodesbuffer[s].getRender()->destroyBuffer();
                }
            }
        }

        if (instmesh){
            //Destroy instmesh buffer
            if (!preserveAssets){
                instmesh->buffer.clearAll();
            }
            instmesh->buffer.getRender()->destroyBuffer();
        }

        //Destroy render
        submesh.render.destroy();
        submesh.depthRender.destroy();
        submesh.gbufferRender.destroy();

        //Shaders uniforms
        submesh.slotVSParams = -1;
        submesh.slotFSParams = -1;
        submesh.slotFSTexCoordSets = -1;
        submesh.slotFSLighting = -1;
        submesh.slotFSReflectionProbe = -1;
        submesh.slotVSSprite = -1;
        submesh.slotVSShadows = -1;
        submesh.slotFSShadows = -1;
        submesh.slotFSPointShadows = -1;
        submesh.slotVSSkinning = -1;
        submesh.slotVSMorphTarget = -1;
        submesh.slotVSTerrain = -1;

        submesh.slotVSDepthParams = -1;
        submesh.slotFSDepthMaterial = -1;
        submesh.slotVSDepthSkinning = -1;
        submesh.slotVSDepthMorphTarget = -1;
        submesh.slotVSDepthTerrain = -1;

        submesh.slotVSGBufferParams = -1;
        submesh.slotFSGBufferMaterial = -1;
        submesh.slotVSGBufferSkinning = -1;
        submesh.slotVSGBufferMorphTarget = -1;
        submesh.slotVSGBufferTerrain = -1;
    }

    //Destroy buffer
    if (!preserveAssets){
        mesh.buffer.clearAll();
        mesh.indices.clearAll();
    }
    mesh.buffer.getRender()->destroyBuffer();
    mesh.indices.getRender()->destroyBuffer();
    mesh.bonesBuffer.destroyBuffer();
    mesh.bonesTexture.destroyTexture();
    for (int i = 0; i < mesh.numExternalBuffers; i++){
        if (!preserveAssets){
            mesh.eBuffers[i].clearAll();
        }
        mesh.eBuffers[i].getRender()->destroyBuffer();
    }

    if (mesh.needReload){
        // Reload destroys GPU objects immediately; reset load state synchronously so
        // update() can call loadMesh in the same frame.
        // Queuing changeDestroy would leave mesh.loaded true until later, so draw()
        // could run with stale bindings and trip Sokol validation.
        mesh.loaded = false;
        mesh.loadCalled = false;
    }else{
        SystemRender::addQueueCommand(&changeDestroy, new check_load_t{scene, entity});
    }
}

void RenderSystem::prepareMeshForDataReload(Entity entity, MeshComponent& mesh){
    if (!mesh.loaded)
        return;

    // A structural model reload replaces submesh and external-buffer counts. Tear down using
    // the old counts before MeshSystem mutates them, while retaining the synchronous reload state.
    mesh.needReload = true;
    destroyMesh(entity, mesh, true);
}

bool RenderSystem::loadUI(Entity entity, UIComponent& ui, uint8_t pipelines, bool isText){

    if (!Engine::isViewLoaded()) 
        return false;

    ObjectRender& render = ui.render;

    render.beginLoad(ui.primitiveType);

    bool hasUITexture = !ui.texture.empty();

    bool p_hasTexture = false;
    bool p_vertexColorVec4 = true;
    bool p_hasFontAtlasTexture = false;
    if (hasUITexture){
        if (isText){
            p_vertexColorVec4 = false;
            p_hasFontAtlasTexture = true;
        }else{
            p_hasTexture = true;
        }
    }

    ui.shaderProperties = ShaderPool::getUIProperties(p_hasTexture, p_hasFontAtlasTexture, false, p_vertexColorVec4);
    const std::string& uiShaderSrc = ui.customShader.empty() ? scene->getDefaultCustomShader(ShaderType::UI) : ui.customShader;
    ui.customShaderId = ShaderPool::registerCustomShader(uiShaderSrc);
    ui.shader = ShaderPool::get(ShaderType::UI, ui.shaderProperties, ui.customShaderId);
    if (!ui.shader->isCreated()){
        // A custom shader that failed to compile falls back to the built-in shader (so the
        // object stays visible and loading finishes); retried after its source changes.
        if (ui.customShaderId != 0 && ShaderPool::isShaderBuildFailed(ShaderType::UI, ui.shaderProperties, ui.customShaderId)){
            Log::error("Custom shader '%s' failed to compile; using the built-in shader", uiShaderSrc.c_str());
            ui.customShaderId = 0;
            ui.shader = ShaderPool::get(ShaderType::UI, ui.shaderProperties, 0);
        }
        if (!ui.shader->isCreated())
            return false;
    }
    render.setShader(ui.shader.get());
    ShaderData& shaderData = ui.shader.get()->shaderData;

    ui.slotVSParams = shaderData.getUniformBlockIndex(UniformBlockType::UI_VS_PARAMS);
    ui.slotFSParams = shaderData.getUniformBlockIndex(UniformBlockType::UI_FS_PARAMS);

    size_t bufferSize;
    size_t minBufferSize;

    bufferSize = ui.buffer.getSize();
    minBufferSize = ui.minBufferCount * ui.buffer.getStride();
    if (minBufferSize > bufferSize)
        bufferSize = minBufferSize;

    if (bufferSize == 0)
        return false;

    ui.buffer.getRender()->createBuffer(bufferSize, ui.buffer.getData(), ui.buffer.getType(), ui.buffer.getUsage());
    if (ui.buffer.isRenderAttributes()) {
        for (auto const &attr : ui.buffer.getAttributes()) {
            render.addAttribute(shaderData.getAttrIndex(attr.first), ui.buffer.getRender(), attr.second.getElements(), attr.second.getDataType(), ui.buffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
        }
    }
    if (ui.buffer.getUsage() != BufferUsage::IMMUTABLE){
        ui.needUpdateBuffer = true;
    }

    bufferSize = ui.indices.getSize();
    minBufferSize = ui.minIndicesCount * ui.indices.getStride();
    if (minBufferSize > bufferSize)
        bufferSize = minBufferSize;

    if (ui.indices.getCount() > 0){
        ui.indices.getRender()->createBuffer(bufferSize, ui.indices.getData(), ui.indices.getType(), ui.indices.getUsage());
        ui.vertexCount = ui.indices.getCount();
        Attribute indexattr = ui.indices.getAttributes()[AttributeType::INDEX];
        render.setIndex(ui.indices.getRender(), indexattr.getDataType(), indexattr.getOffset());
        if (ui.indices.getUsage() != BufferUsage::IMMUTABLE){
            ui.needUpdateBuffer = true;
        }
    }else{
        ui.vertexCount = ui.buffer.getCount();
    }

    if (TextureRender* textureRender = ui.texture.getRender(&emptyWhite)){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(shaderData.getTextureIndex(TextureShaderType::UI), ShaderStageType::FRAGMENT, textureRender);
    }
    
    ui.needUpdateTexture = false;

    if (!render.endLoad(pipelines, false, true, CullingMode::BACK, WindingOrder::CCW)){
        return false;
    }

    ui.needReload = false;
    ui.needUpdateAABB = true;
    ui.loadCalled = true;
    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

bool RenderSystem::drawUI(UIComponent& ui, Transform& transform, bool renderToTexture){
    if (ui.loaded && ui.buffer.getSize() > 0){

        if (ui.needUpdateTexture || ui.texture.isFramebufferOutdated()){
            ShaderData& shaderData = ui.shader.get()->shaderData;
            if (TextureRender* textureRender = ui.texture.getRender(&emptyWhite))
                if (textureRender->isCreated()){
                    ui.render.addTexture(shaderData.getTextureIndex(TextureShaderType::UI), ShaderStageType::FRAGMENT, textureRender);
                    ui.needUpdateTexture = false;
                }
        }

        if (ui.needUpdateBuffer){
            ui.buffer.getRender()->updateBuffer(ui.buffer.getSize(), ui.buffer.getData());

            if (ui.indices.getCount() > 0){
                ui.indices.getRender()->updateBuffer(ui.indices.getSize(), ui.indices.getData());
                ui.vertexCount = ui.indices.getCount();
            }else{
                ui.vertexCount = ui.buffer.getCount();
            }

            ui.needUpdateBuffer = false;
        }

        ObjectRender& render = ui.render;

        if (!render.beginDraw((renderToTexture)?PIP_RTT:PIP_DEFAULT)){
            ui.needReload = true;
            return false;
        }
        render.applyUniformBlock(ui.slotVSParams, sizeof(float) * 16, &transform.modelViewProjectionMatrix);
        //Color
        render.applyUniformBlock(ui.slotFSParams, sizeof(float) * 4, &ui.color);
        render.draw(0, ui.vertexCount, 1);

    }

    return true;
}

void RenderSystem::destroyUI(Entity entity, UIComponent& ui){
    TextComponent* text = scene->findComponent<TextComponent>(entity);

    if (!ui.loaded)
        return;

    if (!ui.needReload){
        //Destroy shader
        if (ui.shader){
            ui.shader.reset();
            ShaderPool::remove(ShaderType::UI, ui.shaderProperties, ui.customShaderId);
        }

        //Destroy texture
        ui.texture.destroy();
    }

    //Destroy render
    ui.render.destroy();

    //Destroy buffer
    if (!ui.needReload){
        ui.buffer.clearAll();
        ui.indices.clearAll();
    }
    ui.buffer.getRender()->destroyBuffer();
    ui.indices.getRender()->destroyBuffer();

    if (text){
        text->needReloadAtlas = true;
    }

    //Shaders uniforms
    ui.slotVSParams = -1;
    ui.slotFSParams = -1;

    SystemRender::addQueueCommand(&changeDestroy, new check_load_t{scene, entity});
}

bool RenderSystem::loadPoints(Entity entity, PointsComponent& points, uint8_t pipelines){

    if (!Engine::isViewLoaded()) 
        return false;

    ObjectRender& render = points.render;

    render.beginLoad(PrimitiveType::POINTS);

    if (points.autoTransparency && !points.transparent){
        if (points.texture.isTransparent()){ // Particle color is not tested here
            points.transparent = true;
        }
    }

    bool hasPointsTexture = !points.texture.empty();

    bool p_hasTexture = false;
    bool p_hasTextureRect = false;
    if (hasPointsTexture){
        p_hasTexture = true;
        if (points.hasTextureRect){
            p_hasTextureRect = true;
        }
    }

    points.shaderProperties = ShaderPool::getPointsProperties(p_hasTexture, false, true, p_hasTextureRect);
    const std::string& pointsShaderSrc = points.customShader.empty() ? scene->getDefaultCustomShader(ShaderType::POINTS) : points.customShader;
    points.customShaderId = ShaderPool::registerCustomShader(pointsShaderSrc);
    points.shader = ShaderPool::get(ShaderType::POINTS, points.shaderProperties, points.customShaderId);
    if (!points.shader->isCreated()){
        // A custom shader that failed to compile falls back to the built-in shader (so the
        // object stays visible and loading finishes); retried after its source changes.
        if (points.customShaderId != 0 && ShaderPool::isShaderBuildFailed(ShaderType::POINTS, points.shaderProperties, points.customShaderId)){
            Log::error("Custom shader '%s' failed to compile; using the built-in shader", pointsShaderSrc.c_str());
            points.customShaderId = 0;
            points.shader = ShaderPool::get(ShaderType::POINTS, points.shaderProperties, 0);
        }
        if (!points.shader->isCreated())
            return false;
    }
    render.setShader(points.shader.get());
    ShaderData& shaderData = points.shader.get()->shaderData;

    points.slotVSParams = shaderData.getUniformBlockIndex(UniformBlockType::POINTS_VS_PARAMS);

    points.buffer.clear();
    points.buffer.addAttribute(AttributeType::POSITION, 3, 0);
    points.buffer.addAttribute(AttributeType::COLOR, 4, 3 * sizeof(float));
    points.buffer.addAttribute(AttributeType::POINTSIZE, 1, 7 * sizeof(float));
    points.buffer.addAttribute(AttributeType::POINTROTATION, 1, 8 * sizeof(float));
    points.buffer.addAttribute(AttributeType::TEXTURERECT, 4, 9 * sizeof(float));
    points.buffer.setStride(13 * sizeof(float));
    points.buffer.setRenderAttributes(true);
    points.buffer.setUsage(BufferUsage::STREAM);

    // Now buffer size is zero than it needed to be calculated
    size_t bufferSize = points.maxPoints * points.buffer.getStride();

    points.buffer.getRender()->createBuffer(bufferSize, points.buffer.getData(), points.buffer.getType(), points.buffer.getUsage());
    if (points.buffer.isRenderAttributes()) {
        for (auto const &attr : points.buffer.getAttributes()) {
            render.addAttribute(shaderData.getAttrIndex(attr.first), points.buffer.getRender(), attr.second.getElements(), attr.second.getDataType(), points.buffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
        }
    }

    points.needUpdateBuffer = true;

    if (TextureRender* textureRender = points.texture.getRender(&emptyWhite)){
        if (!textureRender->isCreated()){
            return false;
        }
        render.addTexture(shaderData.getTextureIndex(TextureShaderType::POINTS), ShaderStageType::FRAGMENT, textureRender);
    }

    points.needUpdateTexture = false;

    if (!render.endLoad(pipelines, false, true, CullingMode::BACK, WindingOrder::CCW)){
        return false;
    }

    points.needReload = false;
    points.loadCalled = true;
    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

bool RenderSystem::loadLines(Entity entity, LinesComponent& lines, uint8_t pipelines){

    if (!Engine::isViewLoaded()) 
        return false;

    if (lines.lines.size() == 0)
        return false;

    ObjectRender& render = lines.render;

    render.beginLoad(PrimitiveType::LINES);

    lines.shaderProperties = ShaderPool::getLinesProperties(false, true);
    const std::string& linesShaderSrc = lines.customShader.empty() ? scene->getDefaultCustomShader(ShaderType::LINES) : lines.customShader;
    lines.customShaderId = ShaderPool::registerCustomShader(linesShaderSrc);
    lines.shader = ShaderPool::get(ShaderType::LINES, lines.shaderProperties, lines.customShaderId);
    if (!lines.shader->isCreated()){
        // A custom shader that failed to compile falls back to the built-in shader (so the
        // object stays visible and loading finishes); retried after its source changes.
        if (lines.customShaderId != 0 && ShaderPool::isShaderBuildFailed(ShaderType::LINES, lines.shaderProperties, lines.customShaderId)){
            Log::error("Custom shader '%s' failed to compile; using the built-in shader", linesShaderSrc.c_str());
            lines.customShaderId = 0;
            lines.shader = ShaderPool::get(ShaderType::LINES, lines.shaderProperties, 0);
        }
        if (!lines.shader->isCreated())
            return false;
    }
    render.setShader(lines.shader.get());
    ShaderData& shaderData = lines.shader.get()->shaderData;

    lines.slotVSParams = shaderData.getUniformBlockIndex(UniformBlockType::LINES_VS_PARAMS);

    lines.buffer.clear();
    lines.buffer.addAttribute(AttributeType::POSITION, 3, 0);
    lines.buffer.addAttribute(AttributeType::COLOR, 4, 3 * sizeof(float));
    lines.buffer.setStride(7 * sizeof(float));
    lines.buffer.setRenderAttributes(true);
    lines.buffer.setUsage(BufferUsage::DYNAMIC);

    // two points per line
    size_t bufferSize = lines.maxLines * (lines.buffer.getStride() * 2);

    if (bufferSize == 0)
        return false;

    lines.buffer.getRender()->createBuffer(bufferSize, lines.buffer.getData(), lines.buffer.getType(), lines.buffer.getUsage());
    if (lines.buffer.isRenderAttributes()) {
        for (auto const &attr : lines.buffer.getAttributes()) {
            render.addAttribute(shaderData.getAttrIndex(attr.first), lines.buffer.getRender(), attr.second.getElements(), attr.second.getDataType(), lines.buffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
        }
    }
    // buffer is dynamic
    lines.needUpdateBuffer = true;

    if (!render.endLoad(pipelines, false, true, CullingMode::BACK, WindingOrder::CCW)){
        return false;
    }

    lines.needReload = false;
    lines.loadCalled = true;
    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

float RenderSystem::getPointsViewportHeight(const CameraComponent& camera) const{
    if (camera.renderToTexture && camera.framebuffer){
        return (float)camera.framebuffer->getHeight();
    }
    if (isFixedResolutionActive()){
        // the main color pass renders into the fixed-resolution target
        return (float)scene->getFixedResolutionHeight();
    }
    if (Framebuffer* engineFb = Engine::getFramebuffer()){
        if (engineFb->isCreated()){
            return (float)engineFb->getHeight();
        }
    }
    float viewHeight = Engine::getViewRect().getHeight();
    if (viewHeight > 0.0f){
        return viewHeight;
    }
    return (float)Engine::getCanvasHeight();
}

float RenderSystem::computePointsScale(const CameraComponent& camera, float viewportHeight) const{
    if (viewportHeight <= 0.0f){
        return 1.0f;
    }

    if (camera.type == CameraType::CAMERA_PERSPECTIVE){
        float tanHalfFov = tanf(camera.yfov * 0.5f);
        if (tanHalfFov <= 0.0f){
            return 1.0f;
        }
        return viewportHeight / (2.0f * tanHalfFov);
    }

    float orthoHeight = 0.0f;
    if (camera.type == CameraType::CAMERA_UI){
        orthoHeight = fabsf(camera.topClip - camera.bottomClip);
    }else{
        orthoHeight = camera.topClip - camera.bottomClip;
    }

    if (orthoHeight <= 0.0f){
        return 1.0f;
    }

    return viewportHeight / orthoHeight;
}

bool RenderSystem::drawPoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform, bool renderToTexture){
    if (points.loaded && points.numVisible > 0){

        if (points.needUpdateTexture || points.texture.isFramebufferOutdated()){
            ShaderData& shaderData = points.shader.get()->shaderData;
            if (TextureRender* textureRender = points.texture.getRender(&emptyWhite)){
                if (textureRender->isCreated()){
                    points.render.addTexture(shaderData.getTextureIndex(TextureShaderType::POINTS), ShaderStageType::FRAGMENT, textureRender);
                    points.needUpdateTexture = false;
                }
            }
        }

        if (points.needUpdateBuffer){
            // setData here because component can change order and lose reference
            points.buffer.setData((unsigned char*)(&points.renderPoints[0]), sizeof(PointRenderData)*points.numVisible);
            points.buffer.getRender()->updateBuffer(points.buffer.getSize(), points.buffer.getData());
            points.needUpdateBuffer = false;
        }

        ObjectRender& render = points.render;

        if (!render.beginDraw((renderToTexture)?PIP_RTT:PIP_DEFAULT)){
            points.needReload = true;
            return false;
        }
        vs_points_params_t vsParams = {};
        vsParams.mvpMatrix = transform.modelViewProjectionMatrix;
        vsParams.pointScale = computePointsScale(camera, getPointsViewportHeight(camera));
        render.applyUniformBlock(points.slotVSParams, sizeof(vs_points_params_t), &vsParams);
        render.draw(0, points.numVisible, 1);
    }

    return true;
}

void RenderSystem::destroyPoints(Entity entity, PointsComponent& points){
    if (!points.loaded)
        return;

    if (!points.needReload){
        //Destroy shader
        if (points.shader){
            points.shader.reset();
            ShaderPool::remove(ShaderType::POINTS, points.shaderProperties, points.customShaderId);
        }

        //Destroy texture
        points.texture.destroy();
    }

    //Destroy render
    points.render.destroy();

    //Destroy buffer
    if (!points.needReload){
        points.buffer.clearAll();
    }
    points.buffer.getRender()->destroyBuffer();

    //Shaders uniforms
    points.slotVSParams = -1;

    SystemRender::addQueueCommand(&changeDestroy, new check_load_t{scene, entity});
}

bool RenderSystem::drawLines(LinesComponent& lines, Transform& transform, Transform& camTransform, bool renderToTexture){
    if (lines.loaded && lines.lines.size() > 0){

        if (lines.needUpdateBuffer){
            // setData here because component can change order and lose reference
            lines.buffer.setData((unsigned char*)(&lines.lines[0]), sizeof(LineData)*lines.lines.size());
            lines.buffer.getRender()->updateBuffer(lines.buffer.getSize(), lines.buffer.getData());
            lines.needUpdateBuffer = false;
        }

        ObjectRender& render = lines.render;

        if (!render.beginDraw((renderToTexture)?PIP_RTT:PIP_DEFAULT)){
            lines.needReload = true;
            return false;
        }
        render.applyUniformBlock(lines.slotVSParams, sizeof(float) * 16, &transform.modelViewProjectionMatrix);
        render.draw(0, lines.lines.size() * 2, 1);
    }

    return true;
}

void RenderSystem::destroyLines(Entity entity, LinesComponent& lines){
    if (!lines.loaded)
        return;

    if (!lines.needReload){
        //Destroy shader
        if (lines.shader){
            lines.shader.reset();
            ShaderPool::remove(ShaderType::LINES, lines.shaderProperties, lines.customShaderId);
        }
    }

    //Destroy render
    lines.render.destroy();

    //Destroy buffer
    if (!lines.needReload){
        lines.buffer.clearAll();
    }
    lines.buffer.getRender()->destroyBuffer();

    //Shaders uniforms
    lines.slotVSParams = -1;

    SystemRender::addQueueCommand(&changeDestroy, new check_load_t{scene, entity});
}

bool RenderSystem::loadSky(Entity entity, SkyComponent& sky, uint8_t pipelines){

    if (!Engine::isViewLoaded()) 
        return false;	

    sky.buffer.clear();
    sky.buffer.addAttribute(AttributeType::POSITION, 3);

    Attribute* attVertex = sky.buffer.getAttribute(AttributeType::POSITION);

    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f, -1.0f));

    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f,  1.0f));

    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f, -1.0f));

    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f,  1.0f));

    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f,  1.0f, -1.0f));

    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f, -1.0f));
    sky.buffer.addVector3(attVertex, Vector3(-1.0f, -1.0f,  1.0f));
    sky.buffer.addVector3(attVertex, Vector3(1.0f, -1.0f,  1.0f));
    ObjectRender* render = &sky.render;

    render->beginLoad(PrimitiveType::TRIANGLES);

    const std::string& skyShaderSrc = sky.customShader.empty() ? scene->getDefaultCustomShader(ShaderType::SKYBOX) : sky.customShader;
    sky.customShaderId = ShaderPool::registerCustomShader(skyShaderSrc);
    sky.shader = ShaderPool::get(ShaderType::SKYBOX, 0, sky.customShaderId);
    if (!sky.shader->isCreated()){
        // A custom shader that failed to compile falls back to the built-in shader (so the
        // object stays visible and loading finishes); retried after its source changes.
        if (sky.customShaderId != 0 && ShaderPool::isShaderBuildFailed(ShaderType::SKYBOX, 0, sky.customShaderId)){
            Log::error("Custom shader '%s' failed to compile; using the built-in shader", skyShaderSrc.c_str());
            sky.customShaderId = 0;
            sky.shader = ShaderPool::get(ShaderType::SKYBOX, 0, 0);
        }
        if (!sky.shader->isCreated())
            return false;
    }
    render->setShader(sky.shader.get());
    ShaderData& shaderData = sky.shader.get()->shaderData;

    sky.slotVSParams = shaderData.getUniformBlockIndex(UniformBlockType::SKY_VS_PARAMS);
    sky.slotFSParams = shaderData.getUniformBlockIndex(UniformBlockType::SKY_FS_PARAMS);

    if (TextureRender* textureRender = sky.texture.getRender(&emptyCubeWhite)){
        if (!textureRender->isCreated()){
            return false;
        }
        render->addTexture(shaderData.getTextureIndex(TextureShaderType::SKYCUBE), ShaderStageType::FRAGMENT, textureRender);
    } else {
        return false;
    }

    sky.needUpdateTexture = false;

    sky.buffer.getRender()->createBuffer(sky.buffer.getSize(), sky.buffer.getData(), sky.buffer.getType(), sky.buffer.getUsage());

    if (sky.buffer.isRenderAttributes()) {
        for (auto const &attr : sky.buffer.getAttributes()) {
            render->addAttribute(shaderData.getAttrIndex(attr.first), sky.buffer.getRender(), attr.second.getElements(), attr.second.getDataType(), sky.buffer.getStride(), attr.second.getOffset(), attr.second.getNormalized(),  attr.second.getPerInstance());
        }
    }

    if (!render->endLoad(pipelines, true, true, CullingMode::BACK, WindingOrder::CCW)){
        return false;
    }

    sky.loadCalled = true;
    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

bool RenderSystem::drawSky(SkyComponent& sky, bool renderToTexture, bool invertCulling){
    if (sky.loaded){

        if (sky.needUpdateTexture || sky.texture.isFramebufferOutdated()){
            ShaderData& shaderData = sky.shader.get()->shaderData;
            if (TextureRender* textureRender = sky.texture.getRender(&emptyCubeWhite)){
                if (textureRender->isCreated()){
                    sky.render.addTexture(shaderData.getTextureIndex(TextureShaderType::SKYCUBE), ShaderStageType::FRAGMENT, textureRender);
                    sky.needUpdateTexture = false;
                }
            }
        }

        ObjectRender& render = sky.render;

        // a reflection pass flips handedness, so the sky cube needs reversed winding
        // (like meshes) or back-face culling removes its inward-facing faces
        PipelineType pipType = PIP_DEFAULT;
        if (renderToTexture){
            pipType = invertCulling ? PIP_RTT_INVERT : PIP_RTT;
        }
        if (!render.beginDraw(pipType)){
            sky.needReload = true;
            return false;
        }
        render.applyUniformBlock(sky.slotVSParams, sizeof(float) * 16, &sky.skyViewProjectionMatrix);
        render.applyUniformBlock(sky.slotFSParams, sizeof(float) * 4, &sky.color);
        render.draw(0, 36, 1);
    }

    return true;
}

void RenderSystem::destroySky(Entity entity, SkyComponent& sky){
    if (!sky.loaded)
        return;

    if (!sky.needReload){
        //Destroy shader
        if (sky.shader){
            sky.shader.reset();
            ShaderPool::remove(ShaderType::SKYBOX, 0, sky.customShaderId);
        }

        //Destroy texture
        sky.texture.destroy();

        //Release IBL environment maps (pooled maps are kept for reuse)
        releaseSkyEnvironment(sky);
        sky.needUpdateEnvironment = true;
    }

    //Destroy render
    sky.render.destroy();

    //Destroy buffer
    if (!sky.needReload){
        sky.buffer.clearAll();
    }
    sky.buffer.getRender()->destroyBuffer();

    //Shaders uniforms
    sky.slotVSParams = -1;
    sky.slotFSParams = -1;

    SystemRender::addQueueCommand(&changeDestroy, new check_load_t{scene, entity});
}

void RenderSystem::destroyLight(LightComponent& light){
    // shadow maps live in the RenderSystem-owned atlases, so a light owns no GPU
    // resources of its own to release here
    light.shadowMapIndex = -1;
}

void RenderSystem::destroyCamera(CameraComponent& camera, bool entityDestroyed){
    if (camera.framebuffer){
        if (entityDestroyed){
            delete camera.framebuffer; // destroy framebuffer in destructor
        }else{
            camera.framebuffer->destroy();
        }
    }
}

// Rect::fitOnRect moves the origin without shrinking the opposite edge, which widens a
// scissor that starts outside the bounds
static Rect intersectScissor(const Rect& rect, const Rect& bounds){
    float boundsRight = bounds.getX() + bounds.getWidth();
    float boundsTop = bounds.getY() + bounds.getHeight();

    float left = std::min(std::max(rect.getX(), bounds.getX()), boundsRight);
    float bottom = std::min(std::max(rect.getY(), bounds.getY()), boundsTop);
    float right = std::max(std::min(rect.getX() + rect.getWidth(), boundsRight), left);
    float top = std::max(std::min(rect.getY() + rect.getHeight(), boundsTop), bottom);

    return Rect(left, bottom, right - left, top - bottom);
}

Rect RenderSystem::getScissorRect(UILayoutComponent& layout, ImageComponent& img, Transform& transform, const Rect& passViewport){
    // the scissor is in the pass target pixels, so it follows the viewport the color pass
    // applied: offscreen targets start at (0,0), a swapchain keeps the letterbox offset
    float viewX = passViewport.getX();
    float viewY = passViewport.getY();
    float viewWidth = passViewport.getWidth();
    float viewHeight = passViewport.getHeight();

    if (viewWidth <= 0.0f || viewHeight <= 0.0f)
        return Rect(0, 0, 0, 0);

    float localLeft = (float) img.patchMarginLeft;
    float localTop = (float) img.patchMarginTop;
    float localRight = (float) layout.width - (float) img.patchMarginRight;
    float localBottom = (float) layout.height - (float) img.patchMarginBottom;

    if (localRight < localLeft)
        localRight = localLeft;
    if (localBottom < localTop)
        localBottom = localTop;

    Vector4 corners[4] = {
        transform.modelViewProjectionMatrix * Vector4(localLeft, localTop, 0.0f, 1.0f),
        transform.modelViewProjectionMatrix * Vector4(localRight, localTop, 0.0f, 1.0f),
        transform.modelViewProjectionMatrix * Vector4(localRight, localBottom, 0.0f, 1.0f),
        transform.modelViewProjectionMatrix * Vector4(localLeft, localBottom, 0.0f, 1.0f)
    };

    // a corner behind the eye has no screen position and a negative w mirrors the rect,
    // so the quad is clipped against w > 0 first
    const float EPSILON = 1e-5f;
    Vector4 visible[8];
    int visibleCount = 0;

    for (int i = 0; i < 4; i++){
        const Vector4& current = corners[i];
        const Vector4& next = corners[(i + 1) % 4];
        bool currentInFront = current.w > EPSILON;
        bool nextInFront = next.w > EPSILON;

        if (currentInFront)
            visible[visibleCount++] = current;

        if (currentInFront != nextInFront)
            visible[visibleCount++] = current + (next - current) * ((EPSILON - current.w) / (next.w - current.w));
    }

    if (visibleCount == 0)
        return Rect(0, 0, 0, 0);

    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;

    for (int i = 0; i < visibleCount; i++){
        float screenX = viewX + (((visible[i].x / visible[i].w) + 1.0f) * 0.5f * viewWidth);
        float screenY = viewY + (((visible[i].y / visible[i].w) + 1.0f) * 0.5f * viewHeight);

        if (i == 0){
            minX = maxX = screenX;
            minY = maxY = screenY;
        }else{
            minX = std::min(minX, screenX);
            minY = std::min(minY, screenY);
            maxX = std::max(maxX, screenX);
            maxY = std::max(maxY, screenY);
        }
    }

    return intersectScissor(Rect(minX, minY, maxX - minX, maxY - minY), passViewport);
}

void RenderSystem::updateTransform(Transform& transform){
    Matrix4 translateMatrix = Matrix4::translateMatrix(transform.position);
    Matrix4 rotationMatrix = transform.rotation.getRotationMatrix();
    Matrix4 scaleMatrix = Matrix4::scaleMatrix(transform.scale);

    transform.localMatrix = translateMatrix * rotationMatrix * scaleMatrix;

    Transform* transformParent = NULL;
    if (transform.parent != NULL_ENTITY){
        transformParent = scene->findComponent<Transform>(transform.parent);
    }

    if (transformParent){
        transform.modelMatrix = transformParent->modelMatrix * transform.localMatrix;

        transform.worldPosition = transformParent->modelMatrix * transform.position;
        transform.worldScale = transformParent->worldScale * transform.scale;
        transform.worldRotation = transformParent->worldRotation * transform.rotation;
    }else{
        transform.modelMatrix = transform.localMatrix;

        transform.worldPosition = transform.position;
        transform.worldScale = transform.scale;
        transform.worldRotation = transform.rotation;
    }

    if (hasLights){
        transform.normalMatrix = transform.modelMatrix.inverse().transpose();
    }
}

void RenderSystem::updateCamera(CameraComponent& camera, Transform& transform){
    //Update ProjectionMatrix
    if (camera.type == CameraType::CAMERA_UI){
        camera.projectionMatrix = Matrix4::orthoMatrix(camera.leftClip, camera.rightClip, camera.topClip, camera.bottomClip, camera.nearClip, camera.farClip);
    }else if (camera.type == CameraType::CAMERA_ORTHO) {
        camera.projectionMatrix = Matrix4::orthoMatrix(camera.leftClip, camera.rightClip, camera.bottomClip, camera.topClip, camera.nearClip, camera.farClip);
    }else if (camera.type == CameraType::CAMERA_PERSPECTIVE){
        camera.projectionMatrix = Matrix4::perspectiveMatrix(camera.yfov, camera.aspect, camera.nearClip, camera.farClip);
    }

    if (camera.useTarget){
        camera.direction = (transform.position - camera.target).normalize();
        camera.right = (camera.up.crossProduct(camera.direction)).normalize();
        //camera.up = camera.direction.crossProduct(camera.right); // no need to align, keep "up" always same

        if (transform.parent != NULL_ENTITY){
            camera.worldTarget = transform.modelMatrix * (camera.target - transform.position);
            camera.worldUp = ((transform.modelMatrix * camera.up) - (transform.modelMatrix * Vector3(0,0,0))).normalize();
        }else{
            camera.worldTarget = camera.target;
            camera.worldUp = camera.up;
        }

        //Update ViewMatrix
        camera.viewMatrix = Matrix4::lookAtMatrix(transform.worldPosition, camera.worldTarget, camera.worldUp);
    }else{
        camera.direction = transform.rotation * Vector3(0, 0, 1);
        camera.right = transform.rotation * Vector3(1, 0, 0);

        camera.worldTarget = transform.worldPosition - (transform.worldRotation * Vector3(0, 0, 1));

        //Update ViewMatrix
        camera.viewMatrix = transform.worldRotation.inverse().getRotationMatrix();
        camera.viewMatrix.translateInPlace(-transform.worldPosition.x, -transform.worldPosition.y, -transform.worldPosition.z);
    }

    // planar reflection drives the view matrix directly (mainView * reflect);
    // projection above still comes from the copied perspective params
    if (camera.hasCustomViewMatrix){
        camera.viewMatrix = camera.customViewMatrix;
    }

    camera.worldRight = Vector3(camera.viewMatrix[0][0], camera.viewMatrix[1][0], camera.viewMatrix[2][0]);
    camera.worldUp = Vector3(camera.viewMatrix[0][1], camera.viewMatrix[1][1], camera.viewMatrix[2][1]);
    camera.worldDirection = Vector3(camera.viewMatrix[0][2], camera.viewMatrix[1][2], camera.viewMatrix[2][2]);

    //Update ViewProjectionMatrix
    camera.viewProjectionMatrix = camera.projectionMatrix * camera.viewMatrix;

    updateCameraFrustumPlanes(camera.viewProjectionMatrix, camera.frustumPlanes);
}

// Creates the internal render-to-texture camera a mirror drives: a hidden system
// entity (not serialized, not shown in the editor) whose framebuffer feeds the
// mirror mesh base texture. Returns the camera entity.
Entity RenderSystem::createMirrorCamera(Entity mirrorEntity){
    if (!scene->findComponent<MirrorComponent>(mirrorEntity))
        return NULL_ENTITY;

    const Entity existing = getMirrorCamera(mirrorEntity);
    if (existing != NULL_ENTITY && scene->isEntityCreated(existing))
        return existing;

    Entity camEntity = scene->createSystemEntity();
    scene->addComponent<Transform>(camEntity, {});
    scene->addComponent<CameraComponent>(camEntity, {});

    CameraComponent& cam = scene->getComponent<CameraComponent>(camEntity);
    cam.type = CameraType::CAMERA_PERSPECTIVE;
    cam.renderToTexture = true;
    cam.autoResize = false; // driven entirely by updateMirrors
    int w = (Engine::getCanvasWidth() > 0) ? Engine::getCanvasWidth() : 1024;
    int h = (Engine::getCanvasHeight() > 0) ? Engine::getCanvasHeight() : 1024;
    cam.framebuffer->setWidth(w);
    cam.framebuffer->setHeight(h);
    // keep authored fields in sync so RenderSystem::update doesn't resize it back
    cam.framebufferWidth = w;
    cam.framebufferHeight = h;

    mirrorCameras[mirrorEntity] = camEntity;
    return camEntity;
}

Entity RenderSystem::getMirrorCamera(Entity mirrorEntity) const{
    auto it = mirrorCameras.find(mirrorEntity);
    return it != mirrorCameras.end() ? it->second : NULL_ENTITY;
}

void RenderSystem::destroyMirrorCamera(Entity entity){
    // Ownership leaves the map before entity destruction dispatches removal callbacks.
    auto camera = mirrorCameras.extract(entity);
    if (camera.empty())
        return;

    const Entity cameraEntity = camera.mapped();
    if (cameraEntity == NULL_ENTITY || !scene->isEntityCreated(cameraEntity))
        return;

    // Detach the borrowed framebuffer before destroying the camera that owns it.
    if (CameraComponent* cameraComponent = scene->findComponent<CameraComponent>(cameraEntity)){
        if (MeshComponent* mesh = scene->findComponent<MeshComponent>(entity)){
            if (mesh->numSubmeshes > 0){
                Texture& baseTexture = mesh->submeshes[0].material.baseColorTexture;
                if (baseTexture.getFramebuffer() == cameraComponent->framebuffer){
                    baseTexture = Texture();
                }
            }
        }
    }
    scene->destroyEntity(cameraEntity);
}

// Drives each mirror's reflection camera: its view = mainView * reflect(plane),
// so the reflected scene is rendered into that camera's framebuffer. Runs after
// the main camera is updated and before the draw pass. The handedness flip from
// the reflection is compensated by invertCulling on the reflection camera.
void RenderSystem::updateMirrors(Entity mainCameraEntity){
    auto mirrors = scene->getComponentArray<MirrorComponent>();
    if (mirrors->size() == 0)
        return;

    CameraComponent* mainCameraPtr = scene->findComponent<CameraComponent>(mainCameraEntity);
    if (!mainCameraPtr)
        return;

    // copy main camera state before any camera creation: creating a reflection
    // camera below can reallocate the camera/transform arrays and dangle references
    const Matrix4 mainView = mainCameraPtr->viewMatrix;
    // main camera eye in world space (reflected per-mirror below); derived from the
    // view matrix so it doesn't depend on the (possibly dangling) camera component
    const Vector3 mainEye = mainView.inverse() * Vector3(0.0f, 0.0f, 0.0f);
    const CameraType mainType = mainCameraPtr->type;
    const float mYfov = mainCameraPtr->yfov, mAspect = mainCameraPtr->aspect;
    const float mNear = mainCameraPtr->nearClip, mFar = mainCameraPtr->farClip;
    const float mLeft = mainCameraPtr->leftClip, mRight = mainCameraPtr->rightClip;
    const float mBottom = mainCameraPtr->bottomClip, mTop = mainCameraPtr->topClip;
    // mainCameraPtr must not be used past here (may dangle after camera creation)

    // Creating a reflection camera appends to the camera/transform arrays but never
    // the mirror array, so indexed iteration over mirrors stays valid in one pass;
    // transforms/cameras are (re)fetched after creation since their arrays may move.
    for (size_t i = 0; i < mirrors->size(); i++){
        Entity entity = mirrors->getEntity(i);

        Entity camEntity = getMirrorCamera(entity);
        if (camEntity == NULL_ENTITY || !scene->isEntityCreated(camEntity)){
            camEntity = createMirrorCamera(entity);
            // the loop counting render-to-texture cameras has run, but draw() still
            // renders this one in the same frame
            hasMultipleCameras = true;
        }

        CameraComponent* refCam = scene->findComponent<CameraComponent>(camEntity);
        Transform* refCamTransform = scene->findComponent<Transform>(camEntity);
        Transform* mirrorTransform = scene->findComponent<Transform>(entity);
        if (!refCam || !refCamTransform || !mirrorTransform)
            continue;

        // mirror plane in world space (entity position, world-rotated normal)
        Vector3 worldNormal = (mirrorTransform->worldRotation * mirrors->getComponentFromIndex(i).normal).normalize();
        Plane plane(worldNormal, mirrorTransform->worldPosition);

        // reflection view keeps the handedness flip (true mirror); winding is
        // restored by invertCulling so reflected geometry shows front faces
        Matrix4 reflectMat = Matrix4::reflectMatrix(plane);
        refCam->customViewMatrix = mainView * reflectMat;
        refCam->hasCustomViewMatrix = true;

        // Give the reflection camera the reflected eye position. The custom view
        // matrix overrides the view, but distance-based effects still read the
        // transform's worldPosition (terrain CDLOD detail + morph origin, transparent
        // sorting, per-camera lighting eye) — without this they'd key off the origin
        // and the reflected terrain would morph at the wrong LOD (visible cracks).
        refCamTransform->worldPosition = reflectMat * mainEye;
        refCam->invertCulling = true;
        refCam->renderToTexture = true;

        // match the main camera projection so the reflection lines up 1:1
        refCam->type = mainType;
        refCam->yfov = mYfov;
        refCam->aspect = mAspect;
        refCam->nearClip = mNear;
        refCam->farClip = mFar;
        refCam->leftClip = mLeft;
        refCam->rightClip = mRight;
        refCam->bottomClip = mBottom;
        refCam->topClip = mTop;

        // recompute the reflection camera matrices with the override applied
        updateCamera(*refCam, *refCamTransform);

        // oblique near-plane clipping: move the reflection camera's near plane onto
        // the mirror plane so geometry behind the mirror doesn't leak into the
        // reflection. The clip plane (mirror plane, normal toward the front/reflecting
        // side) is taken to the reflection camera's view space via inverse-transpose.
        // Applied ONLY to viewProjectionMatrix (used by meshes), NOT projectionMatrix:
        // the skybox renders from projectionMatrix and the oblique depth would clip it.
        // Only the depth row changes, so projective sampling (clip.xy/w) is unaffected.
        Vector4 worldClipPlane(worldNormal.x, worldNormal.y, worldNormal.z, plane.d);
        Vector4 viewClipPlane = refCam->viewMatrix.inverse().transpose() * worldClipPlane;
        Matrix4 obliqueProjection = refCam->projectionMatrix.obliqueNearClip(viewClipPlane);
        refCam->viewProjectionMatrix = obliqueProjection * refCam->viewMatrix;
        updateCameraFrustumPlanes(refCam->viewProjectionMatrix, refCam->frustumPlanes);

        MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
        if (mesh && mesh->numSubmeshes > 0){
            // (re)bind the reflection framebuffer to the mesh base texture every frame.
            // Doing this per-frame (not just at camera creation) re-heals the mirror
            // after a play-stop snapshot restore, which clears the framebuffer binding
            // (framebuffer textures aren't serialized). One-shot: no rebind once matched.
            Texture& baseTex = mesh->submeshes[0].material.baseColorTexture;
            if (baseTex.getFramebuffer() != refCam->framebuffer){
                baseTex.setFramebuffer(refCam->framebuffer);
                mesh->needReload = true;
            }

            // hand the reflection VP to the mirror mesh for projective sampling
            // (USE_MIRROR shader); logical matrix, the shader handles the texture origin
            mesh->mirrorViewProjection = refCam->viewProjectionMatrix;
        }
    }
}

void RenderSystem::updateSkyViewProjection(SkyComponent& sky, CameraComponent& camera){
    Matrix4 skyViewMatrix = camera.viewMatrix;

    skyViewMatrix.set(3,0,0);
    skyViewMatrix.set(3,1,0);
    skyViewMatrix.set(3,2,0);
    skyViewMatrix.set(3,3,1);
    skyViewMatrix.set(2,3,0);
    skyViewMatrix.set(1,3,0);
    skyViewMatrix.set(0,3,0);

    Matrix4 rotationMatrix = Quaternion(0, sky.rotation, 0).getRotationMatrix();

    sky.skyViewProjectionMatrix = camera.projectionMatrix * skyViewMatrix * rotationMatrix;

    if (isRenderingFlipped(camera)){
        static const Matrix4 flipYMatrix = Matrix4::scaleMatrix(Vector3(1, -1, 1));
        sky.skyViewProjectionMatrix = flipYMatrix * sky.skyViewProjectionMatrix;
    }

    sky.needUpdateSky = false;
}

void RenderSystem::updatePoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform){
    float worldScale = std::max(transform.worldScale.x, std::max(transform.worldScale.y, transform.worldScale.z));

    points.numVisible = 0;
    size_t pointsSize = (points.points.size() < points.maxPoints)? points.points.size() : points.maxPoints;
    if (points.renderPoints.size() < pointsSize){
        points.renderPoints.resize(pointsSize);
    }

    for (int i = 0; i < pointsSize; i++){
        if (points.points[i].visible){
            PointRenderData& renderPoint = points.renderPoints[points.numVisible];
            renderPoint.position = points.points[i].position;
            renderPoint.color = points.points[i].color;
            renderPoint.size = points.points[i].size * worldScale;
            renderPoint.rotation = points.points[i].rotation;
            renderPoint.textureRect = points.points[i].textureRect;
            points.numVisible++;
        }
    }
    points.renderPoints.resize(points.numVisible);

    if (points.loaded)
        points.needUpdateBuffer = true;
}

void RenderSystem::sortPoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform){
    Vector3 camDir = (camTransform.worldPosition - camera.worldTarget).normalize();

    auto comparePoints = [&transform, &camDir](const PointRenderData& a, const PointRenderData& b) -> bool {
        return (transform.modelMatrix * a.position).dotProduct(camDir) < (transform.modelMatrix * b.position).dotProduct(camDir);
    };
    std::sort(points.renderPoints.begin(), points.renderPoints.end(), comparePoints);

    if (points.loaded)
        points.needUpdateBuffer = true;
}

void RenderSystem::updateTerrain(TerrainComponent& terrain, Transform& transform, CameraComponent& camera, Transform& cameraTransform, int viewIndex){
    if (terrain.heightMapLoaded){
        for (int i = 0; i < terrain.numNodes; i++){
            terrain.nodes[i].visible = false;
        }

        for (int s = 0; s < 2; s++){
            terrain.views[viewIndex].nodesbuffer[s].clear();
        }

        for (int i = 0; i < (terrain.rootGridSize*terrain.rootGridSize); i++){
            terrainNodeLODSelect(terrain, transform, camera, cameraTransform, terrain.nodes[terrain.grid[i]], terrain.levels-1, viewIndex);
        }

        terrain.views[viewIndex].needUpdateNodesBuffer = true;

        terrain.views[viewIndex].nodesEyePos = Vector3(cameraTransform.worldPosition.x, cameraTransform.worldPosition.y, cameraTransform.worldPosition.z);
    }
}

AABB RenderSystem::getTerrainNodeAABB(Transform& transform, TerrainNode& terrainNode){
    float halfSize = terrainNode.size / 2.0f;
    Vector3 localMin(terrainNode.position.x - halfSize, terrainNode.minHeight, terrainNode.position.y - halfSize);
    Vector3 localMax(terrainNode.position.x + halfSize, terrainNode.maxHeight, terrainNode.position.y + halfSize);

    AABB box(localMin, localMax);
    box.transform(transform.modelMatrix);
    return box;
};

bool RenderSystem::isTerrainNodeInSphere(Vector3 position, float radius, const AABB& box) {
    float r2 = radius*radius;

    Vector3 c1 = box.getMinimum();
    Vector3 c2 = box.getMaximum();
    Vector3 distV;

    if (position.x < c1.x) distV.x = (position.x - c1.x);
    else if (position.x > c2.x) distV.x = (position.x - c2.x);
    if (position.y < c1.y) distV.y = (position.y - c1.y);
    else if (position.y > c2.y) distV.y = (position.y - c2.y);
    if (position.z < c1.z) distV.z = (position.z - c1.z);
    else if (position.z > c2.z) distV.z = (position.z - c2.z);

    float dist2 = distV.dotProduct(distV);

    return dist2 <= r2;
}

// appends one selected CDLOD node's per-instance data (position, size, range, resolution)
static void appendTerrainNode(InterleavedBuffer& buffer, const TerrainNode& node){
    buffer.addVector2(AttributeType::TERRAINNODEPOSITION, node.position);
    buffer.addFloat(AttributeType::TERRAINNODESIZE, node.size);
    buffer.addFloat(AttributeType::TERRAINNODERANGE, node.currentRange);
    buffer.addFloat(AttributeType::TERRAINNODERESOLUTION, node.resolution);
}

bool RenderSystem::terrainNodeLODSelect(TerrainComponent& terrain, Transform& transform, CameraComponent& camera, Transform& cameraTransform, TerrainNode& terrainNode, int lodLevel, int viewIndex){
    terrainNode.currentRange = terrain.ranges[lodLevel];

    AABB box = getTerrainNodeAABB(transform, terrainNode);

    if (!isTerrainNodeInSphere(cameraTransform.worldPosition, terrain.ranges[lodLevel], box)) {
        // no node or child nodes were selected
        return false;
    }

    if (!isInsideCamera(camera, box)) {
        // return true to parent node does not select itself over area
        return true;
    }

    if( lodLevel == 0 ) {
        //Full resolution
        terrainNode.resolution = terrain.resolution;
        terrainNode.visible = true;
        appendTerrainNode(terrain.views[viewIndex].nodesbuffer[0], terrainNode);

        return true;
    } else {

        if( !isTerrainNodeInSphere(cameraTransform.worldPosition, terrain.ranges[lodLevel-1], box) ) {
            //Full resolution
            terrainNode.resolution = terrain.resolution;
            terrainNode.visible = true;
            appendTerrainNode(terrain.views[viewIndex].nodesbuffer[0], terrainNode);
        } else {
            for (int i = 0; i < 4; i++) {
                TerrainNode& child = terrain.nodes[terrainNode.childs[i]];
                if (!terrainNodeLODSelect(terrain, transform, camera, cameraTransform, child, lodLevel-1, viewIndex)){
                    //Half resolution
                    child.resolution = terrain.resolution / 2;
                    child.currentRange = terrainNode.currentRange;
                    child.visible = true;
                    appendTerrainNode(terrain.views[viewIndex].nodesbuffer[1], child);
                }
            }
        }

        return true;
    }

}

// Fills tilemapDrawRanges with the index runs of a tilemap submesh that survive
// frustum culling, merging chunks that are adjacent in the index buffer so a visible
// region costs as few draw calls as it can. Returns false when nothing of the submesh
// is visible; an empty range list on success means "draw the submesh whole", the
// fallback for a submesh the chunk data does not cover.
bool RenderSystem::selectTilemapChunks(TilemapComponent& tilemap, unsigned int submeshIndex, const float cameraFar, const Plane frustumPlanes[6]){
    tilemapDrawRanges.clear();

    if ((submeshIndex + 1) >= tilemap.chunkStart.size())
        return true;

    const unsigned int begin = tilemap.chunkStart[submeshIndex];
    const unsigned int end = tilemap.chunkStart[submeshIndex + 1];

    for (unsigned int c = begin; c < end; c++){
        const TilemapChunk& chunk = tilemap.chunks[c];

        if (!isInsideCamera(cameraFar, frustumPlanes, chunk.worldAABB))
            continue;

        if (!tilemapDrawRanges.empty() && (tilemapDrawRanges.back().offset + tilemapDrawRanges.back().count) == chunk.offset){
            tilemapDrawRanges.back().count += chunk.count;
        }else{
            tilemapDrawRanges.push_back({chunk.offset, chunk.count});
        }
    }

    return !tilemapDrawRanges.empty();
}

void RenderSystem::updateCameraSize(Entity entity){
    Signature signature = scene->getSignature(entity);
    if (signature.test(scene->getComponentId<CameraComponent>())){
        CameraComponent& camera = scene->getComponent<CameraComponent>(entity);
        
        Rect rect;
        if (camera.renderToTexture) {
            rect = Rect(0, 0, camera.framebuffer->getWidth(), camera.framebuffer->getHeight());
        }else{
            rect = Rect(0, 0, Engine::getCanvasWidth(), Engine::getCanvasHeight());
        }

        if (camera.autoResize){
            float newLeft = rect.getX();
            float newBottom = rect.getY();
            float newRight = rect.getWidth();
            float newTop = rect.getHeight();
            if (newRight <= 0.0f || newTop <= 0.0f) return;
            float newAspect = newRight / newTop;

            if ((camera.leftClip != newLeft) || (camera.bottomClip != newBottom) || (camera.rightClip != newRight) || (camera.topClip != newTop) || (camera.aspect != newAspect)){
                camera.leftClip = newLeft;
                camera.bottomClip = newBottom;
                camera.rightClip = newRight;
                camera.topClip = newTop;
                camera.aspect = newAspect;

                camera.needUpdate = true;
            }
        }
    }
}

bool RenderSystem::isInsideCamera(const float cameraFar, const Plane frustumPlanes[6], const AABB& box){
    if (box.isNull() || box.isInfinite())
        return false;

    Vector3 centre = box.getCenter();
    Vector3 halfSize = box.getHalfSize();

    for (int plane = 0; plane < 6; ++plane){
        if (plane == FRUSTUM_PLANE_FAR && cameraFar == 0)
            continue;

        Plane::Side side = frustumPlanes[plane].getSide(centre, halfSize);
        if (side == Plane::Side::NEGATIVE_SIDE){
            return false;
        }
    }

    return true;
}

bool RenderSystem::isInsideCamera(CameraComponent& camera, const AABB& box){
    return isInsideCamera(camera.farClip, camera.frustumPlanes, box);
}

bool RenderSystem::isInsideCamera(CameraComponent& camera, const Vector3& point){
    for (int plane = 0; plane < 6; ++plane){
        if (plane == FRUSTUM_PLANE_FAR && camera.farClip == 0)
            continue;

        if (camera.frustumPlanes[plane].getSide(point) == Plane::Side::NEGATIVE_SIDE){
            return false;
        }
    }

    return true;
}

bool RenderSystem::isInsideCamera(CameraComponent& camera, const Vector3& center, const float& radius){
    for (int plane = 0; plane < 6; ++plane){
        if (plane == FRUSTUM_PLANE_FAR && camera.farClip == 0)
            continue;

        if (camera.frustumPlanes[plane].getDistance(center) < -radius){
            return false;
        }
    }

    return true;
}

void RenderSystem::needReloadPoints() {
    auto pointsArray = scene->getComponentArray<PointsComponent>();
    for (int i = 0; i < pointsArray->size(); i++) {
        PointsComponent& points = pointsArray->getComponentFromIndex(i);
        points.needReload = true;
    }
}

void RenderSystem::needReloadLines() {
    auto linesArray = scene->getComponentArray<LinesComponent>();
    for (int i = 0; i < linesArray->size(); i++) {
        LinesComponent& lines = linesArray->getComponentFromIndex(i);
        lines.needReload = true;
    }
}

void RenderSystem::needReloadMeshes() {
    auto meshes = scene->getComponentArray<MeshComponent>();
    for (int i = 0; i < meshes->size(); i++) {
        MeshComponent& mesh = meshes->getComponentFromIndex(i);
        mesh.needReload = true;
    }
}

void RenderSystem::needReloadUIs() {
    auto uis = scene->getComponentArray<UIComponent>();
    for (int i = 0; i < uis->size(); i++) {
        UIComponent& ui = uis->getComponentFromIndex(i);
        ui.needReload = true;
    }
}

void RenderSystem::needReloadSky() {
    auto skyArray = scene->getComponentArray<SkyComponent>();
    for (int i = 0; i < skyArray->size(); i++) {
        SkyComponent& sky = skyArray->getComponentFromIndex(i);
        sky.needReload = true;
    }
}

bool RenderSystem::isAllLoaded() const{
    // Check MeshComponents
    auto meshes = scene->getComponentArray<MeshComponent>();
    for (int i = 0; i < meshes->size(); i++) {
        const MeshComponent& mesh = meshes->getComponentFromIndex(i);
        if (!mesh.loaded)
            return false;
    }

    // Check UIComponents
    auto uis = scene->getComponentArray<UIComponent>();
    for (int i = 0; i < uis->size(); i++) {
        const UIComponent& ui = uis->getComponentFromIndex(i);
        // An empty UI never loads: loadUI() retries each frame and nothing sets needReload.
        const size_t uiBufferSize = std::max(ui.buffer.getSize(),
                                             (size_t)ui.minBufferCount * ui.buffer.getStride());
        if (!ui.loaded && uiBufferSize > 0)
            return false;
    }

    // Check PointsComponents
    auto pointsArray = scene->getComponentArray<PointsComponent>();
    for (int i = 0; i < pointsArray->size(); i++) {
        const PointsComponent& points = pointsArray->getComponentFromIndex(i);
        if (!points.loaded)
            return false;
    }

    // Check LinesComponents
    auto linesArray = scene->getComponentArray<LinesComponent>();
    for (int i = 0; i < linesArray->size(); i++) {
        const LinesComponent& lines = linesArray->getComponentFromIndex(i);
        // An empty Lines never loads: addLine() only sets needReload when maxLines grows.
        if (!lines.loaded && !lines.lines.empty())
            return false;
    }

    // Check SkyComponents
    auto skyArray = scene->getComponentArray<SkyComponent>();
    for (int i = 0; i < skyArray->size(); i++) {
        const SkyComponent& sky = skyArray->getComponentFromIndex(i);
        if (!sky.loaded)
            return false;
    }

    return true;
}

void RenderSystem::updateCameraFrustumPlanes(const Matrix4 viewProjectionMatrix, Plane* frustumPlanes){

    frustumPlanes[FRUSTUM_PLANE_LEFT].normal.x = viewProjectionMatrix[0][3] + viewProjectionMatrix[0][0];
    frustumPlanes[FRUSTUM_PLANE_LEFT].normal.y = viewProjectionMatrix[1][3] + viewProjectionMatrix[1][0];
    frustumPlanes[FRUSTUM_PLANE_LEFT].normal.z = viewProjectionMatrix[2][3] + viewProjectionMatrix[2][0];
    frustumPlanes[FRUSTUM_PLANE_LEFT].d = viewProjectionMatrix[3][3] + viewProjectionMatrix[3][0];

    frustumPlanes[FRUSTUM_PLANE_RIGHT].normal.x = viewProjectionMatrix[0][3] - viewProjectionMatrix[0][0];
    frustumPlanes[FRUSTUM_PLANE_RIGHT].normal.y = viewProjectionMatrix[1][3] - viewProjectionMatrix[1][0];
    frustumPlanes[FRUSTUM_PLANE_RIGHT].normal.z = viewProjectionMatrix[2][3] - viewProjectionMatrix[2][0];
    frustumPlanes[FRUSTUM_PLANE_RIGHT].d = viewProjectionMatrix[3][3] - viewProjectionMatrix[3][0];

    frustumPlanes[FRUSTUM_PLANE_TOP].normal.x = viewProjectionMatrix[0][3] - viewProjectionMatrix[0][1];
    frustumPlanes[FRUSTUM_PLANE_TOP].normal.y = viewProjectionMatrix[1][3] - viewProjectionMatrix[1][1];
    frustumPlanes[FRUSTUM_PLANE_TOP].normal.z = viewProjectionMatrix[2][3] - viewProjectionMatrix[2][1];
    frustumPlanes[FRUSTUM_PLANE_TOP].d = viewProjectionMatrix[3][3] - viewProjectionMatrix[3][1];

    frustumPlanes[FRUSTUM_PLANE_BOTTOM].normal.x = viewProjectionMatrix[0][3] + viewProjectionMatrix[0][1];
    frustumPlanes[FRUSTUM_PLANE_BOTTOM].normal.y = viewProjectionMatrix[1][3] + viewProjectionMatrix[1][1];
    frustumPlanes[FRUSTUM_PLANE_BOTTOM].normal.z = viewProjectionMatrix[2][3] + viewProjectionMatrix[2][1];
    frustumPlanes[FRUSTUM_PLANE_BOTTOM].d = viewProjectionMatrix[3][3] + viewProjectionMatrix[3][1];

    frustumPlanes[FRUSTUM_PLANE_NEAR].normal.x = viewProjectionMatrix[0][3] + viewProjectionMatrix[0][2];
    frustumPlanes[FRUSTUM_PLANE_NEAR].normal.y = viewProjectionMatrix[1][3] + viewProjectionMatrix[1][2];
    frustumPlanes[FRUSTUM_PLANE_NEAR].normal.z = viewProjectionMatrix[2][3] + viewProjectionMatrix[2][2];
    frustumPlanes[FRUSTUM_PLANE_NEAR].d = viewProjectionMatrix[3][3] + viewProjectionMatrix[3][2];

    frustumPlanes[FRUSTUM_PLANE_FAR].normal.x = viewProjectionMatrix[0][3] - viewProjectionMatrix[0][2];
    frustumPlanes[FRUSTUM_PLANE_FAR].normal.y = viewProjectionMatrix[1][3] - viewProjectionMatrix[1][2];
    frustumPlanes[FRUSTUM_PLANE_FAR].normal.z = viewProjectionMatrix[2][3] - viewProjectionMatrix[2][2];
    frustumPlanes[FRUSTUM_PLANE_FAR].d = viewProjectionMatrix[3][3] - viewProjectionMatrix[3][2];

    for (int i=0; i<6; i++){
        float length = frustumPlanes[i].normal.normalizeL();
        frustumPlanes[i].d /= length;
    }
}

void RenderSystem::updateInstancedMesh(InstancedMeshComponent& instmesh, MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform){
    instmesh.renderInstances.clear();
    instmesh.renderInstances.reserve(instmesh.instances.size());

    Quaternion bRotation;
    if (instmesh.instancedBillboard){
        Vector3 camPos = camTransform.worldPosition;
        if (instmesh.instancedCylindricalBillboard){
            camPos.y = transform.worldPosition.y;
        }

        Matrix4 m1 = camera.viewMatrix.inverse();
        bRotation.fromRotationMatrix(m1);
        bRotation = transform.worldRotation.inverse() * bRotation;
    }

    // Not AABB::ZERO: that is a finite box at the origin, so merging into it stretches every
    // instanced mesh back to its own origin and defeats culling.
    mesh.aabb.setNull();
    instmesh.numVisible = 0;
    size_t instancesSize = (instmesh.instances.size() < instmesh.maxInstances)? instmesh.instances.size() : instmesh.maxInstances;
    for (int i = 0; i < instancesSize; i++){
        if (instmesh.instances[i].visible){
            Matrix4 translateMatrix = Matrix4::translateMatrix(instmesh.instances[i].position);
            Matrix4 rotationMatrix;
            Matrix4 scaleMatrix = Matrix4::scaleMatrix(instmesh.instances[i].scale);

            if (instmesh.instancedBillboard){
                rotationMatrix = bRotation.getRotationMatrix();
            }else{
                rotationMatrix = instmesh.instances[i].rotation.getRotationMatrix();
            }

            Matrix4 instanceMatrix = translateMatrix * rotationMatrix * scaleMatrix;

            instmesh.renderInstances.push_back({});
            instmesh.renderInstances[instmesh.numVisible].instanceMatrix = instanceMatrix;
            instmesh.renderInstances[instmesh.numVisible].color = instmesh.instances[i].color;
            instmesh.renderInstances[instmesh.numVisible].textureRect = instmesh.instances[i].textureRect;
            instmesh.numVisible++;

            mesh.aabb.merge(instanceMatrix * mesh.verticesAABB);
        }
    }

    mesh.needUpdateAABB = true;

    if (mesh.loaded)
        instmesh.needUpdateBuffer = true;
}

void RenderSystem::sortInstancedMesh(InstancedMeshComponent& instmesh, MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform){
    Vector3 camDir = (camTransform.worldPosition - camera.worldTarget).normalize();

    auto comparePoints = [&transform, &camDir](const InstanceRenderData& a, const InstanceRenderData& b) -> bool {
        Vector3 positionA = Vector3(a.instanceMatrix[3][0], a.instanceMatrix[3][1], a.instanceMatrix[3][2]);
        Vector3 positionB = Vector3(b.instanceMatrix[3][0], b.instanceMatrix[3][1], b.instanceMatrix[3][2]);

        return (transform.modelMatrix * positionA).dotProduct(camDir) < (transform.modelMatrix * positionB).dotProduct(camDir);
    };
    std::sort(instmesh.renderInstances.begin(), instmesh.renderInstances.end(), comparePoints);

    if (mesh.loaded)
        instmesh.needUpdateBuffer = true;
}

void RenderSystem::configureLightShadowNearFar(LightComponent& light, const CameraComponent& camera){
    if (light.automaticShadowCamera){
        light.shadowCameraNearFar.x = camera.nearClip;
        if (light.range == 0.0){
            light.shadowCameraNearFar.y = camera.farClip;
        }else{
            light.shadowCameraNearFar.y = light.range;
        }
    }
}

float RenderSystem::lerp(float a, float b, float fraction) {
    return (a * (1.0f - fraction)) + (b * fraction);
}

Matrix4 RenderSystem::getDirLightProjection(const Matrix4& viewMatrix, const Matrix4& sceneCameraInv, float shadowMaxDistance, const Vector3& lightDirection, const Vector3& cameraPosition){
    // Calculate extended frustum bounds
    Matrix4 extendedSceneCameraInv = sceneCameraInv;

    // Extend the frustum backwards to include more geometry
    float extension = shadowMaxDistance * 0.5f;

    Matrix4 t = viewMatrix * extendedSceneCameraInv;
    std::vector<Vector4> frustumCorners = {
        t * Vector4(-1.f, 1.f, -1.f, 1.f),
        t * Vector4(1.f, 1.f, -1.f, 1.f),
        t * Vector4(1.f, -1.f, -1.f, 1.f),
        t * Vector4(-1.f, -1.f, -1.f, 1.f),
        t * Vector4(-1.f, 1.f, 1.f, 1.f),
        t * Vector4(1.f, 1.f, 1.f, 1.f),
        t * Vector4(1.f, -1.f, 1.f, 1.f),
        t * Vector4(-1.f, -1.f, 1.f, 1.f)
    };

    // Add additional points behind the camera
    Vector3 backwardOffset = -lightDirection * extension;
    Vector4 backPoint = viewMatrix * Vector4(cameraPosition + backwardOffset, 1.0f);
    frustumCorners.push_back(backPoint);

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (auto& p : frustumCorners){
        if (p.w != 0.0f) {
            p = p / p.w;
        }

        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
    }

    float paddingX = (maxX - minX) * 0.05f;
    float paddingY = (maxY - minY) * 0.05f;
    float paddingZ = (maxZ - minZ) * 0.1f;

    minX -= paddingX;
    maxX += paddingX;
    minY -= paddingY;
    maxY += paddingY;
    minZ -= paddingZ;
    maxZ += paddingZ;

    return Matrix4::orthoMatrix(minX, maxX, minY, maxY, -maxZ, -minZ);
}

void RenderSystem::updateLightFromScene(LightComponent& light, Transform& transform, CameraComponent& camera, Transform& cameraTransform){
    light.worldDirection = transform.worldRotation * light.direction;
    light.worldUp = (transform.worldRotation * light.getLocalSpotProjectionUp()).normalized();

    if (hasShadows && (light.intensity > 0)){
        
        Vector3 up = Vector3(0, 1, 0);
        if (light.worldDirection.crossProduct(up) == Vector3::ZERO){
            up = Vector3(0, 0, 1);
        }

        configureLightShadowNearFar(light, camera);

        //TODO: perspective aspect based on shadow map size
        if (light.type == LightType::DIRECTIONAL){

            float shadowSplitLogFactor = 0.7f;
            float shadowMaxDistance = light.shadowCameraNearFar.y;

            Matrix4 projectionMatrix[MAX_SHADOWCASCADES];
            Matrix4 viewMatrix;

            viewMatrix = Matrix4::lookAtMatrix(transform.worldPosition, light.worldDirection + transform.worldPosition, up);

            //TODO: light directional cascades is only considering main camera
            if (camera.type == CameraType::CAMERA_PERSPECTIVE) {

                float zNear = light.shadowCameraNearFar.x;
                float zFar = light.shadowCameraNearFar.y;
                float planeFarOffset = (zFar - zNear) * 0.05f;

                std::vector<float> splitFar;
                std::vector<float> splitNear;

                // Split perspective frustum to create cascades
                Matrix4 projection = camera.projectionMatrix;
                Matrix4 invProjection = projection.inverse();

                std::vector<Vector4> v1 = {
                    invProjection * Vector4(-1.f, 1.f, -1.f, 1.f),
                    invProjection * Vector4(1.f, 1.f, -1.f, 1.f),
                    invProjection * Vector4(1.f, -1.f, -1.f, 1.f),
                    invProjection * Vector4(-1.f, -1.f, -1.f, 1.f),
                    invProjection * Vector4(-1.f, 1.f, 1.f, 1.f),
                    invProjection * Vector4(1.f, 1.f, 1.f, 1.f),
                    invProjection * Vector4(1.f, -1.f, 1.f, 1.f),
                    invProjection * Vector4(-1.f, -1.f, 1.f, 1.f)
                };

                zFar = std::min(zFar, -(v1[4] / v1[4].w).z);
                zNear = -(v1[0] / v1[0].w).z;
                float fov = atanf(1.f / projection[1][1]) * 2.f;
                float ratio = projection[1][1] / projection[0][0];

                splitFar.resize(light.numShadowCascades);
                splitNear.resize(light.numShadowCascades);
                splitNear[0] = zNear;
                splitFar[light.numShadowCascades - 1] = zFar;

                float j = 1.f;
                for (auto i = 0u; i < light.numShadowCascades - 1; ++i, j += 1.f) {
                    splitFar[i] = lerp(
                        zNear + (j / (float)light.numShadowCascades) * (zFar - zNear),
                        zNear * powf(zFar / zNear, j / (float)light.numShadowCascades),
                        shadowSplitLogFactor
                    );
                    splitNear[i + 1] = splitFar[i];
                }

                // Get frustum box and create light ortho
                for (int ca = 0; ca < light.numShadowCascades; ca++) {
                    Matrix4 cascadeProjection = Matrix4::perspectiveMatrix(fov, ratio, splitNear[ca], splitFar[ca] + planeFarOffset);
                    Matrix4 sceneCameraInv = (cascadeProjection * camera.viewMatrix).inverse();

                    projectionMatrix[ca] = getDirLightProjection(viewMatrix, sceneCameraInv, shadowMaxDistance, light.worldDirection, cameraTransform.worldPosition);

                    light.cameras[ca].lightViewMatrix = viewMatrix;
                    light.cameras[ca].lightProjectionMatrix = projectionMatrix[ca];
                    light.cameras[ca].lightViewProjectionMatrix = projectionMatrix[ca] * viewMatrix;
                    light.cameras[ca].nearFar = Vector2(splitNear[ca], splitFar[ca]);
                    updateCameraFrustumPlanes(light.cameras[ca].lightViewProjectionMatrix, light.cameras[ca].frustumPlanes);
                }

            } else {
                // Orthographic camera handling remains the same
                if (light.numShadowCascades > 1){
                    light.numShadowCascades = 1;
                    Log::warn("Can not have multiple cascades shadows when using ortho scene camera. Reducing num shadow cascades to 1");
                }

                Matrix4 sceneCameraInv = camera.viewProjectionMatrix.inverse();

                projectionMatrix[0] = getDirLightProjection(viewMatrix, sceneCameraInv, shadowMaxDistance, light.worldDirection, cameraTransform.worldPosition);

                light.cameras[0].lightViewMatrix = viewMatrix;
                light.cameras[0].lightProjectionMatrix = projectionMatrix[0];
                light.cameras[0].lightViewProjectionMatrix = projectionMatrix[0] * viewMatrix;
                light.cameras[0].nearFar = Vector2(-shadowMaxDistance, shadowMaxDistance);
                updateCameraFrustumPlanes(light.cameras[0].lightViewProjectionMatrix, light.cameras[0].frustumPlanes);

            }

        }else if (light.type == LightType::SPOT){
            Matrix4 projectionMatrix;
            Matrix4 viewMatrix;

            Vector3 projectionUp = light.spotMaskReady ? light.worldUp : up;
            float projectionAspect = light.spotMaskReady ? light.spotMaskAspect : 1.0f;
            viewMatrix = Matrix4::lookAtMatrix(transform.worldPosition, light.worldDirection + transform.worldPosition, projectionUp);

            projectionMatrix = Matrix4::perspectiveMatrix(
                acos(light.outerConeCos) * 2,
                projectionAspect,
                light.shadowCameraNearFar.x,
                light.shadowCameraNearFar.y);

            light.cameras[0].lightViewMatrix = viewMatrix;
            light.cameras[0].lightProjectionMatrix = projectionMatrix;
            light.cameras[0].lightViewProjectionMatrix = projectionMatrix * viewMatrix;
            light.cameras[0].nearFar = light.shadowCameraNearFar;
            updateCameraFrustumPlanes(light.cameras[0].lightViewProjectionMatrix, light.cameras[0].frustumPlanes);

        }else if (light.type == LightType::POINT){
            Matrix4 projectionMatrix;
            Matrix4 viewMatrix[6];

            viewMatrix[0] = Matrix4::lookAtMatrix(transform.worldPosition, Vector3( 1.f, 0.f, 0.f) + transform.worldPosition, Vector3(0.f, -1.f, 0.f));
            viewMatrix[1] = Matrix4::lookAtMatrix(transform.worldPosition, Vector3(-1.f, 0.f, 0.f) + transform.worldPosition, Vector3(0.f, -1.f, 0.f));
            viewMatrix[2] = Matrix4::lookAtMatrix(transform.worldPosition, Vector3( 0.f, 1.f, 0.f) + transform.worldPosition, Vector3(0.f,  0.f, 1.f));
            viewMatrix[3] = Matrix4::lookAtMatrix(transform.worldPosition, Vector3( 0.f,-1.f, 0.f) + transform.worldPosition, Vector3(0.f,  0.f,-1.f));
            viewMatrix[4] = Matrix4::lookAtMatrix(transform.worldPosition, Vector3( 0.f, 0.f, 1.f) + transform.worldPosition, Vector3(0.f, -1.f, 0.f));
            viewMatrix[5] = Matrix4::lookAtMatrix(transform.worldPosition, Vector3( 0.f, 0.f,-1.f) + transform.worldPosition, Vector3(0.f, -1.f, 0.f));

            projectionMatrix = Matrix4::perspectiveMatrix(Angle::degToRad(90), 1, light.shadowCameraNearFar.x, light.shadowCameraNearFar.y);

            Vector2 calculedNearFar;
            float nfsub = light.shadowCameraNearFar.y - light.shadowCameraNearFar.x;
            calculedNearFar.x = (light.shadowCameraNearFar.y + light.shadowCameraNearFar.x) / nfsub * 0.5f + 0.5f;
            calculedNearFar.y =-(light.shadowCameraNearFar.y * light.shadowCameraNearFar.x) / nfsub;

            for (int f = 0; f < 6; f++){
                light.cameras[f].lightViewMatrix = viewMatrix[f];
                light.cameras[f].lightProjectionMatrix = projectionMatrix;
                light.cameras[f].lightViewProjectionMatrix = projectionMatrix * viewMatrix[f];
                light.cameras[f].nearFar = calculedNearFar;
                updateCameraFrustumPlanes(light.cameras[f].lightViewProjectionMatrix, light.cameras[f].frustumPlanes);
            }
        }
        
        
    }
}

void RenderSystem::changeLoaded(void* data){
    if (!Engine::isViewLoaded())
        return;

    check_load_t* loadObj = (check_load_t*)data;

    Scene* scene = loadObj->scene;
    Entity entity = loadObj->entity;

    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<MeshComponent>())){
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);

        mesh.loaded = true;

    }else if (signature.test(scene->getComponentId<UIComponent>())){
        UIComponent& ui = scene->getComponent<UIComponent>(entity);

        ui.loaded = true;

    }else if (signature.test(scene->getComponentId<PointsComponent>())){
        PointsComponent& points = scene->getComponent<PointsComponent>(entity);

        points.loaded = true;

    }else if (signature.test(scene->getComponentId<LinesComponent>())){
        LinesComponent& lines = scene->getComponent<LinesComponent>(entity);

        lines.loaded = true;

    }else if (signature.test(scene->getComponentId<SkyComponent>())){
        SkyComponent& sky = scene->getComponent<SkyComponent>(entity);

        sky.loaded = true;
    }

    delete (check_load_t*)data;
}

void RenderSystem::changeDestroy(void* data){
    check_load_t* loadObj = (check_load_t*)data;

    Scene* scene = loadObj->scene;
    Entity entity = loadObj->entity;

    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<MeshComponent>())){
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);

        mesh.loaded = false;
        mesh.loadCalled = false;

    }else if (signature.test(scene->getComponentId<UIComponent>())){
        UIComponent& ui = scene->getComponent<UIComponent>(entity);

        ui.loaded = false;
        ui.loadCalled = false;

    }else if (signature.test(scene->getComponentId<PointsComponent>())){
        PointsComponent& points = scene->getComponent<PointsComponent>(entity);

        points.loaded = false;
        points.loadCalled = false;

    }else if (signature.test(scene->getComponentId<LinesComponent>())){
        LinesComponent& lines = scene->getComponent<LinesComponent>(entity);

        lines.loaded = false;
        lines.loadCalled = false;

    }else if (signature.test(scene->getComponentId<SkyComponent>())){
        SkyComponent& sky = scene->getComponent<SkyComponent>(entity);

        sky.loaded = false;
        sky.loadCalled = false;
    }

    delete (check_load_t*)data;
}

// a texture cannot be sampled in the pass that renders into it; objects showing a
// camera's framebuffer must be skipped when drawing that same camera
bool RenderSystem::samplesCameraTarget(const CameraComponent& camera, const MeshComponent& mesh){
    if (!camera.renderToTexture)
        return false;

    for (unsigned int i = 0; i < mesh.numSubmeshes; i++){
        const Material& material = mesh.submeshes[i].material;
        if (material.baseColorTexture.getFramebuffer() == camera.framebuffer ||
            material.metallicRoughnessTexture.getFramebuffer() == camera.framebuffer ||
            material.normalTexture.getFramebuffer() == camera.framebuffer ||
            material.occlusionTexture.getFramebuffer() == camera.framebuffer ||
            material.emissiveTexture.getFramebuffer() == camera.framebuffer){
            return true;
        }
    }

    return false;
}

bool RenderSystem::samplesCameraTarget(const CameraComponent& camera, const Texture& texture){
    return camera.renderToTexture && texture.getFramebuffer() == camera.framebuffer;
}

bool RenderSystem::isRenderingFlipped(const CameraComponent& camera) const{
    // Cubemap sampling follows one fixed convention on every API, and with the
    // classic capture table (renderReflectionProbeCapture) it expects GL-style
    // bottom-up face storage: flip only on the backends that natively rasterize
    // top-left. This is the inverse of the 2D RTT rule below, and its winding
    // pairs with PIP_RTT_INVERT (one reversal off PIP_RTT on every backend).
    if (capturingReflectionProbe)
        return !Engine::isOpenGL();
    // OpenGL offscreen targets are bottom-up; rendering them with a Y-flipped
    // projection makes framebuffer textures top-left origin on every backend.
    // Must match the PIP_RTT pipeline selection (its winding is reversed on GL).
    return (camera.renderToTexture || Engine::getFramebuffer() || isFixedResolutionActive() || swapchainRedirect) && Engine::isOpenGL();
}

bool RenderSystem::isFixedResolutionActive() const{
    // main scene only: layer scenes keep rendering at native resolution on top
    return !disableFixedResolution
        && scene->isFixedResolutionEnabled()
        && scene->getFixedResolutionWidth() > 0
        && scene->getFixedResolutionHeight() > 0
        && Engine::getMainScene() == scene;
}

void RenderSystem::updateSwapchainRedirect(){
    // SSR and the post-process chain need the scene in an offscreen color buffer. Without
    // a framebuffer destination (exported builds) the color pass is redirected into it and
    // the last pass targets the swapchain. Main scene only: a layer scene composites over
    // the scene below, and its own pass would clear it.
    bool redirect = false;

    if (!Engine::getFramebuffer() && !isFixedResolutionActive()
            && Engine::getMainScene() == scene && Engine::isViewLoaded()){
        unsigned int w = (unsigned int)Engine::getViewRect().getWidth();
        unsigned int h = (unsigned int)Engine::getViewRect().getHeight();

        // the meshes render with PIP_RTT (flipped on GL) while it is on, so the
        // redirect only starts once everything it needs exists
        if (scene->isSSREnabled()){
            loadSSR();
            redirect = ssrLoaded && ensureSSRFramebuffers(w, h) && ensureGBufferFramebuffer(w, h);
        }
        if (!redirect && !scene->getPostProcessPasses().empty()){
            loadPostProcess();
            loadBlit();
            redirect = postProcessLoaded && blitLoaded && !postProcessPasses.empty()
                    && ensurePostProcessFramebuffers(w, h);
        }
    }

    if (redirect != swapchainRedirect){
        swapchainRedirect = redirect;
        // the flip is baked in the MVP matrices
        scene->getComponent<CameraComponent>(scene->getCamera()).needUpdate = true;
    }
}

void RenderSystem::updateMVP(size_t index, Transform& transform, CameraComponent& camera, Transform& cameraTransform){
    if (transform.billboard && !transform.fakeBillboard){

        Vector3 camPos = cameraTransform.worldPosition;

        if (transform.cylindricalBillboard)
            camPos.y = transform.worldPosition.y;

        Vector3 toCamera = camPos - transform.worldPosition;
        const float EPSILON = 1e-6f;

        if (toCamera.length() > EPSILON && toCamera.crossProduct(camera.worldUp).length() > EPSILON){ // check if not parallel
            Matrix4 m1 = Matrix4::lookAtMatrix(camPos, transform.worldPosition, camera.worldUp).inverse();

            Quaternion oldRotation = transform.rotation;

            if (m1.isValid()){
                transform.rotation.fromRotationMatrix(m1);
                transform.rotation = transform.rotation * transform.billboardRotation;
                if (transform.parent != NULL_ENTITY){
                    Transform* transformParent = scene->findComponent<Transform>(transform.parent);
                    if (transformParent){
                        transform.rotation = transformParent->worldRotation.inverse() * transform.rotation;
                    }
                }
            }

            if (transform.rotation != oldRotation){
                transform.needUpdate = true;

                std::vector<Entity> parentList;
                auto transforms = scene->getComponentArray<Transform>();
                for (int i = index; i < transforms->size(); i++){
                    Transform& childTransform = transforms->getComponentFromIndex(i);

                    // Finding childs
                    if (i > index){
                        if (std::find(parentList.begin(), parentList.end(), childTransform.parent) != parentList.end()){
                            childTransform.needUpdate = true;
                        }else{
                            break;
                        }
                    }

                    if (childTransform.needUpdate){
                        Entity entity = transforms->getEntity(i);
                        parentList.push_back(entity);
                        updateTransform(childTransform);
                    }
                }
            }
        }

    }

    if (transform.billboard && transform.fakeBillboard){
        
        Matrix4 modelViewMatrix = camera.viewMatrix * transform.modelMatrix;

        modelViewMatrix.set(0, 0, transform.worldScale.x);
        modelViewMatrix.set(0, 1, 0.0);
        modelViewMatrix.set(0, 2, 0.0);

        if (!transform.cylindricalBillboard) {
            modelViewMatrix.set(1, 0, 0.0);
            modelViewMatrix.set(1, 1, transform.worldScale.y);
            modelViewMatrix.set(1, 2, 0.0);
        }

        modelViewMatrix.set(2, 0, 0.0);
        modelViewMatrix.set(2, 1, 0.0);
        modelViewMatrix.set(2, 2, transform.worldScale.z);

        transform.modelViewProjectionMatrix = camera.projectionMatrix * modelViewMatrix;

    }else{

        transform.modelViewProjectionMatrix = camera.viewProjectionMatrix * transform.modelMatrix;

    }

    if (isRenderingFlipped(camera)){
        // flip only the render matrix; camera.viewProjectionMatrix stays logical
        // (top-left screen origin) for frustum planes, rays and UI hit tests
        static const Matrix4 flipYMatrix = Matrix4::scaleMatrix(Vector3(1, -1, 1));
        transform.modelViewProjectionMatrix = flipYMatrix * transform.modelViewProjectionMatrix;
    }

    transform.distanceToCamera = (cameraTransform.worldPosition - transform.worldPosition).length();
}

void RenderSystem::update(double dt){
    if (paused) {
        return;
    }

    int numLights = checkLightsAndShadow();

    auto transforms = scene->getComponentArray<Transform>();
    auto cameras = scene->getComponentArray<CameraComponent>();

    for (int i = 0; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);

        if (transform.parent != NULL_ENTITY){
            Transform* transformParent = scene->findComponent<Transform>(transform.parent);
            if (transformParent){
                if (transformParent->needUpdate){
                    transform.needUpdate = true;
                }

                if (transformParent->needUpdateChildVisibility){
                    transform.visible = transformParent->visible;
                    transform.needUpdateChildVisibility = true;
                }
            }
        }

        if (transform.needUpdate){
            updateTransform(transform);
        }
    }

    auto models = scene->getComponentArray<ModelComponent>();
    for (int i = 0; i < models->size(); i++) {
        ModelComponent& model = models->getComponentFromIndex(i);
        for (const ModelSkinBinding& binding : model.skinBindings) {
            MeshComponent* mesh = scene->findComponent<MeshComponent>(binding.mesh);
            Transform* meshTransform = scene->findComponent<Transform>(binding.mesh);
            if (!mesh || !meshTransform)
                continue;

            bool dirty = meshTransform->needUpdate;
            for (Entity joint : binding.joints) {
                if (dirty)
                    break;
                Transform* jointTransform = scene->findComponent<Transform>(joint);
                dirty = jointTransform && jointTransform->needUpdate;
            }
            if (!dirty)
                continue;

            const Matrix4 inverseMeshTransform = meshTransform->modelMatrix.inverse();
            // Scale 0 makes this NaN; posing the skinned AABB with it trips setExtents.
            if (!inverseMeshTransform.isValid())
                continue;

            const size_t jointCount = std::min(binding.joints.size(), binding.inverseBindMatrices.size());
            for (size_t jointIndex = 0; jointIndex < jointCount &&
                    mesh->bonesMatrix.validIndex(static_cast<int>(jointIndex)); jointIndex++) {
                Transform* jointTransform = scene->findComponent<Transform>(binding.joints[jointIndex]);
                if (jointTransform)
                    mesh->bonesMatrix[jointIndex] = inverseMeshTransform * jointTransform->modelMatrix *
                        binding.inverseBindMatrices[jointIndex];
            }
            mesh->needUpdateBones = true;
            mesh->needUpdateAABB = true;
        }
    }

    updateReflectionProbes(dt);

    // the pipeline mask below depends on the redirect
    updateSwapchainRedirect();

    Entity mainCameraEntity = scene->getCamera();
    uint8_t pipelines = 0;

    hasMultipleCameras = false;
    for (int i = 0; i < cameras->size(); i++){
        CameraComponent& camera = cameras->getComponentFromIndex(i);
        Entity cameraEntity = cameras->getEntity(i);
        Transform* cameraTransformPtr = scene->findComponent<Transform>(cameraEntity);
        if (!cameraTransformPtr)
            continue;
        Transform& cameraTransform = *cameraTransformPtr;

        if (camera.renderToTexture){
            // sync authored framebuffer settings (editor/serialized) into the live
            // framebuffer; recreate it when they change so edits take effect. A
            // non-positive size is ignored so an in-use framebuffer is never left
            // destroyed (meshes sampling an uncreated framebuffer would be invalid).
            bool validSize = camera.framebufferWidth > 0 && camera.framebufferHeight > 0;

            bool settingsChanged = validSize &&
                (camera.framebuffer->getWidth() != camera.framebufferWidth ||
                 camera.framebuffer->getHeight() != camera.framebufferHeight ||
                 camera.framebuffer->getMinFilter() != camera.framebufferFilter ||
                 camera.framebuffer->getMagFilter() != camera.framebufferFilter);

            if (settingsChanged){
                camera.framebuffer->setWidth(camera.framebufferWidth);
                camera.framebuffer->setHeight(camera.framebufferHeight);
                camera.framebuffer->setMinFilter(camera.framebufferFilter);
                camera.framebuffer->setMagFilter(camera.framebufferFilter);

                if (camera.framebuffer->isCreated()){
                    camera.framebuffer->destroy();
                }
            }

            bool created = false;
            if (!camera.framebuffer->isCreated() && validSize){
                camera.framebuffer->create();
                created = true;
            }

            // autoResize cameras derive clip/aspect from the framebuffer size;
            // reconcile on first creation and after any size change, otherwise the
            // camera keeps a stale (e.g. canvas) aspect and the RTT image is distorted
            if (settingsChanged || created){
                updateCameraSize(cameraEntity);
            }
        }

        if (camera.renderToTexture && cameraEntity != mainCameraEntity){
            hasMultipleCameras = true;
        }

        if (cameraEntity == mainCameraEntity && !camera.renderToTexture){
            pipelines |= PIP_DEFAULT;
        }

        if (camera.renderToTexture || Engine::getFramebuffer() || isFixedResolutionActive() || swapchainRedirect){
            pipelines |= PIP_RTT;
        }

        if (cameraTransform.needUpdate){
            camera.needUpdate = true;
        }
        
        if (camera.needUpdate){
            updateCamera(camera, cameraTransform);
        }
    }

    // mirrors render the scene with reversed winding (handedness flip); bake the
    // inverted RTT pipeline for meshes only when a mirror is present
    if (scene->getComponentArray<MirrorComponent>()->size() > 0){
        pipelines |= PIP_RTT_INVERT;
    }
    if (scene->getComponentArray<ReflectionProbeComponent>()->size() > 0){
        // probe capture renders with PIP_RTT_INVERT (see isRenderingFlipped)
        pipelines |= PIP_RTT_INVERT;
    }

    // drive mirror reflection cameras from the (now updated) main camera, so the
    // reflection passes render with current matrices in the draw step. This may
    // create reflection camera entities (reallocating the camera/transform arrays),
    // so fetch mainCamera/mainCameraTransform references AFTER it returns.
    updateMirrors(mainCameraEntity);

    // the rest of the frame is built from the main camera view
    CameraComponent* mainCameraPtr = scene->findComponent<CameraComponent>(mainCameraEntity);
    Transform* mainCameraTransformPtr = scene->findComponent<Transform>(mainCameraEntity);
    if (!mainCameraPtr || !mainCameraTransformPtr)
        return;

    CameraComponent& mainCamera = *mainCameraPtr;
    Transform& mainCameraTransform = *mainCameraTransformPtr;

    // while extra cameras render, draw() rewrites the shared MVP and sky matrices
    // per camera; removing the last of them (a mirror, a reflection probe) would
    // otherwise leave the scene on that camera's view until something turns dirty
    bool multiCameraDraw = hasMultipleCameras || hasReflectionProbes;
    if (lastMultiCameraDraw && !multiCameraDraw){
        mainCamera.needUpdate = true;
    }
    lastMultiCameraDraw = multiCameraDraw;

    // the destination is baked into the pipelines at load, so a scene entering or
    // leaving a stack (or fixed resolution switching) has to reload with the new set
    if (pipelines != loadedPipelines){
        loadedPipelines = pipelines;

        needReloadMeshes();
        needReloadUIs();
        needReloadPoints();
        needReloadLines();
        needReloadSky();
    }

    loadLights(numLights);
    updateSpotMaskAtlas(numLights);
    loadLights2D();
    loadAndProcessFog();

    auto skys = scene->getComponentArray<SkyComponent>();
    bool newHasIBL = false;
    if (skys->size() > 0){
        SkyComponent& sky = skys->getComponentFromIndex(0);
        Entity entity = skys->getEntity(0);
        if (mainCamera.needUpdate || sky.needUpdateSky){
            if (!hasMultipleCameras){
                updateSkyViewProjection(sky, mainCamera);
            }
        }

        if (sky.needUpdateTexture){
            if (sky.texture.empty()){
                sky.needReload = true;
            }
            // texture changed: IBL environment maps must be regenerated (or destroyed)
            sky.needUpdateEnvironment = true;
        }

        // generate IBL maps before loadSky uploads (and releases) the texture pixels
        if (Engine::isViewLoaded()){
            updateSkyEnvironment(sky);
        }

        if (sky.loaded && sky.needReload){
            destroySky(entity, sky);
        }
        if (!sky.loadCalled){
            loadSky(entity, sky, pipelines);
        }

        newHasIBL = sky.envMapsLoaded;
    }

    if (hasIBL != newHasIBL){
        hasIBL = newHasIBL;
        needReloadMeshes();
    }

    for (int i = 0; i < transforms->size(); i++){
        Transform& transform = transforms->getComponentFromIndex(i);

        Entity entity = transforms->getEntity(i);
        Signature signature = scene->getSignature(entity);

        if (signature.test(scene->getComponentId<MeshComponent>())){
            MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);

            TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);

            InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
            if (instmesh){
                bool sortTransparentInstances = mesh.transparent && mainCamera.type != CameraType::CAMERA_UI;

                bool instancesNeedUpdate = instmesh->needUpdateInstances || mesh.needUpdateAABB;

                if (instancesNeedUpdate && !instmesh->instancedBillboard){
                    updateInstancedMesh(*instmesh, mesh, transform, mainCamera, mainCameraTransform);
                }

                if (instancesNeedUpdate || ((mainCamera.needUpdate || transform.needUpdate) && sortTransparentInstances)){
                    // Sort/upload transparent instances once here for the main camera.
                    // The instance buffer is shared by every camera and sokol only allows
                    // one sg_update_buffer per buffer per frame (VALIDATE_UPDATEBUF_ONCE),
                    // so render-to-texture cameras reuse this ordering instead of
                    // re-sorting per camera (which would upload the buffer again).
                    if (instmesh->instancedBillboard){
                        updateInstancedMesh(*instmesh, mesh, transform, mainCamera, mainCameraTransform);
                    }
                    sortInstancedMesh(*instmesh, mesh, transform, mainCamera, mainCameraTransform);
                }

                instmesh->needUpdateInstances = false;
            }

            if (terrain && mesh.numSubmeshes > 1){
                for (unsigned int s = 1; s < mesh.numSubmeshes; s++){
                    const bool materialOutOfSync = mesh.submeshes[s].material != mesh.submeshes[0].material;
                    if (materialOutOfSync){
                        mesh.submeshes[s].material = mesh.submeshes[0].material;
                    }
                    if (mesh.submeshes[0].needUpdateTexture || materialOutOfSync){
                        mesh.submeshes[s].needUpdateTexture = true;
                    }
                }
            }

            bool expectedTransparent = false;
            for (int s = 0; s < mesh.numSubmeshes; s++){
                const MaterialAlphaMode alphaMode = mesh.submeshes[s].material.alphaMode;
                const bool alphaBlend = usesAlphaBlend(mesh.submeshes[s].material);
                expectedTransparent = expectedTransparent || alphaBlend;

                if (mesh.submeshes[s].alphaBlend != alphaBlend){
                    mesh.needReload = true;
                }

                // Explicit alpha modes own the derived shadow-cutout state. AUTO
                // remains the compatibility path for setCastShadowsWithTexture().
                if (alphaMode != MaterialAlphaMode::AUTO){
                    const bool expectedTextureShadow = alphaMode == MaterialAlphaMode::MASK;
                    if (mesh.submeshes[s].textureShadow != expectedTextureShadow){
                        mesh.submeshes[s].textureShadow = expectedTextureShadow;
                        mesh.needReload = true;
                    }
                }

                // Alpha mode can be changed directly by scripts/generated code, so
                // shader variant reconciliation must not depend on a texture update.
                const bool shaderAlphaMask = (mesh.submeshes[s].shaderProperties & (1u << 24)) != 0;
                const bool shaderAlphaOpaque = (mesh.submeshes[s].shaderProperties & (1u << 25)) != 0;
                if (shaderAlphaMask != (alphaMode == MaterialAlphaMode::MASK) ||
                    shaderAlphaOpaque != (alphaMode == MaterialAlphaMode::ALPHA_OPAQUE)) {
                    mesh.needReload = true;
                }

                if (mesh.submeshes[s].needUpdateTexture){
                    if (terrain){
                        transform.needUpdate = true;
                    }
                    if (mesh.submeshes[s].textureShadow){
                        mesh.submeshes[s].needUpdateDepthTexture = true;
                    }
                    if (mesh.submeshes[s].gbufferShader){
                        mesh.submeshes[s].needUpdateGBufferTexture = true;
                    }
                    if (checkPBRTextures(mesh.submeshes[s].material, mesh.receiveLights)){
                        if (!(mesh.submeshes[s].shaderProperties & (1 << 1)) && !(mesh.submeshes[s].shaderProperties & (1 << 2))){ // not 'Uv1' and not 'Uv2'
                            mesh.needReload = true;
                        }
                    }else{
                        if ((mesh.submeshes[s].shaderProperties & (1 << 1)) || (mesh.submeshes[s].shaderProperties & (1 << 2))){ // 'Uv1' or 'Uv2'
                            mesh.needReload = true;
                        }
                    }
                    if (!mesh.submeshes[s].material.normalTexture.empty()){
                        if (!(mesh.submeshes[s].shaderProperties & (1 << 7))){ // not 'Nmp'
                            mesh.needReload = true;
                        }
                    }else{
                        if (mesh.submeshes[s].shaderProperties & (1 << 7)){ // 'Nmp'
                            mesh.needReload = true;
                        }
                    }

                }
            }

            if (mesh.autoTransparency && mesh.transparent != expectedTransparent){
                mesh.needReload = true;
            }

            if (mesh.loaded && mesh.needReload){
                destroyMesh(entity, mesh);
            }
            if (!mesh.loadCalled){
                loadMesh(entity, mesh, pipelines, instmesh, terrain);
            }
            if (mesh.needUpdateAABB || transform.needUpdate){
                // Bones cancel out this mesh's model matrix, so the shader draws at
                // modelMatrix * bonesMatrix[b] * vertex and modelMatrix * aabb mis-sizes the box
                // once posed. Each bone bounds only its own share: the whole aabb per bone works
                // too, but unions body-sized boxes over the skeleton. Local space so the editor
                // draws the same box.
                mesh.skinnedAABB.setNull();
                if (!instmesh){
                    const size_t boneBounds = std::min(mesh.bonesAABB.size(), mesh.bonesMatrix.size());
                    for (size_t b = 0; b < boneBounds; b++){
                        if (!mesh.bonesAABB[b].isNull()){
                            mesh.skinnedAABB.merge(mesh.bonesMatrix[b] * mesh.bonesAABB[b]);
                        }
                    }

                    if (mesh.skinnedAABB.isNull()){ // no per-bone bounds to pose
                        const Matrix4 identityMatrix;
                        for (size_t b = 0; b < mesh.bonesMatrix.size(); b++){
                            const Matrix4& bone = mesh.bonesMatrix[b];
                            if (bone != identityMatrix){
                                mesh.skinnedAABB.merge(bone * mesh.aabb);
                            }
                        }
                    }
                }
                mesh.worldAABB = mesh.skinnedAABB.isNull() ? (transform.modelMatrix * mesh.aabb)
                                                           : (transform.modelMatrix * mesh.skinnedAABB);

                mesh.needUpdateAABB = false;

                // Tilemap chunk bounds are refreshed with the mesh AABB: rebuilding a
                // tilemap recalculates that AABB, and MeshSystem updates before this
                // system, so new chunks are always current before draw().
                if (signature.test(scene->getComponentId<TilemapComponent>())){
                    TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(entity);
                    for (TilemapChunk& chunk : tilemap.chunks){
                        chunk.worldAABB = transform.modelMatrix * chunk.aabb;
                    }
                }
            }
        }else if (signature.test(scene->getComponentId<UIComponent>())){
            UIComponent& ui = scene->getComponent<UIComponent>(entity);

            if (ui.needUpdateTexture){
                if (!ui.texture.empty()){
                    if (!(ui.shaderProperties & (1 << 0))){ // not 'Tex'
                        ui.needReload = true;
                    }
                }else{
                    if (ui.shaderProperties & (1 << 0)){ // 'Tex'
                        ui.needReload = true;
                    }
                }
            }

            if (ui.loaded && ui.needReload){
                destroyUI(entity, ui);
            }
            if (!ui.loadCalled){
                bool isText = false;
                if (signature.test(scene->getComponentId<TextComponent>())){
                    isText = true;
                }
                loadUI(entity, ui, pipelines, isText);
            }
            if (ui.needUpdateAABB || transform.needUpdate){
                ui.worldAABB = transform.modelMatrix * ui.aabb;
                ui.needUpdateAABB = false;
            }
        }else if (signature.test(scene->getComponentId<PointsComponent>())){
            PointsComponent& points = scene->getComponent<PointsComponent>(entity);

            if (points.needUpdateTexture){
                if (!points.texture.empty()){
                    if (!(points.shaderProperties & (1 << 0))){ // not 'Tex'
                        points.needReload = true;
                    }
                }else{
                    if (points.shaderProperties & (1 << 0)){ // 'Tex'
                        points.needReload = true;
                    }
                }
            }

            if (points.loaded && points.needReload){
                destroyPoints(entity, points);
            }
            if (!points.loadCalled){
                loadPoints(entity, points, pipelines);
            }

            bool sortTransparentPoints = points.transparent && mainCamera.type != CameraType::CAMERA_UI;

            if (points.needUpdate){
                updatePoints(points, transform, mainCamera, mainCameraTransform);
            }

            // Sort/upload transparent points once here for the main camera. The points
            // buffer is shared by every camera and sokol only allows one sg_update_buffer
            // per buffer per frame (VALIDATE_UPDATEBUF_ONCE), so render-to-texture cameras
            // reuse this ordering instead of re-sorting per camera.
            if (sortTransparentPoints && (points.needUpdate || mainCamera.needUpdate || transform.needUpdate)){
                sortPoints(points, transform, mainCamera, mainCameraTransform);
            }

            if (points.loaded){
                points.needUpdate = false;
            }
        }else if (signature.test(scene->getComponentId<LinesComponent>())){
            LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
            if (lines.loaded && lines.needReload){
                destroyLines(entity, lines);
            }
            if (!lines.loadCalled){
                loadLines(entity, lines, pipelines);
            }

        }else if (signature.test(scene->getComponentId<LightComponent>())){
            LightComponent& light = scene->getComponent<LightComponent>(entity);

            if (mainCamera.needUpdate || transform.needUpdate || light.needUpdateShadowCamera){
                // need to be updated ONLY for main camera
                updateLightFromScene(light, transform, mainCamera, mainCameraTransform);

                light.needUpdateShadowCamera = false;
            }
        }

        if (mainCamera.needUpdate || transform.needUpdate){

            // need to be updated for every camera
            if (!hasMultipleCameras){
                updateMVP(i, transform, mainCamera, mainCameraTransform);
            }

            // Select the main camera's CDLOD cut (view 0) once per frame; both the
            // shadow depth pass and the main color pass use it. Render-to-texture
            // cameras select their own views (1..N-1) during draw().
            if (signature.test(scene->getComponentId<TerrainComponent>())){
                TerrainComponent& terrain = scene->getComponent<TerrainComponent>(entity);

                updateTerrain(terrain, transform, mainCamera, mainCameraTransform, 0);
            }
            
            if (signature.test(scene->getComponentId<SoundComponent>())){
                SoundComponent& audio = scene->getComponent<SoundComponent>(entity);

                audio.needUpdate = true;
            }

        }

        if (transform.needUpdate){

            if (signature.test(scene->getComponentId<ModelComponent>())){
                ModelComponent& model = scene->getComponent<ModelComponent>(entity);

                model.inverseDerivedTransform = transform.modelMatrix.inverse();
            }

            if (signature.test(scene->getComponentId<BoneComponent>())){
                BoneComponent& bone = scene->getComponent<BoneComponent>(entity);

                if (bone.model != NULL_ENTITY && bone.index >= 0){
                    ModelComponent* model = scene->findComponent<ModelComponent>(bone.model);

                    if (model && model->skinBindings.empty()) {
                        Matrix4 skinning = model->inverseDerivedTransform * transform.modelMatrix * bone.offsetMatrix;

                        auto updateDrivenMesh = [&](MeshComponent* drivenMesh) {
                            if (!drivenMesh || !drivenMesh->bonesMatrix.validIndex(bone.index))
                                return;
                            drivenMesh->bonesMatrix[bone.index] = skinning;
                            drivenMesh->needUpdateBones = true;
                            drivenMesh->needUpdateAABB = true;
                        };

                        // Single-mesh / flattened models keep their skinned geometry on the model
                        // entity's own mesh. Multi-node skinned models split each mesh node into a
                        // child entity (each with its own MeshComponent), so the same bone must drive
                        // every child mesh that shares this skeleton.
                        // A moved bone changes the skinned bounds, so flag the driven mesh(es)
                        // for a world-AABB rebuild. Bones are processed after their mesh in this
                        // loop, so this takes effect next frame with the fresh bonesMatrix.
                        updateDrivenMesh(scene->findComponent<MeshComponent>(bone.model));

                        for (auto const& meshNode : model->meshNodesMapping){
                            updateDrivenMesh(scene->findComponent<MeshComponent>(meshNode.second));
                        }
                    }
                }
            }

        }

        transform.needUpdateChildVisibility = false;
        transform.needUpdate = false;
    }

    for (int i = 0; i < cameras->size(); i++){
        CameraComponent& camera = cameras->getComponentFromIndex(i);
        if (camera.needUpdate){
            camera.needUpdate = false;
        }
    }

    processLights(numLights, mainCamera, mainCameraTransform);
    if (hasLights2D){
        processLights2D();
    }

    // build the merged occluder segment list once per frame (world transforms are
    // final here); draw() uploads it at most once, even when the same scene is
    // drawn multiple times per frame (e.g. open as a tab AND layered as a child scene)
    if (hasShadows2D && hasShadow2DAtlas){
        occluder2DVertexCount = buildOccluder2DSegments();
        occluder2DNeedUpdateBuffer = (occluder2DVertexCount > 0);
    }else{
        occluder2DVertexCount = 0;
        occluder2DNeedUpdateBuffer = false;
    }
}

void RenderSystem::draw(){
    std::priority_queue<TransparentRenderData, std::vector<TransparentRenderData>, TransparentRenderComparison> transparentRenders;

    auto transforms = scene->getComponentArray<Transform>();
    auto cameras = scene->getComponentArray<CameraComponent>();

    updateShadowBindings();
    updateAllTerrainRenderTextures();
    updateInstanceBuffers();

    // free the fixed-resolution target when the setting is turned off (the blit
    // pipeline itself is kept; it is cheap and may be re-enabled)
    if (!isFixedResolutionActive() && fixedResFramebuffer.isCreated()){
        fixedResFramebuffer.destroy();
        fixedResWidth = 0;
        fixedResHeight = 0;
    }

    //---------Depth shader----------
    if (hasShadows){
        auto lights = scene->getComponentArray<LightComponent>();
        auto meshes = scene->getComponentArray<MeshComponent>();
        int shadowLightCount = std::min((int)lights->size(), MAX_LIGHTS);

        auto drawShadowCasters = [&](LightComponent& light, int cameraIndex, PipelineType pipelineType){
            for (int i = 0; i < meshes->size(); i++){
                MeshComponent& mesh = meshes->getComponentFromIndex(i);
                Entity entity = meshes->getEntity(i);
                Transform* transform = scene->findComponent<Transform>(entity);

                if (!transform || !transform->visible){
                    continue;
                }

                InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
                TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
                TilemapComponent* tilemap = scene->findComponent<TilemapComponent>(entity);

                vs_depth_t vsDepthParams;
                if (transform->billboard && mesh.shadowsBillboard){
                    Matrix4 modelViewMatrix = light.cameras[cameraIndex].lightViewMatrix * transform->modelMatrix;

                    modelViewMatrix.set(0, 0, transform->worldScale.x);
                    modelViewMatrix.set(0, 1, 0.0);
                    modelViewMatrix.set(0, 2, 0.0);

                    if (!transform->cylindricalBillboard) {
                        modelViewMatrix.set(1, 0, 0.0);
                        modelViewMatrix.set(1, 1, transform->worldScale.y);
                        modelViewMatrix.set(1, 2, 0.0);
                    }

                    modelViewMatrix.set(2, 0, 0.0);
                    modelViewMatrix.set(2, 1, 0.0);
                    modelViewMatrix.set(2, 2, transform->worldScale.z);

                    vsDepthParams = {modelViewMatrix, light.cameras[cameraIndex].lightProjectionMatrix};
                }else{
                    vsDepthParams = {transform->modelMatrix, light.cameras[cameraIndex].lightViewProjectionMatrix};
                }

                drawMeshDepth(
                    mesh,
                    light.cameras[cameraIndex].nearFar.y,
                    light.cameras[cameraIndex].frustumPlanes,
                    vsDepthParams,
                    instmesh,
                    terrain,
                    tilemap,
                    false,
                    pipelineType);
            }
        };

        // All projective slots share one pass: clear once, then viewport/scissor per slot.
        bool projectivePassStarted = false;
        for (int l = 0; l < shadowLightCount; l++){
            LightComponent& light = lights->getComponentFromIndex(l);
            if (light.type == LightType::POINT ||
                light.intensity <= 0 || !light.shadows ||
                light.shadowMapIndex < 0 || !hasShadowAtlas){
                continue;
            }

            int numShadowCameras =
                (light.type == LightType::DIRECTIONAL) ? (int)light.numShadowCascades : 1;
            for (int c = 0; c < numShadowCameras; c++){
                int slotIndex = light.shadowMapIndex +
                    ((light.type == LightType::DIRECTIONAL) ? c : 0);
                if (slotIndex < 0 || slotIndex >= shadowAtlasUsedSlots){
                    continue;
                }

                if (!projectivePassStarted){
                    shadowAtlasPassRender.setClearDepth(1.0f);
                    shadowAtlasPassRender.startRenderPass(&shadowAtlasFramebuffer);
                    projectivePassStarted = true;
                }

                Rect slotRect = getShadowAtlasSlotRect(slotIndex);
                shadowAtlasPassRender.applyViewport(slotRect);
                shadowAtlasPassRender.applyScissor(slotRect);
                drawShadowCasters(light, c, PIP_SHADOW_DEPTH);
            }
        }
        if (projectivePassStarted){
            shadowAtlasPassRender.endRenderPass();
        }

        // Point shadows keep the packed-color depth per-face pass (PIP_DEPTH).
        bool pointAtlasSlotWritten = false;
        for (int l = 0; l < shadowLightCount; l++){
            LightComponent& light = lights->getComponentFromIndex(l);
            if (light.type != LightType::POINT ||
                light.intensity <= 0 || !light.shadows ||
                light.shadowMapIndex < 0 || !hasShadowPointAtlas){
                continue;
            }

            for (int c = 0; c < SHADOW_CUBE_FACES; c++){
                int slotIndex = light.shadowMapIndex + c;
                if (slotIndex < 0 || slotIndex >= MAX_POINT_SHADOW_ATLAS_SLOTS){
                    continue;
                }

                if (!pointAtlasSlotWritten){
                    shadowPointAtlasPassRender.setClearColor(Vector4(1.0, 1.0, 1.0, 1.0));
                }else{
                    shadowPointAtlasPassRender.setLoadActionLoad();
                }

                Rect slotRect = getShadowPointAtlasSlotRect(slotIndex);
                shadowPointAtlasPassRender.startRenderPass(&shadowPointAtlasFramebuffer);
                shadowPointAtlasPassRender.applyViewport(slotRect);
                shadowPointAtlasPassRender.applyScissor(slotRect);
                pointAtlasSlotWritten = true;

                drawShadowCasters(light, c, PIP_DEPTH);
                shadowPointAtlasPassRender.endRenderPass();
            }
        }
    }

    //---------2D light shadow pass (1D polar rows)----------
    if (hasShadows2D && hasShadow2DAtlas){
        if (occluder2DVertexCount > 0 && (!occluder2DLoaded || occluder2DVertexCount > occluder2DBufferCapacity)){
            unsigned int capacity = std::max(occluder2DVertexCount * 2u, 256u);
            loadOccluder2DPass(capacity);
        }

        bool drawSegments = occluder2DLoaded && occluder2DVertexCount > 0;

        if (drawSegments && occluder2DNeedUpdateBuffer){
            // occluders are world-space and can move every frame: re-upload the merged
            // segment buffer (built in update(), at most once per frame — sokol allows
            // a single sg_update_buffer per buffer and frame)
            occluder2DBuffer.setData((unsigned char*)occluder2DSegments.data(), occluder2DSegments.size() * sizeof(float));
            occluder2DBuffer.getRender()->updateBuffer(occluder2DBuffer.getSize(), occluder2DBuffer.getData());
            occluder2DNeedUpdateBuffer = false;
        }

        // rows are rendered (or at least cleared) every frame even with zero
        // segments: the rows are in light-relative polar space, so stale contents
        // would make old shadows follow the light around
        auto lights2d = scene->getComponentArray<Light2DComponent>();
        bool atlasRowWritten = false;

        for (int l = 0; l < numLights2D; l++){
            Light2DComponent& light = lights2d->getComponentFromIndex(l);
            if (!light.shadows || light.shadowMapIndex < 0 || light.intensity <= 0){
                continue;
            }
            Entity entity = lights2d->getEntity(l);
            Transform* lightTransform = scene->findComponent<Transform>(entity);
            if (!lightTransform){
                continue;
            }

            // first row of the frame clears the whole atlas to white (= far/unoccluded)
            if (!atlasRowWritten){
                shadow2DAtlasPassRender.setClearColor(Vector4(1.0, 1.0, 1.0, 1.0));
            }else{
                shadow2DAtlasPassRender.setLoadActionLoad();
            }
            Rect rowRect(0.0f, (float)light.shadowMapIndex, (float)shadow2DAtlasWidth, 1.0f);
            shadow2DAtlasPassRender.startRenderPass(&shadow2DAtlasFramebuffer);
            shadow2DAtlasPassRender.applyViewport(rowRect);
            shadow2DAtlasPassRender.applyScissor(rowRect);
            atlasRowWritten = true;

            if (drawSegments && occluder2DRender.beginDraw(PIP_DEPTH)){
                vs_shadow2d_t shadow2dParams;
                shadow2dParams.lightPos_range = Vector4(
                    lightTransform->worldPosition.x, lightTransform->worldPosition.y,
                    0.0f, std::max(light.range, 0.001f));

                // three passes at NDC x offsets -2/0/+2 so segments crossing the
                // theta = +-pi seam rasterize on both ends of the row
                for (int k = -1; k <= 1; k++){
                    shadow2dParams.offset = Vector4(2.0f * (float)k, 0.0f, 0.0f, 0.0f);
                    occluder2DRender.applyUniformBlock(shadow2DSlotParams, sizeof(vs_shadow2d_t), &shadow2dParams);
                    occluder2DRender.draw(0, occluder2DVertexCount, 1);
                }
            }

            shadow2DAtlasPassRender.endRenderPass();
        }
    }

    // Runtime-captured probes share a strict budget: one cubemap face total per
    // frame. Static probes without an authored cubemap capture once; dynamic
    // probes follow their configured update policy.
    renderReflectionProbeCapture();

    // assigns each rendering camera its own terrain CDLOD view slot this frame:
    // main camera = 0 (selected in update(), also used by the shadow pass), each
    // render-to-texture camera = 1..N-1; cameras past the cap reuse the main view
    int terrainViewCounter = 0;

    for (int i = 0; i < cameras->size(); i++){
        Entity cameraEntity = cameras->getEntity(i);
        CameraComponent& camera = cameras->getComponentFromIndex(i);
        Transform* cameraTransformPtr = scene->findComponent<Transform>(cameraEntity);
        if (!cameraTransformPtr)
            continue;
        Transform& cameraTransform = *cameraTransformPtr;

        bool isMainCamera = (cameraEntity == scene->getCamera());

        if (!isMainCamera && !camera.renderToTexture){
            continue; // camera is not used
        }

        int terrainView = 0;
        if (!isMainCamera){
            terrainViewCounter++;
            terrainView = (terrainViewCounter < MAX_TERRAIN_VIEWS) ? terrainViewCounter : 0;
        }

        // Screen-space reflections (main camera only). The G-buffer geometry pass runs
        // FIRST so SSAO can share its depth (a single geometry pre-pass feeds both
        // effects). Further below the opaque color pass is redirected into an offscreen
        // buffer, then renderSSR() marches the G-buffer and composites reflections to the
        // real destination: a framebuffer (editor / render-to-texture / fixed resolution)
        // or the swapchain in exported builds (see updateSwapchainRedirect).
        // Fixed game resolution (main scene main camera only): the color pass is
        // redirected into fixedResFramebuffer at the scene's fixed size, and
        // renderFixedResolutionBlit() upscales it to the view rect afterwards.
        bool useFixedRes = false;
        if (isMainCamera && !camera.renderToTexture && isFixedResolutionActive()){
            loadBlit();
            if (blitLoaded && ensureFixedResFramebuffer(scene->getFixedResolutionWidth(),
                    scene->getFixedResolutionHeight(), scene->getFixedResolutionFilter())){
                useFixedRes = true;
            }
        }

        bool useSSR = false;
        FramebufferRender* ssrDestination = nullptr;
        if (isMainCamera && scene->isSSREnabled() && (Engine::getFramebuffer() || camera.renderToTexture || useFixedRes || swapchainRedirect)){
            loadSSR();
            unsigned int sw = (unsigned int)Engine::getViewRect().getWidth();
            unsigned int sh = (unsigned int)Engine::getViewRect().getHeight();
            if (useFixedRes){
                // effects run at the fixed resolution: SSR composites into the
                // fixed-res target, which the blit then upscales
                sw = fixedResWidth;
                sh = fixedResHeight;
            }
            if (ssrLoaded && ensureSSRFramebuffers(sw, sh) && ensureGBufferFramebuffer(sw, sh)){
                renderGBufferPass(camera);
                useSSR = true;
            }
        }

        // User post-process chain: like SSR it needs the scene color offscreen, so it
        // follows the same destination-capture rule. Main scene only: its last pass
        // writes every pixel, so a layer scene would erase the scene below it.
        bool usePostProcess = false;
        FramebufferRender* postDestination = nullptr;
        if (isMainCamera && !scene->getPostProcessPasses().empty()
                && (Engine::getMainScene() == scene || camera.renderToTexture)
                && (Engine::getFramebuffer() || camera.renderToTexture || useFixedRes || swapchainRedirect)){
            loadPostProcess();

            // only a swapchain destination ends the chain with a blit
            bool needsBlit = !(Engine::getFramebuffer() || camera.renderToTexture || useFixedRes);
            if (needsBlit){
                loadBlit();
            }

            unsigned int pw = useSSR ? ssrWidth : (unsigned int)Engine::getViewRect().getWidth();
            unsigned int ph = useSSR ? ssrHeight : (unsigned int)Engine::getViewRect().getHeight();
            if (useFixedRes){
                pw = fixedResWidth;
                ph = fixedResHeight;
            }

            if (postProcessLoaded && !postProcessPasses.empty() && (!needsBlit || blitLoaded)
                    && ensurePostProcessFramebuffers(pw, ph)){
                usePostProcess = true;

                // a pass sampling depth needs the pre-pass when neither SSR nor SSAO ran
                if (postProcessNeedsDepth && !useSSR && !scene->isSSAOEnabled()
                        && ensureSSAOFramebuffers(pw, ph)){
                    renderDepthPrePass(camera);
                }
            }
        }

        // Screen-space AO is produced once for the main camera before its color pass.
        // When SSR is active its G-buffer already holds the camera depth (color[0]), so
        // SSAO reuses it instead of running a second geometry pre-pass. Other cameras
        // sample empty white (AO = 1) so their USE_SSAO meshes are unaffected.
        currentSSAOTexture = &emptyWhite;
        if (isMainCamera && scene->isSSAOEnabled()){
            TextureRender* sharedDepth = useSSR ? &gbufferFramebuffer.getRender().getColorAttachmentTexture(0) : nullptr;
            renderSSAO(camera, sharedDepth);
        }

        // whether this camera's color pass targets an offscreen framebuffer
        // (selects PIP_RTT pipelines and flipped rendering on GL)
        bool offscreenTarget = camera.renderToTexture || Engine::getFramebuffer() || useFixedRes || useSSR || usePostProcess;

        if (Engine::getMainScene() == scene || camera.renderToTexture){
            camera.render.setClearColor(scene->getBackgroundColor());
        } else {
            // When drawing a scene as an Engine layer, don't clear the color buffer.
            // Otherwise a scene that was previously the main scene would keep clearing
            // and hide the main scene when reused as a layer.
            camera.render.setLoadActionLoad();
        }

        // the space the UI scissor lives in
        Rect colorPassViewport;

        if (useSSR || usePostProcess){
            // capture the real destination for the composite / post-process chain, then
            // redirect the scene into the offscreen color buffer
            FramebufferRender* destination = nullptr;
            if (camera.renderToTexture){
                if (!camera.framebuffer->isCreated()){
                    camera.framebuffer->create();
                }
                destination = &camera.framebuffer->getRender();
            }else if (useFixedRes){
                destination = &fixedResFramebuffer.getRender();
            }else if (Engine::getFramebuffer()){
                if (!Engine::getFramebuffer()->isCreated()){
                    Engine::getFramebuffer()->create();
                }
                destination = &Engine::getFramebuffer()->getRender();
            }

            // the chain samples its input, so whatever runs before it writes the first
            // ping-pong buffer and the chain writes the real destination
            if (usePostProcess){
                postDestination = destination;
                ssrDestination = &postProcessFramebuffer[0].getRender();
            }else{
                ssrDestination = destination;
            }

            if (useSSR){
                colorPassViewport = Rect(0, 0, (float)ssrWidth, (float)ssrHeight);
                camera.render.startRenderPass(&sceneColorFramebuffer.getRender());
                camera.render.applyViewport(colorPassViewport);
            }else{
                colorPassViewport = Rect(0, 0, (float)postProcessWidth, (float)postProcessHeight);
                camera.render.startRenderPass(&postProcessFramebuffer[0].getRender());
                camera.render.applyViewport(colorPassViewport);
            }
        }else if (useFixedRes){
            // full offscreen target; letterbox/upscale happens in the blit pass
            colorPassViewport = Rect(0, 0, (float)fixedResWidth, (float)fixedResHeight);
            camera.render.startRenderPass(&fixedResFramebuffer.getRender());
        }else if (!camera.renderToTexture){
            if (Engine::getFramebuffer()){
                if (!Engine::getFramebuffer()->isCreated()){
                    Engine::getFramebuffer()->create();
                }
                camera.render.startRenderPass(&Engine::getFramebuffer()->getRender());
            }else{
                camera.render.startRenderPass();
            }
            colorPassViewport = Engine::getViewRect();
            camera.render.applyViewport(colorPassViewport);
        }else{
            if (!camera.framebuffer->isCreated()){
                camera.framebuffer->create();
            }
            colorPassViewport = Rect(0, 0, (float)camera.framebuffer->getWidth(), (float)camera.framebuffer->getHeight());
            camera.render.startRenderPass(&camera.framebuffer->getRender());
        }

        //---------Draw opaque meshes and UI----------
        bool hasActiveScissor = false;

        //---------Draw sky----------
        auto skys = scene->getComponentArray<SkyComponent>();
        if (skys->size() > 0){
            SkyComponent& sky = skys->getComponentFromIndex(0);
            Entity entity = skys->getEntity(0);
            if (sky.visible){
                if (hasMultipleCameras || hasReflectionProbes){
                    updateSkyViewProjection(sky, camera);
                }

                drawSky(sky, offscreenTarget, camera.invertCulling);
            }
        }

        for (int i = 0; i < transforms->size(); i++){
            Transform& transform = transforms->getComponentFromIndex(i);
            Entity entity = transforms->getEntity(i);
            Signature signature = scene->getSignature(entity);

            if (signature.test(scene->getComponentId<CameraComponent>())){
                continue;
            }

            if (hasMultipleCameras || hasReflectionProbes){
                updateMVP(i, transform, camera, cameraTransform);
            }

            // apply scissor on UI
            if (signature.test(scene->getComponentId<UILayoutComponent>())){
                UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);

                Rect parentScissor;
                bool parentScissorActive = false;

                if (transform.parent != NULL_ENTITY){
                    Signature parentSignature = scene->getSignature(transform.parent);
                    if (parentSignature.test(scene->getComponentId<UILayoutComponent>())){
                        UILayoutComponent& parentLayout = scene->getComponent<UILayoutComponent>(transform.parent);

                        parentScissorActive = parentLayout.scissorActive;
                        parentScissor = parentLayout.scissor;
                    }
                }

                // recomputed every pass, so children never get a stale rect
                layout.scissorActive = false;

                bool inheritsScissor = parentScissorActive && !layout.ignoreScissor;
                bool clippedAway = false;

                if (inheritsScissor){
                    layout.scissor = parentScissor;
                    layout.scissorActive = true;

                    if (parentScissor.getWidth() <= 0 || parentScissor.getHeight() <= 0){
                        // sokol widens a scissor to 1x1 on Metal, Vulkan and WebGPU, so an
                        // empty rect would leak a pixel: skip the draw instead
                        clippedAway = true;
                    }else{
                        camera.render.applyScissor(parentScissor);

                        hasActiveScissor = true;
                    }
                }

                if (signature.test(scene->getComponentId<ImageComponent>())){
                    ImageComponent& img = scene->getComponent<ImageComponent>(entity);

                    layout.scissor = getScissorRect(layout, img, transform, colorPassViewport);
                    // applied or not: a clipped away element still passes the empty rect down
                    if (inheritsScissor){
                        layout.scissor = intersectScissor(layout.scissor, parentScissor);
                    }
                    layout.scissorActive = true;
                }

                if (clippedAway)
                    continue;
            }

            if (signature.test(scene->getComponentId<MeshComponent>())){
                MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);

                if (transform.visible && !samplesCameraTarget(camera, mesh)){

                    // Per-camera transparent-instance sorting is intentionally NOT done
                    // here: the instance buffer is shared across all cameras and sokol
                    // only allows one sg_update_buffer per buffer per frame
                    // (VALIDATE_UPDATEBUF_ONCE). Re-sorting per camera re-flags
                    // needUpdateBuffer and uploads the buffer again, which panics the
                    // validation layer when a render-to-texture camera is present. Every
                    // camera reuses the main camera's ordering computed in update().
                    InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);

                    // Per-view CDLOD: the main camera's selection (view 0) is done in
                    // update(); each render-to-texture camera (mirror/RTT) selects its
                    // own node cut here into its own buffer, so the reflection shows
                    // complete, correctly-tessellated terrain.
                    TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
                    if (terrain && terrainView != 0){
                        updateTerrain(*terrain, transform, camera, cameraTransform, terrainView);
                    }

                    // chunk bounds are view-independent, so every camera reuses the
                    // ones refreshed in update()
                    TilemapComponent* tilemap = scene->findComponent<TilemapComponent>(entity);

                    if (!mesh.transparent || !camera.transparentSort){
                        //Draw opaque meshes if transparency is not necessary
                        drawMesh(mesh, transform, camera, cameraTransform, offscreenTarget, instmesh, terrain, tilemap, terrainView);
                    }else{
                        transparentRenders.push({TransparentRenderType::MESH, &mesh, nullptr, instmesh, terrain, tilemap, &transform, transform.distanceToCamera});
                    }
                }

            }else if (signature.test(scene->getComponentId<UIComponent>())){
                UIComponent& ui = scene->getComponent<UIComponent>(entity);

                bool isText = false;
                if (signature.test(scene->getComponentId<TextComponent>())){
                    isText = true;
                }
                if (transform.visible && !samplesCameraTarget(camera, ui.texture))
                    drawUI(ui, transform, offscreenTarget);

            }else if (signature.test(scene->getComponentId<PointsComponent>())){
                PointsComponent& points = scene->getComponent<PointsComponent>(entity);

                // Per-camera sorting is intentionally NOT done here: the points buffer is
                // shared across all cameras and sokol only allows one sg_update_buffer per
                // buffer per frame (VALIDATE_UPDATEBUF_ONCE). Every camera reuses the main
                // camera's ordering computed in update().
                if (transform.visible && !samplesCameraTarget(camera, points.texture)){
                    if (!points.transparent || !camera.transparentSort){
                        drawPoints(points, transform, camera, cameraTransform, offscreenTarget);
                    }else{
                        transparentRenders.push({TransparentRenderType::POINTS, nullptr, &points, nullptr, nullptr, nullptr, &transform, transform.distanceToCamera});
                    }
                }

            }else if (signature.test(scene->getComponentId<LinesComponent>())){
                LinesComponent& lines = scene->getComponent<LinesComponent>(entity);

                if (transform.visible)
                    drawLines(lines, transform, cameraTransform, offscreenTarget);

            }

            if (hasActiveScissor){
                // screen pixels would overflow an offscreen SSR / post-process attachment
                camera.render.applyScissor(colorPassViewport);
                hasActiveScissor = false;
            }
        }

        //---------Draw transparent renderers----------
        while (!transparentRenders.empty()){
            TransparentRenderData renderData = transparentRenders.top();

            if (renderData.type == TransparentRenderType::MESH){
                drawMesh(*renderData.mesh, *renderData.transform, camera, cameraTransform, offscreenTarget, renderData.instmesh, renderData.terrain, renderData.tilemap, terrainView);
            }else if (renderData.type == TransparentRenderType::POINTS){
                drawPoints(*renderData.points, *renderData.transform, camera, cameraTransform, offscreenTarget);
            }

            transparentRenders.pop();
        }

        camera.render.endRenderPass();

        // SSR: march the offscreen scene color and composite reflections into the
        // real destination (the swapchain or the captured framebuffer).
        if (useSSR){
            renderSSR(camera, ssrDestination);
        }

        // buffer 0 now holds the scene color (or the SSR composite)
        if (usePostProcess){
            renderPostProcess(postDestination);
        }

        // Fixed game resolution: upscale the offscreen scene color to the view
        // rect of the real destination. Layer scenes draw after this (load
        // action), so overlaid UI scenes stay at native resolution.
        if (useFixedRes){
            renderFixedResolutionBlit();
        }

    }

    //---------Missing some shaders----------
    if (ShaderPool::getMissingShaders().size() > 0){
        const std::string shaderSpecs = ShaderPool::getMissingShadersDisplayList();
        std::string shaderCliArgs = ShaderPool::getMissingShadersCliArgs();
        const std::string shaderOutputDir = System::instance().getShaderPath();
        const std::string shaderBackend = ShaderPool::getSuggestedCliBackend();

        if (shaderCliArgs.empty()) {
            shaderCliArgs = " --shader \"<shader-spec>\"";
        }

        const std::string shaderCommand = "doriax-editor shaders --out \"" + shaderOutputDir + "\" --backend " + shaderBackend + shaderCliArgs;

        Log::verbose(
            "\n"
            "-------------------\n"
            "Doriax is missing precompiled shaders in:\n"
            "%s\n"
            "\n"
            "Missing shaders:\n"
            "%s\n"
            "\n"
            "Bundle precompiled shaders for graphic backend: %s\n"
            "To generate them manually with Doriax Editor:\n"
            "\n"
            "> %s\n"
            "\n"
            "Current runtime shader format: %s\n"
            "-------------------"
            , shaderOutputDir.c_str(), shaderSpecs.c_str(), shaderBackend.c_str(), shaderCommand.c_str(), ShaderPool::getShaderLangStr().c_str());
        exit(1);
    }
}

void RenderSystem::onComponentAdded(Entity entity, ComponentId componentId) {
    if (componentId == scene->getComponentId<LightComponent>()) {
        needReloadMeshes();
    } else if (componentId == scene->getComponentId<Light2DComponent>()) {
        // shader variant change (USE_LIGHT2D)
        needReloadMeshes();
    } else if (componentId == scene->getComponentId<MirrorComponent>()) {
        // a mirror enables the inverted-culling pipeline on meshes; reload so it bakes
        needReloadMeshes();
    } else if (componentId == scene->getComponentId<ReflectionProbeComponent>()) {
        hasReflectionProbes = true;
        needReloadMeshes();
    }
}

void RenderSystem::onComponentRemoved(Entity entity, ComponentId componentId) {
    if (componentId == scene->getComponentId<LightComponent>()) {
        LightComponent& light = scene->getComponent<LightComponent>(entity);
        destroyLight(light);
        needReloadMeshes();
    } else if (componentId == scene->getComponentId<Light2DComponent>()) {
        needReloadMeshes();
    } else if (componentId == scene->getComponentId<MeshComponent>()) {
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        destroyMesh(entity, mesh);
    } else if (componentId == scene->getComponentId<UIComponent>()) {
        UIComponent& ui = scene->getComponent<UIComponent>(entity);
        destroyUI(entity, ui);
    } else if (componentId == scene->getComponentId<PointsComponent>()) {
        PointsComponent& points = scene->getComponent<PointsComponent>(entity);
        destroyPoints(entity, points);
    } else if (componentId == scene->getComponentId<LinesComponent>()) {
        LinesComponent& lines = scene->getComponent<LinesComponent>(entity);
        destroyLines(entity, lines);
    } else if (componentId == scene->getComponentId<SkyComponent>()) {
        SkyComponent& sky = scene->getComponent<SkyComponent>(entity);
        destroySky(entity, sky);
    } else if (componentId == scene->getComponentId<CameraComponent>()) {
        CameraComponent& camera = scene->getComponent<CameraComponent>(entity);
        destroyCamera(camera, true);
    } else if (componentId == scene->getComponentId<MirrorComponent>()) {
        destroyMirrorCamera(entity);

        // reload meshes so the surface drops the projective-mirror shader variant and
        // other meshes drop the now-unused inverted-culling pipeline
        needReloadMeshes();
    } else if (componentId == scene->getComponentId<ReflectionProbeComponent>()) {
        ReflectionProbeComponent& probe = scene->getComponent<ReflectionProbeComponent>(entity);
        destroyReflectionProbe(entity, probe);
        hasReflectionProbes = scene->getComponentArray<ReflectionProbeComponent>()->size() > 1;
        needReloadMeshes();
    }
}
