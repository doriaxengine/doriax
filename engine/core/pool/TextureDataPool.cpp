//
// (c) 2026 Eduardo Doria.
//

#include "TextureDataPool.h"

#include "Engine.h"
#include "Log.h"
#include "thread/ResourceProgress.h"
#include "thread/ThreadPoolManager.h"

#include <filesystem>

using namespace doriax;

bool TextureDataPool::asyncLoading = false;
std::unordered_map<std::string, std::future<std::array<TextureData,6>>> TextureDataPool::pendingBuilds;
std::mutex TextureDataPool::cacheMutex;
std::atomic<bool> TextureDataPool::shutdownRequested{false};

texturesdata_t& TextureDataPool::getMap(){
    //To prevent similar problem of static init fiasco but on deinitialization
    //https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
    static texturesdata_t* map = new texturesdata_t();
    return *map;
};

std::shared_ptr<std::array<TextureData,6>> TextureDataPool::get(const std::string& id){
	auto& shared = getMap()[id];

	if (shared.use_count() > 0){
		return shared;
	}

	return nullptr;
}

std::shared_ptr<std::array<TextureData,6>> TextureDataPool::get(const std::string& id, std::array<TextureData,6> data){
	auto& shared = getMap()[id];

	if (shared.use_count() > 0){
		return shared;
	}

	shared = std::make_shared<std::array<TextureData,6>>(data);

	return shared;
}

TextureLoadResult TextureDataPool::loadFromFile(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces) {
    auto& shared = getMap()[id];

    TextureLoadResult result;
    result.id = id;

    if (shared && shared.use_count() > 0) {
        result.state = ResourceLoadState::Finished;
        result.data = shared;
        return result;
    }

    if (asyncLoading) {
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
                    std::array<TextureData,6> data = future.get();
                    shared = std::make_shared<std::array<TextureData,6>>(data);
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
        std::string textureName = getTextureDisplayName(id);
        uint64_t buildId = std::hash<std::string>{}(id);

        ResourceProgress::startBuild(buildId, ResourceType::Texture, textureName);

        // Use thread pool instead of std::async
        pendingBuilds[id] = ThreadPoolManager::getInstance().enqueue(
            [id, paths, numFaces, buildId]() {
                return loadTextureInternal(id, paths, numFaces);
            }
        );

    } else {
        // Synchronous loading remains the same
        try {
            std::array<TextureData,6> data = loadTextureInternal(id, paths, numFaces);
            shared = std::make_shared<std::array<TextureData,6>>(data);

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

std::array<TextureData,6> TextureDataPool::loadTextureInternal(const std::string& id, const std::array<std::string, 6>& paths, size_t numFaces) {
    uint64_t buildId = std::hash<std::string>{}(id);

    if (asyncLoading) {
        if (shutdownRequested.load()) {
            throw std::runtime_error("Shutdown requested");
        }

        // Calculate starting progress based on number of faces
        // Single face: start at 0.5, Multiple faces: start proportionally
        float startProgress = numFaces == 1 ? 0.5f : (1.0f / (numFaces * 2.0f));
        ResourceProgress::updateProgress(buildId, startProgress);
    }

    std::array<TextureData,6> data;

    bool isSingleFileCube = (!paths[0].empty());
    for (int f = 1; f < 6 && isSingleFileCube; f++){
        if (!paths[f].empty())
            isSingleFileCube = false;
    }

    if (numFaces == 6 && isSingleFileCube) {
        if (!TextureData::loadCubeMapFromSingleFile(paths[0].c_str(), data)){
            ResourceProgress::failBuild(std::hash<std::string>{}(id));
            Log::error("Failed to load cube texture from file: %s", paths[0].c_str());
            throw std::runtime_error("Failed to load cube texture from file: " + paths[0]);
        }

        std::string validationError = validateTextureFaces(data, numFaces);
        if (!validationError.empty()) {
            ResourceProgress::failBuild(std::hash<std::string>{}(id));
            Log::error("%s in cubemap: %s", validationError.c_str(), paths[0].c_str());
            throw std::runtime_error(validationError + " in cubemap: " + paths[0]);
        }

        if (asyncLoading) {
            if (shutdownRequested.load()) {
                throw std::runtime_error("Shutdown requested");
            }
            ResourceProgress::updateProgress(buildId, 1.0f);
            ResourceProgress::completeBuild(buildId);
        }

        return data;
    }

    for (size_t f = 0; f < numFaces; f++) {
        if (paths[f].empty()) {
            ResourceProgress::failBuild(std::hash<std::string>{}(id));
            Log::error("Texture is missing texture for face %zu", f);
            throw std::runtime_error("Texture is missing texture for face " + std::to_string(f));
        }

        bool success = data[f].loadTextureFromFile(paths[f].c_str());
        if (!success) {
            ResourceProgress::failBuild(std::hash<std::string>{}(id));
            if (numFaces == 1){
                Log::error("Failed to load texture from file: %s", paths[f].c_str());
                throw std::runtime_error("Failed to load texture from file: " + paths[f]);
            }else{
                Log::error("Failed to load texture face %zu from file: %s", f, paths[f].c_str());
                throw std::runtime_error("Failed to load texture face " + std::to_string(f) + " from file: " + paths[f]);
            }
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

        if (asyncLoading) {
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
        ResourceProgress::failBuild(std::hash<std::string>{}(id));
        Log::error("%s", validationError.c_str());
        throw std::runtime_error(validationError);
    }

    if (asyncLoading) {
        if (shutdownRequested.load()) {
            throw std::runtime_error("Shutdown requested");
        }
        ResourceProgress::updateProgress(buildId, 1.0f);  // Completing
        ResourceProgress::completeBuild(std::hash<std::string>{}(id));
    }

    return data;
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

void TextureDataPool::setAsyncLoading(bool enable){
    asyncLoading = enable;
}

bool TextureDataPool::isAsyncLoading(){
    return asyncLoading;
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

void TextureDataPool::remove(const std::string& id){
	if (getMap().count(id)){
		auto& shared = getMap()[id];
		if (shared.use_count() <= 1){
			getMap().erase(id);
		}
	}else{
		if (Engine::isViewLoaded()){
			Log::debug("Trying to destroy a non existent texture data: %s", id.c_str());
		}
	}
}

void TextureDataPool::clear(){
	if (asyncLoading){
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
