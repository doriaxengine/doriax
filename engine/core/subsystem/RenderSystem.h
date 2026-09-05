// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

#include "SubSystem.h"
#include "component/MeshComponent.h"
#include "component/InstancedMeshComponent.h"
#include "component/ModelComponent.h"
#include "component/SkyComponent.h"
#include "component/ReflectionProbeComponent.h"
#include "component/UILayoutComponent.h"
#include "component/UIComponent.h"
#include "component/ImageComponent.h"
#include "component/CameraComponent.h"
#include "component/LightComponent.h"
#include "component/PointsComponent.h"
#include "component/LinesComponent.h"
#include "component/TerrainComponent.h"
#include "component/TilemapComponent.h"
#include "component/SpriteComponent.h"
#include "component/Transform.h"
#include "render/ObjectRender.h"
#include "render/CameraRender.h"
#include "render/BufferRender.h"
#include "render/FramebufferRender.h"
#include "buffer/ExternalBuffer.h"
#include "Engine.h"
#include <map>
#include <array>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>

namespace doriax{
	typedef struct fs_lighting_t {
	    Vector4 direction_range[MAX_LIGHTS];
	    Vector4 color_intensity[MAX_LIGHTS];
	    Vector4 position_type[MAX_LIGHTS];
	    Vector4 inCon_ouCon_shadows_cascades[MAX_LIGHTS];
	    Vector4 spotUp_maskAspect[MAX_LIGHTS]; // world up.xyz; w > 0 means a ready mask and stores its width/height
		Vector4 eyePos;
		Vector4 cameraDir; // xyz = camera backward axis, w = 3D shadow PCF tap radius
		Vector4 globalIllum; //global illumination
		Vector4 envColor; //environment color.rgb (linear) and environment rotation.w (radians)
		Vector4 viewportInfo; //1.0/viewportSize.xy in .xy (used by USE_SSAO)
	} fs_lighting_t;

	typedef struct fs_lighting2d_t {
	    Vector4 position_range[MAX_LIGHTS_2D];   // xy = world pos, z = height, w = range
	    Vector4 color_intensity[MAX_LIGHTS_2D];  // rgb = color (linear), w = intensity
	    Vector4 falloff_shadow[MAX_LIGHTS_2D];   // x = falloff exp, y = shadow atlas row (-1 = none), z = softness (texels), w = bias
		Vector4 ambient;                         // rgb = ambient2D (linear) * intensity, w = numLights2D
		Vector4 atlasInfo;                       // x = 1/atlasWidth, y = 1/MAX_LIGHTS_2D, z = atlasWidth, w = PCF tap radius
	} fs_lighting2d_t;

	typedef struct fs_reflection_probe_t {
		Vector4 position_weight; // xyz = capture position, w = local probe blend weight
		Vector4 boxMin_intensity; // xyz = influence AABB min, w = intensity
		Vector4 boxMax_lod;       // xyz = influence AABB max, w = max available mip LOD
	} fs_reflection_probe_t;

	typedef struct vs_shadow2d_t {
		Vector4 lightPos_range; // xy = light world pos, w = range
		Vector4 offset;         // x = NDC x offset for the polar seam duplication (-2, 0, +2)
	} vs_shadow2d_t;

	typedef struct fs_fog_t {
		Vector4 color_type;
		Vector4 density_start_end;
	} fs_fog_t;

	typedef struct vs_shadows_t {
	    Matrix4 lightViewProjectionMatrix[MAX_SHADOW_ATLAS_SLOTS];
	    Vector4 shadowParams[MAX_SHADOW_ATLAS_SLOTS]; // normalBias in .x
	} vs_shadows_t;

	typedef struct fs_shadows_t {
        Vector4 bias_texSize_nearFar[MAX_SHADOW_ATLAS_SLOTS];
        // xy = atlas origin, zw = atlas scale (directional/spot)
        Vector4 atlasRect[MAX_SHADOW_ATLAS_SLOTS];
	} fs_shadows_t;

	typedef struct fs_point_shadows_t {
        Vector4 bias_texSize_nearFar[MAX_POINT_SHADOW_ATLAS_SLOTS];
        // xy = atlas origin, zw = atlas scale (point cube faces)
        Vector4 atlasRect[MAX_POINT_SHADOW_ATLAS_SLOTS];
	} fs_point_shadows_t;

	typedef struct vs_depth_t {
		Matrix4 modelMatrix;
	    Matrix4 lightSpaceMatrix;
	} vs_depth_t;

	typedef struct vs_gbuffer_t {
		Matrix4 modelMatrix;
		Matrix4 viewProjectionMatrix;
		Matrix4 normalMatrix;       // view-space normal matrix: transpose(inverse(view*model))
	} vs_gbuffer_t;

	typedef struct fs_gbuffer_material_t {
		Vector4 params;             // x = roughness, y = metallic, z = IBL source, w = alpha cutout
		Vector4 baseColorFactor;
	} fs_gbuffer_material_t;

	typedef struct fs_ssao_t {
		Matrix4 projection;
		Matrix4 invProjection;
		Vector4 kernel[SSAO_KERNEL_SIZE];
		Vector4 params;     // radius.x, bias.y, intensity.z
		Vector4 noiseScale; // screenSize/noiseSize in .xy
	} fs_ssao_t;

	typedef struct fs_ssao_blur_t {
		Vector4 texelSize; // 1.0/textureSize in .xy
	} fs_ssao_blur_t;

	typedef struct fs_ssr_t {
		Matrix4 projection;
		Matrix4 invProjection;
		Vector4 params; // x=maxDistance, y=thickness, z=intensity, w=maxSteps
		Vector4 misc;   // xy=1/depthTextureSize, z=flip depth<->scene-color Y, w=glossy-blur amount
	} fs_ssr_t;

	typedef struct fs_ssr_blur_t {
		Vector4 params; // xy=1/textureSize, z=max radius(pixels), w=flip G-buffer Y
	} fs_ssr_blur_t;

	typedef struct fs_composite_t {
		Matrix4 invProjection;
		Matrix4 invView;
		Vector4 params;   // x=intensity, y=flip G-buffer Y, z=debug, w=unused
		Vector4 envColor; // rgb = env color (linear), w = env rotation (radians)
	} fs_composite_t;

	typedef struct fs_blit_t {
		Vector4 params; // x = flip source Y on sample (GL swapchain destination), yzw unused
	} fs_blit_t;

	typedef struct vs_points_params_t {
		Matrix4 mvpMatrix;
		float pointScale;
		float padding[3];
	} vs_points_params_t;

	typedef struct check_load_t {
		Scene* scene;
		Entity entity;
	} check_load_t;

	class DORIAX_API RenderSystem : public SubSystem {
	private:
		enum class TransparentRenderType{
			MESH,
			POINTS
		};

		struct TransparentRenderData{
			TransparentRenderType type;
			MeshComponent* mesh;
			PointsComponent* points;
			InstancedMeshComponent* instmesh;
			TerrainComponent* terrain;
			TilemapComponent* tilemap;
			Transform* transform;
			float distanceToCamera;
		};

		// One merged run of tilemap indices to draw, in elements relative to the
		// submesh index range (see selectTilemapChunks).
		struct TilemapDrawRange{
			unsigned int offset;
			unsigned int count;
		};

		struct TransparentRenderComparison{
			bool operator()(const TransparentRenderData& lhs, const TransparentRenderData& rhs) const{
				return lhs.distanceToCamera < rhs.distanceToCamera;
			}
		};

		struct SpotMaskAtlasEntry{
			std::string sourceId;
			const std::array<TextureData, 6>* sourceOwner = nullptr;
			const void* sourcePixels = nullptr;
			int width = 0;
			int height = 0;
			float aspect = 1.0f;

			bool empty() const{
				return sourceId.empty();
			}

			bool operator==(const SpotMaskAtlasEntry& other) const{
				return sourceId == other.sourceId &&
					sourceOwner == other.sourceOwner &&
					sourcePixels == other.sourcePixels &&
					width == other.width &&
					height == other.height;
			}
		};

		Scene* scene;

		// visible ranges of the tilemap submesh being drawn, refilled by
		// selectTilemapChunks and consumed by the draw right after it
		std::vector<TilemapDrawRange> tilemapDrawRanges;

		// Editor-only viewport debug override that forces all meshes to render
		// without face culling. Defaults to false and is never set at runtime, so
		// exported games are unaffected.
		bool disableFaceCulling = false;

		// Editor-only override that suppresses the scene's fixed game resolution
		// (edit-mode viewports render native; play mode re-enables it). Defaults
		// to false and is never set at runtime, so exported games are unaffected.
		bool disableFixedResolution = false;

		static uint32_t pixelsWhite[64];
		static uint32_t pixelsBlack[64];
		static uint32_t pixelsNormal[64];

		static TextureRender emptyWhite;
		static TextureRender emptyBlack;
		static TextureRender emptyCubeBlack;
		static TextureRender emptyCubeWhite;
		static TextureRender emptyNormal;
		static TextureRender emptyShadowDepth;

		static bool emptyTexturesCreated;
		
		bool hasLights;
		bool hasShadows;
		bool hasFog;
		bool hasIBL;
		bool hasReflectionProbes;
		bool hasMultipleCameras;
		// extra cameras the last frame drew, to catch the switch back to main only
		bool lastMultiCameraDraw;
		bool capturingReflectionProbe;
		// pipelines the objects were loaded with, to catch a destination change
		uint8_t loadedPipelines;

		// Projected spotlight masks share one horizontal R8 atlas. Each punctual-light
		// array index maps directly to one fixed-size atlas tile.
		TextureRender spotMaskAtlas;
		std::array<SpotMaskAtlasEntry, MAX_LIGHTS> spotMaskAtlasEntries;
		std::vector<unsigned char> spotMaskAtlasPixels;
		bool spotMaskAtlasCreated;

		struct ReflectionProbeRuntime{
			FramebufferRender captureFramebuffer;
			CameraRender capturePass;
			std::shared_ptr<TextureRender> irradianceMap;
			std::shared_ptr<TextureRender> prefilteredMap;
			unsigned int resolution = 0;
			int nextFace = 0;
			float elapsed = 0.0f;
			float retryDelay = 0.0f;
			bool ready = false;
			bool captureInProgress = false;
			bool modeInitialized = false;
			ReflectionProbeMode lastMode = ReflectionProbeMode::STATIC;
			unsigned int observedCaptureRevision = 0;
			Vector3 capturePosition;
			Vector3 capturedPosition;
		};

		std::unordered_map<Entity, std::unique_ptr<ReflectionProbeRuntime>> reflectionProbeRuntimes;
		// Each mirror's reflection camera. Kept here so the authored component stays copiable.
		std::unordered_map<Entity, Entity> mirrorCameras;
		Entity activeReflectionProbe = NULL_ENTITY;
		fs_reflection_probe_t fs_reflection_probe;

		// 2D light path (Light2DComponent). Deliberately independent of
		// scene->getLightState(): the editor forces LightState::OFF in 2D scenes
		// to keep 3D lights out, but 2D lights must still work there.
		bool hasLights2D;
		bool hasShadows2D;
		int numLights2D;

		// Directional + spot atlas keeps up to 3x3 logical slots, but its physical
		// grid is compacted to the slots used by the current light set.
		FramebufferRender shadowAtlasFramebuffer;
		CameraRender shadowAtlasPassRender;
		unsigned int shadowAtlasSlotResolution;
		int shadowAtlasCols;
		int shadowAtlasRows;
		int shadowAtlasUsedSlots;
		bool needUpdateShadowAtlas;
		bool hasShadowAtlas;

		// separate atlas for point-light cube faces
		FramebufferRender shadowPointAtlasFramebuffer;
		CameraRender shadowPointAtlasPassRender;
		unsigned int shadowPointAtlasSlotResolution;
		bool needUpdateShadowPointAtlas;
		bool hasShadowPointAtlas;

		// 1D polar shadow atlas for 2D lights: width x MAX_LIGHTS_2D, one row per
		// shadow-casting Light2D. Occluder2D segments of the whole scene are merged
		// into one world-space line-list buffer and drawn once per light row.
		FramebufferRender shadow2DAtlasFramebuffer;
		CameraRender shadow2DAtlasPassRender;
		unsigned int shadow2DAtlasWidth;
		bool hasShadow2DAtlas;

		bool occluder2DLoaded;
		ExternalBuffer occluder2DBuffer;
		std::vector<float> occluder2DSegments; // 4 floats per vertex: endpoint xy + other-endpoint xy
		ObjectRender occluder2DRender;
		std::shared_ptr<ShaderRender> shadow2DShader;
		int shadow2DSlotParams;
		unsigned int occluder2DBufferCapacity; // in vertices
		unsigned int occluder2DVertexCount;    // vertices built this frame
		// sokol allows one sg_update_buffer per buffer per frame, but the same scene
		// can be drawn twice in a frame (e.g. open as a tab AND layered as a child
		// scene), so the upload is flagged in update() and consumed by the first draw
		bool occluder2DNeedUpdateBuffer;

		bool needUpdateShadowBindings;

		fs_lighting_t fs_lighting;
		fs_lighting2d_t fs_lighting2d;
		vs_shadows_t vs_shadows;
		fs_shadows_t fs_shadows;
		fs_point_shadows_t fs_point_shadows;
		fs_fog_t fs_fog;

		// SSAO: per-frame fullscreen passes for the main camera. The depth pre-pass
		// renders camera-space packed depth, then the ssao + blur fullscreen passes
		// produce an AO texture sampled by lit meshes (USE_SSAO).
		bool ssaoLoaded;
		unsigned int ssaoWidth;
		unsigned int ssaoHeight;
		Framebuffer ssaoDepthFramebuffer; // color = packed depth, depth = z-test
		Framebuffer ssaoFramebuffer;      // raw AO
		Framebuffer ssaoBlurFramebuffer;  // blurred AO (sampled by meshes)
		CameraRender ssaoPassRender;      // drives the offscreen SSAO passes
		ObjectRender ssaoRender;          // fullscreen ssao.frag draw
		ObjectRender ssaoBlurRender;      // fullscreen ssao_blur.frag draw
		TextureRender ssaoNoiseTexture;
		std::shared_ptr<ShaderRender> ssaoShader;
		std::shared_ptr<ShaderRender> ssaoBlurShader;
		fs_ssao_t fs_ssao; // kernel filled once in loadSSAO; matrices/params per frame
		fs_ssao_blur_t fs_ssao_blur;
		int ssaoSlotParams;
		int ssaoBlurSlotParams;
		TextureRender* currentSSAOTexture; // AO bound to meshes this camera (or empty white)

		// SSR: per-frame fullscreen passes for the main camera. The opaque color pass
		// renders into sceneColorFramebuffer, the ssr pass marches the depth pre-pass
		// and samples that color, then the composite pass blends reflections into the
		// real render target.
		bool ssrLoaded;
		unsigned int ssrWidth;
		unsigned int ssrHeight;
		Framebuffer gbufferFramebuffer;    // MRT: color[0] packed depth, color[1] view-space normal/roughness/metallic
		Framebuffer sceneColorFramebuffer; // offscreen opaque scene color (top-left)
		Framebuffer ssrFramebuffer;        // reflection color + mask (logical orientation)
		Framebuffer ssrBlurFramebuffer;    // glossy-blurred reflection
		CameraRender gbufferPassRender;    // drives the MRT G-buffer geometry pass
		CameraRender ssrPassRender;        // drives the offscreen ssr + composite passes
		ObjectRender ssrRender;            // fullscreen ssr.frag draw
		ObjectRender ssrBlurRender;        // fullscreen ssr_blur.frag draw
		ObjectRender compositeRender;      // fullscreen composite.frag draw
		std::shared_ptr<ShaderRender> ssrShader;
		std::shared_ptr<ShaderRender> ssrBlurShader;
		std::shared_ptr<ShaderRender> compositeShader;
		fs_ssr_t fs_ssr;
		fs_ssr_blur_t fs_ssr_blur;
		fs_composite_t fs_composite;
		int ssrSlotParams;
		int ssrBlurSlotParams;
		int compositeSlotParams;
		bool swapchainRedirect;            // exported builds: the last pass targets the swapchain

		// Fixed game resolution: when enabled on the Engine main scene (Scene
		// fixedResolution settings), the main camera renders into
		// fixedResFramebuffer and a fullscreen blit pass upscales it to the
		// real destination (view rect).
		bool blitLoaded;
		unsigned int fixedResWidth;
		unsigned int fixedResHeight;
		Framebuffer fixedResFramebuffer;   // offscreen scene color at the fixed size
		CameraRender fixedResPassRender;   // drives the upscale blit pass
		ObjectRender blitRender;           // fullscreen blit.frag draw
		std::shared_ptr<ShaderRender> blitShader;
		fs_blit_t fs_blit;
		int blitSlotParams;

		// User post-process chain: forked fullscreen shaders run after the color (and
		// SSR) pass, ping-ponging between two buffers into the real destination.
		struct PostProcessRuntime{
			std::shared_ptr<ShaderRender> shader;
			ObjectRender render;
			uint16_t customId;                     // 0 = built-in passthrough
			int slotParams;                        // -1 when the fork has no u_fs_postParams
			std::vector<uint8_t> params;           // packed block, applied as raw bytes
			std::pair<int, int> slotSceneColor;
			std::pair<int, int> slotDepth;
			std::pair<int, int> slotGBuffer;
			std::pair<int, int> slotSSAO;
			bool hasResolution;                    // members the engine writes per frame
			bool hasTime;
			ShaderUniform resolutionUniform;
			ShaderUniform timeUniform;
		};
		bool postProcessLoaded;
		bool postProcessNeedReload;
		bool postProcessNeedsDepth;            // a pass samples the depth texture
		bool postProcessNeedsGBuffer;          // a pass samples the G-buffer (needs SSR)
		unsigned int postProcessWidth;
		unsigned int postProcessHeight;
		Framebuffer postProcessFramebuffer[2]; // ping-pong targets
		CameraRender postProcessPassRender;    // drives the offscreen post passes
		std::vector<PostProcessRuntime> postProcessPasses;

		static void changeLoaded(void* data);
		static void changeDestroy(void* data);

		static bool samplesCameraTarget(const CameraComponent& camera, const MeshComponent& mesh);
		static bool samplesCameraTarget(const CameraComponent& camera, const Texture& texture);
		bool isRenderingFlipped(const CameraComponent& camera) const;
		bool isFixedResolutionActive() const;
		void updateSwapchainRedirect();
		void updateMVP(size_t index, Transform& transform, CameraComponent& camera, Transform& cameraTransform);

		void createEmptyTextures();
		int checkLightsAndShadow();
		bool loadLights(int numLights);
		void processLights(int numLights, CameraComponent& camera, Transform& cameraTransform);
		void updateSpotMaskAtlas(int numLights);
		void loadSpotMaskTexture(ShaderData& shaderData, ObjectRender& render);
		bool loadLights2D();
		void processLights2D();
		bool ensureShadow2DAtlas(unsigned int width);
		void loadShadow2DTexture(ShaderData& shaderData, ObjectRender& render, bool receiveShadows2D);
		unsigned int buildOccluder2DSegments();
		bool loadOccluder2DPass(unsigned int vertexCapacity);
		void destroyOccluder2DPass();
		bool loadAndProcessFog();
		void releaseSkyEnvironment(SkyComponent& sky);
		void updateSkyEnvironment(SkyComponent& sky);
		void updateReflectionProbes(double dt);
		void renderReflectionProbeCapture();
		void releaseReflectionProbeMaps(ReflectionProbeRuntime& runtime);
		void destroyReflectionProbe(Entity entity, ReflectionProbeComponent& probe);
		bool selectReflectionProbe(const Vector3& worldPosition, fs_reflection_probe_t& params, TextureRender*& texture);
		void initShadowAtlasRects();
		void initShadowPointAtlasRects();
		unsigned int clampShadowAtlasSlotResolution(unsigned int requestedResolution, int atlasCols, int atlasRows) const;
		bool ensureShadowAtlas(unsigned int slotResolution, int usedSlots);
		bool ensureShadowPointAtlas(unsigned int slotResolution);
		Rect getShadowAtlasSlotRect(int slotIndex) const;
		Rect getShadowPointAtlasSlotRect(int slotIndex) const;
		void configureLightShadowNearFar(LightComponent& light, const CameraComponent& camera);
		Matrix4 getDirLightProjection(const Matrix4& viewMatrix, const Matrix4& sceneCameraInv, float shadowMaxDistance, const Vector3& lightDirection, const Vector3& cameraPosition);
		bool checkPBRFrabebufferUpdate(Material& material);
		bool checkPBRTextures(Material& material, bool receiveLights);
		bool loadPBRTextures(Material& material, ShaderData& shaderData, ObjectRender& render, bool receiveLights);
		void loadShadowTextures(ShaderData& shaderData, ObjectRender& render, bool receiveLights, bool receiveShadow);
		void updateShadowBindings();
		bool loadDepthTexture(Material& material, ShaderData& shaderData, ObjectRender& render);
		bool loadGBufferTextures(Material& material, ShaderData& shaderData, ObjectRender& render);
		bool loadTerrainTextures(TerrainComponent& terrain, ObjectRender& render, ShaderData& shaderData);
		bool loadTerrainHeightTexture(TerrainComponent& terrain, ObjectRender& render, ShaderData& shaderData);
		bool updateTerrainRenderTextures(TerrainComponent& terrain, MeshComponent& mesh);
		void updateAllTerrainRenderTextures();
		void updateInstanceBuffers();
		Rect getScissorRect(UILayoutComponent& layout, ImageComponent& img, Transform& transform, const Rect& passViewport);

		// terrain
		bool terrainNodeLODSelect(TerrainComponent& terrain, Transform& transform, CameraComponent& camera, Transform& cameraTransform, TerrainNode& terrainNode, int lodLevel, int viewIndex);
		AABB getTerrainNodeAABB(Transform& transform, TerrainNode& terrainNode);
		bool isTerrainNodeInSphere(Vector3 position, float radius, const AABB& box);

		// tilemap
		bool selectTilemapChunks(TilemapComponent& tilemap, unsigned int submeshIndex, const float cameraFar, const Plane frustumPlanes[6]);

		float lerp(float a, float b, float fraction);

	protected:

		void updateMeshBuffers(MeshComponent& mesh);
		void updateTerrainNodesBuffer(TerrainComponent& terrain, int viewIndex);
		bool drawMesh(MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform, bool renderToTexture, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap, int terrainView = 0);
		bool drawMeshDepth(MeshComponent& mesh, const float cameraFar, const Plane frustumPlanes[6], vs_depth_t vsDepthParams, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap, bool forSSAO = false, PipelineType pipelineType = PIP_DEPTH);
		void destroyMesh(Entity entity, MeshComponent& mesh, bool clearAssets = false);

		// SSAO
		void loadSSAO();
		void destroySSAO();
		bool ensureSSAOFramebuffers(unsigned int width, unsigned int height);
		// sharedDepth != null reuses an existing packed-depth texture (the SSR G-buffer),
		// skipping the SSAO depth pre-pass; null runs the pre-pass
		void renderSSAO(CameraComponent& camera, TextureRender* sharedDepth = nullptr);

		// SSR
		void loadSSR();
		void destroySSR();
		bool ensureSSRFramebuffers(unsigned int width, unsigned int height);
		void renderDepthPrePass(CameraComponent& camera); // shared depth for SSAO/SSR
		// G-buffer geometry pass for SSR: MRT packed depth + view-space normal/roughness/metallic
		bool ensureGBufferFramebuffer(unsigned int width, unsigned int height);
		void renderGBufferPass(CameraComponent& camera);
		bool drawMeshGBuffer(MeshComponent& mesh, const float cameraFar, const Plane frustumPlanes[6], vs_gbuffer_t vsGBufferParams, bool hasLocalProbe, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap);
		// destination == nullptr renders the composite to the swapchain (backbuffer)
		void renderSSR(CameraComponent& camera, FramebufferRender* destination);

		// fixed game resolution
		void loadBlit();
		void destroyBlit();
		bool ensureFixedResFramebuffer(unsigned int width, unsigned int height, TextureFilter filter);
		// draws source over the real destination (Engine framebuffer in the editor,
		// swapchain in exported builds)
		void renderBlit(TextureRender* source, Rect viewport);
		// upscales fixedResFramebuffer to the view rect
		void renderFixedResolutionBlit();

		// user post-process chain
		void loadPostProcess();
		void destroyPostProcess();
		bool ensurePostProcessFramebuffers(unsigned int width, unsigned int height);
		// runs the chain from ping-pong buffer 0 into destination (swapchain when null)
		void renderPostProcess(FramebufferRender* destination);

		bool drawUI(UIComponent& ui, Transform& transform, bool renderToTexture);
		void destroyUI(Entity entity, UIComponent& ui);

		bool drawPoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform, bool renderToTexture);
		float getPointsViewportHeight(const CameraComponent& camera) const;
		float computePointsScale(const CameraComponent& camera, float viewportHeight) const;
		void destroyPoints(Entity entity, PointsComponent& points);

		bool drawLines(LinesComponent& lines, Transform& transform, Transform& camTransform, bool renderToTexture);
		void destroyLines(Entity entity, LinesComponent& lines);

		bool drawSky(SkyComponent& sky, bool renderToTexture, bool invertCulling);
		void destroySky(Entity entity, SkyComponent& sky);

		void destroyLight(LightComponent& light);
		void destroyCamera(CameraComponent& camera, bool entityDestroyed);
		
		void updateSkyViewProjection(SkyComponent& sky, CameraComponent& camera);
		void updateLightFromScene(LightComponent& light, Transform& transform, CameraComponent& camera, Transform& cameraTransform);
		void updatePoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform);
		void updateTerrain(TerrainComponent& terrain, Transform& transform, CameraComponent& camera, Transform& cameraTransform, int viewIndex);
		void updateCameraFrustumPlanes(const Matrix4 viewProjectionMatrix, Plane* frustumPlanes);
		void updateInstancedMesh(InstancedMeshComponent& instmesh, MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform);

		void sortPoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform);
		void sortInstancedMesh(InstancedMeshComponent& instmesh, MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform);

	public:

		RenderSystem(Scene* scene);
		virtual ~RenderSystem();

		// Editor-only viewport debug override (see member declaration). Toggling it
		// reloads every mesh, since cull mode is baked into the pipeline at load time.
		void setDisableFaceCulling(bool disableFaceCulling);

		// Editor-only override (see member declaration). No reload needed: the
		// editor always renders through Engine::getFramebuffer(), so PIP_RTT is
		// already baked either way.
		void setDisableFixedResolution(bool disableFixedResolution);

		// copies the stacked scene composite to the swapchain (Engine::endCompositeFramebuffer)
		void presentFramebufferToSwapchain(Framebuffer* source);

		bool loadMesh(Entity entity, MeshComponent& mesh, uint8_t pipelines, InstancedMeshComponent* instmesh, TerrainComponent* terrain);
		bool loadPoints(Entity entity, PointsComponent& points, uint8_t pipelines);
		bool loadLines(Entity entity, LinesComponent& lines, uint8_t pipelines);
		bool loadUI(Entity entity, UIComponent& ui, uint8_t pipelines, bool isText);
		bool loadSky(Entity entity, SkyComponent& sky, uint8_t pipelines);

		void updateFramebuffer(CameraComponent& camera);
		void updateTransform(Transform& transform);
		void updateCamera(CameraComponent& camera, Transform& transform);
		void updateMirrors(Entity mainCameraEntity);
		Entity createMirrorCamera(Entity mirrorEntity);
		Entity getMirrorCamera(Entity mirrorEntity) const;
		void destroyMirrorCamera(Entity entity);

		// camera
		void updateCameraSize(Entity entity);
		bool isInsideCamera(const float cameraFar, const Plane frustumPlanes[6], const AABB& box);
		bool isInsideCamera(CameraComponent& camera, const AABB& box);
		bool isInsideCamera(CameraComponent& camera, const Vector3& point);
		bool isInsideCamera(CameraComponent& camera, const Vector3& center, const float& radius);

		void needReloadPoints();
		void needReloadLines();
		void needReloadMeshes();
		void needReloadPostProcess();
		void needReloadUIs();
		void needReloadSky();
		void prepareMeshForDataReload(Entity entity, MeshComponent& mesh);

		bool isAllLoaded() const;
	
		void load() override;
		void draw() override;
		void destroy() override;
		void update(double dt) override;

		void onComponentAdded(Entity entity, ComponentId componentId) override;
		void onComponentRemoved(Entity entity, ComponentId componentId) override;
	};

}

#endif //RENDERSYSTEM_H
