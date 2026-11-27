// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TilemapEditCmd.h"

#include "Catalog.h"
#include "render/SceneRender.h"

#include <algorithm>
#include <string>
#include <utility>

using namespace doriax;

namespace {

bool sameTile(const TileData& a, const TileData& b){
    return a.name == b.name && a.rectId == b.rectId && a.position == b.position && a.width == b.width && a.height == b.height;
}

std::vector<std::string> changedTileProps(const std::vector<TileData>& before, const std::vector<TileData>& after){
    std::vector<std::string> props;
    if (before.size() != after.size()){
        props.push_back("numTiles");
    }

    for (size_t i = 0; i < std::max(before.size(), after.size()); i++){
        if (i < before.size() && i < after.size() && sameTile(before[i], after[i])){
            continue;
        }
        std::string prefix = "tiles[" + std::to_string(i) + "]";
        props.push_back(prefix + ".name");
        props.push_back(prefix + ".rectId");
        props.push_back(prefix + ".position");
        props.push_back(prefix + ".width");
        props.push_back(prefix + ".height");
    }
    return props;
}

}

editor::TilemapTilesCmd::TilemapTilesCmd(Project* project, uint32_t sceneId, Entity entity,
                                         std::vector<TileData> beforeTiles,
                                         std::vector<TileData> afterTiles,
                                         bool wasModifiedBefore)
    : project(project),
      sceneId(sceneId),
      entity(entity),
      beforeTiles(std::move(beforeTiles)),
      afterTiles(std::move(afterTiles)),
      wasModified(wasModifiedBefore) {
}

void editor::TilemapTilesCmd::apply(const std::vector<TileData>& targetTiles, bool restoreModifiedState){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene || !sceneProject->scene->isEntityCreated(entity)){
        return;
    }

    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap){
        return;
    }

    const unsigned int oldCount = tilemap->numTiles;
    const unsigned int newCount = static_cast<unsigned int>(targetTiles.size());

    for (unsigned int i = 0; i < newCount; i++){
        if (!tilemap->tiles.validIndex(static_cast<int>(i))){
            break;
        }
        tilemap->tiles[i] = targetTiles[i];
    }
    for (unsigned int i = newCount; i < oldCount; i++){
        if (tilemap->tiles.validIndex(static_cast<int>(i))){
            tilemap->tiles[i] = TileData{};
        }
    }
    tilemap->numTiles = newCount;

    Catalog::updateEntity(sceneProject->scene, entity, UpdateFlags_Tilemap | UpdateFlags_Body2D_Shapes);

    if (sceneProject->sceneRender &&
        sceneProject->sceneRender->getSelectedTileEntity() == entity &&
        sceneProject->sceneRender->getSelectedTileIndex() >= static_cast<int>(tilemap->numTiles)){
        sceneProject->sceneRender->clearTileSelection();
    }

    // Strokes paint live, so diff the command states rather than the live tilemap
    if (project->isEntityInBundle(sceneId, entity)){
        std::vector<std::string> props = changedTileProps(beforeTiles, afterTiles);
        if (!props.empty()){
            project->bundlePropertyChanged(sceneId, entity, ComponentType::TilemapComponent, std::move(props));
        }
    }

    sceneProject->needUpdateRender = true;
    sceneProject->isModified = restoreModifiedState ? wasModified : true;
}

bool editor::TilemapTilesCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return false;
    }

    apply(afterTiles, false);
    return true;
}

void editor::TilemapTilesCmd::undo(){
    apply(beforeTiles, true);
}

bool editor::TilemapTilesCmd::mergeWith(Command* otherCommand){
    (void)otherCommand;
    return false;
}

editor::TilemapRectCollisionCmd::TilemapRectCollisionCmd(Project* project, uint32_t sceneId, Entity entity,
                                                         std::vector<TileRectCollisionChange> changes,
                                                         bool wasModifiedBefore)
    : project(project),
      sceneId(sceneId),
      entity(entity),
      changes(std::move(changes)),
      wasModified(wasModifiedBefore) {
}

void editor::TilemapRectCollisionCmd::apply(bool useAfter, bool restoreModifiedState){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene || !sceneProject->scene->isEntityCreated(entity)){
        return;
    }

    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap){
        return;
    }

    std::vector<std::string> props;
    for (const TileRectCollisionChange& ch : changes){
        if (ch.rectIndex < 0 || static_cast<unsigned int>(ch.rectIndex) >= tilemap->numTilesRect){
            continue;
        }
        tilemap->tilesRect[ch.rectIndex].collision = useAfter ? ch.after : ch.before;
        props.push_back("tilesRect[" + std::to_string(ch.rectIndex) + "].collision");
    }

    Catalog::updateEntity(sceneProject->scene, entity, UpdateFlags_Tilemap | UpdateFlags_Body2D_Shapes);

    if (!props.empty() && project->isEntityInBundle(sceneId, entity)){
        project->bundlePropertyChanged(sceneId, entity, ComponentType::TilemapComponent, std::move(props));
    }

    sceneProject->needUpdateRender = true;
    sceneProject->isModified = restoreModifiedState ? wasModified : true;
}

bool editor::TilemapRectCollisionCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || changes.empty()){
        return false;
    }

    apply(true, false);
    return true;
}

void editor::TilemapRectCollisionCmd::undo(){
    apply(false, true);
}

bool editor::TilemapRectCollisionCmd::mergeWith(Command* otherCommand){
    (void)otherCommand;
    return false;
}
