//
// (c) 2026 Eduardo Doria.
//

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
#include "util/Angle.h"
#include "buffer/ExternalBuffer.h"
#include "math/AABB.h"
#include <memory>
#include <cmath>

using namespace doriax;

uint32_t RenderSystem::pixelsWhite[64];
uint32_t RenderSystem::pixelsBlack[64];
uint32_t RenderSystem::pixelsNormal[64];

TextureRender RenderSystem::emptyWhite;
TextureRender RenderSystem::emptyBlack;
TextureRender RenderSystem::emptyCubeBlack;
TextureRender RenderSystem::emptyNormal;

bool RenderSystem::emptyTexturesCreated = false;

RenderSystem::RenderSystem(Scene* scene): SubSystem(scene){
    signature.set(scene->getComponentId<Transform>());

    this->scene = scene;
}

RenderSystem::~RenderSystem(){
}

void RenderSystem::load(){
    hasLights = false;
    hasShadows = false;
    hasFog = false;
    hasMultipleCameras = false;

    createEmptyTextures();
        
    update(0); // first update
}

void RenderSystem::destroy(){
    // cannot destroy static textures because it affects other scenes
    //emptyWhite.destroyTexture();
    //emptyBlack.destroyTexture();
    //emptyCubeBlack.destroyTexture();
    //emptyNormal.destroyTexture();

    emptyTexturesCreated = false;

    auto skys = scene->getComponentArray<SkyComponent>();
    if (skys->size() > 0){
        SkyComponent& sky = skys->getComponentFromIndex(0);
        Entity entity = skys->getEntity(0);
        if (sky.loaded){
            destroySky(entity, sky);
        }
    }

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

    return numLights;
}

bool RenderSystem::loadLights(int numLights){
    int freeShadowMap = 0;
    int freeShadowCubeMap = MAX_SHADOWSMAP;

    auto lights = scene->getComponentArray<LightComponent>();
    
    for (int i = 0; i < numLights; i++){
        LightComponent& light = lights->getComponentFromIndex(i);

        light.shadowMapIndex = -1;
        
        if (light.shadows){
            if (light.numShadowCascades > MAX_SHADOWCASCADES){
                light.numShadowCascades = MAX_SHADOWCASCADES;
                Log::warn("Shadow cascades number is bigger than max value");
            }

            if (light.needUpdateShadowMap){
                for (int i = 0; i < light.numShadowCascades; i++) {
                    light.framebuffer[i].destroyFramebuffer();
                }

                light.needUpdateShadowMap = false;
            }

            if (light.type == LightType::POINT){
                if (!light.framebuffer[0].isCreated())
                    light.framebuffer[0].createFramebuffer(
                            TextureType::TEXTURE_CUBE, light.mapResolution, light.mapResolution, 
                            TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::CLAMP_TO_BORDER, TextureWrap::CLAMP_TO_BORDER, true);

                if ((freeShadowCubeMap - MAX_SHADOWSMAP) < MAX_SHADOWSCUBEMAP){
                    light.shadowMapIndex = freeShadowCubeMap++;
                }
            }else if (light.type == LightType::SPOT){
                if (!light.framebuffer[0].isCreated())
                    light.framebuffer[0].createFramebuffer(
                            TextureType::TEXTURE_2D, light.mapResolution, light.mapResolution, 
                            TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::CLAMP_TO_BORDER, TextureWrap::CLAMP_TO_BORDER, true);

                if (freeShadowMap < MAX_SHADOWSMAP){
                    light.shadowMapIndex = freeShadowMap++;
                }
            }else if (light.type == LightType::DIRECTIONAL){
                for (int c = 0; c < light.numShadowCascades; c++){
                    if (!light.framebuffer[c].isCreated())
                        light.framebuffer[c].createFramebuffer(
                                TextureType::TEXTURE_2D, light.mapResolution, light.mapResolution, 
                                TextureFilter::NEAREST, TextureFilter::NEAREST, TextureWrap::CLAMP_TO_BORDER, TextureWrap::CLAMP_TO_BORDER, true);
                }

                if ((freeShadowMap + light.numShadowCascades - 1) < MAX_SHADOWSMAP){
                    light.shadowMapIndex = freeShadowMap;
                    freeShadowMap += light.numShadowCascades;
                }
            }

        }

    }

    return true;
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
                size_t numMaps = (light.type == LightType::DIRECTIONAL) ? light.numShadowCascades : 1;
                for (int c = 0; c < numMaps; c++){
                    if (light.type != LightType::POINT){
                        vs_shadows.lightViewProjectionMatrix[light.shadowMapIndex+c] = light.cameras[c].lightViewProjectionMatrix;

                        // Compute world-space normal bias: normalBias (in texels) * texelSize (world units)
                        float projScale = std::max(std::abs(light.cameras[c].lightProjectionMatrix[0][0]),
                                                   std::abs(light.cameras[c].lightProjectionMatrix[1][1]));
                        float texelSizeWorld = (projScale > 0.0f) ? 2.0f / (projScale * light.mapResolution) : 0.0f;
                        vs_shadows.shadowParams[light.shadowMapIndex+c] = Vector4(light.shadowNormalBias * texelSizeWorld, 0.0, 0.0, 0.0);
                    }
                    fs_shadows.bias_texSize_nearFar[light.shadowMapIndex+c] = Vector4(light.shadowBias, light.mapResolution, light.cameras[c].nearFar.x, light.cameras[c].nearFar.y);
                }
            }else{
                light.shadows = false;
                Log::warn("There are no shadow maps available for all lights, some light shadow will be disabled");
            }
        }

        fs_lighting.direction_range[i] = Vector4(light.worldDirection.x, light.worldDirection.y, light.worldDirection.z, light.range);
        fs_lighting.color_intensity[i] = Vector4(light.color.x, light.color.y, light.color.z, light.intensity);
        fs_lighting.position_type[i] = Vector4(worldPosition.x, worldPosition.y, worldPosition.z, (float)type);
        fs_lighting.inCon_ouCon_shadows_cascades[i] = Vector4(light.innerConeCos, light.outerConeCos, light.shadowMapIndex, light.numShadowCascades);
    }

    fs_lighting.eyePos = Vector4(cameraTransform.worldPosition.x, cameraTransform.worldPosition.y, cameraTransform.worldPosition.z, 0.0);
    fs_lighting.cameraDir = Vector4(camera.worldDirection.x, camera.worldDirection.y, camera.worldDirection.z, 0.0);
    fs_lighting.globalIllum = Vector4(scene->getGlobalIlluminationColorLinear(), scene->getGlobalIlluminationIntensity());

    // Setting intensity of other lights to zero
    for (int i = numLights; i < MAX_LIGHTS; i++){
        fs_lighting.color_intensity[i].w = 0.0;
    }
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

TextureShaderType RenderSystem::getShadowMapByIndex(int index){
    if (index == 0){
        return TextureShaderType::SHADOWMAP1;
    }else if (index == 1){
        return TextureShaderType::SHADOWMAP2;
    }else if (index == 2){
        return TextureShaderType::SHADOWMAP3;
    }else if (index == 3){
        return TextureShaderType::SHADOWMAP4;
    }else if (index == 4){
        return TextureShaderType::SHADOWMAP5;
    }else if (index == 5){
        return TextureShaderType::SHADOWMAP6;
    }else if (index == 6){
        return TextureShaderType::SHADOWMAP7;
    }else if (index == 7){
        return TextureShaderType::SHADOWMAP8;
    }

    return TextureShaderType::SHADOWMAP1;
}

TextureShaderType RenderSystem::getShadowMapCubeByIndex(int index){
    index -= MAX_SHADOWSMAP;
    if (index == 0){
        return TextureShaderType::SHADOWCUBEMAP1;
    }

    return TextureShaderType::SHADOWCUBEMAP1;
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

        textureRender = material.emissiveTexture.getRender(&emptyBlack);
        slotTex = shaderData.getTextureIndex(TextureShaderType::EMISSIVE);
        if (textureRender){
            if (!textureRender->isCreated()){
                return false;
            }
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, textureRender);
        }else{
            render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyBlack);
        }
    }

    return true;
}

void RenderSystem::loadShadowTextures(ShaderData& shaderData, ObjectRender& render, bool receiveLights, bool receiveShadows){
    std::pair<int, int> slotTex(-1, -1);

    if (hasLights && receiveLights && hasShadows && receiveShadows){
        size_t num2DShadows = 0;
        size_t numCubeShadows = 0;
        auto lights = scene->getComponentArray<LightComponent>();
        for (int l = 0; l < lights->size(); l++){
            LightComponent& light = lights->getComponentFromIndex(l);
            if (light.shadowMapIndex >= 0){
                if (light.type == LightType::POINT){
                    slotTex = shaderData.getTextureIndex(getShadowMapCubeByIndex(light.shadowMapIndex));
                    render.addTexture(slotTex, ShaderStageType::FRAGMENT, &light.framebuffer[0].getColorTexture());
                    numCubeShadows++;
                }else if (light.type == LightType::SPOT){
                    slotTex = shaderData.getTextureIndex(getShadowMapByIndex(light.shadowMapIndex));
                    render.addTexture(slotTex, ShaderStageType::FRAGMENT, &light.framebuffer[0].getColorTexture());
                    num2DShadows++;
                }else if (light.type == LightType::DIRECTIONAL){
                    for (int c = 0; c < light.numShadowCascades; c++){
                        slotTex = shaderData.getTextureIndex(getShadowMapByIndex(light.shadowMapIndex+c));
                        render.addTexture(slotTex, ShaderStageType::FRAGMENT, &light.framebuffer[c].getColorTexture());
                        num2DShadows++;
                    }
                }
            }
        }
        if (MAX_SHADOWSMAP > num2DShadows){
            for (int s = num2DShadows; s < MAX_SHADOWSMAP; s++){
                slotTex = shaderData.getTextureIndex(getShadowMapByIndex(s));
                render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyBlack);
            }
        }
        if (MAX_SHADOWSCUBEMAP > numCubeShadows){
            for (int s = numCubeShadows; s < MAX_SHADOWSCUBEMAP; s++){
                slotTex = shaderData.getTextureIndex(getShadowMapCubeByIndex(s+MAX_SHADOWSMAP));
                render.addTexture(slotTex, ShaderStageType::FRAGMENT, &emptyCubeBlack);
            }
        }
    }
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

bool RenderSystem::loadTerrainTextures(TerrainComponent& terrain, ObjectRender& render, ShaderData& shaderData){
    TextureRender* textureRender = NULL;
    std::pair<int, int> slotTex(-1, -1);

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

bool RenderSystem::loadMesh(Entity entity, MeshComponent& mesh, uint8_t pipelines, InstancedMeshComponent* instmesh, TerrainComponent* terrain){

    if (!Engine::isViewLoaded()) 
        return false;

    if (terrain && !terrain->heightMapLoaded)
        return false;

    if (terrain && instmesh){
        Log::warn("Terrain cannot be an InstancedMesh");
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

    if (allBuffersEmpty)
        return false;

    std::map<std::string, unsigned int> bufferStride;

    for (auto const& buf : buffers){
        buf.second->getRender()->createBuffer(buf.second->getSize(), buf.second->getData(), buf.second->getType(), buf.second->getUsage());
        bufferNameToRender[buf.first] = buf.second->getRender();
        bufferStride[buf.first] = buf.second->getStride();
    }

    if (instmesh){
        // Now buffer size is zero than it needed to be calculated
        size_t bufferSize = instmesh->maxInstances * instmesh->buffer.getStride();
        instmesh->buffer.getRender()->createBuffer(bufferSize, instmesh->buffer.getData(), instmesh->buffer.getType(), instmesh->buffer.getUsage());

        instmesh->needUpdateBuffer = true;
    }

    if (terrain){
        for (int s = 0; s < 2; s++){
            size_t bufferSize = pow(terrain->rootGridSize, 2) * pow(terrain->levels, 2) * terrain->nodesbuffer[s].getStride() / 2;
            terrain->nodesbuffer[s].getRender()->createBuffer(bufferSize, terrain->nodesbuffer[s].getData(), terrain->nodesbuffer[s].getType(), terrain->nodesbuffer[s].getUsage());
        }

        // using same material in both terrain submeshes
        mesh.submeshes[1].material = mesh.submeshes[0].material;
    }

    for (int i = 0; i < mesh.numSubmeshes; i++){

        ObjectRender& render = mesh.submeshes[i].render;

        mesh.submeshes[i].hasNormalMap = false;
        mesh.submeshes[i].hasTangent = false;
        mesh.submeshes[i].hasVertexColor4 = false;
        mesh.submeshes[i].hasSkinning = false;
        mesh.submeshes[i].hasMorphTarget = false;
        mesh.submeshes[i].hasMorphNormal = false;

        render.beginLoad(mesh.submeshes[i].primitiveType);

        bool hasPBRTextures = checkPBRTextures(mesh.submeshes[i].material, mesh.receiveLights);

        for (auto const& buf : buffers){
            if (buf.second->isRenderAttributes()) {
                for (auto const &attr : buf.second->getAttributes()) {
                    if (attr.first == AttributeType::TEXCOORD1){
                        mesh.submeshes[i].hasTexCoord1 = true;
                    }
                    if (attr.first == AttributeType::TANGENT){
                        mesh.submeshes[i].hasTangent = true;
                    }
                    if (attr.first == AttributeType::COLOR){
                        mesh.submeshes[i].hasVertexColor4 = true;
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
            if (attr.first == AttributeType::TANGENT){
                mesh.submeshes[i].hasTangent = true;
            }
            if (attr.first == AttributeType::COLOR){
                mesh.submeshes[i].hasVertexColor4 = true;
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
        bool p_hasTexture1 = false;
        bool p_hasNormalMap = false;
        bool p_hasNormal = false;
        bool p_hasTangent = false;
        bool p_receiveShadows = false;
        bool p_shadowsPCF = false;

        if (mesh.submeshes[i].hasTexCoord1 && hasPBRTextures){
            p_hasTexture1 = true;
        }
        if (terrain && (!terrain->blendMap.empty() || hasPBRTextures)){
            p_hasTexture1 = true;
        }
        if (hasLights && mesh.receiveLights){
            p_punctual = true;

            p_hasNormal = true;
            if (mesh.submeshes[i].hasTangent){
                p_hasTangent = true;
            }
            if (mesh.submeshes[i].hasNormalMap){
                p_hasNormalMap = true;
            }
            if (hasShadows && mesh.receiveShadows){
                p_receiveShadows = true;
                if (scene->isShadowsPCF()){
                    p_shadowsPCF = true;
                }
            }
        }else{
            p_unlit = true;
        }

        mesh.submeshes[i].shaderProperties = ShaderPool::getMeshProperties(
                        p_unlit, p_hasTexture1, false, p_punctual, 
                        p_receiveShadows, p_shadowsPCF, p_hasNormal, p_hasNormalMap, 
                        p_hasTangent, false, mesh.submeshes[i].hasVertexColor4, mesh.submeshes[i].hasTextureRect, 
                        hasFog, mesh.submeshes[i].hasSkinning, mesh.submeshes[i].hasMorphTarget, mesh.submeshes[i].hasMorphNormal, mesh.submeshes[i].hasMorphTangent,
                        (terrain)?true:false, (instmesh)?true:false);
        mesh.submeshes[i].shader = ShaderPool::get(ShaderType::MESH, mesh.submeshes[i].shaderProperties);
        if (hasShadows && mesh.castShadows){
            mesh.submeshes[i].depthShaderProperties = ShaderPool::getDepthMeshProperties(
                mesh.submeshes[i].textureShadow, mesh.submeshes[i].hasSkinning, mesh.submeshes[i].hasMorphTarget, 
                mesh.submeshes[i].hasMorphNormal, mesh.submeshes[i].hasMorphTangent, false, (instmesh)?true:false);
            mesh.submeshes[i].depthShader = ShaderPool::get(ShaderType::DEPTH, mesh.submeshes[i].depthShaderProperties);
            if (!mesh.submeshes[i].depthShader->isCreated())
                return false;
        }
        if (!mesh.submeshes[i].shader->isCreated())
            return false;
        render.setShader(mesh.submeshes[i].shader.get());
        ShaderData& shaderData = mesh.submeshes[i].shader.get()->shaderData;

        mesh.submeshes[i].slotVSParams = shaderData.getUniformBlockIndex(UniformBlockType::PBR_VS_PARAMS);
        mesh.submeshes[i].slotFSParams = shaderData.getUniformBlockIndex(UniformBlockType::PBR_FS_PARAMS);
        if (hasFog){
            mesh.submeshes[i].slotFSFog = shaderData.getUniformBlockIndex(UniformBlockType::FS_FOG);
        }
        if (hasLights && mesh.receiveLights){
            mesh.submeshes[i].slotFSLighting = shaderData.getUniformBlockIndex(UniformBlockType::FS_LIGHTING);
            if (hasShadows && mesh.receiveShadows){
                mesh.submeshes[i].slotVSShadows = shaderData.getUniformBlockIndex(UniformBlockType::VS_SHADOWS);
                mesh.submeshes[i].slotFSShadows = shaderData.getUniformBlockIndex(UniformBlockType::FS_SHADOWS);
            }
        }
        if (mesh.submeshes[i].hasTextureRect){
            mesh.submeshes[i].slotVSSprite = shaderData.getUniformBlockIndex(UniformBlockType::SPRITE_VS_PARAMS);
        }
        if (mesh.submeshes[i].hasSkinning){
            mesh.submeshes[i].slotVSSkinning = shaderData.getUniformBlockIndex(UniformBlockType::VS_SKINNING);
        }
        if (mesh.submeshes[i].hasMorphTarget){
            mesh.submeshes[i].slotVSMorphTarget = shaderData.getUniformBlockIndex(UniformBlockType::VS_MORPHTARGET);
        }

        if (!loadPBRTextures(mesh.submeshes[i].material, shaderData, mesh.submeshes[i].render, mesh.receiveLights)){
            return false;
        }
        loadShadowTextures(shaderData, mesh.submeshes[i].render, mesh.receiveLights, mesh.receiveShadows);

        if (terrain){
            mesh.submeshes[i].slotVSTerrain = shaderData.getUniformBlockIndex(UniformBlockType::TERRAIN_VS_PARAMS);

            if (!loadTerrainTextures(*terrain, mesh.submeshes[i].render, shaderData)){
                return false;
            }

            terrain->needUpdateTexture = false;

            for (auto const &attr : terrain->nodesbuffer[i].getAttributes()) {
                render.addAttribute(shaderData.getAttrIndex(attr.first), terrain->nodesbuffer[i].getRender(), attr.second.getElements(), attr.second.getDataType(), terrain->nodesbuffer[i].getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
            }
        }

        mesh.submeshes[i].needUpdateTexture = false;
        mesh.submeshes[i].needUpdateDepthTexture = false;

        if (mesh.autoTransparency && !mesh.transparent){
            if (mesh.submeshes[i].material.baseColorTexture.isTransparent() || mesh.submeshes[i].material.baseColorFactor.w != 1.0){
                mesh.transparent = true;
            }
        }
    
        unsigned int indexCount = 0;

        for (auto const& buf : buffers){
            if (buf.second->isRenderAttributes()) {
                if (buf.second->getType() == BufferType::INDEX_BUFFER){
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

        if (indexCount > 0){
            mesh.submeshes[i].vertexCount = indexCount;
        }else{
            mesh.submeshes[i].vertexCount = mesh.vertexCount;
        }

        if (!render.endLoad(pipelines, mesh.submeshes[i].faceCulling, mesh.cullingMode, mesh.windingOrder)){
            return false;
        }

        //----------Start depth shader---------------
        if (hasShadows && mesh.castShadows){
            ObjectRender& depthRender = mesh.submeshes[i].depthRender;

            depthRender.beginLoad(mesh.submeshes[i].primitiveType);

            depthRender.setShader(mesh.submeshes[i].depthShader.get());
            ShaderData& depthShaderData = mesh.submeshes[i].depthShader.get()->shaderData;

            mesh.submeshes[i].slotVSDepthParams = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_PARAMS);

            if (mesh.submeshes[i].hasSkinning){
                mesh.submeshes[i].slotVSDepthSkinning = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_SKINNING);
            }
            if (mesh.submeshes[i].hasMorphTarget){
                mesh.submeshes[i].slotVSDepthMorphTarget = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_VS_MORPHTARGET);
            }

            if (mesh.submeshes[i].textureShadow){
                if (!loadDepthTexture(mesh.submeshes[i].material, depthShaderData, depthRender)){
                    return false;
                }
            }

            if (terrain){
                mesh.submeshes[i].slotVSDepthTerrain = depthShaderData.getUniformBlockIndex(UniformBlockType::DEPTH_TERRAIN_VS_PARAMS);

                for (auto const &attr : terrain->nodesbuffer[i].getAttributes()) {
                    depthRender.addAttribute(depthShaderData.getAttrIndex(attr.first), terrain->nodesbuffer[i].getRender(), attr.second.getElements(), attr.second.getDataType(), terrain->nodesbuffer[i].getStride(), attr.second.getOffset(), attr.second.getNormalized(), attr.second.getPerInstance());
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
                                    attr.first == AttributeType::MORPHTARGET0 || attr.first == AttributeType::MORPHTARGET4 ||
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
                    }else if (attr.first == AttributeType::POSITION){
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
                            attr.first == AttributeType::MORPHTARGET0 || attr.first == AttributeType::MORPHTARGET4 ||
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

            if (!depthRender.endLoad(PIP_DEPTH, depthFaceCulling, depthCullingMode, mesh.windingOrder)){
                return false;
            }
        }
        //----------End depth shader---------------
    }

    mesh.needReload = false;
    mesh.needUpdateAABB = true;
    mesh.loadCalled = true;
    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

bool RenderSystem::drawMesh(MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform, bool renderToTexture, InstancedMeshComponent* instmesh, TerrainComponent* terrain){
    if (mesh.loaded){

        if (mesh.worldAABB != AABB::ZERO && !isInsideCamera(camera, mesh.worldAABB)) {
            return false;
        }

        if (mesh.needUpdateBuffer){
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
        unsigned int instanceCount = 1;
        if (instmesh){
            instanceCount = instmesh->numVisible;

            if (instmesh->needUpdateBuffer){
                // setData here because component can change order and lose reference
                if (instmesh->numVisible > 0 && !instmesh->renderInstances.empty()){
                    instmesh->buffer.setData((unsigned char*)(&instmesh->renderInstances[0]), sizeof(InstanceRenderData)*instmesh->numVisible);
                    instmesh->buffer.getRender()->updateBuffer(instmesh->buffer.getSize(), instmesh->buffer.getData());
                }

                instmesh->needUpdateBuffer = false;
            }
        }

        if (terrain && terrain->needUpdateNodesBuffer){
            for (int s = 0; s < 2; s++){
                terrain->nodesbuffer[s].getRender()->updateBuffer(terrain->nodesbuffer[s].getSize(), terrain->nodesbuffer[s].getData());
            }

            terrain->needUpdateNodesBuffer = false;
        }

        if (terrain && terrain->needUpdateTexture){
            bool texLoaded = true;
            for (int s = 0; s < 2; s++){
                ShaderData& shaderData = mesh.submeshes[s].shader.get()->shaderData;
                if (!loadTerrainTextures(*terrain, mesh.submeshes[s].render, shaderData)){
                    texLoaded = false;
                }
            }

            if (texLoaded){
                terrain->needUpdateTexture = false;
            }
        }

        for (int i = 0; i < mesh.numSubmeshes; i++){
            ObjectRender& render = mesh.submeshes[i].render;

            if (terrain){
                instanceCount = terrain->nodesbuffer[i].getCount();
            }

            bool needUpdateFramebuffer = checkPBRFrabebufferUpdate(mesh.submeshes[i].material);

            if (mesh.submeshes[i].needUpdateTexture || needUpdateFramebuffer){
                ShaderData& shaderData = mesh.submeshes[i].shader.get()->shaderData;
                if (loadPBRTextures(mesh.submeshes[i].material, shaderData, mesh.submeshes[i].render, mesh.receiveLights)){
                    mesh.submeshes[i].needUpdateTexture = false;
                }
            }

            if (!render.beginDraw((renderToTexture)?PIP_RTT:PIP_DEFAULT)){
                mesh.needReload = true;
                return false;
            }

            if (hasFog){
                render.applyUniformBlock(mesh.submeshes[i].slotFSFog, sizeof(float) * 8, &fs_fog);
            }

            if (hasLights && mesh.receiveLights){
                render.applyUniformBlock(mesh.submeshes[i].slotFSLighting, sizeof(float) * (16 * MAX_LIGHTS + 12), &fs_lighting);
                if (hasShadows && mesh.receiveShadows){
                    render.applyUniformBlock(mesh.submeshes[i].slotVSShadows, sizeof(float) * (20 * MAX_SHADOWSMAP), &vs_shadows);
                    render.applyUniformBlock(mesh.submeshes[i].slotFSShadows, sizeof(float) * (4 * (MAX_SHADOWSMAP + MAX_SHADOWSCUBEMAP)), &fs_shadows);
                }
            }

            if (mesh.submeshes[i].hasTextureRect){
                render.applyUniformBlock(mesh.submeshes[i].slotVSSprite, sizeof(float) * 4, &mesh.submeshes[i].textureRect);
            }

            if (mesh.submeshes[i].hasSkinning){
                render.applyUniformBlock(mesh.submeshes[i].slotVSSkinning, sizeof(float) * 16 * MAX_BONES + (sizeof(float) * 4), &mesh.bonesMatrix);
            }

            if (mesh.submeshes[i].hasMorphTarget){
                if (!mesh.submeshes[i].hasMorphNormal && !mesh.submeshes[i].hasMorphTangent){
                    render.applyUniformBlock(mesh.submeshes[i].slotVSMorphTarget, sizeof(float) * MAX_MORPHTARGETS, &mesh.morphWeights);
                }else{
                    render.applyUniformBlock(mesh.submeshes[i].slotVSMorphTarget, sizeof(float) * MAX_MORPHTARGETS / 2, &mesh.morphWeights);
                }
            }

            if (hasLights && mesh.receiveLights){
                render.applyUniformBlock(mesh.submeshes[i].slotFSParams, sizeof(float) * 12, &mesh.submeshes[i].material);
            }else{
                render.applyUniformBlock(mesh.submeshes[i].slotFSParams, sizeof(float) * 4, &mesh.submeshes[i].material);
            }

            if (terrain){
                render.applyUniformBlock(mesh.submeshes[i].slotVSTerrain, sizeof(float) * 8, &(terrain->eyePos));
            }

            //model, normal and mvp matrix
            render.applyUniformBlock(mesh.submeshes[i].slotVSParams, sizeof(float) * 48, &transform.modelMatrix);

            render.draw(mesh.submeshes[i].vertexCount, instanceCount);
        }
    }

    return true;
}

bool RenderSystem::drawMeshDepth(MeshComponent& mesh, const float cameraFar, const Plane frustumPlanes[6], vs_depth_t vsDepthParams, InstancedMeshComponent* instmesh, TerrainComponent* terrain){
    if (mesh.loaded && mesh.castShadows){

        if (mesh.worldAABB != AABB::ZERO && !isInsideCamera(cameraFar, frustumPlanes, mesh.worldAABB)) {
            return false;
        }

        for (int i = 0; i < mesh.numSubmeshes; i++){
            ObjectRender& depthRender = mesh.submeshes[i].depthRender;

            if (mesh.submeshes[i].needUpdateDepthTexture){
                ShaderData& depthShaderData = mesh.submeshes[i].depthShader.get()->shaderData;
                if (loadDepthTexture(mesh.submeshes[i].material, depthShaderData, mesh.submeshes[i].depthRender)){
                    mesh.submeshes[i].needUpdateDepthTexture = false;
                }
            }

            if (!depthRender.beginDraw(PIP_DEPTH)){
                mesh.needReload = true;
                return false;
            }

            unsigned int instanceCount = 1;
            if (instmesh){
                instanceCount = instmesh->numVisible;
            }

            //model, mvp matrix
            depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthParams, sizeof(float) * 32, &vsDepthParams);

            if (mesh.submeshes[i].hasSkinning){
                depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthSkinning, sizeof(float) * 16 * MAX_BONES + (sizeof(float) * 4), &mesh.bonesMatrix);
            }
            if (mesh.submeshes[i].hasMorphTarget){
                if (!mesh.submeshes[i].hasMorphNormal && !mesh.submeshes[i].hasMorphTangent){
                    depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthMorphTarget, sizeof(float) * MAX_MORPHTARGETS, &mesh.morphWeights);
                }else{
                    depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthMorphTarget, sizeof(float) * MAX_MORPHTARGETS / 2, &mesh.morphWeights);
                }
            }

            if (terrain){
                depthRender.applyUniformBlock(mesh.submeshes[i].slotVSDepthTerrain, sizeof(float) * 8, &(terrain->eyePos));
            }

            depthRender.draw(mesh.submeshes[i].vertexCount, instanceCount);
        }
    }

    return true;
}

void RenderSystem::destroyMesh(Entity entity, MeshComponent& mesh){
    InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
    TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);

    if (!mesh.loaded)
        return;

    for (int i = 0; i < mesh.numSubmeshes; i++){

        Submesh& submesh = mesh.submeshes[i];
        if (!mesh.needReload){
            //Destroy shader
            if (submesh.shader){
                submesh.shader.reset();
                ShaderPool::remove(ShaderType::MESH, submesh.shaderProperties);
            }
            if (hasShadows && mesh.castShadows){
                if (submesh.depthShader){
                    submesh.depthShader.reset();
                    ShaderPool::remove(ShaderType::DEPTH, submesh.depthShaderProperties);
                }
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
            if (!mesh.needReload){
                terrain->heightMap.destroy();
                terrain->blendMap.destroy();
                terrain->textureDetailRed.destroy();
                terrain->textureDetailGreen.destroy();
                terrain->textureDetailBlue.destroy();
            }

            //Destroy terrain buffer
            for (int s = 0; s < 2; s++){
                terrain->nodesbuffer[s].getRender()->destroyBuffer();
            }
        }

        if (instmesh){
            //Destroy instmesh buffer
            if (!mesh.needReload){
                instmesh->buffer.clearAll();
            }
            instmesh->buffer.getRender()->destroyBuffer();
        }

        //Destroy render
        submesh.render.destroy();
        submesh.depthRender.destroy();

        //Shaders uniforms
        submesh.slotVSParams = -1;
        submesh.slotFSParams = -1;
        submesh.slotFSLighting = -1;
        submesh.slotVSSprite = -1;
        submesh.slotVSShadows = -1;
        submesh.slotFSShadows = -1;
        submesh.slotVSSkinning = -1;
        submesh.slotVSMorphTarget = -1;
        submesh.slotVSTerrain = -1;

        submesh.slotVSDepthParams = -1;
        submesh.slotVSDepthSkinning = -1;
        submesh.slotVSDepthMorphTarget = -1;
        submesh.slotVSDepthTerrain = -1;
    }

    //Destroy buffer
    if (!mesh.needReload){
        mesh.buffer.clearAll();
        mesh.indices.clearAll();
    }
    mesh.buffer.getRender()->destroyBuffer();
    mesh.indices.getRender()->destroyBuffer();
    for (int i = 0; i < mesh.numExternalBuffers; i++){
        if (!mesh.needReload){
            mesh.eBuffers[i].clearAll();
        }
        mesh.eBuffers[i].getRender()->destroyBuffer();
    }

    SystemRender::addQueueCommand(&changeDestroy, new check_load_t{scene, entity});
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
    ui.shader = ShaderPool::get(ShaderType::UI, ui.shaderProperties);
    if (!ui.shader->isCreated())
        return false;
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

    if (!render.endLoad(pipelines, false, CullingMode::BACK, WindingOrder::CCW)){
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
        render.draw(ui.vertexCount, 1);

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
            ShaderPool::remove(ShaderType::UI, ui.shaderProperties);
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
    points.shader = ShaderPool::get(ShaderType::POINTS, points.shaderProperties);
    if (!points.shader->isCreated())
        return false;
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

    if (!render.endLoad(pipelines, false, CullingMode::BACK, WindingOrder::CCW)){
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
    lines.shader = ShaderPool::get(ShaderType::LINES, lines.shaderProperties);
    if (!lines.shader->isCreated())
        return false;
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

    if (!render.endLoad(pipelines, false, CullingMode::BACK, WindingOrder::CCW)){
        return false;
    }

    lines.needReload = false;
    lines.loadCalled = true;
    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

bool RenderSystem::drawPoints(PointsComponent& points, Transform& transform, Transform& camTransform, bool renderToTexture){
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
        render.applyUniformBlock(points.slotVSParams, sizeof(float) * 16, &transform.modelViewProjectionMatrix);
        render.draw(points.numVisible, 1);
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
            ShaderPool::remove(ShaderType::POINTS, points.shaderProperties);
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
        render.draw(lines.lines.size() * 2, 1);
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
            ShaderPool::remove(ShaderType::LINES, lines.shaderProperties);
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

    sky.shader = ShaderPool::get(ShaderType::SKYBOX, 0);
    if (!sky.shader->isCreated())
        return false;
    render->setShader(sky.shader.get());
    ShaderData& shaderData = sky.shader.get()->shaderData;

    sky.slotVSParams = shaderData.getUniformBlockIndex(UniformBlockType::SKY_VS_PARAMS);
    sky.slotFSParams = shaderData.getUniformBlockIndex(UniformBlockType::SKY_FS_PARAMS);

    if (TextureRender* textureRender = sky.texture.getRender(&emptyWhite)){
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

    if (!render->endLoad(pipelines, true, CullingMode::BACK, WindingOrder::CCW)){
        return false;
    }

    sky.loadCalled = true;
    SystemRender::addQueueCommand(&changeLoaded, new check_load_t{scene, entity});

    return true;
}

bool RenderSystem::drawSky(SkyComponent& sky, bool renderToTexture){
    if (sky.loaded){

        if (sky.needUpdateTexture || sky.texture.isFramebufferOutdated()){
            ShaderData& shaderData = sky.shader.get()->shaderData;
            if (TextureRender* textureRender = sky.texture.getRender(&emptyWhite)){
                if (textureRender->isCreated()){
                    sky.render.addTexture(shaderData.getTextureIndex(TextureShaderType::SKYCUBE), ShaderStageType::FRAGMENT, textureRender);
                    sky.needUpdateTexture = false;
                }
            }
        }

        ObjectRender& render = sky.render;

        if (!render.beginDraw((renderToTexture)?PIP_RTT:PIP_DEFAULT)){
            sky.needReload = true;
            return false;
        }
        render.applyUniformBlock(sky.slotVSParams, sizeof(float) * 16, &sky.skyViewProjectionMatrix);
        render.applyUniformBlock(sky.slotFSParams, sizeof(float) * 4, &sky.color);
        render.draw(36, 1);
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
            ShaderPool::remove(ShaderType::SKYBOX, 0);
        }

        //Destroy texture
        sky.texture.destroy();
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
    for (int i = 0; i < MAX_SHADOWCASCADES; i++) {
        light.framebuffer[i].destroyFramebuffer();
    }
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

Rect RenderSystem::getScissorRect(UILayoutComponent& layout, ImageComponent& img, Transform& transform, CameraComponent& camera){
    float objScreenPosX = 0;
    float objScreenPosY = 0;
    float objScreenWidth = 0;
    float objScreenHeight = 0;

    if (!camera.renderToTexture) {

        float scaleX = transform.worldScale.x;
        float scaleY = transform.worldScale.y;

        float tempX = (2 * transform.worldPosition.x / (float) Engine::getCanvasWidth()) - 1;
        float tempY = (2 * transform.worldPosition.y / (float) Engine::getCanvasHeight()) - 1;

        float camScaleX = Engine::getCanvasWidth() / (camera.rightClip - camera.leftClip);
        float camScaleY = Engine::getCanvasHeight() / (camera.topClip - camera.bottomClip);

        float camOffsetX = -(camera.worldTarget.x + camera.leftClip);
        float camOffsetY = -(camera.worldTarget.y + camera.bottomClip);

        float widthRatio = scaleX * (Engine::getViewRect().getWidth() / (float) Engine::getCanvasWidth());
        float heightRatio = scaleY * (Engine::getViewRect().getHeight() / (float) Engine::getCanvasHeight());

        objScreenPosX = (camOffsetX + (tempX * Engine::getViewRect().getWidth() + (float) System::instance().getScreenWidth()) / 2)  * camScaleX;
        objScreenPosY = (camOffsetY + (tempY * Engine::getViewRect().getHeight() + (float) System::instance().getScreenHeight()) / 2) * camScaleY;
        objScreenWidth = layout.width * widthRatio * camScaleX;
        objScreenHeight = layout.height * heightRatio * camScaleY;

        if (camera.type == CameraType::CAMERA_UI)
            objScreenPosY = (float) System::instance().getScreenHeight() - objScreenHeight - objScreenPosY;

        if (!(img.patchMarginLeft == 0 && img.patchMarginTop == 0 && img.patchMarginRight == 0 && img.patchMarginBottom == 0)) {
            float borderScreenLeft = img.patchMarginLeft * widthRatio;
            float borderScreenTop = img.patchMarginTop * heightRatio;
            float borderScreenRight = img.patchMarginRight * widthRatio;
            float borderScreenBottom = img.patchMarginBottom * heightRatio;

            objScreenPosX += borderScreenLeft;
            objScreenPosY += borderScreenBottom; // scissor is bottom-left
            objScreenWidth -= (borderScreenLeft + borderScreenRight);
            objScreenHeight -= (borderScreenTop + borderScreenBottom);
        }

    }else {

        objScreenPosX = transform.worldPosition.x;
        objScreenPosY = transform.worldPosition.y;
        objScreenWidth = layout.width;
        objScreenHeight = layout.height;

        if (camera.type == CameraType::CAMERA_UI)
            objScreenPosY = (float) camera.framebuffer->getHeight() - objScreenHeight - objScreenPosY;

        if (!(img.patchMarginLeft == 0 && img.patchMarginTop == 0 && img.patchMarginRight == 0 && img.patchMarginBottom == 0)) {
            float borderScreenLeft = img.patchMarginLeft;
            float borderScreenTop = img.patchMarginTop;
            float borderScreenRight = img.patchMarginRight;
            float borderScreenBottom = img.patchMarginBottom;

            objScreenPosX += borderScreenLeft;
            objScreenPosY += borderScreenBottom; // scissor is bottom-left
            objScreenWidth -= (borderScreenLeft + borderScreenRight);
            objScreenHeight -= (borderScreenTop + borderScreenBottom);
        }

    }

    return Rect(objScreenPosX, objScreenPosY, objScreenWidth, objScreenHeight);
}

void RenderSystem::updateTransform(Transform& transform){
    Matrix4 translateMatrix = Matrix4::translateMatrix(transform.position);
    Matrix4 rotationMatrix = transform.rotation.getRotationMatrix();
    Matrix4 scaleMatrix = Matrix4::scaleMatrix(transform.scale);

    transform.localMatrix = translateMatrix * rotationMatrix * scaleMatrix;

    if (transform.parent != NULL_ENTITY){
        Transform& transformParent = scene->getComponent<Transform>(transform.parent);

        transform.modelMatrix = transformParent.modelMatrix * transform.localMatrix;

        transform.worldPosition = transformParent.modelMatrix * transform.position;
        transform.worldScale = transformParent.worldScale * transform.scale;
        transform.worldRotation = transformParent.worldRotation * transform.rotation;
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

    camera.worldRight = Vector3(camera.viewMatrix[0][0], camera.viewMatrix[1][0], camera.viewMatrix[2][0]);
    camera.worldUp = Vector3(camera.viewMatrix[0][1], camera.viewMatrix[1][1], camera.viewMatrix[2][1]);
    camera.worldDirection = Vector3(camera.viewMatrix[0][2], camera.viewMatrix[1][2], camera.viewMatrix[2][2]);

    //Update ViewProjectionMatrix
    camera.viewProjectionMatrix = camera.projectionMatrix * camera.viewMatrix;

    updateCameraFrustumPlanes(camera.viewProjectionMatrix, camera.frustumPlanes);
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

    sky.needUpdateSky = false;
}

void RenderSystem::updatePoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform){
    points.renderPoints.clear();
    points.renderPoints.reserve(points.points.size());

    // point particle sizes are in pixels, need to convert it to canvas size
    float sizeScale = 1.0f;
    int canvasW = Engine::getCanvasWidth();
    int canvasH = Engine::getCanvasHeight();
    if (canvasW > 0 && canvasH > 0){
        float sizeScaleW = System::instance().getScreenWidth() / (float)canvasW;
        float sizeScaleH = System::instance().getScreenHeight() / (float)canvasH;
        sizeScale = std::max(sizeScaleW, sizeScaleH);
    }

    points.numVisible = 0;
    size_t pointsSize = (points.points.size() < points.maxPoints)? points.points.size() : points.maxPoints;
    for (int i = 0; i < pointsSize; i++){
        if (points.points[i].visible){
            points.renderPoints.push_back({});
            points.renderPoints[points.numVisible].position = points.points[i].position;
            points.renderPoints[points.numVisible].color = points.points[i].color;
            points.renderPoints[points.numVisible].size = points.points[i].size * sizeScale;
            points.renderPoints[points.numVisible].rotation = points.points[i].rotation;
            points.renderPoints[points.numVisible].textureRect = points.points[i].textureRect;
            points.numVisible++;
        }
    }

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

void RenderSystem::updateTerrain(TerrainComponent& terrain, Transform& transform, CameraComponent& camera, Transform& cameraTransform){
    if (terrain.heightMapLoaded){
        for (int i = 0; i < terrain.numNodes; i++){
            terrain.nodes[i].visible = false;
        }

        for (int s = 0; s < 2; s++){
            terrain.nodesbuffer[s].clear();
        }

        for (int i = 0; i < (terrain.rootGridSize*terrain.rootGridSize); i++){
            terrainNodeLODSelect(terrain, transform, camera, cameraTransform, terrain.nodes[terrain.grid[i]], terrain.levels-1);
        }

        terrain.needUpdateNodesBuffer = true;

        terrain.eyePos = Vector3(cameraTransform.worldPosition.x, cameraTransform.worldPosition.y, cameraTransform.worldPosition.z);
    }
}

AABB RenderSystem::getTerrainNodeAABB(Transform& transform, TerrainNode& terrainNode){
    float halfSize = terrainNode.size/2;
    Vector3 worldHalfScale(halfSize * transform.worldScale.x, 1, halfSize * transform.worldScale.z);
    Vector3 worldPosition = transform.modelMatrix * Vector3(terrainNode.position.x, 0, terrainNode.position.y);

    Vector3 c1 = Vector3(worldPosition.x - worldHalfScale.x, terrainNode.minHeight, worldPosition.z - worldHalfScale.z);
    Vector3 c2 = Vector3(worldPosition.x + worldHalfScale.x, terrainNode.maxHeight, worldPosition.z + worldHalfScale.z);

    return AABB(c1, c2);
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

bool RenderSystem::terrainNodeLODSelect(TerrainComponent& terrain, Transform& transform, CameraComponent& camera, Transform& cameraTransform, TerrainNode& terrainNode, int lodLevel){
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
        terrain.nodesbuffer[0].addVector2(AttributeType::TERRAINNODEPOSITION, terrainNode.position);
        terrain.nodesbuffer[0].addFloat(AttributeType::TERRAINNODESIZE, terrainNode.size);
        terrain.nodesbuffer[0].addFloat(AttributeType::TERRAINNODERANGE, terrainNode.currentRange);
        terrain.nodesbuffer[0].addFloat(AttributeType::TERRAINNODERESOLUTION, terrainNode.resolution);

        return true;
    } else {

        if( !isTerrainNodeInSphere(cameraTransform.worldPosition, terrain.ranges[lodLevel-1], box) ) {
            //Full resolution
            terrainNode.resolution = terrain.resolution;
            terrainNode.visible = true;
            terrain.nodesbuffer[0].addVector2(AttributeType::TERRAINNODEPOSITION, terrainNode.position);
            terrain.nodesbuffer[0].addFloat(AttributeType::TERRAINNODESIZE, terrainNode.size);
            terrain.nodesbuffer[0].addFloat(AttributeType::TERRAINNODERANGE, terrainNode.currentRange);
            terrain.nodesbuffer[0].addFloat(AttributeType::TERRAINNODERESOLUTION, terrainNode.resolution);
        } else {
            for (int i = 0; i < 4; i++) {
                TerrainNode& child = terrain.nodes[terrainNode.childs[i]];
                if (!terrainNodeLODSelect(terrain, transform, camera, cameraTransform, child, lodLevel-1)){
                    //Half resolution
                    child.resolution = terrain.resolution / 2;
                    child.currentRange = terrainNode.currentRange;
                    child.visible = true;
                    terrain.nodesbuffer[1].addVector2(AttributeType::TERRAINNODEPOSITION, child.position);
                    terrain.nodesbuffer[1].addFloat(AttributeType::TERRAINNODESIZE, child.size);
                    terrain.nodesbuffer[1].addFloat(AttributeType::TERRAINNODERANGE, child.currentRange);
                    terrain.nodesbuffer[1].addFloat(AttributeType::TERRAINNODERESOLUTION, child.resolution);
                }
            }
        }

        return true;
    }

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
            float newAspect = rect.getWidth() / rect.getHeight();

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
        if (!ui.loaded)
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
        if (!lines.loaded)
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

    mesh.aabb = AABB::ZERO;
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

            viewMatrix = Matrix4::lookAtMatrix(transform.worldPosition, light.worldDirection + transform.worldPosition, up);

            projectionMatrix = Matrix4::perspectiveMatrix(acos(light.outerConeCos)*2, 1, light.shadowCameraNearFar.x, light.shadowCameraNearFar.y);

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
                    auto transformParent = scene->getComponent<Transform>(transform.parent);
                    transform.rotation = transformParent.worldRotation.inverse() * transform.rotation;
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
            Transform& transformParent = scene->getComponent<Transform>(transform.parent);

            if (transformParent.needUpdate){
                transform.needUpdate = true;
            }

            if (transformParent.needUpdateChildVisibility){
                transform.visible = transformParent.visible;
                transform.needUpdateChildVisibility = true;
            }
        }

        if (transform.needUpdate){
            updateTransform(transform);
        }
    }

    Entity mainCameraEntity = scene->getCamera();
    uint8_t pipelines = 0;

    hasMultipleCameras = false;
    for (int i = 0; i < cameras->size(); i++){
        CameraComponent& camera = cameras->getComponentFromIndex(i);
        Entity cameraEntity = cameras->getEntity(i);
        Transform& cameraTransform = scene->getComponent<Transform>(cameraEntity);

        if (camera.renderToTexture){
            if (!camera.framebuffer->isCreated()){
                camera.framebuffer->create();
            }
        }

        if (camera.renderToTexture && cameraEntity != mainCameraEntity){
            hasMultipleCameras = true;
        }

        if (cameraEntity == mainCameraEntity && !camera.renderToTexture){
            pipelines |= PIP_DEFAULT;
        }

        if (camera.renderToTexture || Engine::getFramebuffer()){
            pipelines |= PIP_RTT;
        }

        if (cameraTransform.needUpdate){
            camera.needUpdate = true;
        }
        
        if (camera.needUpdate){
            updateCamera(camera, cameraTransform);
        }
    }

    CameraComponent& mainCamera =  scene->getComponent<CameraComponent>(mainCameraEntity);
    Transform& mainCameraTransform =  scene->getComponent<Transform>(mainCameraEntity);

    loadLights(numLights);
    loadAndProcessFog();

    auto skys = scene->getComponentArray<SkyComponent>();
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
        }

        if (sky.loaded && sky.needReload){
            destroySky(entity, sky);
        }
        if (!sky.loadCalled){
            loadSky(entity, sky, pipelines);
        }
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
                    if (!hasMultipleCameras || !sortTransparentInstances){
                        if (instmesh->instancedBillboard){
                            updateInstancedMesh(*instmesh, mesh, transform, mainCamera, mainCameraTransform);
                        }
                        sortInstancedMesh(*instmesh, mesh, transform, mainCamera, mainCameraTransform);
                    }
                }

                instmesh->needUpdateInstances = false;
            }

            for (int s = 0; s < mesh.numSubmeshes; s++){
                if (mesh.submeshes[s].needUpdateTexture){
                    if (mesh.submeshes[i].textureShadow){
                        mesh.submeshes[i].needUpdateDepthTexture = true;
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

            if (mesh.loaded && mesh.needReload){
                destroyMesh(entity, mesh);
            }
            if (!mesh.loadCalled){
                loadMesh(entity, mesh, pipelines, instmesh, terrain);
            }
            if (mesh.needUpdateAABB || transform.needUpdate){
                mesh.worldAABB = transform.modelMatrix * mesh.aabb;
                mesh.needUpdateAABB = false;
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

            if (points.needUpdate || ((mainCamera.needUpdate || transform.needUpdate) && sortTransparentPoints)){
                if (!hasMultipleCameras || !sortTransparentPoints){
                    sortPoints(points, transform, mainCamera, mainCameraTransform);
                }
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

                if (signature.test(scene->getComponentId<TerrainComponent>())){
                    TerrainComponent& terrain = scene->getComponent<TerrainComponent>(entity);

                    updateTerrain(terrain, transform, mainCamera, mainCameraTransform);
                }
            }
            
            if (signature.test(scene->getComponentId<AudioComponent>())){
                AudioComponent& audio = scene->getComponent<AudioComponent>(entity);

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

                if (bone.model != NULL_ENTITY){
                    ModelComponent* model = scene->findComponent<ModelComponent>(bone.model);
                    MeshComponent* mesh = scene->findComponent<MeshComponent>(bone.model);

                    if (model && mesh) {
                        Matrix4 skinning = model->inverseDerivedTransform * transform.modelMatrix * bone.offsetMatrix;

                        if (bone.index >= 0 && bone.index < MAX_BONES)
                            mesh->bonesMatrix[bone.index] = skinning;
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
}

void RenderSystem::draw(){
    std::priority_queue<TransparentMeshesData, std::vector<TransparentMeshesData>, MeshComparison> transparentMeshes;

    auto transforms = scene->getComponentArray<Transform>();
    auto cameras = scene->getComponentArray<CameraComponent>();

    //---------Depth shader----------
    if (hasShadows){
        auto lights = scene->getComponentArray<LightComponent>();
        auto meshes = scene->getComponentArray<MeshComponent>();
        auto terrains = scene->getComponentArray<TerrainComponent>();
        
        for (int l = 0; l < lights->size(); l++){
            LightComponent& light = lights->getComponentFromIndex(l);

            if (light.intensity > 0 && light.shadows){
                size_t cameras = 1;
                if (light.type == LightType::POINT){
                    cameras = 6;
                }else if (light.type == LightType::DIRECTIONAL){
                    cameras = light.numShadowCascades;
                }

                for (int c = 0; c < cameras; c++){
                    size_t face = 0;
                    size_t fb = c;
                    if (light.type == LightType::POINT){
                        face = c;
                        fb = 0;
                    }
                    light.cameras[c].render.setClearColor(Vector4(1.0, 1.0, 1.0, 1.0));

                    light.cameras[c].render.startRenderPass(&light.framebuffer[fb], face);
                    for (int i = 0; i < meshes->size(); i++){
                        MeshComponent& mesh = meshes->getComponentFromIndex(i);
                        Entity entity = meshes->getEntity(i);
                        Transform* transform = scene->findComponent<Transform>(entity);

                        if (transform){
                            if (transform->visible){
                                InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
                                TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);

                                vs_depth_t vsDepthParams;

                                if (transform->billboard && mesh.shadowsBillboard){
                                    Matrix4 modelViewMatrix = light.cameras[c].lightViewMatrix * transform->modelMatrix;

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

                                    vsDepthParams = {modelViewMatrix, light.cameras[c].lightProjectionMatrix};
                                }else{
                                    vsDepthParams = {transform->modelMatrix, light.cameras[c].lightViewProjectionMatrix};
                                }

                                drawMeshDepth(mesh, light.cameras[c].nearFar.y, light.cameras[c].frustumPlanes, vsDepthParams, instmesh, terrain);
                            }
                        }
                    }

                    light.cameras[c].render.endRenderPass();
                }
            }
        }
    }

    for (int i = 0; i < cameras->size(); i++){
        Entity cameraEntity = cameras->getEntity(i);
        CameraComponent& camera = cameras->getComponentFromIndex(i);
        Transform& cameraTransform = scene->getComponent<Transform>(cameraEntity);

        bool isMainCamera = (cameraEntity == scene->getCamera());

        if (!isMainCamera && !camera.renderToTexture){
            continue; // camera is not used
        }

        if (Engine::getMainScene() == scene || camera.renderToTexture){
            camera.render.setClearColor(scene->getBackgroundColor());
        } else {
            // When drawing a scene as an Engine layer, don't clear the color buffer.
            // Otherwise a scene that was previously the main scene would keep clearing
            // and hide the main scene when reused as a layer.
            camera.render.setLoadActionLoad();
        }
        
        if (!camera.renderToTexture){
            if (Engine::getFramebuffer()){
                if (!Engine::getFramebuffer()->isCreated()){
                    Engine::getFramebuffer()->create();
                }
                camera.render.startRenderPass(&Engine::getFramebuffer()->getRender());
            }else{
                camera.render.startRenderPass();
            }
            camera.render.applyViewport(Engine::getViewRect());
        }else{
            if (!camera.framebuffer->isCreated()){
                camera.framebuffer->create();
            }
            camera.render.startRenderPass(&camera.framebuffer->getRender());
        }

        //---------Draw opaque meshes and UI----------
        bool hasActiveScissor = false;

        //---------Draw sky----------
        auto skys = scene->getComponentArray<SkyComponent>();
        if (skys->size() > 0){
            SkyComponent& sky = skys->getComponentFromIndex(0);
            Entity entity = skys->getEntity(0);
            if (hasMultipleCameras){
                updateSkyViewProjection(sky, camera);
            }

            drawSky(sky, camera.renderToTexture || Engine::getFramebuffer());
        }

        for (int i = 0; i < transforms->size(); i++){
            Transform& transform = transforms->getComponentFromIndex(i);
            Entity entity = transforms->getEntity(i);
            Signature signature = scene->getSignature(entity);

            if (signature.test(scene->getComponentId<CameraComponent>())){
                continue;
            }

            if (hasMultipleCameras){
                updateMVP(i, transform, camera, cameraTransform);
            }

            // apply scissor on UI
            if (signature.test(scene->getComponentId<UILayoutComponent>())){
                UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);

                Rect parentScissor;

                if (transform.parent != NULL_ENTITY){
                    Signature parentSignature = scene->getSignature(transform.parent);
                    if (parentSignature.test(scene->getComponentId<UILayoutComponent>())){
                        UILayoutComponent& parentLayout = scene->getComponent<UILayoutComponent>(transform.parent);

                        parentScissor = parentLayout.scissor;
                        if (!parentScissor.isZero()){
                            if (!layout.ignoreScissor){
                                camera.render.applyScissor(parentScissor);
                                layout.scissor = parentScissor;

                                hasActiveScissor = true;
                            }
                        }
                    }
                }

                if (signature.test(scene->getComponentId<ImageComponent>())){
                    ImageComponent& img = scene->getComponent<ImageComponent>(entity);

                    layout.scissor = getScissorRect(layout, img, transform, camera);
                    if (hasActiveScissor){
                        layout.scissor = layout.scissor.fitOnRect(parentScissor);
                    }
                }
            }

            if (signature.test(scene->getComponentId<MeshComponent>())){
                MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);

                if (transform.visible){

                    InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);
                    if (instmesh){
                        bool sortTransparentInstances = mesh.transparent && camera.type != CameraType::CAMERA_UI;

                        if (hasMultipleCameras && sortTransparentInstances){
                            if (instmesh->instancedBillboard){
                                updateInstancedMesh(*instmesh, mesh, transform, camera, cameraTransform);
                            }
                            sortInstancedMesh(*instmesh, mesh, transform, camera, cameraTransform);
                        }
                    }

                    TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
                    if (terrain && hasMultipleCameras){
                        updateTerrain(*terrain, transform, camera, cameraTransform);
                    }

                    if (!mesh.transparent || !camera.transparentSort){
                        //Draw opaque meshes if transparency is not necessary
                        drawMesh(mesh, transform, camera, cameraTransform, camera.renderToTexture || Engine::getFramebuffer(), instmesh, terrain);
                    }else{
                        transparentMeshes.push({&mesh, instmesh, terrain, &transform, transform.distanceToCamera});
                    }
                }

            }else if (signature.test(scene->getComponentId<UIComponent>())){
                UIComponent& ui = scene->getComponent<UIComponent>(entity);

                bool isText = false;
                if (signature.test(scene->getComponentId<TextComponent>())){
                    isText = true;
                }
                if (transform.visible)
                    drawUI(ui, transform, camera.renderToTexture || Engine::getFramebuffer());

            }else if (signature.test(scene->getComponentId<PointsComponent>())){
                PointsComponent& points = scene->getComponent<PointsComponent>(entity);

                bool sortTransparentPoints = points.transparent && camera.type != CameraType::CAMERA_UI;

                if (hasMultipleCameras && sortTransparentPoints){
                    sortPoints(points, transform, camera, cameraTransform);
                }

                if (transform.visible)
                    drawPoints(points, transform, cameraTransform, camera.renderToTexture || Engine::getFramebuffer());

            }else if (signature.test(scene->getComponentId<LinesComponent>())){
                LinesComponent& lines = scene->getComponent<LinesComponent>(entity);

                if (transform.visible)
                    drawLines(lines, transform, cameraTransform, camera.renderToTexture || Engine::getFramebuffer());

            }

            if (hasActiveScissor){
                if (!camera.renderToTexture){
                    camera.render.applyScissor(Rect(0, 0, System::instance().getScreenWidth(), System::instance().getScreenHeight()));
                }else{
                    camera.render.applyScissor(Rect(0, 0, camera.framebuffer->getWidth(), camera.framebuffer->getHeight()));
                }
                hasActiveScissor = false;
            }
        }

        //---------Draw transparent meshes----------
        while (!transparentMeshes.empty()){
            TransparentMeshesData meshData = transparentMeshes.top();

            //Draw transparent meshes
            drawMesh(*meshData.mesh, *meshData.transform, camera, cameraTransform, camera.renderToTexture || Engine::getFramebuffer(), meshData.instmesh, meshData.terrain);

            transparentMeshes.pop();
        }

        camera.render.endRenderPass();

    }

    //---------Missing some shaders----------
    if (ShaderPool::getMissingShaders().size() > 0){
        std::string misShaders;
        for (int i = 0; i < ShaderPool::getMissingShaders().size(); i++){
            if (!misShaders.empty())
                misShaders += "; ";
            misShaders += ShaderPool::getMissingShaders()[i];
        }
        Log::verbose(
            "\n"
            "-------------------\n"
            "Doriax is missing some shaders, you need to use Supershader tool to create these shaders in project assets directory.\n"
            "Go to directory \"tools/\" and execute the command:\n"
            "\n"
            "> python3 supershader.py -s \"%s\" -l %s\n"
            "-------------------"
            , misShaders.c_str(), ShaderPool::getShaderLangStr().c_str());
        exit(1);
    }
}

void RenderSystem::onComponentAdded(Entity entity, ComponentId componentId) {
    if (componentId == scene->getComponentId<LightComponent>()) {
        needReloadMeshes();
    }
}

void RenderSystem::onComponentRemoved(Entity entity, ComponentId componentId) {
    if (componentId == scene->getComponentId<LightComponent>()) {
        LightComponent& light = scene->getComponent<LightComponent>(entity);
        destroyLight(light);
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
    }
}
