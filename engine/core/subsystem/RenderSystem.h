// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

#include "SubSystem.h"
#include "component/MeshComponent.h"
#include "component/InstancedMeshComponent.h"
#include "component/ModelComponent.h"
#include "component/SkyComponent.h"
#include "component/WaterComponent.h"
#include "component/WaterExclusionComponent.h"
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
	// mirrored by the u_fs_lighting block of mesh.frag and water.frag
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

	typedef struct fs_terrain_layers_t {
		Vector4 colorFactor[MAX_TERRAIN_LAYERS]; // rgb = linear tint, w = 1 on a PBR layer
		Vector4 uvTransform[MAX_TERRAIN_LAYERS]; // xy = detail tiling scale, zw = offset
		Vector4 surface[MAX_TERRAIN_LAYERS];     // roughness, metallic, normal strength, occlusion strength
		Vector4 slices[MAX_TERRAIN_LAYERS];      // color, normal and packed surface slice (-1 = none)
	} fs_terrain_layers_t;

	typedef struct fs_reflection_probe_t {
		Vector4 position_weight; // xyz = capture position, w = local probe blend weight
		Vector4 boxMin_intensity; // xyz = influence AABB min, w = intensity
		Vector4 boxMax_lod;       // xyz = influence AABB max, w = max available mip LOD
	} fs_reflection_probe_t;

	typedef struct fs_probe_filter_t {
		Vector4 params; // x = roughness, y = cube face, z = samples (taps per side when downsampling), w = 1 to downsample
		Vector4 source; // x = destination level size, y = source base size, z = source mip count
	} fs_probe_filter_t;

	typedef struct vs_shadow2d_t {
		Vector4 lightPos_range; // xy = light world pos, w = range
		Vector4 offset;         // x = NDC x offset for the polar seam duplication (-2, 0, +2)
	} vs_shadow2d_t;

	typedef struct fs_fog_t {
		Vector4 color_type;
		Vector4 density_start_end;
	} fs_fog_t;

	// u_fs_shadow_projection, and u_vs_shadows in shader forks that predate it
	typedef struct shadow_projection_t {
	    Matrix4 lightViewProjectionMatrix[MAX_SHADOW_ATLAS_SLOTS];
	    Vector4 shadowParams[MAX_SHADOW_ATLAS_SLOTS]; // normalBias in .x
	} shadow_projection_t;

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
		Matrix4 lightVPMatrix;
		Matrix4 mvpMatrix; // lightVPMatrix * modelMatrix
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

	typedef struct vs_water_t {
		Matrix4 modelMatrix;
		Matrix4 viewProjectionMatrix; // render matrix, Y-flipped for offscreen GL passes
		Vector4 waves[WATER_WAVE_COUNT]; // xy = direction, z = amplitude, w = wavelength
		Vector4 phases;
		Vector4 params; // x = crest steepness
	} vs_water_t;

	typedef struct fs_water_t {
		Matrix4 viewProjection;           // logical, finds the scene depth under the fragment
		Matrix4 invViewProjection;        // logical, unprojects the scene depth
		Matrix4 reflectionViewProjection; // logical, of the planar reflection camera
		Vector4 shallowColor;  // rgb = linear color, w = depth fade
		Vector4 deepColor;     // rgb = linear color, w = 1 when the scene depth is valid
		Vector4 foamColor;     // rgb = linear color, w = shore foam depth
		Vector4 ripples;       // x = 1 / tile size, y = strength, z = wave height, w = crest foam
		Vector4 rippleOffsets; // xy = first layer, zw = second layer (tiles)
		Vector4 surface;       // x = reflectivity, y = specular, z = roughness, w = reflection distortion
		Vector4 envColor;      // rgb = sky tint (linear), w = sky rotation (radians)
		Vector4 eyePos;        // xyz = eye of this pass, w = time
		Vector4 flags;         // x = scene lights on, y = IBL ambient available, z = planar reflection, w = eye inside the water
		Vector4 refraction;    // x = distortion, y = 1 when this pass has the scene copy
		Vector4 refractionRect; // xy = view origin, zw = view size, in scene copy uv
		Vector4 caustics;      // x = strength, y = 1 / cell size
		Vector4 waveScale;     // x = longest wavelength
		Vector4 exclusionCount; // x = volumes keeping the water out
		Vector4 exclusionRows[MAX_WATER_EXCLUSIONS * 3]; // world to 0-1 across the volume, three rows each
		Vector4 exclusionShapes[MAX_WATER_EXCLUSIONS];   // x = shape, y = hull map layer
	} fs_water_t;

	typedef struct fs_underwater_t {
		Matrix4 invViewProjection; // logical, unprojects the scene depth
		Vector4 shallowColor;  // rgb = linear color, w = depth fade
		Vector4 deepColor;     // rgb = linear color, w = 1 when the scene depth is valid
		Vector4 light;         // rgb = light reaching the water besides the sky (linear)
		Vector4 envColor;      // rgb = sky tint (linear)
		Vector4 eyePos;        // xyz = eye
		Vector4 sceneRect;     // xy = view origin, zw = view size, in scene copy uv
	} fs_underwater_t;

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
			// the terrain layer uniforms are looked up by entity, so the sorted draw keeps it
			Entity entity;
			MeshComponent* mesh;
			PointsComponent* points;
			InstancedMeshComponent* instmesh;
			TerrainComponent* terrain;
			TilemapComponent* tilemap;
			Transform* transform;
			float distanceToCamera;
		};

		// opaque mesh of a 3D pass, drawn front to back so early depth rejects covered fragments
		struct OpaqueRenderData{
			Entity entity;
			MeshComponent* mesh;
			InstancedMeshComponent* instmesh;
			TerrainComponent* terrain;
			TilemapComponent* tilemap;
			Transform* transform;
			float distanceToCamera;
			uint64_t sortKey;
		};

		// water a camera pass draws
		struct WaterRenderData{
			Entity entity;
			WaterComponent* water;
			Transform* transform;
			bool eyeAbove;
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

		// editor-only, draws as if the scene had no fog
		bool disableFog = false;

		// editor-only, draws as if the camera were never inside a water
		bool disableUnderwater = false;

		// Editor-only override that suppresses the scene's fixed game resolution
		// (edit-mode viewports render native; play mode re-enables it). Defaults
		// to false and is never set at runtime, so exported games are unaffected.
		bool disableFixedResolution = false;

		// editor-only, holds the waves while the scene is stopped or paused
		bool waterPaused = false;

		static uint32_t pixelsWhite[64];
		static uint32_t pixelsBlack[64];
		static uint32_t pixelsNormal[64];

		static TextureRender emptyWhite;
		static TextureRender emptyArrayWhite;
		static TextureRender emptyBlack;
		static TextureRender emptyCubeBlack;
		static TextureRender emptyCubeWhite;
		static TextureRender emptyNormal;
		static TextureRender emptyShadowDepth;

		static bool emptyTexturesCreated;
		
		bool hasLights;
		bool hasShadows;
		// every light is directional: meshes load the 'Dlo' shader variant
		bool onlyDirectionalLights;
		bool hasFog;
		bool hasIBL;
		bool hasReflectionProbes;
		// a water samples the scene depth, so meshes need their depth shaders
		bool hasWaterDepth;
		bool hasMultipleCameras;
		// extra cameras the last frame drew, to catch the switch back to main only
		bool lastMultiCameraDraw;
		bool capturingReflectionProbe;
		// batch order currently baked into the shared points/instance arrays
		bool lastBatchSort;
		// scene mesh LOD switch the levels were built (or freed) for
		bool lastMeshLodEnabled = false;
		// pipelines the objects were loaded with, to catch a destination change
		uint16_t loadedPipelines;

		// Projected spotlight masks share one horizontal R8 atlas. Each punctual-light
		// array index maps directly to one fixed-size atlas tile.
		TextureRender spotMaskAtlas;
		std::array<SpotMaskAtlasEntry, MAX_LIGHTS> spotMaskAtlasEntries;
		std::vector<unsigned char> spotMaskAtlasPixels;
		bool spotMaskAtlasCreated;

		// one layer per water exclusion hull
		TextureRender waterExclusionMaps;
		int waterExclusionMapLayers;
		bool needUpdateWaterExclusionMaps;

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
			// the capture prefiltered into one roughness per mip level
			TextureRender filteredMap;
			bool needFilter = false;
		};

		std::unordered_map<Entity, std::unique_ptr<ReflectionProbeRuntime>> reflectionProbeRuntimes;

		// prefilter of the finished captures (probe_filter.frag)
		bool probeFilterLoaded;
		TextureRender probeSourceMap; // box-filtered mip chain of the capture
		TextureRender probeReduceMap; // scratch target while building the source chain
		int probeSourceSize;
		CameraRender probeFilterPassRender;
		ObjectRender probeFilterRender;
		std::shared_ptr<ShaderRender> probeFilterShader;
		fs_probe_filter_t fs_probe_filter;
		int probeFilterSlotParams;

		// Each mirror's reflection camera. Kept here so the authored component stays copiable.
		std::unordered_map<Entity, Entity> mirrorCameras;
		// Main camera position for the instance distance fade: the colour and depth passes must
		// fade against the same point or a shadow outlives its instance.
		Vector3 fadeEyePosition;
		// light component indices in the shader slots, sorted
		std::vector<size_t> activeLights;

		// culled instances of every instanced mesh for one view, uploaded once per frame
		struct InstanceView{
			std::vector<InstanceRenderData> data;
			ExternalBuffer buffer;
			size_t capacity = 0; // bytes of the GPU buffer
			bool needUpload = false;
		};
		InstanceView instanceViews[1 + MAX_SHADOW_ATLAS_SLOTS];
		bool instanceViewsDirty;

		// Consecutive sprites of a color pass that share pipeline and textures are drawn
		// as one: their vertices go to world space and the first sprite's render draws them.
		struct SpriteBatchItem{
			Entity entity;
			MeshComponent* mesh;
			Transform* transform;
		};
		// where a sprite's interleaved vertices keep each attribute
		struct SpriteLayout{
			unsigned int stride = 0;
			size_t positionOffset = 0;
			size_t colorOffset = 0;
			int texcoordOffset = -1;
		};
		struct SpriteBatch{
			bool active = false;
			bool depthPrepass = false;
			PipelineType pipType = PIP_DEFAULT;
			CameraComponent* camera = nullptr;
			Transform* cameraTransform = nullptr;
			std::vector<SpriteBatchItem> items;
			Submesh* previous = nullptr; // the sprite seen before, to tell when a run starts
			SpriteLayout layout; // of the first sprite, shared by the others
			std::vector<unsigned char> vertices;
			std::vector<uint16_t> indices;
			BufferRender vertexBuffer;
			BufferRender indexBuffer;
			// bytes of the GPU buffers and bytes this frame asked for
			size_t vertexCapacity = 0;
			size_t indexCapacity = 0;
			size_t vertexDemand = 0;
			size_t indexDemand = 0;
			// planes of this pass, numbered so settled sprites know when their culling still holds
			Plane frustumPlanes[6];
			float farClip = 0;
			uint64_t frustum = 0;
		};
		SpriteBatch spriteBatch;

		// batch data of a sprite whose mesh did not change, so frames draw it without reading the mesh
		struct SettledSprite{
			Entity entity = NULL_ENTITY;
			uint32_t state = 0;
			unsigned int vertexCount = 0;
			unsigned int indexCount = 0;
			bool culls = false;
			bool inside = false;
			uint32_t settledAt = 0; // in spriteUpdates
			uint64_t cullFrustum = 0; // the frustum inside was found for, 0 for none
			size_t meshIndex = 0; // where its MeshComponent was last found
			AABB aabb; // of the mesh, so moving only writes the mesh's worldAABB
			float lo[3] = {};
			float hi[3] = {};
			float positions[4][3] = {}; // local
			uint16_t indices[6] = {};
			unsigned char vertices[4 * 64]; // world positions, texture rect and color applied
		};
		// equal pipelines, bindings and vertex layout, so its sprites draw as one
		struct SpriteState{
			std::vector<uint32_t> key;
			SpriteLayout layout;
			unsigned int sprites = 0;
		};
		struct SpriteRun{
			Entity entity; // its render object draws the run
			uint32_t state;
			size_t vertexStart;
			size_t indexStart;
			unsigned int indexCount;
		};
		// settled sprites drawn since the last other draw, uploaded together
		struct SpriteSegment{
			std::vector<unsigned char> vertices;
			std::vector<uint16_t> indices;
			std::vector<SpriteRun> runs;
			// drawn from their meshes when the buffers are still too small
			std::vector<std::pair<size_t, Entity>> sprites;
		};
		std::vector<SettledSprite> settledSprites;
		// the settled sprite of each entity, -1 when none, in pages freed once none of theirs is
		// settled: entity ids only grow, so a plain array would grow with every sprite ever spawned
		struct SettledPage{
			int index[1024];
			unsigned int count = 0;
		};
		std::vector<std::unique_ptr<SettledPage>> settledPages;
		std::vector<SpriteState> spriteStates;
		std::vector<uint32_t> freeSpriteStates;
		std::map<std::vector<uint32_t>, uint32_t> spriteStateOf;
		SpriteSegment spriteSegment;
		Signature spriteLoopComponents; // ones the frame loops act on, which keep a sprite unsettled
		uint32_t spriteUpdates = 0;
		bool settledSkippedMVP = false; // the last update left the MVP of settled sprites as it was
		// by entity: how long a sprite written again soon after it settled waits to settle again
		struct SettleWait{
			uint32_t until = 0; // in spriteUpdates
			uint32_t length = 0;
		};
		std::unordered_map<Entity, SettleWait> settleWaits;

		// error a view hides at a distance: threshold * (perUnit * distance + constant)
		struct LodView{
			Vector3 origin;
			float perUnit = 0;   // perspective: units per pixel at distance 1
			float constant = 0;  // orthographic: units per pixel
			float threshold = 1;
			bool enabled = false;
		};
		LodView mainLodView;

		struct ShadowSlot{
			int slot;
			LightComponent* light;
			int cameraIndex;
		};

		// what one pass draws of a mesh
		struct MeshDraw{
			unsigned int instanceCount = 1;
			unsigned int instanceFirst = 0;
			unsigned int lod = 0;
		};
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
		shadow_projection_t shadowProjection;
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
		// packed opaque depth of this camera pass, or null
		TextureRender* currentSceneDepthTexture;
		bool depthPrePassRendered;
		// copy of the scene color drawn before the water, for the main camera
		Framebuffer sceneCopyFramebuffer;
		CameraRender resumePassRender; // continues the camera pass after the copy
		TextureRender* currentSceneCopy;
		Vector4 currentSceneCopyRect;
		// the water holding the main camera, which fades the scene copy
		WaterComponent* currentUnderwater;

		// engine-written custom uniforms: seconds since startup, sampled once per draw(),
		// and the size of the target being drawn (shadow slot, SSAO depth or camera color)
		float frameTime;
		Vector2 passResolution;

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
		CameraRender blitPassRender;       // drives the fullscreen blit passes
		ObjectRender blitRender;           // fullscreen blit.frag draw
		std::shared_ptr<ShaderRender> blitShader;
		fs_blit_t fs_blit;
		int blitSlotParams;

		// User post-process chain: forked fullscreen shaders run after the color (and
		// SSR) pass, ping-ponging between two buffers into the real destination.
		struct PostProcessRuntime{
			std::shared_ptr<ShaderRender> shader;
			ObjectRender render;
			int passIndex;                         // index in the scene chain (disabled passes skipped)
			uint16_t customId;                     // 0 = built-in passthrough
			CustomUniformBlock params;             // u_fs_postParams, slot -1 when the fork has none
			std::pair<int, int> slotSceneColor;
			std::pair<int, int> slotDepth;
			std::pair<int, int> slotGBuffer;
			std::pair<int, int> slotSSAO;
		};
		bool postProcessLoaded;
		bool postProcessNeedReload;
		bool postProcessNeedUpdateUniforms;    // rewrite pass values, no chain rebuild
		bool postProcessNeedsDepth;            // a pass samples the depth texture
		bool postProcessNeedsGBuffer;          // a pass samples the G-buffer (needs SSR)
		unsigned int postProcessWidth;
		unsigned int postProcessHeight;
		Framebuffer postProcessFramebuffer[2]; // ping-pong targets
		CameraRender postProcessPassRender;    // drives the offscreen post passes
		std::vector<PostProcessRuntime> postProcessPasses;

		static void changeLoaded(void* data);
		static void changeDestroy(void* data);
		// water can share its entity with a mesh, so it has its own callbacks
		static void changeWaterLoaded(void* data);
		static void changeWaterDestroy(void* data);

		static bool samplesCameraTarget(const CameraComponent& camera, const MeshComponent& mesh);
		static bool samplesCameraTarget(const CameraComponent& camera, const Texture& texture);
		bool isRenderingFlipped(const CameraComponent& camera) const;
		bool isFixedResolutionActive() const;
		void updateSwapchainRedirect();
		void updateMVP(size_t index, Transform& transform, CameraComponent& camera, Transform& cameraTransform);

		void createEmptyTextures();
		std::vector<size_t> chooseLights(const Vector3& eye, const std::vector<size_t>& inUse);
		void selectLights(const Vector3& eye);
		void applyViewLights(const Vector3& eye);
		void setLightSlot(int slot, size_t index);
		void checkLightsAndShadow();
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
		void loadProbeFilter();
		void destroyProbeFilter();
		void filterReflectionProbes();
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
		int getShadowReceiverPlanes(const CameraComponent& camera, const LightComponent& light, int cascade, Plane* planes);
		bool checkPBRFrabebufferUpdate(Material& material);
		bool checkPBRTextures(Material& material, bool receiveLights);
		bool loadPBRTextures(Material& material, ShaderData& shaderData, ObjectRender& render, bool receiveLights);
		void loadShadowTextures(ShaderData& shaderData, ObjectRender& render, bool receiveLights, bool receiveShadow);
		void updateShadowBindings();
		bool loadDepthTexture(Material& material, ShaderData& shaderData, ObjectRender& render);
		bool loadGBufferTextures(Material& material, ShaderData& shaderData, ObjectRender& render);
		// Every map of every layer is a slice of one array texture, so the whole terrain
		// costs a single bind slot. Kept here, not on the component, which stays copiable.
		struct TerrainDetailArray{
			TextureRender render;
			// what each slice was built from, in array order
			std::vector<std::string> sliceKeys;
			// how many leading slices hold layer colors (rounded up to whole blend maps)
			int colorSlices = 0;
			// where a layer's normal and packed surface slices landed, -1 when it has none
			std::array<int, MAX_TERRAIN_LAYERS> normalSlice;
			std::array<int, MAX_TERRAIN_LAYERS> surfaceSlice;
			// one sampler serves every slice, so its settings are part of what the cache holds
			TextureFilter minFilter = TextureFilter::LINEAR;
			TextureFilter magFilter = TextureFilter::LINEAR;
			TextureWrap wrapU = TextureWrap::REPEAT;
			TextureWrap wrapV = TextureWrap::REPEAT;
			bool failed = false;
		};
		std::unordered_map<Entity, TerrainDetailArray> terrainDetailArrays;

		TerrainDetailArray* getTerrainDetailArray(Entity entity, TerrainComponent& terrain);
		void destroyTerrainDetailArray(Entity entity);
		void applyTerrainLayersUniform(ObjectRender& render, int slot, Entity entity, TerrainComponent& terrain);
		bool loadTerrainTextures(Entity entity, TerrainComponent& terrain, ObjectRender& render, ShaderData& shaderData);
		bool loadTerrainHeightTexture(TerrainComponent& terrain, ObjectRender& render, ShaderData& shaderData);
		bool updateTerrainRenderTextures(Entity entity, TerrainComponent& terrain, MeshComponent& mesh);
		void updateAllTerrainRenderTextures();
		void updateInstanceBuffers();
		void updateSkinBuffers();
		bool canBatchSprite(const MeshComponent& mesh) const;
		bool batchSprite(Entity entity, MeshComponent& mesh, Transform& transform, CameraComponent& camera, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap);
		bool getSpriteLayout(MeshComponent& mesh, SpriteLayout& layout) const;
		void bakeSpriteVertices(unsigned char* vertices, unsigned int count, const SpriteLayout& layout, const Submesh& submesh) const;
		void appendSpriteGeometry(const SpriteBatchItem& item);
		void applySpriteBatchUniforms(ObjectRender& render, Submesh& submesh);
		bool drawSpriteBatch();
		void flushSpriteItems();
		void flushSpriteBatch();
		void prepareSpriteBatchBuffers();
		void settleSprite(Entity entity, Signature signature, MeshComponent& mesh, const Transform& transform);
		void unsettleSprite(Entity entity);
		void unsettleSprites();
		int settledIndex(Entity entity) const{
			const size_t page = entity >> 10;
			return (page < settledPages.size() && settledPages[page]) ? settledPages[page]->index[entity & 1023] : -1;
		}
		void setSettledIndex(Entity entity, int index);
		void placeSettledSprite(SettledSprite& sprite, const Transform& transform);
		void drawSettledSprite(SettledSprite& sprite, size_t index);
		void flushSpriteSegment();
		Rect getScissorRect(UILayoutComponent& layout, ImageComponent& img, Transform& transform, const Rect& passViewport);

		// mesh LOD
		struct LodAttribute{
			Buffer* buffer = nullptr;
			Attribute* attribute = nullptr;
		};
		struct LodAttributes{
			LodAttribute position;
			LodAttribute index;
			LodAttribute normal;
			LodAttribute uv;
		};
		void updateMeshLods(Entity entity, MeshComponent& mesh, bool rehash);
		static LodAttribute findSubmeshAttribute(Submesh& submesh, std::map<std::string, Buffer*>& buffers, AttributeType type);
		static bool findLodAttributes(Submesh& submesh, std::map<std::string, Buffer*>& buffers, LodAttributes& attributes);
		static uint64_t attributeHash(uint64_t seed, const LodAttribute& attribute);
		static bool readLodSource(const LodAttributes& attributes, MeshLodSource& source);
		void pollMeshLod(MeshComponent& mesh, Submesh& submesh);
		static bool releaseSubmeshLod(Submesh& submesh);
		void releaseMeshLods(MeshComponent& mesh);
		static bool isMeshLodPending(Submesh& submesh);
		void updateMeshLodErrors(MeshComponent& mesh);
		void updateMainLodView(CameraComponent& camera, Transform& cameraTransform);
		unsigned int selectMeshLod(const MeshComponent& mesh, const LodView* view, float distance, float worldScale) const;
		LodView shadowLodView(const LightComponent& light, int cameraIndex, int slot) const;
		std::vector<ShadowSlot> collectShadowSlots();

		// instance views
		void buildInstanceViews(CameraComponent& mainCamera, Transform& mainCameraTransform);
		void buildInstanceView(int viewIndex, InstancedMeshComponent& instmesh, MeshComponent& mesh, Transform& transform, const float cameraFar, const Plane frustumPlanes[6], const LodView& lodView, const Vector3& eye, const Vector3& origin, const Vector3& direction, bool keepOrder);
		bool selectMeshDraw(MeshComponent& mesh, Transform& transform, InstancedMeshComponent* instmesh, bool ownLod, int instanceView, const LodView* lodView, MeshDraw& draw);
		void bindInstances(ObjectRender& render, InstancedMeshComponent& instmesh, int instanceView, unsigned int first);
		void drawSubmeshGeometry(ObjectRender& render, MeshComponent& mesh, unsigned int submeshIndex, InstancedMeshComponent* instmesh, int instanceView, unsigned int lod, unsigned int instanceCount);

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
		// instanceView -1 draws every instance from the component buffer; lodView null is full detail
		bool drawMesh(Entity entity, MeshComponent& mesh, Transform& transform, CameraComponent& camera, Transform& camTransform, PipelineType pipType, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap, int terrainView = 0, int instanceView = -1, const LodView* lodView = nullptr);
		bool drawMeshDepth(MeshComponent& mesh, Transform& transform, const float cameraFar, const Plane frustumPlanes[6], vs_depth_t vsDepthParams, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap, bool forSSAO = false, PipelineType pipelineType = PIP_DEPTH, int instanceView = -1, const LodView* lodView = nullptr);
		void destroyMesh(Entity entity, MeshComponent& mesh, bool clearAssets = false);

		// SSAO
		void loadSSAO();
		void destroySSAO();
		bool ensureDepthPrePassFramebuffer(unsigned int width, unsigned int height);
		bool ensureSSAOFramebuffers(unsigned int width, unsigned int height);
		// sharedDepth != null reuses an existing packed-depth texture (the SSR G-buffer),
		// skipping the SSAO depth pre-pass; null runs the pre-pass
		void renderSSAO(CameraComponent& camera, TextureRender* sharedDepth = nullptr);

		// SSR
		void loadSSR();
		void destroySSR();
		bool ensureSceneColorFramebuffer(unsigned int width, unsigned int height);
		bool ensureSSRFramebuffers(unsigned int width, unsigned int height);
		void renderDepthPrePass(CameraComponent& camera); // shared depth for SSAO/SSR
		// G-buffer geometry pass for SSR: MRT packed depth + view-space normal/roughness/metallic
		bool ensureGBufferFramebuffer(unsigned int width, unsigned int height);
		void renderGBufferPass(CameraComponent& camera);
		bool drawMeshGBuffer(Entity entity, MeshComponent& mesh, Transform& transform, const float cameraFar, const Plane frustumPlanes[6], vs_gbuffer_t vsGBufferParams, bool hasLocalProbe, InstancedMeshComponent* instmesh, TerrainComponent* terrain, TilemapComponent* tilemap, int instanceView = -1, const LodView* lodView = nullptr);
		// destination == nullptr renders the composite to the swapchain (backbuffer)
		void renderSSR(CameraComponent& camera, FramebufferRender* destination);

		// fixed game resolution
		void loadBlit();
		void destroyBlit();
		bool ensureFixedResFramebuffer(unsigned int width, unsigned int height, TextureFilter filter);
		// draws source over destination (the swapchain when null), in viewport or the whole target
		void renderBlit(TextureRender* source, FramebufferRender* destination, const Rect* viewport);
		// the fullscreen blit draw alone, in the pass already started
		void drawBlit(TextureRender* source, PipelineType pipeline, bool flipY);
		bool ensureSceneCopyFramebuffer(unsigned int width, unsigned int height);
		void renderSceneCopy(Framebuffer* source);
		// upscales fixedResFramebuffer to the view rect
		void renderFixedResolutionBlit();

		// user post-process chain
		void loadPostProcess();
		void updatePostProcessUniforms();
		void destroyPostProcess();
		bool ensurePostProcessFramebuffers(unsigned int width, unsigned int height);
		// runs the chain from ping-pong buffer 0 into destination (swapchain when null)
		void renderPostProcess(FramebufferRender* destination);

		bool drawUI(UIComponent& ui, Transform& transform, PipelineType pipType);
		void destroyUI(Entity entity, UIComponent& ui);

		bool drawPoints(PointsComponent& points, Transform& transform, CameraComponent& camera, Transform& camTransform, PipelineType pipType);
		float getPointsViewportHeight(const CameraComponent& camera) const;
		float computePointsScale(const CameraComponent& camera, float viewportHeight) const;
		void destroyPoints(Entity entity, PointsComponent& points);

		bool drawLines(LinesComponent& lines, Transform& transform, Transform& camTransform, PipelineType pipType);
		void destroyLines(Entity entity, LinesComponent& lines);

		bool drawSky(SkyComponent& sky, PipelineType pipType);
		void destroySky(Entity entity, SkyComponent& sky);

		std::shared_ptr<TextureRender> getDefaultWaterNormalMap();
		void updateWater(Entity entity, WaterComponent& water, Transform& transform, uint16_t pipelines, double dt);
		void advanceWater(WaterComponent& water, float seconds);
		bool drawWater(Entity entity, WaterComponent& water, Transform& transform, CameraComponent& camera, Transform& camTransform, PipelineType pipType, bool mainCamera);
		void destroyWater(Entity entity, WaterComponent& water);
		static bool isBehindWater(const std::vector<WaterRenderData>& waters, const AABB& box);
		WaterComponent* findUnderwater(const Vector3& eye);
		bool loadUnderwater(WaterComponent& water);
		void updateWaterExclusions();
		void setWaterExclusionParams(const WaterComponent& water, const Vector3& eye, fs_water_t& params);
		// the fullscreen fade of the scene copy, in the pass already started
		void drawUnderwater(WaterComponent& water, CameraComponent& camera, Transform& cameraTransform);

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

		void setDisableFog(bool disableFog);

		void setDisableUnderwater(bool disableUnderwater);

		// Editor-only override (see member declaration). No reload needed: the
		// editor always renders through Engine::getFramebuffer(), so PIP_RTT is
		// already baked either way.
		void setDisableFixedResolution(bool disableFixedResolution);

		void setWaterPaused(bool waterPaused);
		// moves the waves by the update time since the last call; draw() does it after drawing
		void advanceWaters();

		// the depth pass alpha-tests the submesh (MASK, or the legacy AUTO + textureShadow);
		// the editor derives export keys from it, so the rule lives in one place
		static bool usesAlphaMask(const Material& material, bool textureShadow);

		// water surface offset over the world point (x, z) as drawn, for Water::getHeight and buoyancy
		static Vector3 getWaterSurfaceOffset(const WaterComponent& water, float x, float z, Vector3* normal = nullptr);

		static bool isInsideWaterExclusion(const WaterExclusionComponent& exclusion, const Transform& transform, const Vector3& point);

		// near/far as rendered, perspective keeps 0 < near < far
		static Vector2 getCameraNearFar(const CameraComponent& camera);

		// copies the stacked scene composite to the swapchain (Engine::endCompositeFramebuffer)
		void presentFramebufferToSwapchain(Framebuffer* source);

		// pipelines this scene's cameras need; objects reload when it changes
		uint16_t getScenePipelines() const;

		bool loadMesh(Entity entity, MeshComponent& mesh, uint16_t pipelines, InstancedMeshComponent* instmesh, TerrainComponent* terrain);
		bool loadPoints(Entity entity, PointsComponent& points, uint16_t pipelines);
		bool loadLines(Entity entity, LinesComponent& lines, uint16_t pipelines);
		bool loadUI(Entity entity, UIComponent& ui, uint16_t pipelines, bool isText);
		bool loadSky(Entity entity, SkyComponent& sky, uint16_t pipelines);
		bool loadWater(Entity entity, WaterComponent& water, uint16_t pipelines);

		void updateFramebuffer(CameraComponent& camera);
		void updateTransform(Transform& transform, Entity entity);
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
		bool isInsideCamera(const float cameraFar, const Plane frustumPlanes[6], const Vector3& center, const float& radius);

		void needReloadPoints();
		void needReloadLines();
		void needReloadMeshes();
		// true while the sprite draws from data the render system kept, see settleSprite
		bool isSpriteSettled(Entity entity) const{
			return settledIndex(entity) >= 0;
		}
		void updateSpriteBatch(Entity entity);
		void needReloadPostProcess();
		void needUpdatePostProcessUniforms();
		// components inheriting the default shader of this type rewrite their custom blocks
		void needUpdateDefaultShaderUniforms(ShaderType type);
		void needReloadUIs();
		void needReloadSky();
		void needReloadWater();
		void prepareMeshForDataReload(Entity entity, MeshComponent& mesh);

		bool isAllLoaded() const;
		void getLoadCount(size_t& loaded, size_t& total) const;
		bool hasPendingMeshLods() const;
	
		void load() override;
		void draw() override;
		void destroy() override;
		void update(double dt) override;

		void onComponentAdded(Entity entity, ComponentId componentId) override;
		void onComponentRemoved(Entity entity, ComponentId componentId) override;
	};

}

#endif //RENDERSYSTEM_H
