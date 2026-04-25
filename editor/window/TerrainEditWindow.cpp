#include "TerrainEditWindow.h"

#include "Backend.h"
#include "Catalog.h"
#include "command/CommandHandle.h"
#include "external/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace doriax;
using namespace doriax::editor;

namespace {

    uint64_t gTerrainEditTextureCounter = 1;

    void showTooltip(const char* text, ImGuiHoveredFlags flags = 0){
        if (ImGui::IsItemHovered(flags)){
            ImGui::SetTooltip("%s", text);
        }
    }

    bool iconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec2& size){
        std::string label = std::string(icon) + "##" + id;

        if (selected){
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        }

        bool clicked = ImGui::Button(label.c_str(), size);

        if (selected){
            ImGui::PopStyleColor(3);
        }

        showTooltip(tooltip, ImGuiHoveredFlags_AllowWhenDisabled);
        return clicked;
    }

    bool colorIconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec4& color, const ImVec2& size){
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        bool clicked = iconButton(icon, id, tooltip, selected, size);
        ImGui::PopStyleColor();
        return clicked;
    }

    std::string makeEditableTextureId(uint32_t sceneId, Entity entity, editor::TerrainMapTarget target){
        const char* suffix = target == editor::TerrainMapTarget::HeightMap ? "height" : "blend";
        return "__terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix + "_" + std::to_string(gTerrainEditTextureCounter++);
    }

    Texture& getTerrainTexture(TerrainComponent& terrain, editor::TerrainMapTarget target){
        return target == editor::TerrainMapTarget::HeightMap ? terrain.heightMap : terrain.blendMap;
    }

    const char* getTerrainPropertyName(editor::TerrainMapTarget target){
        return target == editor::TerrainMapTarget::HeightMap ? "heightMap" : "blendMap";
    }

    int expectedChannels(editor::TerrainMapTarget target){
        return target == editor::TerrainMapTarget::HeightMap ? 1 : 4;
    }

    ColorFormat expectedFormat(editor::TerrainMapTarget target){
        return target == editor::TerrainMapTarget::HeightMap ? ColorFormat::RED : ColorFormat::RGBA;
    }

    unsigned char clampByte(float value){
        return static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, value)));
    }

    bool hasLoadedData(Texture& texture){
        if (texture.empty() || texture.isFramebuffer()){
            return false;
        }

        texture.setReleaseDataAfterLoad(false);
        TextureLoadResult result = texture.load();
        return result.state == ResourceLoadState::Finished && result.data && texture.getData().getData();
    }

    struct TerrainMapInfo{
        bool present = false;
        bool sizeKnown = false;
        bool framebuffer = false;
        int width = 0;
        int height = 0;
        int channels = 0;
    };

    TerrainMapInfo getTerrainMapInfo(Texture& texture){
        TerrainMapInfo info;
        info.present = !texture.empty();
        if (!info.present){
            return info;
        }

        if (texture.isFramebuffer()){
            info.framebuffer = true;
            info.width = static_cast<int>(texture.getWidth());
            info.height = static_cast<int>(texture.getHeight());
            info.sizeKnown = info.width > 0 && info.height > 0;
            return info;
        }

        if (hasLoadedData(texture)){
            TextureData& data = texture.getData();
            info.width = data.getWidth();
            info.height = data.getHeight();
            info.channels = data.getChannels();
            info.sizeKnown = info.width > 0 && info.height > 0;
        }else{
            info.width = static_cast<int>(texture.getWidth());
            info.height = static_cast<int>(texture.getHeight());
            info.sizeKnown = info.width > 0 && info.height > 0;
        }

        return info;
    }

    std::string getTerrainMapStatusText(const TerrainMapInfo& info){
        if (!info.present){
            return std::string(ICON_FA_TRIANGLE_EXCLAMATION) + "  Missing";
        }
        if (info.sizeKnown){
            return std::string(ICON_FA_CIRCLE_CHECK) + "  " + std::to_string(info.width) + " x " + std::to_string(info.height);
        }
        return std::string(ICON_FA_TRIANGLE_EXCLAMATION) + "  Size unavailable";
    }

    void showTerrainMapStatus(const TerrainMapInfo& info){
        std::string status = getTerrainMapStatusText(info);
        if (!info.present){
            ImGui::TextDisabled("%s", status.c_str());
        }else if (!info.sizeKnown){
            ImGui::TextColored(ImVec4(0.95f, 0.67f, 0.24f, 1.0f), "%s", status.c_str());
        }else{
            ImGui::TextUnformatted(status.c_str());
        }
    }

    std::vector<unsigned char> copyTexturePixels(TextureData& data){
        std::vector<unsigned char> pixels;
        if (!data.getData() || data.getSize() == 0){
            return pixels;
        }

        pixels.resize(data.getSize());
        std::memcpy(pixels.data(), data.getData(), data.getSize());
        return pixels;
    }

    std::vector<unsigned char> convertTexturePixels(TextureData& data, editor::TerrainMapTarget target){
        const int width = data.getWidth();
        const int height = data.getHeight();
        const int srcChannels = data.getChannels();
        const int dstChannels = expectedChannels(target);
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(dstChannels), 0);

        if (!data.getData() || width <= 0 || height <= 0 || srcChannels <= 0){
            return pixels;
        }

        unsigned char* src = static_cast<unsigned char*>(data.getData());
        for (int y = 0; y < height; y++){
            for (int x = 0; x < width; x++){
                const size_t srcIndex = (static_cast<size_t>(y) * width + x) * srcChannels;
                const size_t dstIndex = (static_cast<size_t>(y) * width + x) * dstChannels;
                if (target == editor::TerrainMapTarget::HeightMap){
                    pixels[dstIndex] = src[srcIndex];
                }else{
                    auto srcComponent = [&](int channel){
                        return src[srcIndex + std::min(channel, srcChannels - 1)];
                    };
                    pixels[dstIndex + 0] = srcComponent(0);
                    pixels[dstIndex + 1] = srcComponent(1);
                    pixels[dstIndex + 2] = srcComponent(2);
                    pixels[dstIndex + 3] = srcChannels >= 4 ? src[srcIndex + 3] : 255;
                }
            }
        }
        return pixels;
    }

    void setOwnedTextureData(Texture& texture, const std::string& id, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels){
        const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
        unsigned char* raw = static_cast<unsigned char*>(std::malloc(size));
        if (!raw){
            return;
        }
        if (!pixels.empty()){
            std::memcpy(raw, pixels.data(), size);
        }else{
            std::memset(raw, 0, size);
        }

        TextureData data(width, height, static_cast<unsigned int>(size), format, channels, raw);
        data.setDataOwned(false);
        texture.setData(id, data);
        texture.getData().setDataOwned(true);
    }

    editor::TerrainMapSnapshot captureSnapshot(Texture& texture, bool forcePixels){
        editor::TerrainMapSnapshot snapshot;
        snapshot.minFilter = texture.getMinFilter();
        snapshot.magFilter = texture.getMagFilter();
        snapshot.wrapU = texture.getWrapU();
        snapshot.wrapV = texture.getWrapV();

        if (texture.empty() || texture.isFramebuffer()){
            return snapshot;
        }

        snapshot.empty = false;
        snapshot.path = texture.getPath(0);
        snapshot.id = texture.getId();

        if (!forcePixels && !snapshot.path.empty()){
            return snapshot;
        }

        if (hasLoadedData(texture)){
            TextureData& data = texture.getData();
            snapshot.width = data.getWidth();
            snapshot.height = data.getHeight();
            snapshot.channels = data.getChannels();
            snapshot.colorFormat = data.getColorFormat();
            snapshot.pixels = copyTexturePixels(data);
        }

        return snapshot;
    }

    bool snapshotsEqual(const editor::TerrainMapSnapshot& a, const editor::TerrainMapSnapshot& b){
        return a.empty == b.empty &&
               a.path == b.path &&
               a.id == b.id &&
               a.minFilter == b.minFilter &&
               a.magFilter == b.magFilter &&
               a.wrapU == b.wrapU &&
               a.wrapV == b.wrapV &&
               a.colorFormat == b.colorFormat &&
               a.width == b.width &&
               a.height == b.height &&
               a.channels == b.channels &&
               a.pixels == b.pixels;
    }

    void applySnapshotToTexture(Texture& texture, const editor::TerrainMapSnapshot& snapshot){
        if (snapshot.empty){
            texture.destroy();
            texture = Texture();
            return;
        }

        if (!snapshot.pixels.empty() && snapshot.width > 0 && snapshot.height > 0 && snapshot.channels > 0){
            std::string id = snapshot.id.empty() ? ("__terrain_edit_snapshot_" + std::to_string(gTerrainEditTextureCounter++)) : snapshot.id;
            setOwnedTextureData(texture, id, snapshot.width, snapshot.height, snapshot.colorFormat, snapshot.channels, snapshot.pixels);
        }else if (!snapshot.path.empty()){
            texture.setPath(snapshot.path);
        }else if (!snapshot.id.empty()){
            texture.destroy();
            texture.setId(snapshot.id);
        }else{
            texture.destroy();
            texture = Texture();
        }

        texture.setMinFilter(snapshot.minFilter);
        texture.setMagFilter(snapshot.magFilter);
        texture.setWrapU(snapshot.wrapU);
        texture.setWrapV(snapshot.wrapV);
        texture.setReleaseDataAfterLoad(false);
    }

    bool ensureEditableMap(SceneProject* sceneProject, Entity entity, editor::TerrainMapTarget target, int resolution){
        TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entity);
        Texture& texture = getTerrainTexture(terrain, target);
        const int channels = expectedChannels(target);
        const ColorFormat format = expectedFormat(target);

        if (texture.empty()){
            const int safeResolution = std::max(2, resolution);
            std::vector<unsigned char> pixels(static_cast<size_t>(safeResolution) * safeResolution * channels, 0);
            if (target == editor::TerrainMapTarget::BlendMap){
                for (size_t i = 3; i < pixels.size(); i += 4){
                    pixels[i] = 255;
                }
            }
            setOwnedTextureData(texture, makeEditableTextureId(sceneProject->id, entity, target), safeResolution, safeResolution, format, channels, pixels);
            return true;
        }

        if (!hasLoadedData(texture)){
            return false;
        }

        TextureData& data = texture.getData();
        const bool shouldConvert = !texture.getPath(0).empty() || data.getChannels() != channels || data.getColorFormat() != format;
        if (shouldConvert){
            std::vector<unsigned char> pixels = convertTexturePixels(data, target);
            setOwnedTextureData(texture, makeEditableTextureId(sceneProject->id, entity, target), data.getWidth(), data.getHeight(), format, channels, pixels);
        }else{
            texture.getData().setDataOwned(true);
        }

        return true;
    }

    float readHeight(TextureData& data, int x, int y){
        if (!data.getData() || data.getWidth() <= 0 || data.getHeight() <= 0 || data.getChannels() <= 0){
            return 0.0f;
        }
        x = std::max(0, std::min(data.getWidth() - 1, x));
        y = std::max(0, std::min(data.getHeight() - 1, y));
        unsigned char* pixels = static_cast<unsigned char*>(data.getData());
        return pixels[(static_cast<size_t>(y) * data.getWidth() + x) * data.getChannels()] / 255.0f;
    }

    void writeHeight(TextureData& data, int x, int y, float value){
        unsigned char* pixels = static_cast<unsigned char*>(data.getData());
        if (!pixels){
            return;
        }
        const int channels = data.getChannels();
        const size_t index = (static_cast<size_t>(y) * data.getWidth() + x) * channels;
        unsigned char byteValue = clampByte(value * 255.0f);
        pixels[index] = byteValue;
        if (channels >= 3){
            pixels[index + 1] = byteValue;
            pixels[index + 2] = byteValue;
        }
        if (channels >= 4){
            pixels[index + 3] = 255;
        }
    }

    class TerrainTextureEditCmd: public editor::Command{
    private:
        Project* project;
        uint32_t sceneId;
        Entity entity;
        editor::TerrainMapTarget target;
        editor::TerrainMapSnapshot beforeSnapshot;
        editor::TerrainMapSnapshot afterSnapshot;
        bool wasModified = false;

        void apply(const editor::TerrainMapSnapshot& snapshot, bool restoreModifiedState){
            SceneProject* sceneProject = project->getScene(sceneId);
            if (!sceneProject || !sceneProject->scene->isEntityCreated(entity)){
                return;
            }
            TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(entity);
            if (!terrain){
                return;
            }

            Texture& texture = getTerrainTexture(*terrain, target);
            applySnapshotToTexture(texture, snapshot);

            if (target == editor::TerrainMapTarget::HeightMap){
                terrain->heightMapLoaded = false;
                terrain->needUpdateTerrain = true;
                terrain->needUpdateTexture = true;
            }else{
                terrain->needUpdateTexture = true;
            }

            if (project->isEntityInBundle(sceneId, entity)){
                project->bundlePropertyChanged(sceneId, entity, ComponentType::TerrainComponent, {getTerrainPropertyName(target)});
            }

            if (restoreModifiedState){
                sceneProject->isModified = wasModified;
            }else{
                sceneProject->isModified = true;
            }
        }

    public:
        TerrainTextureEditCmd(Project* project, uint32_t sceneId, Entity entity, editor::TerrainMapTarget target,
                              const editor::TerrainMapSnapshot& beforeSnapshot, const editor::TerrainMapSnapshot& afterSnapshot):
            project(project), sceneId(sceneId), entity(entity), target(target), beforeSnapshot(beforeSnapshot), afterSnapshot(afterSnapshot){}

        bool execute() override{
            SceneProject* sceneProject = project->getScene(sceneId);
            if (!sceneProject){
                return false;
            }
            wasModified = sceneProject->isModified;
            apply(afterSnapshot, false);
            return true;
        }

        void undo() override{
            apply(beforeSnapshot, true);
        }

        bool mergeWith(editor::Command* otherCommand) override{
            return false;
        }
    };

}

editor::TerrainEditWindow::TerrainEditWindow(Project* project){
    this->project = project;
    windowOpen = false;
    brushActive = false;
    normalizeBlendPaint = true;
    selectedSceneId = NULL_PROJECT_SCENE;
    selectedEntity = NULL_ENTITY;
    brushMode = TerrainBrushMode::Raise;
    brushShape = TerrainBrushShape::Circle;
    brushFalloff = TerrainBrushFalloff::Smooth;
    brushSize = 8.0f;
    brushStrength = 0.04f;
    flattenHeight = 0.5f;
    heightMapResolution = 512;
    blendMapResolution = 512;
}

editor::TerrainEditWindow::~TerrainEditWindow(){
}

SceneProject* editor::TerrainEditWindow::findSceneProject(Scene* scene) const{
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

SceneProject* editor::TerrainEditWindow::getTargetSceneProject() const{
    if (!project || selectedSceneId == NULL_PROJECT_SCENE){
        return nullptr;
    }
    return project->getScene(selectedSceneId);
}

bool editor::TerrainEditWindow::updateTargetFromSelection(){
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
    if (!sceneProject){
        selectedSceneId = NULL_PROJECT_SCENE;
        selectedEntity = NULL_ENTITY;
        return false;
    }

    std::vector<Entity> selected = project->getSelectedEntities(sceneProject->id);
    if (selected.size() == 1 && sceneProject->scene->findComponent<TerrainComponent>(selected[0])){
        selectedSceneId = sceneProject->id;
        selectedEntity = selected[0];
        return true;
    }

    selectedSceneId = NULL_PROJECT_SCENE;
    selectedEntity = NULL_ENTITY;
    brushActive = false;
    return false;
}

bool editor::TerrainEditWindow::hasValidTarget(SceneProject* sceneProject) const{
    SceneProject* targetScene = sceneProject ? sceneProject : getTargetSceneProject();
    return targetScene &&
           selectedEntity != NULL_ENTITY &&
           targetScene->scene->isEntityCreated(selectedEntity) &&
           targetScene->scene->findComponent<TerrainComponent>(selectedEntity) &&
           targetScene->scene->findComponent<Transform>(selectedEntity);
}

editor::TerrainMapTarget editor::TerrainEditWindow::getBrushTarget() const{
    return isHeightBrush() ? TerrainMapTarget::HeightMap : TerrainMapTarget::BlendMap;
}

bool editor::TerrainEditWindow::isHeightBrush() const{
    return brushMode == TerrainBrushMode::Raise ||
           brushMode == TerrainBrushMode::Lower ||
           brushMode == TerrainBrushMode::Smooth ||
           brushMode == TerrainBrushMode::Flatten;
}

bool editor::TerrainEditWindow::findTerrainHit(Scene* scene, const Ray& ray, Entity& entity, Vector3& localPoint, Vector3& worldPoint, float& localHeight) const{
    SceneProject* sceneProject = findSceneProject(scene);
    if (!sceneProject || sceneProject->id != selectedSceneId || !hasValidTarget(sceneProject)){
        return false;
    }

    entity = selectedEntity;
    Transform& transform = scene->getComponent<Transform>(entity);
    TerrainComponent& terrain = scene->getComponent<TerrainComponent>(entity);

    Matrix4 inverseModel = transform.modelMatrix.inverse();
    Vector3 localOrigin = inverseModel * ray.getOrigin();
    Vector3 localEnd = inverseModel * (ray.getOrigin() + ray.getDirection());
    Ray localRay(localOrigin, localEnd - localOrigin);

    RayReturn hit = localRay.intersects(Plane(Vector3(0.0f, 1.0f, 0.0f), 0.0f));
    if (!hit){
        return false;
    }

    float halfSize = terrain.terrainSize * 0.5f;
    if (hit.point.x < -halfSize || hit.point.x > halfSize || hit.point.z < -halfSize || hit.point.z > halfSize){
        return false;
    }

    localPoint = hit.point;
    localHeight = 0.0f;
    if (!terrain.heightMap.empty() && hasLoadedData(const_cast<Texture&>(terrain.heightMap))){
        TextureData& heightData = terrain.heightMap.getData();
        int px = static_cast<int>(((localPoint.x + halfSize) / terrain.terrainSize) * heightData.getWidth());
        int py = static_cast<int>(((localPoint.z + halfSize) / terrain.terrainSize) * heightData.getHeight());
        localHeight = readHeight(heightData, px, py) * terrain.maxHeight;
    }

    Vector3 surfacePoint(localPoint.x, localHeight, localPoint.z);
    worldPoint = transform.modelMatrix * surfacePoint;
    return true;
}

void editor::TerrainEditWindow::refreshTerrain(SceneProject* sceneProject, Entity entity, TerrainMapTarget target){
    if (!sceneProject){
        return;
    }
    TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(entity);
    if (!terrain){
        return;
    }

    if (target == TerrainMapTarget::HeightMap){
        terrain->heightMapLoaded = false;
        terrain->needUpdateTerrain = true;
        terrain->needUpdateTexture = true;
    }else{
        terrain->needUpdateTexture = true;
    }

    Texture& texture = getTerrainTexture(*terrain, target);
    texture.invalidateRender();
}

bool editor::TerrainEditWindow::applyBrush(SceneProject* sceneProject, Entity entity, const Vector3& localPoint){
    if (!sceneProject || !sceneProject->scene->findComponent<TerrainComponent>(entity)){
        return false;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entity);
    TerrainMapTarget target = getBrushTarget();
    int resolution = target == TerrainMapTarget::HeightMap ? heightMapResolution : blendMapResolution;
    if (!ensureEditableMap(sceneProject, entity, target, resolution)){
        return false;
    }

    Texture& texture = getTerrainTexture(terrain, target);
    TextureData& data = texture.getData();
    if (!data.getData() || data.getWidth() <= 0 || data.getHeight() <= 0){
        return false;
    }

    const float halfSize = terrain.terrainSize * 0.5f;
    const float u = (localPoint.x + halfSize) / terrain.terrainSize;
    const float v = (localPoint.z + halfSize) / terrain.terrainSize;
    const int centerX = static_cast<int>(u * static_cast<float>(data.getWidth() - 1));
    const int centerY = static_cast<int>(v * static_cast<float>(data.getHeight() - 1));
    const int radiusPixels = std::max(1, static_cast<int>((brushSize / terrain.terrainSize) * static_cast<float>(std::max(data.getWidth(), data.getHeight()))));

    unsigned char* pixels = static_cast<unsigned char*>(data.getData());
    const int width = data.getWidth();
    const int height = data.getHeight();
    const int channels = data.getChannels();

    int minX = std::max(0, centerX - radiusPixels);
    int maxX = std::min(width - 1, centerX + radiusPixels);
    int minY = std::max(0, centerY - radiusPixels);
    int maxY = std::min(height - 1, centerY + radiusPixels);

    std::vector<unsigned char> original;
    if (brushMode == TerrainBrushMode::Smooth){
        original = copyTexturePixels(data);
    }

    for (int y = minY; y <= maxY; y++){
        for (int x = minX; x <= maxX; x++){
            float dx = static_cast<float>(x - centerX) / static_cast<float>(radiusPixels);
            float dy = static_cast<float>(y - centerY) / static_cast<float>(radiusPixels);
            float distance = brushShape == TerrainBrushShape::Circle ? std::sqrt(dx * dx + dy * dy) : std::max(std::abs(dx), std::abs(dy));
            if (distance > 1.0f){
                continue;
            }

            float falloff = 1.0f;
            if (brushFalloff == TerrainBrushFalloff::Linear){
                falloff = 1.0f - distance;
            }else if (brushFalloff == TerrainBrushFalloff::Smooth){
                float t = 1.0f - distance;
                falloff = t * t * (3.0f - 2.0f * t);
            }

            float weight = std::max(0.0f, std::min(1.0f, brushStrength * falloff));
            if (target == TerrainMapTarget::HeightMap){
                float current = readHeight(data, x, y);
                float next = current;
                if (brushMode == TerrainBrushMode::Raise){
                    next = current + weight;
                }else if (brushMode == TerrainBrushMode::Lower){
                    next = current - weight;
                }else if (brushMode == TerrainBrushMode::Flatten){
                    next = current + (flattenHeight - current) * weight;
                }else if (brushMode == TerrainBrushMode::Smooth && !original.empty()){
                    float sum = 0.0f;
                    int count = 0;
                    for (int oy = -1; oy <= 1; oy++){
                        for (int ox = -1; ox <= 1; ox++){
                            int sx = std::max(0, std::min(width - 1, x + ox));
                            int sy = std::max(0, std::min(height - 1, y + oy));
                            sum += original[(static_cast<size_t>(sy) * width + sx) * channels] / 255.0f;
                            count++;
                        }
                    }
                    float average = count > 0 ? sum / static_cast<float>(count) : current;
                    next = current + (average - current) * weight;
                }
                writeHeight(data, x, y, next);
            }else{
                int paintChannel = 0;
                if (brushMode == TerrainBrushMode::PaintGreen){
                    paintChannel = 1;
                }else if (brushMode == TerrainBrushMode::PaintBlue){
                    paintChannel = 2;
                }

                const size_t index = (static_cast<size_t>(y) * width + x) * channels;
                if (channels >= 4){
                    for (int c = 0; c < 3; c++){
                        float current = pixels[index + c] / 255.0f;
                        float targetValue = c == paintChannel ? 1.0f : (normalizeBlendPaint ? 0.0f : current);
                        pixels[index + c] = clampByte((current + (targetValue - current) * weight) * 255.0f);
                    }
                    pixels[index + 3] = 255;
                }
            }
        }
    }

    refreshTerrain(sceneProject, entity, target);
    return true;
}

void editor::TerrainEditWindow::clearStroke(){
    stroke = ActiveStroke();
}

bool editor::TerrainEditWindow::createMapForTarget(TerrainMapTarget target, int resolution){
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        return false;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(selectedEntity);
    Texture& texture = getTerrainTexture(terrain, target);

    TerrainMapSnapshot before = captureSnapshot(texture, false);
    TerrainMapSnapshot after;
    after.empty = false;
    after.id = makeEditableTextureId(sceneProject->id, selectedEntity, target);
    after.minFilter = texture.getMinFilter();
    after.magFilter = texture.getMagFilter();
    after.wrapU = texture.getWrapU();
    after.wrapV = texture.getWrapV();
    after.colorFormat = expectedFormat(target);
    after.channels = expectedChannels(target);
    after.width = std::max(2, resolution);
    after.height = std::max(2, resolution);
    after.pixels.assign(static_cast<size_t>(after.width) * after.height * after.channels, 0);

    if (target == TerrainMapTarget::BlendMap){
        for (size_t i = 3; i < after.pixels.size(); i += 4){
            after.pixels[i] = 255;
        }
    }

    if (snapshotsEqual(before, after)){
        return false;
    }

    CommandHandle::get(sceneProject->id)->addCommandNoMerge(new TerrainTextureEditCmd(project, sceneProject->id, selectedEntity, target, before, after));
    return true;
}

void editor::TerrainEditWindow::show(){
    if (!windowOpen){
        brushActive = false;
        clearStroke();
        return;
    }

    updateTargetFromSelection();

    ImGui::SetNextWindowSize(ImVec2(460.0f, 500.0f), ImGuiCond_FirstUseEver);

    bool wasOpen = windowOpen;
    if (!ImGui::Begin(WINDOW_NAME, &windowOpen)){
        ImGui::End();
        if (wasOpen && !windowOpen){
            brushActive = false;
            clearStroke();
        }
        return;
    }

    SceneProject* sceneProject = getTargetSceneProject();
    bool validTarget = hasValidTarget(sceneProject);

    if (!validTarget){
        brushActive = false;
        ImGui::Spacing();
        ImGui::TextDisabled("No terrain is selected");
        ImGui::End();
        return;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(selectedEntity);

    ImGui::TextUnformatted(sceneProject->scene->getEntityName(selectedEntity).c_str());
    TerrainMapInfo heightInfo = getTerrainMapInfo(terrain.heightMap);
    TerrainMapInfo blendInfo = getTerrainMapInfo(terrain.blendMap);
    const bool hasHeightMap = heightInfo.present;
    const bool hasBlendMap = blendInfo.present;

    if (brushActive && ((isHeightBrush() && !hasHeightMap) || (!isHeightBrush() && !hasBlendMap))){
        brushActive = false;
        clearStroke();
    }

    ImGui::SeparatorText("Maps");

    heightMapResolution = std::max(2, heightMapResolution);
    blendMapResolution = std::max(2, blendMapResolution);

    const float fontSize = ImGui::GetFontSize();
    const float resolutionWidth = 6.5f * fontSize;
    const float actionWidth = std::max(8.5f * fontSize, ImGui::CalcTextSize(ICON_FA_ARROWS_ROTATE "  Recreate").x + ImGui::GetStyle().FramePadding.x * 2.0f);

    if (ImGui::BeginTable("terrain_map_status", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)){
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 8.8f * fontSize);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, resolutionWidth);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, actionWidth);
        ImGui::TableHeadersRow();

        auto mapRow = [&](TerrainMapTarget target, const char* icon, const char* label, const TerrainMapInfo& info, int& resolution, const char* id){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            std::string mapLabel = std::string(icon) + "  " + label;
            ImGui::TextUnformatted(mapLabel.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            showTerrainMapStatus(info);
            if (info.present && !info.sizeKnown){
                showTooltip("Map is assigned, but its texture data is not available yet.");
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputInt((std::string("##") + id + "_resolution").c_str(), &resolution);
            showTooltip(info.present ? "Resolution used when recreating this map" : "Resolution used when creating this map");
            resolution = std::max(2, resolution);

            ImGui::TableSetColumnIndex(3);
            const char* actionText = info.present ? ICON_FA_ARROWS_ROTATE "  Recreate" : ICON_FA_PLUS "  Create";
            std::string actionLabel = std::string(actionText) + "##" + id + "_map_action";
            if (ImGui::Button(actionLabel.c_str(), ImVec2(-1.0f, 0.0f))){
                createMapForTarget(target, resolution);
            }
            showTooltip(info.present ? "Reset this map with the chosen resolution" : "Create an editable map with the chosen resolution");
        };

        mapRow(TerrainMapTarget::HeightMap, ICON_FA_MOUNTAIN, "Heightmap", heightInfo, heightMapResolution, "height");
        mapRow(TerrainMapTarget::BlendMap, ICON_FA_PALETTE, "Blendmap", blendInfo, blendMapResolution, "blend");

        ImGui::EndTable();
    }

    ImGui::SeparatorText("Sculpt");

    const ImVec2 toolButtonSize(ImGui::GetFrameHeight() * 1.35f, ImGui::GetFrameHeight() * 1.35f);

    auto brushButton = [&](TerrainBrushMode mode, const char* icon, const char* id, const char* tooltip){
        bool selected = brushActive && brushMode == mode;
        if (iconButton(icon, id, tooltip, selected, toolButtonSize)){
            if (selected){
                brushActive = false;
                clearStroke();
            }else{
                brushMode = mode;
                brushActive = true;
            }
        }
    };

    ImGui::BeginDisabled(!hasHeightMap);
    brushButton(TerrainBrushMode::Raise, ICON_FA_ARROW_UP, "terrain_raise", "Raise terrain");
    ImGui::SameLine();
    brushButton(TerrainBrushMode::Lower, ICON_FA_ARROW_DOWN, "terrain_lower", "Lower terrain");
    ImGui::SameLine();
    brushButton(TerrainBrushMode::Smooth, ICON_FA_WATER, "terrain_smooth", "Smooth terrain");
    ImGui::SameLine();
    brushButton(TerrainBrushMode::Flatten, ICON_FA_GRIP_LINES, "terrain_flatten", "Flatten terrain");
    ImGui::EndDisabled();
    if (!hasHeightMap){
        ImGui::TextDisabled("Heightmap missing");
    }

    ImGui::SeparatorText("Paint");

    auto paintButton = [&](TerrainBrushMode mode, const char* id, const char* tooltip, const ImVec4& color){
        bool selected = brushActive && brushMode == mode;
        if (colorIconButton(ICON_FA_BRUSH, id, tooltip, selected, color, toolButtonSize)){
            if (selected){
                brushActive = false;
                clearStroke();
            }else{
                brushMode = mode;
                brushActive = true;
            }
        }
    };

    ImGui::BeginDisabled(!hasBlendMap);
    paintButton(TerrainBrushMode::PaintRed, "terrain_paint_red", "Paint red blend channel", ImVec4(0.95f, 0.28f, 0.20f, 1.0f));
    ImGui::SameLine();
    paintButton(TerrainBrushMode::PaintGreen, "terrain_paint_green", "Paint green blend channel", ImVec4(0.28f, 0.78f, 0.28f, 1.0f));
    ImGui::SameLine();
    paintButton(TerrainBrushMode::PaintBlue, "terrain_paint_blue", "Paint blue blend channel", ImVec4(0.25f, 0.48f, 0.95f, 1.0f));
    ImGui::EndDisabled();
    if (!hasBlendMap){
        ImGui::TextDisabled("Blendmap missing");
    }

    ImGui::SeparatorText("Brush");

    const bool brushTargetAvailable = isHeightBrush() ? hasHeightMap : hasBlendMap;
    ImGui::BeginDisabled(!brushTargetAvailable);

    if (iconButton(ICON_FA_CIRCLE, "shape_circle", "Circle shape", brushShape == TerrainBrushShape::Circle, toolButtonSize)){
        brushShape = TerrainBrushShape::Circle;
    }
    ImGui::SameLine();
    if (iconButton(ICON_FA_SQUARE, "shape_square", "Square shape", brushShape == TerrainBrushShape::Square, toolButtonSize)){
        brushShape = TerrainBrushShape::Square;
    }
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    if (iconButton(ICON_FA_WATER, "falloff_smooth", "Smooth falloff", brushFalloff == TerrainBrushFalloff::Smooth, toolButtonSize)){
        brushFalloff = TerrainBrushFalloff::Smooth;
    }
    ImGui::SameLine();
    if (iconButton(ICON_FA_SLASH, "falloff_linear", "Linear falloff", brushFalloff == TerrainBrushFalloff::Linear, toolButtonSize)){
        brushFalloff = TerrainBrushFalloff::Linear;
    }
    ImGui::SameLine();
    if (iconButton(ICON_FA_CIRCLE_DOT, "falloff_constant", "Constant falloff", brushFalloff == TerrainBrushFalloff::Constant, toolButtonSize)){
        brushFalloff = TerrainBrushFalloff::Constant;
    }

    const float maxBrushSize = std::max(1.0f, terrain.terrainSize);
    brushSize = std::clamp(brushSize, 0.1f, maxBrushSize);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##brush_size", &brushSize, 0.1f, maxBrushSize, ICON_FA_CIRCLE "  %.2f");
    showTooltip("Brush size");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##brush_strength", &brushStrength, 0.001f, 0.25f, ICON_FA_GAUGE_HIGH "  %.3f");
    showTooltip("Brush strength");

    if (brushMode == TerrainBrushMode::Flatten){
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##flatten_height", &flattenHeight, 0.0f, 1.0f, ICON_FA_GRIP_LINES "  %.3f");
        showTooltip("Flatten height");
    }

    if (!isHeightBrush()){
        if (iconButton(ICON_FA_SCALE_BALANCED, "normalize_blend", "Normalize blend paint", normalizeBlendPaint, toolButtonSize)){
            normalizeBlendPaint = !normalizeBlendPaint;
        }
    }

    ImGui::EndDisabled();

    ImGui::End();

    if (wasOpen && !windowOpen){
        brushActive = false;
        clearStroke();
    }
}

void editor::TerrainEditWindow::openForEntity(Entity entity, uint32_t sceneId){
    windowOpen = true;
    selectedSceneId = sceneId;
    selectedEntity = entity;
}

bool editor::TerrainEditWindow::isEditingScene(Scene* scene) const{
    if (!windowOpen || !brushActive || !scene){
        return false;
    }
    SceneProject* sceneProject = findSceneProject(scene);
    if (!sceneProject || sceneProject->id != selectedSceneId || !hasValidTarget(sceneProject)){
        return false;
    }

    TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(selectedEntity);
    if (!terrain){
        return false;
    }

    return !getTerrainTexture(*terrain, getBrushTarget()).empty();
}

bool editor::TerrainEditWindow::beginStroke(Scene* scene, const Ray& ray){
    if (!isEditingScene(scene)){
        return false;
    }

    SceneProject* sceneProject = findSceneProject(scene);
    Entity entity = NULL_ENTITY;
    Vector3 localPoint;
    Vector3 worldPoint;
    float localHeight = 0.0f;
    if (!findTerrainHit(scene, ray, entity, localPoint, worldPoint, localHeight)){
        return false;
    }

    TerrainMapTarget target = getBrushTarget();
    Texture& texture = getTerrainTexture(scene->getComponent<TerrainComponent>(entity), target);

    clearStroke();
    stroke.active = true;
    stroke.sceneId = sceneProject->id;
    stroke.entity = entity;
    stroke.target = target;
    stroke.beforeSnapshot = captureSnapshot(texture, false);

    return applyBrush(sceneProject, entity, localPoint);
}

bool editor::TerrainEditWindow::paintStroke(Scene* scene, const Ray& ray){
    if (!stroke.active || !isEditingScene(scene)){
        return false;
    }

    SceneProject* sceneProject = findSceneProject(scene);
    Entity entity = NULL_ENTITY;
    Vector3 localPoint;
    Vector3 worldPoint;
    float localHeight = 0.0f;
    if (!findTerrainHit(scene, ray, entity, localPoint, worldPoint, localHeight)){
        return false;
    }
    if (entity != stroke.entity || sceneProject->id != stroke.sceneId){
        return false;
    }

    return applyBrush(sceneProject, entity, localPoint);
}

void editor::TerrainEditWindow::endStroke(){
    if (!stroke.active){
        return;
    }

    SceneProject* sceneProject = project->getScene(stroke.sceneId);
    if (sceneProject && sceneProject->scene->isEntityCreated(stroke.entity)){
        TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(stroke.entity);
        if (terrain){
            Texture& texture = getTerrainTexture(*terrain, stroke.target);
            TerrainMapSnapshot after = captureSnapshot(texture, true);
            if (!snapshotsEqual(stroke.beforeSnapshot, after)){
                CommandHandle::get(stroke.sceneId)->addCommandNoMerge(new TerrainTextureEditCmd(project, stroke.sceneId, stroke.entity, stroke.target, stroke.beforeSnapshot, after));
            }
        }
    }

    clearStroke();
}

bool editor::TerrainEditWindow::updateCursor(Scene* scene, const Ray& ray, TerrainBrushCursor& cursor) const{
    if (!isEditingScene(scene)){
        return false;
    }

    Entity entity = NULL_ENTITY;
    Vector3 localPoint;
    Vector3 worldPoint;
    float localHeight = 0.0f;
    if (!findTerrainHit(scene, ray, entity, localPoint, worldPoint, localHeight)){
        return false;
    }

    Transform& transform = scene->getComponent<Transform>(entity);
    Vector3 centerLocal(localPoint.x, localHeight + 0.02f, localPoint.z);
    Vector3 axisXLocal(localPoint.x + brushSize, localHeight + 0.02f, localPoint.z);
    Vector3 axisZLocal(localPoint.x, localHeight + 0.02f, localPoint.z + brushSize);

    cursor.visible = true;
    cursor.center = transform.modelMatrix * centerLocal;
    cursor.axisX = (transform.modelMatrix * axisXLocal) - cursor.center;
    cursor.axisZ = (transform.modelMatrix * axisZLocal) - cursor.center;
    cursor.shape = brushShape;
    return true;
}
