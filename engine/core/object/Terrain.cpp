//
// (c) 2026 Eduardo Doria.
//

#include "Terrain.h"
#include "util/Color.h"

#include "subsystem/MeshSystem.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

Terrain::Terrain(Scene* scene): Mesh(scene){
    addComponent<TerrainComponent>();
}

Terrain::Terrain(Scene* scene, Entity entity): Mesh(scene, entity){
}

Terrain::~Terrain(){
}

bool Terrain::createTerrain(){
    TerrainComponent& terrain = getComponent<TerrainComponent>();
    MeshComponent& mesh = getComponent<MeshComponent>();

    return scene->getSystem<MeshSystem>()->createOrUpdateTerrain(terrain, mesh);
}

void Terrain::setHeightMap(const std::string& path){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.heightMap.setPath(path);

    terrain.needUpdateTexture = true;
}

void Terrain::setHeightMap(Framebuffer* framebuffer){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.heightMap.setFramebuffer(framebuffer);

    terrain.needUpdateTexture = true;
}

void Terrain::setBlendMap(const std::string& path){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.blendMap.setPath(path);

    terrain.needUpdateTexture = true;
}

void Terrain::setBlendMap(Framebuffer* framebuffer){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.blendMap.setFramebuffer(framebuffer);

    terrain.needUpdateTexture = true;
}

void Terrain::setTextureDetailRed(const std::string& path){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.textureDetailRed.setPath(path);

    terrain.needUpdateTexture = true;
}

void Terrain::setTextureDetailGreen(const std::string& path){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.textureDetailGreen.setPath(path);

    terrain.needUpdateTexture = true;
}

void Terrain::setTextureDetailBlue(const std::string& path){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.textureDetailBlue.setPath(path);

    terrain.needUpdateTexture = true;
}

void Terrain::setSize(float size){
    TerrainComponent& terrain = getComponent<TerrainComponent>();
    Transform& transform = getComponent<Transform>();

    if (terrain.terrainSize != size){
        terrain.terrainSize = size;

        terrain.needUpdateTerrain = true;
        transform.needUpdate = true;
    }
}

float Terrain::getSize() const{
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    return terrain.terrainSize;
}

void Terrain::setMaxHeight(float maxHeight){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    if (terrain.maxHeight != maxHeight){
        terrain.maxHeight = maxHeight;
        terrain.needUpdateTerrain = true;
    }
}

float Terrain::getMaxHeight() const{
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    return terrain.maxHeight;
}

void Terrain::setResolution(int resolution){
    TerrainComponent& terrain = getComponent<TerrainComponent>();
    MeshComponent& mesh = getComponent<MeshComponent>();
    Transform& transform = getComponent<Transform>();

    if (terrain.resolution != resolution){
        terrain.resolution = resolution;

        terrain.needUpdateTerrain = true;
        mesh.needReload = true;
        transform.needUpdate = true;
    }
}

int Terrain::getResolution() const{
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    return (int)terrain.resolution;
}

void Terrain::setTextureBaseTiles(int textureBaseTiles){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.textureBaseTiles = textureBaseTiles; // no need update terrain
}

int Terrain::getTextureBaseTiles() const{
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    return (int)terrain.textureBaseTiles;
}

void Terrain::setTextureDetailTiles(int textureDetailTiles){
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    terrain.textureDetailTiles = textureDetailTiles; // no need update terrain
}

int Terrain::getTextureDetailTiles() const{
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    return (int)terrain.textureDetailTiles;
}

void Terrain::setRootGridSize(int rootGridSize){
    TerrainComponent& terrain = getComponent<TerrainComponent>();
    MeshComponent& mesh = getComponent<MeshComponent>();
    Transform& transform = getComponent<Transform>();

    if (terrain.rootGridSize != rootGridSize){
        terrain.rootGridSize = rootGridSize;

        terrain.needUpdateTerrain = true;
        mesh.needReload = true;
        transform.needUpdate = true;
    }
}

int Terrain::getRootGridSize() const{
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    return (int)terrain.rootGridSize;
}

void Terrain::setLevels(int levels){
    TerrainComponent& terrain = getComponent<TerrainComponent>();
    MeshComponent& mesh = getComponent<MeshComponent>();
    Transform& transform = getComponent<Transform>();

    if (terrain.levels != levels){
        terrain.levels = levels;

        terrain.needUpdateTerrain = true;
        mesh.needReload = true;
        transform.needUpdate = true;
    }
}

int Terrain::getLevels() const{
    TerrainComponent& terrain = getComponent<TerrainComponent>();

    return (int)terrain.levels;
}