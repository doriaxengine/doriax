// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef RESIZE_WITH_STB
#define RESIZE_WITH_STB 0
#endif

#include "TextureData.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include "stb_image.h"
#if RESIZE_WITH_STB
#include "stb_image_resize2.h"
#endif
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#include "Log.h"
#include "Texture.h"

using namespace doriax;

bool TextureData::hasSvgExtension(const char* filename) {
    if (!filename) {
        return false;
    }

    std::string path(filename);
    std::string extension;
    const size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        extension = path.substr(dotPos);
    }
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".svg";
}

static const std::string SVG_SCALE_MARKER = "?svgScale=";

std::string TextureData::parseSvgScalePath(const std::string& path, float* outScale) {
    const size_t pos = path.find(SVG_SCALE_MARKER);
    if (pos == std::string::npos) {
        return path;
    }

    if (outScale) {
        try {
            *outScale = std::stof(path.substr(pos + SVG_SCALE_MARKER.size()));
        } catch (...) {
            // Malformed suffix: leave the caller's scale untouched.
        }
    }
    return path.substr(0, pos);
}

std::string TextureData::buildSvgScalePath(const std::string& cleanPath, float scale) {
    // No suffix for the default scale (or invalid values): keep the path clean.
    if (!(scale > 0.0f) || std::fabs(scale - 1.0f) < 1e-4f) {
        return cleanPath;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%g", scale);
    return cleanPath + SVG_SCALE_MARKER + buffer;
}

bool TextureData::looksLikeSvg(Data* filedata) {
    if (!filedata || !filedata->getMemPtr() || filedata->length() == 0) {
        return false;
    }

    const unsigned int inspectLength = std::min<unsigned int>(filedata->length(), 1024);
    unsigned char* mem = filedata->getMemPtr();

    // Skip an optional UTF-8 BOM so it doesn't hide the first real character.
    unsigned int start = 0;
    if (inspectLength >= 3 && mem[0] == 0xEF && mem[1] == 0xBB && mem[2] == 0xBF) {
        start = 3;
    }
    // Skip leading whitespace.
    while (start < inspectLength && std::isspace(mem[start])) {
        start++;
    }

    // A valid SVG/XML document always begins with '<' (e.g. "<?xml", "<!--",
    // "<!DOCTYPE", or "<svg"). Requiring this avoids misrouting a binary raster
    // image to the SVG decoder just because the bytes "<svg" appear in its metadata.
    if (start >= inspectLength || mem[start] != '<') {
        return false;
    }

    std::string header;
    header.reserve(inspectLength - start);
    for (unsigned int i = start; i < inspectLength; i++) {
        header.push_back(static_cast<char>(std::tolower(mem[i])));
    }

    return header.find("<svg") != std::string::npos;
}

TextureData::TextureData() {
    this->width = 0;
    this->height = 0;
    this->originalWidth = 0;
    this->originalHeight = 0;
    this->size = 0;
    this->color_format = ColorFormat::RGBA;
    this->channels = 0;
    this->data = NULL;

    this->transparent = false;

    this->dataOwned = false;

    this->svgScale = 1.0f;
}

TextureData::TextureData(int width, int height, unsigned int size, ColorFormat color_format, int channels, void* data){
    this->width = width;
    this->height = height;
    this->originalWidth = width;
    this->originalHeight = height;
    this->size = size;
    this->color_format = color_format;
    this->channels = channels;
    this->data = data;

    this->transparent = false;

    this->dataOwned = false;

    this->svgScale = 1.0f;
}

TextureData::TextureData(const char* filename) : TextureData(){
    loadTextureFromFile(filename);
}

TextureData::TextureData(unsigned char* data, unsigned int dataLength) : TextureData(){
    loadTextureFromMemory(data, dataLength);
}

TextureData::TextureData(const TextureData& v) : TextureData(){
    this->copy(v);
}

TextureData& TextureData::operator = ( const TextureData& v ){
    this->copy(v);

    return *this;
}

bool TextureData::operator == ( const TextureData& v ) const{
    return (
        v.width == width &&
        v.height == height &&
        v.originalWidth == originalWidth &&
        v.originalHeight == originalHeight &&
        v.size == size &&
        v.color_format == color_format &&
        v.channels == channels &&
        v.data == data &&
        v.transparent == transparent &&
        v.dataOwned == dataOwned
    );
}

bool TextureData::operator != ( const TextureData& v ) const{
    return (
        v.width != width ||
        v.height != height ||
        v.originalWidth != originalWidth ||
        v.originalHeight != originalHeight ||
        v.size != size ||
        v.color_format != color_format ||
        v.channels != channels ||
        v.data != data ||
        v.transparent != transparent ||
        v.dataOwned != dataOwned
    );
}

bool TextureData::loadTexture(Data* filedata) {
    filedata->seek(0);

    if (dataOwned && data)
        releaseImageData();

    if (looksLikeSvg(filedata)) {
        if (!loadSvgTexture(filedata)) {
            Log::error("Error loading SVG texture");
            return false;
        }
        return true;
    }
    
    //----- Start std_image read texture
    stbi_info_from_memory((stbi_uc const *)filedata->getMemPtr(), filedata->length(), &width, &height, &channels);

    //Renders (not GL) only support 8bpp and 32bpp
    int desired_channels = 1;
    color_format = ColorFormat::RED;

    if (channels != 1){
        desired_channels = 4;
        color_format = ColorFormat::RGBA;
    }

    // Single-channel 16-bit images (e.g. terrain heightmaps) are kept at full precision
    // as RED16. Multi-channel images are still downconverted to 8-bit RGBA.
    const bool is16Bit = desired_channels == 1 &&
                         stbi_is_16_bit_from_memory((stbi_uc const *)filedata->getMemPtr(), filedata->length());

    if (is16Bit){
        color_format = ColorFormat::RED16;
        data = stbi_load_16_from_memory((stbi_uc const *)filedata->getMemPtr(), filedata->length(), &width, &height, &channels, desired_channels);
    }else{
        data = stbi_load_from_memory((stbi_uc const *)filedata->getMemPtr(), filedata->length(), &width, &height, &channels, desired_channels);
    }

    if (!data){
        Log::error("Error loading texture: %s", stbi_failure_reason());
        return false;
    }

    channels = desired_channels;

    size = width * height * channels * (is16Bit ? 2 : 1); //in bytes
    //----- End std_image read texture

    originalWidth = width;
    originalHeight = height;

    transparent = hasAlpha();
    
    return true;
}

bool TextureData::loadSvgTexture(Data* filedata) {
    if (!filedata || !filedata->getMemPtr() || filedata->length() == 0) {
        return false;
    }

    // A non-positive or non-finite scale falls back to the SVG's intrinsic size.
    float scale = svgScale;
    if (!(scale > 0.0f) || !std::isfinite(scale)) {
        scale = 1.0f;
    }

    std::vector<char> svgData(filedata->length() + 1);
    memcpy(svgData.data(), filedata->getMemPtr(), filedata->length());
    svgData[filedata->length()] = '\0';

    NSVGimage* image = nsvgParse(svgData.data(), "px", 96.0f);
    if (!image || image->width <= 0.0f || image->height <= 0.0f) {
        if (image) {
            nsvgDelete(image);
        }
        return false;
    }

    // Cap the rasterized size to a GPU-friendly bound. This also prevents the
    // int cast below from overflowing when svgScale is unreasonably large.
    const float maxRasterDim = 16384.0f;
    const float scaledWidth = std::ceil(image->width * scale);
    const float scaledHeight = std::ceil(image->height * scale);
    if (scaledWidth > maxRasterDim || scaledHeight > maxRasterDim) {
        Log::error("SVG rasterization size too large (%.0f x %.0f) for scale %.2f", scaledWidth, scaledHeight, scale);
        nsvgDelete(image);
        return false;
    }

    const int rasterWidth = std::max(1, static_cast<int>(scaledWidth));
    const int rasterHeight = std::max(1, static_cast<int>(scaledHeight));
    const size_t rasterSize = static_cast<size_t>(rasterWidth) * static_cast<size_t>(rasterHeight) * 4;

    unsigned char* rasterData = static_cast<unsigned char*>(malloc(rasterSize));
    if (!rasterData) {
        nsvgDelete(image);
        return false;
    }
    memset(rasterData, 0, rasterSize);

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (!rasterizer) {
        free(rasterData);
        nsvgDelete(image);
        return false;
    }

    nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale, rasterData, rasterWidth, rasterHeight, rasterWidth * 4);

    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);

    data = rasterData;
    width = rasterWidth;
    height = rasterHeight;
    originalWidth = rasterWidth;
    originalHeight = rasterHeight;
    channels = 4;
    color_format = ColorFormat::RGBA;
    size = static_cast<unsigned int>(rasterSize);
    transparent = hasAlpha();

    return true;
}

bool TextureData::loadTextureFromFile(const char* filename) {
    // A texture path may carry a rasterization scale as "<path>?svgScale=<value>".
    // Split it off here: load from the clean path, and let an embedded scale
    // override the current svgScale setting.
    float parsedScale = svgScale;
    std::string cleanPath = parseSvgScalePath(filename ? filename : "", &parsedScale);
    svgScale = parsedScale;

    Data filedata;

    int res = filedata.open(cleanPath.c_str());

    if (res==FileErrors::FILE_NOT_FOUND){
        Log::error("Texture file not found: %s", cleanPath.c_str());
        return false;
    }
    if (res==FileErrors::INVALID_PARAMETER){
        Log::error("Texture file path is invalid: %s", cleanPath.c_str());
        return false;
    }

    bool result = false;
    if (hasSvgExtension(cleanPath.c_str())) {
        if (dataOwned && data)
            releaseImageData();
        result = loadSvgTexture(&filedata);
    } else {
        result = loadTexture(&filedata);
    }

    if (!result){
        Log::error("Texture file not loaded: %s", cleanPath.c_str());
    }

    return result;
}

bool TextureData::loadTextureFromMemory(unsigned char* data, unsigned int dataLength){
    Data filedata;

    int res = filedata.open(data, dataLength, false, false);

    bool result = loadTexture(&filedata);

    if (!result){
        Log::error("Texture from memory not loaded");
    }

    return result;
}

bool TextureData::loadCubeMapFromSingleFile(const char* filename, std::array<TextureData, 6>& data){
    TextureData texture;
    texture.setDataOwned(true);
    if (!texture.loadTextureFromFile(filename))
        return false;

    int tileW = 0;
    int tileH = 0;

    int right[2];
    int left[2];
    int top[2];
    int bottom[2];
    int front[2];
    int back[2];

    uint64_t ratio = texture.getWidth() * 100 / texture.getHeight();

    // Horizontal Cross
    if (ratio == 133){ // 4:3
        tileW = texture.getWidth() / 4;
        tileH = texture.getHeight() / 3;

        right[0] = 2; right[1] = 1;
        left[0] = 0; left[1] = 1;
        top[0] = 1; top[1] = 0;
        bottom[0] = 1; bottom[1] = 2;
        front[0] = 1; front[1] = 1;
        back[0] = 3; back[1] = 1;

    // Vertical Cross
    }else if (ratio == 75){ // 3:4
        tileW = texture.getWidth() / 3;
        tileH = texture.getHeight() / 4;

        right[0] = 2; right[1] = 1;
        left[0] = 0; left[1] = 1;
        top[0] = 1; top[1] = 0;
        bottom[0] = 1; bottom[1] = 2;
        front[0] = 1; front[1] = 1;
        back[0] = 1; back[1] = 3;
    }else{
        Log::error("Failed to load cubemap: format not supported");
        return false;
    }

    // Face indices follow the backend cubemap convention (sokol/OpenGL style):
    // 0=+X (Right), 1=-X (Left), 2=+Y (Top), 3=-Y (Bottom), 4=+Z, 5=-Z.
    // This loader uses OpenGL-style naming: Front = +Z (4), Back = -Z (5).

    // Extract each face into its own buffer: copying 'texture' per face and cropping
    // would duplicate the whole image six times.

    auto extractFace = [&](int faceIndex, int xTile, int yTile){
        const int xOffset = xTile * tileW;
        const int yOffset = yTile * tileH;

        unsigned char* src = (unsigned char*)texture.getData();
        const int srcW = texture.getWidth();
        const int srcChannels = texture.getChannels();
        const ColorFormat srcFormat = texture.getColorFormat();

        const int rowBytes = tileW * srcChannels;
        const int bufsize = tileW * tileH * srcChannels;
        unsigned char* faceData = (unsigned char*)malloc(bufsize * sizeof(unsigned char));
        if (!faceData)
            throw std::runtime_error("Out of memory while extracting cubemap face");

        for (int y = 0; y < tileH; y++){
            const size_t srcOffset = ((size_t)(yOffset + y) * (size_t)srcW + (size_t)xOffset) * (size_t)srcChannels;
            const size_t dstOffset = (size_t)y * (size_t)rowBytes;
            memcpy(faceData + dstOffset, src + srcOffset, (size_t)rowBytes);
        }

        data[faceIndex] = TextureData(tileW, tileH, (unsigned int)bufsize, srcFormat, srcChannels, faceData);
        data[faceIndex].setDataOwned(true);
    };

    // Using DORIAX convention:
    // Right (+X)
    extractFace(0, right[0], right[1]);
    // Left (-X)
    extractFace(1, left[0], left[1]);
    // Top (+Y)
    extractFace(2, top[0], top[1]);
    // Bottom (-Y)
    extractFace(3, bottom[0], bottom[1]);
    // Front (+Z)
    extractFace(4, front[0], front[1]);
    // Back (-Z)
    extractFace(5, back[0], back[1]);

    return true;
}

void TextureData::copy ( const TextureData& v ){
    if (this == &v)
        return;

    if (dataOwned && data != v.data)
        releaseImageData();

    this->width = v.width;
    this->height = v.height;
    this->originalWidth = v.originalWidth;
    this->originalHeight = v.originalHeight;
    this->size = v.size;
    this->color_format = v.color_format;
    this->channels = v.channels;

    this->dataOwned = v.dataOwned;

    this->transparent = v.transparent;

    this->svgScale = v.svgScale;

    this->data = v.data;

    // an owner's pixels can die with it, so its copy gets its own
    if (v.dataOwned && v.data){
        this->data = malloc(v.size);
        if (this->data)
            memcpy(this->data, v.data, v.size);
    }
}

TextureData::~TextureData() {
    if (dataOwned)
        releaseImageData();
}

void TextureData::releaseImageData(){
    if (data){
        stbi_image_free(data);
        data = NULL;
    }
}

int TextureData::getBytesPerChannel(ColorFormat format){
    return format == ColorFormat::RED16 ? 2 : 1;
}

int TextureData::getNearestPowerOfTwo(int size){
    int i;
    for(i = 31; i >= 0; i--)
        if ((size - 1) & (1<<i))
            break;
    return (1<<(i + 1));
}

bool TextureData::hasAlpha(){
    if (channels == 4){
        for(int y = 0; y < height; y++){
            for(int x = 0; x < width; x++){
                int pixel = (y * (width * channels)) + (x * channels);
                if (((unsigned char*)data)[pixel + 3] != 255)
                    return true;
            }
        }
    }
    return false;
}

void TextureData::crop(int xOffset, int yOffset, int newWidth, int newHeight){
    
    int rowsize = width * channels;
    int newRowsize = newWidth * channels;
    int bufsize = newWidth * newHeight * channels;
    
    unsigned char* newData = (unsigned char*) malloc(bufsize*sizeof(unsigned char));
    
    int row_cnt;
    long off1 = 0;
    long off2 = 0;
    long off3 = xOffset*channels;
    
    for (row_cnt=yOffset;row_cnt<yOffset+(newHeight);row_cnt++) {
        off1=row_cnt*rowsize;
        off2=(row_cnt-yOffset)*newRowsize;
        
        memcpy(newData+off2,(unsigned char*)data+off1+off3,newRowsize);
    }

    stbi_image_free(data);
    
    width = newWidth;
    height = newHeight;
    size = bufsize;
    data = newData;
}

void TextureData::resizePowerOfTwo(){
    resize(getNearestPowerOfTwo(width), getNearestPowerOfTwo(height));
}

void TextureData::resizeToSquare(){
    if (width != height){
        int side = std::max(width, height);
        resize(side, side);
    }
}

void TextureData::resize(int newWidth, int newHeight){

    if ((newWidth != width) || (newHeight != height)){

        // RED16 carries two bytes per channel, so a pixel is not always one byte per channel
        int pixelSize = channels * getBytesPerChannel(color_format);
        int bufsize = newWidth * newHeight * pixelSize;
        unsigned char* newData = (unsigned char*) malloc(bufsize*sizeof(unsigned char));

        #if RESIZE_WITH_STB

        stbir_pixel_layout pixel_layout = stbir_pixel_layout::STBIR_RGBA;
        if (channels == 1){
            pixel_layout = stbir_pixel_layout::STBIR_1CHANNEL;
        }else if (channels == 2){
            pixel_layout = stbir_pixel_layout::STBIR_2CHANNEL;
        }else if (channels == 3){
            pixel_layout = stbir_pixel_layout::STBIR_RGB;
        }else if (channels == 4){
            pixel_layout = stbir_pixel_layout::STBIR_RGBA;
        }

        stbir_resize((unsigned char*)data, width, height, 0,
	                   newData, newWidth, newHeight, 0, pixel_layout,
	                   (color_format == ColorFormat::RED16) ? STBIR_TYPE_UINT16 : STBIR_TYPE_UINT8,
	                   STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT);

        #else

        double scaleWidth =  (double)newWidth / (double)width;
        double scaleHeight = (double)newHeight / (double)height;

        for(int cy = 0; cy < newHeight; cy++){
            for(int cx = 0; cx < newWidth; cx++){
                
                int pixel = (cy * (newWidth*pixelSize)) + (cx*pixelSize);
                int nearestMatch =  (((int)(cy / scaleHeight) * (width*pixelSize)) + ((int)(cx / scaleWidth) * pixelSize) );
            
                for (int b = 0; b < pixelSize; b++){
                    newData[pixel + b] =  ((unsigned char*)data)[nearestMatch + b];
                }
            
            }
        }

        #endif

        stbi_image_free(data);
    
        width = newWidth;
        height = newHeight;
        size = bufsize;
        data = newData;
        
    }

}

void TextureData::fitPowerOfTwo(){
    fitSize(0, 0, getNearestPowerOfTwo(width), getNearestPowerOfTwo(height));
}

void TextureData::fitSize(int xOffset, int yOffset, int newWidth, int newHeight){
    
    if ((newWidth != width) || (newHeight != height)){

        int bufsize = newWidth * newHeight * channels;
        unsigned char* newData = (unsigned char*) malloc(bufsize*sizeof(unsigned char));
        
        for( unsigned int i = 0; i < bufsize; ++i )
        {
            newData[i] = 0;
        }
        
        for( unsigned int i = 0; i < height; ++i )
        {
            memcpy( (unsigned char*)newData + channels * ( newWidth * ( yOffset + i ) + xOffset ), (unsigned char*)data + width * i * channels, width * channels );
        }
        
        stbi_image_free(data);
        
        width = newWidth;
        height = newHeight;
        size = bufsize;
        data = newData;
        
    }
    
}

void TextureData::fixAlphaBorder(){
    // rings of transparent pixels colored around visible ones, enough for a few mip levels
    const int borderSize = 4;

    if (!data || channels != 4 || getBytesPerChannel(color_format) != 1)
        return;

    unsigned char* pixels = (unsigned char*)data;
    const int count = width * height;

    // 0 transparent, 1 colored, 2 queued in the next ring
    std::vector<unsigned char> state(count);
    bool hasTransparent = false;
    for (int i = 0; i < count; i++){
        state[i] = (pixels[i * 4 + 3] > 0) ? 1 : 0;
        if (state[i] == 0)
            hasTransparent = true;
    }
    if (!hasTransparent)
        return;

    std::vector<int> ring;
    std::vector<int> current;

    auto queueNeighbors = [&](int x, int y){
        for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ny++){
            for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); nx++){
                int j = ny * width + nx;
                if (state[j] == 0){
                    state[j] = 2;
                    ring.push_back(j);
                }
            }
        }
    };

    for (int y = 0; y < height; y++){
        for (int x = 0; x < width; x++){
            if (state[y * width + x] != 0)
                continue;
            bool touchesColor = false;
            for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1) && !touchesColor; ny++){
                for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); nx++){
                    if (state[ny * width + nx] == 1){
                        touchesColor = true;
                        break;
                    }
                }
            }
            if (touchesColor){
                state[y * width + x] = 2;
                ring.push_back(y * width + x);
            }
        }
    }

    for (int pass = 0; pass < borderSize && !ring.empty(); pass++){
        current.swap(ring);
        ring.clear();

        for (int i : current){
            int x = i % width;
            int y = i / width;
            int color[3] = {0, 0, 0};
            int n = 0;
            for (int ny = std::max(0, y - 1); ny <= std::min(height - 1, y + 1); ny++){
                for (int nx = std::max(0, x - 1); nx <= std::min(width - 1, x + 1); nx++){
                    int j = ny * width + nx;
                    if (state[j] == 1){
                        for (int c = 0; c < 3; c++)
                            color[c] += pixels[j * 4 + c];
                        n++;
                    }
                }
            }
            for (int c = 0; c < 3; c++)
                pixels[i * 4 + c] = (unsigned char)(color[c] / n);
        }

        for (int i : current){
            state[i] = 1;
        }
        for (int i : current){
            queueNeighbors(i % width, i / width);
        }
    }
}

void TextureData::flipVertical(){
    
    int bufsize = width * channels;
    
    unsigned char* tb1 = new unsigned char[bufsize];
    unsigned char* tb2 = new unsigned char[bufsize];
    
    int row_cnt;
    long off1 = 0;
    long off2 = 0;
    
    for (row_cnt=0;row_cnt<(height+1)/2;row_cnt++) {
        off1=row_cnt*bufsize;
        off2=((height-1)-row_cnt)*bufsize;
        
        memcpy(tb1,(unsigned char*)data+off1,bufsize);
        memcpy(tb2,(unsigned char*)data+off2,bufsize);
        memcpy((unsigned char*)data+off1,tb2,bufsize);
        memcpy((unsigned char*)data+off2,tb1,bufsize);
    }
    
    delete [] tb1;
    delete [] tb2;
    
}

unsigned char TextureData::getColorComponent(int x, int y, int color){
    return ((unsigned char*)data)[((x + y*width)*channels)+color];
}

void TextureData::setDataOwned(bool dataOwned){
    this->dataOwned = dataOwned;
}

bool TextureData::getDataOwned() const{
    return this->dataOwned;
}

void TextureData::setSVGScale(float svgScale){
    this->svgScale = svgScale;
}

float TextureData::getSVGScale() const{
    return this->svgScale;
}

int TextureData::getWidth(){
    return width;
}

int TextureData::getHeight(){
    return height;
}

int TextureData::getOriginalWidth(){
    return originalWidth;
}

int TextureData::getOriginalHeight(){
    return originalHeight;
}

unsigned int TextureData::getSize(){
    return size;
}

ColorFormat TextureData::getColorFormat(){
    return color_format;
}

int TextureData::getChannels(){
    return channels;
}

void* TextureData::getData(){
    return data;
}

bool TextureData::isTransparent(){
    return transparent;
}

int TextureData::getMinNearestPowerOfTwo(){
    return getNearestPowerOfTwo(std::min(width, height));
}

void TextureData::cleanupTexture(void* data){
    TextureData* texData = (TextureData*)data;
    if (texData->data){
        stbi_image_free(texData->data);
        texData->data = NULL;
    }
}
