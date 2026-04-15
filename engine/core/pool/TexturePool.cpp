//
// (c) 2026 Eduardo Doria.
//

#include "TexturePool.h"

#include "Engine.h"
#include "Log.h"

using namespace doriax;

textures_t& TexturePool::getMap(){
    //To prevent similar problem of static init fiasco but on deinitialization
    //https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
    static textures_t* map = new textures_t();
    return *map;
};

std::shared_ptr<TextureRender> TexturePool::get(const std::string& id){
	auto& shared = getMap()[id];

    if (shared && shared->isCreated()){
        return shared;
    }

	return NULL;
}

std::shared_ptr<TextureRender> TexturePool::get(const std::string& id, TextureType type, const std::shared_ptr<std::array<TextureData,6>> &data, TextureFilter minFilter, TextureFilter magFilter, TextureWrap wrapU, TextureWrap wrapV){
	auto& shared = getMap()[id];

    if (shared && shared->isCreated()){
        return shared;
    }

    if (!shared) {
        shared = std::make_shared<TextureRender>();
    }

	int numFaces = 1;
	if (type == TextureType::TEXTURE_CUBE){
		numFaces = 6;
	}

	if (data){
		void* data_array[6];
		size_t size_array[6];

		for (int f = 0; f < numFaces; f++){
			data_array[f] = data->at(f).getData();
			size_array[f] = (size_t)data->at(f).getSize();
		}

		shared->createTexture(id, data->at(0).getWidth(), data->at(0).getHeight(), data->at(0).getColorFormat(), type, numFaces, data_array, size_array, minFilter, magFilter, wrapU, wrapV);
		//Log::debug("Create texture %s", id.c_str());
	}

	return shared;
}

void TexturePool::remove(const std::string& id){
	if (getMap().count(id)){
		auto& shared = getMap()[id];
		if (shared.use_count() <= 1){
			shared->destroyTexture();
			//Log::debug("Remove texture %s", id.c_str());
			getMap().erase(id);
		}
	}else{
		if (Engine::isViewLoaded()){
			Log::debug("Trying to destroy a non existent texture: %s", id.c_str());
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
}
