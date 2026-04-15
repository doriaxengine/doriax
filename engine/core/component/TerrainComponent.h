//
// (c) 2026 Eduardo Doria.
//

#ifndef TERRAIN_COMPONENT_H
#define TERRAIN_COMPONENT_H

#define MAX_TERRAINGRID 16

#include "buffer/InterleavedBuffer.h"
#include "buffer/IndexBuffer.h"
#include "texture/Material.h"
#include "Engine.h"

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

    struct DORIAX_API TerrainComponent{
        // 0 for fullRes and 1 for halfRes
        InterleavedBuffer nodesbuffer[2];

        Texture heightMap;
        Texture blendMap;
        Texture textureDetailRed;
        Texture textureDetailGreen;
        Texture textureDetailBlue;

        bool autoSetRanges = true;
        bool heightMapLoaded = false;

        Vector2 offset;
        std::vector<float> ranges;

        //using std::vector to avoid chkstk.asm stack overflow error in Windows
        std::vector<TerrainNode> nodes;
        unsigned int numNodes = 0;

        size_t grid[MAX_TERRAINGRID]; //root nodes

        //-----u_vs_terrainParams
        Vector3 eyePos;
        float terrainSize = 200;
        float maxHeight = 10;
        float resolution = 32; //int
        float textureBaseTiles = 1; //int
        float textureDetailTiles = 20; //int
        //-----

        int rootGridSize = 2;
        int levels = 6;

        bool needUpdateTerrain = true;
        bool needUpdateTexture = false;
        bool needUpdateNodesBuffer = false;
    };
    
}

#endif //TERRAIN_COMPONENT_H
