// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef TERRAIN_COMPONENT_H
#define TERRAIN_COMPONENT_H

#define MAX_TERRAINGRID 16
// CDLOD node selection is per-view: index 0 is the main camera (also reused by the
// shadow depth pass), 1..N-1 are render-to-texture cameras (mirror reflections,
// scene captures), each selecting + morphing its own node cut. Extra RTT cameras
// beyond this cap fall back to the main camera's selection (view 0).
#define MAX_TERRAIN_VIEWS 4

// The quadtree materializes rootGridSize^2 * (4^levels - 1)/3 nodes, so "levels"
// grows the node count exponentially. These bounds keep the node vector from
// requesting an impossible allocation (a large "levels" would abort with bad_alloc).
// MAX_TERRAIN_LEVELS also keeps getTerrainGridArraySize's 4^i math within size_t.
#define MAX_TERRAIN_LEVELS 20
#define MAX_TERRAIN_NODES 2000000u

// A blend map weights three layers in its RGB. Alpha is not a fourth weight: saved
// maps are opaque, so it would read as full strength.
#define MAX_TERRAIN_BLENDMAPS 3
#define MAX_TERRAIN_LAYERS (MAX_TERRAIN_BLENDMAPS * 3)

// One array texture holds every layer map, so a bigger source is downscaled into it
// instead of resizing every other slice up with it.
#define MAX_TERRAIN_DETAIL_SIZE 2048

#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"
#include "texture/Material.h"
#include "ecs/Entity.h"
#include "Engine.h"

#include <string>
#include <vector>

namespace doriax{

    struct TerrainNode{
        //-----u_vs_terrainNodeParams
        Vector2 position = Vector2(0, 0);
        float size = 0;
        float currentRange = 0;
        float resolution = 0; //int
        uint8_t _pad_20[12];
        //-----

        size_t childs[4];
        bool hasChilds = false;

        float maxHeight = 0;
        float minHeight = 0;
    };

    // Per-view CDLOD state. Index 0 is the main camera (also reused by the shadow
    // depth pass); 1..N-1 are render-to-texture cameras (mirror reflections, scene
    // captures), each selecting and morphing its own node cut. Grouping the per-view
    // buffers in a struct (instead of a 2-D array of InterleavedBuffer) keeps MSVC's
    // code generation from crashing when the ECS pool instantiates TerrainComponent.
    struct TerrainView{
        // 0 = fullRes, 1 = halfRes; selected and uploaded independently once per frame
        InterleavedBuffer nodesbuffer[2];
        // per-view morph origin, paired with this view's node selection
        Vector3 nodesEyePos;
        bool needUpdateNodesBuffer = false;
    };

    // One painted surface. The maps are separate inputs so an existing material can fill
    // them without repacking images; the renderer packs them into its own array slices.
    struct TerrainSurfaceLayer{
        // Off is the historical layer: it blends its color and leaves every other property
        // to the terrain material, with the texture alpha carrying the blending height.
        bool pbr = false;

        Texture colorTexture;
        Texture normalTexture;
        Texture roughnessTexture; //green channel, or the only channel of a grayscale map
        Texture metallicTexture; //blue channel, same fallback
        Texture occlusionTexture; //red channel
        Texture heightTexture; //blending height only, never geometry

        Vector4 colorFactor = Vector4(1, 1, 1, 1); //linear tint; PBR layers ignore alpha
        float normalStrength = 1;
        float roughnessFactor = 1;
        float metallicFactor = 0;
        float occlusionStrength = 1;

        Vector2 uvScale = Vector2(1, 1); //multiplies the shared detail tiling
        Vector2 uvOffset;

        bool operator == (const TerrainSurfaceLayer& other) const{
            return pbr == other.pbr &&
                   colorTexture == other.colorTexture &&
                   normalTexture == other.normalTexture &&
                   roughnessTexture == other.roughnessTexture &&
                   metallicTexture == other.metallicTexture &&
                   occlusionTexture == other.occlusionTexture &&
                   heightTexture == other.heightTexture &&
                   colorFactor == other.colorFactor &&
                   normalStrength == other.normalStrength &&
                   roughnessFactor == other.roughnessFactor &&
                   metallicFactor == other.metallicFactor &&
                   occlusionStrength == other.occlusionStrength &&
                   uvScale == other.uvScale &&
                   uvOffset == other.uvOffset;
        }

        bool operator != (const TerrainSurfaceLayer& other) const{
            return !(*this == other);
        }
    };

    // A scattered mesh layer painted over the terrain. The editor authors its density map;
    // instances are resolved from that map instead of being stored.
    struct TerrainFoliageLayer{
        std::string meshPath;
        Texture densityMap;

        float density = 1; //instances per square world unit where the map is fully painted
        float minScale = 0.8f;
        float maxScale = 1.2f;
        float rotationJitter = 1; //share of a full turn of random yaw
        float alignToNormal = 0; //0 stands instances upright, 1 lays them along the surface
        float minSlope = 0; //degrees
        float maxSlope = 35;
        float minHeight = 0; //normalized against the terrain maxHeight
        float maxHeight = 1;
        float drawDistance = 50;
        float minDrawDistance = 0; // nearer ones are skipped, so a layer copy can be its far detail level
        unsigned int seed = 0;
        bool castShadows = true; // off spares grass-scale scatter the shadow cascades
        float windSway = 0; // tips bend this far at 10 m/s of scene wind
    };

    struct DORIAX_API TerrainComponent{
        // per-view CDLOD node selection (see TerrainView). Extra RTT cameras beyond
        // MAX_TERRAIN_VIEWS fall back to the main camera's selection (view 0).
        TerrainView views[MAX_TERRAIN_VIEWS];

        Texture heightMap;
        // blendMaps[m] channel c weights surfaceLayers[m * 3 + c]
        std::vector<Texture> blendMaps;
        std::vector<TerrainSurfaceLayer> surfaceLayers;

        std::vector<TerrainFoliageLayer> foliageLayers;
        // Runtime quality setting: scales every foliage layer's draw distances (1 = as authored)
        float foliageDistanceScale = 1.0f;

        bool autoSetRanges = true;
        bool heightMapLoaded = false;

        Vector2 offset;
        std::vector<float> ranges;

        //using std::vector to avoid chkstk.asm stack overflow error in Windows
        std::vector<TerrainNode> nodes;
        unsigned int numNodes = 0;

        size_t grid[MAX_TERRAINGRID]; //root nodes

        //-----u_vs_terrainParams
        // eyePos is the morph origin for the view currently being drawn; it is set
        // from views[view].nodesEyePos right before the uniform upload (keep this
        // block's memory layout intact — it is uploaded as a contiguous struct).
        Vector3 eyePos;
        float terrainSize = 200;
        float maxHeight = 5;
        float resolution = 32; //int
        float textureBaseTiles = 1; //int
        float textureDetailTiles = 20; //int
        //-----

        int rootGridSize = 2;
        int levels = 6;

        bool needUpdateTerrain = true;
        bool needUpdateTexture = false;
        bool needUpdateFoliage = true;
    };

    // The one place a material becomes a layer, shared by the engine setter and the editor
    inline TerrainSurfaceLayer terrainLayerFromMaterial(const Material& material, const TerrainSurfaceLayer& previous){
        TerrainSurfaceLayer layer;
        layer.pbr = true;
        layer.colorTexture = material.baseColorTexture;
        layer.colorFactor = material.baseColorFactor;
        layer.normalTexture = material.normalTexture;
        layer.roughnessTexture = material.metallicRoughnessTexture;
        layer.metallicTexture = material.metallicRoughnessTexture;
        layer.occlusionTexture = material.occlusionTexture;
        layer.roughnessFactor = material.roughnessFactor;
        layer.metallicFactor = material.metallicFactor;
        // Tiling is the layer's own, and no material carries it
        layer.uvScale = previous.uvScale;
        layer.uvOffset = previous.uvOffset;
        return layer;
    }

    // Every map a layer can hold, so callers walking terrain assets cannot miss one
    template<typename F>
    inline void forEachTerrainLayerTexture(TerrainSurfaceLayer& layer, F&& fn){
        fn(layer.colorTexture);
        fn(layer.normalTexture);
        fn(layer.roughnessTexture);
        fn(layer.metallicTexture);
        fn(layer.occlusionTexture);
        fn(layer.heightTexture);
    }

    // A layer with its own surface, or its own tiling, needs the wider terrain shader
    inline bool hasTerrainSurfaceLayers(const TerrainComponent& terrain){
        for (const TerrainSurfaceLayer& layer : terrain.surfaceLayers){
            if (layer.pbr || layer.uvScale != Vector2(1, 1) || layer.uvOffset != Vector2(0, 0)){
                return true;
            }
        }
        return false;
    }

}

#endif //TERRAIN_COMPONENT_H
