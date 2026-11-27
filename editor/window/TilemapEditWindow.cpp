// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TilemapEditWindow.h"

#include "Backend.h"
#include "Catalog.h"
#include "Theme.h"
#include "command/CommandHandle.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/PropertyCmd.h"
#include "command/type/TilemapEditCmd.h"
#include "external/IconsFontAwesome6.h"
#include "render/SceneRender.h"
#include "util/UIUtils.h"
#include "window/Widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

using namespace doriax;
using namespace doriax::editor;

namespace {

ImVec2 commandButtonSize(){
    const float size = ImGui::GetFrameHeight() * 1.5f;
    return ImVec2(size, size);
}

void toolbarGroupGap(){
    ImGui::SameLine(0.0f, Theme::dpi(14.0f));
}

bool beginTilemapProperties(const char* id){
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)){
        return false;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, std::min(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x * 0.4f));
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void tilemapPropertyRow(const char* label, const char* tooltip = nullptr){
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip){
        ImGui::SetItemTooltip("%s", tooltip);
    }
    ImGui::TableSetColumnIndex(1);
}

float paletteFooterReserve(){
    return ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
}

uint32_t hashCell(int x, int y, uint32_t seed){
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

std::string toLowerString(const std::string& input){
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool rectMatches(const Rect& a, const Rect& b){
    return std::fabs(a.getX() - b.getX()) < 0.01f &&
           std::fabs(a.getY() - b.getY()) < 0.01f &&
           std::fabs(a.getWidth() - b.getWidth()) < 0.01f &&
           std::fabs(a.getHeight() - b.getHeight()) < 0.01f;
}

Texture* getSubmeshTexture(MeshComponent* meshComp, int submeshId){
    if (!meshComp || submeshId < 0 || static_cast<unsigned int>(submeshId) >= meshComp->numSubmeshes){
        return nullptr;
    }
    Texture* tex = &meshComp->submeshes[submeshId].material.baseColorTexture;
    return tex->empty() ? nullptr : tex;
}

// Same rects on the same textures, so a rect index means the same tile on both tilemaps
bool sameTileset(Scene* scene, Entity entity, const TilemapComponent& tilemap, const MeshComponent* meshComp){
    TilemapComponent* other = (scene && scene->isEntityCreated(entity)) ? scene->findComponent<TilemapComponent>(entity) : nullptr;
    if (!other || other->numTilesRect != tilemap.numTilesRect){
        return false;
    }
    for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
        if (other->tilesRect[i].submeshId != tilemap.tilesRect[i].submeshId || !rectMatches(other->tilesRect[i].rect, tilemap.tilesRect[i].rect)){
            return false;
        }
    }

    const MeshComponent* otherMesh = scene->findComponent<MeshComponent>(entity);
    const unsigned int numSubmeshes = meshComp ? meshComp->numSubmeshes : 0;
    if ((otherMesh ? otherMesh->numSubmeshes : 0) != numSubmeshes){
        return false;
    }
    for (unsigned int s = 0; s < numSubmeshes; s++){
        if (otherMesh->submeshes[s].material.baseColorTexture.getPath(0) != meshComp->submeshes[s].material.baseColorTexture.getPath(0)){
            return false;
        }
    }
    return true;
}

std::vector<std::pair<int, int>> buildBresenhamLine(int x0, int y0, int x1, int y1){
    std::vector<std::pair<int, int>> cells;
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int x = x0;
    int y = y0;
    const int maxSteps = std::min(4096, dx + dy + 2);
    for (int step = 0; step < maxSteps; step++){
        cells.emplace_back(x, y);
        if (x == x1 && y == y1){
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy){
            err -= dy;
            x += sx;
        }
        if (e2 < dx){
            err += dx;
            y += sy;
        }
    }
    return cells;
}

// Cells a rectangle fill paints, negative ones skipped, in 64 bits for huge drags
int64_t rectFillCellCount(int x0, int y0, int x1, int y1){
    const int64_t width = static_cast<int64_t>(std::max(x0, x1)) - std::max(0, std::min(x0, x1)) + 1;
    const int64_t height = static_cast<int64_t>(std::max(y0, y1)) - std::max(0, std::min(y0, y1)) + 1;
    return (width > 0 && height > 0) ? width * height : 0;
}

int positiveMod(int v, int m){
    if (m <= 0){
        return 0;
    }
    int r = v % m;
    return r < 0 ? r + m : r;
}

void drawFloatingBadge(ImDrawList* drawList, const ImVec2& anchor, const char* text, ImU32 textColor){
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 bMin(anchor.x, anchor.y - ts.y - Theme::dpi(8.0f));
    const ImVec2 bMax(bMin.x + ts.x + Theme::dpi(10.0f), bMin.y + ts.y + Theme::dpi(6.0f));
    drawList->AddRectFilled(bMin, bMax, IM_COL32(20, 20, 26, 215), Theme::dpi(4.0f));
    drawList->AddText(ImVec2(bMin.x + Theme::dpi(5.0f), bMin.y + Theme::dpi(3.0f)), textColor, text);
}

} // namespace

editor::TilemapEditWindow::TilemapEditWindow(Project* project)
    : project(project) {
}

editor::TilemapEditWindow::~TilemapEditWindow(){
}

void editor::TilemapEditWindow::showTooltip(const char* text, ImGuiHoveredFlags flags){
    if (ImGui::IsItemHovered(flags)){
        ImGui::SetTooltip("%s", text);
    }
}

bool editor::TilemapEditWindow::iconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec2& size){
    std::string label = "##" + std::string(id);

    if (selected){
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    }

    bool clicked = Widgets::iconButton(label.c_str(), icon, size);

    if (selected){
        ImGui::PopStyleColor(3);
    }

    showTooltip(tooltip, ImGuiHoveredFlags_AllowWhenDisabled);
    return clicked;
}

void editor::TilemapEditWindow::drawRectTooltip(int rectId, const TileRectData& rectData){
    ImGui::BeginTooltip();
    ImGui::Text("[%d] %s", rectId, rectData.name.empty() ? "(unnamed)" : rectData.name.c_str());
    ImGui::TextDisabled("%.0f x %.0f  •  Collision: %s",
                        rectData.rect.getWidth(), rectData.rect.getHeight(),
                        rectData.collision ? "Solid" : "Passable");
    ImGui::EndTooltip();
}

void editor::TilemapEditWindow::removeTileAt(TilemapComponent& tilemap, unsigned int index){
    if (index >= tilemap.numTiles){
        return;
    }
    for (unsigned int j = index; j + 1 < tilemap.numTiles; j++){
        tilemap.tiles[j] = tilemap.tiles[j + 1];
    }
    tilemap.numTiles--;
    tilemap.tiles[tilemap.numTiles] = TileData{};
}

std::vector<TileData> editor::TilemapEditWindow::snapshotTiles(const TilemapComponent& tilemap){
    std::vector<TileData> out;
    out.reserve(tilemap.numTiles);
    for (unsigned int i = 0; i < tilemap.numTiles; i++){
        out.push_back(tilemap.tiles[i]);
    }
    return out;
}

bool editor::TilemapEditWindow::tilesEqual(const std::vector<TileData>& a, const std::vector<TileData>& b){
    if (a.size() != b.size()){
        return false;
    }
    for (size_t i = 0; i < a.size(); i++){
        if (a[i].rectId != b[i].rectId ||
            a[i].position != b[i].position ||
            a[i].width != b[i].width ||
            a[i].height != b[i].height ||
            a[i].name != b[i].name){
            return false;
        }
    }
    return true;
}

void editor::TilemapEditWindow::notifyTilemapChanged(SceneProject* sceneProject, Entity entity){
    if (!sceneProject || !sceneProject->scene){
        return;
    }
    Catalog::updateEntity(sceneProject->scene, entity, UpdateFlags_Tilemap | UpdateFlags_Body2D_Shapes);
    sceneProject->needUpdateRender = true;
}

Command* editor::TilemapEditWindow::buildApplySliceCmd(Project* project, uint32_t sceneId, Entity entity,
                                                       const TextureSlicerToolDialog::SliceResult& result,
                                                       int* outFirstNewRectIndex){
    SceneProject* sceneProject = project ? project->getScene(sceneId) : nullptr;
    if (!sceneProject || !sceneProject->scene || !sceneProject->scene->isEntityCreated(entity)){
        return nullptr;
    }
    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap){
        return nullptr;
    }

    struct KeptRect{
        std::string name;
        int submeshId;
        Vector4 rect;
        bool collision;
    };
    std::vector<KeptRect> kept;
    std::vector<unsigned int> replacedRects;
    std::vector<int> rectRemap(tilemap->numTilesRect, -1); // old rect index to new, -1 when gone

    for (unsigned int i = 0; i < tilemap->numTilesRect; i++){
        const TileRectData& rd = tilemap->tilesRect[i];
        if (rd.submeshId != result.submeshId){
            rectRemap[i] = static_cast<int>(kept.size());
            kept.push_back({rd.name, rd.submeshId,
                Vector4(rd.rect.getX(), rd.rect.getY(), rd.rect.getWidth(), rd.rect.getHeight()),
                rd.collision});
        }else{
            replacedRects.push_back(i);
        }
    }

    const int firstNewRect = static_cast<int>(kept.size());
    if (outFirstNewRectIndex){
        *outFirstNewRectIndex = firstNewRect;
    }

    // A replaced rect follows the new rect with the same bounds and passes on its collision flag
    std::vector<int> newRectSource(result.rects.size(), -1);
    bool anyBoundsMatch = false;
    for (unsigned int oldIndex : replacedRects){
        for (size_t k = 0; k < result.rects.size(); k++){
            if (rectMatches(tilemap->tilesRect[oldIndex].rect, result.rects[k].rect)){
                rectRemap[oldIndex] = firstNewRect + static_cast<int>(k);
                if (newRectSource[k] < 0){
                    newRectSource[k] = static_cast<int>(oldIndex);
                }
                anyBoundsMatch = true;
                break;
            }
        }
    }
    // A new grid with no matching bounds keeps the slice order instead
    if (!anyBoundsMatch){
        for (size_t i = 0; i < replacedRects.size() && i < result.rects.size(); i++){
            rectRemap[replacedRects[i]] = firstNewRect + static_cast<int>(i);
        }
    }

    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
    const ComponentType cpType = ComponentType::TilemapComponent;
    const unsigned int newTotal = static_cast<unsigned int>(kept.size() + result.rects.size());
    unsigned int writeIndex = 0;

    for (size_t i = 0; i < kept.size(); i++, writeIndex++){
        std::string prefix = "tilesRect[" + std::to_string(writeIndex) + "]";
        multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, prefix + ".name", kept[i].name);
        multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, prefix + ".submeshId", kept[i].submeshId);
        multiCmd->addPropertyCmd<Vector4>(project, sceneId, entity, cpType, prefix + ".rect", kept[i].rect);
        multiCmd->addPropertyCmd<bool>(project, sceneId, entity, cpType, prefix + ".collision", kept[i].collision);
    }

    for (size_t i = 0; i < result.rects.size(); i++, writeIndex++){
        const bool collision = (newRectSource[i] >= 0) ? tilemap->tilesRect[newRectSource[i]].collision : true;
        std::string prefix = "tilesRect[" + std::to_string(writeIndex) + "]";
        multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, prefix + ".name", result.rects[i].name);
        multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, prefix + ".submeshId", result.submeshId);
        multiCmd->addPropertyCmd<Vector4>(project, sceneId, entity, cpType, prefix + ".rect",
            Vector4(result.rects[i].rect.getX(), result.rects[i].rect.getY(),
                    result.rects[i].rect.getWidth(), result.rects[i].rect.getHeight()));
        multiCmd->addPropertyCmd<bool>(project, sceneId, entity, cpType, prefix + ".collision", collision);
    }

    for (unsigned int i = newTotal; i < tilemap->numTilesRect; i++){
        std::string prefix = "tilesRect[" + std::to_string(i) + "]";
        multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, cpType, prefix + ".name", std::string(""));
        multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType, prefix + ".submeshId", -1);
        multiCmd->addPropertyCmd<Vector4>(project, sceneId, entity, cpType, prefix + ".rect", Vector4(0, 0, 0, 0));
        multiCmd->addPropertyCmd<bool>(project, sceneId, entity, cpType, prefix + ".collision", true);
    }

    multiCmd->addPropertyCmd<unsigned int>(project, sceneId, entity, cpType, "numTilesRect", newTotal);

    // Placed tiles follow their rect, so they keep showing the same image
    for (unsigned int i = 0; i < tilemap->numTiles; i++){
        const int rectId = tilemap->tiles[i].rectId;
        if (rectId < 0 || static_cast<unsigned int>(rectId) >= tilemap->numTilesRect || rectRemap[rectId] == rectId){
            continue;
        }
        multiCmd->addPropertyCmd<int>(project, sceneId, entity, cpType,
            "tiles[" + std::to_string(i) + "].rectId", rectRemap[rectId]);
    }

    multiCmd->setNoMerge();
    return multiCmd;
}

SceneProject* editor::TilemapEditWindow::findSceneProject(Scene* scene) const{
    if (!project || !scene){
        return nullptr;
    }
    for (SceneProject& sceneProject : project->getScenes()){
        if (sceneProject.scene == scene){
            return &sceneProject;
        }
    }
    return nullptr;
}

SceneProject* editor::TilemapEditWindow::getTargetSceneProject() const{
    if (!project || selectedSceneId == NULL_PROJECT_SCENE){
        return nullptr;
    }
    return project->getScene(selectedSceneId);
}

bool editor::TilemapEditWindow::updateTargetFromSelection(){
    if (!project){
        selectedSceneId = NULL_PROJECT_SCENE;
        selectedEntity = NULL_ENTITY;
        return false;
    }

    uint32_t sceneId = project->getSelectedSceneForProperties();
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject){
        sceneProject = project->getSelectedScene();
    }
    if (!sceneProject || !sceneProject->scene){
        selectedSceneId = NULL_PROJECT_SCENE;
        selectedEntity = NULL_ENTITY;
        paintActive = false;
        return false;
    }

    std::vector<Entity> selected = project->getSelectedEntities(sceneProject->id);
    if (selected.size() == 1 && sceneProject->scene->findComponent<TilemapComponent>(selected[0])){
        if (selectedSceneId != sceneProject->id || selectedEntity != selected[0]){
            cancelStroke();
            finishCollisionDrag();
            selectedSceneId = sceneProject->id;
            selectedEntity = selected[0];
            hasLastPaintedCell = false;
            sheetFitPending = true;
        }
        return true;
    }

    cancelStroke();
    finishCollisionDrag();
    selectedSceneId = NULL_PROJECT_SCENE;
    selectedEntity = NULL_ENTITY;
    paintActive = false;
    return false;
}

// Follows the selection and checks the brush against it before anything paints with it
bool editor::TilemapEditWindow::refreshTarget(){
    updateTargetFromSelection();
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        paintActive = false;
        cancelStroke();
        finishCollisionDrag();
        return false;
    }
    ensureValidSelection(sceneProject->scene->getComponent<TilemapComponent>(selectedEntity),
                         sceneProject->scene->findComponent<MeshComponent>(selectedEntity));
    return true;
}

// Selecting is enough, the window follows the selection (Properties' scene included) next frame
void editor::TilemapEditWindow::selectTilemapEntity(SceneProject* sceneProject, Entity entity){
    project->clearAllSelections(sceneProject->id);
    project->addSelectedEntity(sceneProject->id, entity);
    project->setSelectedSceneForProperties(sceneProject->id);
}

bool editor::TilemapEditWindow::hasValidTarget(SceneProject* sceneProject) const{
    SceneProject* targetScene = sceneProject ? sceneProject : getTargetSceneProject();
    return targetScene &&
           targetScene->scene &&
           targetScene->playState == ScenePlayState::STOPPED &&
           selectedEntity != NULL_ENTITY &&
           targetScene->scene->isEntityCreated(selectedEntity) &&
           targetScene->scene->findComponent<TilemapComponent>(selectedEntity) != nullptr &&
           targetScene->scene->findComponent<Transform>(selectedEntity) != nullptr;
}

void editor::TilemapEditWindow::ensureValidSelection(const TilemapComponent& tilemap, const MeshComponent* meshComp){
    // The brush holds rect indices, so it only carries over to a tilemap with the same tileset
    if (brushSceneId != selectedSceneId || brushEntity != selectedEntity){
        SceneProject* brushScene = project->getScene(brushSceneId);
        if (!brushScene || !sameTileset(brushScene->scene, brushEntity, tilemap, meshComp)){
            resetBrush();
        }
        brushSceneId = selectedSceneId;
        brushEntity = selectedEntity;
    }

    const int numSubmeshes = meshComp ? std::max(1, static_cast<int>(meshComp->numSubmeshes)) : 1;
    selectedSubmesh = std::clamp(selectedSubmesh, 0, numSubmeshes - 1);

    const int numRects = static_cast<int>(tilemap.numTilesRect);
    if (numRects <= 0){
        selectedRectId = -1;
        stampCells.clear();
        stampCols = 1;
        stampRows = 1;
        variantRectIds.clear();
        return;
    }

    stampCells.erase(std::remove_if(stampCells.begin(), stampCells.end(), [numRects](const TileStampCell& c){
        return c.rectId < 0 || c.rectId >= numRects;
    }), stampCells.end());

    variantRectIds.erase(std::remove_if(variantRectIds.begin(), variantRectIds.end(), [numRects](int id){
        return id < 0 || id >= numRects;
    }), variantRectIds.end());

    if (selectedRectId < 0 || selectedRectId >= numRects){
        int fallback = 0;
        for (int i = 0; i < numRects; i++){
            if (tilemap.tilesRect[i].submeshId == selectedSubmesh){
                fallback = i;
                break;
            }
        }
        selectSingleRect(fallback, tilemap);
    }else if (stampCells.empty()){
        selectSingleRect(selectedRectId, tilemap);
    }
    // selectSingleRect follows the rect's submeshId, which can point past the mesh
    selectedSubmesh = std::clamp(selectedSubmesh, 0, numSubmeshes - 1);
}

Vector2 editor::TilemapEditWindow::getEffectiveCellSize(const TilemapComponent& tilemap) const{
    if (!autoCellSize){
        return Vector2(std::max(1.0f, customCellWidth), std::max(1.0f, customCellHeight));
    }

    if (selectedRectId >= 0 && static_cast<unsigned int>(selectedRectId) < tilemap.numTilesRect){
        const Rect& r = tilemap.tilesRect[selectedRectId].rect;
        if (r.getWidth() > 0.0f && r.getHeight() > 0.0f){
            return Vector2(r.getWidth(), r.getHeight());
        }
    }

    for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
        const Rect& r = tilemap.tilesRect[i].rect;
        if (r.getWidth() > 0.0f && r.getHeight() > 0.0f){
            return Vector2(r.getWidth(), r.getHeight());
        }
    }

    for (unsigned int i = 0; i < tilemap.numTiles; i++){
        if (tilemap.tiles[i].width > 0.0f && tilemap.tiles[i].height > 0.0f){
            return Vector2(tilemap.tiles[i].width, tilemap.tiles[i].height);
        }
    }

    return Vector2(32.0f, 32.0f);
}

bool editor::TilemapEditWindow::raycastTilemapPlane(Scene* scene, Entity entity, const Ray& ray, Vector2& outLocalPos) const{
    Transform* transform = scene ? scene->findComponent<Transform>(entity) : nullptr;
    if (!transform){
        return false;
    }

    Vector3 origin = transform->modelMatrix * Vector3(0.0f, 0.0f, 0.0f);
    Vector3 normal = transform->modelMatrix * Vector3(0.0f, 0.0f, 1.0f) - origin;
    if (normal.length() < 1e-6f){
        normal = Vector3(0.0f, 0.0f, 1.0f);
    }else{
        normal.normalize();
    }

    RayReturn rr = ray.intersects(Plane(normal, origin));
    if (!rr){
        return false;
    }

    Vector3 localHit = transform->modelMatrix.inverse() * rr.point;
    outLocalPos = Vector2(localHit.x, localHit.y);
    return true;
}

bool editor::TilemapEditWindow::worldToScreen(Camera* camera, const Vector3& worldPos, const ImVec2& imageMin, const ImVec2& imageSize, ImVec2& outScreen) const{
    if (!camera || imageSize.x <= 0.0f || imageSize.y <= 0.0f){
        return false;
    }

    Vector4 clip = camera->getViewProjectionMatrix() * Vector4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    if (std::fabs(clip.w) < 1e-6f || clip.w < 0.0f){
        return false;
    }

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    outScreen.x = imageMin.x + (ndcX + 1.0f) * 0.5f * imageSize.x;
    outScreen.y = imageMin.y + (1.0f - ndcY) * 0.5f * imageSize.y;
    return true;
}

void editor::TilemapEditWindow::resetBrush(){
    selectedSubmesh = 0;
    selectedRectId = -1;
    stampCells.clear();
    stampCols = 1;
    stampRows = 1;
    variantRectIds.clear();
    randomizeBrush = false;
}

void editor::TilemapEditWindow::selectSingleRect(int rectId, const TilemapComponent& tilemap){
    if (rectId < 0 || static_cast<unsigned int>(rectId) >= tilemap.numTilesRect){
        return;
    }
    selectedRectId = rectId;
    stampCells.clear();
    stampCells.push_back({0, 0, rectId});
    stampCols = 1;
    stampRows = 1;
    variantRectIds.clear();
    variantRectIds.push_back(rectId);

    if (tilemap.tilesRect[rectId].submeshId >= 0){
        selectedSubmesh = tilemap.tilesRect[rectId].submeshId;
    }
}

void editor::TilemapEditWindow::toggleVariantRect(int rectId, const TilemapComponent& tilemap){
    if (rectId < 0 || static_cast<unsigned int>(rectId) >= tilemap.numTilesRect){
        return;
    }
    auto it = std::find(variantRectIds.begin(), variantRectIds.end(), rectId);
    if (it != variantRectIds.end()){
        if (variantRectIds.size() > 1){
            variantRectIds.erase(it);
        }
    }else{
        variantRectIds.push_back(rectId);
    }

    selectedRectId = variantRectIds.front();
    stampCells.clear();
    stampCells.push_back({0, 0, selectedRectId});
    stampCols = 1;
    stampRows = 1;
    if (variantRectIds.size() > 1){
        randomizeBrush = true;
    }
}

void editor::TilemapEditWindow::selectPaletteTile(SceneProject* sceneProject, const TilemapComponent& tilemap, int rectId, bool ctrlHeld){
    if (ctrlHeld){
        toggleVariantRect(rectId, tilemap);
    }else{
        selectSingleRect(rectId, tilemap);
    }
    paintWithPalette(sceneProject);
}

void editor::TilemapEditWindow::enablePaintMode(SceneProject* sceneProject){
    paintActive = true;
    if (sceneProject && sceneProject->sceneRender){
        sceneProject->sceneRender->clearTileSelection();
    }
}

void editor::TilemapEditWindow::paintWithPalette(SceneProject* sceneProject){
    if (activeTool == TilemapTool::Eraser || activeTool == TilemapTool::Eyedropper){
        activeTool = TilemapTool::Brush;
    }
    enablePaintMode(sceneProject);
}

bool editor::TilemapEditWindow::buildStampFromSheetRect(const TilemapComponent& tilemap, int submeshId, float x0, float y0, float x1, float y1){
    const float minSelX = std::min(x0, x1);
    const float maxSelX = std::max(x0, x1);
    const float minSelY = std::min(y0, y1);
    const float maxSelY = std::max(y0, y1);

    struct MatchedRect{
        int id;
        float x, y, w, h;
    };
    std::vector<MatchedRect> matched;

    for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
        const TileRectData& rd = tilemap.tilesRect[i];
        if (rd.submeshId != submeshId){
            continue;
        }
        const float rx = rd.rect.getX();
        const float ry = rd.rect.getY();
        const float rw = rd.rect.getWidth();
        const float rh = rd.rect.getHeight();
        if (rw <= 0.0f || rh <= 0.0f){
            continue;
        }
        if (rx + rw > minSelX && rx < maxSelX && ry + rh > minSelY && ry < maxSelY){
            matched.push_back({static_cast<int>(i), rx, ry, rw, rh});
        }
    }

    if (matched.empty()){
        return false;
    }
    if (matched.size() == 1){
        selectSingleRect(matched[0].id, tilemap);
        return true;
    }

    float baseW = matched[0].w;
    float baseH = matched[0].h;
    for (const auto& m : matched){
        baseW = std::min(baseW, m.w);
        baseH = std::min(baseH, m.h);
    }

    // Group distinct X/Y tracks so sheets with padding still form clean grid offsets
    std::vector<float> uniqueXs;
    std::vector<float> uniqueYs;
    const float epsX = std::max(1.0f, baseW * 0.25f);
    const float epsY = std::max(1.0f, baseH * 0.25f);

    auto addUnique = [](std::vector<float>& vals, float v, float eps){
        for (float u : vals){
            if (std::fabs(u - v) < eps){
                return;
            }
        }
        vals.push_back(v);
    };

    for (const auto& m : matched){
        addUnique(uniqueXs, m.x, epsX);
        addUnique(uniqueYs, m.y, epsY);
    }
    std::sort(uniqueXs.begin(), uniqueXs.end());
    std::sort(uniqueYs.begin(), uniqueYs.end());

    auto findClosestIndex = [](const std::vector<float>& vals, float v){
        int best = 0;
        float bestDist = std::numeric_limits<float>::max();
        for (size_t i = 0; i < vals.size(); i++){
            float d = std::fabs(vals[i] - v);
            if (d < bestDist){
                bestDist = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    };

    stampCells.clear();
    variantRectIds.clear();
    for (const auto& m : matched){
        stampCells.push_back({findClosestIndex(uniqueXs, m.x), findClosestIndex(uniqueYs, m.y), m.id});
        variantRectIds.push_back(m.id);
    }

    stampCols = std::max(1, static_cast<int>(uniqueXs.size()));
    stampRows = std::max(1, static_cast<int>(uniqueYs.size()));
    selectedRectId = matched[0].id;
    randomizeBrush = false;
    return true;
}

std::unordered_set<int> editor::TilemapEditWindow::buildSelectedRectSet() const{
    std::unordered_set<int> selectedSet;
    if (randomizeBrush && variantRectIds.size() > 1){
        selectedSet.insert(variantRectIds.begin(), variantRectIds.end());
    }else if (!stampCells.empty()){
        for (const TileStampCell& c : stampCells){
            selectedSet.insert(c.rectId);
        }
    }else if (selectedRectId >= 0){
        selectedSet.insert(selectedRectId);
    }
    return selectedSet;
}

int editor::TilemapEditWindow::pickBrushRectForCell(int cellX, int cellY, uint32_t seed) const{
    if (randomizeBrush && variantRectIds.size() > 1){
        uint32_t idx = hashCell(cellX, cellY, seed) % static_cast<uint32_t>(variantRectIds.size());
        return variantRectIds[idx];
    }
    return selectedRectId;
}

int editor::TilemapEditWindow::pickStampRectForOffset(int offsetX, int offsetY, bool flipY) const{
    if (stampCells.empty()){
        return selectedRectId;
    }
    if (stampCells.size() == 1){
        return stampCells[0].rectId;
    }
    const int col = positiveMod(offsetX, stampCols);
    const int rawRow = positiveMod(offsetY, stampRows);
    const int sheetRow = flipY ? (stampRows - 1 - rawRow) : rawRow;

    for (const TileStampCell& cell : stampCells){
        if (cell.colOffset == col && cell.rowOffset == sheetRow){
            return cell.rectId;
        }
    }
    return -1;
}

int editor::TilemapEditWindow::resolveCellRect(int cellX, int cellY, int originX, int originY, bool flipY, uint32_t seed) const{
    if (randomizeBrush && variantRectIds.size() > 1){
        return pickBrushRectForCell(cellX, cellY, seed);
    }
    if (stampCells.size() > 1){
        return pickStampRectForOffset(cellX - originX, cellY - originY, flipY);
    }
    return selectedRectId;
}

bool editor::TilemapEditWindow::hasValidBrush(const TilemapComponent& tilemap) const{
    if (activeTool == TilemapTool::Eraser || activeTool == TilemapTool::Eyedropper){
        return true;
    }
    return tilemap.numTilesRect > 0 && selectedRectId >= 0 && static_cast<unsigned int>(selectedRectId) < tilemap.numTilesRect;
}

bool editor::TilemapEditWindow::paintCellSingle(TilemapComponent& tilemap, int cellX, int cellY, int rectId, const Vector2& cellSize){
    if (cellX < 0 || cellY < 0 || rectId < 0 || static_cast<unsigned int>(rectId) >= tilemap.numTilesRect){
        return false;
    }

    const TileRectData& rectData = tilemap.tilesRect[rectId];
    float tileW = (autoCellSize && rectData.rect.getWidth() > 0.0f) ? rectData.rect.getWidth() : cellSize.x;
    float tileH = (autoCellSize && rectData.rect.getHeight() > 0.0f) ? rectData.rect.getHeight() : cellSize.y;

    const float posX = static_cast<float>(cellX) * cellSize.x;
    const float posY = static_cast<float>(cellY) * cellSize.y;

    if (replaceExisting){
        std::vector<unsigned int> matchingIndices;
        for (unsigned int i = 0; i < tilemap.numTiles; i++){
            const TileData& t = tilemap.tiles[i];
            const float txCenter = t.position.x + t.width * 0.5f;
            const float tyCenter = t.position.y + t.height * 0.5f;
            const bool centerInCell = (txCenter >= posX && txCenter < posX + cellSize.x &&
                                       tyCenter >= posY && tyCenter < posY + cellSize.y);
            const bool sameOrigin = (std::fabs(t.position.x - posX) < 0.25f &&
                                     std::fabs(t.position.y - posY) < 0.25f);
            if (centerInCell || sameOrigin){
                matchingIndices.push_back(i);
            }
        }

        if (!matchingIndices.empty()){
            TileData& existing = tilemap.tiles[matchingIndices.back()];
            const bool changed = (matchingIndices.size() > 1) ||
                                 existing.rectId != rectId ||
                                 std::fabs(existing.position.x - posX) > 1e-3f ||
                                 std::fabs(existing.position.y - posY) > 1e-3f ||
                                 std::fabs(existing.width - tileW) > 1e-3f ||
                                 std::fabs(existing.height - tileH) > 1e-3f;
            if (!changed){
                return false;
            }

            existing.name = rectData.name;
            existing.rectId = rectId;
            existing.position = Vector2(posX, posY);
            existing.width = tileW;
            existing.height = tileH;

            for (int k = static_cast<int>(matchingIndices.size()) - 2; k >= 0; k--){
                removeTileAt(tilemap, matchingIndices[k]);
            }
            return true;
        }
    }

    if (tilemap.numTiles >= MAX_TILEMAP_RENDERED_TILES){
        return false;
    }

    const int freeSlot = static_cast<int>(tilemap.numTiles);
    if (!tilemap.tiles.validIndex(freeSlot)){
        return false;
    }

    tilemap.tiles[freeSlot] = TileData{rectData.name, rectId, Vector2(posX, posY), tileW, tileH};
    tilemap.numTiles = static_cast<unsigned int>(freeSlot + 1);
    return true;
}

bool editor::TilemapEditWindow::paintStampAt(TilemapComponent& tilemap, int anchorCellX, int anchorCellY, const Vector2& cellSize, uint32_t seed){
    if (randomizeBrush && variantRectIds.size() > 1){
        return paintCellSingle(tilemap, anchorCellX, anchorCellY, pickBrushRectForCell(anchorCellX, anchorCellY, seed), cellSize);
    }
    if (stampCells.size() <= 1){
        return paintCellSingle(tilemap, anchorCellX, anchorCellY, selectedRectId, cellSize);
    }

    bool anyChanged = false;
    for (const TileStampCell& cell : stampCells){
        const int targetX = anchorCellX + cell.colOffset;
        const int targetY = anchorCellY + (tilemap.flipY ? (stampRows - 1 - cell.rowOffset) : cell.rowOffset);
        if (paintCellSingle(tilemap, targetX, targetY, cell.rectId, cellSize)){
            anyChanged = true;
        }
    }
    return anyChanged;
}

bool editor::TilemapEditWindow::eraseCell(TilemapComponent& tilemap, int cellX, int cellY, const Vector2& cellSize){
    // Unlike painting, erasing also reaches tiles at negative cells
    if (tilemap.numTiles == 0){
        return false;
    }

    const float posX = static_cast<float>(cellX) * cellSize.x;
    const float posY = static_cast<float>(cellY) * cellSize.y;
    const float centerX = posX + cellSize.x * 0.5f;
    const float centerY = posY + cellSize.y * 0.5f;

    bool erased = false;
    for (int i = static_cast<int>(tilemap.numTiles) - 1; i >= 0; i--){
        const TileData& t = tilemap.tiles[i];
        const bool hitCenter = (centerX >= t.position.x && centerX < t.position.x + t.width &&
                                centerY >= t.position.y && centerY < t.position.y + t.height);
        const bool tileCenterInCell = (t.position.x + t.width * 0.5f >= posX && t.position.x + t.width * 0.5f < posX + cellSize.x &&
                                       t.position.y + t.height * 0.5f >= posY && t.position.y + t.height * 0.5f < posY + cellSize.y);
        if (hitCenter || tileCenterInCell){
            removeTileAt(tilemap, static_cast<unsigned int>(i));
            erased = true;
        }
    }
    return erased;
}

void editor::TilemapEditWindow::paintLineCells(TilemapComponent& tilemap, int x0, int y0, int x1, int y1, const Vector2& cellSize, uint32_t seed){
    for (const auto& [cx, cy] : buildBresenhamLine(x0, y0, x1, y1)){
        int rectId = resolveCellRect(cx, cy, x0, y0, tilemap.flipY, seed);
        if (rectId >= 0){
            paintCellSingle(tilemap, cx, cy, rectId, cellSize);
        }
    }
}

void editor::TilemapEditWindow::paintRectCells(TilemapComponent& tilemap, int x0, int y0, int x1, int y1, const Vector2& cellSize, uint32_t seed){
    // More cells than a tilemap can hold would only stall the editor scanning them
    if (rectFillCellCount(x0, y0, x1, y1) > MAX_TILEMAP_RENDERED_TILES){
        return;
    }
    const int minX = std::max(0, std::min(x0, x1));
    const int maxX = std::max(x0, x1);
    const int minY = std::max(0, std::min(y0, y1));
    const int maxY = std::max(y0, y1);
    if (maxX < 0 || maxY < 0){
        return;
    }

    for (int cy = minY; cy <= maxY; cy++){
        for (int cx = minX; cx <= maxX; cx++){
            int rectId = resolveCellRect(cx, cy, minX, minY, tilemap.flipY, seed);
            if (rectId >= 0){
                paintCellSingle(tilemap, cx, cy, rectId, cellSize);
            }
        }
    }
}

int editor::TilemapEditWindow::findTileAtLocalPos(const TilemapComponent& tilemap, const Vector2& localPos) const{
    for (int i = static_cast<int>(tilemap.numTiles) - 1; i >= 0; i--){
        const TileData& t = tilemap.tiles[i];
        if (localPos.x >= t.position.x && localPos.x <= t.position.x + t.width &&
            localPos.y >= t.position.y && localPos.y <= t.position.y + t.height){
            return i;
        }
    }
    return -1;
}

bool editor::TilemapEditWindow::applyFloodFill(TilemapComponent& tilemap, int startCellX, int startCellY, const Vector2& cellSize, uint32_t seed){
    if (startCellX < 0 || startCellY < 0 || cellSize.x <= 0.0f || cellSize.y <= 0.0f){
        return false;
    }

    auto packCell = [](int x, int y) -> uint64_t {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
    };

    std::unordered_map<uint64_t, int> cellRectMap;
    int maxExistingX = startCellX;
    int maxExistingY = startCellY;
    for (unsigned int i = 0; i < tilemap.numTiles; i++){
        const TileData& t = tilemap.tiles[i];
        int cx = static_cast<int>(std::floor((t.position.x + t.width * 0.5f) / cellSize.x));
        int cy = static_cast<int>(std::floor((t.position.y + t.height * 0.5f) / cellSize.y));
        if (cx >= 0 && cy >= 0){
            cellRectMap[packCell(cx, cy)] = t.rectId;
            maxExistingX = std::max(maxExistingX, cx);
            maxExistingY = std::max(maxExistingY, cy);
        }
    }

    auto getCellRect = [&](int x, int y) -> int {
        auto it = cellRectMap.find(packCell(x, y));
        return (it != cellRectMap.end()) ? it->second : -1;
    };

    const int targetRectId = getCellRect(startCellX, startCellY);
    const bool isSingleUniformBrush = (!randomizeBrush || variantRectIds.size() <= 1) && stampCells.size() <= 1;
    if (isSingleUniformBrush && targetRectId == selectedRectId){
        return false;
    }

    int maxBoundX = std::max(maxExistingX, startCellX);
    int maxBoundY = std::max(maxExistingY, startCellY);
    if (tilemap.numTiles == 0){
        maxBoundX = std::max(maxBoundX, startCellX + 15);
        maxBoundY = std::max(maxBoundY, startCellY + 15);
    }

    std::queue<std::pair<int, int>> frontier;
    std::unordered_set<uint64_t> visited;
    std::vector<std::pair<int, int>> filledCells;

    frontier.emplace(startCellX, startCellY);
    visited.insert(packCell(startCellX, startCellY));

    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!frontier.empty() && static_cast<int>(filledCells.size()) < MAX_FLOOD_FILL_CELLS){
        auto [cx, cy] = frontier.front();
        frontier.pop();
        filledCells.emplace_back(cx, cy);

        for (const auto& d : dirs){
            int nx = cx + d[0];
            int ny = cy + d[1];
            if (nx < 0 || ny < 0 || nx > maxBoundX || ny > maxBoundY){
                continue;
            }
            uint64_t key = packCell(nx, ny);
            if (visited.count(key) || getCellRect(nx, ny) != targetRectId){
                continue;
            }
            visited.insert(key);
            frontier.emplace(nx, ny);
        }
    }

    bool anyChanged = false;
    for (const auto& [cx, cy] : filledCells){
        int rectId = resolveCellRect(cx, cy, startCellX, startCellY, tilemap.flipY, seed);
        if (rectId >= 0 && paintCellSingle(tilemap, cx, cy, rectId, cellSize)){
            anyChanged = true;
        }
    }

    return anyChanged;
}

void editor::TilemapEditWindow::handleCollisionPaint(SceneProject* sceneProject, TilemapComponent& tilemap, int rectId, bool clicked, bool hovered){
    if (rectId < 0 || static_cast<unsigned int>(rectId) >= tilemap.numTilesRect){
        return;
    }

    if (clicked){
        collisionDragging = true;
        collisionDragSceneId = sceneProject->id;
        collisionDragEntity = selectedEntity;
        collisionDragWasModified = sceneProject->isModified;
        collisionDragBefore.clear();
        collisionDragValue = !tilemap.tilesRect[rectId].collision;
        collisionDragBefore[rectId] = tilemap.tilesRect[rectId].collision;
        tilemap.tilesRect[rectId].collision = collisionDragValue;
        notifyTilemapChanged(sceneProject, selectedEntity);
    }else if (collisionDragging && hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        if (collisionDragBefore.find(rectId) == collisionDragBefore.end()){
            collisionDragBefore[rectId] = tilemap.tilesRect[rectId].collision;
        }
        if (tilemap.tilesRect[rectId].collision != collisionDragValue){
            tilemap.tilesRect[rectId].collision = collisionDragValue;
            notifyTilemapChanged(sceneProject, selectedEntity);
        }
    }
}

void editor::TilemapEditWindow::finishCollisionDrag(){
    if (!collisionDragging){
        return;
    }

    SceneProject* sceneProject = project ? project->getScene(collisionDragSceneId) : nullptr;
    if (sceneProject && sceneProject->scene && sceneProject->scene->isEntityCreated(collisionDragEntity)){
        TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(collisionDragEntity);
        if (tilemap && !collisionDragBefore.empty()){
            std::vector<TileRectCollisionChange> changes;
            for (const auto& [rectIdx, beforeVal] : collisionDragBefore){
                if (rectIdx >= 0 && static_cast<unsigned int>(rectIdx) < tilemap->numTilesRect){
                    bool afterVal = tilemap->tilesRect[rectIdx].collision;
                    if (afterVal != beforeVal){
                        changes.push_back({rectIdx, beforeVal, afterVal});
                    }
                }
            }
            if (!changes.empty()){
                CommandHandle::get(collisionDragSceneId)->addCommandNoMerge(
                    new TilemapRectCollisionCmd(project, collisionDragSceneId, collisionDragEntity, std::move(changes), collisionDragWasModified));
            }
        }
    }

    collisionDragging = false;
    collisionDragBefore.clear();
    collisionDragSceneId = NULL_PROJECT_SCENE;
    collisionDragEntity = NULL_ENTITY;
}

void editor::TilemapEditWindow::clearStroke(){
    stroke = ActiveStroke{};
}

void editor::TilemapEditWindow::cancelStroke(){
    // Line and rectangle only paint on release, the other tools painted live and get committed
    if (stroke.effectiveTool == TilemapTool::Line || stroke.effectiveTool == TilemapTool::RectFill){
        clearStroke();
    }else{
        endStroke();
    }
}

void editor::TilemapEditWindow::openSlicerForTarget(SceneProject* sceneProject, Entity entity){
    if (!sceneProject || !sceneProject->scene || !sceneProject->scene->isEntityCreated(entity)){
        return;
    }
    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entity);
    if (!tilemap){
        return;
    }

    std::vector<TextureSlicerToolDialog::SubmeshInfo> submeshInfos;
    if (meshComp){
        for (unsigned int s = 0; s < meshComp->numSubmeshes; s++){
            TextureSlicerToolDialog::SubmeshInfo info;
            std::string texPath = meshComp->submeshes[s].material.baseColorTexture.getPath(0);
            std::string fileLabel = texPath.empty() ? "" : (" (" + std::filesystem::path(texPath).filename().string() + ")");
            info.name = "Submesh " + std::to_string(s) + fileLabel;
            info.texture = meshComp->submeshes[s].material.baseColorTexture;
            submeshInfos.push_back(info);
        }
    }

    const uint32_t sceneId = sceneProject->id;
    slicerDialog.openTileset(
        submeshInfos,
        static_cast<int>(tilemap->width),
        static_cast<int>(tilemap->height),
        [this, sceneId, entity](const TextureSlicerToolDialog::SliceResult& result){
            int firstNewRect = 0;
            if (Command* cmd = buildApplySliceCmd(project, sceneId, entity, result, &firstNewRect)){
                CommandHandle::get(sceneId)->addCommand(cmd);
                selectedSubmesh = result.submeshId;
                selectedRectId = firstNewRect;
                stampCells.clear();
                sheetFitPending = true;
            }
        }
    );
}

void editor::TilemapEditWindow::drawLayerBar(SceneProject* sceneProject){
    std::vector<Entity> tilemapEntities;
    for (Entity e : project->getEntities(sceneProject->id)){
        if (sceneProject->scene->isEntityCreated(e) && sceneProject->scene->findComponent<TilemapComponent>(e)){
            tilemapEntities.push_back(e);
        }
    }

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonSize = ImGui::GetFrameHeight();
    const float slicerWidth = ImGui::CalcTextSize(ICON_FA_TABLE_CELLS " Slice").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float comboWidth = std::max(Theme::dpi(80.0f), ImGui::GetContentRegionAvail().x - slicerWidth - buttonSize - spacing * 2.0f);

    std::string currentName = sceneProject->scene->getEntityName(selectedEntity);
    TilemapComponent& currentTilemap = sceneProject->scene->getComponent<TilemapComponent>(selectedEntity);
    std::string previewLabel = std::string(ICON_FA_LAYER_GROUP " ") + currentName + " (" + std::to_string(currentTilemap.numTiles) + ")";

    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("##tilemap_layer", previewLabel.c_str())){
        for (Entity e : tilemapEntities){
            TilemapComponent& tm = sceneProject->scene->getComponent<TilemapComponent>(e);
            std::string itemLabel = sceneProject->scene->getEntityName(e) + " (" + std::to_string(tm.numTiles) + " tiles)##layer_" + std::to_string(e);
            bool isCurrent = (e == selectedEntity);
            if (ImGui::Selectable(itemLabel.c_str(), isCurrent) && e != selectedEntity){
                selectTilemapEntity(sceneProject, e);
            }
            if (isCurrent){
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    showTooltip("Active tilemap entity (switch between tilemap layers in this scene)");

    ImGui::SameLine();
    if (Transform* tr = sceneProject->scene->findComponent<Transform>(selectedEntity)){
        bool visible = tr->visible;
        if (!visible){
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        }
        if (Widgets::iconButton("##tilemap_vis", visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(buttonSize, buttonSize))){
            PropertyCmd<bool>* cmd = new PropertyCmd<bool>(project, sceneProject->id, selectedEntity, ComponentType::Transform, "visible", !visible);
            cmd->setNoMerge();
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        if (!visible){
            ImGui::PopStyleColor();
        }
        showTooltip(visible ? "Hide tilemap layer" : "Show tilemap layer");
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TABLE_CELLS " Slice##open_slicer", ImVec2(slicerWidth, 0))){
        openSlicerForTarget(sceneProject, selectedEntity);
    }
    showTooltip("Open Tileset Slicer Tool to slice a texture into tile rects");
}

void editor::TilemapEditWindow::drawToolbar(SceneProject* sceneProject, TilemapComponent& tilemap){
    const ImVec2 toolSize = commandButtonSize();

    auto toolBtn = [&](TilemapTool tool, const char* icon, const char* id, const char* tooltip){
        const bool selected = paintActive && activeTool == tool;
        if (iconButton(icon, id, tooltip, selected, toolSize)){
            cancelStroke();
            if (selected){
                paintActive = false;
            }else{
                activeTool = tool;
                enablePaintMode(sceneProject);
            }
        }
    };

    const bool hasRects = tilemap.numTilesRect > 0;

    ImGui::BeginDisabled(!hasRects);
    toolBtn(TilemapTool::Brush, ICON_FA_PAINTBRUSH, "tm_brush", "Brush (B) — Paint tiles (Shift+Click draws line, RMB erases)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    toolBtn(TilemapTool::Eraser, ICON_FA_ERASER, "tm_eraser", "Eraser (E) — Erase tiles (or Right-Click in 2D/UI)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasRects);
    toolBtn(TilemapTool::Line, ICON_FA_SLASH, "tm_line", "Line (L) — Drag a straight line of tiles");
    ImGui::SameLine();
    toolBtn(TilemapTool::RectFill, ICON_FA_SQUARE, "tm_rect", "Rectangle Fill (R) — Drag to fill a rectangle");
    ImGui::SameLine();
    toolBtn(TilemapTool::FloodFill, ICON_FA_FILL_DRIP, "tm_fill", "Flood Fill (G) — Fill contiguous area");
    ImGui::EndDisabled();
    ImGui::SameLine();
    toolBtn(TilemapTool::Eyedropper, ICON_FA_EYE_DROPPER, "tm_pick", "Eyedropper (I / Ctrl+Click) — Pick tile from viewport");

    toolbarGroupGap();

    if (iconButton(ICON_FA_BORDER_ALL, "tm_grid", showGrid ? "Hide tilemap grid" : "Show tilemap grid", showGrid, toolSize)){
        showGrid = !showGrid;
    }
    ImGui::SameLine();
    if (iconButton(ICON_FA_WEIGHT_HANGING, "tm_col_overlay",
                   showCollisionOverlay ? "Hide collision overlay in viewport & palette" : "Highlight solid collision tiles in viewport & palette",
                   showCollisionOverlay, toolSize)){
        showCollisionOverlay = !showCollisionOverlay;
    }

    if (paintActive){
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.92f, 0.55f, 1.0f));
        ImGui::TextUnformatted(ICON_FA_CHECK " Paint mode active (Esc to pause)");
        ImGui::PopStyleColor();
    }else{
        ImGui::TextDisabled("Paint mode paused — click a tool or tile to paint");
    }
}

void editor::TilemapEditWindow::drawPlacementOptions(TilemapComponent& tilemap){
    if (!ImGui::CollapsingHeader("Placement Options") || !beginTilemapProperties("tilemap_options_table")){
        return;
    }

    tilemapPropertyRow("Cell size", "Grid spacing in tilemap local units. Auto follows the selected tile rect.");
    if (ImGui::Checkbox("Auto##cell_auto", &autoCellSize)){
        if (!autoCellSize){
            Vector2 cur = getEffectiveCellSize(tilemap);
            customCellWidth = cur.x;
            customCellHeight = cur.y;
        }
    }
    if (!autoCellSize){
        ImGui::SameLine();
        float avail = std::max(Theme::dpi(60.0f), ImGui::GetContentRegionAvail().x);
        float halfW = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(halfW);
        UIUtils::dragFloat("##cell_w", &customCellWidth, 1.0f, 1.0f, 4096.0f, "W: %.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(halfW);
        UIUtils::dragFloat("##cell_h", &customCellHeight, 1.0f, 1.0f, 4096.0f, "H: %.0f");
    }else{
        Vector2 eff = getEffectiveCellSize(tilemap);
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0f x %.0f)", eff.x, eff.y);
    }

    tilemapPropertyRow("Replace tile", "Replace any tile already occupying the painted cell instead of stacking duplicates.");
    ImGui::Checkbox("##replace_existing", &replaceExisting);

    tilemapPropertyRow("Randomize", "Pick randomly from Ctrl+Clicked tile variants per cell.");
    ImGui::BeginDisabled(variantRectIds.size() <= 1);
    ImGui::Checkbox("##randomize_brush", &randomizeBrush);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (variantRectIds.size() > 1){
        ImGui::TextDisabled("(%d variants)", static_cast<int>(variantRectIds.size()));
    }else{
        ImGui::TextDisabled("(Ctrl+Click tiles)");
    }

    ImGui::EndTable();
}

void editor::TilemapEditWindow::drawPalette(SceneProject* sceneProject, TilemapComponent& tilemap, MeshComponent* meshComp){
    ImGui::SeparatorText("Tileset Palette");

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float groupGap = Theme::dpi(14.0f);
    const float btnH = ImGui::GetFrameHeight();
    const ImVec2 smallBtn(btnH, btnH);

    if (meshComp && meshComp->numSubmeshes > 1){
        std::string curTexPath = meshComp->submeshes[selectedSubmesh].material.baseColorTexture.getPath(0);
        std::string curLabel = "Submesh " + std::to_string(selectedSubmesh);
        if (!curTexPath.empty()){
            curLabel += " — " + std::filesystem::path(curTexPath).filename().string();
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##palette_submesh", curLabel.c_str())){
            for (unsigned int s = 0; s < meshComp->numSubmeshes; s++){
                std::string texPath = meshComp->submeshes[s].material.baseColorTexture.getPath(0);
                std::string label = "Submesh " + std::to_string(s);
                if (!texPath.empty()){
                    label += " — " + std::filesystem::path(texPath).filename().string();
                }
                int rectCount = 0;
                for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
                    if (tilemap.tilesRect[i].submeshId == static_cast<int>(s)){
                        rectCount++;
                    }
                }
                label += " (" + std::to_string(rectCount) + ")";

                bool isSel = (static_cast<int>(s) == selectedSubmesh);
                if (ImGui::Selectable(label.c_str(), isSel) && selectedSubmesh != static_cast<int>(s)){
                    selectedSubmesh = static_cast<int>(s);
                    sheetFitPending = true;
                    for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
                        if (tilemap.tilesRect[i].submeshId == selectedSubmesh){
                            selectSingleRect(static_cast<int>(i), tilemap);
                            break;
                        }
                    }
                }
                if (isSel){
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (iconButton(ICON_FA_IMAGE, "view_sheet", "Tileset Sheet View — Click or drag-select multi-tile stamps directly on the atlas", paletteView == TilemapPaletteView::Sheet, smallBtn)){
        paletteView = TilemapPaletteView::Sheet;
        sheetFitPending = true;
    }
    ImGui::SameLine();
    if (iconButton(ICON_FA_GRIP, "view_grid", "Compact Grid View — Searchable thumbnail grid", paletteView == TilemapPaletteView::Grid, smallBtn)){
        paletteView = TilemapPaletteView::Grid;
    }

    toolbarGroupGap();

    if (iconButton(ICON_FA_WEIGHT_HANGING, "collision_paint",
                   collisionPaintMode ? "Collision Paint ON — Click/drag tiles in palette to toggle solid collision" : "Collision Paint Mode — Click/drag tiles in palette to toggle solid collision",
                   collisionPaintMode, smallBtn)){
        collisionPaintMode = !collisionPaintMode;
        if (collisionPaintMode){
            showCollisionOverlay = true;
        }
    }

    toolbarGroupGap();

    if (paletteView == TilemapPaletteView::Sheet){
        if (Widgets::iconButton("##sheet_zoom_out", ICON_FA_MAGNIFYING_GLASS_MINUS, smallBtn)){
            sheetZoom = std::max(0.25f, sheetZoom / 1.25f);
        }
        showTooltip("Zoom out tileset sheet (or Ctrl+Scroll)");
        ImGui::SameLine();
        if (Widgets::iconButton("##sheet_zoom_in", ICON_FA_MAGNIFYING_GLASS_PLUS, smallBtn)){
            sheetZoom = std::min(12.0f, sheetZoom * 1.25f);
        }
        showTooltip("Zoom in tileset sheet (or Ctrl+Scroll)");
        ImGui::SameLine();
        if (ImGui::Button("Fit##sheet_fit")){
            sheetFitPending = true;
        }
        showTooltip("Fit tileset sheet to panel width");
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f%%", sheetZoom * 100.0f);
    }else{
        const float filterWidth = Theme::dpi(68.0f);
        const float zoomWidth = smallBtn.x * 2.0f + spacing + groupGap;
        const float searchWidth = std::max(Theme::dpi(52.0f), ImGui::GetContentRegionAvail().x - filterWidth - zoomWidth - spacing);
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##tile_search", ICON_FA_MAGNIFYING_GLASS " Search...", searchBuffer, sizeof(searchBuffer));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(filterWidth);
        ImGui::Combo("##col_filter", &collisionFilter, "All\0Solid\0Pass\0");
        showTooltip("Filter tiles by collision flag");
        toolbarGroupGap();
        if (Widgets::iconButton("##grid_zoom_out", ICON_FA_MAGNIFYING_GLASS_MINUS, smallBtn)){
            gridThumbSize = std::max(28.0f, gridThumbSize - 8.0f);
        }
        showTooltip("Smaller tile thumbnails (or Ctrl+Scroll)");
        ImGui::SameLine();
        if (Widgets::iconButton("##grid_zoom_in", ICON_FA_MAGNIFYING_GLASS_PLUS, smallBtn)){
            gridThumbSize = std::min(96.0f, gridThumbSize + 8.0f);
        }
        showTooltip("Larger tile thumbnails (or Ctrl+Scroll)");
    }

    if (collisionPaintMode){
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.30f, 1.0f));
        ImGui::TextUnformatted(ICON_FA_WEIGHT_HANGING " Click or drag tiles below to toggle collision");
        ImGui::PopStyleColor();
    }

    if (tilemap.numTilesRect == 0){
        ImGui::Spacing();
        ImGui::TextWrapped("No tile rects defined yet. Assign a texture in the Mesh component, then click Slice above to cut your tileset.");
        if (ImGui::Button(ICON_FA_TABLE_CELLS " Open Slicer Tool", ImVec2(-1.0f, 0.0f))){
            openSlicerForTarget(sceneProject, selectedEntity);
        }
        return;
    }

    if (paletteView == TilemapPaletteView::Sheet){
        drawSheetPalette(sceneProject, tilemap, meshComp);
    }else{
        drawGridPalette(sceneProject, tilemap, meshComp);
    }
}

void editor::TilemapEditWindow::drawSheetPalette(SceneProject* sceneProject, TilemapComponent& tilemap, MeshComponent* meshComp){
    Texture* tex = getSubmeshTexture(meshComp, selectedSubmesh);
    if (!tex){
        ImGui::TextWrapped("No texture assigned to Submesh %d. Switch to Grid View or assign a texture in the Mesh component.", selectedSubmesh);
        return;
    }

    if (tex->getWidth() == 0 || tex->getHeight() == 0){
        tex->load();
    }
    const float texW = static_cast<float>(tex->getWidth());
    const float texH = static_cast<float>(tex->getHeight());
    if (texW <= 0.0f || texH <= 0.0f || !tex->getRender()){
        ImGui::TextDisabled("Loading tileset texture...");
        return;
    }

    const float availH = std::max(Theme::dpi(160.0f), ImGui::GetContentRegionAvail().y - paletteFooterReserve());

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 28, 32, 255));
    ImGui::BeginChild("##tilemap_sheet_scroll", ImVec2(0.0f, availH), true, ImGuiWindowFlags_HorizontalScrollbar);

    if (sheetFitPending){
        float viewW = std::max(Theme::dpi(64.0f), ImGui::GetContentRegionAvail().x - Theme::dpi(8.0f));
        sheetZoom = std::clamp(viewW / texW, 0.25f, 8.0f);
        sheetFitPending = false;
    }

    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f){
        float factor = (ImGui::GetIO().MouseWheel > 0.0f) ? 1.15f : (1.0f / 1.15f);
        sheetZoom = std::clamp(sheetZoom * factor, 0.25f, 12.0f);
    }

    const ImVec2 canvasSize(texW * sheetZoom, texH * sheetZoom);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax(origin.x + canvasSize.x, origin.y + canvasSize.y);

    ImGui::InvisibleButton("##sheet_canvas", canvasSize);
    const bool canvasHovered = ImGui::IsItemHovered();
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const float mouseTexX = (mousePos.x - origin.x) / sheetZoom;
    const float mouseTexY = (mousePos.y - origin.y) / sheetZoom;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float checkerStep = Theme::dpi(12.0f);
    drawList->AddRectFilled(origin, canvasMax, IM_COL32(38, 38, 42, 255));
    const int numCX = std::min(128, static_cast<int>(std::ceil(canvasSize.x / checkerStep)));
    const int numCY = std::min(128, static_cast<int>(std::ceil(canvasSize.y / checkerStep)));
    if (numCX * numCY <= 4096){
        for (int cy = 0; cy < numCY; cy++){
            for (int cx = 0; cx < numCX; cx++){
                if (((cx + cy) & 1) == 0){
                    continue;
                }
                ImVec2 c0(origin.x + cx * checkerStep, origin.y + cy * checkerStep);
                ImVec2 c1(std::min(canvasMax.x, c0.x + checkerStep), std::min(canvasMax.y, c0.y + checkerStep));
                drawList->AddRectFilled(c0, c1, IM_COL32(48, 48, 54, 255));
            }
        }
    }

    Widgets::addImage(drawList, Backend::getImGuiTexture(tex->getRender()), origin, canvasMax);
    drawList->AddRect(origin, canvasMax, IM_COL32(95, 95, 105, 255));

    const std::unordered_set<int> selectedSet = buildSelectedRectSet();
    const bool usingRandomVariants = randomizeBrush && variantRectIds.size() > 1;

    int hoveredRectId = -1;
    for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
        const TileRectData& rd = tilemap.tilesRect[i];
        if (rd.submeshId != selectedSubmesh){
            continue;
        }
        const float rx = rd.rect.getX();
        const float ry = rd.rect.getY();
        const float rw = rd.rect.getWidth();
        const float rh = rd.rect.getHeight();
        if (rw <= 0.0f || rh <= 0.0f){
            continue;
        }

        ImVec2 p0(origin.x + rx * sheetZoom, origin.y + ry * sheetZoom);
        ImVec2 p1(origin.x + (rx + rw) * sheetZoom, origin.y + (ry + rh) * sheetZoom);

        if (canvasHovered && mouseTexX >= rx && mouseTexX < rx + rw && mouseTexY >= ry && mouseTexY < ry + rh){
            hoveredRectId = static_cast<int>(i);
        }

        if ((showCollisionOverlay || collisionPaintMode) && rd.collision){
            drawList->AddRectFilled(p0, p1, IM_COL32(60, 180, 255, 55));
            drawList->AddRect(p0, p1, IM_COL32(80, 200, 255, 175), 0.0f, 0, 1.0f);
        }else{
            drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 50), 0.0f, 0, 1.0f);
        }

        if (!collisionPaintMode && selectedSet.count(static_cast<int>(i))){
            ImU32 selFill = usingRandomVariants ? IM_COL32(180, 120, 255, 55) : IM_COL32(255, 205, 50, 55);
            ImU32 selBorder = usingRandomVariants ? IM_COL32(210, 150, 255, 245) : IM_COL32(255, 210, 60, 255);
            drawList->AddRectFilled(p0, p1, selFill);
            drawList->AddRect(p0, p1, selBorder, 0.0f, 0, Theme::dpi(2.0f));
        }
    }

    if (hoveredRectId >= 0){
        const Rect& hr = tilemap.tilesRect[hoveredRectId].rect;
        ImVec2 hp0(origin.x + hr.getX() * sheetZoom, origin.y + hr.getY() * sheetZoom);
        ImVec2 hp1(origin.x + (hr.getX() + hr.getWidth()) * sheetZoom, origin.y + (hr.getY() + hr.getHeight()) * sheetZoom);
        drawList->AddRect(hp0, hp1, IM_COL32(255, 255, 255, 230), 0.0f, 0, Theme::dpi(1.5f));
    }

    if (collisionPaintMode){
        handleCollisionPaint(sceneProject, tilemap, hoveredRectId,
                             canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                             canvasHovered);
        if (collisionDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            finishCollisionDrag();
        }
    }else{
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
            if (ImGui::GetIO().KeyCtrl && hoveredRectId >= 0){
                selectPaletteTile(sceneProject, tilemap, hoveredRectId, true);
            }else{
                sheetDragging = true;
                sheetDragStartTex = ImVec2(std::clamp(mouseTexX, 0.0f, texW), std::clamp(mouseTexY, 0.0f, texH));
                sheetDragEndTex = sheetDragStartTex;
                if (hoveredRectId >= 0){
                    selectPaletteTile(sceneProject, tilemap, hoveredRectId, false);
                }
            }
        }

        if (sheetDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            sheetDragEndTex = ImVec2(std::clamp(mouseTexX, 0.0f, texW), std::clamp(mouseTexY, 0.0f, texH));
            ImVec2 d0(origin.x + std::min(sheetDragStartTex.x, sheetDragEndTex.x) * sheetZoom,
                      origin.y + std::min(sheetDragStartTex.y, sheetDragEndTex.y) * sheetZoom);
            ImVec2 d1(origin.x + std::max(sheetDragStartTex.x, sheetDragEndTex.x) * sheetZoom,
                      origin.y + std::max(sheetDragStartTex.y, sheetDragEndTex.y) * sheetZoom);
            if ((d1.x - d0.x) > Theme::dpi(4.0f) || (d1.y - d0.y) > Theme::dpi(4.0f)){
                drawList->AddRectFilled(d0, d1, IM_COL32(80, 175, 255, 45));
                drawList->AddRect(d0, d1, IM_COL32(100, 200, 255, 235), 0.0f, 0, Theme::dpi(1.5f));
                // A drag that starts on empty sheet space never selected a tile on click
                if (buildStampFromSheetRect(tilemap, selectedSubmesh, sheetDragStartTex.x, sheetDragStartTex.y, sheetDragEndTex.x, sheetDragEndTex.y)){
                    paintWithPalette(sceneProject);
                }
            }
        }

        if (sheetDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            sheetDragging = false;
        }
    }

    if (canvasHovered && hoveredRectId >= 0 && !sheetDragging && !collisionDragging){
        drawRectTooltip(hoveredRectId, tilemap.tilesRect[hoveredRectId]);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void editor::TilemapEditWindow::drawGridPalette(SceneProject* sceneProject, TilemapComponent& tilemap, MeshComponent* meshComp){
    const float availH = std::max(Theme::dpi(160.0f), ImGui::GetContentRegionAvail().y - paletteFooterReserve());
    const std::string queryLower = toLowerString(searchBuffer);
    const std::unordered_set<int> selectedSet = buildSelectedRectSet();
    const bool usingRandomVariants = randomizeBrush && variantRectIds.size() > 1;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 28, 32, 255));
    ImGui::BeginChild("##tilemap_grid_scroll", ImVec2(0.0f, availH), true);

    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f){
        gridThumbSize = std::clamp(gridThumbSize + ImGui::GetIO().MouseWheel * 6.0f, 28.0f, 96.0f);
    }

    const float cellPx = Theme::dpi(gridThumbSize);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availW = std::max(cellPx, ImGui::GetContentRegionAvail().x);
    const int columns = std::max(1, static_cast<int>((availW + spacing) / (cellPx + spacing)));

    int drawn = 0;
    for (unsigned int i = 0; i < tilemap.numTilesRect; i++){
        const TileRectData& rd = tilemap.tilesRect[i];
        if (meshComp && meshComp->numSubmeshes > 1 && rd.submeshId != selectedSubmesh){
            continue;
        }
        if ((collisionFilter == 1 && !rd.collision) || (collisionFilter == 2 && rd.collision)){
            continue;
        }
        if (!queryLower.empty()){
            std::string haystack = "[" + std::to_string(i) + "] " + toLowerString(rd.name);
            if (haystack.find(queryLower) == std::string::npos){
                continue;
            }
        }

        if (drawn > 0 && (drawn % columns) != 0){
            ImGui::SameLine();
        }
        drawn++;

        ImGui::PushID(static_cast<int>(i));
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + cellPx, p0.y + cellPx);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float rounding = ImGui::GetStyle().FrameRounding;

        ImGui::InvisibleButton("##grid_tile", ImVec2(cellPx, cellPx));
        const bool hovered = ImGui::IsItemHovered();
        // The pressed tile stays active during a collision drag, which blocks plain hover on the others
        const bool dragHovered = collisionDragging && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool isSelected = selectedSet.count(static_cast<int>(i)) > 0;

        drawList->AddRectFilled(p0, p1, IM_COL32(42, 42, 48, 255), rounding);

        Texture* tex = getSubmeshTexture(meshComp, rd.submeshId);
        if (tex && tex->getRender() && tex->getWidth() > 0 && tex->getHeight() > 0 &&
            rd.rect.getWidth() > 0.0f && rd.rect.getHeight() > 0.0f){
            const float texW = static_cast<float>(tex->getWidth());
            const float texH = static_cast<float>(tex->getHeight());
            const float innerSize = std::max(1.0f, cellPx - Theme::dpi(6.0f));
            const float aspect = rd.rect.getWidth() / rd.rect.getHeight();
            const float dw = (aspect > 1.0f) ? innerSize : innerSize * aspect;
            const float dh = (aspect > 1.0f) ? innerSize / aspect : innerSize;

            ImVec2 imgMin(p0.x + (cellPx - dw) * 0.5f, p0.y + (cellPx - dh) * 0.5f);
            ImVec2 imgMax(imgMin.x + dw, imgMin.y + dh);
            ImVec2 uv0(rd.rect.getX() / texW, rd.rect.getY() / texH);
            ImVec2 uv1((rd.rect.getX() + rd.rect.getWidth()) / texW, (rd.rect.getY() + rd.rect.getHeight()) / texH);
            Widgets::addImageRounded(drawList, Backend::getImGuiTexture(tex->getRender()), imgMin, imgMax, uv0, uv1, IM_COL32_WHITE, rounding, ImDrawFlags_RoundAll);
        }

        if ((showCollisionOverlay || collisionPaintMode) && rd.collision){
            drawList->AddRectFilled(p0, p1, IM_COL32(60, 180, 255, 50), rounding);
            drawList->AddCircleFilled(ImVec2(p1.x - Theme::dpi(6.0f), p0.y + Theme::dpi(6.0f)), Theme::dpi(3.5f), IM_COL32(80, 200, 255, 240));
        }

        if (isSelected && !collisionPaintMode){
            ImU32 selCol = usingRandomVariants ? IM_COL32(210, 150, 255, 255) : IM_COL32(255, 210, 60, 255);
            drawList->AddRect(p0, p1, selCol, rounding, 0, Theme::dpi(2.0f));
        }else if (hovered || dragHovered){
            drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, 210), rounding, 0, Theme::dpi(1.5f));
        }else{
            drawList->AddRect(p0, p1, IM_COL32(72, 72, 82, 255), rounding, 0, 1.0f);
        }

        if (collisionPaintMode){
            handleCollisionPaint(sceneProject, tilemap, static_cast<int>(i), clicked, dragHovered);
        }else if (clicked){
            selectPaletteTile(sceneProject, tilemap, static_cast<int>(i), ImGui::GetIO().KeyCtrl);
        }

        if (ImGui::BeginPopupContextItem("##grid_tile_ctx")){
            if (ImGui::MenuItem(rd.collision ? "Disable Collision" : "Enable Collision")){
                PropertyCmd<bool>* cmd = new PropertyCmd<bool>(project, sceneProject->id, selectedEntity, ComponentType::TilemapComponent,
                    "tilesRect[" + std::to_string(i) + "].collision", !rd.collision);
                cmd->setNoMerge();
                CommandHandle::get(sceneProject->id)->addCommand(cmd);
            }
            ImGui::EndPopup();
        }

        if (hovered){
            drawRectTooltip(static_cast<int>(i), rd);
        }

        ImGui::PopID();
    }

    if (collisionDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        finishCollisionDrag();
    }

    if (drawn == 0){
        ImGui::TextDisabled("No tile rects match the current filter.");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void editor::TilemapEditWindow::drawFooter(SceneProject* sceneProject, TilemapComponent& tilemap){
    ImGui::Separator();

    if (randomizeBrush && variantRectIds.size() > 1){
        ImGui::Text("Brush: Random (%d variants)", static_cast<int>(variantRectIds.size()));
    }else if (stampCells.size() > 1){
        ImGui::Text("Stamp: %dx%d (%d tiles)", stampCols, stampRows, static_cast<int>(stampCells.size()));
    }else if (selectedRectId >= 0 && static_cast<unsigned int>(selectedRectId) < tilemap.numTilesRect){
        const TileRectData& rd = tilemap.tilesRect[selectedRectId];
        ImGui::Text("Tile: [%d] %s", selectedRectId, rd.name.empty() ? "(unnamed)" : rd.name.c_str());
    }else{
        ImGui::TextDisabled("Tile: None");
    }

    const float clearBtnW = ImGui::CalcTextSize(ICON_FA_TRASH_CAN " Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Tiles: %u / %d  •  Rects: %u", tilemap.numTiles, MAX_TILEMAP_RENDERED_TILES, tilemap.numTilesRect);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - clearBtnW));
    ImGui::BeginDisabled(tilemap.numTiles == 0);
    if (ImGui::Button(ICON_FA_TRASH_CAN " Clear##clear_all_tiles", ImVec2(clearBtnW, 0.0f))){
        cancelStroke();
        std::vector<TileData> before = snapshotTiles(tilemap);
        CommandHandle::get(sceneProject->id)->addCommandNoMerge(
            new TilemapTilesCmd(project, sceneProject->id, selectedEntity, std::move(before), {}, sceneProject->isModified));
    }
    ImGui::EndDisabled();
    showTooltip("Clear all placed tiles on this tilemap (undoable)");
}

void editor::TilemapEditWindow::show(){
    if (!windowOpen){
        return;
    }

    // Painting works from a background tab too, so this runs before Begin can skip the window
    const bool validTarget = refreshTarget();
    SceneProject* sceneProject = getTargetSceneProject();

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26.0f, ImGui::GetFontSize() * 42.0f), ImGuiCond_FirstUseEver);
    if (focusRequested){
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }

    if (!ImGui::Begin(WINDOW_NAME, &windowOpen)){
        ImGui::End();
        if (!windowOpen){
            setOpen(false);
        }
        return;
    }

    if (!validTarget){
        if (project && project->isAnyScenePlaying()){
            ImGui::TextWrapped("Stop the scene to edit tilemaps.");
        }else{
            ImGui::TextWrapped("Select a tilemap entity in the scene to paint tiles.");

            SceneProject* activeScene = project ? project->getSelectedScene() : nullptr;
            if (activeScene && activeScene->scene){
                std::vector<Entity> available;
                for (Entity e : project->getEntities(activeScene->id)){
                    if (activeScene->scene->isEntityCreated(e) && activeScene->scene->findComponent<TilemapComponent>(e)){
                        available.push_back(e);
                    }
                }
                if (!available.empty()){
                    ImGui::Spacing();
                    ImGui::SeparatorText("Tilemaps in Scene");
                    for (Entity e : available){
                        TilemapComponent& tm = activeScene->scene->getComponent<TilemapComponent>(e);
                        std::string btnLabel = std::string(ICON_FA_LAYER_GROUP " ") + activeScene->scene->getEntityName(e)
                                               + " (" + std::to_string(tm.numTiles) + " tiles)##pick_tm_" + std::to_string(e);
                        if (ImGui::Button(btnLabel.c_str(), ImVec2(-1.0f, 0.0f))){
                            selectTilemapEntity(activeScene, e);
                            enablePaintMode(activeScene);
                        }
                    }
                }
            }
        }

        ImGui::End();
        if (!windowOpen){
            setOpen(false);
        }
        return;
    }

    TilemapComponent& tilemap = sceneProject->scene->getComponent<TilemapComponent>(selectedEntity);
    MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(selectedEntity);

    drawLayerBar(sceneProject);
    ImGui::Spacing();
    drawToolbar(sceneProject, tilemap);
    drawPlacementOptions(tilemap);
    drawPalette(sceneProject, tilemap, meshComp);
    drawFooter(sceneProject, tilemap);

    slicerDialog.show();

    ImGui::End();

    if (!windowOpen){
        setOpen(false);
    }
}

void editor::TilemapEditWindow::open(){
    setOpen(true);
    focusRequested = true;
}

void editor::TilemapEditWindow::openForEntity(Entity entity, uint32_t sceneId){
    // open() follows the selection, which is this entity when Properties shows the button
    open();
    if (selectedSceneId == sceneId && selectedEntity == entity){
        enablePaintMode(project->getScene(sceneId));
    }
}

void editor::TilemapEditWindow::setOpen(bool open){
    if (open){
        if (!windowOpen){
            focusRequested = true;
            sheetFitPending = true;
        }
        windowOpen = true;
        refreshTarget();
        return;
    }

    windowOpen = false;
    focusRequested = false;
    paintActive = false;
    sheetDragging = false;
    cancelStroke();
    finishCollisionDrag();
    clearHover();
    slicerDialog.close();
}

bool editor::TilemapEditWindow::isOpen() const{
    return windowOpen;
}

void editor::TilemapEditWindow::resetProjectState(){
    cancelStroke();
    finishCollisionDrag();
    clearHover();
    slicerDialog.close();
    sheetDragging = false;
    selectedSceneId = NULL_PROJECT_SCENE;
    selectedEntity = NULL_ENTITY;
    paintActive = false;
    hasLastPaintedCell = false;
    resetBrush();
    brushSceneId = NULL_PROJECT_SCENE;
    brushEntity = NULL_ENTITY;
    sheetFitPending = true;
}

bool editor::TilemapEditWindow::isEditingScene(Scene* scene) const{
    if (!windowOpen || !paintActive || !scene){
        return false;
    }
    SceneProject* sceneProject = findSceneProject(scene);
    return sceneProject && sceneProject->id == selectedSceneId && hasValidTarget(sceneProject);
}

bool editor::TilemapEditWindow::isRightStrokeActive() const{
    return stroke.active && stroke.rightButtonErase;
}

bool editor::TilemapEditWindow::handleShortcutKeys(Scene* scene){
    if (!isEditingScene(scene)){
        return false;
    }

    // Esc cancels a drag first and pauses only with no button held, or the drag becomes a selection box
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)){
        if (stroke.active){
            cancelStroke();
        }else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)){
            paintActive = false;
        }
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_B)){ cancelStroke(); activeTool = TilemapTool::Brush; return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_E)){ cancelStroke(); activeTool = TilemapTool::Eraser; return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_L)){ cancelStroke(); activeTool = TilemapTool::Line; return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_R)){ cancelStroke(); activeTool = TilemapTool::RectFill; return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_G)){ cancelStroke(); activeTool = TilemapTool::FloodFill; return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_I)){ cancelStroke(); activeTool = TilemapTool::Eyedropper; return true; }

    return false;
}

void editor::TilemapEditWindow::updateHover(Scene* scene, const Ray& ray){
    if (!isEditingScene(scene)){
        hoverValid = false;
        return;
    }

    SceneProject* sceneProject = findSceneProject(scene);
    Vector2 localPos;
    if (!sceneProject || !raycastTilemapPlane(scene, selectedEntity, ray, localPos)){
        hoverValid = false;
        return;
    }

    const TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(selectedEntity);
    const Vector2 cellSize = getEffectiveCellSize(tilemap);

    hoverValid = true;
    hoverSceneId = sceneProject->id;
    hoverEntity = selectedEntity;
    hoverLocalPos = localPos;
    hoverCellX = static_cast<int>(std::floor(localPos.x / cellSize.x));
    hoverCellY = static_cast<int>(std::floor(localPos.y / cellSize.y));
}

void editor::TilemapEditWindow::clearHover(){
    hoverValid = false;
    hoverSceneId = NULL_PROJECT_SCENE;
    hoverEntity = NULL_ENTITY;
}

bool editor::TilemapEditWindow::beginStroke(Scene* scene, const Ray& ray, bool rightButton){
    if (!isEditingScene(scene)){
        return false;
    }

    // A second button only ends the stroke in progress, so its undo entry is kept
    if (stroke.active){
        cancelStroke();
        return true;
    }

    SceneProject* sceneProject = findSceneProject(scene);
    Vector2 localPos;
    if (!sceneProject || !raycastTilemapPlane(scene, selectedEntity, ray, localPos)){
        return false;
    }

    TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(selectedEntity);
    const Vector2 cellSize = getEffectiveCellSize(tilemap);
    const int cellX = static_cast<int>(std::floor(localPos.x / cellSize.x));
    const int cellY = static_cast<int>(std::floor(localPos.y / cellSize.y));

    hoverValid = true;
    hoverSceneId = sceneProject->id;
    hoverEntity = selectedEntity;
    hoverLocalPos = localPos;
    hoverCellX = cellX;
    hoverCellY = cellY;

    const ImGuiIO& io = ImGui::GetIO();

    if (!rightButton && (activeTool == TilemapTool::Eyedropper || io.KeyCtrl)){
        int hitIdx = findTileAtLocalPos(tilemap, localPos);
        if (hitIdx >= 0){
            int rectId = tilemap.tiles[hitIdx].rectId;
            if (rectId >= 0 && static_cast<unsigned int>(rectId) < tilemap.numTilesRect){
                selectSingleRect(rectId, tilemap);
                if (activeTool == TilemapTool::Eyedropper){
                    activeTool = TilemapTool::Brush;
                }
            }
        }
        return true;
    }

    TilemapTool effective = rightButton ? TilemapTool::Eraser : activeTool;
    if (effective != TilemapTool::Eraser && !hasValidBrush(tilemap)){
        return false;
    }

    clearStroke();
    stroke.active = true;
    stroke.rightButtonErase = rightButton;
    stroke.sceneId = sceneProject->id;
    stroke.entity = selectedEntity;
    stroke.effectiveTool = effective;
    stroke.beforeTiles = snapshotTiles(tilemap);
    stroke.wasModifiedBefore = sceneProject->isModified;
    stroke.startCellX = cellX;
    stroke.startCellY = cellY;
    stroke.currentCellX = cellX;
    stroke.currentCellY = cellY;
    stroke.lastCellX = cellX;
    stroke.lastCellY = cellY;
    stroke.hasLastCell = true;
    stroke.randomSeed = strokeSeedCounter++;

    if (effective == TilemapTool::FloodFill){
        if (applyFloodFill(tilemap, cellX, cellY, cellSize, stroke.randomSeed)){
            notifyTilemapChanged(sceneProject, selectedEntity);
        }
        hasLastPaintedCell = true;
        lastPaintedCellX = cellX;
        lastPaintedCellY = cellY;
        return true;
    }

    if (effective == TilemapTool::Line || effective == TilemapTool::RectFill){
        sceneProject->needUpdateRender = true;
        return true;
    }

    if (effective == TilemapTool::Eraser){
        if (eraseCell(tilemap, cellX, cellY, cellSize)){
            notifyTilemapChanged(sceneProject, selectedEntity);
        }
        return true;
    }

    if (io.KeyShift && hasLastPaintedCell){
        paintLineCells(tilemap, lastPaintedCellX, lastPaintedCellY, cellX, cellY, cellSize, stroke.randomSeed);
        notifyTilemapChanged(sceneProject, selectedEntity);
    }else if (paintStampAt(tilemap, cellX, cellY, cellSize, stroke.randomSeed)){
        notifyTilemapChanged(sceneProject, selectedEntity);
    }

    hasLastPaintedCell = true;
    lastPaintedCellX = cellX;
    lastPaintedCellY = cellY;
    return true;
}

bool editor::TilemapEditWindow::paintStroke(Scene* scene, const Ray& ray){
    if (!stroke.active || !isEditingScene(scene)){
        return false;
    }

    SceneProject* sceneProject = findSceneProject(scene);
    Vector2 localPos;
    if (!sceneProject || sceneProject->id != stroke.sceneId || selectedEntity != stroke.entity ||
        !raycastTilemapPlane(scene, selectedEntity, ray, localPos)){
        return false;
    }

    TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(selectedEntity);
    const Vector2 cellSize = getEffectiveCellSize(tilemap);
    const int cellX = static_cast<int>(std::floor(localPos.x / cellSize.x));
    const int cellY = static_cast<int>(std::floor(localPos.y / cellSize.y));

    hoverValid = true;
    hoverSceneId = sceneProject->id;
    hoverEntity = selectedEntity;
    hoverLocalPos = localPos;
    hoverCellX = cellX;
    hoverCellY = cellY;

    if (stroke.currentCellX == cellX && stroke.currentCellY == cellY){
        return false;
    }
    stroke.currentCellX = cellX;
    stroke.currentCellY = cellY;

    if (stroke.effectiveTool == TilemapTool::Line || stroke.effectiveTool == TilemapTool::RectFill){
        sceneProject->needUpdateRender = true;
        return true;
    }
    if (stroke.effectiveTool == TilemapTool::FloodFill){
        return false;
    }

    const int fromX = stroke.hasLastCell ? stroke.lastCellX : cellX;
    const int fromY = stroke.hasLastCell ? stroke.lastCellY : cellY;

    bool anyChanged = false;
    for (const auto& [cx, cy] : buildBresenhamLine(fromX, fromY, cellX, cellY)){
        if (stroke.effectiveTool == TilemapTool::Eraser){
            if (eraseCell(tilemap, cx, cy, cellSize)){
                anyChanged = true;
            }
        }else{
            if (paintStampAt(tilemap, cx, cy, cellSize, stroke.randomSeed)){
                anyChanged = true;
            }
            hasLastPaintedCell = true;
            lastPaintedCellX = cx;
            lastPaintedCellY = cy;
        }
    }

    stroke.lastCellX = cellX;
    stroke.lastCellY = cellY;
    stroke.hasLastCell = true;

    if (anyChanged){
        notifyTilemapChanged(sceneProject, selectedEntity);
    }
    return anyChanged;
}

void editor::TilemapEditWindow::endStroke(){
    if (!stroke.active){
        return;
    }

    SceneProject* sceneProject = project ? project->getScene(stroke.sceneId) : nullptr;
    if (sceneProject && sceneProject->scene && sceneProject->scene->isEntityCreated(stroke.entity)){
        TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(stroke.entity);
        if (tilemap){
            const Vector2 cellSize = getEffectiveCellSize(*tilemap);

            if (stroke.effectiveTool == TilemapTool::Line){
                paintLineCells(*tilemap, stroke.startCellX, stroke.startCellY, stroke.currentCellX, stroke.currentCellY, cellSize, stroke.randomSeed);
                hasLastPaintedCell = true;
                lastPaintedCellX = stroke.currentCellX;
                lastPaintedCellY = stroke.currentCellY;
            }else if (stroke.effectiveTool == TilemapTool::RectFill){
                paintRectCells(*tilemap, stroke.startCellX, stroke.startCellY, stroke.currentCellX, stroke.currentCellY, cellSize, stroke.randomSeed);
                hasLastPaintedCell = true;
                lastPaintedCellX = stroke.currentCellX;
                lastPaintedCellY = stroke.currentCellY;
            }

            std::vector<TileData> afterTiles = snapshotTiles(*tilemap);
            if (!tilesEqual(stroke.beforeTiles, afterTiles)){
                CommandHandle::get(stroke.sceneId)->addCommandNoMerge(
                    new TilemapTilesCmd(project, stroke.sceneId, stroke.entity,
                                        std::move(stroke.beforeTiles), std::move(afterTiles),
                                        stroke.wasModifiedBefore));
            }
        }
    }

    clearStroke();
}

void editor::TilemapEditWindow::drawViewportOverlay(SceneProject* sceneProject, const ImVec2& imageMin, const ImVec2& imageSize, bool isMouseInCanvas){
    if (!sceneProject || !sceneProject->scene || !sceneProject->sceneRender || !isEditingScene(sceneProject->scene)){
        return;
    }
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f){
        return;
    }

    Camera* camera = sceneProject->sceneRender->getCamera();
    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(selectedEntity);
    MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(selectedEntity);
    Transform* transform = sceneProject->scene->findComponent<Transform>(selectedEntity);
    if (!camera || !tilemap || !transform){
        return;
    }

    const Vector2 cellSize = getEffectiveCellSize(*tilemap);
    if (cellSize.x <= 0.0f || cellSize.y <= 0.0f){
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
    drawList->PushClipRect(imageMin, imageMax, true);

    const Matrix4& model = transform->modelMatrix;

    auto projectLocal = [&](float lx, float ly, ImVec2& out) -> bool {
        return worldToScreen(camera, model * Vector3(lx, ly, 0.0f), imageMin, imageSize, out);
    };

    auto drawLocalCellQuad = [&](float lx, float ly, float w, float h, int rectId, ImU32 tint, ImU32 borderCol, float borderThickness){
        ImVec2 p0, p1, p2, p3;
        if (!projectLocal(lx, ly, p0) ||
            !projectLocal(lx + w, ly, p1) ||
            !projectLocal(lx + w, ly + h, p2) ||
            !projectLocal(lx, ly + h, p3)){
            return;
        }

        bool drewTexture = false;
        if (rectId >= 0 && static_cast<unsigned int>(rectId) < tilemap->numTilesRect){
            const TileRectData& rd = tilemap->tilesRect[rectId];
            Texture* tex = getSubmeshTexture(meshComp, rd.submeshId);
            if (tex && tex->getRender() && tex->getWidth() > 0 && tex->getHeight() > 0 &&
                rd.rect.getWidth() > 0.0f && rd.rect.getHeight() > 0.0f){
                ImTextureID texId = Backend::getImGuiTexture(tex->getRender());
                if (texId != ImTextureID{}){
                    const float texW = static_cast<float>(tex->getWidth());
                    const float texH = static_cast<float>(tex->getHeight());
                    const float u0 = rd.rect.getX() / texW;
                    const float v0 = rd.rect.getY() / texH;
                    const float u1 = (rd.rect.getX() + rd.rect.getWidth()) / texW;
                    const float v1 = (rd.rect.getY() + rd.rect.getHeight()) / texH;

                    // Matches MeshSystem::createTilemap UV layout for flipY
                    if (tilemap->flipY){
                        drawList->AddImageQuad(texId, p0, p1, p2, p3,
                                               ImVec2(u0, v1), ImVec2(u1, v1), ImVec2(u1, v0), ImVec2(u0, v0), tint);
                    }else{
                        drawList->AddImageQuad(texId, p0, p1, p2, p3,
                                               ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1), tint);
                    }
                    drewTexture = true;
                }
            }
        }

        if (!drewTexture && (tint & IM_COL32_A_MASK) != 0){
            drawList->AddQuadFilled(p0, p1, p2, p3, tint);
        }
        if ((borderCol & IM_COL32_A_MASK) != 0){
            drawList->AddQuad(p0, p1, p2, p3, borderCol, borderThickness);
        }
    };

    if (showGrid){
        float minLocalX = 0.0f;
        float minLocalY = 0.0f;
        float maxLocalX = std::max(static_cast<float>(tilemap->width), cellSize.x * 16.0f);
        float maxLocalY = std::max(static_cast<float>(tilemap->height), cellSize.y * 16.0f);

        const Matrix4 invVP = camera->getViewProjectionMatrix().inverse();
        const float ndcCorners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        float visMinX = std::numeric_limits<float>::max();
        float visMaxX = -std::numeric_limits<float>::max();
        float visMinY = std::numeric_limits<float>::max();
        float visMaxY = -std::numeric_limits<float>::max();
        int validCorners = 0;
        for (const auto& c : ndcCorners){
            Vector4 nearWorld = invVP * Vector4(c[0], c[1], -1.0f, 1.0f);
            Vector4 farWorld = invVP * Vector4(c[0], c[1], 1.0f, 1.0f);
            if (std::fabs(nearWorld.w) < 1e-6f || std::fabs(farWorld.w) < 1e-6f){
                continue;
            }
            nearWorld.divideByW();
            farWorld.divideByW();
            Vector3 ro(nearWorld[0], nearWorld[1], nearWorld[2]);
            Vector3 rd(farWorld[0] - nearWorld[0], farWorld[1] - nearWorld[1], farWorld[2] - nearWorld[2]);
            Vector2 hit;
            if (raycastTilemapPlane(sceneProject->scene, selectedEntity, Ray(ro, rd), hit)){
                visMinX = std::min(visMinX, hit.x);
                visMaxX = std::max(visMaxX, hit.x);
                visMinY = std::min(visMinY, hit.y);
                visMaxY = std::max(visMaxY, hit.y);
                validCorners++;
            }
        }
        if (validCorners == 4){
            minLocalX = std::max(0.0f, visMinX);
            minLocalY = std::max(0.0f, visMinY);
            maxLocalX = std::max(0.0f, visMaxX);
            maxLocalY = std::max(0.0f, visMaxY);
        }

        ImVec2 s0, sx, sy;
        float cellScreenPx = 16.0f;
        if (projectLocal(0.0f, 0.0f, s0) && projectLocal(cellSize.x, 0.0f, sx) && projectLocal(0.0f, cellSize.y, sy)){
            cellScreenPx = std::min(std::hypot(sx.x - s0.x, sx.y - s0.y), std::hypot(sy.x - s0.x, sy.y - s0.y));
        }

        int stepCells = 1;
        while (cellScreenPx * static_cast<float>(stepCells) < Theme::dpi(6.0f) && stepCells < 64){
            stepCells *= 2;
        }

        const int startCol = std::max(0, static_cast<int>(std::floor(minLocalX / cellSize.x)) / stepCells * stepCells);
        const int endCol = std::min(startCol + stepCells * 160, static_cast<int>(std::ceil(maxLocalX / cellSize.x)) + stepCells);
        const int startRow = std::max(0, static_cast<int>(std::floor(minLocalY / cellSize.y)) / stepCells * stepCells);
        const int endRow = std::min(startRow + stepCells * 160, static_cast<int>(std::ceil(maxLocalY / cellSize.y)) + stepCells);

        const float gridX0 = static_cast<float>(startCol) * cellSize.x;
        const float gridX1 = static_cast<float>(endCol) * cellSize.x;
        const float gridY0 = static_cast<float>(startRow) * cellSize.y;
        const float gridY1 = static_cast<float>(endRow) * cellSize.y;

        const ImU32 gridColor = IM_COL32(255, 255, 255, 34);
        const ImU32 majorColor = IM_COL32(255, 255, 255, 62);

        for (int c = startCol; c <= endCol; c += stepCells){
            float lx = static_cast<float>(c) * cellSize.x;
            ImVec2 a, b;
            if (projectLocal(lx, gridY0, a) && projectLocal(lx, gridY1, b)){
                ImU32 col = (c == 0) ? IM_COL32(100, 220, 120, 180) : ((c % (stepCells * 4) == 0) ? majorColor : gridColor);
                drawList->AddLine(a, b, col, (c == 0) ? Theme::dpi(1.5f) : 1.0f);
            }
        }
        for (int r = startRow; r <= endRow; r += stepCells){
            float ly = static_cast<float>(r) * cellSize.y;
            ImVec2 a, b;
            if (projectLocal(gridX0, ly, a) && projectLocal(gridX1, ly, b)){
                ImU32 col = (r == 0) ? IM_COL32(235, 100, 100, 180) : ((r % (stepCells * 4) == 0) ? majorColor : gridColor);
                drawList->AddLine(a, b, col, (r == 0) ? Theme::dpi(1.5f) : 1.0f);
            }
        }
    }

    if (showCollisionOverlay){
        for (unsigned int i = 0; i < tilemap->numTiles; i++){
            const TileData& t = tilemap->tiles[i];
            if (t.rectId < 0 || static_cast<unsigned int>(t.rectId) >= tilemap->numTilesRect || !tilemap->tilesRect[t.rectId].collision){
                continue;
            }
            ImVec2 p0, p1, p2, p3;
            if (projectLocal(t.position.x, t.position.y, p0) &&
                projectLocal(t.position.x + t.width, t.position.y, p1) &&
                projectLocal(t.position.x + t.width, t.position.y + t.height, p2) &&
                projectLocal(t.position.x, t.position.y + t.height, p3)){
                drawList->AddQuadFilled(p0, p1, p2, p3, IM_COL32(60, 185, 255, 48));
                drawList->AddQuad(p0, p1, p2, p3, IM_COL32(90, 210, 255, 190), 1.0f);
            }
        }
    }

    const bool navigating = !stroke.active && (
        ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
        ImGui::IsKeyDown(ImGuiMod_Alt) ||
        (sceneProject->sceneType != SceneType::SCENE_3D && ImGui::IsKeyDown(ImGuiKey_Space)) ||
        sceneProject->sceneRender->getCursorSelected() == CursorSelected::HAND);
    const bool showPreview = !navigating && hoverValid && hoverSceneId == sceneProject->id && hoverEntity == selectedEntity && (isMouseInCanvas || stroke.active);

    if (showPreview){
        const ImU32 ghostTint = IM_COL32(255, 255, 255, 160);
        const ImU32 validBorder = IM_COL32(255, 215, 70, 235);
        const ImU32 invalidFill = IM_COL32(230, 70, 70, 55);
        const ImU32 invalidBorder = IM_COL32(245, 95, 95, 230);

        auto previewCell = [&](int cx, int cy, int rectId){
            Vector2 sz = cellSize;
            if (autoCellSize && rectId >= 0 && static_cast<unsigned int>(rectId) < tilemap->numTilesRect){
                const Rect& r = tilemap->tilesRect[rectId].rect;
                if (r.getWidth() > 0.0f && r.getHeight() > 0.0f){
                    sz = Vector2(r.getWidth(), r.getHeight());
                }
            }
            const float lx = static_cast<float>(cx) * cellSize.x;
            const float ly = static_cast<float>(cy) * cellSize.y;
            if (cx < 0 || cy < 0){
                drawLocalCellQuad(lx, ly, sz.x, sz.y, -1, invalidFill, invalidBorder, Theme::dpi(1.5f));
            }else{
                drawLocalCellQuad(lx, ly, sz.x, sz.y, rectId, ghostTint, validBorder, Theme::dpi(1.5f));
            }
        };

        const TilemapTool previewTool = stroke.active ? stroke.effectiveTool : activeTool;

        if (stroke.active && stroke.effectiveTool == TilemapTool::Line){
            std::vector<std::pair<int, int>> lineCells = buildBresenhamLine(stroke.startCellX, stroke.startCellY, stroke.currentCellX, stroke.currentCellY);
            for (const auto& [cx, cy] : lineCells){
                int rectId = resolveCellRect(cx, cy, stroke.startCellX, stroke.startCellY, tilemap->flipY, stroke.randomSeed);
                if (rectId >= 0){
                    previewCell(cx, cy, rectId);
                }
            }
            ImVec2 curScreen;
            if (projectLocal((stroke.currentCellX + 1.0f) * cellSize.x, (stroke.currentCellY + 1.0f) * cellSize.y, curScreen)){
                char badge[48];
                std::snprintf(badge, sizeof(badge), "Line: %d tiles", static_cast<int>(lineCells.size()));
                drawFloatingBadge(drawList, ImVec2(curScreen.x + Theme::dpi(8.0f), curScreen.y - Theme::dpi(2.0f)), badge, IM_COL32(255, 225, 110, 255));
            }
        }else if (stroke.active && stroke.effectiveTool == TilemapTool::RectFill){
            const int minX = std::min(stroke.startCellX, stroke.currentCellX);
            const int maxX = std::max(stroke.startCellX, stroke.currentCellX);
            const int minY = std::min(stroke.startCellY, stroke.currentCellY);
            const int maxY = std::max(stroke.startCellY, stroke.currentCellY);
            const int spanX = maxX - minX + 1;
            const int spanY = maxY - minY + 1;
            const int64_t totalCells = static_cast<int64_t>(spanX) * spanY;
            const bool tooLarge = rectFillCellCount(minX, minY, maxX, maxY) > MAX_TILEMAP_RENDERED_TILES;
            const int clampedOriginX = std::max(0, minX);
            const int clampedOriginY = std::max(0, minY);

            if (totalCells <= 512){
                for (int cy = minY; cy <= maxY; cy++){
                    for (int cx = minX; cx <= maxX; cx++){
                        int rectId = resolveCellRect(cx, cy, clampedOriginX, clampedOriginY, tilemap->flipY, stroke.randomSeed);
                        if (rectId >= 0){
                            previewCell(cx, cy, rectId);
                        }
                    }
                }
            }else{
                drawLocalCellQuad(minX * cellSize.x, minY * cellSize.y, spanX * cellSize.x, spanY * cellSize.y, -1,
                                  tooLarge ? invalidFill : IM_COL32(255, 215, 70, 45), tooLarge ? invalidBorder : validBorder, Theme::dpi(2.0f));
            }

            ImVec2 curScreen;
            if (projectLocal((maxX + 1.0f) * cellSize.x, (maxY + 1.0f) * cellSize.y, curScreen)){
                char badge[64];
                if (tooLarge){
                    std::snprintf(badge, sizeof(badge), "%d x %d (too many tiles)", spanX, spanY);
                }else{
                    std::snprintf(badge, sizeof(badge), "%d x %d (%lld)", spanX, spanY, static_cast<long long>(totalCells));
                }
                drawFloatingBadge(drawList, ImVec2(curScreen.x + Theme::dpi(8.0f), curScreen.y - Theme::dpi(2.0f)), badge, IM_COL32(255, 225, 110, 255));
            }
        }else if (previewTool == TilemapTool::Eraser){
            drawLocalCellQuad(hoverCellX * cellSize.x, hoverCellY * cellSize.y, cellSize.x, cellSize.y,
                              -1, IM_COL32(235, 70, 70, 75), IM_COL32(255, 95, 95, 240), Theme::dpi(2.0f));
        }else if (previewTool == TilemapTool::Eyedropper || ImGui::GetIO().KeyCtrl){
            int hitIdx = findTileAtLocalPos(*tilemap, hoverLocalPos);
            if (hitIdx >= 0){
                const TileData& t = tilemap->tiles[hitIdx];
                drawLocalCellQuad(t.position.x, t.position.y, t.width, t.height,
                                  -1, IM_COL32(90, 200, 255, 55), IM_COL32(110, 220, 255, 245), Theme::dpi(2.0f));

                ImVec2 labelPos;
                if (projectLocal(t.position.x, t.position.y + t.height, labelPos)){
                    std::string rectName = (t.rectId >= 0 && static_cast<unsigned int>(t.rectId) < tilemap->numTilesRect)
                        ? tilemap->tilesRect[t.rectId].name : "";
                    char badge[96];
                    std::snprintf(badge, sizeof(badge), "Pick [%d] %s", t.rectId, rectName.c_str());
                    drawFloatingBadge(drawList, labelPos, badge, IM_COL32(130, 225, 255, 255));
                }
            }else{
                drawLocalCellQuad(hoverCellX * cellSize.x, hoverCellY * cellSize.y, cellSize.x, cellSize.y,
                                  -1, IM_COL32(90, 200, 255, 30), IM_COL32(110, 220, 255, 160), Theme::dpi(1.5f));
            }
        }else if (hasValidBrush(*tilemap)){
            if (randomizeBrush && variantRectIds.size() > 1){
                previewCell(hoverCellX, hoverCellY, pickBrushRectForCell(hoverCellX, hoverCellY, strokeSeedCounter));
            }else if (stampCells.size() > 1 && previewTool == TilemapTool::Brush){
                for (const TileStampCell& cell : stampCells){
                    int tx = hoverCellX + cell.colOffset;
                    int ty = hoverCellY + (tilemap->flipY ? (stampRows - 1 - cell.rowOffset) : cell.rowOffset);
                    previewCell(tx, ty, cell.rectId);
                }
            }else{
                previewCell(hoverCellX, hoverCellY, selectedRectId);
            }
        }
    }

    drawList->PopClipRect();
}
