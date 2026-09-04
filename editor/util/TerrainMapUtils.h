// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"

#include <algorithm>
#include <string>
#include <vector>

namespace doriax::editor{

    enum class TerrainMapTarget{
        HeightMap,
        BlendMap,
        DensityMap
    };

    // Names one editable map. Only DensityMap uses "layer", to pick the foliage layer.
    struct TerrainMapRef{
        TerrainMapTarget target = TerrainMapTarget::HeightMap;
        int layer = 0;

        TerrainMapRef() = default;
        TerrainMapRef(TerrainMapTarget target, int layer = 0): target(target), layer(layer){}

        bool operator==(const TerrainMapRef& other) const{
            return target == other.target && layer == other.layer;
        }
    };

    // Inclusive texel bounds of what a stroke wrote.
    struct TerrainMapRegion{
        int minX = 0;
        int minY = 0;
        int maxX = -1;
        int maxY = -1;

        bool empty() const{
            return maxX < minX || maxY < minY;
        }

        int width() const{
            return empty() ? 0 : maxX - minX + 1;
        }

        int height() const{
            return empty() ? 0 : maxY - minY + 1;
        }

        bool fitsIn(int mapWidth, int mapHeight) const{
            return !empty() && minX >= 0 && minY >= 0 && maxX < mapWidth && maxY < mapHeight;
        }

        void merge(int x0, int y0, int x1, int y1){
            if (empty()){
                minX = x0;
                minY = y0;
                maxX = x1;
                maxY = y1;
                return;
            }
            minX = std::min(minX, x0);
            minY = std::min(minY, y0);
            maxX = std::max(maxX, x1);
            maxY = std::max(maxY, y1);
        }
    };

    // Undo payload for a stroke: the map rect it changed, kept instead of a whole map copy.
    // Only applies while the texture still points at this file with this geometry.
    struct TerrainMapPatch{
        std::string path;
        ColorFormat colorFormat = ColorFormat::RGBA;
        int mapWidth = 0;
        int mapHeight = 0;
        int channels = 0;
        TerrainMapRegion region;
        std::vector<unsigned char> beforePixels;
        std::vector<unsigned char> afterPixels;
    };

    // Terrain map storage shared by the terrain editor and its undo commands: both bind, read,
    // persist and refresh the same height, blend and foliage density maps.
    class TerrainMapUtils{

    public:
        // Null when a density map is asked for a foliage layer the terrain no longer has.
        static Texture* findTexture(TerrainComponent& terrain, const TerrainMapRef& ref);
        static std::string getPropertyName(const TerrainMapRef& ref);
        static bool hasLoadedData(Texture& texture);
        static bool writeFile(Project* project, const std::string& relativePath, int width, int height, int channels, int bytesPerChannel, const std::vector<unsigned char>& pixels);
        static void refresh(SceneProject* sceneProject, Entity entity, const TerrainMapRef& ref);

        // Copies an inclusive texel rect out of a full map buffer.
        static std::vector<unsigned char> copyRegion(const unsigned char* pixels, int mapWidth, int bytesPerTexel, const TerrainMapRegion& region);

        // A patch shorter than its own rect writes nothing.
        static bool writeRegion(unsigned char* pixels, int mapWidth, int bytesPerTexel, const TerrainMapRegion& region, const std::vector<unsigned char>& regionPixels);
    };

}
