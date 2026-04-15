//
// (c) 2026 Eduardo Doria.
//

#ifndef TERRAIN_H
#define TERRAIN_H

#include "Mesh.h"

namespace doriax{

    class DORIAX_API Terrain: public Mesh{

    public:
        Terrain(Scene* scene);
        Terrain(Scene* scene, Entity entity);
        virtual ~Terrain();

        bool createTerrain();

        void setHeightMap(const std::string& path);
        void setHeightMap(Framebuffer* framebuffer);

        void setBlendMap(const std::string& path);
        void setBlendMap(Framebuffer* framebuffer);

        void setTextureDetailRed(const std::string& path);
        void setTextureDetailGreen(const std::string& path);
        void setTextureDetailBlue(const std::string& path);

        void setSize(float size);
        float getSize() const;

        void setMaxHeight(float maxHeight);
        float getMaxHeight() const;

        void setResolution(int resolution);
        int getResolution() const;

        void setTextureBaseTiles(int textureBaseTiles);
        int getTextureBaseTiles() const;

        void setTextureDetailTiles(int textureDetailTiles);
        int getTextureDetailTiles() const;

        void setRootGridSize(int rootGridSize);
        int getRootGridSize() const;

        void setLevels(int levels);
        int getLevels() const;
    };

}

#endif //TERRAIN_H