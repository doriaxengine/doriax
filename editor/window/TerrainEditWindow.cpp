// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "TerrainEditWindow.h"

#include "Backend.h"
#include "Catalog.h"
#include "Out.h"
#include "command/CommandHandle.h"
#include "command/type/PropertyCmd.h"
#include "command/type/TerrainMapPatchCmd.h"
#include "external/IconsFontAwesome6.h"
#include "subsystem/MeshSystem.h"
#include "util/TerrainMapFileWriter.h"
#include "util/TerrainMapUtils.h"
#include "util/UIUtils.h"
#include "util/Util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace doriax;
using namespace doriax::editor;


bool editor::TerrainEditWindow::loadTerrainTextureDataFromPath(Project* project, const std::string& path, TextureData& data){
    if (path.empty()){
        return false;
    }

    fs::path texturePath = project ? project->resolveAssetPath(path) : fs::path(path);

    // A background write for this map may still be in flight; the disk read below
    // must observe it.
    TerrainMapFileWriter::get().flushPath(texturePath.string());

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

// Map file stem. The layer index is left out on purpose: removing a layer shifts the ones above
// it, and a stem keyed on the index would un-own their files.
static std::string makeMapSuffix(const TerrainMapRef& ref){
    if (ref.target == TerrainMapTarget::HeightMap){
        return "height";
    }
    return ref.target == TerrainMapTarget::BlendMap ? "blend" : "density";
}

std::string editor::TerrainEditWindow::makeEditableTextureId(uint32_t sceneId, Entity entity, const TerrainMapRef& ref){
    return "__terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + makeMapSuffix(ref) + "_" + std::to_string(editTextureCounter++);
}

std::string editor::TerrainEditWindow::makeEditableTexturePath(Project* project, uint32_t sceneId, Entity entity, const TerrainMapRef& ref){
    fs::path baseDir = project ? project->getTerrainMapsDir() : fs::path("terrain_maps");

    const std::string suffix = makeMapSuffix(ref);
    for (int attempt = 0; attempt < 10000; attempt++){
        const uint64_t serial = editTextureCounter++;
        fs::path candidatePath = baseDir / ("terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix + "_" + std::to_string(serial) + ".png");
        if (!project || project->getProjectPath().empty()){
            return candidatePath.generic_string();
        }

        std::error_code ec;
        if (!fs::exists(candidatePath, ec)){
            std::error_code ec2;
            fs::path relPath = fs::relative(candidatePath, project->getAssetsPath(), ec2);
            return (!ec2 && !relPath.empty()) ? relPath.generic_string() : candidatePath.generic_string();
        }
    }

    fs::path fallbackPath = baseDir / ("terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix + ".png");
    if (project && !project->getProjectPath().empty()){
        std::error_code ec;
        fs::path relPath = fs::relative(fallbackPath, project->getAssetsPath(), ec);
        if (!ec && !relPath.empty()) return relPath.generic_string();
    }
    return fallbackPath.generic_string();
}

static bool isEditableTexturePath(const std::string& path){
    if (path.empty()){
        return false;
    }

    fs::path texturePath(path);
    const std::string parentName = texturePath.parent_path().filename().string();
    const std::string filename = texturePath.filename().string();
    return parentName == "terrain_maps" && filename.rfind("terrain_edit_", 0) == 0;
}

bool editor::TerrainEditWindow::isOwnedEditableTexturePath(const std::string& path, uint32_t sceneId, Entity entity, const TerrainMapRef& ref){
    if (!isEditableTexturePath(path)){
        return false;
    }

    fs::path texturePath(path);
    const std::string suffix = makeMapSuffix(ref);
    const std::string expectedStem = "terrain_edit_" + std::to_string(sceneId) + "_" + std::to_string(entity) + "_" + suffix;
    const std::string stem = texturePath.stem().string();
    return stem == expectedStem || stem.rfind(expectedStem + "_", 0) == 0;
}

int editor::TerrainEditWindow::expectedChannels(TerrainMapTarget target){
    return isScalarTarget(target) ? 1 : 4;
}

ColorFormat editor::TerrainEditWindow::expectedFormat(TerrainMapTarget target){
    // Heightmaps use 16-bit single channel (RED16) so large maxHeight values don't quantize into
    // visible terraces; blend maps stay 8-bit RGBA and density maps 8-bit single channel.
    if (target == TerrainMapTarget::HeightMap){
        return ColorFormat::RED16;
    }
    return target == TerrainMapTarget::DensityMap ? ColorFormat::RED : ColorFormat::RGBA;
}

// bytes occupied by one texel for a terrain map target
int editor::TerrainEditWindow::expectedBytesPerTexel(TerrainMapTarget target){
    return expectedChannels(target) * TextureData::getBytesPerChannel(expectedFormat(target));
}

// Decode a normalized [0,1] height from a raw pixel buffer, honoring 8- or 16-bit storage.
// 16-bit samples are stored little-endian in memory (native unsigned short).
float editor::TerrainEditWindow::decodeHeightTexel(const unsigned char* pixels, size_t texelIndex, int channels, int bytesPerChannel){
    const size_t byteIndex = texelIndex * static_cast<size_t>(channels) * static_cast<size_t>(bytesPerChannel);
    if (bytesPerChannel >= 2){
        const unsigned int value = static_cast<unsigned int>(pixels[byteIndex]) |
                                   (static_cast<unsigned int>(pixels[byteIndex + 1]) << 8);
        return static_cast<float>(value) / 65535.0f;
    }
    return pixels[byteIndex] / 255.0f;
}

// Encode a normalized [0,1] height into a raw pixel buffer (single channel), 8- or 16-bit.
void editor::TerrainEditWindow::encodeHeightTexel(unsigned char* pixels, size_t texelIndex, int bytesPerChannel, float value){
    const float clamped = std::max(0.0f, std::min(1.0f, value));
    if (bytesPerChannel >= 2){
        const unsigned int quantized = static_cast<unsigned int>(std::lround(clamped * 65535.0f));
        const size_t byteIndex = texelIndex * 2;
        pixels[byteIndex] = static_cast<unsigned char>(quantized & 0xFF);
        pixels[byteIndex + 1] = static_cast<unsigned char>((quantized >> 8) & 0xFF);
    }else{
        pixels[texelIndex] = static_cast<unsigned char>(std::lround(clamped * 255.0f));
    }
}

unsigned char editor::TerrainEditWindow::clampByte(float value){
    return static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, value)));
}

bool editor::TerrainEditWindow::setFileBackedTextureData(Project* project, Texture& texture, const std::string& relativePath, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels){
    const int bytesPerChannel = TextureData::getBytesPerChannel(format);
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels) * static_cast<size_t>(bytesPerChannel);
    if (width <= 0 || height <= 0 || channels <= 0 || pixels.size() < expectedSize){
        return false;
    }

    if (!TerrainMapUtils::writeFile(project, relativePath, width, height, channels, bytesPerChannel, pixels)){
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

editor::TerrainMapInfo editor::TerrainEditWindow::getTerrainMapInfo(Texture& texture){
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

    if (TerrainMapUtils::hasLoadedData(texture)){
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
    const int srcBytesPerChannel = TextureData::getBytesPerChannel(data.getColorFormat());
    const int dstChannels = expectedChannels(target);
    const int dstBytesPerChannel = expectedBytesPerTexel(target) / dstChannels;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(dstChannels) * static_cast<size_t>(dstBytesPerChannel), 0);

    if (!data.getData() || width <= 0 || height <= 0 || srcChannels <= 0){
        return pixels;
    }

    unsigned char* src = static_cast<unsigned char*>(data.getData());
    for (int y = 0; y < height; y++){
        for (int x = 0; x < width; x++){
            const size_t texelIndex = static_cast<size_t>(y) * width + x;
            if (isScalarTarget(target)){
                // Decode the source value (8- or 16-bit, R channel) and re-encode at the
                // destination precision. This upcasts legacy 8-bit heightmaps to 16-bit.
                const float normalized = decodeHeightTexel(src, texelIndex, srcChannels, srcBytesPerChannel);
                encodeHeightTexel(pixels.data(), texelIndex, dstBytesPerChannel, normalized);
            }else{
                const size_t srcIndex = texelIndex * srcChannels * srcBytesPerChannel;
                const size_t dstIndex = texelIndex * dstChannels;
                auto srcComponent = [&](int channel){
                    return src[srcIndex + std::min(channel, srcChannels - 1) * srcBytesPerChannel];
                };
                pixels[dstIndex + 0] = srcComponent(0);
                pixels[dstIndex + 1] = srcComponent(1);
                pixels[dstIndex + 2] = srcComponent(2);
                pixels[dstIndex + 3] = srcChannels >= 4 ? src[srcIndex + 3 * srcBytesPerChannel] : 255;
            }
        }
    }
    return pixels;
}

void editor::TerrainEditWindow::setOwnedTextureData(Texture& texture, const std::string& id, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels){
    const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels) * static_cast<size_t>(TextureData::getBytesPerChannel(format));
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

    if (TerrainMapUtils::hasLoadedData(texture)){
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
            std::string id = snapshot.id.empty() ? ("__terrain_edit_snapshot_" + std::to_string(editTextureCounter++)) : snapshot.id;
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

// Extracts terrain-editor map filenames ("terrain_edit_<stem>.png") from a text blob.
// A plain token scan is component-agnostic: it finds the reference no matter which
// component or property holds the path, and never breaks when the YAML schema evolves.
static void collectTerrainMapTokens(const std::string& content, std::unordered_set<std::string>& out){
    static const std::string prefix = "terrain_edit_";
    size_t pos = content.find(prefix);
    while (pos != std::string::npos){
        size_t end = pos + prefix.size();
        while (end < content.size() && (std::isalnum(static_cast<unsigned char>(content[end])) || content[end] == '_')){
            end++;
        }
        if (content.compare(end, 4, ".png") == 0){
            out.insert(content.substr(pos, end + 4 - pos));
        }
        pos = content.find(prefix, end);
    }
}

// Scans every project file that can reference textures — scenes, entity bundles
// (.bundle via Util::isBundleFile) and materials — and collects the terrain map
// filenames they mention. Returns false when any file could not be read; the caller
// must then not delete anything, since an unreadable file may hold the only
// reference to a map.
static bool collectTerrainMapDiskReferences(const fs::path& projectPath, std::unordered_set<std::string>& out){
    if (projectPath.empty()){
        return false;
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(projectPath, fs::directory_options::skip_permission_denied, ec);
    if (ec){
        return false;
    }

    const fs::recursive_directory_iterator endIt;
    while (it != endIt){
        const fs::directory_entry& entry = *it;

        if (entry.is_directory(ec)){
            // Dot-directories hold caches and VCS data (.doriax export copies, .git);
            // nothing in them defines a live reference.
            if (entry.path().filename().string().rfind(".", 0) == 0){
                it.disable_recursion_pending();
            }
        }else if (entry.is_regular_file(ec)){
            const std::string entryPath = entry.path().string();
            if (Util::isSceneFile(entryPath) || Util::isMaterialFile(entryPath) || Util::isBundleFile(entryPath)){
                std::ifstream file(entry.path(), std::ios::binary);
                if (!file){
                    return false;
                }
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (file.bad()){
                    return false;
                }
                collectTerrainMapTokens(content, out);
            }
        }

        it.increment(ec);
        if (ec){
            return false;
        }
    }

    return true;
}

bool editor::TerrainEditWindow::cleanUnusedTerrainMaps(Project* project){
    if (!project || project->getProjectPath().empty()){
        return true;
    }

    // Settle pending background writes first, so a late write can't resurrect a
    // file this scan is about to delete. This runs on every scene save, so a
    // failure here means the save is incomplete — say so, loudly.
    const bool flushOk = TerrainMapFileWriter::get().flushAll();
    if (!flushOk){
        Out::error("Scene saved, but %zu terrain map(s) could not be written to disk; the scene stays marked as unsaved — save again after checking free disk space and permissions.",
                   TerrainMapFileWriter::get().failedCount());
    }

    // Pass 1 — live references: loaded scenes are inspected through their in-memory
    // components, so maps repointed by still-unsaved edits stay protected. Closed
    // scenes keep a null Scene* (deleteSceneProject) and are covered by pass 2.
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
                for (const TerrainFoliageLayer& layer : terrain->foliageLayers){
                    const std::string& densityPath = layer.densityMap.getPath(0);
                    if (!densityPath.empty() && isEditableTexturePath(densityPath)){
                        activeFiles.insert(fs::path(densityPath).filename().string());
                    }
                }
            }
        }
    }

    fs::path absoluteBaseDir = project->getTerrainMapsDir();

    std::error_code ec;
    if (!fs::exists(absoluteBaseDir, ec) || !fs::is_directory(absoluteBaseDir, ec)){
        return flushOk;
    }

    // Candidate orphans: editable maps on disk that no loaded scene references.
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(absoluteBaseDir, ec)){
        if (entry.is_regular_file(ec)){
            const std::string filename = entry.path().filename().string();
            if (filename.rfind("terrain_edit_", 0) == 0 && activeFiles.find(filename) == activeFiles.end()){
                candidates.push_back(entry.path());
            }
        }
    }
    if (candidates.empty()){
        return flushOk;
    }

    // Pass 2 — on-disk references: closed scenes (and bundles/materials) can still
    // reference candidate maps, so scan the project files before deleting. When the
    // scan cannot complete, delete nothing: a false "unused" verdict permanently
    // loses painted terrain, while a kept orphan only wastes a little disk.
    if (!collectTerrainMapDiskReferences(project->getProjectPath(), activeFiles)){
        return flushOk;
    }

    for (const fs::path& candidate : candidates){
        if (activeFiles.find(candidate.filename().string()) == activeFiles.end()){
            fs::remove(candidate, ec);
            Out::info("Garbage collected old terrain map: %s", candidate.filename().string().c_str());
        }
    }

    return flushOk;
}

// Builds the initial pixel buffer for a freshly created terrain map, honoring the target's
// bit depth. Heightmaps optionally start at the middle (0.5) so they can be raised or lowered.
std::vector<unsigned char> editor::TerrainEditWindow::makeInitialMapPixels(TerrainMapTarget target, int width, int height){
    const int channels = expectedChannels(target);
    const int bytesPerTexel = expectedBytesPerTexel(target);
    const size_t texelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<unsigned char> pixels(texelCount * static_cast<size_t>(bytesPerTexel), 0);

    if (target == TerrainMapTarget::HeightMap && heightMapStartAtMiddle){
        const int bytesPerChannel = bytesPerTexel / channels;
        for (size_t i = 0; i < texelCount; i++){
            encodeHeightTexel(pixels.data(), i, bytesPerChannel, 0.5f);
        }
    }else if (target == TerrainMapTarget::BlendMap){
        // Opaque alpha, no painted channels yet. A density map starts empty and keeps the zeros.
        for (size_t i = 3; i < pixels.size(); i += 4){
            pixels[i] = 255;
        }
    }
    return pixels;
}

bool editor::TerrainEditWindow::ensureEditableMap(Project* project, SceneProject* sceneProject, Entity entity, const TerrainMapRef& ref, int resolution){
    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entity);
    Texture* texturePtr = TerrainMapUtils::findTexture(terrain, ref);
    if (!texturePtr){
        return false;
    }

    Texture& texture = *texturePtr;
    const TerrainMapTarget target = ref.target;
    const int channels = expectedChannels(target);
    const ColorFormat format = expectedFormat(target);

    if (texture.empty()){
        const int safeResolution = std::max(2, resolution);
        std::vector<unsigned char> pixels = makeInitialMapPixels(target, safeResolution, safeResolution);
        const std::string path = makeEditableTexturePath(project, sceneProject->id, entity, ref);
        if (!setFileBackedTextureData(project, texture, path, safeResolution, safeResolution, format, channels, pixels)){
            setOwnedTextureData(texture, makeEditableTextureId(sceneProject->id, entity, ref), safeResolution, safeResolution, format, channels, pixels);
        }
        return true;
    }

    const std::string path = texture.getPath(0);
    TextureData fileData;
    TextureData* data = nullptr;
    bool loadedFromFile = false;

    if (TerrainMapUtils::hasLoadedData(texture)){
        data = &texture.getData();
    }else if (!path.empty() && loadTerrainTextureDataFromPath(project, path, fileData)){
        data = &fileData;
        loadedFromFile = true;
    }else{
        return false;
    }

    const bool needsEditableFile = path.empty() || !isOwnedEditableTexturePath(path, sceneProject->id, entity, ref);
    const bool shouldConvert = loadedFromFile || needsEditableFile || data->getChannels() != channels || data->getColorFormat() != format;
    if (shouldConvert){
        std::vector<unsigned char> pixels = convertTexturePixels(*data, target);
        const std::string editablePath = needsEditableFile ? makeEditableTexturePath(project, sceneProject->id, entity, ref) : path;
        if (!setFileBackedTextureData(project, texture, editablePath, data->getWidth(), data->getHeight(), format, channels, pixels)){
            setOwnedTextureData(texture, makeEditableTextureId(sceneProject->id, entity, ref), data->getWidth(), data->getHeight(), format, channels, pixels);
        }
    }else{
        texture.getData().setDataOwned(true);
    }

    return true;
}

// Bilinearly sample a normalized [0,1] height at a float texel coordinate.
float editor::TerrainEditWindow::bilinearHeightSample(const unsigned char* pixels, int width, int height, int channels, int bytesPerChannel, float texelX, float texelY){
    if (!pixels || width <= 0 || height <= 0 || channels <= 0){
        return 0.0f;
    }
    texelX = std::clamp(texelX, 0.0f, static_cast<float>(width - 1));
    texelY = std::clamp(texelY, 0.0f, static_cast<float>(height - 1));
    const int lowerX = std::clamp(static_cast<int>(std::floor(texelX)), 0, width - 1);
    const int lowerY = std::clamp(static_cast<int>(std::floor(texelY)), 0, height - 1);
    const int upperX = std::min(lowerX + 1, width - 1);
    const int upperY = std::min(lowerY + 1, height - 1);
    const float blendX = texelX - static_cast<float>(lowerX);
    const float blendY = texelY - static_cast<float>(lowerY);

    auto sample = [&](int x, int y){
        return decodeHeightTexel(pixels, static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x), channels, bytesPerChannel);
    };

    const float lower = sample(lowerX, lowerY) + (sample(upperX, lowerY) - sample(lowerX, lowerY)) * blendX;
    const float upper = sample(lowerX, upperY) + (sample(upperX, upperY) - sample(lowerX, upperY)) * blendX;
    return lower + (upper - lower) * blendY;
}


void editor::TerrainEditWindow::writeHeight(TextureData& data, int x, int y, float value){
    unsigned char* pixels = static_cast<unsigned char*>(data.getData());
    if (!pixels){
        return;
    }
    const int channels = data.getChannels();
    const int bytesPerChannel = TextureData::getBytesPerChannel(data.getColorFormat());
    const size_t texelIndex = static_cast<size_t>(y) * data.getWidth() + x;

    if (bytesPerChannel >= 2){
        // 16-bit single-channel heightmap.
        encodeHeightTexel(pixels, texelIndex, bytesPerChannel, value);
        return;
    }

    // Legacy 8-bit path (also covers replicated grayscale into RGB if a height map
    // ever ends up multi-channel).
    const size_t index = texelIndex * channels;
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

bool editor::TerrainEditWindow::raycastTerrainStrokeSurface(const Ray& localRay, TerrainComponent& terrain, const ActiveStroke* activeStroke, Vector3& localPoint, float& localHeight) const{
    const bool useHeightReference = activeStroke &&
                                    activeStroke->active &&
                                    activeStroke->ref.target == TerrainMapTarget::HeightMap &&
                                    activeStroke->heightReferenceValid &&
                                    !activeStroke->heightReferencePixels.empty();
    if (!useHeightReference){
        return false;
    }

    const unsigned char* currentPixels = nullptr;
    int currentWidth = 0;
    int currentHeight = 0;
    int currentChannels = 0;
    int currentBytesPerChannel = 1;

    if (!terrain.heightMap.empty() && TerrainMapUtils::hasLoadedData(terrain.heightMap)){
        TextureData& heightData = terrain.heightMap.getData();
        currentPixels = static_cast<const unsigned char*>(heightData.getData());
        currentWidth = heightData.getWidth();
        currentHeight = heightData.getHeight();
        currentChannels = heightData.getChannels();
        currentBytesPerChannel = TextureData::getBytesPerChannel(heightData.getColorFormat());
    }

    auto sampleHeightPixels = [](const unsigned char* pixels, int width, int height, int channels, int bytesPerChannel, float terrainSize, float maxHeight, float localX, float localZ, float& sampledHeight){
        if (terrainSize <= std::numeric_limits<float>::epsilon()){
            return false;
        }

        const float halfSize = terrainSize * 0.5f;
        if (localX < -halfSize || localX > halfSize || localZ < -halfSize || localZ > halfSize){
            return false;
        }

        sampledHeight = 0.0f;
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
            const size_t texelIndex = static_cast<size_t>(sampleZ) * static_cast<size_t>(width) + static_cast<size_t>(sampleX);
            return decodeHeightTexel(pixels, texelIndex, channels, bytesPerChannel);
        };

        const float height00 = samplePixel(lowerX, lowerZ);
        const float height10 = samplePixel(upperX, lowerZ);
        const float height01 = samplePixel(lowerX, upperZ);
        const float height11 = samplePixel(upperX, upperZ);
        const float lowerBlend = height00 + (height10 - height00) * blendX;
        const float upperBlend = height01 + (height11 - height01) * blendX;
        sampledHeight = (lowerBlend + (upperBlend - lowerBlend) * blendZ) * maxHeight;
        return true;
    };

    auto sampleCurrentHeight = [&](float localX, float localZ, float& sampledHeight){
        return sampleHeightPixels(currentPixels, currentWidth, currentHeight, currentChannels, currentBytesPerChannel, terrain.terrainSize, terrain.maxHeight, localX, localZ, sampledHeight);
    };

    auto sampleRaycastHeight = [&](float localX, float localZ, float& sampledHeight){
        return sampleHeightPixels(activeStroke->heightReferencePixels.data(),
                                  activeStroke->heightReferenceWidth,
                                  activeStroke->heightReferenceHeight,
                                  activeStroke->heightReferenceChannels,
                                  activeStroke->heightReferenceBytesPerChannel,
                                  activeStroke->heightReferenceTerrainSize,
                                  activeStroke->heightReferenceMaxHeight,
                                  localX,
                                  localZ,
                                  sampledHeight);
    };

    const float halfSize = terrain.terrainSize * 0.5f;
    const float surfaceMinHeight = std::min(0.0f, activeStroke->heightReferenceMaxHeight);
    const float surfaceMaxHeight = std::max(0.0f, activeStroke->heightReferenceMaxHeight);
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
            closestPoint = localRay.getPoint((lowerParameter + upperParameter) * 0.5f);
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
    stroke.heightReferenceBytesPerChannel = 1;

    if (terrain.heightMap.empty() || !TerrainMapUtils::hasLoadedData(terrain.heightMap)){
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
    stroke.heightReferenceBytesPerChannel = TextureData::getBytesPerChannel(heightData.getColorFormat());
    stroke.heightReferenceValid = true;
}

class editor::TerrainEditWindow::TerrainTextureEditCmd: public editor::Command{
private:
    TerrainEditWindow* window;
    Project* project;
    uint32_t sceneId;
    Entity entity;
    TerrainMapRef ref;
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

        Texture* texture = TerrainMapUtils::findTexture(*terrain, ref);
        if (!texture){
            return;
        }
        window->applySnapshotToTexture(project, *texture, snapshot);
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

public:
    TerrainTextureEditCmd(TerrainEditWindow* window, Project* project, uint32_t sceneId, Entity entity, const TerrainMapRef& ref,
                          const TerrainMapSnapshot& beforeSnapshot, const TerrainMapSnapshot& afterSnapshot):
        window(window), project(project), sceneId(sceneId), entity(entity), ref(ref), beforeSnapshot(beforeSnapshot), afterSnapshot(afterSnapshot){}

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
    focusRequested = false;
    brushActive = false;
    selectedSceneId = NULL_PROJECT_SCENE;
    selectedEntity = NULL_ENTITY;
    brushMode     = TerrainBrushMode::Raise;
    brushShape    = TerrainBrushShape::Circle;
    brushFalloff  = TerrainBrushFalloff::Smooth;
    brushSize     = 4.0f;
    brushStrength = 0.3f;
    flattenHeight = 0.5f;
    heightMapResolution = 512;
    blendMapResolution  = 512;
    densityMapResolution = 512;
    selectedFoliageLayer = 0;
    normalizeBlendPaint = true;
    heightMapStartAtMiddle = true;
    flattenPickOnStroke = true;
}

editor::TerrainEditWindow::~TerrainEditWindow(){
    // Drain pending map writes while the app is still fully alive (the writer's
    // own static destructor runs during late shutdown, where it makes one final
    // synchronous attempt at anything still failing).
    if (!TerrainMapFileWriter::get().flushAll()){
        Out::error("%zu terrain map(s) could not be written to disk; one final attempt will be made at exit, but the sculpt may be lost. Check free disk space and permissions.",
                   TerrainMapFileWriter::get().failedCount());
    }
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

int editor::TerrainEditWindow::mapResolutionFor(TerrainMapTarget target) const{
    if (target == TerrainMapTarget::HeightMap){
        return heightMapResolution;
    }
    return target == TerrainMapTarget::DensityMap ? densityMapResolution : blendMapResolution;
}

editor::TerrainMapRef editor::TerrainEditWindow::getBrushMapRef() const{
    return TerrainMapRef(getBrushTarget(), selectedFoliageLayer);
}

editor::TerrainMapTarget editor::TerrainEditWindow::getBrushTarget() const{
    if (isHeightBrush()){
        return TerrainMapTarget::HeightMap;
    }
    return isDensityBrush() ? TerrainMapTarget::DensityMap : TerrainMapTarget::BlendMap;
}

bool editor::TerrainEditWindow::isHeightBrush() const{
    return brushMode == TerrainBrushMode::Raise ||
           brushMode == TerrainBrushMode::Lower ||
           brushMode == TerrainBrushMode::Smooth ||
           brushMode == TerrainBrushMode::Flatten;
}

bool editor::TerrainEditWindow::isDensityBrush() const{
    return brushMode == TerrainBrushMode::PaintDensity || brushMode == TerrainBrushMode::EraseDensity;
}

// Height and density share the single-channel storage and stamping path.
bool editor::TerrainEditWindow::isScalarTarget(TerrainMapTarget target){
    return target != TerrainMapTarget::BlendMap;
}

bool editor::TerrainEditWindow::findTerrainHit(Scene* scene, const Ray& ray, Entity& entity, Vector3& localPoint, Vector3& worldPoint, float& localHeight, const ActiveStroke* activeStroke) const{
    SceneProject* sceneProject = findSceneProject(scene);
    if (!sceneProject || sceneProject->id != selectedSceneId || !hasValidTarget(sceneProject)){
        return false;
    }

    entity = selectedEntity;
    Transform& transform = scene->getComponent<Transform>(entity);
    TerrainComponent& terrain = scene->getComponent<TerrainComponent>(entity);
    const bool useHeightReference = activeStroke &&
                                    activeStroke->active &&
                                    activeStroke->ref.target == TerrainMapTarget::HeightMap &&
                                    activeStroke->heightReferenceValid &&
                                    !activeStroke->heightReferencePixels.empty();

    Matrix4 inverseModel = transform.modelMatrix.inverse();
    if (!useHeightReference){
        if (!scene->getSystem<MeshSystem>()->raycastTerrainSurface(ray, terrain, transform, worldPoint)){
            return false;
        }

        localPoint = inverseModel * worldPoint;
        localHeight = localPoint.y;
        return true;
    }

    Vector3 localOrigin = inverseModel * ray.getOrigin();
    Vector3 localEnd = inverseModel * (ray.getOrigin() + ray.getDirection());
    Ray localRay(localOrigin, localEnd - localOrigin);

    if (!raycastTerrainStrokeSurface(localRay, terrain, activeStroke, localPoint, localHeight)){
        return false;
    }

    worldPoint = transform.modelMatrix * localPoint;
    return true;
}

// Full-strength height deposition rate in normalized map units per second of painting.
static constexpr float BRUSH_FLOW_PER_SECOND = 1.5f;
// Blend painting deposits much faster than height sculpting: switching an area to a
// new texture means overpainting a channel that is often already at full strength,
// which is impractically slow at the sculpt rate (a normal drag would leave the new
// channel in the single digits under the old one), so texture painting gets its own
// higher flow. Tune this, not brushStrength, to change how quickly textures switch.
static constexpr float BRUSH_BLEND_FLOW_PER_SECOND = 10.0f;
// Time budget granted to a single click (one stamp), so a tap gives a small,
// predictable nudge instead of a full-strength jump.
static constexpr float BRUSH_CLICK_SECONDS = 1.0f / 60.0f;
// Longest interval a single event may deposit, so hitches don't cause spikes.
static constexpr float BRUSH_MAX_STAMP_SECONDS = 0.05f;

bool editor::TerrainEditWindow::applyBrush(SceneProject* sceneProject, Entity entity, const Vector3& localPoint){
    if (!sceneProject || !stroke.active || !sceneProject->scene->findComponent<TerrainComponent>(entity)){
        return false;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entity);
    if (terrain.terrainSize <= std::numeric_limits<float>::epsilon()){
        return false;
    }
    const TerrainMapRef ref = stroke.ref;
    const TerrainMapTarget target = ref.target;
    if (!ensureEditableMap(project, sceneProject, entity, ref, mapResolutionFor(target))){
        return false;
    }

    Texture* texture = TerrainMapUtils::findTexture(terrain, ref);
    if (!texture){
        return false;
    }

    TextureData& data = texture->getData();
    if (!data.getData() || data.getWidth() <= 0 || data.getHeight() <= 0){
        return false;
    }

    // Time-based flow: deposition scales with elapsed time instead of event rate,
    // so frame rate and mouse event frequency don't change stroke intensity.
    const auto now = std::chrono::steady_clock::now();
    float deltaTime = BRUSH_CLICK_SECONDS;
    if (stroke.hasLastPoint){
        deltaTime = std::chrono::duration<float>(now - stroke.lastStampTime).count();
        deltaTime = std::clamp(deltaTime, 0.0f, BRUSH_MAX_STAMP_SECONDS);
    }

    // Interpolate stamps along the path so fast strokes stay continuous; the time
    // budget is split across the stamps, keeping total deposition speed-independent.
    int stampCount = 1;
    Vector3 startPoint = localPoint;
    if (stroke.hasLastPoint){
        startPoint = stroke.lastPoint;
        const float dx = localPoint.x - startPoint.x;
        const float dz = localPoint.z - startPoint.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float spacing = std::max(brushSize * 0.25f, terrain.terrainSize / 4096.0f);
        // The spacing floor bounds distance/spacing by ~1.42 * 4096 for any segment
        // inside the terrain, so this cap never truncates a real stroke into dots;
        // it only guards degenerate inputs.
        stampCount = std::clamp(static_cast<int>(std::ceil(distance / spacing)), 1, 8192);
    }

    const float stampDelta = deltaTime / static_cast<float>(stampCount);
    bool applied = false;
    for (int i = 1; i <= stampCount; i++){
        const float t = static_cast<float>(i) / static_cast<float>(stampCount);
        const Vector3 point(startPoint.x + (localPoint.x - startPoint.x) * t, 0.0f,
                            startPoint.z + (localPoint.z - startPoint.z) * t);
        applied |= stampBrush(terrain, data, target, point, stampDelta);
    }

    stroke.lastPoint = localPoint;
    stroke.hasLastPoint = true;
    stroke.lastStampTime = now;

    if (applied){
        TerrainMapUtils::refresh(sceneProject, entity, ref);
    }
    return applied;
}

bool editor::TerrainEditWindow::stampBrush(TerrainComponent& terrain, TextureData& data, TerrainMapTarget target, const Vector3& localPoint, float deltaTime){
    unsigned char* pixels = static_cast<unsigned char*>(data.getData());
    const int width = data.getWidth();
    const int height = data.getHeight();
    const int channels = data.getChannels();
    const int bytesPerChannel = TextureData::getBytesPerChannel(data.getColorFormat());
    const size_t texelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    const int workingChannels = isScalarTarget(target) ? 1 : 4;

    if (!pixels || (target == TerrainMapTarget::BlendMap && channels < 3)){
        return false;
    }

    // Lazily decode the map into the stroke's float working buffer.
    if (stroke.workingWidth != width || stroke.workingHeight != height ||
        stroke.workingPixels.size() != texelCount * static_cast<size_t>(workingChannels)){
        stroke.workingPixels.resize(texelCount * static_cast<size_t>(workingChannels));
        if (isScalarTarget(target)){
            for (size_t i = 0; i < texelCount; i++){
                stroke.workingPixels[i] = decodeHeightTexel(pixels, i, channels, bytesPerChannel);
            }
        }else{
            for (size_t i = 0; i < texelCount; i++){
                for (int c = 0; c < 4; c++){
                    const int srcChannel = std::min(c, channels - 1);
                    stroke.workingPixels[i * 4 + c] = pixels[(i * channels + srcChannel) * bytesPerChannel] / 255.0f;
                }
            }
        }
        stroke.workingWidth = width;
        stroke.workingHeight = height;
    }

    const float halfSize = terrain.terrainSize * 0.5f;
    const float centerX = ((localPoint.x + halfSize) / terrain.terrainSize) * static_cast<float>(width - 1);
    const float centerY = ((localPoint.z + halfSize) / terrain.terrainSize) * static_cast<float>(height - 1);
    // Per-axis texel radii keep the brush circular in world space on non-square
    // maps, and float precision allows sub-texel radii for very small brushes.
    const float radiusX = std::max(0.01f, (brushSize / terrain.terrainSize) * static_cast<float>(width - 1));
    const float radiusY = std::max(0.01f, (brushSize / terrain.terrainSize) * static_cast<float>(height - 1));

    const int minX = std::max(0, static_cast<int>(std::floor(centerX - radiusX)));
    const int maxX = std::min(width - 1, static_cast<int>(std::ceil(centerX + radiusX)));
    const int minY = std::max(0, static_cast<int>(std::floor(centerY - radiusY)));
    const int maxY = std::min(height - 1, static_cast<int>(std::ceil(centerY + radiusY)));
    if (minX > maxX || minY > maxY){
        return false;
    }

    const TerrainBrushMode mode = stroke.effectiveMode;
    const float flattenTargetValue = std::clamp(flattenPickOnStroke ? stroke.flattenTarget : flattenHeight, 0.0f, 1.0f);
    const float flowRate = target == TerrainMapTarget::HeightMap ? BRUSH_FLOW_PER_SECOND : BRUSH_BLEND_FLOW_PER_SECOND;

    // Smooth samples a radius-scaled kernel from a snapshot of the stamp region, so
    // results don't depend on texel visit order.
    std::vector<float> smoothSource;
    int smoothStep = 1;
    int srcMinX = 0, srcMinY = 0, srcWidth = 0, srcHeight = 0;
    if (mode == TerrainBrushMode::Smooth && target == TerrainMapTarget::HeightMap){
        smoothStep = std::max(1, static_cast<int>(std::lround(std::min(radiusX, radiusY) * 0.25f)));
        srcMinX = std::max(0, minX - smoothStep);
        srcMinY = std::max(0, minY - smoothStep);
        const int srcMaxX = std::min(width - 1, maxX + smoothStep);
        const int srcMaxY = std::min(height - 1, maxY + smoothStep);
        srcWidth = srcMaxX - srcMinX + 1;
        srcHeight = srcMaxY - srcMinY + 1;
        smoothSource.resize(static_cast<size_t>(srcWidth) * static_cast<size_t>(srcHeight));
        for (int y = 0; y < srcHeight; y++){
            const float* srcRow = stroke.workingPixels.data() + static_cast<size_t>(srcMinY + y) * width + srcMinX;
            std::memcpy(smoothSource.data() + static_cast<size_t>(y) * srcWidth, srcRow, static_cast<size_t>(srcWidth) * sizeof(float));
        }
    }

    auto smoothSample = [&](int x, int y){
        const int sx = std::clamp(x, srcMinX, srcMinX + srcWidth - 1) - srcMinX;
        const int sy = std::clamp(y, srcMinY, srcMinY + srcHeight - 1) - srcMinY;
        return smoothSource[static_cast<size_t>(sy) * srcWidth + sx];
    };

    int paintChannel = 0;
    if (mode == TerrainBrushMode::PaintGreen){
        paintChannel = 1;
    }else if (mode == TerrainBrushMode::PaintBlue){
        paintChannel = 2;
    }

    auto applyTexel = [&](int x, int y, float weight){
        const size_t texelIndex = static_cast<size_t>(y) * width + x;
        if (target == TerrainMapTarget::DensityMap){
            const float current = stroke.workingPixels[texelIndex];
            const float targetValue = mode == TerrainBrushMode::EraseDensity ? 0.0f : 1.0f;
            stroke.workingPixels[texelIndex] = current + (targetValue - current) * weight;
        }else if (target == TerrainMapTarget::HeightMap){
            const float current = stroke.workingPixels[texelIndex];
            float next = current;
            if (mode == TerrainBrushMode::Raise){
                next = current + weight;
            }else if (mode == TerrainBrushMode::Lower){
                next = current - weight;
            }else if (mode == TerrainBrushMode::Flatten){
                next = current + (flattenTargetValue - current) * weight;
            }else if (mode == TerrainBrushMode::Smooth && !smoothSource.empty()){
                float sum = 0.0f;
                for (int oy = -1; oy <= 1; oy++){
                    for (int ox = -1; ox <= 1; ox++){
                        sum += smoothSample(x + ox * smoothStep, y + oy * smoothStep);
                    }
                }
                next = current + (sum / 9.0f - current) * weight;
            }
            stroke.workingPixels[texelIndex] = std::clamp(next, 0.0f, 1.0f);
        }else{
            const size_t index = texelIndex * 4;
            for (int c = 0; c < 3; c++){
                const float current = stroke.workingPixels[index + c];
                const float targetValue = c == paintChannel ? 1.0f : (normalizeBlendPaint ? 0.0f : current);
                stroke.workingPixels[index + c] = current + (targetValue - current) * weight;
            }
            stroke.workingPixels[index + 3] = 1.0f;
        }
    };

    bool touched = false;
    for (int y = minY; y <= maxY; y++){
        for (int x = minX; x <= maxX; x++){
            const float dx = (static_cast<float>(x) - centerX) / radiusX;
            const float dy = (static_cast<float>(y) - centerY) / radiusY;
            const float distance = brushShape == TerrainBrushShape::Circle ? std::sqrt(dx * dx + dy * dy) : std::max(std::abs(dx), std::abs(dy));
            if (distance > 1.0f){
                continue;
            }

            float falloff = 1.0f;
            if (brushFalloff == TerrainBrushFalloff::Linear){
                falloff = 1.0f - distance;
            }else if (brushFalloff == TerrainBrushFalloff::Smooth){
                const float t = 1.0f - distance;
                falloff = t * t * (3.0f - 2.0f * t);
            }

            applyTexel(x, y, std::clamp(brushStrength * falloff * flowRate * deltaTime, 0.0f, 1.0f));
            touched = true;
        }
    }

    // Sub-texel brushes can miss every texel center; guarantee the nearest texel
    // still receives the stamp so tiny brushes keep working.
    if (!touched){
        const int x = std::clamp(static_cast<int>(std::lround(centerX)), minX, maxX);
        const int y = std::clamp(static_cast<int>(std::lround(centerY)), minY, maxY);
        applyTexel(x, y, std::clamp(brushStrength * flowRate * deltaTime, 0.0f, 1.0f));
        touched = true;
    }

    stroke.dirtyRegion.merge(minX, minY, maxX, maxY);

    // Quantize the touched rect from the float working buffer back into the stored
    // pixel data.
    for (int y = minY; y <= maxY; y++){
        for (int x = minX; x <= maxX; x++){
            const size_t texelIndex = static_cast<size_t>(y) * width + x;
            if (isScalarTarget(target)){
                writeHeight(data, x, y, stroke.workingPixels[texelIndex]);
            }else{
                const size_t index = texelIndex * static_cast<size_t>(channels);
                for (int c = 0; c < std::min(channels, 4); c++){
                    pixels[index + c] = clampByte(stroke.workingPixels[texelIndex * 4 + c] * 255.0f);
                }
            }
        }
    }

    return touched;
}

// Builds the stroke's undo patch, or returns false when the map is no longer the one the stroke
// started on and the caller must fall back to whole-map snapshots.
bool editor::TerrainEditWindow::addStrokePatchCommand(SceneProject* sceneProject, Texture& texture){
    const TerrainMapSnapshot& before = stroke.beforeSnapshot;
    if (before.pixels.empty() || !stroke.dirtyRegion.fitsIn(before.width, before.height)){
        return false;
    }

    const std::string path = texture.getPath(0);
    if (path.empty() || path != before.path || !TerrainMapUtils::hasLoadedData(texture)){
        return false;
    }

    TextureData& data = texture.getData();
    if (data.getWidth() != before.width || data.getHeight() != before.height ||
        data.getChannels() != before.channels || data.getColorFormat() != before.colorFormat){
        return false;
    }

    const int bytesPerTexel = before.channels * TextureData::getBytesPerChannel(before.colorFormat);
    const size_t mapBytes = static_cast<size_t>(before.width) * static_cast<size_t>(before.height) * static_cast<size_t>(bytesPerTexel);
    if (before.pixels.size() < mapBytes || !data.getData() || data.getSize() < mapBytes){
        return false;
    }

    TerrainMapPatch patch;
    patch.path = path;
    patch.colorFormat = before.colorFormat;
    patch.mapWidth = before.width;
    patch.mapHeight = before.height;
    patch.channels = before.channels;
    patch.region = stroke.dirtyRegion;
    patch.beforePixels = TerrainMapUtils::copyRegion(before.pixels.data(), patch.mapWidth, bytesPerTexel, patch.region);
    patch.afterPixels = TerrainMapUtils::copyRegion(static_cast<const unsigned char*>(data.getData()), patch.mapWidth, bytesPerTexel, patch.region);

    if (patch.beforePixels != patch.afterPixels){
        CommandHandle::get(sceneProject->id)->addCommandNoMerge(new TerrainMapPatchCmd(project, sceneProject->id, stroke.entity, stroke.ref, std::move(patch)));
    }

    return true;
}

void editor::TerrainEditWindow::clearStroke(){
    stroke = ActiveStroke();
}

bool editor::TerrainEditWindow::createMapForTarget(const TerrainMapRef& ref, int width, int height){
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        return false;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(selectedEntity);
    Texture* texture = TerrainMapUtils::findTexture(terrain, ref);
    if (!texture){
        return false;
    }

    const bool forceBeforePixels = texture->getPath(0).empty() || isOwnedEditableTexturePath(texture->getPath(0), sceneProject->id, selectedEntity, ref);
    TerrainMapSnapshot before = captureSnapshot(project, *texture, forceBeforePixels);
    TerrainMapSnapshot after;
    after.empty = false;
    after.path = makeEditableTexturePath(project, sceneProject->id, selectedEntity, ref);
    after.id = after.path;
    after.minFilter = texture->getMinFilter();
    after.magFilter = texture->getMagFilter();
    after.wrapU = texture->getWrapU();
    after.wrapV = texture->getWrapV();
    after.colorFormat = expectedFormat(ref.target);
    after.channels = expectedChannels(ref.target);
    after.width = std::max(2, width);
    after.height = std::max(2, height);
    after.pixels = makeInitialMapPixels(ref.target, after.width, after.height);

    if (snapshotsEqual(before, after)){
        return false;
    }

    CommandHandle::get(sceneProject->id)->addCommandNoMerge(new TerrainTextureEditCmd(this, project, sceneProject->id, selectedEntity, ref, before, after));
    return true;
}

bool editor::TerrainEditWindow::deleteMapForTarget(const TerrainMapRef& ref){
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        return false;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(selectedEntity);
    Texture* texture = TerrainMapUtils::findTexture(terrain, ref);
    if (!texture || texture->empty()){
        return false;
    }

    const bool forceBeforePixels = texture->getPath(0).empty() || isOwnedEditableTexturePath(texture->getPath(0), sceneProject->id, selectedEntity, ref);
    TerrainMapSnapshot before = captureSnapshot(project, *texture, forceBeforePixels);
    TerrainMapSnapshot after;

    if (snapshotsEqual(before, after)){
        return false;
    }

    CommandHandle::get(sceneProject->id)->addCommandNoMerge(new TerrainTextureEditCmd(this, project, sceneProject->id, selectedEntity, ref, before, after));
    return true;
}

// Layer add and remove rewrite the whole vector through the property system, so undo, bundle
// propagation and scene-modified tracking all come from PropertyCmd.
bool editor::TerrainEditWindow::setFoliageLayers(const std::vector<TerrainFoliageLayer>& layers){
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        return false;
    }

    CommandHandle::get(sceneProject->id)->addCommandNoMerge(new PropertyCmd<std::vector<TerrainFoliageLayer>>(
        project, sceneProject->id, selectedEntity, ComponentType::TerrainComponent, "foliageLayers", layers));
    return true;
}

// Merged, so dragging a slider collapses into one undo entry.
template<typename T>
bool editor::TerrainEditWindow::setFoliageLayerProperty(const char* field, const T& value){
    SceneProject* sceneProject = getTargetSceneProject();
    if (!hasValidTarget(sceneProject)){
        return false;
    }

    const std::string property = "foliageLayers[" + std::to_string(selectedFoliageLayer) + "]." + field;
    CommandHandle::get(sceneProject->id)->addCommand(new PropertyCmd<T>(
        project, sceneProject->id, selectedEntity, ComponentType::TerrainComponent, property, value));
    return true;
}

void editor::TerrainEditWindow::show(){
    if (!windowOpen){
        return;
    }

    updateTargetFromSelection();

    ImGui::SetNextWindowSize(ImVec2(360.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (focusRequested){
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }

    bool wasOpen = windowOpen;
    if (!ImGui::Begin(WINDOW_NAME, &windowOpen)){
        ImGui::End();
        if (wasOpen && !windowOpen){
            brushActive = false;
            endStroke();
        }
        return;
    }

    SceneProject* sceneProject = getTargetSceneProject();
    bool validTarget = hasValidTarget(sceneProject);

    if (!validTarget){
        brushActive = false;
        endStroke();
        ImGui::Spacing();
        ImGui::TextDisabled("No terrain is selected");
        ImGui::End();
        return;
    }

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(selectedEntity);

    std::string selectedTerrainLabel = "Selected terrain: " + sceneProject->scene->getEntityName(selectedEntity);
    ImGui::TextUnformatted(selectedTerrainLabel.c_str());
    const int foliageLayerCount = static_cast<int>(terrain.foliageLayers.size());
    selectedFoliageLayer = std::clamp(selectedFoliageLayer, 0, std::max(0, foliageLayerCount - 1));

    TerrainMapInfo heightInfo = getTerrainMapInfo(terrain.heightMap);
    TerrainMapInfo blendInfo = getTerrainMapInfo(terrain.blendMap);
    TerrainMapInfo densityInfo;
    if (foliageLayerCount > 0){
        densityInfo = getTerrainMapInfo(terrain.foliageLayers[selectedFoliageLayer].densityMap);
    }
    const bool hasHeightMap = heightInfo.present;
    const bool hasBlendMap = blendInfo.present;
    const bool hasDensityMap = densityInfo.present;

    const bool brushTargetAvailable = isHeightBrush() ? hasHeightMap : (isDensityBrush() ? hasDensityMap : hasBlendMap);
    if (brushActive && !brushTargetAvailable){
        brushActive = false;
        endStroke();
    }

    ImGui::SeparatorText("Maps");

    heightMapResolution = std::max(2, heightMapResolution);
    blendMapResolution = std::max(2, blendMapResolution);
    densityMapResolution = std::max(2, densityMapResolution);

    auto mapSection = [&](const TerrainMapRef& ref, const char* icon, const char* label, const TerrainMapInfo& info, int& resolution, const char* id){
        ImGui::PushID(id);

        const float resolutionInputWidth = 80.0f;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float mapActionIconWidth = std::max(std::max(ImGui::CalcTextSize(ICON_FA_ARROWS_ROTATE).x,
                                                           ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x),
                                                  ImGui::CalcTextSize(ICON_FA_CIRCLE_HALF_STROKE).x) + style.FramePadding.x * 2.0f;
        const ImVec2 iconButtonSize(mapActionIconWidth, 0.0f);
        const bool canRecreate = info.present && info.sizeKnown && info.width > 0 && info.height > 0;
        const char* statusIcon = (!info.present || !info.sizeKnown) ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_CHECK;
        const char* statusTooltip = !info.present ? "Map is not assigned" : (info.sizeKnown ? "Map is assigned" : "Map is assigned, but its texture data is not available yet.");
        const char* createText = ICON_FA_PLUS "  Create";

        auto heightMiddleButton = [&](){
            if (ref.target != TerrainMapTarget::HeightMap){
                return;
            }

            ImGui::SameLine();
            if (iconButton(ICON_FA_CIRCLE_HALF_STROKE, "height_middle_start", "Generate heightmaps at middle height", heightMapStartAtMiddle, iconButtonSize)){
                heightMapStartAtMiddle = !heightMapStartAtMiddle;
            }
        };

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

            heightMiddleButton();

            ImGui::SameLine();
            if (ImGui::Button((std::string(createText) + "##create_map_action").c_str())){
                createMapForTarget(ref, resolution, resolution);
            }
            showTooltip("Create an editable map with the chosen resolution");
        }else{
            if (info.sizeKnown){
                ImGui::TextUnformatted(resolutionText.c_str());
            }else{
                ImGui::TextDisabled("%s", resolutionText.c_str());
            }

            heightMiddleButton();

            ImGui::SameLine();
            ImGui::BeginDisabled(!canRecreate);
            if (iconButton(ICON_FA_ARROWS_ROTATE, "recreate_map_action", canRecreate ? "Recreate this map using its current resolution" : "Current map size is not available yet", false, iconButtonSize)){
                createMapForTarget(ref, info.width, info.height);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (iconButton(ICON_FA_TRASH_CAN, "delete_map_action", "Delete this map", false, iconButtonSize)){
                deleteMapForTarget(ref);
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
            endStroke();
            if (selected){
                brushActive = false;
            }else{
                brushMode = mode;
                brushActive = true;
            }
        }
    };

    ImGui::BeginDisabled(!hasHeightMap);
    brushButton(TerrainBrushMode::Raise, ICON_FA_ARROW_UP, "terrain_raise", "Raise terrain (Ctrl-drag lowers, Shift-drag smooths)");
    ImGui::SameLine();
    brushButton(TerrainBrushMode::Lower, ICON_FA_ARROW_DOWN, "terrain_lower", "Lower terrain (Ctrl-drag raises, Shift-drag smooths)");
    ImGui::SameLine();
    brushButton(TerrainBrushMode::Smooth, ICON_FA_WATER, "terrain_smooth", "Smooth terrain");
    ImGui::SameLine();
    brushButton(TerrainBrushMode::Flatten, ICON_FA_GRIP_LINES, "terrain_flatten", "Flatten terrain (Shift-drag smooths)");
    ImGui::EndDisabled();
    if (!hasHeightMap){
        ImGui::TextDisabled("Heightmap missing");
    }

    ImGui::SeparatorText("Paint");

    auto paintButton = [&](TerrainBrushMode mode, const char* id, const char* tooltip, const ImVec4& color){
        bool selected = brushActive && brushMode == mode;
        if (colorIconButton(ICON_FA_BRUSH, id, tooltip, selected, color, toolButtonSize)){
            endStroke();
            if (selected){
                brushActive = false;
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

    ImGui::SeparatorText("Foliage");

    // Adding or removing a layer resizes the vector the rows below read, so the section stops
    // for the frame and picks the new set up on the next one.
    bool foliageLayersChanged = false;
    const ImVec2 layerButtonSize(ImGui::GetFrameHeight(), 0.0f);

    auto addFoliageLayer = [&](){
        endStroke();

        std::vector<TerrainFoliageLayer> layers = terrain.foliageLayers;
        layers.push_back(TerrainFoliageLayer());
        if (setFoliageLayers(layers)){
            selectedFoliageLayer = static_cast<int>(layers.size()) - 1;
            foliageLayersChanged = true;
        }
    };

    if (foliageLayerCount == 0){
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("No foliage layers");
        ImGui::SameLine();
        if (iconButton(ICON_FA_PLUS, "add_foliage_layer", "Add a foliage layer", false, layerButtonSize)){
            addFoliageLayer();
        }
    }else{
        std::string layerPreview = "Layer " + std::to_string(selectedFoliageLayer + 1) + " of " + std::to_string(foliageLayerCount);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) * 2.0f);
        if (ImGui::BeginCombo("##foliage_layer", layerPreview.c_str())){
            for (int i = 0; i < foliageLayerCount; i++){
                std::string label = "Layer " + std::to_string(i + 1);
                if (ImGui::Selectable(label.c_str(), i == selectedFoliageLayer)){
                    endStroke();
                    selectedFoliageLayer = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (iconButton(ICON_FA_PLUS, "add_foliage_layer", "Add a foliage layer", false, layerButtonSize)){
            addFoliageLayer();
        }

        ImGui::SameLine();
        if (iconButton(ICON_FA_TRASH_CAN, "remove_foliage_layer", "Remove this foliage layer", false, layerButtonSize)){
            // Removing shifts every layer above this one, so the stroke has to be recorded while
            // stroke.ref still names the map it painted.
            endStroke();

            std::vector<TerrainFoliageLayer> layers = terrain.foliageLayers;
            layers.erase(layers.begin() + selectedFoliageLayer);
            if (setFoliageLayers(layers)){
                brushActive = false;
                foliageLayersChanged = true;
            }
        }
    }

    if (foliageLayerCount > 0 && !foliageLayersChanged){
        mapSection(TerrainMapRef(TerrainMapTarget::DensityMap, selectedFoliageLayer), ICON_FA_SEEDLING, "Density", densityInfo, densityMapResolution, "density");

        TerrainFoliageLayer& layer = terrain.foliageLayers[selectedFoliageLayer];

        const std::string meshName = layer.meshPath.empty() ? "Drop a model here" : fs::path(layer.meshPath).filename().string();
        ImGui::Button((std::string(ICON_FA_CUBE) + "  " + meshName + "##foliage_mesh").c_str(), ImVec2(-1.0f, 0.0f));
        if (ImGui::BeginDragDropTarget()){
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")){
                std::vector<std::string> dropped = Util::getStringsFromPayload(payload);
                if (!dropped.empty() && Util::isModelFile(dropped[0])){
                    const bool insideAssets = project->isInsideAssetsPath(dropped[0]);
                    const std::string meshPath = insideAssets ? project->normalizeToAssetsRelative(dropped[0]).generic_string()
                                                              : fs::path(dropped[0]).generic_string();
                    setFoliageLayerProperty<std::string>("meshPath", meshPath);
                }
            }
            ImGui::EndDragDropTarget();
        }
        showTooltip(layer.meshPath.empty() ? "Drag a model from Resources to scatter it over the painted area" : layer.meshPath.c_str());

        float density = layer.density;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat("##foliage_density", &density, 0.05f, 0.0f, 20.0f, "Density  %.2f")){
            setFoliageLayerProperty<float>("density", std::max(0.0f, density));
        }
        showTooltip("Instances per square world unit where the density map is fully painted");

        float minScale = layer.minScale;
        float maxScale = layer.maxScale;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloatRange2("##foliage_scale", &minScale, &maxScale, 0.01f, 0.01f, 20.0f, "Scale  %.2f", "to %.2f")){
            setFoliageLayerProperty<float>("minScale", std::max(0.01f, minScale));
            setFoliageLayerProperty<float>("maxScale", std::max(minScale, maxScale));
        }
        showTooltip("Random scale range applied per instance");

        float minSlope = layer.minSlope;
        float maxSlope = layer.maxSlope;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloatRange2("##foliage_slope", &minSlope, &maxSlope, 0.5f, 0.0f, 90.0f, "Slope  %.0f" ICON_FA_ANGLE_UP, "to %.0f" ICON_FA_ANGLE_UP)){
            setFoliageLayerProperty<float>("minSlope", std::clamp(minSlope, 0.0f, 90.0f));
            setFoliageLayerProperty<float>("maxSlope", std::clamp(maxSlope, minSlope, 90.0f));
        }
        showTooltip("Instances only land on ground within this slope range, in degrees");

        float rotationJitter = layer.rotationJitter;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##foliage_rotation", &rotationJitter, 0.0f, 1.0f, "Rotation  %.2f")){
            setFoliageLayerProperty<float>("rotationJitter", rotationJitter);
        }
        showTooltip("Share of a full turn of random yaw per instance");

        float alignToNormal = layer.alignToNormal;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##foliage_align", &alignToNormal, 0.0f, 1.0f, "Align  %.2f")){
            setFoliageLayerProperty<float>("alignToNormal", alignToNormal);
        }
        showTooltip("0 stands instances upright, 1 lays them along the surface");

        float drawDistance = layer.drawDistance;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat("##foliage_distance", &drawDistance, 1.0f, 1.0f, 500.0f, "Distance  %.0f")){
            setFoliageLayerProperty<float>("drawDistance", std::max(1.0f, drawDistance));
        }
        showTooltip("Instances beyond this distance from the camera are not resolved");

        int seed = static_cast<int>(layer.seed);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragInt("##foliage_seed", &seed, 1.0f, 0, 100000, "Seed  %d")){
            setFoliageLayerProperty<unsigned int>("seed", static_cast<unsigned int>(std::max(0, seed)));
        }
        showTooltip("Changing the seed reshuffles where instances land");

        ImGui::BeginDisabled(!hasDensityMap);
        paintButton(TerrainBrushMode::PaintDensity, "terrain_paint_density", "Paint foliage density (Ctrl-drag erases)", ImVec4(0.45f, 0.75f, 0.35f, 1.0f));
        ImGui::SameLine();
        paintButton(TerrainBrushMode::EraseDensity, "terrain_erase_density", "Erase foliage density (Ctrl-drag paints)", ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
        ImGui::EndDisabled();
        if (!hasDensityMap){
            ImGui::TextDisabled("Density map missing");
        }
    }

    ImGui::SeparatorText("Brush");

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

    brushSize = std::clamp(brushSize, MIN_BRUSH_SIZE, MAX_BRUSH_SIZE);
    ImGui::SetNextItemWidth(-1.0f);
    UIUtils::sliderFloatInput("##brush_size", &brushSize, MIN_BRUSH_SIZE, MAX_BRUSH_SIZE, ICON_FA_CIRCLE "  %.2f");
    showTooltip("Brush size ([ and ] while painting)");

    brushStrength = std::clamp(brushStrength, MIN_BRUSH_STRENGTH, MAX_BRUSH_STRENGTH);
    ImGui::SetNextItemWidth(-1.0f);
    UIUtils::sliderFloatInput("##brush_strength", &brushStrength, MIN_BRUSH_STRENGTH, MAX_BRUSH_STRENGTH, ICON_FA_GAUGE_HIGH "  %.2f");
    showTooltip("Brush strength: how fast the brush acts while held (Shift+[ and Shift+] while painting)");

    if (brushMode == TerrainBrushMode::Flatten){
        if (iconButton(ICON_FA_EYE_DROPPER, "flatten_pick", "Pick the flatten height from the terrain at the start of each stroke", flattenPickOnStroke, toolButtonSize)){
            flattenPickOnStroke = !flattenPickOnStroke;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(flattenPickOnStroke);
        ImGui::SetNextItemWidth(-1.0f);
        UIUtils::sliderFloatInput("##flatten_height", &flattenHeight, 0.0f, 1.0f, ICON_FA_GRIP_LINES "  %.3f");
        showTooltip("Flatten height (normalized)");
        ImGui::EndDisabled();
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
        ts.densityMapResolution = densityMapResolution;
        ts.normalizeBlendPaint = normalizeBlendPaint;
        ts.heightMapStartAtMiddle = heightMapStartAtMiddle;
        ts.flattenPickOnStroke = flattenPickOnStroke;
    }

    if (wasOpen && !windowOpen){
        setOpen(false);
    }
}

void editor::TerrainEditWindow::open(){
    setOpen(true);
    updateTargetFromSelection();
}

void editor::TerrainEditWindow::setOpen(bool open){
    if (open){
        if (!windowOpen){
            focusRequested = true;
        }
        windowOpen = true;
        return;
    }

    windowOpen = false;
    focusRequested = false;
    brushActive = false;
    endStroke();
}

void editor::TerrainEditWindow::openForEntity(Entity entity, uint32_t sceneId){
    open();
    selectedSceneId = sceneId;
    selectedEntity = entity;

    const TerrainEditorSettings& ts = project->getTerrainEditorSettings();
    brushMode     = static_cast<TerrainBrushMode>(ts.brushMode);
    brushShape    = static_cast<TerrainBrushShape>(ts.brushShape);
    brushFalloff  = static_cast<TerrainBrushFalloff>(ts.brushFalloff);
    brushSize     = std::clamp(ts.brushSize, MIN_BRUSH_SIZE, MAX_BRUSH_SIZE);
    brushStrength = std::clamp(ts.brushStrength, MIN_BRUSH_STRENGTH, MAX_BRUSH_STRENGTH);
    flattenHeight = ts.flattenHeight;
    heightMapResolution = ts.heightMapResolution;
    blendMapResolution  = ts.blendMapResolution;
    densityMapResolution = ts.densityMapResolution;
    normalizeBlendPaint = ts.normalizeBlendPaint;
    heightMapStartAtMiddle = ts.heightMapStartAtMiddle;
    flattenPickOnStroke = ts.flattenPickOnStroke;
}

bool editor::TerrainEditWindow::isOpen() const{
    return windowOpen;
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

    Texture* texture = TerrainMapUtils::findTexture(*terrain, getBrushMapRef());
    return texture && !texture->empty();
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

    const TerrainMapRef ref = getBrushMapRef();
    Texture* texture = TerrainMapUtils::findTexture(scene->getComponent<TerrainComponent>(entity), ref);
    if (!texture){
        return false;
    }

    clearStroke();
    stroke.active = true;
    stroke.sceneId = sceneProject->id;
    stroke.entity = entity;
    stroke.ref = ref;

    // Modifiers picked up at stroke start and held for the whole stroke:
    // Shift turns any sculpt brush into Smooth, Ctrl inverts Raise/Lower and paint/erase.
    const ImGuiIO& io = ImGui::GetIO();
    stroke.effectiveMode = brushMode;
    if (isHeightBrush()){
        if (io.KeyShift){
            stroke.effectiveMode = TerrainBrushMode::Smooth;
        }else if (io.KeyCtrl){
            if (brushMode == TerrainBrushMode::Raise){
                stroke.effectiveMode = TerrainBrushMode::Lower;
            }else if (brushMode == TerrainBrushMode::Lower){
                stroke.effectiveMode = TerrainBrushMode::Raise;
            }
        }
    }else if (isDensityBrush() && io.KeyCtrl){
        stroke.effectiveMode = brushMode == TerrainBrushMode::PaintDensity ? TerrainBrushMode::EraseDensity : TerrainBrushMode::PaintDensity;
    }

    const bool forceBeforePixels = texture->getPath(0).empty() || isOwnedEditableTexturePath(texture->getPath(0), sceneProject->id, entity, ref);
    stroke.beforeSnapshot = captureSnapshot(project, *texture, forceBeforePixels);
    if (ref.target == TerrainMapTarget::HeightMap){
        TerrainComponent& terrain = scene->getComponent<TerrainComponent>(entity);
        captureStrokeHeightReference(terrain);

        // Flatten levels toward the height under the first click (slider value is
        // used instead when picking is disabled).
        stroke.flattenTarget = flattenHeight;
        if (std::abs(terrain.maxHeight) > std::numeric_limits<float>::epsilon()){
            stroke.flattenTarget = std::clamp(localHeight / terrain.maxHeight, 0.0f, 1.0f);
        }
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
        // The ray left the terrain: drop the path anchor so re-entering doesn't
        // interpolate a streak across the gap.
        stroke.hasLastPoint = false;
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
            Texture* texture = TerrainMapUtils::findTexture(*terrain, stroke.ref);
            if (texture && !addStrokePatchCommand(sceneProject, *texture)){
                TerrainMapSnapshot after = captureSnapshot(project, *texture, true);
                if (!snapshotsEqual(stroke.beforeSnapshot, after)){
                    CommandHandle::get(stroke.sceneId)->addCommandNoMerge(new TerrainTextureEditCmd(this, project, stroke.sceneId, stroke.entity, stroke.ref, stroke.beforeSnapshot, after));
                }
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
    TerrainComponent& terrain = scene->getComponent<TerrainComponent>(entity);

    // Drape the cursor over the live heightmap so it follows the terrain's
    // topology (including edits still in progress).
    const unsigned char* heightPixels = nullptr;
    int mapWidth = 0, mapHeight = 0, mapChannels = 0, mapBytesPerChannel = 1;
    if (!terrain.heightMap.empty() && !terrain.heightMap.isFramebuffer() && TerrainMapUtils::hasLoadedData(terrain.heightMap)){
        TextureData& heightData = terrain.heightMap.getData();
        heightPixels = static_cast<const unsigned char*>(heightData.getData());
        mapWidth = heightData.getWidth();
        mapHeight = heightData.getHeight();
        mapChannels = heightData.getChannels();
        mapBytesPerChannel = TextureData::getBytesPerChannel(heightData.getColorFormat());
    }

    const float terrainSize = std::max(terrain.terrainSize, std::numeric_limits<float>::epsilon());
    const float halfSize = terrainSize * 0.5f;
    const float lift = std::max(0.03f, std::abs(terrain.maxHeight) * 0.01f);
    auto surfacePoint = [&](float localX, float localZ){
        const float clampedX = std::clamp(localX, -halfSize, halfSize);
        const float clampedZ = std::clamp(localZ, -halfSize, halfSize);
        float surfaceHeight = 0.0f;
        if (heightPixels){
            const float u = (clampedX + halfSize) / terrainSize;
            const float v = (clampedZ + halfSize) / terrainSize;
            surfaceHeight = bilinearHeightSample(heightPixels, mapWidth, mapHeight, mapChannels, mapBytesPerChannel,
                                                 u * static_cast<float>(mapWidth - 1), v * static_cast<float>(mapHeight - 1)) * terrain.maxHeight;
        }
        return transform.modelMatrix * Vector3(clampedX, surfaceHeight + lift, clampedZ);
    };

    auto buildLoop = [&](float radius, int segmentsPerQuarter, std::vector<Vector3>& points){
        points.clear();
        if (brushShape == TerrainBrushShape::Circle){
            const int segments = segmentsPerQuarter * 4;
            const float twoPi = 6.28318530718f;
            points.reserve(segments);
            for (int i = 0; i < segments; i++){
                const float angle = (twoPi * static_cast<float>(i)) / static_cast<float>(segments);
                points.push_back(surfacePoint(localPoint.x + radius * std::cos(angle), localPoint.z + radius * std::sin(angle)));
            }
        }else{
            const Vector2 corners[4] = {
                Vector2(localPoint.x - radius, localPoint.z - radius),
                Vector2(localPoint.x + radius, localPoint.z - radius),
                Vector2(localPoint.x + radius, localPoint.z + radius),
                Vector2(localPoint.x - radius, localPoint.z + radius)};
            points.reserve(segmentsPerQuarter * 4);
            for (int edge = 0; edge < 4; edge++){
                const Vector2& from = corners[edge];
                const Vector2& to = corners[(edge + 1) % 4];
                for (int i = 0; i < segmentsPerQuarter; i++){
                    const float t = static_cast<float>(i) / static_cast<float>(segmentsPerQuarter);
                    points.push_back(surfacePoint(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t));
                }
            }
        }
    };

    cursor.visible = true;
    buildLoop(brushSize, 16, cursor.outerPoints);
    if (brushFalloff != TerrainBrushFalloff::Constant){
        // Half-strength contour: both smoothstep and linear falloff reach 0.5 at
        // half the brush radius.
        buildLoop(brushSize * 0.5f, 12, cursor.innerPoints);
    }else{
        cursor.innerPoints.clear();
    }
    return true;
}

void editor::TerrainEditWindow::adjustBrushSize(float factor){
    brushSize = std::clamp(brushSize * factor, MIN_BRUSH_SIZE, MAX_BRUSH_SIZE);
}

void editor::TerrainEditWindow::adjustBrushStrength(float factor){
    brushStrength = std::clamp(brushStrength * factor, MIN_BRUSH_STRENGTH, MAX_BRUSH_STRENGTH);
}
