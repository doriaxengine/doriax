// (c) Eduardo Doria
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

#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"
#include "texture/Material.h"
#include "ecs/Entity.h"
#include "Engine.h"

#include <string>

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
        
        float visible = false;
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
        float drawDistance = 50;
        unsigned int seed = 0;
    };

    // One instanced entity per chunk, so each is culled on its own AABB. Slots are recycled as
    // the ring follows the camera: a chunk that stays in it keeps its mesh and its instances.
    struct TerrainFoliageChunk{
        Entity entity = NULL_ENTITY;
        int chunkX = 0;
        int chunkZ = 0;
        bool assigned = false; //false until the slot holds the resolve of the coordinate above
        bool meshLoaded = false;
        unsigned int loadedCapacity = 0; //what the buffer holds, lagging maxInstances until the reload
    };

    // Derived state for one layer, rebuilt from the layer rather than serialized.
    struct TerrainFoliageInstances{
        std::vector<TerrainFoliageChunk> chunks; //(2*radius+1)^2 grid, indexed by coordinate modulo its side
        std::string loadedMeshPath;
        bool loadFailed = false;
        float chunkSize = 0; //the size the slot coordinates are in
        bool needUpdate = true;
    };

    struct DORIAX_API TerrainComponent{
        // per-view CDLOD node selection (see TerrainView). Extra RTT cameras beyond
        // MAX_TERRAIN_VIEWS fall back to the main camera's selection (view 0).
        TerrainView views[MAX_TERRAIN_VIEWS];

        Texture heightMap;
        Texture blendMap;
        Texture textureDetailRed;
        Texture textureDetailGreen;
        Texture textureDetailBlue;

        std::vector<TerrainFoliageLayer> foliageLayers;
        std::vector<TerrainFoliageInstances> foliageInstances;

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
    
}

#endif //TERRAIN_COMPONENT_H
