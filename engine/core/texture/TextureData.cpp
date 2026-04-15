//
// (c) 2026 Eduardo Doria.
//

#ifndef RESIZE_WITH_STB
#define RESIZE_WITH_STB 0
#endif

#include "TextureData.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "stb_image.h"
#if RESIZE_WITH_STB
#include "stb_image_resize2.h"
#endif
#include "Log.h"
#include "Texture.h"

using namespace doriax;

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
}

TextureData::TextureData(const char* filename) : TextureData(){
    loadTextureFromFile(filename);
}

TextureData::TextureData(unsigned char* data, unsigned int dataLength) : TextureData(){
    loadTextureFromMemory(data, dataLength);
}

TextureData::TextureData(const TextureData& v){
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
    
    //----- Start std_image read texture
    stbi_info_from_memory((stbi_uc const *)filedata->getMemPtr(), filedata->length(), &width, &height, &channels);

    //Renders (not GL) only support 8bpp and 32bpp
    int desired_channels = 1;
    color_format = ColorFormat::RED;

    if (channels != 1){
        desired_channels = 4;
        color_format = ColorFormat::RGBA;
    }

    data = stbi_load_from_memory((stbi_uc const *)filedata->getMemPtr(), filedata->length(), &width, &height, &channels, desired_channels);

    if (!data){
        Log::error("Error loading texture: %s", stbi_failure_reason());
        return false;
    }

    channels = desired_channels;

    //Considering one byte per channel
    size = width * height * channels; //in bytes
    //----- End std_image read texture

    originalWidth = width;
    originalHeight = height;

    transparent = hasAlpha();
    
    return true;
}

bool TextureData::loadTextureFromFile(const char* filename) {
    Data filedata;

    int res = filedata.open(filename);

    if (res==FileErrors::FILE_NOT_FOUND){
        Log::error("Texture file not found: %s", filename);
        return false;
    }
    if (res==FileErrors::INVALID_PARAMETER){
        Log::error("Texture file path is invalid: %s", filename);
        return false;
    }

    bool result = loadTexture(&filedata);

    if (!result){
        Log::error("Texture file not loaded: %s", filename);
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

    // IMPORTANT:
    // Do not assign/copy 'texture' into each face and then call crop(), because TextureData::copy()
    // is shallow and would make all faces share the same underlying pixel pointer. The first crop()
    // frees that buffer, and subsequent crops crash.
    //
    // Instead, extract each face into its own buffer.

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
    };

    try {
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
    } catch (...) {
        texture.releaseImageData();
        throw;
    }

    // Release temporary source image data (it is not stored in the returned faces)
    texture.releaseImageData();

    return true;
}

void TextureData::copy ( const TextureData& v ){
    this->width = v.width;
    this->height = v.height;
    this->originalWidth = v.originalWidth;
    this->originalHeight = v.originalHeight;
    this->size = v.size;
    this->color_format = v.color_format;
    this->channels = v.channels;

    this->dataOwned = v.dataOwned;

    this->transparent = v.transparent;

    this->data = v.data;
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

        int bufsize = newWidth * newHeight * channels;
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

        stbir_resize_uint8_linear((unsigned char*)data, width, height, 0,
	                   newData, newWidth, newHeight, 0, pixel_layout);

        #else

        double scaleWidth =  (double)newWidth / (double)width;
        double scaleHeight = (double)newHeight / (double)height;

        for(int cy = 0; cy < newHeight; cy++){
            for(int cx = 0; cx < newWidth; cx++){
                
                int pixel = (cy * (newWidth*channels)) + (cx*channels);
                int nearestMatch =  (((int)(cy / scaleHeight) * (width*channels)) + ((int)(cx / scaleWidth) * channels) );
            
                for (int color = 0; color < channels; color++){
                    newData[pixel + color] =  ((unsigned char*)data)[nearestMatch + color];
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
