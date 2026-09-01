// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "STBText.h"

#include <cmath>
#include <string>
#include "Log.h"
#include "io/Data.h"
#include "DefaultFont.h"
#include "DefaultFontArabic.h"
#include "StringUtils.h"

#include "hb.h"
#include "SheenBidi/SheenBidi.h"

using namespace doriax;

STBText::STBText() {
    fontLoaded = false;

    atlasWidth = 0;
    atlasHeight = 0;
    atlasVersion = 0;

    ascent = 0;
    descent = 0;
    lineGap = 0;
    lineHeight = 0;

    textureData = NULL;
}

STBText::~STBText() {
    faces.clear();

    if (textureData){
        delete textureData;
    }
}

uint64_t STBText::glyphKey(unsigned int face, uint32_t glyphIndex){
    return ((uint64_t)face << 32) | (uint64_t)glyphIndex;
}

STBText::FontFace::~FontFace(){
    if (font)
        hb_font_destroy(font);
    if (face)
        hb_face_destroy(face);
    if (blob)
        hb_blob_destroy(blob);
}

bool STBText::initFace(FontFace* face, unsigned int fontSize, const std::string& label){
    //font collections (.ttc) keep the first table directory past the start of the file
    int fontOffset = stbtt_GetFontOffsetForIndex(face->data.getMemPtr(), 0);
    if (fontOffset < 0) {
        fontOffset = 0;
    }

    if (!stbtt_InitFont(&face->info, face->data.getMemPtr(), fontOffset)) {
        Log::error("Failed to initialize font: %s", label.c_str());
        return false;
    }
    face->scale = stbtt_ScaleForPixelHeight(&face->info, fontSize);

    face->blob = hb_blob_create((const char*)face->data.getMemPtr(), face->data.length(), HB_MEMORY_MODE_READONLY, NULL, NULL);
    face->face = hb_face_create(face->blob, 0);
    face->font = hb_font_create(face->face);

    if (!face->font || face->face == hb_face_get_empty()){
        Log::error("Failed to create shaper for font: %s", label.c_str());
        return false;
    }

    //shaper stays in font units, positions are multiplied by scale when read
    unsigned int upem = hb_face_get_upem(face->face);
    hb_font_set_scale(face->font, (int)upem, (int)upem);

    return true;
}

bool STBText::addFace(const std::string& fontpath, unsigned int fontSize){
    std::unique_ptr<FontFace> face(new FontFace());

    if (face->data.open(fontpath.c_str()) != FileErrors::FILEDATA_OK) {
        Log::error("Font file not found: %s", fontpath.c_str());
        return false;
    }

    if (!initFace(face.get(), fontSize, fontpath)){
        return false;
    }

    faces.push_back(std::move(face));

    return true;
}

bool STBText::addMemoryFace(unsigned char* data, unsigned int length, unsigned int fontSize, const std::string& label){
    std::unique_ptr<FontFace> face(new FontFace());

    if (face->data.open(data, length, false, false) != FileErrors::FILEDATA_OK) {
        Log::error("Can't open font: %s", label.c_str());
        return false;
    }

    if (!initFace(face.get(), fontSize, label)){
        return false;
    }

    faces.push_back(std::move(face));

    return true;
}

void STBText::addBuiltInFaces(unsigned int fontSize){
    //Latin first, the metrics and the .notdef box come from it
    addMemoryFace(roboto_v20_latin_regular_ttf, roboto_v20_latin_regular_ttf_len, fontSize, "built-in Roboto");
    addMemoryFace(noto_sans_arabic_ttf, noto_sans_arabic_ttf_len, fontSize, "built-in Noto Sans Arabic");
}

unsigned int STBText::selectFace(uint32_t codepoint, unsigned int current) const{
    //nothing to fall back to, skip the coverage lookups
    if (faces.size() <= 1)
        return 0;

    if (current < faces.size() && stbtt_FindGlyphIndex(&faces[current]->info, (int)codepoint) != 0){
        return current;
    }

    for (unsigned int i = 0; i < faces.size(); i++){
        if (stbtt_FindGlyphIndex(&faces[i]->info, (int)codepoint) != 0){
            return i;
        }
    }

    //uncovered, the primary font draws its .notdef box
    return 0;
}

void STBText::shapeRun(const std::vector<uint32_t>& codepoints, size_t offset, size_t length, bool rtl, unsigned int face, uint32_t script, unsigned int line, std::vector<ShapedGlyph>& shaped){
    if (length == 0 || face >= faces.size())
        return;

    float faceScale = faces[face]->scale;

    hb_buffer_t* buffer = hb_buffer_create();

    hb_buffer_add_utf32(buffer, codepoints.data(), (int)codepoints.size(), (unsigned int)offset, (int)length);
    //direction and script are already resolved for this run, only language is guessed
    hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, (hb_script_t)script);
    hb_buffer_guess_segment_properties(buffer);

    hb_shape(faces[face]->font, buffer, NULL, 0);

    unsigned int count = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);

    for (unsigned int i = 0; i < count; i++){
        ShapedGlyph glyph;

        glyph.face = face;
        glyph.rtl = rtl;
        glyph.glyphIndex = infos[i].codepoint;
        glyph.cluster = (size_t)infos[i].cluster;
        glyph.xOffset = positions[i].x_offset * faceScale;
        glyph.yOffset = positions[i].y_offset * faceScale;
        glyph.xAdvance = positions[i].x_advance * faceScale;
        glyph.line = line;

        shaped.push_back(glyph);
    }

    hb_buffer_destroy(buffer);
}

void STBText::shapeRunItems(const std::vector<uint32_t>& codepoints, size_t offset, size_t length, bool rtl, unsigned int line, std::vector<ShapedGlyph>& shaped){
    if (length == 0)
        return;

    //shaping must not cross a font or a script, a letter only joins inside its own
    struct Item {
        size_t offset;
        size_t length;
        unsigned int face;
        hb_script_t script;
    };

    hb_unicode_funcs_t* unicode = hb_unicode_funcs_get_default();

    std::vector<Item> items;
    size_t start = offset;
    unsigned int face = selectFace(codepoints[offset], 0);
    hb_script_t script = HB_SCRIPT_COMMON;

    for (size_t i = offset; i < offset + length; i++){
        unsigned int nextFace = selectFace(codepoints[i], face);

        hb_script_t nextScript = script;
        hb_script_t charScript = hb_unicode_script(unicode, codepoints[i]);
        //punctuation, spaces and combining marks take the script around them
        if (charScript != HB_SCRIPT_COMMON && charScript != HB_SCRIPT_INHERITED && charScript != HB_SCRIPT_UNKNOWN){
            nextScript = charScript;
        }

        if (nextFace == face && (nextScript == script || script == HB_SCRIPT_COMMON)){
            //a run that started on neutrals adopts the first real script it meets
            script = nextScript;
            continue;
        }

        items.push_back({start, i - start, face, script});

        start = i;
        face = nextFace;
        script = nextScript;
    }
    items.push_back({start, offset + length - start, face, script});

    //glyphs are emitted left to right, so an RTL run lays its pieces out backwards
    for (size_t i = 0; i < items.size(); i++){
        const Item& item = rtl ? items[items.size() - 1 - i] : items[i];
        shapeRun(codepoints, item.offset, item.length, rtl, item.face, (uint32_t)item.script, line, shaped);
    }
}

void STBText::shapeRange(const std::vector<uint32_t>& codepoints, size_t offset, size_t length, unsigned int line, std::vector<ShapedGlyph>& shaped){
    if (length == 0)
        return;

    SBCodepointSequence sequence;
    sequence.stringEncoding = SBStringEncodingUTF32;
    sequence.stringBuffer = (void*)codepoints.data();
    sequence.stringLength = (SBUInteger)codepoints.size();

    SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
    if (!algorithm){
        shapeRunItems(codepoints, offset, length, false, line, shaped);
        return;
    }

    //base direction comes from the first strong character
    SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, (SBUInteger)offset, (SBUInteger)length, SBLevelDefaultLTR);
    SBLineRef bidiLine = paragraph ? SBParagraphCreateLine(paragraph, (SBUInteger)offset, SBParagraphGetLength(paragraph)) : NULL;

    if (bidiLine){
        //runs come back in visual order, left to right
        SBUInteger runCount = SBLineGetRunCount(bidiLine);
        const SBRun* runs = SBLineGetRunsPtr(bidiLine);

        for (SBUInteger i = 0; i < runCount; i++){
            shapeRunItems(codepoints, (size_t)runs[i].offset, (size_t)runs[i].length, (runs[i].level & 1) != 0, line, shaped);
        }

        SBLineRelease(bidiLine);
    }else{
        shapeRunItems(codepoints, offset, length, false, line, shaped);
    }

    if (paragraph)
        SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);
}

void STBText::measureAdvances(const std::vector<uint32_t>& codepoints, size_t offset, size_t length, std::vector<float>& advances){
    std::vector<ShapedGlyph> shaped;
    shapeRange(codepoints, offset, length, 0, shaped);

    //widths do not depend on visual order, charge each advance to its codepoint
    for (const ShapedGlyph& glyph : shaped){
        if (glyph.cluster < advances.size()){
            advances[glyph.cluster] += glyph.xAdvance;
        }
    }
}

float STBText::shapeLineInto(const std::vector<uint32_t>& codepoints, size_t offset, size_t length, unsigned int line, std::vector<ShapedGlyph>& lineGlyphs){
    lineGlyphs.clear();
    shapeRange(codepoints, offset, length, line, lineGlyphs);

    float total = 0.0f;
    for (const ShapedGlyph& glyph : lineGlyphs){
        total += glyph.xAdvance;
    }

    return total;
}

unsigned int STBText::shapeLines(const std::vector<uint32_t>& codepoints, unsigned int width, bool fixedWidth, bool multiline, std::vector<ShapedGlyph>& shaped){
    unsigned int line = 0;

    std::vector<float> advances;
    if (multiline && fixedWidth){
        advances.assign(codepoints.size(), 0.0f);
    }

    std::vector<ShapedGlyph> lineGlyphs;

    size_t start = 0;
    while (start <= codepoints.size()){
        size_t end = start;
        while (end < codepoints.size() && codepoints[end] != 10){
            end++;
        }

        if (multiline && fixedWidth && end > start){
            //a first guess at where the line ends
            measureAdvances(codepoints, start, end - start, advances);

            size_t lineStart = start;
            while (lineStart < end){
                size_t breakAt = end;
                size_t lastSpace = end;
                float estimate = 0.0f;

                for (size_t i = lineStart; i < end; i++){
                    if (codepoints[i] == 32){
                        lastSpace = i;
                    }

                    estimate += advances[i];

                    if (estimate > (float)width && i > lineStart){
                        breakAt = (lastSpace > lineStart && lastSpace < i) ? lastSpace : i;
                        break;
                    }
                }

                //the guess is not binding, a letter that was medial inside the paragraph
                //becomes a wider final form at the break, so give ground until it fits
                while (true){
                    if (shapeLineInto(codepoints, lineStart, breakAt - lineStart, line, lineGlyphs) <= (float)width)
                        break;
                    if (breakAt <= lineStart + 1)
                        break; //a single cluster wider than the budget, nothing to gain

                    size_t retreat = breakAt - 1;
                    for (size_t j = breakAt - 1; j > lineStart; j--){
                        if (codepoints[j] == 32){
                            retreat = j;
                            break;
                        }
                    }
                    breakAt = retreat;
                }

                shaped.insert(shaped.end(), lineGlyphs.begin(), lineGlyphs.end());
                line++;

                //a break on a space swallows it, like a newline would
                lineStart = (breakAt < end && codepoints[breakAt] == 32) ? breakAt + 1 : breakAt;
            }

        }else{
            shapeRange(codepoints, start, end - start, line, shaped);
            line++;
        }

        if (end >= codepoints.size())
            break;

        start = end + 1;
    }

    return line;
}

void STBText::resetAtlas(unsigned int width, unsigned int height){
    if (!atlasPixels.empty()){
        retiredAtlases.push_back(std::move(atlasPixels));
    }

    atlasWidth = width;
    atlasHeight = height;

    atlasPixels.assign((size_t)atlasWidth * (size_t)atlasHeight, 0);
    shelves.clear();

    atlasVersion++;
}

bool STBText::packRect(int width, int height, int& outX, int& outY){
    outX = 0;
    outY = 0;

    if (width <= 0 || height <= 0){
        return true;
    }

    width += atlasPadding;
    height += atlasPadding;

    //shortest shelf that still fits, to not waste the taller ones
    Shelf* best = NULL;
    for (Shelf& shelf : shelves){
        if (shelf.h >= height && (shelf.x + width) <= (int)atlasWidth){
            if (!best || shelf.h < best->h)
                best = &shelf;
        }
    }

    if (best){
        outX = best->x;
        outY = best->y;
        best->x += width;

        return true;
    }

    int shelfY = 0;
    if (!shelves.empty()){
        shelfY = shelves.back().y + shelves.back().h;
    }

    if (width > (int)atlasWidth || (shelfY + height) > (int)atlasHeight){
        return false;
    }

    shelves.push_back({width, shelfY, height});

    outY = shelfY;

    return true;
}

bool STBText::growAtlas(){
    if (atlasWidth * 2 > atlasLimit || atlasHeight * 2 > atlasLimit){
        return false;
    }

    std::vector<uint64_t> cached;
    cached.reserve(glyphMap.size());
    for (const auto& [key, _] : glyphMap){
        cached.push_back(key);
    }

    resetAtlas(atlasWidth * 2, atlasHeight * 2);
    glyphMap.clear();

    for (uint64_t key : cached){
        getGlyph((unsigned int)(key >> 32), (uint32_t)key);
    }

    return true;
}

void STBText::rasterizeGlyph(unsigned int face, uint32_t glyphIndex, FontGlyph& glyph, int x, int y){
    stbtt_fontinfo* info = &faces[face]->info;
    float scale = faces[face]->scale;

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

    int gw = x1 - x0;
    int gh = y1 - y0;

    if (gw > 0 && gh > 0){
        stbtt_MakeGlyphBitmap(info, &atlasPixels[(size_t)y * atlasWidth + x], gw, gh, atlasWidth, scale, scale, glyphIndex);
    }

    int advance, leftSideBearing;
    stbtt_GetGlyphHMetrics(info, glyphIndex, &advance, &leftSideBearing);

    glyph.xoff = x0;
    glyph.yoff = y0;
    glyph.xoff2 = x1;
    glyph.yoff2 = y1;

    glyph.s0 = (float)x / atlasWidth;
    glyph.t0 = (float)y / atlasHeight;
    glyph.s1 = (float)(x + gw) / atlasWidth;
    glyph.t1 = (float)(y + gh) / atlasHeight;

    glyph.xadvance = advance * scale;

    atlasVersion++;
}

const STBText::FontGlyph* STBText::getGlyph(unsigned int face, uint32_t glyphIndex){
    if (!fontLoaded || face >= faces.size())
        return NULL;

    uint64_t key = glyphKey(face, glyphIndex);

    auto it = glyphMap.find(key);
    if (it != glyphMap.end()){
        return &it->second;
    }

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&faces[face]->info, glyphIndex, faces[face]->scale, faces[face]->scale, &x0, &y0, &x1, &y1);

    int x, y;
    if (!packRect(x1 - x0, y1 - y0, x, y)){
        //growAtlas() repacks the cached glyphs, this one is still missing
        if (!growAtlas() || !packRect(x1 - x0, y1 - y0, x, y)){
            Log::error("Failed to pack glyph in font atlas");
            return NULL;
        }
    }

    FontGlyph& glyph = glyphMap[key];
    rasterizeGlyph(face, glyphIndex, glyph, x, y);

    return &glyph;
}

const STBText::FontGlyph* STBText::getGlyphForCodepoint(uint32_t codepoint){
    if (!fontLoaded)
        return NULL;

    auto it = codepointMap.find(codepoint);
    if (it == codepointMap.end()){
        unsigned int face = selectFace(codepoint, 0);
        //not covered by any face resolves to glyph 0, the primary .notdef box
        uint32_t glyphIndex = (uint32_t)stbtt_FindGlyphIndex(&faces[face]->info, (int)codepoint);

        it = codepointMap.emplace(codepoint, glyphKey(face, glyphIndex)).first;
    }

    return getGlyph((unsigned int)(it->second >> 32), (uint32_t)(it->second & 0xFFFFFFFF));
}

void STBText::refreshTextureData(){
    unsigned int textureSize = atlasWidth * atlasHeight * sizeof(unsigned char);

    if (textureData){
        delete textureData;
    }
    textureData = new TextureData(atlasWidth, atlasHeight, textureSize, ColorFormat::RED, 1, (void*)atlasPixels.data());
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

float STBText::getCharWidth(uint32_t codepoint){
    const FontGlyph* glyph = getGlyphForCodepoint(codepoint);
    if (!glyph)
        return 0;

    return glyph->xadvance;
}

unsigned long STBText::getAtlasVersion() const{
    return atlasVersion;
}

TextureData* STBText::load(const std::string& fontpath, const std::vector<std::string>& fallbackPaths, unsigned int fontSize){

    fontLoaded = false;
    faces.clear();

    if (!fontpath.empty() && !addFace(fontpath, fontSize)){
        faces.clear();
        return NULL;
    }

    for (const std::string& fallbackPath : fallbackPaths){
        //a missing fallback is not fatal, the rest of the chain still covers the text
        addFace(fallbackPath, fontSize);
    }

    //the built-in fonts close the chain, a font made for one script still renders
    //what it does not carry
    addBuiltInFaces(fontSize);

    if (faces.empty()){
        return NULL;
    }

    //line metrics come from the primary font, fallbacks follow its baseline
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&faces[0]->info, &ascent, &descent, &lineGap);

    float scale = faces[0]->scale;

    this->ascent = ascent * scale;
    this->descent = descent * scale;
    this->lineGap = lineGap * scale;
    this->lineHeight = (ascent - descent + lineGap) * scale;

    glyphMap.clear();
    codepointMap.clear();

    resetAtlas(512, 512);

    fontLoaded = true;

    refreshTextureData();

    return textureData;
}

void STBText::createText(const std::string& text, Buffer* buffer, std::vector<uint16_t>& indices, std::vector<Vector2>& charPositions,
                         std::vector<CharExtent>& charExtents,
                         unsigned int& width, unsigned int& height, bool fixedWidth, bool fixedHeight, bool multiline, bool invert){

    bool hadInvalid = false;
    std::vector<uint32_t> codepoints = StringUtils::decodeUtf8ToCodepoints(text, hadInvalid);
    if (hadInvalid) {
        Log::warn("Invalid character");
    }

    unsigned long startVersion = atlasVersion;

    std::vector<ShapedGlyph> shaped;
    int lineCount = (int)shapeLines(codepoints, width, fixedWidth, multiline, shaped);

    //rasterize every glyph first, a grow in the middle of the layout would repack
    //the ones already written in the buffer
    for (const ShapedGlyph& glyph : shaped){
        getGlyph(glyph.face, glyph.glyphIndex);
    }

    float offsetX = 0;
    float offsetY = 0;

    Attribute* atrVertice = buffer->getAttribute(AttributeType::POSITION);
    Attribute* atrTexcoord = buffer->getAttribute(AttributeType::TEXCOORD1);

    int minX0 = 0, maxX1 = 0, minY0 = 0, maxY1 = 0;
    int ind = 0;
    unsigned int currentLine = 0;

    //trailing pen position of each codepoint, so charPositions can stay indexed by
    //codepoint even when a cluster produced several glyphs or none
    std::vector<Vector2> clusterEnd(codepoints.size(), Vector2(0, 0));
    std::vector<bool> clusterSet(codepoints.size(), false);

    //visual span of each cluster, which is what a caret and a selection need
    std::vector<CharExtent> clusterExtent(codepoints.size());

    for (size_t s = 0; s < shaped.size(); s++){

        const ShapedGlyph& shapedGlyph = shaped[s];

        if (shapedGlyph.line != currentLine){
            currentLine = shapedGlyph.line;
            offsetY = currentLine * lineHeight;
            offsetX = 0;
        }

        const FontGlyph* glyph = getGlyph(shapedGlyph.face, shapedGlyph.glyphIndex);
        if (!glyph)
            continue;

        //aligned to integer, like stbtt_GetPackedQuad does
        float quadX = std::floor(offsetX + shapedGlyph.xOffset + glyph->xoff + 0.5f);
        float quadY = std::floor(offsetY - shapedGlyph.yOffset + glyph->yoff + 0.5f);

        stbtt_aligned_quad quad;
        quad.x0 = quadX;
        quad.y0 = quadY;
        quad.x1 = quadX + (glyph->xoff2 - glyph->xoff);
        quad.y1 = quadY + (glyph->yoff2 - glyph->yoff);
        quad.s0 = glyph->s0;
        quad.t0 = glyph->t0;
        quad.s1 = glyph->s1;
        quad.t1 = glyph->t1;

        float glyphStart = offsetX;
        offsetX += shapedGlyph.xAdvance;

        if (shapedGlyph.cluster < clusterEnd.size()){
            size_t cluster = shapedGlyph.cluster;

            clusterEnd[cluster] = Vector2(offsetX, offsetY);

            if (!clusterSet[cluster]){
                clusterExtent[cluster].left = glyphStart;
                clusterExtent[cluster].right = offsetX;
            }else{
                //several glyphs of the same cluster, take the whole span they cover
                clusterExtent[cluster].left = std::min(clusterExtent[cluster].left, glyphStart);
                clusterExtent[cluster].right = std::max(clusterExtent[cluster].right, offsetX);
            }
            clusterExtent[cluster].y = offsetY;
            clusterExtent[cluster].rtl = shapedGlyph.rtl;

            clusterSet[cluster] = true;
        }

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
            
        {
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

    //a cluster covers every codepoint up to the next one, and they share its glyphs,
    //so they split its width. Inside an RTL run the first owns the rightmost slice
    Vector2 lastPosition = Vector2(0, 0);
    for (size_t i = 0; i < codepoints.size(); i++){
        if (!clusterSet[i]){
            clusterEnd[i] = lastPosition;
            continue;
        }

        size_t covered = 1;
        while (i + covered < codepoints.size() && !clusterSet[i + covered] && codepoints[i + covered] != 10){
            covered++;
        }

        CharExtent cluster = clusterExtent[i];
        float slice = (cluster.right - cluster.left) / (float)covered;
        lastPosition = clusterEnd[i];

        for (size_t j = 0; j < covered; j++){
            CharExtent& extent = clusterExtent[i + j];

            extent = cluster;
            if (cluster.rtl){
                extent.right = cluster.right - slice * j;
                extent.left = extent.right - slice;
            }else{
                extent.left = cluster.left + slice * j;
                extent.right = extent.left + slice;
            }

            clusterEnd[i + j] = lastPosition;
        }

        i += covered - 1;
    }

    //both arrays stay indexed by codepoint, in logical order
    charPositions.clear();
    charExtents.clear();

    for (size_t i = 0; i < codepoints.size(); i++){
        if (codepoints[i] == 10) //\n
            continue;

        charPositions.push_back(clusterEnd[i]);
        charExtents.push_back(clusterExtent[i]);
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

    if (atlasVersion != startVersion){
        refreshTextureData();
    }
}

TextureData* STBText::getTextureData(){
    return textureData;
}
