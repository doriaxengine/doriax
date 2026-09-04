// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "TerrainMapPatchCmd.h"

#include <utility>

using namespace doriax;

editor::TerrainMapPatchCmd::TerrainMapPatchCmd(Project* project, uint32_t sceneId, Entity entity, const TerrainMapRef& ref, TerrainMapPatch patch){
    this->project = project;
    this->sceneId = sceneId;
    this->entity = entity;
    this->ref = ref;
    this->patch = std::move(patch);
}

void editor::TerrainMapPatchCmd::apply(const std::vector<unsigned char>& regionPixels, bool restoreModifiedState){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene->isEntityCreated(entity)){
        return;
    }
    TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(entity);
    if (!terrain){
        return;
    }

    // A recreate or delete swaps the map for a new file and carries its own snapshot command,
    // so the patch applies only while the map it was cut from is still bound.
    Texture* texture = TerrainMapUtils::findTexture(*terrain, ref);
    if (!texture || texture->getPath(0) != patch.path || !TerrainMapUtils::hasLoadedData(*texture)){
        return;
    }

    TextureData& data = texture->getData();
    if (data.getWidth() != patch.mapWidth || data.getHeight() != patch.mapHeight ||
        data.getChannels() != patch.channels || data.getColorFormat() != patch.colorFormat ||
        !patch.region.fitsIn(patch.mapWidth, patch.mapHeight)){
        return;
    }

    const int bytesPerChannel = TextureData::getBytesPerChannel(patch.colorFormat);
    const int bytesPerTexel = patch.channels * bytesPerChannel;
    const size_t mapBytes = static_cast<size_t>(patch.mapWidth) * static_cast<size_t>(patch.mapHeight) * static_cast<size_t>(bytesPerTexel);
    unsigned char* pixels = static_cast<unsigned char*>(data.getData());
    if (!pixels || data.getSize() < mapBytes){
        return;
    }

    if (!TerrainMapUtils::writeRegion(pixels, patch.mapWidth, bytesPerTexel, patch.region, regionPixels)){
        return;
    }

    TerrainMapUtils::writeFile(project, patch.path, patch.mapWidth, patch.mapHeight, patch.channels, bytesPerChannel,
                               std::vector<unsigned char>(pixels, pixels + mapBytes));
    TerrainMapUtils::refresh(sceneProject, entity, ref);

    if (project->isEntityInBundle(sceneId, entity)){
        project->bundlePropertyChanged(sceneId, entity, ComponentType::TerrainComponent, {TerrainMapUtils::getPropertyName(ref)});
    }

    if (restoreModifiedState){
        sceneProject->isModified = wasModified;
    }else{
        sceneProject->isModified = true;
    }
}

bool editor::TerrainMapPatchCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        return false;
    }

    wasModified = sceneProject->isModified;
    apply(patch.afterPixels, false);
    return true;
}

void editor::TerrainMapPatchCmd::undo(){
    apply(patch.beforePixels, true);
}

bool editor::TerrainMapPatchCmd::mergeWith(Command* otherCommand){
    return false;
}
