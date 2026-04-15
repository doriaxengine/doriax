//
// (c) 2026 Eduardo Doria.
//

#include "STBText.h"

#include <string>
#include "Log.h"
#include "io/Data.h"
#include "DefaultFont.h"
#include "StringUtils.h"

using namespace doriax;

STBText::STBText() {
    atlasWidth = 0;
    atlasHeight = 0;

    ascent = 0;
    descent = 0;
    lineGap = 0;
    lineHeight = 0;

    textureData = NULL;
}

STBText::~STBText() {
    if (textureData){
        delete textureData;
    }
}

void STBText::tryFindBitmapSize(const stbtt_fontinfo *info, float scale){

    atlasWidth = 512;
    atlasHeight = 512;

    int x0; int y0; int x1; int y1;

    stbtt_GetFontBoundingBox(info, &x0, &y0, &x1, &y1);
    int gfh = (y1-y0) * scale;

    bool fitBitmap = false;
    while (!fitBitmap && atlasWidth <= atlasLimit){

        int xOffset = 0;
        int yOffset = 0;

        for (int i = firstChar; (i < lastChar && yOffset <= atlasHeight && xOffset <= atlasWidth); i++){
            stbtt_GetCodepointBox(info, i, &x0, &y0, &x1, &y1);
            int gw = (x1-x0) * scale;
            xOffset += gw+1;
            if (xOffset > atlasWidth){
                xOffset = 0;
                yOffset += gfh;
            }
        }

        if (yOffset > atlasHeight || xOffset > atlasWidth){
            atlasWidth = 2 * atlasWidth;
            atlasHeight = 2 * atlasHeight;
        }else {
            fitBitmap = true;
        }
    }
}

float STBText::getAscent(){
    return ascent;
}

float STBText::getDescent(){
    return descent;
}

float STBText::getLineGap(){
    return lineGap;
}

int STBText::getLineHeight(){
    return lineHeight;
}

float STBText::getCharWidth(char c){
    int intchar = (int)c;
    if (intchar < firstChar || intchar > lastChar)
        return 0;

    float offsetX = 0;
    float offsetY = 0;
    stbtt_aligned_quad quad;
    stbtt_GetPackedQuad(charInfo, atlasWidth, atlasHeight, intchar - firstChar, &offsetX, &offsetY, &quad, 1);

    return offsetX;
}

TextureData* STBText::load(const std::string& fontpath, unsigned int fontSize){

    Data fontData;

    if (!fontpath.empty()) {
        if (fontData.open(fontpath.c_str()) != FileErrors::FILEDATA_OK) {
            Log::error("Font file not found: %s", fontpath.c_str());
            return NULL;
        }
    }else{
        if (fontData.open(roboto_v20_latin_regular_ttf, roboto_v20_latin_regular_ttf_len, false, false) != FileErrors::FILEDATA_OK) {
            Log::error("Can't open default font");
            return NULL;
        }
    }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, fontData.getMemPtr(), 0)) {
        Log::error("Failed to initialize font: %s", fontpath.c_str());
        return NULL;
    }
    float scale = stbtt_ScaleForPixelHeight(&info, fontSize);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    this->ascent = ascent * scale;
    this->descent = descent * scale;
    this->lineGap = lineGap * scale;
    this->lineHeight = (ascent - descent + lineGap) * scale;

    tryFindBitmapSize(&info, scale);

    unsigned char *atlasData = NULL;
    charInfo = new stbtt_packedchar[charCount];

    stbtt_pack_context context;

    bool fitBitmap = false;
    while (!fitBitmap && atlasWidth <= atlasLimit) {
        if (atlasData) delete[] atlasData;
        atlasData = new unsigned char[atlasWidth * atlasHeight];

        if (!stbtt_PackBegin(&context, atlasData, atlasWidth, atlasHeight, 0, 1, nullptr)){
            Log::error("Failed to initialize font");
            return NULL;
        }

        //stbtt_PackSetOversampling(&context, oversampleX, oversampleY);
        if (!stbtt_PackFontRange(&context, fontData.getMemPtr(), 0, fontSize, firstChar, charCount, charInfo)){
            atlasWidth = atlasWidth * 2;
            atlasHeight = atlasHeight * 2;
        }else{
            fitBitmap = true;
        }
    }
    if (atlasWidth > atlasLimit){
        Log::error("Failed to pack font");
        return NULL;
    }
    stbtt_PackEnd(&context);

    unsigned int textureSize = atlasWidth * atlasHeight * sizeof(unsigned char);

    if (textureData){
        delete textureData;
    }
    textureData = new TextureData(atlasWidth, atlasHeight, textureSize, ColorFormat::RED, 1, (void*)atlasData);
    return textureData;
}

void STBText::createText(const std::string& text, Buffer* buffer, std::vector<uint16_t>& indices, std::vector<Vector2>& charPositions,
                         unsigned int& width, unsigned int& height, bool fixedWidth, bool fixedHeight, bool multiline, bool invert){

    bool hadInvalid = false;
    std::vector<uint32_t> codepoints = StringUtils::decodeUtf8ToCodepoints(text, hadInvalid);
    if (hadInvalid) {
        Log::warn("Invalid character");
    }
    
    float offsetX = 0;
    float offsetY = 0;

    Attribute* atrVertice = buffer->getAttribute(AttributeType::POSITION);
    Attribute* atrTexcoord = buffer->getAttribute(AttributeType::TEXCOORD1);

    if (multiline && fixedWidth){

        int lastSpace = 0;
        for (int i = 0; i < (int)codepoints.size(); i++){
            int intchar = (int)codepoints[(size_t)i];
            if (intchar == 32){ //space
                lastSpace = i;
            }
            if (intchar == 10){ //\n
                offsetX = 0;
            }
            if (intchar >= firstChar && intchar <= lastChar) {
                stbtt_aligned_quad quad;
                stbtt_GetPackedQuad(charInfo, atlasWidth, atlasHeight, intchar - firstChar, &offsetX, &offsetY, &quad, 1);
                
                if (offsetX > (float)width){
                    if (lastSpace > 0){
                        codepoints[(size_t)lastSpace] = '\n';
                        i = lastSpace;
                        lastSpace = 0;
                    }else{
                        codepoints.insert(codepoints.begin() + i, (uint32_t)'\n');
                    }
                    offsetX = 0;
                }
            }
        }

        offsetX = 0;
        offsetY = 0;
    }

    int minX0 = 0, maxX1 = 0, minY0 = 0, maxY1 = 0;
    int ind = 0;
    int lineCount = 1;
    charPositions.clear();

    for (int i = 0; i < (int)codepoints.size(); i++){

        int intchar = (int)codepoints[(size_t)i];

        if (intchar == 10){ //\n
            offsetY += lineHeight;
            offsetX = 0;
            lineCount++;

            continue;
        }

        //When char is not in bitmap
        if (intchar < firstChar || intchar > lastChar) {
            if (firstChar <= 127 && lastChar >= 127)
                intchar = 127;
            else
                intchar = firstChar;
        }

        stbtt_aligned_quad quad;
        stbtt_GetPackedQuad(charInfo, atlasWidth, atlasHeight, intchar - firstChar, &offsetX, &offsetY, &quad, 1);

        charPositions.push_back(Vector2(offsetX, offsetY));
            
        if (invert) {
            float auxt0 = quad.t0;
            quad.t0 = quad.t1;
            quad.t1 = auxt0;

            float auxy0 = quad.y0;
            quad.y0 = -quad.y1;
            quad.y1 = -auxy0;
        }
            
        if (quad.x0 < minX0)
            minX0 = quad.x0;
        if (quad.y0 < minY0)
            minY0 = quad.y0;
        if (quad.x1 > maxX1)
            maxX1 = quad.x1;
        if (quad.y1 > maxY1)
            maxY1 = quad.y1;
        if (offsetX > maxX1)
            maxX1 = offsetX;
            
        if ((!fixedWidth || offsetX <= width) && (!fixedHeight || offsetY <= height)){
            buffer->addVector3(atrVertice, Vector3(quad.x0, quad.y0, 0));
            buffer->addVector3(atrVertice, Vector3(quad.x1, quad.y0, 0));
            buffer->addVector3(atrVertice, Vector3(quad.x1, quad.y1, 0));
            buffer->addVector3(atrVertice, Vector3(quad.x0, quad.y1, 0));

            buffer->addVector2(atrTexcoord, Vector2(quad.s0, quad.t0));
            buffer->addVector2(atrTexcoord, Vector2(quad.s1, quad.t0));
            buffer->addVector2(atrTexcoord, Vector2(quad.s1, quad.t1));
            buffer->addVector2(atrTexcoord, Vector2(quad.s0, quad.t1));
                
            indices.push_back(ind);
            indices.push_back(ind+1);
            indices.push_back(ind+2);
            indices.push_back(ind);
            indices.push_back(ind+2);
            indices.push_back(ind+3);
            ind = ind + 4;
        }

    }
    //Empty text
    if (codepoints.size() == 0){
        buffer->addVector3(atrVertice, Vector3(0.0f, 0.0f, 0.0f));
        buffer->addVector3(atrVertice, Vector3(0.0f, 0.0f, 0.0f));
        buffer->addVector3(atrVertice, Vector3(0.0f, 0.0f, 0.0f));

        buffer->addVector2(atrTexcoord, Vector2(0.0f, 0.0f));
        buffer->addVector2(atrTexcoord, Vector2(0.0f, 0.0f));
        buffer->addVector2(atrTexcoord, Vector2(0.0f, 0.0f));
        
        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);
    }
    if (!fixedWidth)
        width = maxX1 - minX0;
    if (!fixedHeight)
        height = lineCount * lineHeight;
}

TextureData* STBText::getTextureData(){
    return textureData;
}
