//
// (c) 2026 Eduardo Doria.
//

#include "Tilemap.h"
#include "subsystem/MeshSystem.h"

using namespace doriax;

Tilemap::Tilemap(Scene* scene): Mesh(scene){
    addComponent<TilemapComponent>();
}

Tilemap::Tilemap(Scene* scene, Entity entity): Mesh(scene, entity){
}

Tilemap::~Tilemap(){

}

bool Tilemap::createTilemap(){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();
    MeshComponent& mesh = getComponent<MeshComponent>();

    return scene->getSystem<MeshSystem>()->createOrUpdateTilemap(tilemap, mesh);
}

int Tilemap::findRectByString(const std::string& name){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    for (int i = 0; i < (int)tilemap.numTilesRect; i++){
        if (tilemap.tilesRect[i].name == name){
            return i;
        }
    }

    return -1;
}

int Tilemap::findTileByString(const std::string& name){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    for (int i = 0; i < (int)tilemap.numTiles; i++){
        if (tilemap.tiles[i].name == name){
            return i;
        }
    }

    return -1;
}

void Tilemap::setTextureScaleFactor(float textureScaleFactor){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();
    
    if (tilemap.textureScaleFactor != textureScaleFactor){
        tilemap.textureScaleFactor = textureScaleFactor;
        tilemap.needUpdateTilemap = true;
    }
}

float Tilemap::getTextureScaleFactor() const{
    TilemapComponent& tilemap = getComponent<TilemapComponent>();
    
    return tilemap.textureScaleFactor;
}

void Tilemap::addRect(int id, const std::string& name, const std::string& texture, TextureFilter texFilter, Rect rect){
    MeshComponent& mesh = getComponent<MeshComponent>();
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    int submeshId = 0;

    if (id < 0){
        Log::error("Error adding rect with id %i", id);
        return;
    }

    if (!tilemap.tilesRect.validIndex(id)){
        Log::error("Cannot add rect with id %i, max valid id is %i", id, tilemap.tilesRect.size() - 1);
        return;
    }

    if (mesh.numSubmeshes >= mesh.submeshes.size()){
        Log::error("Cannot add submesh for rect id %i, max submeshes is %i", id, mesh.submeshes.size());
        return;
    }

    if (!texture.empty()) {
        bool textureFound = false;
        for (int s = 0; s < mesh.numSubmeshes; s++){
            if (texture == mesh.submeshes[s].material.baseColorTexture.getPath()) {
                textureFound = true;
                submeshId = s;
                break;
            }
        }

        if (!textureFound) {
            submeshId = mesh.numSubmeshes;
            mesh.submeshes[submeshId].material.baseColorTexture.setPath(texture);
            mesh.submeshes[submeshId].material.baseColorTexture.setMinFilter(texFilter);
            mesh.submeshes[submeshId].material.baseColorTexture.setMagFilter(texFilter);
            mesh.submeshes[submeshId].needUpdateTexture = true;
            mesh.numSubmeshes++;
        }
    }

    tilemap.tilesRect[id] = {name, submeshId, rect};
    if ((unsigned int)(id + 1) > tilemap.numTilesRect){
        tilemap.numTilesRect = id + 1;
    }
}

void Tilemap::addRect(int id, const std::string& name, const std::string& texture, Rect rect){
    addRect(id, name, texture, TextureFilter::LINEAR, rect);
}

void Tilemap::addRect(int id, const std::string& name, Rect rect){
    addRect(id, name, "", rect);
}

void Tilemap::addRect(const std::string& name, float x, float y, float width, float height){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    int id = (int)tilemap.numTilesRect;
    addRect(id, name, Rect(x, y, width, height));
}

void Tilemap::addRect(float x, float y, float width, float height){
    addRect("", x, y, width, height);
}

void Tilemap::addRect(Rect rect){
    addRect(rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight());
}

void Tilemap::removeRect(int id){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    if (id >= 0 && (unsigned int)id < tilemap.numTilesRect){
        // Shift remaining rects left
        for (unsigned int i = id; i + 1 < tilemap.numTilesRect; i++){
            tilemap.tilesRect[i] = tilemap.tilesRect[i + 1];
        }
        tilemap.numTilesRect--;
        tilemap.tilesRect[tilemap.numTilesRect] = TileRectData{};

        // Remap tile rectIds
        for (int i = 0; i < (int)tilemap.numTiles; i++){
            if (tilemap.tiles[i].rectId == id){
                tilemap.tiles[i].rectId = -1; // invalidated
            }else if (tilemap.tiles[i].rectId > id){
                tilemap.tiles[i].rectId--;
            }
        }
    }else{
        Log::error("Error removing rect with id %i", id);
    }
}

void Tilemap::removeRect(const std::string& name){
    int rect = findRectByString(name);
    if (rect >= 0)
        removeRect(rect);
}

void Tilemap::clearRects(){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
        tilemap.tilesRect[i] = TileRectData{};
    }
    tilemap.numTilesRect = 0;
}

TileRectData& Tilemap::getRect(int id){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    if (id >= 0 && (unsigned int)id < tilemap.numTilesRect){
        return tilemap.tilesRect[id];
    }else{
        throw std::out_of_range("error getting rect tilemap");
    }
}

TileRectData& Tilemap::getRect(const std::string& name){
    int rect = findRectByString(name);
    if (rect >= 0)
        return getRect(rect);

    throw std::out_of_range("error getting rect tilemap");
}

void Tilemap::addTile(int id, const std::string& name, int rectId, Vector2 position, float width, float height){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    if (tilemap.tiles.validIndex(id)){
        tilemap.tiles[id] = {name, rectId, position, width, height};
        if ((unsigned int)(id + 1) > tilemap.numTiles){
            tilemap.numTiles = id + 1;
        }
    }else{
        Log::error("Error adding tile with id %i", id);
    }

    tilemap.needUpdateTilemap = true;
}

void Tilemap::addTile(const std::string& name, int rectId, Vector2 position, float width, float height){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    int id = (int)tilemap.numTiles;
    addTile(id, name, rectId, position, width, height);
}

void Tilemap::addTile(int rectId, Vector2 position, float width, float height){
    addTile("", rectId, position, width, height);
}

void Tilemap::addTile(const std::string& name, const std::string& rectString, Vector2 position, float width, float height){
    addTile(name, findRectByString(rectString), position, width, height);
}

void Tilemap::addTile(const std::string& rectString, Vector2 position, float width, float height){
    addTile("", findRectByString(rectString), position, width, height);
}

void Tilemap::removeTile(int id){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    if (id >= 0 && (unsigned int)id < tilemap.numTiles){
        for (unsigned int i = id; i + 1 < tilemap.numTiles; i++){
            tilemap.tiles[i] = tilemap.tiles[i + 1];
        }
        tilemap.numTiles--;
        tilemap.tiles[tilemap.numTiles] = TileData{};
    }else{
        Log::error("Error removing tile with id %i", id);
    }

    tilemap.needUpdateTilemap = true;
}

void Tilemap::removeTile(const std::string& name){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    for (int id = (int)tilemap.numTiles - 1; id >= 0; id--){
        if (tilemap.tiles[id].name == name){
            removeTile(id);
        }
    }
}

void Tilemap::clearTiles(){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    for (unsigned int i = 0; i < tilemap.numTiles; i++){
        tilemap.tiles[i] = TileData{};
    }
    tilemap.numTiles = 0;
}

TileData& Tilemap::getTile(int id){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    if (id >= 0 && (unsigned int)id < tilemap.numTiles){
        return tilemap.tiles[id];
    }else{
        throw std::out_of_range("error getting tile");
    }
}

TileData& Tilemap::getTile(const std::string& name){
    int tile = findTileByString(name);
    if (tile >= 0)
        return getTile(tile);

    throw std::out_of_range("error getting tile");
}

void Tilemap::setReserveTiles(unsigned int reserveTiles){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    tilemap.reserveTiles = reserveTiles;
}

unsigned int Tilemap::getReserveTiles() const{
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    return tilemap.reserveTiles;
}

unsigned int Tilemap::getWidth(){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    return tilemap.width;
}

unsigned int Tilemap::getHeight(){
    TilemapComponent& tilemap = getComponent<TilemapComponent>();

    return tilemap.height;
}

void Tilemap::clearAll(){
    clearTiles();
    clearRects();
}