// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TextureDataPool.h"

#include "Engine.h"
#include "Log.h"
#include "thread/ResourceProgress.h"
#include "thread/ThreadPoolManager.h"

#include <filesystem>
#include <vector>

using namespace doriax;

std::unordered_map<std::string, std::future<std::shared_ptr<std::array<TextureData,6>>>> TextureDataPool::pendingBuilds;
std::mutex TextureDataPool::cacheMutex;
std::atomic<bool> TextureDataPool::shutdownRequested{false};

texturesdata_t& TextureDataPool::getMap(){
    //To prevent similar problem of static init fiasco but on deinitialization
    //https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
    static texturesdata_t* map = new texturesdata_t();
    return *map;
};

bool TextureDataPool::hasTexturePixels(const std::shared_ptr<std::array<TextureData,6>>& data, size_t numFaces) {
    if (!data) {
        return false;
    }

    for (size_t f = 0; f < numFaces; f++) {
        TextureData& face = data->at(f);
        if (!face.getData() || face.getSize() == 0) {
            return false;
        }
    }

    return true;
}

size_t TextureDataPool::getTextureFaces(std::array<TextureData,6>& data) {
    for (size_t f = 1; f < 6; f++) {
        if (data.at(f).getData() && data.at(f).getSize() > 0) {
            return 6;
        }
    }

    return 1;
}

std::shared_ptr<std::array<TextureData,6>> TextureDataPool::get(const std::string& id){
    auto& map = getMap();
    auto it = map.find(id);

    if (it != map.end() && it->second){
        return it->second;
    }

    return nullptr;
}

void TextureDataPool::put(const std::string& id, std::shared_ptr<std::array<TextureData,6>> data){
    if (!data){
        return;
    }

    auto& map = getMap();
    auto it = map.find(id);
    if (it != map.end() && hasTexturePixels(it->second, getTextureFaces(*it->second))){
        return; // already cached with pixels; keep the existing entry
    }

    map[id] = std::move(data);
}

std::shared_ptr<std::array<TextureData,6>> TextureDataPool::get(const std::string& id, std::array<TextureData,6> data){
    auto& map = getMap();
    auto it = map.find(id);
    std::shared_ptr<std::array<TextureData,6>> shared = (it != map.end()) ? it->second : nullptr;
    size_t numFaces = getTextureFaces(data);

    if (shared && hasTexturePixels(shared, numFaces)){
		return shared;
	}

	shared = std::make_shared<std::array<TextureData,6>>(data);
    map[id] = shared;

	return shared;
}

TextureLoadResult TextureDataPool::loadFromFile(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces, bool fixAlphaBorder) {
    auto& shared = getMap()[id];

    TextureLoadResult result;
    result.id = id;

    if (shared && shared.use_count() > 0 && hasTexturePixels(shared, numFaces)) {
        result.state = ResourceLoadState::Finished;
        result.data = shared;
        return result;
    }

    // The pool entry may still exist while other Texture instances hold the old
    // shared_ptr, even after releaseDataAfterLoad freed the pixel buffer. In
    // that case, keep the stale object alive for existing holders but replace
    // the cached map entry with a freshly loaded copy.

    if (Engine::isAsyncLoading()) {
        std::lock_guard<std::mutex> lock(cacheMutex);

        if (shutdownRequested.load()) {
            throw std::runtime_error("Shutdown requested");
        }

        // Check if already building
        auto it = pendingBuilds.find(id);
        if (it != pendingBuilds.end()) {
            auto& future = it->second;
            if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                // Build finished, move to cache
                try {
                    shared = future.get();
                    pendingBuilds.erase(it);

                    result.state = ResourceLoadState::Finished;
                    result.data = shared;
                    return result;
                } catch (const std::exception& e) {
                    pendingBuilds.erase(it);

                    result.state = ResourceLoadState::Failed;
                    result.errorMessage = e.what();
                    return result;
                }
            } else if (future.valid()) {
                // Still building
                result.state = ResourceLoadState::Loading;
                return result;
            } else {
                // Invalid future, remove it
                pendingBuilds.erase(it);
                result.state = ResourceLoadState::Failed;
                result.errorMessage = "Invalid future";
                return result;
            }
        }

        if (shutdownRequested.load()) {
            throw std::runtime_error("Shutdown requested");
        }

        // Start new async build
        std::string textureName = getTextureDisplayName(paths[0].empty() ? id : paths[0]);
        uint64_t buildId = std::hash<std::string>{}(id);

        ResourceProgress::startBuild(buildId, ResourceType::Texture, textureName);

        // Use thread pool instead of std::async
        pendingBuilds[id] = ThreadPoolManager::getInstance().enqueue(
            [id, paths, numFaces, fixAlphaBorder, buildId]() {
                return loadTextureInternal(id, paths, numFaces, fixAlphaBorder, true);
            }
        );

    } else {
        // Synchronous loading remains the same
        try {
            shared = loadTextureInternal(id, paths, numFaces, fixAlphaBorder, false);

            result.state = ResourceLoadState::Finished;
            result.data = shared;
            return result;
        } catch (const std::exception& e) {
            result.state = ResourceLoadState::Failed;
            result.errorMessage = e.what();
            return result;
        }
    }

    result.state = ResourceLoadState::Loading;
    return result;
}

std::shared_ptr<std::array<TextureData,6>> TextureDataPool::loadTextureInternal(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces, bool fixAlphaBorder, bool trackProgress) {
    uint64_t buildId = std::hash<std::string>{}(id);

    if (trackProgress) {
        if (shutdownRequested.load()) {
            throw std::runtime_error("Shutdown requested");
        }

        // Calculate starting progress based on number of faces
        // Single face: start at 0.5, Multiple faces: start proportionally
        float startProgress = numFaces == 1 ? 0.5f : (1.0f / (numFaces * 2.0f));
        ResourceProgress::updateProgress(buildId, startProgress);
    }

    auto shared = std::make_shared<std::array<TextureData,6>>();
    std::array<TextureData,6>& data = *shared;
    // frees the pixels even if the load fails or is never used
    for (TextureData& face : data) {
        face.setDataOwned(true);
    }

    bool isSingleFileCube = (!paths[0].empty());
    for (int f = 1; f < 6 && isSingleFileCube; f++){
        if (!paths[f].empty())
            isSingleFileCube = false;
    }

    if (numFaces == 6 && isSingleFileCube) {
        if (!TextureData::loadCubeMapFromSingleFile(paths[0].c_str(), data)){
            if (trackProgress) {
                ResourceProgress::failBuild(std::hash<std::string>{}(id));
            }
            Log::error("Failed to load cube texture from file: %s", paths[0].c_str());
            throw std::runtime_error("Failed to load cube texture from file: " + paths[0]);
        }

        std::string validationError = validateTextureFaces(data, numFaces);
        if (!validationError.empty()) {
            if (trackProgress) {
                ResourceProgress::failBuild(std::hash<std::string>{}(id));
            }
            Log::error("%s in cubemap: %s", validationError.c_str(), paths[0].c_str());
            throw std::runtime_error(validationError + " in cubemap: " + paths[0]);
        }

        if (trackProgress) {
            if (shutdownRequested.load()) {
                throw std::runtime_error("Shutdown requested");
            }
            ResourceProgress::updateProgress(buildId, 1.0f);
            ResourceProgress::completeBuild(buildId);
        }

        return shared;
    }

    for (size_t f = 0; f < numFaces; f++) {
        if (paths[f].empty()) {
            if (trackProgress) {
                ResourceProgress::failBuild(std::hash<std::string>{}(id));
            }
            Log::error("Texture is missing texture for face %zu", f);
            throw std::runtime_error("Texture is missing texture for face " + std::to_string(f));
        }

        bool success = data[f].loadTextureFromFile(paths[f].c_str());
        if (!success) {
            if (trackProgress) {
                ResourceProgress::failBuild(std::hash<std::string>{}(id));
            }
            if (numFaces == 1){
                Log::error("Failed to load texture from file: %s", paths[f].c_str());
                throw std::runtime_error("Failed to load texture from file: " + paths[f]);
            }else{
                Log::error("Failed to load texture face %zu from file: %s", f, paths[f].c_str());
                throw std::runtime_error("Failed to load texture face " + std::to_string(f) + " from file: " + paths[f]);
            }
        }

        if (fixAlphaBorder) {
            data[f].fixAlphaBorder();
        }

        // Apply texture strategy
        if (Engine::getTextureStrategy() == TextureStrategy::FIT) {
            data[f].fitPowerOfTwo();
        } else if (Engine::getTextureStrategy() == TextureStrategy::RESIZE) {
            data[f].resizePowerOfTwo();
        }

        // Cubemap faces must be square; resize to the larger dimension if needed
        if (numFaces == 6) {
            data[f].resizeToSquare();
        }

        if (trackProgress) {
            if (shutdownRequested.load()) {
                throw std::runtime_error("Shutdown requested");
            }

            // Calculate progress with better distribution
            float faceProgress;
            if (numFaces == 1) {
                // For single face: go from 0.5 to 0.95
                faceProgress = 0.5f + (0.45f * (f + 1) / static_cast<float>(numFaces));
            } else {
                // For multiple faces: use more granular progress
                float startProgress = 1.0f / (numFaces * 2.0f);
                float workingRange = 0.9f - startProgress;
                faceProgress = startProgress + (workingRange * (f + 1) / static_cast<float>(numFaces));
            }

            ResourceProgress::updateProgress(buildId, faceProgress);
        }
    }

    std::string validationError = validateTextureFaces(data, numFaces);
    if (!validationError.empty()) {
        if (trackProgress) {
            ResourceProgress::failBuild(std::hash<std::string>{}(id));
        }
        Log::error("%s", validationError.c_str());
        throw std::runtime_error(validationError);
    }

    if (trackProgress) {
        if (shutdownRequested.load()) {
            throw std::runtime_error("Shutdown requested");
        }
        ResourceProgress::updateProgress(buildId, 1.0f);  // Completing
        ResourceProgress::completeBuild(std::hash<std::string>{}(id));
    }

    return shared;
}

std::string TextureDataPool::getTextureDisplayName(const std::string& path) {
    std::filesystem::path filePath(path);
    return filePath.filename().string();
}

std::string TextureDataPool::validateTextureFaces(std::array<TextureData,6>& data, size_t numFaces) {
    if (numFaces != 6) {
        return "";
    }

    const int expectedWidth = data[0].getWidth();
    const int expectedHeight = data[0].getHeight();
    const int expectedChannels = data[0].getChannels();
    const ColorFormat expectedFormat = data[0].getColorFormat();

    if (expectedWidth <= 0 || expectedHeight <= 0) {
        return "Invalid cubemap face 0: empty texture data";
    }

    if (expectedWidth != expectedHeight) {
        return "Invalid cubemap face 0: cubemap faces must be square, got "
            + std::to_string(expectedWidth) + "x" + std::to_string(expectedHeight);
    }

    for (size_t f = 1; f < numFaces; f++) {
        const int width = data[f].getWidth();
        const int height = data[f].getHeight();

        if (width <= 0 || height <= 0) {
            return "Invalid cubemap face " + std::to_string(f) + ": empty texture data";
        }

        if (width != height) {
            return "Invalid cubemap face " + std::to_string(f)
                + ": cubemap faces must be square, got "
                + std::to_string(width) + "x" + std::to_string(height);
        }

        if (width != expectedWidth || height != expectedHeight) {
            return "Invalid cubemap face " + std::to_string(f)
                + ": all cubemap faces must share the same dimensions, expected "
                + std::to_string(expectedWidth) + "x" + std::to_string(expectedHeight)
                + ", got " + std::to_string(width) + "x" + std::to_string(height);
        }

        if (data[f].getChannels() != expectedChannels || data[f].getColorFormat() != expectedFormat) {
            return "Invalid cubemap face " + std::to_string(f)
                + ": all cubemap faces must share the same pixel format";
        }
    }

    return "";
}

void TextureDataPool::requestShutdown() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    shutdownRequested = true;

    // Wait for all pending builds to complete
    for (auto& [id, future] : pendingBuilds) {
        if (future.valid()) {
            future.wait(); // Wait for completion
        }
    }
    pendingBuilds.clear();
}

void TextureDataPool::cancelShutdown() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    shutdownRequested = false;
}

void TextureDataPool::remove(const std::string& id){
    bool removedPending = false;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto pendingIt = pendingBuilds.find(id);
        if (pendingIt != pendingBuilds.end()) {
            // Erasing a packaged_task future does not cancel its work. Wait so
            // the old load cannot overlap a replacement request with this id.
            if (pendingIt->second.valid()) {
                pendingIt->second.wait();
            }
            pendingBuilds.erase(pendingIt);
            removedPending = true;
        }
    }

    auto& map = getMap();
    auto it = map.find(id);
    if (it != map.end()){
        if (!it->second || it->second.use_count() <= 1){
            map.erase(it);
        }
	}else if (!removedPending){
		if (Engine::isViewLoaded()){
			Log::debug("Trying to destroy a non existent texture data: %s", id.c_str());
		}
	}
}

void TextureDataPool::invalidate(const std::string& id){
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto pendingIt = pendingBuilds.find(id);
        if (pendingIt != pendingBuilds.end()) {
            if (pendingIt->second.valid()) {
                pendingIt->second.wait();
            }
            pendingBuilds.erase(pendingIt);
        }
    }
    getMap().erase(id);
}

void TextureDataPool::clear(){
    {
		std::lock_guard<std::mutex> lock(cacheMutex);
		// Clear pending builds
		for (auto& [id, future] : pendingBuilds) {
			if (future.valid()) {
				future.wait(); // Wait for completion before clearing
			}
		}
		pendingBuilds.clear();
	}
	getMap().clear();
}

void TextureDataPool::clearUnused(){
    // Quiesce in-flight loads before sweeping the cache. The decode workers do
    // not acquire cacheMutex, while holding it prevents a replacement load from
    // being registered under the same id during the wait.
    {
		std::lock_guard<std::mutex> lock(cacheMutex);
		for (auto& [id, future] : pendingBuilds) {
			if (future.valid()) {
				future.wait();
			}
		}
		pendingBuilds.clear();
	}

	std::vector<std::string> ids;
	ids.reserve(getMap().size());
	for (const auto& [id, _] : getMap()) {
		ids.push_back(id);
	}
	for (const auto& id : ids) {
		remove(id);
	}
}
