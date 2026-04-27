#include "TerrainEditWindow.h"

#include "Backend.h"
#include "Catalog.h"
#include "Out.h"
#include "command/CommandHandle.h"
#include "external/IconsFontAwesome6.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <system_error>
#include <unordered_set>

using namespace doriax;
using namespace doriax::editor;

uint64_t editor::TerrainEditWindow::s_editTextureCounter = 1;

bool editor::TerrainEditWindow::loadTerrainTextureDataFromPath(Project* project, const std::string& path, TextureData& data){
    if (path.empty()){
        return false;
    }

    fs::path texturePath(path);
    if (texturePath.is_relative() && project && !project->getProjectPath().empty()){
        texturePath = project->getProjectPath() / texturePath;
    }

    if (!data.loadTextureFromFile(texturePath.string().c_str())){
        return false;
    }

    data.setDataOwned(true);
    return data.getData() && data.getWidth() > 0 && data.getHeight() > 0 && data.getChannels() > 0;
}

void editor::TerrainEditWindow::showTooltip(const char* text, ImGuiHoveredFlags flags){
    if (ImGui::IsItemHovered(flags)){
        ImGui::SetTooltip("%s", text);
    }
}

bool editor::TerrainEditWindow::iconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec2& size){
    std::string label = "##" + std::string(id);

    if (selected){
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    }

    bool clicked = ImGui::Button(label.c_str(), size);

    ImVec2 buttonMin = ImGui::GetItemRectMin();
    ImVec2 buttonMax = ImGui::GetItemRectMax();
    ImVec2 textSize = ImGui::CalcTextSize(icon);
    ImVec2 textPos(
        buttonMin.x + (buttonMax.x - buttonMin.x - textSize.x) * 0.5f,
        buttonMin.y + (buttonMax.y - buttonMin.y - textSize.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), icon);

    if (selected){
        ImGui::PopStyleColor(3);
    }

    showTooltip(tooltip, ImGuiHoveredFlags_AllowWhenDisabled);
    return clicked;
}

bool editor::TerrainEditWindow::colorIconButton(const char* icon, const char* id, const char* tooltip, bool selected, const ImVec4& color, const ImVec2& size){
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool clicked = iconButton(icon, id, tooltip, selected, size);
    ImGui::PopStyleColor();
    return clicked;
}

std::string editor::TerrainEditWindow::makeEditableTextureId(uint32_t sceneId, Entity entity, TerrainMapTarget target){
    const char* suffix = target == TerrainMapTarget::HeightMap ? "height" : "blend";
    return "__terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix + "_" + std::to_string(s_editTextureCounter++);
}

std::string editor::TerrainEditWindow::makeEditableTexturePath(Project* project, uint32_t sceneId, Entity entity, TerrainMapTarget target){
    fs::path baseDir = project ? project->getTerrainMapsDir() : fs::path("terrain_maps");

    const char* suffix = target == TerrainMapTarget::HeightMap ? "height" : "blend";
    for (int attempt = 0; attempt < 10000; attempt++){
        const uint64_t serial = s_editTextureCounter++;
        fs::path candidatePath = baseDir / ("terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix + "_" + std::to_string(serial) + ".png");
        if (!project || project->getProjectPath().empty()){
            return candidatePath.generic_string();
        }

        std::error_code ec;
        if (!fs::exists(candidatePath, ec)){
            std::error_code ec2;
            fs::path relPath = fs::relative(candidatePath, project->getProjectPath(), ec2);
            return (!ec2 && !relPath.empty()) ? relPath.generic_string() : candidatePath.generic_string();
        }
    }

    fs::path fallbackPath = baseDir / ("terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix + ".png");
    if (project && !project->getProjectPath().empty()){
        std::error_code ec;
        fs::path relPath = fs::relative(fallbackPath, project->getProjectPath(), ec);
        if (!ec && !relPath.empty()) return relPath.generic_string();
    }
    return fallbackPath.generic_string();
}

bool editor::TerrainEditWindow::isEditableTexturePath(const std::string& path){
    if (path.empty()){
        return false;
    }

    fs::path texturePath(path);
    const std::string parentName = texturePath.parent_path().filename().string();
    const std::string filename = texturePath.filename().string();
    return parentName == "terrain_maps" && filename.rfind("terrain_edit_", 0) == 0;
}

bool editor::TerrainEditWindow::isOwnedEditableTexturePath(const std::string& path, uint32_t sceneId, Entity entity, TerrainMapTarget target){
    if (!isEditableTexturePath(path)){
        return false;
    }

    fs::path texturePath(path);
    const char* suffix = target == TerrainMapTarget::HeightMap ? "height" : "blend";
    const std::string expectedStem = "terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix;
    const std::string stem = texturePath.stem().string();
    return stem == expectedStem || stem.rfind(expectedStem + "_", 0) == 0;
}

Texture& editor::TerrainEditWindow::getTerrainTexture(TerrainComponent& terrain, TerrainMapTarget target){
    return target == TerrainMapTarget::HeightMap ? terrain.heightMap : terrain.blendMap;
}

const char* editor::TerrainEditWindow::getTerrainPropertyName(TerrainMapTarget target){
    return target == TerrainMapTarget::HeightMap ? "heightMap" : "blendMap";
}

int editor::TerrainEditWindow::expectedChannels(TerrainMapTarget target){
    return target == TerrainMapTarget::HeightMap ? 1 : 4;
}

ColorFormat editor::TerrainEditWindow::expectedFormat(TerrainMapTarget target){
    return target == TerrainMapTarget::HeightMap ? ColorFormat::RED : ColorFormat::RGBA;
}

unsigned char editor::TerrainEditWindow::clampByte(float value){
    return static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, value)));
}

bool editor::TerrainEditWindow::writeTextureFile(Project* project, const std::string& relativePath, int width, int height, int channels, const std::vector<unsigned char>& pixels){
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    if (!project || project->getProjectPath().empty() || relativePath.empty() || width <= 0 || height <= 0 || channels <= 0 || pixels.size() < expectedSize){
        return false;
    }

    fs::path outputPath(relativePath);
    if (outputPath.is_relative()){
        outputPath = project->getProjectPath() / outputPath;
    }

    std::error_code ec;
    fs::create_directories(outputPath.parent_path(), ec);
    if (ec){
        Out::warning("Failed to create terrain texture directory: %s", outputPath.parent_path().string().c_str());
        return false;
    }

    const int written = stbi_write_png(outputPath.string().c_str(), width, height, channels, pixels.data(), width * channels);
    if (!written){
        Out::warning("Failed to write terrain texture: %s", outputPath.string().c_str());
        return false;
    }

    return true;
}

bool editor::TerrainEditWindow::setFileBackedTextureData(Project* project, Texture& texture, const std::string& relativePath, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels){
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    if (width <= 0 || height <= 0 || channels <= 0 || pixels.size() < expectedSize){
        return false;
    }

    if (!writeTextureFile(project, relativePath, width, height, channels, pixels)){
        return false;
    }

    // Set the path first so destroy() clears any prior pool entry for this texture, and paths[0]
    // is updated so the scene serializer references the file on save.
    texture.setPath(relativePath);
    texture.setReleaseDataAfterLoad(false);

    // Pre-populate TextureDataPool with an in-memory copy of the pixels we just wrote. This bypasses
    // async file loading (which would otherwise return Loading and force the caller to fall back to
    // an in-memory-only texture, dropping the file path and breaking persistence on the first edit).
    unsigned char* raw = static_cast<unsigned char*>(std::malloc(expectedSize));
    if (!raw){
        return false;
    }
    std::memcpy(raw, pixels.data(), expectedSize);

    TextureData seed(width, height, static_cast<unsigned int>(expectedSize), format, channels, raw);
    seed.setDataOwned(false);

    std::array<TextureData, 6> arr;
    arr[0] = seed;
    auto poolEntry = TextureDataPool::get(relativePath, arr);

    bool seedAccepted = poolEntry && poolEntry->at(0).getData() == raw;
    if (!seedAccepted){
        // An existing pool entry was reused (use_count > 1 prevented eviction). Free our buffer
        // to avoid a leak; the existing entry will own its own pixels.
        std::free(raw);
    }

    TextureLoadResult result = texture.load();
    if (result.state != ResourceLoadState::Finished || !result.data || !texture.getData().getData()){
        return false;
    }

    TextureData& data = texture.getData();
    if (data.getChannels() != channels || data.getColorFormat() != format){
        return false;
    }

    if (seedAccepted){
        data.setDataOwned(true);
    }
    return true;
}

bool editor::TerrainEditWindow::hasLoadedData(Texture& texture){
    if (texture.empty() || texture.isFramebuffer()){
        return false;
    }

    texture.setReleaseDataAfterLoad(false);
    TextureLoadResult result = texture.load();
    return result.state == ResourceLoadState::Finished && result.data && texture.getData().getData();
}

editor::TerrainEditWindow::TerrainMapInfo editor::TerrainEditWindow::getTerrainMapInfo(Texture& texture){
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

std::string editor::TerrainEditWindow::getTerrainMapStatusText(const TerrainMapInfo& info){
    if (!info.present){
        return std::string(ICON_FA_TRIANGLE_EXCLAMATION) + "  Missing";
    }
    if (info.sizeKnown){
        return std::string(ICON_FA_CIRCLE_CHECK) + "  " + std::to_string(info.width) + " x " + std::to_string(info.height);
    }
    return std::string(ICON_FA_TRIANGLE_EXCLAMATION) + "  Size unavailable";
}

void editor::TerrainEditWindow::showTerrainMapStatus(const TerrainMapInfo& info){
    std::string status = getTerrainMapStatusText(info);
    if (!info.present){
        ImGui::TextDisabled("%s", status.c_str());
    }else if (!info.sizeKnown){
        ImGui::TextColored(ImVec4(0.95f, 0.67f, 0.24f, 1.0f), "%s", status.c_str());
    }else{
        ImGui::TextUnformatted(status.c_str());
    }
}

std::vector<unsigned char> editor::TerrainEditWindow::copyTexturePixels(TextureData& data){
    std::vector<unsigned char> pixels;
    if (!data.getData() || data.getSize() == 0){
        return pixels;
    }

    pixels.resize(data.getSize());
    std::memcpy(pixels.data(), data.getData(), data.getSize());
    return pixels;
}

std::vector<unsigned char> editor::TerrainEditWindow::convertTexturePixels(TextureData& data, TerrainMapTarget target){
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
            if (target == TerrainMapTarget::HeightMap){
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

void editor::TerrainEditWindow::setOwnedTextureData(Texture& texture, const std::string& id, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels){
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

TerrainMapSnapshot editor::TerrainEditWindow::captureSnapshot(Project* project, Texture& texture, bool forcePixels){
    TerrainMapSnapshot snapshot;
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
    }else if (!snapshot.path.empty()){
        TextureData fileData;
        if (loadTerrainTextureDataFromPath(project, snapshot.path, fileData)){
            snapshot.width = fileData.getWidth();
            snapshot.height = fileData.getHeight();
            snapshot.channels = fileData.getChannels();
            snapshot.colorFormat = fileData.getColorFormat();
            snapshot.pixels = copyTexturePixels(fileData);
        }
    }

    return snapshot;
}

bool editor::TerrainEditWindow::snapshotsEqual(const TerrainMapSnapshot& a, const TerrainMapSnapshot& b){
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

void editor::TerrainEditWindow::applySnapshotToTexture(Project* project, Texture& texture, const TerrainMapSnapshot& snapshot){
    if (snapshot.empty){
        texture.destroy();
        texture = Texture();
        return;
    }

    if (!snapshot.pixels.empty() && snapshot.width > 0 && snapshot.height > 0 && snapshot.channels > 0){
        bool restoredFromFile = false;
        if (!snapshot.path.empty()){
            restoredFromFile = setFileBackedTextureData(project, texture, snapshot.path, snapshot.width, snapshot.height, snapshot.colorFormat, snapshot.channels, snapshot.pixels);
        }
        if (!restoredFromFile){
            std::string id = snapshot.id.empty() ? ("__terrain_edit_snapshot_" + std::to_string(s_editTextureCounter++)) : snapshot.id;
            setOwnedTextureData(texture, id, snapshot.width, snapshot.height, snapshot.colorFormat, snapshot.channels, snapshot.pixels);
        }
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

void editor::TerrainEditWindow::cleanUnusedTerrainMaps(Project* project){
    if (!project || project->getProjectPath().empty()){
        return;
    }

    std::unordered_set<std::string> activeFiles;
    for (SceneProject& sceneProject : project->getScenes()){
        if (!sceneProject.scene){
            continue;
        }

        for (int i = 0; i < sceneProject.entities.size(); i++){
            Entity entity = sceneProject.entities[i];
            TerrainComponent* terrain = sceneProject.scene->findComponent<TerrainComponent>(entity);
            if (terrain){
                if (!terrain->heightMap.getPath(0).empty() && isEditableTexturePath(terrain->heightMap.getPath(0))){
                    activeFiles.insert(fs::path(terrain->heightMap.getPath(0)).filename().string());
                }
                if (!terrain->blendMap.getPath(0).empty() && isEditableTexturePath(terrain->blendMap.getPath(0))){
                    activeFiles.insert(fs::path(terrain->blendMap.getPath(0)).filename().string());
                }
            }
        }
    }

    fs::path absoluteBaseDir = project->getTerrainMapsDir();

    std::error_code ec;
    if (!fs::exists(absoluteBaseDir, ec) || !fs::is_directory(absoluteBaseDir, ec)){
        return;
    }

    for (const auto& entry : fs::directory_iterator(absoluteBaseDir, ec)){
        if (entry.is_regular_file(ec)){
            std::string filename = entry.path().filename().string();
            if (filename.rfind("terrain_edit_", 0) == 0){
                if (activeFiles.find(filename) == activeFiles.end()){
                    fs::remove(entry.path(), ec);
                    Out::info("Garbage collected old terrain map: %s", filename.c_str());
                }
            }
        }
    }
}

bool editor::TerrainEditWindow::ensureEditableMap(Project* project, SceneProject* sceneProject, Entity entity, TerrainMapTarget target, int resolution){
    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entity);
    Texture& texture = getTerrainTexture(terrain, target);
    const int channels = expectedChannels(target);
    const ColorFormat format = expectedFormat(target);

    if (texture.empty()){
        const int safeResolution = std::max(2, resolution);
        std::vector<unsigned char> pixels(static_cast<size_t>(safeResolution) * safeResolution * channels, 0);
        if (target == TerrainMapTarget::BlendMap){
            for (size_t i = 3; i < pixels.size(); i += 4){
                pixels[i] = 255;
            }
        }
        const std::string path = makeEditableTexturePath(project, sceneProject->id, entity, target);
        if (!setFileBackedTextureData(project, texture, path, safeResolution, safeResolution, format, channels, pixels)){
            setOwnedTextureData(texture, makeEditableTextureId(sceneProject->id, entity, target), safeResolution, safeResolution, format, channels, pixels);
        }
        return true;
    }

    const std::string path = texture.getPath(0);
    TextureData fileData;
    TextureData* data = nullptr;
    bool loadedFromFile = false;

    if (hasLoadedData(texture)){
        data = &texture.getData();
    }else if (!path.empty() && loadTerrainTextureDataFromPath(project, path, fileData)){
        data = &fileData;
        loadedFromFile = true;
    }else{
        return false;
    }

    const bool needsEditableFile = path.empty() || !isOwnedEditableTexturePath(path, sceneProject->id, entity, target);
    const bool shouldConvert = loadedFromFile || needsEditableFile || data->getChannels() != channels || data->getColorFormat() != format;
    if (shouldConvert){
        std::vector<unsigned char> pixels = convertTexturePixels(*data, target);
        const std::string editablePath = needsEditableFile ? makeEditableTexturePath(project, sceneProject->id, entity, target) : path;
        if (!setFileBackedTextureData(project, texture, editablePath, data->getWidth(), data->getHeight(), format, channels, pixels)){
            setOwnedTextureData(texture, makeEditableTextureId(sceneProject->id, entity, target), data->getWidth(), data->getHeight(), format, channels, pixels);
        }
    }else{
        texture.getData().setDataOwned(true);
    }

    return true;
}

float editor::TerrainEditWindow::readHeight(TextureData& data, int x, int y){
    if (!data.getData() || data.getWidth() <= 0 || data.getHeight() <= 0 || data.getChannels() <= 0){
        return 0.0f;
    }
    x = std::max(0, std::min(data.getWidth() - 1, x));
    y = std::max(0, std::min(data.getHeight() - 1, y));
    unsigned char* pixels = static_cast<unsigned char*>(data.getData());
    return pixels[(static_cast<size_t>(y) * data.getWidth() + x) * data.getChannels()] / 255.0f;
}

void editor::TerrainEditWindow::writeHeight(TextureData& data, int x, int y, float value){
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

bool editor::TerrainEditWindow::sampleTerrainHeightPixels(const unsigned char* pixels, int width, int height, int channels, float terrainSize, float maxHeight, float localX, float localZ, float& localHeight){
    if (terrainSize <= std::numeric_limits<float>::epsilon()){
        return false;
    }

    const float halfSize = terrainSize * 0.5f;
    if (localX < -halfSize || localX > halfSize || localZ < -halfSize || localZ > halfSize){
        return false;
    }

    localHeight = 0.0f;
    if (!pixels || width <= 0 || height <= 0 || channels <= 0){
        return true;
    }

    const float normalizedX = std::clamp((localX + halfSize) / terrainSize, 0.0f, 1.0f);
    const float normalizedZ = std::clamp((localZ + halfSize) / terrainSize, 0.0f, 1.0f);
    const float texelX = normalizedX * static_cast<float>(width - 1);
    const float texelZ = normalizedZ * static_cast<float>(height - 1);
    const int lowerX = std::clamp(static_cast<int>(std::floor(texelX)), 0, width - 1);
    const int lowerZ = std::clamp(static_cast<int>(std::floor(texelZ)), 0, height - 1);
    const int upperX = std::min(lowerX + 1, width - 1);
    const int upperZ = std::min(lowerZ + 1, height - 1);
    const float blendX = texelX - static_cast<float>(lowerX);
    const float blendZ = texelZ - static_cast<float>(lowerZ);

    auto samplePixel = [&](int sampleX, int sampleZ){
        const size_t index = (static_cast<size_t>(sampleZ) * static_cast<size_t>(width) + static_cast<size_t>(sampleX)) * static_cast<size_t>(channels);
        return pixels[index] / 255.0f;
    };

    const float height00 = samplePixel(lowerX, lowerZ);
    const float height10 = samplePixel(upperX, lowerZ);
    const float height01 = samplePixel(lowerX, upperZ);
    const float height11 = samplePixel(upperX, upperZ);
    const float lowerBlend = height00 + (height10 - height00) * blendX;
    const float upperBlend = height01 + (height11 - height01) * blendX;
    localHeight = (lowerBlend + (upperBlend - lowerBlend) * blendZ) * maxHeight;
    return true;
}

bool editor::TerrainEditWindow::raycastTerrainSurface(const Ray& localRay, TerrainComponent& terrain, const ActiveStroke* activeStroke, Vector3& localPoint, float& localHeight){
    const unsigned char* currentPixels = nullptr;
    int currentWidth = 0;
    int currentHeight = 0;
    int currentChannels = 0;

    if (!terrain.heightMap.empty() && hasLoadedData(terrain.heightMap)){
        TextureData& heightData = terrain.heightMap.getData();
        currentPixels = static_cast<const unsigned char*>(heightData.getData());
        currentWidth = heightData.getWidth();
        currentHeight = heightData.getHeight();
        currentChannels = heightData.getChannels();
    }

    auto sampleCurrentHeight = [&](float localX, float localZ, float& sampledHeight){
        return sampleTerrainHeightPixels(currentPixels, currentWidth, currentHeight, currentChannels, terrain.terrainSize, terrain.maxHeight, localX, localZ, sampledHeight);
    };

    const bool useHeightReference = activeStroke &&
                                    activeStroke->active &&
                                    activeStroke->target == TerrainMapTarget::HeightMap &&
                                    activeStroke->heightReferenceValid &&
                                    !activeStroke->heightReferencePixels.empty();

    auto sampleRaycastHeight = [&](float localX, float localZ, float& sampledHeight){
        if (useHeightReference){
            return sampleTerrainHeightPixels(activeStroke->heightReferencePixels.data(),
                                             activeStroke->heightReferenceWidth,
                                             activeStroke->heightReferenceHeight,
                                             activeStroke->heightReferenceChannels,
                                             activeStroke->heightReferenceTerrainSize,
                                             activeStroke->heightReferenceMaxHeight,
                                             localX,
                                             localZ,
                                             sampledHeight);
        }
        return sampleCurrentHeight(localX, localZ, sampledHeight);
    };

    const float halfSize = terrain.terrainSize * 0.5f;
    const float surfaceMinHeight = std::min(0.0f, useHeightReference ? activeStroke->heightReferenceMaxHeight : terrain.maxHeight);
    const float surfaceMaxHeight = std::max(0.0f, useHeightReference ? activeStroke->heightReferenceMaxHeight : terrain.maxHeight);
    const Vector3 rayOrigin = localRay.getOrigin();
    const Vector3 rayDirection = localRay.getDirection();

    float rayEntry = 0.0f;
    float rayExit = 1.0f;
    auto clipAxis = [&](float axisOrigin, float axisDirection, float minValue, float maxValue){
        if (std::abs(axisDirection) <= std::numeric_limits<float>::epsilon()){
            return axisOrigin >= minValue && axisOrigin <= maxValue;
        }

        float nearParameter = (minValue - axisOrigin) / axisDirection;
        float farParameter = (maxValue - axisOrigin) / axisDirection;
        if (nearParameter > farParameter){
            std::swap(nearParameter, farParameter);
        }
        rayEntry = std::max(rayEntry, nearParameter);
        rayExit = std::min(rayExit, farParameter);
        return rayEntry <= rayExit;
    };

    if (!clipAxis(rayOrigin.x, rayDirection.x, -halfSize, halfSize) ||
        !clipAxis(rayOrigin.y, rayDirection.y, surfaceMinHeight, surfaceMaxHeight) ||
        !clipAxis(rayOrigin.z, rayDirection.z, -halfSize, halfSize)){
        return false;
    }

    auto signedDistanceToSurface = [&](float rayParameter, float& signedDistance, Vector3& point){
        point = localRay.getPoint(rayParameter);
        float sampledHeight = 0.0f;
        if (!sampleRaycastHeight(point.x, point.z, sampledHeight)){
            return false;
        }
        signedDistance = point.y - sampledHeight;
        return true;
    };

    const int raycastSteps = 160;
    const float surfaceEpsilon = std::max(0.001f, std::abs(surfaceMaxHeight - surfaceMinHeight) * 0.0005f);
    float previousParameter = rayEntry;
    float previousDistance = 0.0f;
    Vector3 previousPoint;
    if (!signedDistanceToSurface(previousParameter, previousDistance, previousPoint)){
        return false;
    }

    float closestParameter = previousParameter;
    float closestDistance = previousDistance;
    Vector3 closestPoint = previousPoint;
    if (std::abs(previousDistance) <= surfaceEpsilon){
        closestDistance = 0.0f;
    }

    for (int sampleIndex = 1; sampleIndex <= raycastSteps; sampleIndex++){
        const float rayParameter = rayEntry + (rayExit - rayEntry) * (static_cast<float>(sampleIndex) / static_cast<float>(raycastSteps));
        float currentDistance = 0.0f;
        Vector3 currentPoint;
        if (!signedDistanceToSurface(rayParameter, currentDistance, currentPoint)){
            continue;
        }

        if (std::abs(currentDistance) < std::abs(closestDistance)){
            closestParameter = rayParameter;
            closestDistance = currentDistance;
            closestPoint = currentPoint;
        }

        const bool crossedSurface = (previousDistance <= 0.0f && currentDistance >= 0.0f) ||
                                    (previousDistance >= 0.0f && currentDistance <= 0.0f);
        if (crossedSurface){
            float lowerParameter = previousParameter;
            float upperParameter = rayParameter;
            float lowerDistance = previousDistance;
            for (int refineIndex = 0; refineIndex < 12; refineIndex++){
                const float midParameter = (lowerParameter + upperParameter) * 0.5f;
                float midDistance = 0.0f;
                Vector3 midPoint;
                if (!signedDistanceToSurface(midParameter, midDistance, midPoint)){
                    break;
                }
                const bool sameSide = (lowerDistance <= 0.0f && midDistance <= 0.0f) ||
                                      (lowerDistance >= 0.0f && midDistance >= 0.0f);
                if (sameSide){
                    lowerParameter = midParameter;
                    lowerDistance = midDistance;
                }else{
                    upperParameter = midParameter;
                }
            }
            closestParameter = (lowerParameter + upperParameter) * 0.5f;
            closestPoint = localRay.getPoint(closestParameter);
            closestDistance = 0.0f;
            break;
        }

        previousParameter = rayParameter;
        previousDistance = currentDistance;
    }

    if (std::abs(closestDistance) > surfaceEpsilon * 4.0f){
        return false;
    }

    if (!sampleCurrentHeight(closestPoint.x, closestPoint.z, localHeight)){
        return false;
    }

    localPoint = Vector3(closestPoint.x, localHeight, closestPoint.z);
    return true;
}

void editor::TerrainEditWindow::captureStrokeHeightReference(TerrainComponent& terrain){
    stroke.heightReferenceValid = false;
    stroke.heightReferencePixels.clear();
    stroke.heightReferenceTerrainSize = terrain.terrainSize;
    stroke.heightReferenceMaxHeight = terrain.maxHeight;
    stroke.heightReferenceWidth = 0;
    stroke.heightReferenceHeight = 0;
    stroke.heightReferenceChannels = 0;

    if (terrain.heightMap.empty() || !hasLoadedData(terrain.heightMap)){
        return;
    }

    TextureData& heightData = terrain.heightMap.getData();
    if (!heightData.getData() || heightData.getWidth() <= 0 || heightData.getHeight() <= 0 || heightData.getChannels() <= 0){
        return;
    }

    stroke.heightReferencePixels = copyTexturePixels(heightData);
    if (stroke.heightReferencePixels.empty()){
        return;
    }

    stroke.heightReferenceWidth = heightData.getWidth();
    stroke.heightReferenceHeight = heightData.getHeight();
    stroke.heightReferenceChannels = heightData.getChannels();
    stroke.heightReferenceValid = true;
}

class editor::TerrainEditWindow::TerrainTextureEditCmd: public editor::Command{
private:
    Project* project;
    uint32_t sceneId;
    Entity entity;
    TerrainMapTarget target;
    TerrainMapSnapshot beforeSnapshot;
    TerrainMapSnapshot afterSnapshot;
    bool wasModified = false;

    void apply(const TerrainMapSnapshot& snapshot, bool restoreModifiedState){
        SceneProject* sceneProject = project->getScene(sceneId);
        if (!sceneProject || !sceneProject->scene->isEntityCreated(entity)){
            return;
        }
        TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(entity);
        if (!terrain){
            return;
        }

        Texture& texture = TerrainEditWindow::getTerrainTexture(*terrain, target);
        TerrainEditWindow::applySnapshotToTexture(project, texture, snapshot);

        if (target == TerrainMapTarget::HeightMap){
            terrain->heightMapLoaded = false;
            terrain->needUpdateTerrain = true;
            terrain->needUpdateTexture = true;
        }else{
            terrain->needUpdateTexture = true;
        }

        if (project->isEntityInBundle(sceneId, entity)){
            project->bundlePropertyChanged(sceneId, entity, ComponentType::TerrainComponent, {TerrainEditWindow::getTerrainPropertyName(target)});
        }

        if (restoreModifiedState){
            sceneProject->isModified = wasModified;
        }else{
            sceneProject->isModified = true;
        }
    }

public:
    TerrainTextureEditCmd(Project* project, uint32_t sceneId, Entity entity, TerrainMapTarget target,
                          const TerrainMapSnapshot& beforeSnapshot, const TerrainMapSnapshot& afterSnapshot):
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

editor::TerrainEditWindow::TerrainEditWindow(Project* project){
    this->project = project;
    windowOpen = false;
    brushActive = false;
    selectedSceneId = NULL_PROJECT_SCENE;
    selectedEntity = NULL_ENTITY;
    brushMode     = TerrainBrushMode::Raise;
    brushShape    = TerrainBrushShape::Circle;
    brushFalloff  = TerrainBrushFalloff::Smooth;
    brushSize     = 4.0f;
    brushStrength = 0.04f;
    flattenHeight = 0.5f;
    heightMapResolution = 512;
    blendMapResolution  = 512;
    normalizeBlendPaint = true;
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

bool editor::TerrainEditWindow::findTerrainHit(Scene* scene, const Ray& ray, Entity& entity, Vector3& localPoint, Vector3& worldPoint, float& localHeight, const ActiveStroke* activeStroke) const{
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

    if (!raycastTerrainSurface(localRay, terrain, activeStroke, localPoint, localHeight)){
        return false;
    }

    worldPoint = transform.modelMatrix * localPoint;
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
    if (!ensureEditableMap(project, sceneProject, entity, target, resolution)){
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

bool editor::TerrainEditWindow::createMapForTarget(TerrainMapTarget target, int width, int height){
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        return false;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(selectedEntity);
    Texture& texture = getTerrainTexture(terrain, target);

    const bool forceBeforePixels = texture.getPath(0).empty() || isOwnedEditableTexturePath(texture.getPath(0), sceneProject->id, selectedEntity, target);
    TerrainMapSnapshot before = captureSnapshot(project, texture, forceBeforePixels);
    TerrainMapSnapshot after;
    after.empty = false;
    after.path = makeEditableTexturePath(project, sceneProject->id, selectedEntity, target);
    after.id = after.path;
    after.minFilter = texture.getMinFilter();
    after.magFilter = texture.getMagFilter();
    after.wrapU = texture.getWrapU();
    after.wrapV = texture.getWrapV();
    after.colorFormat = expectedFormat(target);
    after.channels = expectedChannels(target);
    after.width = std::max(2, width);
    after.height = std::max(2, height);
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

bool editor::TerrainEditWindow::deleteMapForTarget(TerrainMapTarget target){
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        return false;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(selectedEntity);
    Texture& texture = getTerrainTexture(terrain, target);
    if (texture.empty()){
        return false;
    }

    const bool forceBeforePixels = texture.getPath(0).empty() || isOwnedEditableTexturePath(texture.getPath(0), sceneProject->id, selectedEntity, target);
    TerrainMapSnapshot before = captureSnapshot(project, texture, forceBeforePixels);
    TerrainMapSnapshot after;

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

    ImGui::SetNextWindowSize(ImVec2(340.0f, 480.0f), ImGuiCond_FirstUseEver);

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

    std::string selectedTerrainLabel = "Selected terrain: " + sceneProject->scene->getEntityName(selectedEntity);
    ImGui::TextUnformatted(selectedTerrainLabel.c_str());
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

    auto mapSection = [&](TerrainMapTarget target, const char* icon, const char* label, const TerrainMapInfo& info, int& resolution, const char* id){
        ImGui::PushID(id);

        const float resolutionInputWidth = 80.0f;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float mapActionIconWidth = std::max(ImGui::CalcTextSize(ICON_FA_ARROWS_ROTATE).x,
                                                   ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x) + style.FramePadding.x * 2.0f;
        const ImVec2 iconButtonSize(mapActionIconWidth, 0.0f);
        const bool canRecreate = info.present && info.sizeKnown && info.width > 0 && info.height > 0;
        const char* statusIcon = (!info.present || !info.sizeKnown) ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_CHECK;
        const char* statusTooltip = !info.present ? "Map is not assigned" : (info.sizeKnown ? "Map is assigned" : "Map is assigned, but its texture data is not available yet.");
        const char* createText = ICON_FA_PLUS "  Create";

        std::string resolutionText = "Unknown";
        if (info.sizeKnown){
            resolutionText = info.width == info.height ? std::to_string(info.width) : std::to_string(info.width) + "x" + std::to_string(info.height);
        }

        std::string title = std::string(icon) + "  " + label;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", statusIcon);
        showTooltip(statusTooltip);

        ImGui::SameLine();
        if (!info.present){
            ImGui::SetNextItemWidth(resolutionInputWidth);
            ImGui::DragInt("##resolution", &resolution, 1.0f, 2, 8192);
            showTooltip("Resolution used when creating this map");
            resolution = std::max(2, resolution);

            ImGui::SameLine();
            if (ImGui::Button((std::string(createText) + "##create_map_action").c_str())){
                createMapForTarget(target, resolution, resolution);
            }
            showTooltip("Create an editable map with the chosen resolution");
        }else{
            if (info.sizeKnown){
                ImGui::TextUnformatted(resolutionText.c_str());
            }else{
                ImGui::TextDisabled("%s", resolutionText.c_str());
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(!canRecreate);
            if (iconButton(ICON_FA_ARROWS_ROTATE, "recreate_map_action", canRecreate ? "Recreate this map using its current resolution" : "Current map size is not available yet", false, iconButtonSize)){
                createMapForTarget(target, info.width, info.height);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (iconButton(ICON_FA_TRASH_CAN, "delete_map_action", "Delete this map", false, iconButtonSize)){
                deleteMapForTarget(target);
            }
        }

        ImGui::PopID();
    };

    mapSection(TerrainMapTarget::HeightMap, ICON_FA_MOUNTAIN, "Heightmap", heightInfo, heightMapResolution, "height");
    ImGui::Separator();
    mapSection(TerrainMapTarget::BlendMap, ICON_FA_PALETTE, "Blendmap", blendInfo, blendMapResolution, "blend");

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
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    if (iconButton(ICON_FA_SCALE_BALANCED, "normalize_blend", "Normalize blend paint: paint one channel while fading the others out", normalizeBlendPaint, toolButtonSize)){
        normalizeBlendPaint = !normalizeBlendPaint;
    }
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
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
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

    const float maxBrushSize = 50.0f;
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

    ImGui::EndDisabled();

    ImGui::End();

    // Persist brush settings back to project so they survive across sessions
    {
        TerrainEditorSettings& ts = project->getTerrainEditorSettings();
        ts.brushMode           = static_cast<int>(brushMode);
        ts.brushShape          = static_cast<int>(brushShape);
        ts.brushFalloff        = static_cast<int>(brushFalloff);
        ts.brushSize           = brushSize;
        ts.brushStrength       = brushStrength;
        ts.flattenHeight       = flattenHeight;
        ts.heightMapResolution = heightMapResolution;
        ts.blendMapResolution  = blendMapResolution;
        ts.normalizeBlendPaint = normalizeBlendPaint;
    }

    if (wasOpen && !windowOpen){
        brushActive = false;
        clearStroke();
    }
}

void editor::TerrainEditWindow::openForEntity(Entity entity, uint32_t sceneId){
    windowOpen = true;
    selectedSceneId = sceneId;
    selectedEntity = entity;

    const TerrainEditorSettings& ts = project->getTerrainEditorSettings();
    brushMode     = static_cast<TerrainBrushMode>(ts.brushMode);
    brushShape    = static_cast<TerrainBrushShape>(ts.brushShape);
    brushFalloff  = static_cast<TerrainBrushFalloff>(ts.brushFalloff);
    brushSize     = ts.brushSize;
    brushStrength = ts.brushStrength;
    flattenHeight = ts.flattenHeight;
    heightMapResolution = ts.heightMapResolution;
    blendMapResolution  = ts.blendMapResolution;
    normalizeBlendPaint = ts.normalizeBlendPaint;
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
    const bool forceBeforePixels = texture.getPath(0).empty() || isOwnedEditableTexturePath(texture.getPath(0), sceneProject->id, entity, target);
    stroke.beforeSnapshot = captureSnapshot(project, texture, forceBeforePixels);
    if (target == TerrainMapTarget::HeightMap){
        captureStrokeHeightReference(scene->getComponent<TerrainComponent>(entity));
    }

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
    if (!findTerrainHit(scene, ray, entity, localPoint, worldPoint, localHeight, &stroke)){
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
            TerrainMapSnapshot after = captureSnapshot(project, texture, true);
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
    if (!findTerrainHit(scene, ray, entity, localPoint, worldPoint, localHeight, stroke.active ? &stroke : nullptr)){
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
