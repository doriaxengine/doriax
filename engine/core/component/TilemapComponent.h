// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef TILEMAP_COMPONENT_H
#define TILEMAP_COMPONENT_H

// A tile quad is 4 vertices and the index buffer is 16-bit, so no more than this
// many tiles can be addressed by a single tilemap mesh.
#define MAX_TILEMAP_RENDERED_TILES 16383

// Tiles are grouped into a chunk grid to be frustum-culled per chunk. The grid is
// sized to keep chunks near TILEMAP_CHUNK_TARGET_TILES, and stays 1x1 below
// TILEMAP_MIN_CHUNKED_TILES, where splitting a map costs more in draw calls than
// the offscreen vertices it saves.
#define TILEMAP_MIN_CHUNKED_TILES 256
#define TILEMAP_CHUNK_TARGET_TILES 64
#define MAX_TILEMAP_CHUNKS_PER_AXIS 16

#include "util/HybridArray.h"
#include "math/AABB.h"
#include "Engine.h"
#include <vector>

namespace doriax{

    struct TileRectData{
        std::string name;
        int submeshId = -1;
        Rect rect;
        bool collision = true; // solid in a Body2D shape with a tilemap source
    };
    
    struct TileData{
        std::string name;
        int rectId;
        Vector2 position;
        float width;
        float height;
    };

    // One spatial chunk of a submesh: a contiguous run of indices inside that
    // submesh's index range, frustum-culled as a unit at draw time so an offscreen
    // part of a large map is never submitted.
    struct TilemapChunk{
        AABB aabb;      // local-space bounds of the chunk's tiles
        AABB worldAABB; // aabb through the model matrix, refreshed by RenderSystem
        unsigned int offset = 0; // first index, relative to the submesh index range
        unsigned int count = 0;  // number of indices
    };

    struct DORIAX_API TilemapComponent{
        unsigned int width = 0;
        unsigned int height = 0;

        bool automaticFlipY = true;
        bool flipY = false;

        float textureScaleFactor = 0.0;

        unsigned int reserveTiles = 10;
        unsigned int renderedTiles = 0;
        
        HybridArray<TileRectData, MAX_TILEMAP_TILESRECT> tilesRect;
        unsigned int numTilesRect = 0;

        HybridArray<TileData, MAX_TILEMAP_TILES> tiles;
        unsigned int numTiles = 0;

        // Chunks of every submesh, grouped by submesh: submesh s owns
        // [chunkStart[s], chunkStart[s+1]).
        // using std::vector because the chunk count is derived from the tile count
        std::vector<TilemapChunk> chunks;
        std::vector<unsigned int> chunkStart;

        bool needUpdateTilemap = true;
    };

}

#endif //TILEMAP_COMPONENT_H