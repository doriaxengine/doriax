//
// (c) 2026 Eduardo Doria.
//

#ifndef TILEMAP_H
#define TILEMAP_H

#include "Mesh.h"

namespace doriax{

    class DORIAX_API Tilemap: public Mesh{

    public:
        Tilemap(Scene* scene);
        Tilemap(Scene* scene, Entity entity);
        virtual ~Tilemap();

        bool createTilemap();

        int findRectByString(const std::string& name);
        int findTileByString(const std::string& name);

        void setTextureScaleFactor(float textureScaleFactor);
        float getTextureScaleFactor() const;

        void addRect(int id, const std::string& name, const std::string& texture, TextureFilter texFilter, Rect rect);
        void addRect(int id, const std::string& name, const std::string& texture, Rect rect);
        void addRect(int id, const std::string& name, Rect rect);
        void addRect(const std::string& name, float x, float y, float width, float height);
        void addRect(float x, float y, float width, float height);
        void addRect(Rect rect);
        void removeRect(int id);
        void removeRect(const std::string& name);
        void clearRects();

        TileRectData& getRect(int id);
        TileRectData& getRect(const std::string& name);

        void addTile(int id, const std::string& name, int rectId, Vector2 position, float width, float height);
        void addTile(const std::string& name, int rectId, Vector2 position, float width, float height);
        void addTile(int rectId, Vector2 position, float width, float height);
        void addTile(const std::string& name, const std::string& rectString, Vector2 position, float width, float height);
        void addTile(const std::string& rectString, Vector2 position, float width, float height);
        void removeTile(int id);
        void removeTile(const std::string& name);
        void clearTiles();

        TileData& getTile(int id);
        TileData& getTile(const std::string& name);

        void setReserveTiles(unsigned int reserveTiles);
        unsigned int getReserveTiles() const;

        unsigned int getWidth();
        unsigned int getHeight();

        void clearAll();

    };

}

#endif //TERRAIN_H