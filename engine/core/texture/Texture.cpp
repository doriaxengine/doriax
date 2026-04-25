//
// (c) 2026 Eduardo Doria.
//

#include "Texture.h"

#include "Engine.h"
#include "Log.h"
#include "render/SystemRender.h"

using namespace doriax;

Texture::Texture(){
    this->render = NULL;
    this->framebuffer = NULL;

    this->id = "";
    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = false;
    this->releaseDataAfterLoad = true;
    this->needLoad = false;

    this->minFilter = TextureFilter::LINEAR;
    this->magFilter = TextureFilter::LINEAR;
    this->wrapU = TextureWrap::REPEAT;
    this->wrapV = TextureWrap::REPEAT;
}

Texture::Texture(const std::string& path){
    this->render = NULL;
    this->framebuffer = NULL;

    this->paths[0] = path;

    this->id = path;
    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = true;
    this->releaseDataAfterLoad = true;
    this->needLoad = true;

    this->minFilter = TextureFilter::LINEAR;
    this->magFilter = TextureFilter::LINEAR;
    this->wrapU = TextureWrap::REPEAT;
    this->wrapV = TextureWrap::REPEAT;
}

Texture::Texture(const std::string& id, TextureData data){
    this->render = NULL;
    this->framebuffer = NULL;

    this->id = id;
    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = false;
    this->releaseDataAfterLoad = false;
    this->needLoad = true;

    this->data = std::make_shared<std::array<TextureData,6>>();
    this->data->at(0) = data;
    this->data = TextureDataPool::get(id, *this->data.get());

    this->minFilter = TextureFilter::LINEAR;
    this->magFilter = TextureFilter::LINEAR;
    this->wrapU = TextureWrap::REPEAT;
    this->wrapV = TextureWrap::REPEAT;
}

Texture::Texture(Framebuffer* framebuffer){
    this->render = NULL;
    this->framebuffer = framebuffer;

    this->render = NULL;
    this->framebuffer = framebuffer;

    this->id = "";
    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = false;
    this->releaseDataAfterLoad = true;
    this->needLoad = false;

    this->minFilter = TextureFilter::LINEAR;
    this->magFilter = TextureFilter::LINEAR;
    this->wrapU = TextureWrap::REPEAT;
    this->wrapV = TextureWrap::REPEAT;
}

Texture::Texture(const Texture& rhs){
    render = rhs.render;
    framebuffer = rhs.framebuffer;
    type = rhs.type;
    id = rhs.id;
    for (int i = 0; i < 6; i++){
        paths[i] = rhs.paths[i];
    }
    data = rhs.data;
    numFaces = rhs.numFaces;
    loadFromPath = rhs.loadFromPath;
    releaseDataAfterLoad = rhs.releaseDataAfterLoad;
    needLoad = rhs.needLoad;
    minFilter = rhs.minFilter;
    magFilter = rhs.magFilter;
    wrapU = rhs.wrapU;
    wrapV = rhs.wrapV;
}

Texture& Texture::operator=(const Texture& rhs){
    if (this != &rhs) {
        render = rhs.render;
        framebuffer = rhs.framebuffer;
        type = rhs.type;
        id = rhs.id;
        for (int i = 0; i < 6; i++){
            paths[i] = rhs.paths[i];
        }
        data = rhs.data;
        numFaces = rhs.numFaces;
        loadFromPath = rhs.loadFromPath;
        releaseDataAfterLoad = rhs.releaseDataAfterLoad;
        needLoad = rhs.needLoad;
        minFilter = rhs.minFilter;
        magFilter = rhs.magFilter;
        wrapU = rhs.wrapU;
        wrapV = rhs.wrapV;
    }

    return *this; 
}

bool Texture::operator == ( const Texture& rhs ) const{
    return (
        framebuffer == rhs.framebuffer &&
        type == rhs.type &&
        id == rhs.id &&
        paths[0] == rhs.paths[0] &&
        paths[1] == rhs.paths[1] &&
        paths[2] == rhs.paths[2] &&
        paths[3] == rhs.paths[3] &&
        paths[4] == rhs.paths[4] &&
        paths[5] == rhs.paths[5] &&
        numFaces == rhs.numFaces &&
        loadFromPath == rhs.loadFromPath &&
        releaseDataAfterLoad == rhs.releaseDataAfterLoad &&
        minFilter == rhs.minFilter &&
        magFilter == rhs.magFilter &&
        wrapU == rhs.wrapU &&
        wrapV == rhs.wrapV
     );
}

bool Texture::operator != ( const Texture& rhs ) const{
    return (
        framebuffer != rhs.framebuffer ||
        type != rhs.type ||
        id != rhs.id ||
        paths[0] != rhs.paths[0] ||
        paths[1] != rhs.paths[1] ||
        paths[2] != rhs.paths[2] ||
        paths[3] != rhs.paths[3] ||
        paths[4] != rhs.paths[4] ||
        paths[5] != rhs.paths[5] ||
        numFaces != rhs.numFaces ||
        loadFromPath != rhs.loadFromPath ||
        releaseDataAfterLoad != rhs.releaseDataAfterLoad ||
        minFilter != rhs.minFilter ||
        magFilter != rhs.magFilter ||
        wrapU != rhs.wrapU ||
        wrapV != rhs.wrapV
    );
}

Texture::~Texture(){

}

void Texture::setPath(const std::string& path){
    destroy();

    this->paths[0] = path;
    this->id = path;
    this->framebuffer = NULL;
    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = true;
    this->releaseDataAfterLoad = true;
    this->needLoad = true;

    this->render = TexturePool::get(id);
}

void Texture::setData(const std::string& id, TextureData data){
    destroy();

    this->id = id;
    this->framebuffer = NULL;
    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = false;
    this->releaseDataAfterLoad = false;
    this->needLoad = true;

    this->data = std::make_shared<std::array<TextureData,6>>();
    this->data->at(0) = data;
    this->data = TextureDataPool::get(id, *this->data.get());

    this->render = TexturePool::get(id);
}

void Texture::setId(const std::string& id){
    this->id = id;
    if (!id.empty()) {
        this->render = TexturePool::get(id);
        this->data = TextureDataPool::get(id);
    }
}

void Texture::setCubeMap(const std::string& path){
    destroy();

    this->paths[0] = path;

    // Single-file cubemap uses only paths[0]. Clear previous per-face paths
    // so TextureDataPool can correctly detect the single-file cubemap case.
    for (int f = 1; f < 6; f++){
        this->paths[f].clear();
    }

    this->framebuffer = NULL;
    this->type = TextureType::TEXTURE_CUBE;
    this->numFaces = 6;
    this->loadFromPath = true;
    this->releaseDataAfterLoad = true;
    this->needLoad = true;

    this->id = "cube|" + path;
    this->render = TexturePool::get(id);
}

void Texture::setCubePath(size_t index, const std::string& path){
    this->paths[index] = path;

    // Allow setting faces one-by-one. Only when all faces are set we rebuild the id and
    // invalidate cached GPU/data resources.
    for (int f = 0; f < 6; f++){
        if (this->paths[f].empty()){
            return;
        }
    }

    destroy();

    this->framebuffer = NULL;
    this->type = TextureType::TEXTURE_CUBE;
    this->numFaces = 6;
    this->loadFromPath = true;
    this->releaseDataAfterLoad = true;
    this->needLoad = true;

    std::string id = "cube";
    for (int f = 0; f < 6; f++){
        id = id + "|" + paths[f];
    }
    this->id = id;
    this->render = TexturePool::get(id);
}

void Texture::setCubePaths(const std::string& front, const std::string& back,
    const std::string& left, const std::string& right,
    const std::string& up, const std::string& down){

    destroy();

    // OpenGL-style cubemap naming:
    // Front = +Z (face index 4)
    // Back  = -Z (face index 5)
    this->paths[4] = front;
    this->paths[5] = back;
    this->paths[1] = left;
    this->paths[0] = right;
    this->paths[2] = up;
    this->paths[3] = down;

    this->framebuffer = NULL;
    this->type = TextureType::TEXTURE_CUBE;
    this->numFaces = 6;
    this->loadFromPath = true;
    this->releaseDataAfterLoad = true;
    this->needLoad = true;

    std::string id = "cube";
    for (int f = 0; f < 6; f++){
        id = id + "|" + paths[f];
    }
    this->id = id;
    this->render = TexturePool::get(id);
}

void Texture::setCubeDatas(const std::string& id, TextureData front, TextureData back, TextureData left, TextureData right, TextureData up, TextureData down){
    destroy();

    this->id = id;
    this->framebuffer = NULL;
    this->type = TextureType::TEXTURE_CUBE;
    this->numFaces = 6;
    this->loadFromPath = false;
    this->releaseDataAfterLoad = false;
    this->needLoad = true;

    this->data = std::make_shared<std::array<TextureData,6>>();
    // OpenGL-style cubemap naming:
    // Front = +Z (face index 4)
    // Back  = -Z (face index 5)
    this->data->at(4) = front;
    this->data->at(5) = back;
    this->data->at(1) = left;
    this->data->at(0) = right;
    this->data->at(2) = up;
    this->data->at(3) = down;
    this->data = TextureDataPool::get(id, *this->data.get());

    this->render = TexturePool::get(id);
}

void Texture::setFramebuffer(Framebuffer* framebuffer){
    destroy();

    this->framebuffer = framebuffer;
    this->id.clear();
    this->numFaces = 1;
    this->loadFromPath = false;
    this->releaseDataAfterLoad = false;
    this->needLoad = false;
}

TextureLoadResult Texture::load() {
    TextureLoadResult result;
    result.id = id;

    if (framebuffer) {
        result.state = ResourceLoadState::Finished;
        result.data = nullptr; // Or however you want to signal framebuffer
        return result;
    }

    if (data) {
        result.state = ResourceLoadState::Finished;
        result.data = data;
        return result;
    } else {
        data = TextureDataPool::get(id);
        if (data) {
            result.state = ResourceLoadState::Finished;
            result.data = data;
            return result;
        }
    }

    if (!needLoad) {
        result.state = ResourceLoadState::NotStarted;
        return result;
    }

    if (loadFromPath) {
        std::array<std::string, 6> aPaths = {paths[0], paths[1], paths[2], paths[3], paths[4], paths[5]};
        result = TextureDataPool::loadFromFile(id, aPaths, numFaces);
        if (result && result.data) {
            data = result.data;
            needLoad = false;
        } else if (result.state == ResourceLoadState::Failed) {
            needLoad = false;
        }
        return result;
    }

    result.state = ResourceLoadState::NotStarted;
    return result;
}

void Texture::destroy(){
    if (!id.empty()){

        if (render) {
            render.reset();
            TexturePool::remove(id);
        }

        if (data) {
            data.reset();
            TextureDataPool::remove(id);
        }

        if (!framebuffer){
            needLoad = true;
        }
    }
}

void Texture::invalidateRender(){
    if (!id.empty() && render) {
        render.reset();
        TexturePool::remove(id);
    }
}

TextureRender* Texture::getRender(TextureRender* fallBackTexture){
    if (framebuffer){
        lastFramebufferVersion = framebuffer->getVersion();
        return &framebuffer->getRender().getColorTexture();
    }

    render = TexturePool::get(id);

    if (render){
        data = TextureDataPool::get(id);
        return render.get();
    }

    TextureLoadResult texResult = load();
    if (texResult.state == ResourceLoadState::Failed){
        return fallBackTexture;
    }

    if (!id.empty()){
        render = TexturePool::get(id, type, data, minFilter, magFilter, wrapU, wrapV);
    }

    if (data && releaseDataAfterLoad){
        for (int f = 0; f < numFaces; f++){
            SystemRender::scheduleCleanup(TextureData::cleanupTexture, &data->at(f));
        }
    }

    if (render){
        return render.get();
    }

    return NULL;
}

std::string Texture::getPath(size_t index) const{
    return paths[index];
}

TextureData& Texture::getData(size_t index) const{
    return data->at(index);
}

std::string Texture::getId() const{
    return id;
}

size_t Texture::getNumFaces() const{
    return numFaces;
}

TextureType Texture::getType() const{
    return type;
}

bool Texture::isCubeMap() const{
    return type == TextureType::TEXTURE_CUBE;
}

unsigned int Texture::getWidth() const{
    if (this->framebuffer){
        return framebuffer->getWidth();
    }
    if (!data){
        return 0;
    }
    return static_cast<unsigned int>(getData().getOriginalWidth());
}

unsigned int Texture::getHeight() const{
    if (this->framebuffer){
        return framebuffer->getHeight();
    }
    if (!data){
        return 0;
    }
    return static_cast<unsigned int>(getData().getOriginalHeight());
}

bool Texture::isTransparent() const{
    if (!data){
        return false;
    }
    return getData().isTransparent();
}

void Texture::setReleaseDataAfterLoad(bool releaseDataAfterLoad){
    this->releaseDataAfterLoad = releaseDataAfterLoad;
}

bool Texture::isReleaseDataAfterLoad() const{
    return this->releaseDataAfterLoad;
}

void Texture::releaseData(){
    for (int f = 0; f < numFaces; f++){
        data->at(f).releaseImageData();
    }
}

bool Texture::empty() const{
    if (!needLoad && !data && !render && !framebuffer)
        return true;

    return false;
}

bool Texture::isFramebuffer() const{
    if (framebuffer)
        return true;

    return false;
}

bool Texture::isFramebufferOutdated() const{
    if (framebuffer){
        return (lastFramebufferVersion != framebuffer->getVersion());
    }

    return false;
}

void Texture::setMinFilter(TextureFilter filter){
    if (framebuffer){
        framebuffer->setMinFilter(filter);
    }
    minFilter = filter;
}

TextureFilter Texture::getMinFilter() const{
    if (framebuffer){
        return framebuffer->getMinFilter();
    }
    return minFilter;
}

void Texture::setMagFilter(TextureFilter filter){
    if (framebuffer){
        framebuffer->setMagFilter(filter);
    }
    magFilter = filter;
}

TextureFilter Texture::getMagFilter() const{
    if (framebuffer){
        return framebuffer->getMagFilter();
    }
    return magFilter;
}

void Texture::setWrapU(TextureWrap wrapU){
    if (framebuffer){
        framebuffer->setWrapU(wrapU);
    }
    this->wrapU = wrapU;
}

TextureWrap Texture::getWrapU() const{
    if (framebuffer){
        return framebuffer->getWrapU();
    }
    return wrapU;
}

void Texture::setWrapV(TextureWrap wrapV){
    if (framebuffer){
        framebuffer->setWrapV(wrapV);
    }
    this->wrapV = wrapV;
}

TextureWrap Texture::getWrapV() const{
    if (framebuffer){
        return framebuffer->getWrapV();
    }
    return wrapV;
}