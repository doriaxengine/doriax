// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Texture.h"

#include "render/SystemRender.h"

using namespace doriax;

namespace {
    // owns the face data, a project switch frees the Texture before the render queue runs
    struct TextureFaceCleanup{
        std::shared_ptr<std::array<TextureData,6>> data;
        size_t face;
    };

    void cleanupTextureFace(void* cleanupData){
        TextureFaceCleanup* cleanup = static_cast<TextureFaceCleanup*>(cleanupData);
        TextureData::cleanupTexture(&cleanup->data->at(cleanup->face));
        delete cleanup;
    }
}

Texture::Texture(){
    this->render = NULL;
    this->framebuffer = NULL;
    this->alphaBorder = TextureAlphaBorder::AUTO;
    this->alphaBorderAuto = false;

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
    this->svgScale = 1.0f;
}

Texture::Texture(const std::string& path){
    this->render = NULL;
    this->framebuffer = NULL;
    this->alphaBorder = TextureAlphaBorder::AUTO;
    this->alphaBorderAuto = false;

    // Legacy form "<path>?svgScale=N" is absorbed into the svgScale field so paths[] always
    // holds a real file path.
    this->svgScale = 1.0f;
    this->paths[0] = TextureData::parseSvgScalePath(path, &this->svgScale);

    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = true;
    this->releaseDataAfterLoad = true;
    this->needLoad = true;

    this->id = buildPathTextureId();

    this->minFilter = TextureFilter::LINEAR;
    this->magFilter = TextureFilter::LINEAR;
    this->wrapU = TextureWrap::REPEAT;
    this->wrapV = TextureWrap::REPEAT;
}

Texture::Texture(const std::string& id, TextureData data){
    this->render = NULL;
    this->framebuffer = NULL;
    this->alphaBorder = TextureAlphaBorder::AUTO;
    this->alphaBorderAuto = false;

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
    this->svgScale = 1.0f;
}

Texture::Texture(Framebuffer* framebuffer){
    this->render = NULL;
    this->framebuffer = framebuffer;
    this->alphaBorder = TextureAlphaBorder::AUTO;
    this->alphaBorderAuto = false;

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
    this->svgScale = 1.0f;
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
    svgScale = rhs.svgScale;
    alphaBorder = rhs.alphaBorder;
    alphaBorderAuto = rhs.alphaBorderAuto;
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
        svgScale = rhs.svgScale;
        alphaBorder = rhs.alphaBorder;
        alphaBorderAuto = rhs.alphaBorderAuto;
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
        wrapV == rhs.wrapV &&
        svgScale == rhs.svgScale &&
        alphaBorder == rhs.alphaBorder
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
        wrapV != rhs.wrapV ||
        svgScale != rhs.svgScale ||
        alphaBorder != rhs.alphaBorder
    );
}

Texture::~Texture(){

}

void Texture::setPath(const std::string& path){
    destroy();

    // Legacy form "<path>?svgScale=N" is absorbed into the svgScale field so paths[] always
    // holds a real file path. A new path resets the scale of the previous source.
    this->svgScale = 1.0f;
    this->paths[0] = TextureData::parseSvgScalePath(path, &this->svgScale);
    this->framebuffer = NULL;
    this->type = TextureType::TEXTURE_2D;
    this->numFaces = 1;
    this->loadFromPath = true;
    this->releaseDataAfterLoad = true;
    this->needLoad = true;

    this->id = buildPathTextureId();
    this->render = TexturePool::get(getRenderId());
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
    this->svgScale = 1.0f;

    this->data = std::make_shared<std::array<TextureData,6>>();
    this->data->at(0) = data;
    this->data = TextureDataPool::get(id, *this->data.get());

    this->render = TexturePool::get(getRenderId());
}

void Texture::setId(const std::string& id){
    this->id = id;
    if (!id.empty()) {
        this->render = TexturePool::get(getRenderId());
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
    this->svgScale = 1.0f;

    this->id = "cube|" + path;
    this->render = TexturePool::get(getRenderId());
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
    this->svgScale = 1.0f;

    this->id = buildCubeTextureId(paths);
    this->render = TexturePool::get(getRenderId());
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
    this->svgScale = 1.0f;

    this->id = buildCubeTextureId(paths);
    this->render = TexturePool::get(getRenderId());
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
    this->svgScale = 1.0f;

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

    this->render = TexturePool::get(getRenderId());
}

void Texture::setFramebuffer(Framebuffer* framebuffer){
    destroy();

    this->framebuffer = framebuffer;
    this->id.clear();
    this->numFaces = 1;
    this->loadFromPath = false;
    this->releaseDataAfterLoad = false;
    this->needLoad = false;
    this->svgScale = 1.0f;
}

// Returns true if every required face of the texture data still owns its
// CPU pixel buffer. After `releaseDataAfterLoad` the pixels are freed but
// the TextureData object (with its width/height/size) survives, so the
// shared_ptr alone is not enough to know whether the data is usable for a
// GPU upload.
bool Texture::hasTexturePixels(const std::shared_ptr<std::array<TextureData,6>>& data, size_t numFaces) {
    if (!data) return false;
    for (size_t f = 0; f < numFaces; f++) {
        TextureData& face = data->at(f);
        if (!face.getData() || face.getSize() == 0) {
            return false;
        }
    }
    return true;
}

std::string Texture::buildCubeTextureId(const std::string paths[6]) {
    std::string id = "cube";
    for (int f = 0; f < 6; f++) {
        id += "|" + paths[f];
    }
    return id;
}

// The rasterization scale is part of the texture identity: the same SVG at two scales must
// produce two pool entries. Non-SVG sources keep the plain path as id, and scale 1.0 maps
// to the plain path too, so ids stay identical to the pre-svgScale-field format.
std::string Texture::buildPathTextureId() const {
    std::string pathId = TextureData::hasSvgExtension(paths[0].c_str()) ? TextureData::buildSvgScalePath(paths[0], svgScale) : paths[0];
    // a file loaded with and without the fix is two pool entries
    if (isAlphaBorderFixed()) {
        pathId += "|alphaborder";
    }
    return pathId;
}

TextureLoadResult Texture::load() {
    TextureLoadResult result;
    result.id = id;

    if (framebuffer) {
        result.state = ResourceLoadState::Finished;
        result.data = nullptr; // Or however you want to signal framebuffer
        return result;
    }

    // Refresh from the data pool if we don't have a local reference yet, skipping
    // a released entry that other Texture copies keep alive while it is replaced.
    if (!data) {
        std::shared_ptr<std::array<TextureData,6>> pooledData = TextureDataPool::get(id);
        if (hasTexturePixels(pooledData, numFaces)) {
            data = pooledData;
        }
    }

    if (data && hasTexturePixels(data, numFaces)) {
        result.state = ResourceLoadState::Finished;
        result.data = data;
        return result;
    }

    // Pixels were released after a previous GPU upload. Drop only this reference,
    // loadFromFile replaces the pool entry. Calling TextureDataPool::remove() here
    // would instead discard the pending reload and restart it every frame.
    if (data) {
        data.reset();
        if (loadFromPath) {
            needLoad = true;
        }
    }

    if (!needLoad) {
        result.state = ResourceLoadState::NotStarted;
        return result;
    }

    if (loadFromPath) {
        // The loader receives the scale re-encoded as the "?svgScale=N" suffix it already
        // parses; paths[] itself stays a clean filesystem path.
        std::array<std::string, 6> aPaths;
        for (int f = 0; f < 6; f++) {
            aPaths[f] = TextureData::hasSvgExtension(paths[f].c_str())
                ? TextureData::buildSvgScalePath(paths[f], svgScale)
                : paths[f];
        }
        result = TextureDataPool::loadFromFile(id, aPaths, numFaces, isAlphaBorderFixed());
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

void Texture::retryLoad(){
    if (loadFromPath && !framebuffer)
        needLoad = true;
}

void Texture::destroy(){
    if (!id.empty()){

        if (render) {
            render.reset();
            TexturePool::remove(getRenderId());
        }

        if (data) {
            // remove() works on the pool's current copy, which may no longer be this one
            const bool pooled = data == TextureDataPool::get(id);
            data.reset();
            if (pooled) {
                TextureDataPool::remove(id);
            }
        }

        if (!framebuffer){
            needLoad = true;
        }
    }
}

void Texture::invalidateRender(){
    if (!id.empty() && render) {
        render.reset();
        // remove() would keep the cached (stale) texture alive while any other Texture copy
        // still references it — undo-history copies alone guarantee that in the editor — so
        // mark it stale and let the next getRender() with data rebuild it in-place.
        TexturePool::invalidate(id);
    }
}

TextureRender* Texture::getRender(TextureRender* fallBackTexture){
    if (framebuffer){
        lastFramebufferVersion = framebuffer->getVersion();
        return &framebuffer->getRender().getColorTexture();
    }

    // Fast path: GPU texture already created and cached.
    render = TexturePool::get(getRenderId());
    if (render){
        return render.get();
    }

    TextureLoadResult texResult = load();
    if (texResult.state == ResourceLoadState::Loading){
        render = std::make_shared<TextureRender>();
        return render.get();
    }

    if (texResult.state != ResourceLoadState::Finished || !hasTexturePixels(data, numFaces)){
        return fallBackTexture;
    }

    if (!id.empty()){
        render = TexturePool::get(getRenderId(), type, data, minFilter, magFilter, wrapU, wrapV);
    }

    if (render && releaseDataAfterLoad && data){
        for (int f = 0; f < numFaces; f++){
            SystemRender::scheduleCleanup(cleanupTextureFace, new TextureFaceCleanup{data, (size_t)f});
        }
    }

    if (render){
        return render.get();
    }

    return fallBackTexture;
}

std::string Texture::getPath(size_t index) const{
    return paths[index];
}

TextureData& Texture::getData(size_t index) const{
    return data->at(index);
}

bool Texture::hasData() const{
    return data != nullptr;
}

std::string Texture::getId() const{
    return id;
}

std::string Texture::getRenderId() const{
    return TexturePool::getRenderId(id, minFilter, magFilter, wrapU, wrapV);
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
    if (data){
        return static_cast<unsigned int>(data->at(0).getOriginalWidth());
    }
    std::shared_ptr<std::array<TextureData,6>> sharedData = id.empty() ? nullptr : TextureDataPool::get(id);
    if (!sharedData){
        return 0;
    }
    return static_cast<unsigned int>(sharedData->at(0).getOriginalWidth());
}

unsigned int Texture::getHeight() const{
    if (this->framebuffer){
        return framebuffer->getHeight();
    }
    if (data){
        return static_cast<unsigned int>(data->at(0).getOriginalHeight());
    }
    std::shared_ptr<std::array<TextureData,6>> sharedData = id.empty() ? nullptr : TextureDataPool::get(id);
    if (!sharedData){
        return 0;
    }
    return static_cast<unsigned int>(sharedData->at(0).getOriginalHeight());
}

bool Texture::isTransparent() const{
    if (!data){
        // a copy that took its render from the pool has no data
        return render && render->isTransparent();
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

Framebuffer* Texture::getFramebuffer() const{
    return framebuffer;
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

void Texture::setSvgScale(float scale){
    if (!(scale > 0.0f)){
        scale = 1.0f;
    }
    if (scale == svgScale){
        return;
    }

    // The scale is part of the texture identity for SVG path sources, so changing it means
    // pointing at a different pool entry: release the old one and reload, like setPath().
    if (loadFromPath && TextureData::hasSvgExtension(paths[0].c_str())){
        destroy();
        svgScale = scale;
        id = buildPathTextureId();
        needLoad = true;
        render = TexturePool::get(getRenderId());
    }else{
        svgScale = scale;
    }
}

float Texture::getSvgScale() const{
    return svgScale;
}

void Texture::setAlphaBorder(TextureAlphaBorder alphaBorder){
    bool wasFixed = isAlphaBorderFixed();
    this->alphaBorder = alphaBorder;
    reloadAlphaBorder(wasFixed);
}

TextureAlphaBorder Texture::getAlphaBorder() const{
    return alphaBorder;
}

void Texture::setAlphaBorderAuto(bool fix){
    bool wasFixed = isAlphaBorderFixed();
    alphaBorderAuto = fix;
    reloadAlphaBorder(wasFixed);
}

bool Texture::isAlphaBorderFixed() const{
    return alphaBorder == TextureAlphaBorder::FIX || (alphaBorder == TextureAlphaBorder::AUTO && alphaBorderAuto);
}

// Part of the texture identity for file sources, so a change points at another pool entry
void Texture::reloadAlphaBorder(bool wasFixed){
    if (wasFixed == isAlphaBorderFixed() || !loadFromPath || type != TextureType::TEXTURE_2D){
        return;
    }

    destroy();
    id = buildPathTextureId();
    needLoad = true;
    render = TexturePool::get(getRenderId());
}

TextureWrap Texture::getWrapV() const{
    if (framebuffer){
        return framebuffer->getWrapV();
    }
    return wrapV;
}
