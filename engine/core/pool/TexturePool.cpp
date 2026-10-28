// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TexturePool.h"

#include "Engine.h"
#include "Log.h"

#include <vector>

using namespace doriax;

textures_t& TexturePool::getMap(){
    //To prevent similar problem of static init fiasco but on deinitialization
    //https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
    static textures_t* map = new textures_t();
    return *map;
};

// Ids marked by invalidate() whose backend texture must be rebuilt. The stale texture is
// kept alive (holders may still bind it) until get() with fresh data swaps it in-place.
std::set<std::string>& TexturePool::getInvalidated(){
    static std::set<std::string>* invalidated = new std::set<std::string>();
    return *invalidated;
};

std::string TexturePool::getRenderId(const std::string& id, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){
	if (id.empty() || (minFilter == TextureFilter::LINEAR && magFilter == TextureFilter::LINEAR &&
	                   wrapU == TextureWrap::REPEAT && wrapV == TextureWrap::REPEAT)){
		return id;
	}
	return id + "|sampler=" + std::to_string((int)minFilter) + std::to_string((int)magFilter) +
	       std::to_string((int)wrapU) + std::to_string((int)wrapV);
}

std::shared_ptr<TextureRender> TexturePool::get(const std::string& id){
	auto& map = getMap();
	auto it = map.find(id);

    if (it != map.end() && it->second && it->second->isCreated() && getInvalidated().count(id) == 0){
        return it->second;
    }

	return NULL;
}

std::shared_ptr<TextureRender> TexturePool::get(const std::string& id, TextureType type, const std::shared_ptr<std::array<TextureData,6>> &data, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){
	auto& map = getMap();
	auto it = map.find(id);
	std::shared_ptr<TextureRender> shared = (it != map.end()) ? it->second : nullptr;

	bool stale = getInvalidated().count(id) > 0;

	if (shared && shared->isCreated() && !stale){
		return shared;
	}

	if (!data){
		return NULL;
	}

	int numFaces = (type == TextureType::TEXTURE_CUBE) ? 6 : 1;

	void* data_array[6];
	size_t size_array[6];

	for (int f = 0; f < numFaces; f++){
		data_array[f] = data->at(f).getData();
		size_array[f] = (size_t)data->at(f).getSize();
		if (!data_array[f] || size_array[f] == 0){
			// Pixel data was released or never populated. Skip the upload so we
			// don't trigger Sokol's VALIDATE_IMAGEDATA_NODATA. The caller will
			// fall back and Texture::load() will refresh the data on a later try.
			Log::error("Texture data is empty: %s", id.c_str());
			return NULL;
		}
	}

	if (!shared){
		shared = std::make_shared<TextureRender>();
	}

	// Stale entry with fresh data: swap the backend texture now, inside a single call with
	// no draw in between, so holders never bind a destroyed texture.
	if (stale && shared->isCreated()){
		shared->destroyTexture();
	}

	if (!shared->createTexture(id, data->at(0).getWidth(), data->at(0).getHeight(), data->at(0).getColorFormat(), type, numFaces, data_array, size_array, minFilter, magFilter, wrapU, wrapV)){
		shared->destroyTexture();
		if (it != map.end()){
			map.erase(it);
		}
		return NULL;
	}
	//Log::debug("Create texture %s", id.c_str());
	shared->setTransparent(data->at(0).isTransparent());

	map[id] = shared;
	getInvalidated().erase(id);

	return shared;
}

void TexturePool::add(const std::string& id, std::shared_ptr<TextureRender> render){
	if (!id.empty() && render){
		getMap()[id] = render;
	}
}

void TexturePool::remove(const std::string& id){
	auto& map = getMap();
	auto it = map.find(id);
	if (it != map.end()){
		if (!it->second || it->second.use_count() <= 1){
			if (it->second){
				it->second->destroyTexture();
			}
			//Log::debug("Remove texture %s", id.c_str());
			map.erase(it);
			getInvalidated().erase(id);
		}
	}else{
		if (Engine::isViewLoaded()){
			Log::debug("Trying to destroy a non existent texture: %s", id.c_str());
		}
	}
}

// Unlike remove(), forces a rebuild even while other Texture instances still reference the
// pooled TextureRender. The backend texture is NOT destroyed here: reloading can take
// frames (async file load / SVG rasterization) and holders keep binding the old texture
// meanwhile, so it must stay alive. The id is only marked stale — get() then reports a miss
// so callers reload, and get() with data destroys and recreates the texture in-place inside
// the same shared object, letting every holder pick up the rebuilt texture. Unreferenced
// entries are destroyed and erased immediately, like remove().
void TexturePool::invalidate(const std::string& id){
	auto& map = getMap();

	std::vector<std::string> keys = {id};
	const std::string prefix = id + "|sampler=";
	for (auto it = map.lower_bound(prefix); it != map.end() && it->first.rfind(prefix, 0) == 0; ++it){
		keys.push_back(it->first);
	}

	for (const std::string& key : keys){
		auto it = map.find(key);
		if (it == map.end()){
			continue;
		}

		if (!it->second || it->second.use_count() <= 1){
			if (it->second){
				it->second->destroyTexture();
			}
			map.erase(it);
		}else{
			getInvalidated().insert(key);
		}
	}
}

void TexturePool::clear(){
	for (auto& it: getMap()) {
		if (it.second)
			it.second->destroyTexture();
	}
	//Log::debug("Remove all textures");
	getMap().clear();
	getInvalidated().clear();
}

void TexturePool::clearUnused(){
	std::vector<std::string> ids;
	ids.reserve(getMap().size());
	for (const auto& [id, _] : getMap()) {
		ids.push_back(id);
	}
	for (const auto& id : ids) {
		remove(id);
	}
}
