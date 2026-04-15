//
// (c) 2026 Eduardo Doria.
//

#ifndef TILEMAP_COMPONENT_H
#define TILEMAP_COMPONENT_H

#include "util/HybridArray.h"
#include "Engine.h"

namespace doriax{

    struct TileRectData{
        std::string name;
        int submeshId = -1;
        Rect rect;
    };
    
    struct TileData{
        std::string name;
        int rectId;
        Vector2 position;
        float width;
        float height;
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

        bool needUpdateTilemap = true;
    };

}

#endif //TILEMAP_COMPONENT_H