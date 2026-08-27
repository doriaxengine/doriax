// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "Stream.h"

#include "Base64.h"
#include "Catalog.h"
#include "Factory.h"
#include "pool/SoundPool.h"
#include "pool/ShaderPool.h"
#include "util/CameraTextureLink.h"
#include "util/ProjectUtils.h"
#include "util/StringUtils.h"
#include "render/SceneRender2D.h"
#include "render/SceneRender3D.h"
#include "window/TerrainEditWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

using namespace doriax;

std::string editor::Stream::makeEmbeddedTextureId(){
    static uint64_t embeddedTextureCounter = 1;
    return "__stream_embedded_texture_" + std::to_string(embeddedTextureCounter++);
}

const char* editor::Stream::colorFormatToString(ColorFormat format){
    return format == ColorFormat::RED ? "red" : "rgba";
}

ColorFormat editor::Stream::stringToColorFormat(const std::string& value, int channels){
    if (value == "red"){
        return ColorFormat::RED;
    }
    if (value == "rgba"){
        return ColorFormat::RGBA;
    }
    return channels == 1 ? ColorFormat::RED : ColorFormat::RGBA;
}

bool editor::Stream::getEmbeddedTextureData(const Texture& texture, TextureData*& data){
    if (texture.empty() || texture.isFramebuffer()){
        return false;
    }

    Texture& mutableTexture = const_cast<Texture&>(texture);
    TextureLoadResult result = mutableTexture.load();
    if (result.state != ResourceLoadState::Finished || !result.data){
        return false;
    }

    TextureData& loadedData = mutableTexture.getData();
    if (!loadedData.getData() || loadedData.getSize() == 0){
        return false;
    }

    data = &loadedData;
    return true;
}

bool editor::Stream::setEmbeddedTextureData(Texture& texture, const std::string& preferredId, int width, int height, ColorFormat format, int channels, const std::vector<unsigned char>& pixels){
    const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    if (width <= 0 || height <= 0 || channels <= 0 || pixels.size() < size){
        return false;
    }

    unsigned char* raw = static_cast<unsigned char*>(std::malloc(size));
    if (!raw){
        return false;
    }

    std::memcpy(raw, pixels.data(), size);

    TextureData data(width, height, static_cast<unsigned int>(size), format, channels, raw);
    data.setDataOwned(false);

    std::string textureId = preferredId.empty() ? makeEmbeddedTextureId() : preferredId;
    texture.setData(textureId, data);
    texture.getData().setDataOwned(true);
    return true;
}

float editor::Stream::finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : (std::isfinite(fallback) ? fallback : 0.0f);
}

double editor::Stream::finiteOr(double value, double fallback) {
    return std::isfinite(value) ? value : (std::isfinite(fallback) ? fallback : 0.0);
}

float editor::Stream::positiveFiniteOr(float value, float fallback) {
    if (std::isfinite(value) && value > 0.0f) {
        return value;
    }
    return std::isfinite(fallback) && fallback > 0.0f ? fallback : 1.0f;
}

float editor::Stream::decodeFinite(const YAML::Node& node, float fallback) {
    if (!node || !node.IsScalar()) {
        return finiteOr(fallback);
    }

    try {
        float value = node.as<float>();
        return finiteOr(value, fallback);
    } catch (const YAML::Exception&) {
        return finiteOr(fallback);
    }
}

double editor::Stream::decodeFiniteDouble(const YAML::Node& node, double fallback) {
    if (!node || !node.IsScalar()) {
        return finiteOr(fallback);
    }

    try {
        double value = node.as<double>();
        return finiteOr(value, fallback);
    } catch (const YAML::Exception&) {
        return finiteOr(fallback);
    }
}

float editor::Stream::decodePositiveFinite(const YAML::Node& node, float fallback) {
    if (!node || !node.IsScalar()) {
        return positiveFiniteOr(fallback, 1.0f);
    }

    try {
        float value = node.as<float>();
        return positiveFiniteOr(value, fallback);
    } catch (const YAML::Exception&) {
        return positiveFiniteOr(fallback, 1.0f);
    }
}

void editor::Stream::encodeFinite(YAML::Node& node, const char* key, float value) {
    if (std::isfinite(value)) {
        node[key] = value;
    }
}

void editor::Stream::encodeFinite(YAML::Node& node, const char* key, double value) {
    if (std::isfinite(value)) {
        node[key] = value;
    }
}

void editor::Stream::encodePositiveFinite(YAML::Node& node, const char* key, float value) {
    if (std::isfinite(value) && value > 0.0f) {
        node[key] = value;
    }
}

void editor::Stream::encodeFiniteVector3(YAML::Node& node, const char* key, const Vector3& value) {
    if (value.isValid()) {
        node[key] = encodeVector3(value);
    }
}

Vector3 editor::Stream::decodeFiniteVector3(const YAML::Node& node, const Vector3& fallback) {
    Vector3 safeFallback = fallback.isValid() ? fallback : Vector3::ZERO;
    if (!node || !node.IsSequence() || node.size() < 3) {
        return safeFallback;
    }

    return Vector3(
        decodeFinite(node[0], safeFallback.x),
        decodeFinite(node[1], safeFallback.y),
        decodeFinite(node[2], safeFallback.z)
    );
}

std::string editor::Stream::sceneTypeToString(editor::SceneType type){
    switch (type) {
        case SceneType::SCENE_3D: return "scene_3d";
        case SceneType::SCENE_2D: return "scene_2d";
        case SceneType::SCENE_UI: return "scene_ui";
        default: return "scene_3d";
    }
}

editor::SceneType editor::Stream::stringToSceneType(const std::string& str){
    if (str == "scene_3d") return SceneType::SCENE_3D;
    if (str == "scene_2d") return SceneType::SCENE_2D;
    if (str == "scene_ui") return SceneType::SCENE_UI;
    return SceneType::SCENE_3D; // Default
}

std::string editor::Stream::primitiveTypeToString(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::TRIANGLES: return "triangles";
        case PrimitiveType::TRIANGLE_STRIP: return "triangle_strip";
        case PrimitiveType::POINTS: return "points";
        case PrimitiveType::LINES: return "lines";
        default: return "triangles";
    }
}

PrimitiveType editor::Stream::stringToPrimitiveType(const std::string& str) {
    if (str == "triangles") return PrimitiveType::TRIANGLES;
    if (str == "triangle_strip") return PrimitiveType::TRIANGLE_STRIP;
    if (str == "points") return PrimitiveType::POINTS;
    if (str == "lines") return PrimitiveType::LINES;
    return PrimitiveType::TRIANGLES; // Default
}

std::string editor::Stream::easeTypeToString(EaseType type) {
    switch (type) {
        case EaseType::LINEAR: return "LINEAR";
        case EaseType::QUAD_IN: return "QUAD_IN";
        case EaseType::QUAD_OUT: return "QUAD_OUT";
        case EaseType::QUAD_IN_OUT: return "QUAD_IN_OUT";
        case EaseType::CUBIC_IN: return "CUBIC_IN";
        case EaseType::CUBIC_OUT: return "CUBIC_OUT";
        case EaseType::CUBIC_IN_OUT: return "CUBIC_IN_OUT";
        case EaseType::QUART_IN: return "QUART_IN";
        case EaseType::QUART_OUT: return "QUART_OUT";
        case EaseType::QUART_IN_OUT: return "QUART_IN_OUT";
        case EaseType::QUINT_IN: return "QUINT_IN";
        case EaseType::QUINT_OUT: return "QUINT_OUT";
        case EaseType::QUINT_IN_OUT: return "QUINT_IN_OUT";
        case EaseType::SINE_IN: return "SINE_IN";
        case EaseType::SINE_OUT: return "SINE_OUT";
        case EaseType::SINE_IN_OUT: return "SINE_IN_OUT";
        case EaseType::EXPO_IN: return "EXPO_IN";
        case EaseType::EXPO_OUT: return "EXPO_OUT";
        case EaseType::EXPO_IN_OUT: return "EXPO_IN_OUT";
        case EaseType::CIRC_IN: return "CIRC_IN";
        case EaseType::CIRC_OUT: return "CIRC_OUT";
        case EaseType::CIRC_IN_OUT: return "CIRC_IN_OUT";
        case EaseType::ELASTIC_IN: return "ELASTIC_IN";
        case EaseType::ELASTIC_OUT: return "ELASTIC_OUT";
        case EaseType::ELASTIC_IN_OUT: return "ELASTIC_IN_OUT";
        case EaseType::BACK_IN: return "BACK_IN";
        case EaseType::BACK_OUT: return "BACK_OUT";
        case EaseType::BACK_IN_OUT: return "BACK_IN_OUT";
        case EaseType::BOUNCE_IN: return "BOUNCE_IN";
        case EaseType::BOUNCE_OUT: return "BOUNCE_OUT";
        case EaseType::BOUNCE_IN_OUT: return "BOUNCE_IN_OUT";
        case EaseType::STEP: return "STEP";
        case EaseType::CUSTOM: return "CUSTOM";
        default: return "LINEAR";
    }
}

EaseType editor::Stream::stringToEaseType(const std::string& str) {
    std::string normalized;
    normalized.reserve(str.size());
    for (char ch : str) {
        if (ch == '-') {
            normalized.push_back('_');
        } else {
            normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
    }

    if (normalized == "LINEAR") return EaseType::LINEAR;
    if (normalized == "QUAD_IN") return EaseType::QUAD_IN;
    if (normalized == "QUAD_OUT") return EaseType::QUAD_OUT;
    if (normalized == "QUAD_IN_OUT") return EaseType::QUAD_IN_OUT;
    if (normalized == "CUBIC_IN") return EaseType::CUBIC_IN;
    if (normalized == "CUBIC_OUT") return EaseType::CUBIC_OUT;
    if (normalized == "CUBIC_IN_OUT") return EaseType::CUBIC_IN_OUT;
    if (normalized == "QUART_IN") return EaseType::QUART_IN;
    if (normalized == "QUART_OUT") return EaseType::QUART_OUT;
    if (normalized == "QUART_IN_OUT") return EaseType::QUART_IN_OUT;
    if (normalized == "QUINT_IN") return EaseType::QUINT_IN;
    if (normalized == "QUINT_OUT") return EaseType::QUINT_OUT;
    if (normalized == "QUINT_IN_OUT") return EaseType::QUINT_IN_OUT;
    if (normalized == "SINE_IN") return EaseType::SINE_IN;
    if (normalized == "SINE_OUT") return EaseType::SINE_OUT;
    if (normalized == "SINE_IN_OUT") return EaseType::SINE_IN_OUT;
    if (normalized == "EXPO_IN") return EaseType::EXPO_IN;
    if (normalized == "EXPO_OUT") return EaseType::EXPO_OUT;
    if (normalized == "EXPO_IN_OUT") return EaseType::EXPO_IN_OUT;
    if (normalized == "CIRC_IN") return EaseType::CIRC_IN;
    if (normalized == "CIRC_OUT") return EaseType::CIRC_OUT;
    if (normalized == "CIRC_IN_OUT") return EaseType::CIRC_IN_OUT;
    if (normalized == "ELASTIC_IN") return EaseType::ELASTIC_IN;
    if (normalized == "ELASTIC_OUT") return EaseType::ELASTIC_OUT;
    if (normalized == "ELASTIC_IN_OUT") return EaseType::ELASTIC_IN_OUT;
    if (normalized == "BACK_IN") return EaseType::BACK_IN;
    if (normalized == "BACK_OUT") return EaseType::BACK_OUT;
    if (normalized == "BACK_IN_OUT") return EaseType::BACK_IN_OUT;
    if (normalized == "BOUNCE_IN") return EaseType::BOUNCE_IN;
    if (normalized == "BOUNCE_OUT") return EaseType::BOUNCE_OUT;
    if (normalized == "BOUNCE_IN_OUT") return EaseType::BOUNCE_IN_OUT;
    if (normalized == "STEP") return EaseType::STEP;
    if (normalized == "CUSTOM") return EaseType::CUSTOM;
    return EaseType::LINEAR;
}

YAML::Node editor::Stream::encodeEase(const Ease& ease) {
    YAML::Node node;
    node = easeTypeToString(ease.getType());
    return node;
}

Ease editor::Stream::decodeEase(const YAML::Node& node, const Ease* oldEase) {
    Ease ease;
    if (oldEase) {
        ease = *oldEase;
    }

    EaseType type = node.IsScalar() ? stringToEaseType(node.as<std::string>()) : EaseType::LINEAR;

    if (type == EaseType::CUSTOM) {
        if (!oldEase || oldEase->getType() != EaseType::CUSTOM) {
            ease = Ease(EaseType::LINEAR);
        }
        ease.setType(EaseType::CUSTOM);
    } else {
        ease.setType(type);
    }

    return ease;
}

std::string editor::Stream::bufferTypeToString(BufferType type) {
    switch (type) {
        case BufferType::VERTEX_BUFFER: return "vertex_buffer";
        case BufferType::INDEX_BUFFER: return "index_buffer";
        case BufferType::STORAGE_BUFFER: return "storage_buffer";
        default: return "vertex_buffer";
    }
}

BufferType editor::Stream::stringToBufferType(const std::string& str) {
    if (str == "vertex_buffer") return BufferType::VERTEX_BUFFER;
    if (str == "index_buffer") return BufferType::INDEX_BUFFER;
    if (str == "storage_buffer") return BufferType::STORAGE_BUFFER;
    return BufferType::VERTEX_BUFFER; // Default
}

// BufferUsage enum conversion
std::string editor::Stream::bufferUsageToString(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::IMMUTABLE: return "immutable";
        case BufferUsage::DYNAMIC: return "dynamic";
        case BufferUsage::STREAM: return "stream";
        default: return "immutable"; // Default
    }
}

BufferUsage editor::Stream::stringToBufferUsage(const std::string& str) {
    if (str == "immutable") return BufferUsage::IMMUTABLE;
    if (str == "dynamic") return BufferUsage::DYNAMIC;
    if (str == "stream") return BufferUsage::STREAM;
    return BufferUsage::IMMUTABLE; // Default
}

// AttributeType enum conversion
std::string editor::Stream::attributeTypeToString(AttributeType type) {
    switch (type) {
        case AttributeType::INDEX: return "index";
        case AttributeType::POSITION: return "position";
        case AttributeType::TEXCOORD1: return "texcoord1";
        case AttributeType::TEXCOORD2: return "texcoord2";
        case AttributeType::NORMAL: return "normal";
        case AttributeType::TANGENT: return "tangent";
        case AttributeType::COLOR: return "color";
        case AttributeType::POINTSIZE: return "pointsize";
        case AttributeType::POINTROTATION: return "pointrotation";
        case AttributeType::TEXTURERECT: return "texturerect";
        case AttributeType::BONEWEIGHTS: return "boneweights";
        case AttributeType::BONEIDS: return "boneids";
        case AttributeType::MORPHTARGET0: return "morphtarget0";
        case AttributeType::MORPHTARGET1: return "morphtarget1";
        case AttributeType::MORPHTARGET2: return "morphtarget2";
        case AttributeType::MORPHTARGET3: return "morphtarget3";
        case AttributeType::MORPHTARGET4: return "morphtarget4";
        case AttributeType::MORPHTARGET5: return "morphtarget5";
        case AttributeType::MORPHTARGET6: return "morphtarget6";
        case AttributeType::MORPHTARGET7: return "morphtarget7";
        case AttributeType::MORPHNORMAL0: return "morphnormal0";
        case AttributeType::MORPHNORMAL1: return "morphnormal1";
        case AttributeType::MORPHNORMAL2: return "morphnormal2";
        case AttributeType::MORPHNORMAL3: return "morphnormal3";
        case AttributeType::MORPHTANGENT0: return "morphtangent0";
        case AttributeType::MORPHTANGENT1: return "morphtangent1";
        case AttributeType::INSTANCEMATRIXCOL1: return "instancematrixcol1";
        case AttributeType::INSTANCEMATRIXCOL2: return "instancematrixcol2";
        case AttributeType::INSTANCEMATRIXCOL3: return "instancematrixcol3";
        case AttributeType::INSTANCEMATRIXCOL4: return "instancematrixcol4";
        case AttributeType::INSTANCECOLOR: return "instancecolor";
        case AttributeType::INSTANCETEXTURERECT: return "instancetexturerect";
        case AttributeType::TERRAINNODEPOSITION: return "terrainnodeposition";
        case AttributeType::TERRAINNODESIZE: return "terrainnodesize";
        case AttributeType::TERRAINNODERANGE: return "terrainnoderange";
        case AttributeType::TERRAINNODERESOLUTION: return "terrainnoderesolution";
        default: return "position"; // Default
    }
}

AttributeType editor::Stream::stringToAttributeType(const std::string& str) {
    if (str == "index") return AttributeType::INDEX;
    if (str == "position") return AttributeType::POSITION;
    if (str == "texcoord1") return AttributeType::TEXCOORD1;
    if (str == "texcoord2") return AttributeType::TEXCOORD2;
    if (str == "normal") return AttributeType::NORMAL;
    if (str == "tangent") return AttributeType::TANGENT;
    if (str == "color") return AttributeType::COLOR;
    if (str == "pointsize") return AttributeType::POINTSIZE;
    if (str == "pointrotation") return AttributeType::POINTROTATION;
    if (str == "texturerect") return AttributeType::TEXTURERECT;
    if (str == "boneweights") return AttributeType::BONEWEIGHTS;
    if (str == "boneids") return AttributeType::BONEIDS;
    if (str == "morphtarget0") return AttributeType::MORPHTARGET0;
    if (str == "morphtarget1") return AttributeType::MORPHTARGET1;
    if (str == "morphtarget2") return AttributeType::MORPHTARGET2;
    if (str == "morphtarget3") return AttributeType::MORPHTARGET3;
    if (str == "morphtarget4") return AttributeType::MORPHTARGET4;
    if (str == "morphtarget5") return AttributeType::MORPHTARGET5;
    if (str == "morphtarget6") return AttributeType::MORPHTARGET6;
    if (str == "morphtarget7") return AttributeType::MORPHTARGET7;
    if (str == "morphnormal0") return AttributeType::MORPHNORMAL0;
    if (str == "morphnormal1") return AttributeType::MORPHNORMAL1;
    if (str == "morphnormal2") return AttributeType::MORPHNORMAL2;
    if (str == "morphnormal3") return AttributeType::MORPHNORMAL3;
    if (str == "morphtangent0") return AttributeType::MORPHTANGENT0;
    if (str == "morphtangent1") return AttributeType::MORPHTANGENT1;
    if (str == "instancematrixcol1") return AttributeType::INSTANCEMATRIXCOL1;
    if (str == "instancematrixcol2") return AttributeType::INSTANCEMATRIXCOL2;
    if (str == "instancematrixcol3") return AttributeType::INSTANCEMATRIXCOL3;
    if (str == "instancematrixcol4") return AttributeType::INSTANCEMATRIXCOL4;
    if (str == "instancecolor") return AttributeType::INSTANCECOLOR;
    if (str == "instancetexturerect") return AttributeType::INSTANCETEXTURERECT;
    if (str == "terrainnodeposition") return AttributeType::TERRAINNODEPOSITION;
    if (str == "terrainnodesize") return AttributeType::TERRAINNODESIZE;
    if (str == "terrainnoderange") return AttributeType::TERRAINNODERANGE;
    if (str == "terrainnoderesolution") return AttributeType::TERRAINNODERESOLUTION;
    return AttributeType::POSITION; // Default
}

// AttributeDataType enum conversion
std::string editor::Stream::attributeDataTypeToString(AttributeDataType type) {
    switch (type) {
        case AttributeDataType::BYTE: return "byte";
        case AttributeDataType::UNSIGNED_BYTE: return "unsigned_byte";
        case AttributeDataType::SHORT: return "short";
        case AttributeDataType::UNSIGNED_SHORT: return "unsigned_short";
        case AttributeDataType::INT: return "int";
        case AttributeDataType::UNSIGNED_INT: return "unsigned_int";
        case AttributeDataType::FLOAT: return "float";
        default: return "float"; // Default
    }
}

AttributeDataType editor::Stream::stringToAttributeDataType(const std::string& str) {
    if (str == "byte") return AttributeDataType::BYTE;
    if (str == "unsigned_byte") return AttributeDataType::UNSIGNED_BYTE;
    if (str == "short") return AttributeDataType::SHORT;
    if (str == "unsigned_short") return AttributeDataType::UNSIGNED_SHORT;
    if (str == "int") return AttributeDataType::INT;
    if (str == "unsigned_int") return AttributeDataType::UNSIGNED_INT;
    if (str == "float") return AttributeDataType::FLOAT;
    return AttributeDataType::FLOAT; // Default
}

// CullingMode enum conversion
std::string editor::Stream::cullingModeToString(CullingMode mode) {
    switch (mode) {
        case CullingMode::BACK: return "back";
        case CullingMode::FRONT: return "front";
        default: return "back";
    }
}

CullingMode editor::Stream::stringToCullingMode(const std::string& str) {
    if (str == "back") return CullingMode::BACK;
    if (str == "front") return CullingMode::FRONT;
    return CullingMode::BACK;
}

// WindingOrder enum conversion
std::string editor::Stream::windingOrderToString(WindingOrder order) {
    switch (order) {
        case WindingOrder::CCW: return "ccw";
        case WindingOrder::CW: return "cw";
        default: return "ccw";
    }
}

WindingOrder editor::Stream::stringToWindingOrder(const std::string& str) {
    if (str == "ccw") return WindingOrder::CCW;
    if (str == "cw") return WindingOrder::CW;
    return WindingOrder::CCW;
}

// TextureFilter enum conversion
std::string editor::Stream::textureFilterToString(TextureFilter filter) {
    switch (filter) {
        case TextureFilter::NEAREST: return "nearest";
        case TextureFilter::LINEAR: return "linear";
        case TextureFilter::NEAREST_MIPMAP_NEAREST: return "nearest_mipmap_nearest";
        case TextureFilter::NEAREST_MIPMAP_LINEAR: return "nearest_mipmap_linear";
        case TextureFilter::LINEAR_MIPMAP_NEAREST: return "linear_mipmap_nearest";
        case TextureFilter::LINEAR_MIPMAP_LINEAR: return "linear_mipmap_linear";
        default: return "linear";
    }
}

TextureFilter editor::Stream::stringToTextureFilter(const std::string& str) {
    if (str == "nearest") return TextureFilter::NEAREST;
    if (str == "linear") return TextureFilter::LINEAR;
    if (str == "nearest_mipmap_nearest") return TextureFilter::NEAREST_MIPMAP_NEAREST;
    if (str == "nearest_mipmap_linear") return TextureFilter::NEAREST_MIPMAP_LINEAR;
    if (str == "linear_mipmap_nearest") return TextureFilter::LINEAR_MIPMAP_NEAREST;
    if (str == "linear_mipmap_linear") return TextureFilter::LINEAR_MIPMAP_LINEAR;
    return TextureFilter::LINEAR;
}

// TextureWrap enum conversion
std::string editor::Stream::textureWrapToString(TextureWrap wrap) {
    switch (wrap) {
        case TextureWrap::REPEAT: return "repeat";
        case TextureWrap::MIRRORED_REPEAT: return "mirrored_repeat";
        case TextureWrap::CLAMP_TO_EDGE: return "clamp_to_edge";
        case TextureWrap::CLAMP_TO_BORDER: return "clamp_to_border";
        default: return "repeat";
    }
}

TextureWrap editor::Stream::stringToTextureWrap(const std::string& str) {
    if (str == "repeat") return TextureWrap::REPEAT;
    if (str == "mirrored_repeat") return TextureWrap::MIRRORED_REPEAT;
    if (str == "clamp_to_edge") return TextureWrap::CLAMP_TO_EDGE;
    if (str == "clamp_to_border") return TextureWrap::CLAMP_TO_BORDER;
    return TextureWrap::REPEAT;
}

std::string editor::Stream::anchorPresetToString(AnchorPreset preset) {
    switch (preset) {
        case AnchorPreset::NONE: return "NONE";
        case AnchorPreset::TOP_LEFT: return "TOP_LEFT";
        case AnchorPreset::TOP_RIGHT: return "TOP_RIGHT";
        case AnchorPreset::BOTTOM_LEFT: return "BOTTOM_LEFT";
        case AnchorPreset::BOTTOM_RIGHT: return "BOTTOM_RIGHT";
        case AnchorPreset::CENTER_LEFT: return "CENTER_LEFT";
        case AnchorPreset::CENTER_TOP: return "CENTER_TOP";
        case AnchorPreset::CENTER_RIGHT: return "CENTER_RIGHT";
        case AnchorPreset::CENTER_BOTTOM: return "CENTER_BOTTOM";
        case AnchorPreset::CENTER: return "CENTER";
        case AnchorPreset::LEFT_WIDE: return "LEFT_WIDE";
        case AnchorPreset::TOP_WIDE: return "TOP_WIDE";
        case AnchorPreset::RIGHT_WIDE: return "RIGHT_WIDE";
        case AnchorPreset::BOTTOM_WIDE: return "BOTTOM_WIDE";
        case AnchorPreset::VERTICAL_CENTER_WIDE: return "VERTICAL_CENTER_WIDE";
        case AnchorPreset::HORIZONTAL_CENTER_WIDE: return "HORIZONTAL_CENTER_WIDE";
        case AnchorPreset::FULL_LAYOUT: return "FULL_LAYOUT";
        default: return "NONE";
    }
}

AnchorPreset editor::Stream::stringToAnchorPreset(const std::string& str) {
    if (str == "NONE") return AnchorPreset::NONE;
    if (str == "TOP_LEFT") return AnchorPreset::TOP_LEFT;
    if (str == "TOP_RIGHT") return AnchorPreset::TOP_RIGHT;
    if (str == "BOTTOM_LEFT") return AnchorPreset::BOTTOM_LEFT;
    if (str == "BOTTOM_RIGHT") return AnchorPreset::BOTTOM_RIGHT;
    if (str == "CENTER_LEFT") return AnchorPreset::CENTER_LEFT;
    if (str == "CENTER_TOP") return AnchorPreset::CENTER_TOP;
    if (str == "CENTER_RIGHT") return AnchorPreset::CENTER_RIGHT;
    if (str == "CENTER_BOTTOM") return AnchorPreset::CENTER_BOTTOM;
    if (str == "CENTER") return AnchorPreset::CENTER;
    if (str == "LEFT_WIDE") return AnchorPreset::LEFT_WIDE;
    if (str == "TOP_WIDE") return AnchorPreset::TOP_WIDE;
    if (str == "RIGHT_WIDE") return AnchorPreset::RIGHT_WIDE;
    if (str == "BOTTOM_WIDE") return AnchorPreset::BOTTOM_WIDE;
    if (str == "VERTICAL_CENTER_WIDE") return AnchorPreset::VERTICAL_CENTER_WIDE;
    if (str == "HORIZONTAL_CENTER_WIDE") return AnchorPreset::HORIZONTAL_CENTER_WIDE;
    if (str == "FULL_LAYOUT") return AnchorPreset::FULL_LAYOUT;
    return AnchorPreset::NONE;
}

std::string editor::Stream::materialAlphaModeToString(MaterialAlphaMode mode) {
    switch (mode) {
        case MaterialAlphaMode::AUTO: return "AUTO";
        case MaterialAlphaMode::ALPHA_OPAQUE: return "ALPHA_OPAQUE";
        case MaterialAlphaMode::MASK: return "MASK";
        case MaterialAlphaMode::BLEND: return "BLEND";
        default: return "AUTO";
    }
}

MaterialAlphaMode editor::Stream::stringToMaterialAlphaMode(const std::string& str) {
    if (str == "AUTO") return MaterialAlphaMode::AUTO;
    if (str == "ALPHA_OPAQUE") return MaterialAlphaMode::ALPHA_OPAQUE;
    if (str == "MASK") return MaterialAlphaMode::MASK;
    if (str == "BLEND") return MaterialAlphaMode::BLEND;
    return MaterialAlphaMode::AUTO;
}

std::string editor::Stream::scriptPropertyTypeToString(ScriptPropertyType type) {
    switch (type) {
        case ScriptPropertyType::Bool: return "Bool";
        case ScriptPropertyType::Int: return "Int";
        case ScriptPropertyType::Float: return "Float";
        case ScriptPropertyType::String: return "String";
        case ScriptPropertyType::Vector2: return "Vector2";
        case ScriptPropertyType::Vector3: return "Vector3";
        case ScriptPropertyType::Vector4: return "Vector4";
        case ScriptPropertyType::Color3: return "Color3";
        case ScriptPropertyType::Color4: return "Color4";
        case ScriptPropertyType::EntityReference: return "EntityReference";
        default: return "Bool";
    }
}

ScriptPropertyType editor::Stream::stringToScriptPropertyType(const std::string& str) {
    if (str == "Bool") return ScriptPropertyType::Bool;
    if (str == "Int") return ScriptPropertyType::Int;
    if (str == "Float") return ScriptPropertyType::Float;
    if (str == "String") return ScriptPropertyType::String;
    if (str == "Vector2") return ScriptPropertyType::Vector2;
    if (str == "Vector3") return ScriptPropertyType::Vector3;
    if (str == "Vector4") return ScriptPropertyType::Vector4;
    if (str == "Color3") return ScriptPropertyType::Color3;
    if (str == "Color4") return ScriptPropertyType::Color4;
    if (str == "EntityReference") return ScriptPropertyType::EntityReference;
    return ScriptPropertyType::Bool;
}

std::string editor::Stream::particleEmitterShapeToString(ParticleEmitterShape shape) {
    switch (shape) {
        case ParticleEmitterShape::Box: return "Box";
        case ParticleEmitterShape::Sphere: return "Sphere";
        case ParticleEmitterShape::Hemisphere: return "Hemisphere";
        case ParticleEmitterShape::Circle: return "Circle";
        case ParticleEmitterShape::Cone: return "Cone";
        default: return "Box";
    }
}

ParticleEmitterShape editor::Stream::stringToParticleEmitterShape(const std::string& str) {
    if (str == "Box") return ParticleEmitterShape::Box;
    if (str == "Sphere") return ParticleEmitterShape::Sphere;
    if (str == "Hemisphere") return ParticleEmitterShape::Hemisphere;
    if (str == "Circle") return ParticleEmitterShape::Circle;
    if (str == "Cone") return ParticleEmitterShape::Cone;
    return ParticleEmitterShape::Box;
}

namespace {

std::string terrainBrushModeToString(editor::TerrainBrushMode mode) {
    switch (mode) {
        case editor::TerrainBrushMode::Raise: return "Raise";
        case editor::TerrainBrushMode::Lower: return "Lower";
        case editor::TerrainBrushMode::Smooth: return "Smooth";
        case editor::TerrainBrushMode::Flatten: return "Flatten";
        case editor::TerrainBrushMode::PaintRed: return "PaintRed";
        case editor::TerrainBrushMode::PaintGreen: return "PaintGreen";
        case editor::TerrainBrushMode::PaintBlue: return "PaintBlue";
        default: return "Raise";
    }
}

editor::TerrainBrushMode stringToTerrainBrushMode(const std::string& str) {
    if (str == "Raise") return editor::TerrainBrushMode::Raise;
    if (str == "Lower") return editor::TerrainBrushMode::Lower;
    if (str == "Smooth") return editor::TerrainBrushMode::Smooth;
    if (str == "Flatten") return editor::TerrainBrushMode::Flatten;
    if (str == "PaintRed") return editor::TerrainBrushMode::PaintRed;
    if (str == "PaintGreen") return editor::TerrainBrushMode::PaintGreen;
    if (str == "PaintBlue") return editor::TerrainBrushMode::PaintBlue;
    return editor::TerrainBrushMode::Raise;
}

std::string terrainBrushShapeToString(editor::TerrainBrushShape shape) {
    switch (shape) {
        case editor::TerrainBrushShape::Circle: return "Circle";
        case editor::TerrainBrushShape::Square: return "Square";
        default: return "Circle";
    }
}

editor::TerrainBrushShape stringToTerrainBrushShape(const std::string& str) {
    if (str == "Circle") return editor::TerrainBrushShape::Circle;
    if (str == "Square") return editor::TerrainBrushShape::Square;
    return editor::TerrainBrushShape::Circle;
}

std::string terrainBrushFalloffToString(editor::TerrainBrushFalloff falloff) {
    switch (falloff) {
        case editor::TerrainBrushFalloff::Smooth: return "Smooth";
        case editor::TerrainBrushFalloff::Linear: return "Linear";
        case editor::TerrainBrushFalloff::Constant: return "Constant";
        default: return "Smooth";
    }
}

editor::TerrainBrushFalloff stringToTerrainBrushFalloff(const std::string& str) {
    if (str == "Smooth") return editor::TerrainBrushFalloff::Smooth;
    if (str == "Linear") return editor::TerrainBrushFalloff::Linear;
    if (str == "Constant") return editor::TerrainBrushFalloff::Constant;
    return editor::TerrainBrushFalloff::Smooth;
}

} // namespace

std::string editor::Stream::containerTypeToString(ContainerType type) {
    switch (type) {
        case ContainerType::VERTICAL: return "vertical";
        case ContainerType::HORIZONTAL: return "horizontal";
        case ContainerType::VERTICAL_WRAP: return "vertical_wrap";
        case ContainerType::HORIZONTAL_WRAP: return "horizontal_wrap";
        default: return "vertical";
    }
}

ContainerType editor::Stream::stringToContainerType(const std::string& str) {
    if (str == "vertical") return ContainerType::VERTICAL;
    if (str == "horizontal") return ContainerType::HORIZONTAL;
    if (str == "vertical_wrap") return ContainerType::VERTICAL_WRAP;
    if (str == "horizontal_wrap") return ContainerType::HORIZONTAL_WRAP;
    return ContainerType::VERTICAL;
}

std::string editor::Stream::scrollbarTypeToString(ScrollbarType type) {
    switch (type) {
        case ScrollbarType::VERTICAL: return "vertical";
        case ScrollbarType::HORIZONTAL: return "horizontal";
        default: return "vertical";
    }
}

ScrollbarType editor::Stream::stringToScrollbarType(const std::string& str) {
    if (str == "horizontal") return ScrollbarType::HORIZONTAL;
    return ScrollbarType::VERTICAL;
}

std::string editor::Stream::progressbarTypeToString(ProgressbarType type) {
    switch (type) {
        case ProgressbarType::VERTICAL: return "vertical";
        case ProgressbarType::HORIZONTAL: return "horizontal";
        default: return "horizontal";
    }
}

ProgressbarType editor::Stream::stringToProgressbarType(const std::string& str) {
    if (str == "vertical") return ProgressbarType::VERTICAL;
    return ProgressbarType::HORIZONTAL;
}

std::string editor::Stream::scalingModeToString(Scaling mode) {
    switch (mode) {
        case Scaling::FITWIDTH: return "fitwidth";
        case Scaling::FITHEIGHT: return "fitheight";
        case Scaling::LETTERBOX: return "letterbox";
        case Scaling::CROP: return "crop";
        case Scaling::STRETCH: return "stretch";
        case Scaling::NATIVE: return "native";
        default: return "fitwidth";
    }
}

Scaling editor::Stream::stringToScalingMode(const std::string& str) {
    if (str == "fitwidth") return Scaling::FITWIDTH;
    if (str == "fitheight") return Scaling::FITHEIGHT;
    if (str == "letterbox") return Scaling::LETTERBOX;
    if (str == "crop") return Scaling::CROP;
    if (str == "stretch") return Scaling::STRETCH;
    if (str == "native") return Scaling::NATIVE;
    return Scaling::FITWIDTH;
}

std::string editor::Stream::textureStrategyToString(TextureStrategy strategy) {
    switch (strategy) {
        case TextureStrategy::FIT: return "fit";
        case TextureStrategy::RESIZE: return "resize";
        case TextureStrategy::NONE: return "none";
        default: return "resize";
    }
}

TextureStrategy editor::Stream::stringToTextureStrategy(const std::string& str) {
    if (str == "fit") return TextureStrategy::FIT;
    if (str == "resize") return TextureStrategy::RESIZE;
    if (str == "none") return TextureStrategy::NONE;
    return TextureStrategy::RESIZE;
}

std::string editor::Stream::windowModeToString(WindowMode mode) {
    switch (mode) {
        case WindowMode::WINDOWED: return "windowed";
        case WindowMode::MAXIMIZED: return "maximized";
        case WindowMode::FULLSCREEN: return "fullscreen";
        default: return "windowed";
    }
}

editor::WindowMode editor::Stream::stringToWindowMode(const std::string& str) {
    if (str == "windowed") return WindowMode::WINDOWED;
    if (str == "maximized") return WindowMode::MAXIMIZED;
    if (str == "fullscreen") return WindowMode::FULLSCREEN;
    return WindowMode::WINDOWED;
}

std::string editor::Stream::lightTypeToString(LightType type) {
    switch (type) {
        case LightType::DIRECTIONAL: return "directional";
        case LightType::POINT: return "point";
        case LightType::SPOT: return "spot";
        default: return "directional";
    }
}

LightType editor::Stream::stringToLightType(const std::string& str) {
    if (str == "directional") return LightType::DIRECTIONAL;
    if (str == "point") return LightType::POINT;
    if (str == "spot") return LightType::SPOT;
    return LightType::DIRECTIONAL; // Default
}

std::string editor::Stream::fogTypeToString(FogType type) {
    switch (type) {
        case FogType::LINEAR: return "linear";
        case FogType::EXPONENTIAL: return "exponential";
        case FogType::EXPONENTIALSQUARED: return "exponential_squared";
        default: return "linear";
    }
}

FogType editor::Stream::stringToFogType(const std::string& str) {
    if (str == "linear") return FogType::LINEAR;
    if (str == "exponential") return FogType::EXPONENTIAL;
    if (str == "exponential_squared" || str == "exponentialsquared" || str == "exponentialSquared") return FogType::EXPONENTIALSQUARED;
    return FogType::LINEAR;
}

static std::string shadowQualityToString(ShadowQuality quality) {
    switch (quality) {
        case ShadowQuality::NONE: return "none";
        case ShadowQuality::LOW: return "low";
        case ShadowQuality::MEDIUM: return "medium";
        case ShadowQuality::HIGH: return "high";
    }
    return "low";
}

static ShadowQuality stringToShadowQuality(const std::string& str) {
    if (str == "none") return ShadowQuality::NONE;
    if (str == "medium") return ShadowQuality::MEDIUM;
    if (str == "high") return ShadowQuality::HIGH;
    return ShadowQuality::LOW; // Default
}

std::string editor::Stream::lightStateToString(LightState state) {
    switch (state) {
        case LightState::OFF: return "off";
        case LightState::ON: return "on";
        case LightState::AUTO: return "auto";
        default: return "auto";
    }
}

LightState editor::Stream::stringToLightState(const std::string& str) {
    if (str == "off") return LightState::OFF;
    if (str == "on") return LightState::ON;
    if (str == "auto") return LightState::AUTO;
    return LightState::AUTO; // Default
}

std::string editor::Stream::soundStateToString(SoundState state) {
    switch (state) {
        case SoundState::Playing: return "playing";
        case SoundState::Paused: return "paused";
        case SoundState::Stopped: return "stopped";
        default: return "stopped";
    }
}

SoundState editor::Stream::stringToSoundState(const std::string& str) {
    if (str == "playing") return SoundState::Playing;
    if (str == "paused") return SoundState::Paused;
    if (str == "stopped") return SoundState::Stopped;
    return SoundState::Stopped;
}

std::string editor::Stream::soundAttenuationToString(SoundAttenuation attenuation) {
    switch (attenuation) {
        case SoundAttenuation::NO_ATTENUATION: return "no_attenuation";
        case SoundAttenuation::INVERSE_DISTANCE: return "inverse_distance";
        case SoundAttenuation::LINEAR_DISTANCE: return "linear_distance";
        case SoundAttenuation::EXPONENTIAL_DISTANCE: return "exponential_distance";
        default: return "no_attenuation";
    }
}

SoundAttenuation editor::Stream::stringToSoundAttenuation(const std::string& str) {
    if (str == "no_attenuation") return SoundAttenuation::NO_ATTENUATION;
    if (str == "inverse_distance") return SoundAttenuation::INVERSE_DISTANCE;
    if (str == "linear_distance") return SoundAttenuation::LINEAR_DISTANCE;
    if (str == "exponential_distance") return SoundAttenuation::EXPONENTIAL_DISTANCE;
    return SoundAttenuation::NO_ATTENUATION;
}

std::string editor::Stream::uiEventStateToString(UIEventState state) {
    switch (state) {
        case UIEventState::NOT_SET: return "not_set";
        case UIEventState::ENABLED: return "true";
        case UIEventState::DISABLED: return "false";
        default: return "not_set";
    }
}

UIEventState editor::Stream::stringToUIEventState(const std::string& str) {
    if (str == "not_set") return UIEventState::NOT_SET;
    if (str == "true") return UIEventState::ENABLED;
    if (str == "false") return UIEventState::DISABLED;
    return UIEventState::NOT_SET; // Default
}

std::string editor::Stream::cameraTypeToString(CameraType type) {
    switch (type) {
        case CameraType::CAMERA_UI: return "camera_ui";
        case CameraType::CAMERA_ORTHO: return "camera_ortho";
        case CameraType::CAMERA_PERSPECTIVE: return "camera_perspective";
        default: return "camera_perspective";
    }
}

CameraType editor::Stream::stringToCameraType(const std::string& str) {
    if (str == "camera_ui") return CameraType::CAMERA_UI;
    if (str == "camera_ortho") return CameraType::CAMERA_ORTHO;
    if (str == "camera_perspective") return CameraType::CAMERA_PERSPECTIVE;
    return CameraType::CAMERA_PERSPECTIVE;
}

std::string editor::Stream::pivotPresetToString(PivotPreset preset) {
    switch (preset) {
        case PivotPreset::CENTER:
            return "center";
        case PivotPreset::TOP_CENTER:
            return "top_center";
        case PivotPreset::BOTTOM_CENTER:
            return "bottom_center";
        case PivotPreset::LEFT_CENTER:
            return "left_center";
        case PivotPreset::RIGHT_CENTER:
            return "right_center";
        case PivotPreset::TOP_LEFT:
            return "top_left";
        case PivotPreset::BOTTOM_LEFT:
            return "bottom_left";
        case PivotPreset::TOP_RIGHT:
            return "top_right";
        case PivotPreset::BOTTOM_RIGHT:
            return "bottom_right";
        default:
            return "bottom_left";
    }
}

PivotPreset editor::Stream::stringToPivotPreset(const std::string& str) {
    if (str == "center") return PivotPreset::CENTER;
    if (str == "top_center") return PivotPreset::TOP_CENTER;
    if (str == "bottom_center") return PivotPreset::BOTTOM_CENTER;
    if (str == "left_center") return PivotPreset::LEFT_CENTER;
    if (str == "right_center") return PivotPreset::RIGHT_CENTER;
    if (str == "top_left") return PivotPreset::TOP_LEFT;
    if (str == "bottom_left") return PivotPreset::BOTTOM_LEFT;
    if (str == "top_right") return PivotPreset::TOP_RIGHT;
    if (str == "bottom_right") return PivotPreset::BOTTOM_RIGHT;
    return PivotPreset::BOTTOM_LEFT;
}

std::string editor::Stream::scriptTypeToString(ScriptType type) {
    switch (type) {
        case ScriptType::CPP: return "cpp";
        case ScriptType::LUA: return "lua";
        default:              return "cpp";
    }
}

ScriptType editor::Stream::stringToScriptType(const std::string& str) {
    if (str == "lua") return ScriptType::LUA;
    if (str == "cpp") return ScriptType::CPP;

    // Backward compatibility: projects saved before ScriptType was reduced to
    // CPP/LUA used these three values. Remove these branches once support for
    // scenes written by those editor versions is intentionally dropped.
    if (str == "script_lua") return ScriptType::LUA;
    if (str == "subclass" || str == "script_class") return ScriptType::CPP;

    return ScriptType::CPP;
}

YAML::Node editor::Stream::encodeVector2(const Vector2& vec){
    YAML::Node node;
    node.SetStyle(YAML::EmitterStyle::Flow);
    node.push_back(finiteOr(vec.x));
    node.push_back(finiteOr(vec.y));
    return node;
}

Vector2 editor::Stream::decodeVector2(const YAML::Node& node) {
    if (!node || !node.IsSequence() || node.size() < 2) {
        return Vector2();
    }

    return Vector2(decodeFinite(node[0], 0.0f), decodeFinite(node[1], 0.0f));
}

YAML::Node editor::Stream::encodeVector3(const Vector3& vec) {
    YAML::Node node;
    node.SetStyle(YAML::EmitterStyle::Flow);
    node.push_back(finiteOr(vec.x));
    node.push_back(finiteOr(vec.y));
    node.push_back(finiteOr(vec.z));
    return node;
}

Vector3 editor::Stream::decodeVector3(const YAML::Node& node) {
    if (!node || !node.IsSequence() || node.size() < 3) {
        return Vector3();
    }

    return Vector3(decodeFinite(node[0], 0.0f), decodeFinite(node[1], 0.0f), decodeFinite(node[2], 0.0f));
}

YAML::Node editor::Stream::encodeVector4(const Vector4& vec){
    YAML::Node node;
    node.SetStyle(YAML::EmitterStyle::Flow);
    node.push_back(finiteOr(vec.x));
    node.push_back(finiteOr(vec.y));
    node.push_back(finiteOr(vec.z));
    node.push_back(finiteOr(vec.w));
    return node;
}

Vector4 editor::Stream::decodeVector4(const YAML::Node& node) {
    if (!node || !node.IsSequence() || node.size() < 4) {
        return Vector4();
    }

    return Vector4(decodeFinite(node[0], 0.0f), decodeFinite(node[1], 0.0f), decodeFinite(node[2], 0.0f), decodeFinite(node[3], 0.0f));
}

YAML::Node editor::Stream::encodeQuaternion(const Quaternion& quat) {
    YAML::Node node;
    node.SetStyle(YAML::EmitterStyle::Flow);
    node.push_back(finiteOr(quat.w, 1.0f));
    node.push_back(finiteOr(quat.x));
    node.push_back(finiteOr(quat.y));
    node.push_back(finiteOr(quat.z));
    return node;
}

Quaternion editor::Stream::decodeQuaternion(const YAML::Node& node) {
    if (!node || !node.IsSequence() || node.size() < 4) {
        return Quaternion::IDENTITY;
    }

    return Quaternion(decodeFinite(node[0], 1.0f), decodeFinite(node[1], 0.0f), decodeFinite(node[2], 0.0f), decodeFinite(node[3], 0.0f));
}

YAML::Node editor::Stream::encodeRect(const Rect& rect) {
    YAML::Node node;
    node.SetStyle(YAML::EmitterStyle::Flow);
    node.push_back(finiteOr(rect.getX()));
    node.push_back(finiteOr(rect.getY()));
    node.push_back(finiteOr(rect.getWidth()));
    node.push_back(finiteOr(rect.getHeight()));
    return node;
}

Rect editor::Stream::decodeRect(const YAML::Node& node) {
    if (!node || !node.IsSequence() || node.size() < 4) {
        return Rect();
    }

    return Rect(decodeFinite(node[0], 0.0f), decodeFinite(node[1], 0.0f), decodeFinite(node[2], 0.0f), decodeFinite(node[3], 0.0f));
}

YAML::Node editor::Stream::encodeMatrix4(const Matrix4& mat) {
    YAML::Node node;
    node.SetStyle(YAML::EmitterStyle::Flow);
    Matrix4 safeMatrix = mat.isValid() ? mat : Matrix4();
    for (int i = 0; i < 4; i++) {
        YAML::Node row;
        row.SetStyle(YAML::EmitterStyle::Flow);
        for (int j = 0; j < 4; j++) {
            row.push_back(safeMatrix[i][j]);
        }
        node.push_back(row);
    }
    return node;
}

Matrix4 editor::Stream::decodeMatrix4(const YAML::Node& node) {
    Matrix4 mat;
    if (!node || !node.IsSequence() || node.size() < 4) {
        return mat;
    }

    for (int i = 0; i < 4; i++) {
        if (!node[i] || !node[i].IsSequence() || node[i].size() < 4) {
            return Matrix4();
        }

        for (int j = 0; j < 4; j++) {
            mat[i][j] = decodeFinite(node[i][j], i == j ? 1.0f : 0.0f);
        }
    }
    return mat;
}

YAML::Node editor::Stream::encodeTexture(const Texture& texture, bool embedData) {
    YAML::Node node;

    // camera render-to-texture link; checked before empty() because an
    // unresolved link has no framebuffer bound yet
    Entity linkedCamera = CameraTextureLink::parse(texture.getId());
    if (linkedCamera != NULL_ENTITY) {
        node["textureType"] = "2D";
        node["source"] = "camera";
        node["camera"] = linkedCamera;
        return node;
    }

    if (texture.empty() || texture.isFramebuffer())
        return node;

    const bool isCube = texture.isCubeMap() || (texture.getNumFaces() == 6);
    node["textureType"] = isCube ? "CUBE" : "2D";

    // Source selection: prefer filesystem paths; fall back to id only when there is no path.
    if (!isCube) {
        const std::string path = texture.getPath(0);
        if (!path.empty()) {
            node["source"] = "path";
            node["path"] = path;
        } else {
            TextureData* embeddedData = nullptr;
            if (embedData && getEmbeddedTextureData(texture, embeddedData)) {
                node["source"] = "data";
                node["id"] = texture.getId();
                node["width"] = embeddedData->getWidth();
                node["height"] = embeddedData->getHeight();
                node["channels"] = embeddedData->getChannels();
                node["size"] = embeddedData->getSize();
                node["colorFormat"] = colorFormatToString(embeddedData->getColorFormat());
                node["data"] = Base64::encode(static_cast<const unsigned char*>(embeddedData->getData()), embeddedData->getSize());
            } else {
                node["source"] = "id";
                node["id"] = texture.getId();
            }
        }
    } else {
        bool hasAnyNonZeroFace = false;
        bool hasAnyEmptyFace = false;
        for (int f = 1; f < 6; f++) {
            if (!texture.getPath((size_t)f).empty()) hasAnyNonZeroFace = true;
            if (texture.getPath((size_t)f).empty()) hasAnyEmptyFace = true;
        }

        const bool singleFileCube = !texture.getPath(0).empty() && !hasAnyNonZeroFace;
        if (singleFileCube) {
            node["cubeMode"] = "single";
            node["source"] = "path";
            node["path"] = texture.getPath(0);
        } else {
            node["cubeMode"] = "faces";
            node["source"] = "paths";
            YAML::Node pathsNode;
            pathsNode.SetStyle(YAML::EmitterStyle::Flow);
            for (int f = 0; f < 6; f++) {
                pathsNode.push_back(texture.getPath((size_t)f));
            }
            node["paths"] = pathsNode;
            node["hasEmptyFaces"] = hasAnyEmptyFace;
        }
    }

    node["minFilter"] = textureFilterToString(texture.getMinFilter());
    node["magFilter"] = textureFilterToString(texture.getMagFilter());
    node["wrapU"] = textureWrapToString(texture.getWrapU());
    node["wrapV"] = textureWrapToString(texture.getWrapV());
    if (texture.getSvgScale() != 1.0f) node["svgScale"] = texture.getSvgScale();
    node["releaseDataAfterLoad"] = texture.isReleaseDataAfterLoad();
    return node;
}

Texture editor::Stream::decodeTexture(const YAML::Node& node) {
    Texture texture;
    if (node.IsMap()) { // Check if node has data
        const std::string textureType = node["textureType"] ? node["textureType"].as<std::string>() : std::string();
        const std::string source = node["source"] ? node["source"].as<std::string>() : std::string();

        if (textureType == "CUBE") {
            const std::string cubeMode = node["cubeMode"] ? node["cubeMode"].as<std::string>() : std::string();
            if (cubeMode == "single") {
                if (node["path"]) {
                    texture.setCubeMap(node["path"].as<std::string>());
                }
            } else {
                if (node["paths"] && node["paths"].IsSequence() && node["paths"].size() == 6) {
                    std::array<std::string, 6> paths;
                    for (size_t f = 0; f < 6; f++) {
                        paths[f] = node["paths"][f].as<std::string>();
                    }

                    // Set per-face paths individually (supports incomplete cubemaps).
                    for (size_t f = 0; f < 6; f++) {
                        texture.setCubePath(f, paths[f]);
                    }
                }
            }
        } else if (textureType == "2D") {
            if (source == "path") {
                if (node["path"]) {
                    texture.setPath(node["path"].as<std::string>());
                }
            } else if (source == "data") {
                if (node["data"] && node["width"] && node["height"] && node["channels"]) {
                    std::vector<unsigned char> decodedData = Base64::decode(node["data"].as<std::string>());
                    const int width = node["width"].as<int>();
                    const int height = node["height"].as<int>();
                    const int channels = node["channels"].as<int>();
                    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
                    const size_t dataSize = node["size"] ? node["size"].as<size_t>() : expectedSize;
                    const std::string colorFormat = node["colorFormat"] ? node["colorFormat"].as<std::string>() : std::string();
                    const std::string textureId = node["id"] ? node["id"].as<std::string>() : std::string();

                    if (dataSize == expectedSize && decodedData.size() >= dataSize) {
                        setEmbeddedTextureData(texture, textureId, width, height, stringToColorFormat(colorFormat, channels), channels, decodedData);
                    }
                }
            } else if (source == "id") {
                if (node["id"]) {
                    texture.setId(node["id"].as<std::string>());
                }
            } else if (source == "camera") {
                // framebuffer is bound later by CameraTextureLink::resolve,
                // after every entity of the scene exists
                if (node["camera"]) {
                    texture.setId(CameraTextureLink::makeId(node["camera"].as<Entity>()));
                }
            }
        }

        if (node["minFilter"]) texture.setMinFilter(stringToTextureFilter(node["minFilter"].as<std::string>()));
        if (node["magFilter"]) texture.setMagFilter(stringToTextureFilter(node["magFilter"].as<std::string>()));
        if (node["wrapU"]) texture.setWrapU(stringToTextureWrap(node["wrapU"].as<std::string>()));
        if (node["wrapV"]) texture.setWrapV(stringToTextureWrap(node["wrapV"].as<std::string>()));
        // Old scenes carry the scale inside the path ("?svgScale=N"), absorbed by setPath()
        if (node["svgScale"]) texture.setSvgScale(node["svgScale"].as<float>());

        //if (node["isFramebuffer"] && node["isFramebuffer"].as<bool>()) {
        //    texture.setIsFramebuffer(true);
        //}

        //if (node["width"] && node["height"]) {
        //    texture.setWidth(node["width"].as<int>());
        //    texture.setHeight(node["height"].as<int>());
        //}

        if (node["releaseDataAfterLoad"]) texture.setReleaseDataAfterLoad(node["releaseDataAfterLoad"].as<bool>());
    }
    return texture;
}

YAML::Node editor::Stream::encodeBuffer(const Buffer& buffer) {
    YAML::Node node;

    // Encode buffer properties
    node["type"] = bufferTypeToString(buffer.getType());
    node["usage"] = bufferUsageToString(buffer.getUsage());
    node["stride"] = buffer.getStride();
    node["size"] = buffer.getSize();
    node["count"] = buffer.getCount();
    node["renderAttributes"] = buffer.isRenderAttributes();
    node["instanceBuffer"] = buffer.isInstanceBuffer();

    // Encode attributes
    YAML::Node attributesNode;
    for (const auto& [type, attr] : buffer.getAttributes()) {
        YAML::Node attrNode;
        attrNode["type"] = attributeTypeToString(type);
        attrNode["dataType"] = attributeDataTypeToString(attr.getDataType());
        attrNode["bufferName"] = attr.getBufferName();
        attrNode["elements"] = attr.getElements();
        attrNode["offset"] = attr.getOffset();
        attrNode["count"] = attr.getCount();
        attrNode["normalized"] = attr.getNormalized();
        attrNode["perInstance"] = attr.getPerInstance();
        attributesNode.push_back(attrNode);
    }
    node["attributes"] = attributesNode;

    // Encode buffer data if it exists
    if (buffer.getData() && buffer.getSize() > 0) {
        std::string base64Data = Base64::encode(buffer.getData(), buffer.getSize());
        node["data"] = base64Data;
    }

    return node;
}

void editor::Stream::decodeBuffer(Buffer& buffer, const YAML::Node& node) {
    if (!node.IsMap()) return;

    // Decode buffer properties
    buffer.setType(stringToBufferType(node["type"].as<std::string>()));
    buffer.setUsage(stringToBufferUsage(node["usage"].as<std::string>()));
    buffer.setStride(node["stride"].as<unsigned int>());
    buffer.setSize(node["size"].as<size_t>());
    buffer.setCount(node["count"].as<unsigned int>());
    buffer.setRenderAttributes(node["renderAttributes"].as<bool>());
    buffer.setInstanceBuffer(node["instanceBuffer"].as<bool>());

    // Decode attributes
    if (node["attributes"]) {
        for (const auto& attrNode : node["attributes"]) {
            AttributeType type = stringToAttributeType(attrNode["type"].as<std::string>());
            AttributeDataType dataType = stringToAttributeDataType(attrNode["dataType"].as<std::string>());
            std::string bufferName = attrNode["bufferName"].as<std::string>();
            unsigned int elements = attrNode["elements"].as<unsigned int>();
            size_t offset = attrNode["offset"].as<size_t>();
            unsigned int count = attrNode["count"].as<unsigned int>();
            bool normalized = attrNode["normalized"].as<bool>();
            bool perInstance = attrNode["perInstance"].as<bool>();

            Attribute attr(dataType, bufferName, elements, offset, count, normalized, perInstance);
            buffer.addAttribute(type, attr);
        }
    }

    // Decode buffer data if it exists
    if (node["data"]) {
        std::string base64Data = node["data"].as<std::string>();
        std::vector<unsigned char> decodedData = Base64::decode(base64Data);

        buffer.importData(decodedData.data(), decodedData.size());
    }
}

YAML::Node editor::Stream::encodeInterleavedBuffer(const InterleavedBuffer& buffer) {
    YAML::Node node = encodeBuffer(buffer);
    node["vertexSize"] = buffer.getVertexSize();
    return node;
}

void editor::Stream::decodeInterleavedBuffer(InterleavedBuffer& buffer, const YAML::Node& node) {
    decodeBuffer(buffer, node);

    if (node["vertexSize"]) {
        buffer.setVertexSize(node["vertexSize"].as<unsigned int>());
    }
}

YAML::Node editor::Stream::encodeIndexBuffer(const IndexBuffer& buffer) {
    YAML::Node node = encodeBuffer(buffer);
    return node;
}

void editor::Stream::decodeIndexBuffer(IndexBuffer& buffer, const YAML::Node& node) {
    decodeBuffer(buffer, node);
}

YAML::Node editor::Stream::encodeExternalBuffer(const ExternalBuffer& buffer) {
    YAML::Node node = encodeBuffer(buffer); // Use base Buffer encoding
    node["name"] = buffer.getName(); // Add ExternalBuffer specific property
    return node;
}

void editor::Stream::decodeExternalBuffer(ExternalBuffer& buffer, const YAML::Node& node) {
    decodeBuffer(buffer, node); // Use base Buffer decoding
    buffer.setName(node["name"].as<std::string>()); // Set ExternalBuffer specific property
}

YAML::Node editor::Stream::encodeSubmesh(const Submesh& submesh, bool embedTextureData) {
    YAML::Node node;

    node["material"] = encodeMaterial(submesh.material, embedTextureData);
    node["textureRect"] = encodeRect(submesh.textureRect);
    node["primitiveType"] = primitiveTypeToString(submesh.primitiveType);
    node["vertexCount"] = submesh.vertexCount;
    node["faceCulling"] = submesh.faceCulling;
    node["textureShadow"] = submesh.textureShadow;

    // The fields the next model load must keep instead of taking them from the file.
    if (submesh.overrideFields != 0) {
        node["overrideFields"] = submesh.overrideFields;
    }

    // Flags
    node["hasTexCoord1"] = submesh.hasTexCoord1;
    node["hasTexCoord2"] = submesh.hasTexCoord2;
    node["hasNormalMap"] = submesh.hasNormalMap;
    node["hasTangent"] = submesh.hasTangent;
    node["hasVertexColor3"] = submesh.hasVertexColor3;
    node["hasVertexColor4"] = submesh.hasVertexColor4;
    node["hasTextureRect"] = submesh.hasTextureRect;
    node["hasSkinning"] = submesh.hasSkinning;
    node["hasMorphTarget"] = submesh.hasMorphTarget;
    node["hasMorphNormal"] = submesh.hasMorphNormal;
    node["hasMorphTangent"] = submesh.hasMorphTangent;

    if (submesh.hasSkinningNormalization) {
        node["normAdjustJoint"] = submesh.normAdjustJoint;
        node["normAdjustWeight"] = submesh.normAdjustWeight;
    }

    return node;
}

Submesh editor::Stream::decodeSubmesh(const YAML::Node& node, const Submesh* oldSubmesh) {
    Submesh submesh;
    if (oldSubmesh) {
        submesh = *oldSubmesh;
    }

    submesh.material = decodeMaterial(node["material"]);
    submesh.textureRect = decodeRect(node["textureRect"]);
    submesh.primitiveType = stringToPrimitiveType(node["primitiveType"].as<std::string>());
    submesh.vertexCount = node["vertexCount"].as<uint32_t>();
    submesh.faceCulling = node["faceCulling"].as<bool>();
    submesh.textureShadow = node["textureShadow"].as<bool>();

    // Omitted at its default, so an absent key is a value, not a reason to keep the old one.
    submesh.overrideFields = node["overrideFields"] ? node["overrideFields"].as<uint32_t>() : 0;

    // Flags
    if (node["hasTexCoord1"]) submesh.hasTexCoord1 = node["hasTexCoord1"].as<bool>();
    if (node["hasTexCoord2"]) submesh.hasTexCoord2 = node["hasTexCoord2"].as<bool>();
    submesh.hasNormalMap = node["hasNormalMap"].as<bool>();
    submesh.hasTangent = node["hasTangent"].as<bool>();
    if (node["hasVertexColor3"]) submesh.hasVertexColor3 = node["hasVertexColor3"].as<bool>();
    if (node["hasVertexColor4"]) submesh.hasVertexColor4 = node["hasVertexColor4"].as<bool>();
    submesh.hasTextureRect = node["hasTextureRect"].as<bool>();
    submesh.hasSkinning = node["hasSkinning"].as<bool>();
    submesh.hasMorphTarget = node["hasMorphTarget"].as<bool>();
    submesh.hasMorphNormal = node["hasMorphNormal"].as<bool>();
    submesh.hasMorphTangent = node["hasMorphTangent"].as<bool>();

    submesh.hasSkinningNormalization = false;
    if (node["normAdjustJoint"] || node["normAdjustWeight"]) {
        submesh.normAdjustJoint = node["normAdjustJoint"] ? node["normAdjustJoint"].as<float>() : 1.0f;
        submesh.normAdjustWeight = node["normAdjustWeight"] ? node["normAdjustWeight"].as<float>() : 1.0f;
        submesh.hasSkinningNormalization = true;
    }

    return submesh;
}

YAML::Node editor::Stream::encodeAABB(const AABB& aabb) {
    YAML::Node node;
    node["min"] = encodeVector3(aabb.getMinimum());
    node["max"] = encodeVector3(aabb.getMaximum());
    return node;
}

AABB editor::Stream::decodeAABB(const YAML::Node& node) {
    Vector3 min = decodeVector3(node["min"]);
    Vector3 max = decodeVector3(node["max"]);
    return AABB(min, max);
}

YAML::Node editor::Stream::encodeSpriteFrameData(const SpriteFrameData& frameData) {
    YAML::Node node;
    node["name"] = frameData.name;
    node["rect"] = encodeRect(frameData.rect);
    return node;
}

SpriteFrameData editor::Stream::decodeSpriteFrameData(const YAML::Node& node) {
    SpriteFrameData frameData;
    if (node["name"]) frameData.name = node["name"].as<std::string>();
    if (node["rect"]) frameData.rect = decodeRect(node["rect"]);
    return frameData;
}

YAML::Node editor::Stream::encodeTileRectData(const TileRectData& tileRect) {
    YAML::Node node;
    node["name"] = tileRect.name;
    node["submeshId"] = tileRect.submeshId;
    node["rect"] = encodeRect(tileRect.rect);
    return node;
}

TileRectData editor::Stream::decodeTileRectData(const YAML::Node& node) {
    TileRectData tileRect;
    if (node["name"]) tileRect.name = node["name"].as<std::string>();
    if (node["submeshId"]) tileRect.submeshId = node["submeshId"].as<int>();
    if (node["rect"]) tileRect.rect = decodeRect(node["rect"]);
    return tileRect;
}

YAML::Node editor::Stream::encodeTileData(const TileData& tile) {
    YAML::Node node;
    node["name"] = tile.name;
    node["rectId"] = tile.rectId;
    node["position"] = encodeVector2(tile.position);
    node["width"] = tile.width;
    node["height"] = tile.height;
    return node;
}

TileData editor::Stream::decodeTileData(const YAML::Node& node) {
    TileData tile;
    if (node["name"]) tile.name = node["name"].as<std::string>();
    if (node["rectId"]) tile.rectId = node["rectId"].as<int>();
    if (node["position"]) tile.position = decodeVector2(node["position"]);
    if (node["width"]) tile.width = node["width"].as<float>();
    if (node["height"]) tile.height = node["height"].as<float>();
    return tile;
}

YAML::Node editor::Stream::encodeProject(Project* project) {
    YAML::Node root;

    root["name"] = project->getName();
    root["nextSceneId"] = project->getNextSceneId();
    root["selectedScene"] = project->getSelectedSceneId();

    root["canvasWidth"] = project->getCanvasWidth();
    root["canvasHeight"] = project->getCanvasHeight();

    root["scalingMode"] = scalingModeToString(project->getScalingMode());
    root["textureStrategy"] = textureStrategyToString(project->getTextureStrategy());
    root["vsync"] = project->isVSyncEnabled();

    root["windowMode"] = windowModeToString(project->getWindowMode());
    root["windowWidth"] = project->getWindowWidth();
    root["windowHeight"] = project->getWindowHeight();
    root["windowResizable"] = project->isWindowResizable();
    if (!project->getWindowTitle().empty()) {
        root["windowTitle"] = project->getWindowTitle();
    }
    if (!project->getWindowIcon().empty()) {
        root["windowIcon"] = project->getWindowIcon().string();
    }

    if (!project->getAssetsDir().empty() && project->getAssetsDir() != ".") {
        root["assetsDir"] = project->getAssetsDir().string();
    }
    if (!project->getLuaDir().empty()) {
        root["luaDir"] = project->getLuaDir().string();
    }
    if (!project->getScriptDirs().empty()) {
        YAML::Node scriptDirsNode;
        for (const fs::path& scriptDir : project->getScriptDirs()) {
            scriptDirsNode.push_back(scriptDir.generic_string());
        }
        root["scriptDirs"] = scriptDirsNode;
    }
    if (!project->getCMakeCCompiler().empty()) {
        root["cmakeCCompiler"] = project->getCMakeCCompiler();
    }
    if (!project->getCMakeCxxCompiler().empty()) {
        root["cmakeCxxCompiler"] = project->getCMakeCxxCompiler();
    }
    if (!project->getCMakeGenerator().empty()) {
        root["cmakeGenerator"] = project->getCMakeGenerator();
    }
    if (project->getCMakeBuildJobs() != 0) {
        root["cmakeBuildJobs"] = project->getCMakeBuildJobs();
    }
    if (project->shouldPackNativeResources() != Project::defaultPackNativeResources) {
        root["packNativeResources"] = project->shouldPackNativeResources();
    }

    if (project->getStartSceneId() != NULL_PROJECT_SCENE) {
        root["startSceneId"] = project->getStartSceneId();
    }

    if (!project->getStandaloneBundles().empty()) {
        YAML::Node bundlesNode;
        for (const fs::path& bundlePath : project->getStandaloneBundles()) {
            bundlesNode.push_back(bundlePath.generic_string());
        }
        root["standaloneBundles"] = bundlesNode;
    }

    {
        const TerrainEditorSettings& ts = project->getTerrainEditorSettings();
        YAML::Node terrainNode;
        terrainNode["brushMode"] = terrainBrushModeToString(static_cast<TerrainBrushMode>(ts.brushMode));
        terrainNode["brushShape"] = terrainBrushShapeToString(static_cast<TerrainBrushShape>(ts.brushShape));
        terrainNode["brushFalloff"] = terrainBrushFalloffToString(static_cast<TerrainBrushFalloff>(ts.brushFalloff));
        encodePositiveFinite(terrainNode, "brushSize", ts.brushSize);
        encodeFinite(terrainNode, "brushStrength", ts.brushStrength);
        encodeFinite(terrainNode, "flattenHeight", ts.flattenHeight);
        terrainNode["heightMapResolution"] = ts.heightMapResolution;
        terrainNode["blendMapResolution"] = ts.blendMapResolution;
        terrainNode["normalizeBlendPaint"] = ts.normalizeBlendPaint;
        terrainNode["heightMapStartAtMiddle"] = ts.heightMapStartAtMiddle;
        terrainNode["flattenPickOnStroke"] = ts.flattenPickOnStroke;
        root["terrainEditor"] = terrainNode;
    }

    // Add tabs array
    YAML::Node tabsNode;
    for (const auto& tab : project->getTabs()) {
        YAML::Node tabNode;
        switch (tab.type) {
            case TabType::SCENE:        tabNode["type"] = "scene"; break;
            case TabType::CODE_EDITOR:  tabNode["type"] = "codeeditor"; break;
            case TabType::IMAGE_VIEWER: tabNode["type"] = "imageviewer"; break;
        }
        tabNode["filepath"] = tab.filepath;
        tabsNode.push_back(tabNode);
    }
    root["tabs"] = tabsNode;

    // Add scenes array
    YAML::Node scenesNode;
    for (const auto& sceneProject : project->getScenes()) {
        YAML::Node sceneNode;
        if (!sceneProject.filepath.empty()) {
            sceneNode["filepath"] = sceneProject.filepath.string();
            sceneNode["showAllJoints"]        = sceneProject.displaySettings.showAllJoints;
            sceneNode["showAllBones"]         = sceneProject.displaySettings.showAllBones;
            sceneNode["showAllBodies"]        = sceneProject.displaySettings.showAllBodies;
            sceneNode["hideCameraView"]       = sceneProject.displaySettings.hideCameraView;
            sceneNode["hideLightIcons"]       = sceneProject.displaySettings.hideLightIcons;
            sceneNode["hideSoundIcons"]       = sceneProject.displaySettings.hideSoundIcons;
            sceneNode["hideContainerGuides"]  = sceneProject.displaySettings.hideContainerGuides;
            sceneNode["showOrigin"]           = sceneProject.displaySettings.showOrigin;
            sceneNode["showGrid3D"]           = sceneProject.displaySettings.showGrid3D;
            sceneNode["hideSelectionOutline"] = sceneProject.displaySettings.hideSelectionOutline;
            sceneNode["disableFaceCulling"]   = sceneProject.displaySettings.disableFaceCulling;
            sceneNode["showGrid2D"]           = sceneProject.displaySettings.showGrid2D;
            encodePositiveFinite(sceneNode, "gridSpacing2D", sceneProject.displaySettings.gridSpacing2D);
            encodePositiveFinite(sceneNode, "gridSpacing3D", sceneProject.displaySettings.gridSpacing3D);
            sceneNode["snapToGrid"]           = sceneProject.displaySettings.snapToGrid;
            sceneNode["snapTile"]             = sceneProject.displaySettings.snapTile;
            sceneNode["snapRotation"]         = sceneProject.displaySettings.snapRotation;
            encodePositiveFinite(sceneNode, "rotationSnapDegrees", sceneProject.displaySettings.rotationSnapDegrees);

            if (sceneProject.sceneRender) {
                Camera* editorCam = sceneProject.sceneRender->getCamera();
                if (editorCam) {
                    float zoom = 0.0f;
                    float walkSpeedOffset = 0.0f;
                    if (sceneProject.sceneType == SceneType::SCENE_2D || sceneProject.sceneType == SceneType::SCENE_UI) {
                        zoom = static_cast<SceneRender2D*>(sceneProject.sceneRender)->getZoom();
                    } else if (sceneProject.sceneType == SceneType::SCENE_3D) {
                        walkSpeedOffset = static_cast<SceneRender3D*>(sceneProject.sceneRender)->getWalkSpeedOffset();
                    }
                    sceneNode["editorCamera"] = encodeEditorCamera(editorCam, zoom, walkSpeedOffset);
                }
            } else if (sceneProject.editorCameraState.IsDefined()) {
                sceneNode["editorCamera"] = sceneProject.editorCameraState;
            }

            scenesNode.push_back(sceneNode);
        }
    }
    root["scenes"] = scenesNode;

    return root;
}

void editor::Stream::decodeProject(Project* project, const YAML::Node& node) {
    if (!node.IsMap()) return;

    if (node["name"]) {
        project->setName(node["name"].as<std::string>());
    }

    // Set nextSceneId if it exists in the node and is greater than current
    if (node["nextSceneId"]) {
        uint32_t nextId = node["nextSceneId"].as<uint32_t>();
        if (nextId > project->getNextSceneId()) {
            project->setNextSceneId(nextId);
        }
    }

    if (node["selectedScene"]) {
        project->setSelectedSceneId(node["selectedScene"].as<uint32_t>());
    }

    if (node["canvasWidth"] && node["canvasHeight"]) {
        project->setCanvasSize(
            node["canvasWidth"].as<unsigned int>(),
            node["canvasHeight"].as<unsigned int>()
        );
    }

    if (node["scalingMode"]) {
        project->setScalingMode(stringToScalingMode(node["scalingMode"].as<std::string>()));
    }

    if (node["textureStrategy"]) {
        project->setTextureStrategy(stringToTextureStrategy(node["textureStrategy"].as<std::string>()));
    }

    if (node["vsync"].IsDefined()) {
        project->setVSyncEnabled(node["vsync"].as<bool>());
    }

    if (node["windowMode"]) {
        project->setWindowMode(stringToWindowMode(node["windowMode"].as<std::string>()));
    }

    if (node["windowWidth"] && node["windowHeight"]) {
        project->setWindowSize(
            node["windowWidth"].as<unsigned int>(),
            node["windowHeight"].as<unsigned int>()
        );
    } else {
        // Projects saved before window settings existed follow the canvas size
        project->setWindowSize(project->getCanvasWidth(), project->getCanvasHeight());
    }

    if (node["windowResizable"].IsDefined()) {
        project->setWindowResizable(node["windowResizable"].as<bool>());
    }

    if (node["windowTitle"]) {
        project->setWindowTitle(node["windowTitle"].as<std::string>());
    }

    if (node["windowIcon"]) {
        project->setWindowIcon(std::filesystem::path(node["windowIcon"].as<std::string>()));
    }

    if (node["assetsDir"]) {
        project->setAssetsDir(node["assetsDir"].as<std::string>());
    }

    if (node["luaDir"]) {
        project->setLuaDir(node["luaDir"].as<std::string>());
    }

    if (node["scriptDirs"]) {
        std::vector<fs::path> scriptDirs;
        for (const auto& dirNode : node["scriptDirs"]) {
            scriptDirs.push_back(dirNode.as<std::string>());
        }
        project->setScriptDirs(std::move(scriptDirs));
    }

    if (node["cmakeCCompiler"] || node["cmakeCxxCompiler"] || node["cmakeGenerator"]) {
        std::string cc = node["cmakeCCompiler"] ? node["cmakeCCompiler"].as<std::string>() : "";
        std::string cxx = node["cmakeCxxCompiler"] ? node["cmakeCxxCompiler"].as<std::string>() : "";
        std::string gen = node["cmakeGenerator"] ? node["cmakeGenerator"].as<std::string>() : "";
        project->setCMakeKit(cc, cxx, gen);
    }
    if (node["cmakeBuildJobs"]) {
        // Only sanitized to a machine-independent range here; clamping to this
        // machine's core count would silently rewrite a shared project file
        // (the per-machine cap is applied at build time instead).
        const long long jobs = node["cmakeBuildJobs"].as<long long>();
        const long long maxJobs = static_cast<long long>(Generator::MAX_SUPPORTED_PARALLEL_BUILD_JOBS);
        project->setCMakeBuildJobs(static_cast<unsigned int>(std::clamp(jobs, 0LL, maxJobs)));
    }
    if (node["packNativeResources"].IsDefined()) {
        project->setPackNativeResources(node["packNativeResources"].as<bool>());
    }

    if (node["startSceneId"]) {
        project->setStartSceneId(node["startSceneId"].as<uint32_t>());
    }

    if (node["standaloneBundles"]) {
        std::vector<fs::path> bundlePaths;
        for (const auto& pathNode : node["standaloneBundles"]) {
            bundlePaths.push_back(pathNode.as<std::string>());
        }
        project->setStandaloneBundles(std::move(bundlePaths));
    }

    if (node["terrainEditor"] && node["terrainEditor"].IsMap()) {
        const auto& tn = node["terrainEditor"];
        TerrainEditorSettings ts;
        if (tn["brushMode"].IsDefined())          ts.brushMode          = static_cast<int>(stringToTerrainBrushMode(tn["brushMode"].as<std::string>()));
        if (tn["brushShape"].IsDefined())         ts.brushShape         = static_cast<int>(stringToTerrainBrushShape(tn["brushShape"].as<std::string>()));
        if (tn["brushFalloff"].IsDefined())       ts.brushFalloff       = static_cast<int>(stringToTerrainBrushFalloff(tn["brushFalloff"].as<std::string>()));
        if (tn["brushSize"].IsDefined())          ts.brushSize          = decodePositiveFinite(tn["brushSize"], ts.brushSize);
        if (tn["brushStrength"].IsDefined())      ts.brushStrength      = decodeFinite(tn["brushStrength"], ts.brushStrength);
        if (tn["flattenHeight"].IsDefined())      ts.flattenHeight      = decodeFinite(tn["flattenHeight"], ts.flattenHeight);
        if (tn["heightMapResolution"].IsDefined()) ts.heightMapResolution = tn["heightMapResolution"].as<int>();
        if (tn["blendMapResolution"].IsDefined()) ts.blendMapResolution = tn["blendMapResolution"].as<int>();
        if (tn["normalizeBlendPaint"].IsDefined()) ts.normalizeBlendPaint = tn["normalizeBlendPaint"].as<bool>();
        if (tn["heightMapStartAtMiddle"].IsDefined()) ts.heightMapStartAtMiddle = tn["heightMapStartAtMiddle"].as<bool>();
        if (tn["flattenPickOnStroke"].IsDefined()) ts.flattenPickOnStroke = tn["flattenPickOnStroke"].as<bool>();
        else ts.brushStrength = TerrainEditorSettings{}.brushStrength; // file predates time-based flow; old per-event strengths are far too weak under the new semantics
        project->getTerrainEditorSettings() = ts;
    }

    // Build set of scene filepaths that should be opened from tabs
    std::set<std::string> openedScenePaths;
    bool hasTabs = node["tabs"] && node["tabs"].IsSequence();

    if (hasTabs) {
        for (const auto& tabNode : node["tabs"]) {
            std::string type = tabNode["type"] ? tabNode["type"].as<std::string>() : "";
            std::string filepath = tabNode["filepath"] ? tabNode["filepath"].as<std::string>() : "";
            if (type == "scene") {
                openedScenePaths.insert(filepath);
                project->addTab(TabType::SCENE, filepath);
            } else if (type == "codeeditor") {
                project->addTab(TabType::CODE_EDITOR, filepath);
            } else if (type == "imageviewer") {
                project->addTab(TabType::IMAGE_VIEWER, filepath);
            }
        }
    }

    // Load scenes information
    bool isFirstScene = true;
    if (node["scenes"]) {
        for (const auto& sceneNode : node["scenes"]) {
            if (sceneNode["filepath"]) {
                fs::path scenePath = sceneNode["filepath"].as<std::string>();
                if (scenePath.is_relative()) {
                    scenePath = project->getProjectPath() / scenePath;
                }
                bool opened;
                if (hasTabs) {
                    std::string relStr = sceneNode["filepath"].as<std::string>();
                    opened = openedScenePaths.count(relStr) > 0;
                } else {
                    // No tabs: open first scene only
                    opened = isFirstScene;
                }
                if (fs::exists(scenePath)) {
                    project->loadScene(scenePath, opened, true, opened);

                    // Restore display settings into the just-loaded scene
                    auto& scenes = project->getScenes();
                    if (!scenes.empty()) {
                        SceneDisplaySettings& ds = scenes.back().displaySettings;
                        if (sceneNode["showAllJoints"])        ds.showAllJoints        = sceneNode["showAllJoints"].as<bool>();
                        if (sceneNode["showAllBones"])         ds.showAllBones         = sceneNode["showAllBones"].as<bool>();
                        if (sceneNode["showAllBodies"])        ds.showAllBodies        = sceneNode["showAllBodies"].as<bool>();
                        if (sceneNode["hideCameraView"])       ds.hideCameraView       = sceneNode["hideCameraView"].as<bool>();
                        if (sceneNode["hideLightIcons"])       ds.hideLightIcons       = sceneNode["hideLightIcons"].as<bool>();
                        if (sceneNode["hideSoundIcons"])       ds.hideSoundIcons       = sceneNode["hideSoundIcons"].as<bool>();
                        if (sceneNode["hideContainerGuides"])  ds.hideContainerGuides  = sceneNode["hideContainerGuides"].as<bool>();

                        if (sceneNode["showOrigin"])           ds.showOrigin           = sceneNode["showOrigin"].as<bool>();
                        if (sceneNode["showGrid3D"])           ds.showGrid3D           = sceneNode["showGrid3D"].as<bool>();
                        if (sceneNode["hideSelectionOutline"]) ds.hideSelectionOutline = sceneNode["hideSelectionOutline"].as<bool>();
                        if (sceneNode["disableFaceCulling"])   ds.disableFaceCulling   = sceneNode["disableFaceCulling"].as<bool>();
                        if (sceneNode["showGrid2D"])           ds.showGrid2D           = sceneNode["showGrid2D"].as<bool>();

                        if (sceneNode["gridSpacing2D"])        ds.gridSpacing2D        = decodePositiveFinite(sceneNode["gridSpacing2D"], ds.gridSpacing2D);
                        if (sceneNode["gridSpacing3D"])        ds.gridSpacing3D        = decodePositiveFinite(sceneNode["gridSpacing3D"], ds.gridSpacing3D);
                        if (sceneNode["snapToGrid"])           ds.snapToGrid           = sceneNode["snapToGrid"].as<bool>();
                        if (sceneNode["snapTile"])             ds.snapTile             = sceneNode["snapTile"].as<bool>();
                        if (sceneNode["snapRotation"])         ds.snapRotation         = sceneNode["snapRotation"].as<bool>();
                        if (sceneNode["rotationSnapDegrees"])  ds.rotationSnapDegrees  = decodePositiveFinite(sceneNode["rotationSnapDegrees"], ds.rotationSnapDegrees);

                        if (sceneNode["editorCamera"]) {
                            SceneProject& loadedScene = scenes.back();
                            loadedScene.editorCameraState = YAML::Clone(sceneNode["editorCamera"]);
                            if (loadedScene.sceneRender) {
                                Camera* editorCam = loadedScene.sceneRender->getCamera();
                                if (editorCam) {
                                    float zoom = 0.0f;
                                    float walkSpeedOffset = 0.0f;
                                    Stream::decodeEditorCamera(editorCam, sceneNode["editorCamera"], zoom, walkSpeedOffset);
                                    if ((loadedScene.sceneType == SceneType::SCENE_2D || loadedScene.sceneType == SceneType::SCENE_UI) && zoom > 0.0f) {
                                        static_cast<SceneRender2D*>(loadedScene.sceneRender)->setZoom(zoom);
                                    }
                                    if (loadedScene.sceneType == SceneType::SCENE_3D) {
                                        static_cast<SceneRender3D*>(loadedScene.sceneRender)->setWalkSpeedOffset(walkSpeedOffset);
                                    }
                                }
                            }
                        }
                    }
                }
                isFirstScene = false;
            }
        }
    }
}

YAML::Node editor::Stream::encodeSceneProject(const Project* project, const SceneProject* sceneProject) {
    YAML::Node root;
    root["id"] = sceneProject->id;
    root["name"] = sceneProject->name;
    root["scene"] = encodeScene(sceneProject->scene);
    root["sceneType"] = sceneTypeToString(sceneProject->sceneType);
    root["mainCamera"] = sceneProject->mainCamera;

    YAML::Node maxValuesNode;
    maxValuesNode["maxSubmeshes"] = sceneProject->maxValues.maxSubmeshes;
    maxValuesNode["maxTilemapTilesRect"] = sceneProject->maxValues.maxTilemapTilesRect;
    maxValuesNode["maxTilemapTiles"] = sceneProject->maxValues.maxTilemapTiles;
    maxValuesNode["maxExternalBuffers"] = sceneProject->maxValues.maxExternalBuffers;
    maxValuesNode["maxSpriteFrames"] = sceneProject->maxValues.maxSpriteFrames;
    maxValuesNode["maxBones"] = sceneProject->maxValues.maxBones;
    root["maxValues"] = maxValuesNode;

    if (!sceneProject->shaderKeys.empty()) {
        YAML::Node shaderKeysNode;
        for (const auto& key : sceneProject->shaderKeys) {
            shaderKeysNode.push_back(key);
        }
        root["shaderKeys"] = shaderKeysNode;
    }

    if (!sceneProject->childScenes.empty()) {
        YAML::Node childScenesNode;
        for (const ChildSceneRef& childSceneRef : sceneProject->childScenes) {
            YAML::Node childSceneNode;
            childSceneNode["id"] = childSceneRef.id;
            childSceneNode["startActive"] = childSceneRef.startActive;
            childScenesNode.push_back(childSceneNode);
        }
        root["childScenes"] = childScenesNode;
    }

    if (!sceneProject->cppScripts.empty()) {
        YAML::Node scriptsNode;
        for (const auto& script : sceneProject->cppScripts) {
            YAML::Node scriptNode;
            scriptNode["path"] = script.path.generic_string();
            if (!script.headerPath.empty()) {
                scriptNode["headerPath"] = script.headerPath.generic_string();
            }
            if (!script.className.empty()) {
                scriptNode["className"] = script.className;
            }

            if (!script.properties.empty()) {
                YAML::Node propertiesNode;
                for (const auto& prop : script.properties) {
                    YAML::Node propNode;
                    propNode["name"] = prop.name;
                    propNode["isPtr"] = prop.isPtr;
                    if (!prop.ptrTypeName.empty()) {
                        propNode["ptrTypeName"] = prop.ptrTypeName;
                    }
                    propertiesNode.push_back(propNode);
                }
                scriptNode["properties"] = propertiesNode;
            }

            scriptsNode.push_back(scriptNode);
        }
        root["cppScripts"] = scriptsNode;
    }

    if (!sceneProject->bundles.empty()) {
        YAML::Node bundlesNode;
        for (const auto& bundle : sceneProject->bundles) {
            bundlesNode.push_back(bundle.bundlePath.generic_string());
        }
        root["bundles"] = bundlesNode;
    }

    YAML::Node entitiesNode;
    for (Entity entity : sceneProject->entities) {
        // Skip bundle children (they are stored in the bundle file)
        if (project) {
            fs::path bundlePath = project->findEntityBundlePathFor(sceneProject->id, entity);
            if (!bundlePath.empty()) {
                const EntityBundle* bundle = project->getEntityBundle(bundlePath);
                if (bundle && bundle->getRootEntity(sceneProject->id, entity) != entity) {
                    continue;
                }
            }
        }

        if (Transform* transform = sceneProject->scene->findComponent<Transform>(entity)) {
            if (transform->parent == NULL_ENTITY) {
                entitiesNode.push_back(encodeEntity(entity, sceneProject->scene, project, sceneProject));
            }
        }else{
            entitiesNode.push_back(encodeEntity(entity, sceneProject->scene, project, sceneProject));
        }
    }

    root["entities"] = entitiesNode;

    return root;
}

void editor::Stream::decodeSceneProject(SceneProject* sceneProject, const YAML::Node& node, bool loadScene) {
    if (node["id"]) sceneProject->id = node["id"].as<uint32_t>();
    if (node["name"]) sceneProject->name = node["name"].as<std::string>();
    if (loadScene){
        if (node["scene"]) sceneProject->scene = decodeScene(sceneProject->scene, node["scene"]);
    }
    if (node["sceneType"]) sceneProject->sceneType = stringToSceneType(node["sceneType"].as<std::string>());
    if (node["mainCamera"]) sceneProject->mainCamera = node["mainCamera"].as<Entity>();

    sceneProject->maxValues = {};
    if (node["maxValues"]) {
        const YAML::Node& maxValuesNode = node["maxValues"];
        if (maxValuesNode["maxSubmeshes"]) sceneProject->maxValues.maxSubmeshes = maxValuesNode["maxSubmeshes"].as<unsigned int>();
        if (maxValuesNode["maxTilemapTilesRect"]) sceneProject->maxValues.maxTilemapTilesRect = maxValuesNode["maxTilemapTilesRect"].as<unsigned int>();
        if (maxValuesNode["maxTilemapTiles"]) sceneProject->maxValues.maxTilemapTiles = maxValuesNode["maxTilemapTiles"].as<unsigned int>();
        if (maxValuesNode["maxExternalBuffers"]) sceneProject->maxValues.maxExternalBuffers = maxValuesNode["maxExternalBuffers"].as<unsigned int>();
        if (maxValuesNode["maxSpriteFrames"]) sceneProject->maxValues.maxSpriteFrames = maxValuesNode["maxSpriteFrames"].as<unsigned int>();
        if (maxValuesNode["maxBones"]) sceneProject->maxValues.maxBones = maxValuesNode["maxBones"].as<unsigned int>();
    }

    sceneProject->shaderKeys.clear();
    if (node["shaderKeys"]) {
        for (const auto& keyNode : node["shaderKeys"]) {
            // Normalize on load so keys persisted by older editor versions (carrying
            // retired property bits like MESH bit 5) collapse onto their current
            // equivalent instead of producing stale duplicate shader variants at export.
            sceneProject->shaderKeys.insert(ShaderPool::normalizeKey(keyNode.as<uint64_t>()));
        }
    }

    sceneProject->childScenes.clear();
    if (node["childScenes"]) {
        for (const auto& childSceneNode : node["childScenes"]) {
            ChildSceneRef childSceneRef;
            if (!childSceneNode.IsMap() || !childSceneNode["id"]) {
                continue;
            }

            childSceneRef.id = childSceneNode["id"].as<uint32_t>();
            if (childSceneNode["startActive"]) {
                childSceneRef.startActive = childSceneNode["startActive"].as<bool>();
            }
            sceneProject->childScenes.push_back(childSceneRef);
        }
    }

    sceneProject->cppScripts.clear();
    if (node["cppScripts"]) {
        for (const auto& scriptNode : node["cppScripts"]) {
            SceneScriptSource script;

            if (scriptNode["path"]) {
                script.path = fs::path(scriptNode["path"].as<std::string>());
            }
            if (scriptNode["headerPath"]) {
                script.headerPath = fs::path(scriptNode["headerPath"].as<std::string>());
            }
            if (scriptNode["className"]) {
                script.className = scriptNode["className"].as<std::string>();
            }
            if (scriptNode["properties"]) {
                for (const auto& propNode : scriptNode["properties"]) {
                    ScriptPropertyInfo prop;
                    if (propNode["name"]) {
                        prop.name = propNode["name"].as<std::string>();
                    }
                    if (propNode["isPtr"]) {
                        prop.isPtr = propNode["isPtr"].as<bool>();
                    }
                    if (propNode["ptrTypeName"]) {
                        prop.ptrTypeName = propNode["ptrTypeName"].as<std::string>();
                    }
                    script.properties.push_back(prop);
                }
            }

            sceneProject->cppScripts.push_back(script);
        }
    } else if (node["cppScriptPaths"]) {
        for (const auto& pathNode : node["cppScriptPaths"]) {
            SceneScriptSource script;
            script.path = fs::path(pathNode.as<std::string>());
            sceneProject->cppScripts.push_back(script);
        }
    }

    sceneProject->bundles.clear();
    if (node["bundles"]) {
        for (const auto& pathNode : node["bundles"]) {
            BundleSceneInfo info;
            info.bundlePath = pathNode.as<std::string>();
            info.functionName = Factory::bundleToFunctionName(info.bundlePath);
            sceneProject->bundles.push_back(std::move(info));
        }
    }
}

void editor::Stream::decodeSceneProjectEntities(Project* project, SceneProject* sceneProject, const YAML::Node& node){
    sceneProject->entities.clear();
    sceneProject->selectedEntities.clear();

    auto entitiesNode = node["entities"];
    for (const auto& entityNode : entitiesNode){
        decodeEntity(entityNode, sceneProject->scene, &sceneProject->entities, project, sceneProject);
    }

    CameraTextureLink::resolve(sceneProject->scene);
}

YAML::Node editor::Stream::encodeEditorCamera(Camera* camera, float zoom, float walkSpeedOffset) {
    if (!camera) return YAML::Node();
    Entity camEntity = camera->getEntity();
    Scene* scene = camera->getScene();
    CameraComponent& camComp = scene->getComponent<CameraComponent>(camEntity);
    Transform& camTransform = scene->getComponent<Transform>(camEntity);
    YAML::Node camNode;
    camNode["type"] = cameraTypeToString(camComp.type);
    encodeFiniteVector3(camNode, "target", camComp.target);
    encodeFiniteVector3(camNode, "up", camComp.up);
    encodePositiveFinite(camNode, "yfov", camComp.yfov);
    encodePositiveFinite(camNode, "aspect", camComp.aspect);
    encodeFinite(camNode, "nearClip", camComp.nearClip);
    encodeFinite(camNode, "farClip", camComp.farClip);
    encodeFiniteVector3(camNode, "position", camTransform.position);
    encodePositiveFinite(camNode, "zoom", zoom);
    if (walkSpeedOffset != 0.0f) {
        encodeFinite(camNode, "walkSpeed", walkSpeedOffset);
    }
    return camNode;
}

void editor::Stream::decodeEditorCamera(Camera* camera, const YAML::Node& node, float& zoom, float& walkSpeedOffset) {
    zoom = 0.0f;
    walkSpeedOffset = 0.0f;
    if (!camera || !node) return;
    Entity camEntity = camera->getEntity();
    Scene* scene = camera->getScene();
    CameraComponent& camComp = scene->getComponent<CameraComponent>(camEntity);
    Transform& camTransform = scene->getComponent<Transform>(camEntity);
    if (node["type"]) camComp.type = stringToCameraType(node["type"].as<std::string>());
    camComp.target = decodeFiniteVector3(node["target"], camComp.target);
    camComp.up = decodeFiniteVector3(node["up"], camComp.up);
    camComp.yfov = decodePositiveFinite(node["yfov"], camComp.yfov);
    camComp.aspect = decodePositiveFinite(node["aspect"], camComp.aspect);
    camComp.nearClip = decodeFinite(node["nearClip"], camComp.nearClip);
    camComp.farClip = decodeFinite(node["farClip"], camComp.farClip);
    camTransform.position = decodeFiniteVector3(node["position"], camTransform.position);
    camComp.needUpdate = true;
    if (node["zoom"]) {
        float decodedZoom = decodeFinite(node["zoom"], 0.0f);
        if (decodedZoom > 0.0f) {
            zoom = decodedZoom;
        }
    }
    walkSpeedOffset = decodeFinite(node["walkSpeed"], 0.0f);
}

YAML::Node editor::Stream::encodeScene(Scene* scene) {
    YAML::Node sceneNode;

    sceneNode["backgroundColor"] = encodeVector4(scene->getBackgroundColor());
    sceneNode["shadowQuality"] = shadowQualityToString(scene->getShadowQuality());
    sceneNode["lightState"] = lightStateToString(scene->getLightState());
    sceneNode["globalIlluminationIntensity"] = scene->getGlobalIlluminationIntensity();
    sceneNode["globalIlluminationColor"] = encodeVector3(scene->getGlobalIlluminationColor());
    sceneNode["ambientLight2DIntensity"] = scene->getAmbientLight2DIntensity();
    sceneNode["ambientLight2DColor"] = encodeVector3(scene->getAmbientLight2DColor());
    sceneNode["shadow2DQuality"] = shadowQualityToString(scene->getShadow2DQuality());
    sceneNode["ssaoEnabled"] = scene->isSSAOEnabled();
    sceneNode["ssaoRadius"] = scene->getSSAORadius();
    sceneNode["ssaoIntensity"] = scene->getSSAOIntensity();
    sceneNode["ssaoBias"] = scene->getSSAOBias();
    sceneNode["ssrEnabled"] = scene->isSSREnabled();
    sceneNode["ssrMaxDistance"] = scene->getSSRMaxDistance();
    sceneNode["ssrThickness"] = scene->getSSRThickness();
    sceneNode["ssrMaxSteps"] = scene->getSSRMaxSteps();
    sceneNode["ssrIntensity"] = scene->getSSRIntensity();
    sceneNode["ssrBlur"] = scene->getSSRBlur();
    sceneNode["gravity2D"] = encodeVector2(scene->getGravity2D());
    sceneNode["gravity3D"] = encodeVector3(scene->getGravity3D());
    sceneNode["fixedResolutionEnabled"] = scene->isFixedResolutionEnabled();
    sceneNode["fixedResolutionWidth"] = scene->getFixedResolutionWidth();
    sceneNode["fixedResolutionHeight"] = scene->getFixedResolutionHeight();
    sceneNode["fixedResolutionFilter"] = textureFilterToString(scene->getFixedResolutionFilter());
    sceneNode["enableUIEvents"] = uiEventStateToString(scene->getEnableUIEvents());

    if (!scene->getDefaultMeshShader().empty())
        sceneNode["defaultMeshShader"] = scene->getDefaultMeshShader();
    if (!scene->getDefaultUIShader().empty())
        sceneNode["defaultUIShader"] = scene->getDefaultUIShader();
    if (!scene->getDefaultSkyShader().empty())
        sceneNode["defaultSkyShader"] = scene->getDefaultSkyShader();
    if (!scene->getDefaultPointsShader().empty())
        sceneNode["defaultPointsShader"] = scene->getDefaultPointsShader();
    if (!scene->getDefaultLinesShader().empty())
        sceneNode["defaultLinesShader"] = scene->getDefaultLinesShader();

    for (const PostProcessPass& pass : scene->getPostProcessPasses()) {
        YAML::Node passNode;
        passNode["shader"] = pass.shader;
        passNode["enabled"] = pass.enabled;
        for (const auto& uniform : pass.uniforms) {
            passNode["uniforms"][uniform.first] = encodeVector4(uniform.second);
        }
        sceneNode["postProcess"].push_back(passNode);
    }

    return sceneNode;
}

Scene* editor::Stream::decodeScene(Scene* scene, const YAML::Node& node) {
    if (!scene){
        scene = new Scene();
    }

    if (node["backgroundColor"]) {
        scene->setBackgroundColor(decodeVector4(node["backgroundColor"]));
    }

    if (node["shadowQuality"]) {
        scene->setShadowQuality(stringToShadowQuality(node["shadowQuality"].as<std::string>()));
    }

    if (node["lightState"]) {
        scene->setLightState(stringToLightState(node["lightState"].as<std::string>()));
    }

    if (node["globalIlluminationIntensity"] && node["globalIlluminationColor"]) {
        scene->setGlobalIllumination(
            node["globalIlluminationIntensity"].as<float>(),
            decodeVector3(node["globalIlluminationColor"])
        );
    } else if (node["globalIlluminationIntensity"]) {
        scene->setGlobalIllumination(node["globalIlluminationIntensity"].as<float>());
    } else if (node["globalIlluminationColor"]) {
        scene->setGlobalIllumination(decodeVector3(node["globalIlluminationColor"]));
    }

    if (node["ambientLight2DIntensity"] && node["ambientLight2DColor"]) {
        scene->setAmbientLight2D(
            node["ambientLight2DIntensity"].as<float>(),
            decodeVector3(node["ambientLight2DColor"])
        );
    } else if (node["ambientLight2DIntensity"]) {
        scene->setAmbientLight2D(node["ambientLight2DIntensity"].as<float>());
    } else if (node["ambientLight2DColor"]) {
        scene->setAmbientLight2D(decodeVector3(node["ambientLight2DColor"]));
    }

    if (node["shadow2DQuality"]) {
        scene->setShadow2DQuality(stringToShadowQuality(node["shadow2DQuality"].as<std::string>()));
    }

    if (node["ssaoEnabled"]) {
        scene->setSSAOEnabled(node["ssaoEnabled"].as<bool>());
    }
    if (node["ssaoRadius"]) {
        scene->setSSAORadius(node["ssaoRadius"].as<float>());
    }
    if (node["ssaoIntensity"]) {
        scene->setSSAOIntensity(node["ssaoIntensity"].as<float>());
    }
    if (node["ssaoBias"]) {
        scene->setSSAOBias(node["ssaoBias"].as<float>());
    }
    if (node["ssrEnabled"]) {
        scene->setSSREnabled(node["ssrEnabled"].as<bool>());
    }
    if (node["ssrMaxDistance"]) {
        scene->setSSRMaxDistance(node["ssrMaxDistance"].as<float>());
    }
    if (node["ssrThickness"]) {
        scene->setSSRThickness(node["ssrThickness"].as<float>());
    }
    if (node["ssrMaxSteps"]) {
        scene->setSSRMaxSteps(node["ssrMaxSteps"].as<int>());
    }
    if (node["ssrIntensity"]) {
        scene->setSSRIntensity(node["ssrIntensity"].as<float>());
    }
    if (node["ssrBlur"]) {
        scene->setSSRBlur(node["ssrBlur"].as<float>());
    }

    // absent key restores the engine default so decoding into a reused scene (play restore) is exact
    scene->setGravity2D(node["gravity2D"] ? decodeVector2(node["gravity2D"]) : Vector2(0.0f, -9.81f));
    scene->setGravity3D(node["gravity3D"] ? decodeVector3(node["gravity3D"]) : Vector3(0.0f, -9.81f, 0.0f));

    if (node["fixedResolutionEnabled"]) {
        scene->setFixedResolutionEnabled(node["fixedResolutionEnabled"].as<bool>());
    }
    if (node["fixedResolutionWidth"]) {
        scene->setFixedResolutionWidth(node["fixedResolutionWidth"].as<unsigned int>());
    }
    if (node["fixedResolutionHeight"]) {
        scene->setFixedResolutionHeight(node["fixedResolutionHeight"].as<unsigned int>());
    }
    if (node["fixedResolutionFilter"]) {
        scene->setFixedResolutionFilter(stringToTextureFilter(node["fixedResolutionFilter"].as<std::string>()));
    }

    if (node["enableUIEvents"]) {
        scene->setEnableUIEvents(stringToUIEventState(node["enableUIEvents"].as<std::string>()));
    }

    // absent key clears the default so decoding into a reused scene (play restore) is exact
    scene->setDefaultMeshShader(node["defaultMeshShader"] ? node["defaultMeshShader"].as<std::string>() : "");
    scene->setDefaultUIShader(node["defaultUIShader"] ? node["defaultUIShader"].as<std::string>() : "");
    scene->setDefaultSkyShader(node["defaultSkyShader"] ? node["defaultSkyShader"].as<std::string>() : "");
    scene->setDefaultPointsShader(node["defaultPointsShader"] ? node["defaultPointsShader"].as<std::string>() : "");
    scene->setDefaultLinesShader(node["defaultLinesShader"] ? node["defaultLinesShader"].as<std::string>() : "");

    // absent key clears the chain so decoding into a reused scene (play restore) is exact
    std::vector<PostProcessPass> postProcess;
    if (node["postProcess"]) {
        for (const auto& passNode : node["postProcess"]) {
            PostProcessPass pass;
            pass.shader = passNode["shader"] ? passNode["shader"].as<std::string>() : "";
            pass.enabled = passNode["enabled"] ? passNode["enabled"].as<bool>() : true;
            if (passNode["uniforms"]) {
                for (const auto& uniform : passNode["uniforms"]) {
                    pass.uniforms.push_back({uniform.first.as<std::string>(), decodeVector4(uniform.second)});
                }
            }
            postProcess.push_back(pass);
        }
    }
    scene->setPostProcessPasses(postProcess);

    return scene;
}

YAML::Node editor::Stream::encodeEntitySelection(const std::vector<Entity>& entities, const EntityRegistry* registry, const Project* project, const SceneProject* sceneProject) {
    if (entities.empty()) {
        return YAML::Node();
    }

    if (entities.size() == 1) {
        return encodeEntity(entities[0], registry, project, sceneProject);
    }

    YAML::Node bundleNode;
    bundleNode["type"] = "EntityBundle";
    YAML::Node membersNode(YAML::NodeType::Sequence);
    for (Entity entity : entities) {
        membersNode.push_back(encodeEntity(entity, registry, project, sceneProject));
    }
    bundleNode["members"] = membersNode;

    return bundleNode;
}

std::vector<Entity> editor::Stream::decodeEntitySelection(const YAML::Node& entityNode, EntityRegistry* registry, std::vector<Entity>* entities, Project* project, SceneProject* sceneProject, Entity parent, bool createNewIfExists, std::unordered_map<Entity, Entity>* entityRemap) {
    if (!entityNode || !entityNode.IsMap()) {
        return {};
    }

    if (entityNode["members"] && entityNode["members"].IsSequence()) {
        std::vector<Entity> allEntities;
        for (const auto& memberNode : entityNode["members"]) {
            std::vector<Entity> memberEntities = decodeEntitySelection(memberNode, registry, entities, project, sceneProject, parent, createNewIfExists, entityRemap);
            allEntities.insert(allEntities.end(), memberEntities.begin(), memberEntities.end());
        }
        return allEntities;
    }

    return decodeEntity(entityNode, registry, entities, project, sceneProject, parent, createNewIfExists, false, entityRemap);
}

YAML::Node editor::Stream::encodeEntity(const Entity entity, const EntityRegistry* registry,
        const Project* project, const SceneProject* sceneProject) {
    return encodeEntity(entity, registry, project, sceneProject, false);
}

YAML::Node editor::Stream::encodeEntity(const Entity entity, const EntityRegistry* registry,
        const Project* project, const SceneProject* sceneProject, bool ignoreContainingBundle) {
    std::map<Entity, YAML::Node> entityNodes;

    auto importedBundlePath = [&](Entity candidate) {
        fs::path path;
        if (!project || !sceneProject) {
            return path;
        }
        Signature signature = registry->getSignature(candidate);
        if (!signature.test(registry->getComponentId<BundleComponent>())) {
            return path;
        }
        const BundleComponent& component = registry->getComponent<BundleComponent>(candidate);
        const EntityBundle* bundle = project->getEntityBundle(component.path);
        const EntityBundle::Instance* instance = bundle ?
            bundle->getInstance(sceneProject->id, candidate) : nullptr;
        if (instance && instance->rootEntity == candidate) {
            path = component.path;
        }
        return path;
    };

    bool hasCurrentEntity = !sceneProject ||
        std::find(sceneProject->entities.begin(), sceneProject->entities.end(), entity) != sceneProject->entities.end();

    if (hasCurrentEntity) {
        fs::path currentBundlePath = ignoreContainingBundle ? fs::path{} : importedBundlePath(entity);
        YAML::Node& currentNode = entityNodes[entity];
        currentNode = ignoreContainingBundle ? encodeEntityAux(entity, registry) :
            encodeEntityAux(entity, registry, project, sceneProject, currentBundlePath);

        // Imported children are represented by the bundle root and its overrides.
        if (!ignoreContainingBundle && project && registry->getSignature(entity).test(registry->getComponentId<BundleComponent>()) &&
            (!sceneProject || !currentBundlePath.empty())) {
            return entityNodes[entity];
        }

        Signature signature = registry->getSignature(entity);

        if (signature.test(registry->getComponentId<Transform>())) {
            auto transforms = registry->getComponentArray<Transform>();
            size_t firstIndex = transforms->getIndex(entity);
            std::unordered_set<Entity> skippedBundleBranch;

            for (size_t i = firstIndex + 1; i < transforms->size(); ++i) {
                Entity currentEntity = transforms->getEntity(i);
                const Transform& transform = transforms->getComponentFromIndex(i);

                if (skippedBundleBranch.count(transform.parent)) {
                    skippedBundleBranch.insert(currentEntity);
                    continue;
                }

                hasCurrentEntity = !sceneProject ||
                    std::find(sceneProject->entities.begin(), sceneProject->entities.end(), currentEntity) != sceneProject->entities.end();
                if (hasCurrentEntity) {
                    fs::path currentBundlePath = importedBundlePath(currentEntity);
                    YAML::Node& currentNode = entityNodes[currentEntity];
                    currentNode = ignoreContainingBundle && currentBundlePath.empty() ?
                        encodeEntityAux(currentEntity, registry) :
                        encodeEntityAux(currentEntity, registry, project, sceneProject, currentBundlePath);

                    if (entityNodes.find(transform.parent) != entityNodes.end()) {
                        YAML::Node& parentNode = entityNodes[transform.parent];
                        if (!parentNode["children"]) {
                            parentNode["children"] = YAML::Node();
                        }
                        parentNode["children"].push_back(currentNode);
                        if (project && registry->getSignature(currentEntity).test(registry->getComponentId<BundleComponent>()) &&
                            (!sceneProject || !currentBundlePath.empty())) {
                            skippedBundleBranch.insert(currentEntity);
                        }
                    } else {
                        break; // No more childs
                    }
                }
            }
        }
    }

    return entityNodes[entity];
}

YAML::Node editor::Stream::encodeEntityAux(const Entity entity, const EntityRegistry* registry,
        const Project* project, const SceneProject* sceneProject, const std::filesystem::path& bundlePathOverride) {
    YAML::Node entityNode;

    fs::path bundlePath = bundlePathOverride;
    if (bundlePath.empty() && project && sceneProject) {
        bundlePath = project->findEntityBundlePathFor(sceneProject->id, entity);
    }

    if (!bundlePath.empty()) {
        const EntityBundle* bundle = project->getEntityBundle(bundlePath);
        const Entity rootEntity = bundle->getRootEntity(sceneProject->id, entity);

        if (rootEntity == entity) {
                // Bundle root: encode as normal Entity (BundleComponent carries the path)
                entityNode["type"] = "Entity";
                entityNode["entity"] = entity;
                entityNode["name"] = registry->getEntityName(entity);

                Signature signature = registry->getSignature(entity);
                YAML::Node components = encodeComponents(entity, registry, signature);

                if ((components.IsMap() && components.size() > 0)){
                    entityNode["components"] = components;
                }

                // Encode bundle overrides and local entities
                auto sceneIt = bundle->instances.find(sceneProject->id);
                if (sceneIt != bundle->instances.end()) {
                    for (const auto& instance : sceneIt->second) {
                        if (instance.rootEntity != entity) continue;

                        // Encode component overrides keyed by registryEntity
                        YAML::Node overridesNode;
                        for (const auto& member : instance.members) {
                            auto overrideIt = instance.overrides.find(member.localEntity);
                            if (overrideIt != instance.overrides.end() && overrideIt->second != 0) {
                                YAML::Node entry;
                                entry["registryEntity"] = member.registryEntity;
                                Signature sig = Catalog::componentMaskToSignature(registry, overrideIt->second);
                                YAML::Node overrideComps = encodeComponents(member.localEntity, registry, sig);
                                if (overrideComps.IsMap() && overrideComps.size() > 0) {
                                    entry["components"] = overrideComps;
                                }
                                overridesNode.push_back(entry);
                            }
                        }

                        // Encode nested bundle overrides (tagged with bundlePath)
                        for (const auto& member : instance.members) {
                            if (!registry->getSignature(member.localEntity).test(registry->getComponentId<BundleComponent>())) continue;
                            const BundleComponent& nestedBC = registry->getComponent<BundleComponent>(member.localEntity);
                            if (nestedBC.path.empty()) continue;

                            const EntityBundle* nestedBundle = project->getEntityBundle(nestedBC.path);
                            if (!nestedBundle) continue;

                            auto nestedSceneIt = nestedBundle->instances.find(sceneProject->id);
                            if (nestedSceneIt == nestedBundle->instances.end()) continue;

                            for (const auto& nestedInst : nestedSceneIt->second) {
                                if (nestedInst.rootEntity != member.localEntity) continue;

                                for (const auto& nestedMember : nestedInst.members) {
                                    auto nOverrideIt = nestedInst.overrides.find(nestedMember.localEntity);
                                    if (nOverrideIt != nestedInst.overrides.end() && nOverrideIt->second != 0) {
                                        YAML::Node entry;
                                        entry["registryEntity"] = nestedMember.registryEntity;
                                        entry["bundlePath"] = nestedBC.path;
                                        entry["bundleRootRegistryEntity"] = member.registryEntity;
                                        Signature sig = Catalog::componentMaskToSignature(registry, nOverrideIt->second);
                                        YAML::Node overrideComps = encodeComponents(nestedMember.localEntity, registry, sig);
                                        if (overrideComps.IsMap() && overrideComps.size() > 0) {
                                            entry["components"] = overrideComps;
                                        }
                                        overridesNode.push_back(entry);
                                    }
                                }
                                break;
                            }
                        }

                        if (overridesNode.size() > 0) {
                            entityNode["bundleOverrides"] = overridesNode;
                        }

                        // Encode scene-specific local entities (children of bundle members/root not in members list)
                        YAML::Node localEntsNode;
                        std::unordered_set<Entity> memberSet;
                        memberSet.insert(instance.rootEntity);
                        for (const auto& m : instance.members) {
                            memberSet.insert(m.localEntity);

                            // Also add nested bundle members to memberSet
                            if (registry->getSignature(m.localEntity).test(registry->getComponentId<BundleComponent>())) {
                                const BundleComponent& nbc = registry->getComponent<BundleComponent>(m.localEntity);
                                if (!nbc.path.empty()) {
                                    const EntityBundle* nb = project->getEntityBundle(nbc.path);
                                    if (nb) {
                                        auto nsi = nb->instances.find(sceneProject->id);
                                        if (nsi != nb->instances.end()) {
                                            for (const auto& ni : nsi->second) {
                                                if (ni.rootEntity == m.localEntity) {
                                                    for (const auto& nm : ni.members) {
                                                        memberSet.insert(nm.localEntity);
                                                    }
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Check root and each member for non-member children
                        // Skip nested bundle roots (their local entities are handled separately)
                        std::vector<std::pair<Entity, Entity>> parentCandidates; // {parentLocal, parentRegistry}
                        parentCandidates.push_back({instance.rootEntity, NULL_ENTITY});
                        for (const auto& m : instance.members) {
                            if (registry->getSignature(m.localEntity).test(registry->getComponentId<BundleComponent>())) {
                                continue;
                            }
                            parentCandidates.push_back({m.localEntity, m.registryEntity});
                        }

                        for (const auto& [parentLocal, parentReg] : parentCandidates) {
                            if (!registry->getSignature(parentLocal).test(registry->getComponentId<Transform>())) continue;
                            auto transforms = registry->getComponentArray<Transform>();
                            size_t parentIdx = transforms->getIndex(parentLocal);
                            size_t childPos = 0;
                            for (size_t ti = parentIdx + 1; ti < transforms->size(); ti++) {
                                Transform& t = transforms->getComponentFromIndex(ti);
                                Entity childEnt = transforms->getEntity(ti);
                                if (t.parent != parentLocal) {
                                    if (t.parent == NULL_ENTITY) break;
                                    // Skip deeper descendants
                                    continue;
                                }
                                if (memberSet.find(childEnt) == memberSet.end()) {
                                    // This is a local entity
                                    YAML::Node localEntNode = encodeEntity(childEnt, registry, project, sceneProject);
                                    localEntNode["parentRegistryEntity"] = parentReg;
                                    localEntNode["childIndex"] = childPos;
                                    localEntsNode.push_back(localEntNode);
                                }
                                childPos++;
                            }
                        }

                        // Encode nested bundle local entities (tagged with bundlePath)
                        for (const auto& member : instance.members) {
                            if (!registry->getSignature(member.localEntity).test(registry->getComponentId<BundleComponent>())) continue;
                            const BundleComponent& nestedBCLoc = registry->getComponent<BundleComponent>(member.localEntity);
                            if (nestedBCLoc.path.empty()) continue;

                            const EntityBundle* nestedBundle = project->getEntityBundle(nestedBCLoc.path);
                            if (!nestedBundle) continue;

                            auto nestedSceneIt = nestedBundle->instances.find(sceneProject->id);
                            if (nestedSceneIt == nestedBundle->instances.end()) continue;

                            for (const auto& nestedInst : nestedSceneIt->second) {
                                if (nestedInst.rootEntity != member.localEntity) continue;

                                std::unordered_set<Entity> nestedMemberSet;
                                nestedMemberSet.insert(nestedInst.rootEntity);
                                for (const auto& nm : nestedInst.members) {
                                    nestedMemberSet.insert(nm.localEntity);
                                }

                                std::vector<std::pair<Entity, Entity>> nestedParentCandidates;
                                nestedParentCandidates.push_back({nestedInst.rootEntity, NULL_ENTITY});
                                for (const auto& nm : nestedInst.members) {
                                    nestedParentCandidates.push_back({nm.localEntity, nm.registryEntity});
                                }

                                for (const auto& [nParentLocal, nParentReg] : nestedParentCandidates) {
                                    if (!registry->getSignature(nParentLocal).test(registry->getComponentId<Transform>())) continue;
                                    auto transforms = registry->getComponentArray<Transform>();
                                    size_t nParentIdx = transforms->getIndex(nParentLocal);
                                    size_t nChildPos = 0;
                                    for (size_t ti = nParentIdx + 1; ti < transforms->size(); ti++) {
                                        Transform& t = transforms->getComponentFromIndex(ti);
                                        Entity childEnt = transforms->getEntity(ti);
                                        if (t.parent != nParentLocal) {
                                            if (t.parent == NULL_ENTITY) break;
                                            continue;
                                        }
                                        if (nestedMemberSet.find(childEnt) == nestedMemberSet.end()) {
                                            YAML::Node localEntNode = encodeEntity(childEnt, registry, project, sceneProject);
                                            localEntNode["parentRegistryEntity"] = nParentReg;
                                            localEntNode["childIndex"] = nChildPos;
                                            localEntNode["bundlePath"] = nestedBCLoc.path;
                                            localEntNode["bundleRootRegistryEntity"] = member.registryEntity;
                                            localEntsNode.push_back(localEntNode);
                                        }
                                        nChildPos++;
                                    }
                                }
                                break;
                            }
                        }

                        if (localEntsNode.size() > 0) {
                            entityNode["bundleLocalEntities"] = localEntsNode;
                        }

                        break;
                    }
                }
            }
        // Bundle children are not encoded (they live in the bundle file)
    }else{
        entityNode["type"] = "Entity";

        entityNode["entity"] = entity;
        entityNode["name"] = registry->getEntityName(entity);

        Signature signature = registry->getSignature(entity);
        YAML::Node components = encodeComponents(entity, registry, signature);

        if ((components.IsMap() && components.size() > 0)){
            entityNode["components"] = components;
        }
    }

    return entityNode;
}

YAML::Node editor::Stream::encodeScriptProperty(const ScriptProperty& prop) {
    YAML::Node node;
    node["name"] = prop.name;
    node["displayName"] = prop.displayName;
    node["type"] = scriptPropertyTypeToString(prop.type);

    // Store ptrTypeName if it's a pointer type
    if (prop.type == ScriptPropertyType::EntityReference && !prop.ptrTypeName.empty()) {
        node["ptrTypeName"] = prop.ptrTypeName;
    }

    // getValue falls back to the type default instead of throwing on a stale value
    switch (prop.type) {
        case ScriptPropertyType::Bool:
            node["value"] = prop.getValue<bool>();
            node["defaultValue"] = prop.getDefaultValue<bool>();
            break;
        case ScriptPropertyType::Int:
            node["value"] = prop.getValue<int>();
            node["defaultValue"] = prop.getDefaultValue<int>();
            break;
        case ScriptPropertyType::Float:
            node["value"] = prop.getValue<float>();
            node["defaultValue"] = prop.getDefaultValue<float>();
            break;
        case ScriptPropertyType::String:
            node["value"] = prop.getValue<std::string>();
            node["defaultValue"] = prop.getDefaultValue<std::string>();
            break;
        case ScriptPropertyType::Vector2:
            node["value"] = encodeVector2(prop.getValue<Vector2>());
            node["defaultValue"] = encodeVector2(prop.getDefaultValue<Vector2>());
            break;
        case ScriptPropertyType::Vector3:
        case ScriptPropertyType::Color3:
            node["value"] = encodeVector3(prop.getValue<Vector3>());
            node["defaultValue"] = encodeVector3(prop.getDefaultValue<Vector3>());
            break;
        case ScriptPropertyType::Vector4:
        case ScriptPropertyType::Color4:
            node["value"] = encodeVector4(prop.getValue<Vector4>());
            node["defaultValue"] = encodeVector4(prop.getDefaultValue<Vector4>());
            break;
        case ScriptPropertyType::EntityReference: {
            const EntityReference entRef = prop.getValue<EntityReference>();
            node["value"] = entRef.entity;
            node["defaultValue"] = prop.getDefaultValue<EntityReference>().entity;
            if (entRef.sceneId != 0) {
                node["sceneId"] = entRef.sceneId;
            }
            break;
        }
    }

    return node;
}

ScriptProperty editor::Stream::decodeScriptProperty(const YAML::Node& node) {
    ScriptProperty prop;

    if (node["name"]) prop.name = node["name"].as<std::string>();
    if (node["displayName"]) prop.displayName = node["displayName"].as<std::string>();
    if (node["type"]) prop.type = stringToScriptPropertyType(node["type"].as<std::string>());

    // Restore ptrTypeName if it exists
    if (node["ptrTypeName"]) {
        prop.ptrTypeName = node["ptrTypeName"].as<std::string>();
    }

    if (node["value"]) {
        switch (prop.type) {
            case ScriptPropertyType::Bool:
                prop.value = node["value"].as<bool>();
                prop.defaultValue = node["defaultValue"] ? node["defaultValue"].as<bool>() : false;
                break;
            case ScriptPropertyType::Int:
                prop.value = node["value"].as<int>();
                prop.defaultValue = node["defaultValue"] ? node["defaultValue"].as<int>() : 0;
                break;
            case ScriptPropertyType::Float:
                prop.value = node["value"].as<float>();
                prop.defaultValue = node["defaultValue"] ? node["defaultValue"].as<float>() : 0.0f;
                break;
            case ScriptPropertyType::String:
                prop.value = node["value"].as<std::string>();
                prop.defaultValue = node["defaultValue"] ? node["defaultValue"].as<std::string>() : std::string("");
                break;
            case ScriptPropertyType::Vector2:
                prop.value = decodeVector2(node["value"]);
                prop.defaultValue = node["defaultValue"] ? decodeVector2(node["defaultValue"]) : Vector2();
                break;
            case ScriptPropertyType::Vector3:
            case ScriptPropertyType::Color3:
                prop.value = decodeVector3(node["value"]);
                prop.defaultValue = node["defaultValue"] ? decodeVector3(node["defaultValue"]) : Vector3();
                break;
            case ScriptPropertyType::Vector4:
            case ScriptPropertyType::Color4:
                prop.value = decodeVector4(node["value"]);
                prop.defaultValue = node["defaultValue"] ? decodeVector4(node["defaultValue"]) : Vector4();
                break;
            case ScriptPropertyType::EntityReference: {
                uint32_t sid = node["sceneId"] ? node["sceneId"].as<uint32_t>(0) : 0;
                prop.value = EntityReference{node["value"].as<Entity>(NULL_ENTITY), sid};
                prop.defaultValue = EntityReference{node["defaultValue"] ? node["defaultValue"].as<Entity>(NULL_ENTITY) : Entity(NULL_ENTITY), 0};
                break;
            }
        }
    } else {
        // Initialize with default values if no value is provided
        switch (prop.type) {
            case ScriptPropertyType::Bool: prop.value = false; prop.defaultValue = false; break;
            case ScriptPropertyType::Int: prop.value = 0; prop.defaultValue = 0; break;
            case ScriptPropertyType::Float: prop.value = 0.0f; prop.defaultValue = 0.0f; break;
            case ScriptPropertyType::String: prop.value = std::string(""); prop.defaultValue = std::string(""); break;
            case ScriptPropertyType::Vector2: prop.value = Vector2(); prop.defaultValue = Vector2(); break;
            case ScriptPropertyType::Vector3:
            case ScriptPropertyType::Color3: prop.value = Vector3(); prop.defaultValue = Vector3(); break;
            case ScriptPropertyType::Vector4:
            case ScriptPropertyType::Color4: prop.value = Vector4(); prop.defaultValue = Vector4(); break;
            case ScriptPropertyType::EntityReference: prop.value = EntityReference{NULL_ENTITY, 0}; prop.defaultValue = EntityReference{NULL_ENTITY, 0}; break;
        }
    }

    return prop;
}

std::vector<Entity> editor::Stream::decodeEntity(const YAML::Node& entityNode, EntityRegistry* registry, std::vector<Entity>* entities, Project* project, SceneProject* sceneProject, Entity parent, bool createNewIfExists, bool removeMissingComponents, std::unordered_map<Entity, Entity>* entityRemap) {
    std::vector<Entity> allEntities;

    if (!entityNode || !entityNode.IsMap() || !entityNode["type"]) {
        return allEntities;
    }

    std::string entityType = entityNode["type"].as<std::string>();

    if (entityType == "Entity") {

        Entity entity = NULL_ENTITY;
        if (entityNode["entity"]){
            Entity serializedEntity = entityNode["entity"].as<Entity>();
            entity = serializedEntity;
            if (!registry->recreateEntity(entity)){
                if (createNewIfExists){
                    entity = registry->createUserEntity();
                }
            }
            if (entityRemap && entity != serializedEntity) {
                (*entityRemap)[serializedEntity] = entity;
            }
        }else{
            entity = registry->createUserEntity();
        }

        allEntities.push_back(entity);

        if (entities){
            entities->push_back(entity);
        }

        std::string name = entityNode["name"].as<std::string>();
        registry->setEntityName(entity, name);

        if (entityNode["components"]){
            decodeComponents(entity, parent, registry, entityNode["components"]);
        }

        // Components added to a live entity after it was encoded, like a script calling
        // getBody3D(), are not removed by decodeComponents
        if (removeMissingComponents){
            YAML::Node savedComponents = entityNode["components"];
            std::vector<Entity> unusedEntities;
            std::vector<Entity>& sceneEntities = entities ? *entities : (sceneProject ? sceneProject->entities : unusedEntities);

            for (ComponentType cpType : Catalog::findComponents(registry, entity)){
                if (savedComponents && savedComponents[Catalog::getComponentName(cpType, true)]) continue;
                ProjectUtils::removeEntityComponent(registry, entity, cpType, sceneEntities);
            }
        }

        // If entity has BundleComponent, import bundle children from its path
        if (project && sceneProject) {
            BundleComponent* bundleComp = registry->findComponent<BundleComponent>(entity);
            if (bundleComp && !bundleComp->path.empty()) {
                std::vector<Entity> bundleEntities = project->importEntityBundle(
                    sceneProject, entities, bundleComp->path, entity, false,
                    entityNode["bundleOverrides"], entityNode["bundleLocalEntities"], entityRemap);
                allEntities.insert(allEntities.end(), bundleEntities.begin(), bundleEntities.end());
            }
        }

        // Decode children from actualNode
        if (entityNode["children"]) {
            for (const auto& childNode : entityNode["children"]) {
                std::vector<Entity> childEntities = decodeEntity(childNode, registry, entities, project, sceneProject, entity, createNewIfExists, removeMissingComponents, entityRemap);
                std::copy(childEntities.begin(), childEntities.end(), std::back_inserter(allEntities));
            }
        }
    }

    return allEntities;
}

YAML::Node editor::Stream::encodeMaterial(const Material& material, bool embedTextureData) {
    YAML::Node node;

    // Encode shader part properties
    node["baseColorFactor"] = encodeVector4(material.baseColorFactor);
    node["metallicFactor"] = material.metallicFactor;
    node["roughnessFactor"] = material.roughnessFactor;
    node["alphaCutoff"] = material.alphaCutoff;
    node["alphaMode"] = materialAlphaModeToString(material.alphaMode);
    node["emissiveFactor"] = encodeVector3(material.emissiveFactor);

    // Encode textures using the helper method
    if (!material.baseColorTexture.empty()) {
        node["baseColorTexture"] = encodeTexture(material.baseColorTexture, embedTextureData);
    }

    if (!material.emissiveTexture.empty()) {
        node["emissiveTexture"] = encodeTexture(material.emissiveTexture, embedTextureData);
    }

    if (!material.metallicRoughnessTexture.empty()) {
        node["metallicRoughnessTexture"] = encodeTexture(material.metallicRoughnessTexture, embedTextureData);
    }

    if (!material.occlusionTexture.empty()) {
        node["occlusionTexture"] = encodeTexture(material.occlusionTexture, embedTextureData);
    }

    if (!material.normalTexture.empty()) {
        node["normalTexture"] = encodeTexture(material.normalTexture, embedTextureData);
    }

    // Per-texture UV set (glTF texCoord): 0 = a_texcoord1, 1 = a_texcoord2
    node["baseColorTexCoord"] = material.baseColorTexCoord;
    node["metallicRoughnessTexCoord"] = material.metallicRoughnessTexCoord;
    node["occlusionTexCoord"] = material.occlusionTexCoord;
    node["emissiveTexCoord"] = material.emissiveTexCoord;
    node["normalTexCoord"] = material.normalTexCoord;

    // Encode material name
    node["name"] = material.name;

    return node;
}

Material editor::Stream::decodeMaterial(const YAML::Node& node) {
    Material material;

    material.baseColorFactor = decodeVector4(node["baseColorFactor"]);
    material.metallicFactor = node["metallicFactor"].as<float>();
    material.roughnessFactor = node["roughnessFactor"].as<float>();
    if (node["alphaCutoff"]) material.alphaCutoff = node["alphaCutoff"].as<float>();
    if (node["alphaMode"]) {
        material.alphaMode = stringToMaterialAlphaMode(node["alphaMode"].as<std::string>());
    }
    material.emissiveFactor = decodeVector3(node["emissiveFactor"]);

    if (node["baseColorTexture"]) {
        material.baseColorTexture = decodeTexture(node["baseColorTexture"]);
    }

    if (node["emissiveTexture"]) {
        material.emissiveTexture = decodeTexture(node["emissiveTexture"]);
    }

    if (node["metallicRoughnessTexture"]) {
        material.metallicRoughnessTexture = decodeTexture(node["metallicRoughnessTexture"]);
    }

    if (node["occlusionTexture"]) {
        material.occlusionTexture = decodeTexture(node["occlusionTexture"]);
    }

    if (node["normalTexture"]) {
        material.normalTexture = decodeTexture(node["normalTexture"]);
    }

    if (node["baseColorTexCoord"]) material.baseColorTexCoord = node["baseColorTexCoord"].as<int>();
    if (node["metallicRoughnessTexCoord"]) material.metallicRoughnessTexCoord = node["metallicRoughnessTexCoord"].as<int>();
    if (node["occlusionTexCoord"]) material.occlusionTexCoord = node["occlusionTexCoord"].as<int>();
    if (node["emissiveTexCoord"]) material.emissiveTexCoord = node["emissiveTexCoord"].as<int>();
    if (node["normalTexCoord"]) material.normalTexCoord = node["normalTexCoord"].as<int>();

    material.name = node["name"].as<std::string>();

    return material;
}

// A mesh whose geometry is regenerated from a model's source file on load must not
// serialize its buffers/embedded textures (they are large and redundant). This holds
// for an entity carrying the ModelComponent itself, and for the per-node child mesh
// entities a multi-node model spreads its geometry across (tracked in meshNodesMapping).
bool editor::Stream::isModelBackedMesh(const Entity entity, const EntityRegistry* registry, Signature signature) {
    const auto modelId = registry->getComponentId<ModelComponent>();
    const auto transformId = registry->getComponentId<Transform>();

    if (signature.test(modelId) && !registry->getComponent<ModelComponent>(entity).filename.empty()) {
        return true;
    }
    if (!signature.test(transformId)) {
        return false;
    }
    // Walk up to the owning model: a child mesh is model-backed only if that model has a
    // source file and lists this entity among its generated nodes (manually added child
    // meshes are not in meshNodesMapping and must still serialize their geometry).
    Entity ancestor = registry->getComponent<Transform>(entity).parent;
    while (ancestor != NULL_ENTITY) {
        Signature aSig = registry->getSignature(ancestor);
        if (aSig.test(modelId)) {
            const ModelComponent& model = registry->getComponent<ModelComponent>(ancestor);
            if (!model.filename.empty()) {
                for (const auto& [nodeIdx, mappedEntity] : model.meshNodesMapping) {
                    if (mappedEntity == entity) {
                        return true;
                    }
                }
            }
            return false; // owning model found, but this entity is not one of its meshes
        }
        if (!aSig.test(transformId)) {
            break;
        }
        ancestor = registry->getComponent<Transform>(ancestor).parent;
    }
    return false;
}

bool editor::Stream::isGeneratedMesh(const EntityRegistry* registry, Signature signature) {
    return signature.test(registry->getComponentId<SpriteComponent>()) ||
           signature.test(registry->getComponentId<TilemapComponent>()) ||
           signature.test(registry->getComponentId<MeshPolygonComponent>()) ||
           signature.test(registry->getComponentId<TerrainComponent>());
}

YAML::Node editor::Stream::encodeComponents(const Entity entity, const EntityRegistry* registry, Signature signature) {
    YAML::Node compNode;

    if (signature.test(registry->getComponentId<Transform>())) {
        Transform transform = registry->getComponent<Transform>(entity);
        compNode[Catalog::getComponentName(ComponentType::Transform, true)] = encodeTransform(transform);
    }

    if (signature.test(registry->getComponentId<MeshComponent>())) {
        // Only treat the mesh as model-backed (and skip encoding its buffers and
        // embedded textures) when its geometry is regenerated from a model source file
        // at runtime. This covers the ModelComponent root and the per-node child mesh
        // entities a multi-node model spreads its geometry across. An empty filename (or
        // a manually added mesh) loads no geometry, so the mesh data must still be
        // encoded to remain visible.
        bool isModel = isModelBackedMesh(entity, registry, signature);
        // a generated mesh rebuilds its buffers on load, but not its textures
        bool isGenerated = isModel || isGeneratedMesh(registry, signature);
        MeshComponent mesh = registry->getComponent<MeshComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::MeshComponent, true)] = encodeMeshComponent(mesh, !isGenerated, !isModel, !isGenerated);
    }

    if (signature.test(registry->getComponentId<UIComponent>())) {
        const bool isText = signature.test(registry->getComponentId<TextComponent>());
        UIComponent ui = registry->getComponent<UIComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::UIComponent, true)] = encodeUIComponent(ui, !isText);
    }

    if (signature.test(registry->getComponentId<ButtonComponent>())) {
        ButtonComponent button = registry->getComponent<ButtonComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ButtonComponent, true)] = encodeButtonComponent(button);
    }

    if (signature.test(registry->getComponentId<ScrollbarComponent>())) {
        ScrollbarComponent scrollbar = registry->getComponent<ScrollbarComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ScrollbarComponent, true)] = encodeScrollbarComponent(scrollbar);
    }

    if (signature.test(registry->getComponentId<ProgressbarComponent>())) {
        ProgressbarComponent progressbar = registry->getComponent<ProgressbarComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ProgressbarComponent, true)] = encodeProgressbarComponent(progressbar);
    }

    if (signature.test(registry->getComponentId<TextEditComponent>())) {
        TextEditComponent textedit = registry->getComponent<TextEditComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::TextEditComponent, true)] = encodeTextEditComponent(textedit);
    }

    if (signature.test(registry->getComponentId<PanelComponent>())) {
        PanelComponent panel = registry->getComponent<PanelComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::PanelComponent, true)] = encodePanelComponent(panel);
    }

    if (signature.test(registry->getComponentId<UILayoutComponent>())) {
        UILayoutComponent layout = registry->getComponent<UILayoutComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::UILayoutComponent, true)] = encodeUILayoutComponent(layout);
    }

    if (signature.test(registry->getComponentId<UIContainerComponent>())) {
        UIContainerComponent container = registry->getComponent<UIContainerComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::UIContainerComponent, true)] = encodeUIContainerComponent(container);
    }

    if (signature.test(registry->getComponentId<TextComponent>())) {
        TextComponent text = registry->getComponent<TextComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::TextComponent, true)] = encodeTextComponent(text);
    }

    if (signature.test(registry->getComponentId<ImageComponent>())) {
        ImageComponent image = registry->getComponent<ImageComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ImageComponent, true)] = encodeImageComponent(image);
    }

    if (signature.test(registry->getComponentId<SpriteComponent>())) {
        SpriteComponent sprite = registry->getComponent<SpriteComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::SpriteComponent, true)] = encodeSpriteComponent(sprite);
    }

    if (signature.test(registry->getComponentId<TilemapComponent>())) {
        TilemapComponent tilemap = registry->getComponent<TilemapComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::TilemapComponent, true)] = encodeTilemapComponent(tilemap);
    }

    if (signature.test(registry->getComponentId<TerrainComponent>())) {
        TerrainComponent terrain = registry->getComponent<TerrainComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::TerrainComponent, true)] = encodeTerrainComponent(terrain);
    }

    if (signature.test(registry->getComponentId<LightComponent>())) {
        LightComponent light = registry->getComponent<LightComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::LightComponent, true)] = encodeLightComponent(light);
    }

    if (signature.test(registry->getComponentId<Light2DComponent>())) {
        Light2DComponent light2d = registry->getComponent<Light2DComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::Light2DComponent, true)] = encodeLight2DComponent(light2d);
    }

    if (signature.test(registry->getComponentId<Occluder2DComponent>())) {
        Occluder2DComponent occluder2d = registry->getComponent<Occluder2DComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::Occluder2DComponent, true)] = encodeOccluder2DComponent(occluder2d);
    }

    if (signature.test(registry->getComponentId<FogComponent>())) {
        FogComponent fog = registry->getComponent<FogComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::FogComponent, true)] = encodeFogComponent(fog);
    }

    if (signature.test(registry->getComponentId<MirrorComponent>())) {
        MirrorComponent mirror = registry->getComponent<MirrorComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::MirrorComponent, true)] = encodeMirrorComponent(mirror);
    }

    if (signature.test(registry->getComponentId<ReflectionProbeComponent>())) {
        ReflectionProbeComponent probe = registry->getComponent<ReflectionProbeComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ReflectionProbeComponent, true)] = encodeReflectionProbeComponent(probe);
    }

    if (signature.test(registry->getComponentId<CameraComponent>())) {
        CameraComponent camera = registry->getComponent<CameraComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::CameraComponent, true)] = encodeCameraComponent(camera);
    }

    if (signature.test(registry->getComponentId<SoundComponent>())) {
        SoundComponent audio = registry->getComponent<SoundComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::SoundComponent, true)] = encodeSoundComponent(audio);
    }

    if (signature.test(registry->getComponentId<ScriptComponent>())) {
        ScriptComponent script = registry->getComponent<ScriptComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ScriptComponent, true)] = encodeScriptComponent(script);
    }

    if (signature.test(registry->getComponentId<SkyComponent>())) {
        SkyComponent sky = registry->getComponent<SkyComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::SkyComponent, true)] = encodeSkyComponent(sky);
    }

    if (signature.test(registry->getComponentId<ModelComponent>())) {
        ModelComponent model = registry->getComponent<ModelComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ModelComponent, true)] = encodeModelComponent(model);
    }

    if (signature.test(registry->getComponentId<Body2DComponent>())) {
        Body2DComponent body = registry->getComponent<Body2DComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::Body2DComponent, true)] = encodeBody2DComponent(body);
    }

    if (signature.test(registry->getComponentId<Body3DComponent>())) {
        Body3DComponent body = registry->getComponent<Body3DComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::Body3DComponent, true)] = encodeBody3DComponent(body);
    }

    if (signature.test(registry->getComponentId<Joint2DComponent>())) {
        Joint2DComponent joint = registry->getComponent<Joint2DComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::Joint2DComponent, true)] = encodeJoint2DComponent(joint);
    }

    if (signature.test(registry->getComponentId<Joint3DComponent>())) {
        Joint3DComponent joint = registry->getComponent<Joint3DComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::Joint3DComponent, true)] = encodeJoint3DComponent(joint);
    }

    if (signature.test(registry->getComponentId<ActionComponent>())) {
        ActionComponent action = registry->getComponent<ActionComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ActionComponent, true)] = encodeActionComponent(action);
    }

    if (signature.test(registry->getComponentId<TimedActionComponent>())) {
        TimedActionComponent timed = registry->getComponent<TimedActionComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::TimedActionComponent, true)] = encodeTimedActionComponent(timed);
    }

    if (signature.test(registry->getComponentId<PositionActionComponent>())) {
        PositionActionComponent posAction = registry->getComponent<PositionActionComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::PositionActionComponent, true)] = encodePositionActionComponent(posAction);
    }

    if (signature.test(registry->getComponentId<RotationActionComponent>())) {
        RotationActionComponent rotAction = registry->getComponent<RotationActionComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::RotationActionComponent, true)] = encodeRotationActionComponent(rotAction);
    }

    if (signature.test(registry->getComponentId<ScaleActionComponent>())) {
        ScaleActionComponent scaleAction = registry->getComponent<ScaleActionComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ScaleActionComponent, true)] = encodeScaleActionComponent(scaleAction);
    }

    if (signature.test(registry->getComponentId<ColorActionComponent>())) {
        ColorActionComponent colorAction = registry->getComponent<ColorActionComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ColorActionComponent, true)] = encodeColorActionComponent(colorAction);
    }

    if (signature.test(registry->getComponentId<AlphaActionComponent>())) {
        AlphaActionComponent alphaAction = registry->getComponent<AlphaActionComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::AlphaActionComponent, true)] = encodeAlphaActionComponent(alphaAction);
    }

    if (signature.test(registry->getComponentId<SpriteAnimationComponent>())) {
        SpriteAnimationComponent spriteanim = registry->getComponent<SpriteAnimationComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::SpriteAnimationComponent, true)] = encodeSpriteAnimationComponent(spriteanim);
    }

    if (signature.test(registry->getComponentId<AnimationComponent>())) {
        AnimationComponent animation = registry->getComponent<AnimationComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::AnimationComponent, true)] = encodeAnimationComponent(animation);
    }

    if (signature.test(registry->getComponentId<BoneComponent>())) {
        BoneComponent bone = registry->getComponent<BoneComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::BoneComponent, true)] = encodeBoneComponent(bone);
    }

    if (signature.test(registry->getComponentId<KeyframeTracksComponent>())) {
        KeyframeTracksComponent tracks = registry->getComponent<KeyframeTracksComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::KeyframeTracksComponent, true)] = encodeKeyframeTracksComponent(tracks);
    }

    if (signature.test(registry->getComponentId<TranslateTracksComponent>())) {
        TranslateTracksComponent tracks = registry->getComponent<TranslateTracksComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::TranslateTracksComponent, true)] = encodeTranslateTracksComponent(tracks);
    }

    if (signature.test(registry->getComponentId<RotateTracksComponent>())) {
        RotateTracksComponent tracks = registry->getComponent<RotateTracksComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::RotateTracksComponent, true)] = encodeRotateTracksComponent(tracks);
    }

    if (signature.test(registry->getComponentId<ScaleTracksComponent>())) {
        ScaleTracksComponent tracks = registry->getComponent<ScaleTracksComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ScaleTracksComponent, true)] = encodeScaleTracksComponent(tracks);
    }

    if (signature.test(registry->getComponentId<MorphTracksComponent>())) {
        MorphTracksComponent tracks = registry->getComponent<MorphTracksComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::MorphTracksComponent, true)] = encodeMorphTracksComponent(tracks);
    }

    if (signature.test(registry->getComponentId<ParticlesComponent>())) {
        ParticlesComponent particles = registry->getComponent<ParticlesComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::ParticlesComponent, true)] = encodeParticlesComponent(particles);
    }

    if (signature.test(registry->getComponentId<PointsComponent>())) {
        PointsComponent pts = registry->getComponent<PointsComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::PointsComponent, true)] = encodePointsComponent(pts);
    }

    if (signature.test(registry->getComponentId<LinesComponent>())) {
        LinesComponent lines = registry->getComponent<LinesComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::LinesComponent, true)] = encodeLinesComponent(lines);
    }

    if (signature.test(registry->getComponentId<PolygonComponent>())) {
        PolygonComponent polygon = registry->getComponent<PolygonComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::PolygonComponent, true)] = encodePolygonComponent(polygon);
    }

    if (signature.test(registry->getComponentId<MeshPolygonComponent>())) {
        MeshPolygonComponent polygon = registry->getComponent<MeshPolygonComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::MeshPolygonComponent, true)] = encodeMeshPolygonComponent(polygon);
    }

    if (signature.test(registry->getComponentId<InstancedMeshComponent>())) {
        InstancedMeshComponent instmesh = registry->getComponent<InstancedMeshComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::InstancedMeshComponent, true)] = encodeInstancedMeshComponent(instmesh);
    }

    if (signature.test(registry->getComponentId<BundleComponent>())) {
        BundleComponent bundle = registry->getComponent<BundleComponent>(entity);
        compNode[Catalog::getComponentName(ComponentType::BundleComponent, true)] = encodeBundleComponent(bundle);
    }

    return compNode;
}

void editor::Stream::decodeComponents(Entity entity, Entity parent, EntityRegistry* registry, const YAML::Node& compNode){
    std::string compName;

    Signature signature = registry->getSignature(entity);

    compName = Catalog::getComponentName(ComponentType::Transform, true);
    if (compNode[compName]) {
        Transform* existing = registry->findComponent<Transform>(entity);
        Transform transform = decodeTransform(compNode[compName], existing);
        transform.parent = parent;
        if (!signature.test(registry->getComponentId<Transform>())){
            registry->addComponent<Transform>(entity, transform);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::Transform, existing, &transform);
            registry->addEntityChild(transform.parent, entity, false);
            registry->getComponent<Transform>(entity) = transform;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::MeshComponent, true);
    if (compNode[compName]) {
        MeshComponent* existing = registry->findComponent<MeshComponent>(entity);
        MeshComponent mesh = decodeMeshComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<MeshComponent>())){
            registry->addComponent<MeshComponent>(entity, mesh);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::MeshComponent, existing, &mesh);
            registry->getComponent<MeshComponent>(entity) = mesh;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::UIComponent, true);
    if (compNode[compName]) {
        UIComponent* existing = registry->findComponent<UIComponent>(entity);
        UIComponent ui = decodeUIComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<UIComponent>())){
            registry->addComponent<UIComponent>(entity, ui);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::UIComponent, existing, &ui);
            registry->getComponent<UIComponent>(entity) = ui;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ButtonComponent, true);
    if (compNode[compName]) {
        ButtonComponent* existing = registry->findComponent<ButtonComponent>(entity);
        ButtonComponent button = decodeButtonComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ButtonComponent>())){
            registry->addComponent<ButtonComponent>(entity, button);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ButtonComponent, existing, &button);
            registry->getComponent<ButtonComponent>(entity) = button;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ScrollbarComponent, true);
    if (compNode[compName]) {
        ScrollbarComponent* existing = registry->findComponent<ScrollbarComponent>(entity);
        ScrollbarComponent scrollbar = decodeScrollbarComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ScrollbarComponent>())){
            registry->addComponent<ScrollbarComponent>(entity, scrollbar);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ScrollbarComponent, existing, &scrollbar);
            registry->getComponent<ScrollbarComponent>(entity) = scrollbar;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ProgressbarComponent, true);
    if (compNode[compName]) {
        ProgressbarComponent* existing = registry->findComponent<ProgressbarComponent>(entity);
        ProgressbarComponent progressbar = decodeProgressbarComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ProgressbarComponent>())){
            registry->addComponent<ProgressbarComponent>(entity, progressbar);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ProgressbarComponent, existing, &progressbar);
            registry->getComponent<ProgressbarComponent>(entity) = progressbar;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::TextEditComponent, true);
    if (compNode[compName]) {
        TextEditComponent* existing = registry->findComponent<TextEditComponent>(entity);
        TextEditComponent textedit = decodeTextEditComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<TextEditComponent>())){
            registry->addComponent<TextEditComponent>(entity, textedit);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::TextEditComponent, existing, &textedit);
            registry->getComponent<TextEditComponent>(entity) = textedit;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::PanelComponent, true);
    if (compNode[compName]) {
        PanelComponent* existing = registry->findComponent<PanelComponent>(entity);
        PanelComponent panel = decodePanelComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<PanelComponent>())){
            registry->addComponent<PanelComponent>(entity, panel);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::PanelComponent, existing, &panel);
            registry->getComponent<PanelComponent>(entity) = panel;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::UILayoutComponent, true);
    if (compNode[compName]) {
        UILayoutComponent* existing = registry->findComponent<UILayoutComponent>(entity);
        UILayoutComponent layout = decodeUILayoutComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<UILayoutComponent>())){
            registry->addComponent<UILayoutComponent>(entity, layout);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::UILayoutComponent, existing, &layout);
            registry->getComponent<UILayoutComponent>(entity) = layout;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::UIContainerComponent, true);
    if (compNode[compName]) {
        UIContainerComponent* existing = registry->findComponent<UIContainerComponent>(entity);
        UIContainerComponent container = decodeUIContainerComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<UIContainerComponent>())){
            registry->addComponent<UIContainerComponent>(entity, container);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::UIContainerComponent, existing, &container);
            registry->getComponent<UIContainerComponent>(entity) = container;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::TextComponent, true);
    if (compNode[compName]) {
        TextComponent* existing = registry->findComponent<TextComponent>(entity);
        TextComponent text = decodeTextComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<TextComponent>())){
            registry->addComponent<TextComponent>(entity, text);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::TextComponent, existing, &text);
            registry->getComponent<TextComponent>(entity) = text;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ImageComponent, true);
    if (compNode[compName]) {
        ImageComponent* existing = registry->findComponent<ImageComponent>(entity);
        ImageComponent image = decodeImageComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ImageComponent>())){
            registry->addComponent<ImageComponent>(entity, image);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ImageComponent, existing, &image);
            registry->getComponent<ImageComponent>(entity) = image;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::SpriteComponent, true);
    if (compNode[compName]) {
        SpriteComponent* existing = registry->findComponent<SpriteComponent>(entity);
        SpriteComponent sprite = decodeSpriteComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<SpriteComponent>())){
            registry->addComponent<SpriteComponent>(entity, sprite);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::SpriteComponent, existing, &sprite);
            registry->getComponent<SpriteComponent>(entity) = sprite;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::TilemapComponent, true);
    if (compNode[compName]) {
        TilemapComponent* existing = registry->findComponent<TilemapComponent>(entity);
        TilemapComponent tilemap = decodeTilemapComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<TilemapComponent>())){
            registry->addComponent<TilemapComponent>(entity, tilemap);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::TilemapComponent, existing, &tilemap);
            registry->getComponent<TilemapComponent>(entity) = tilemap;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::TerrainComponent, true);
    if (compNode[compName]) {
        TerrainComponent* existing = registry->findComponent<TerrainComponent>(entity);
        TerrainComponent terrain = decodeTerrainComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<TerrainComponent>())){
            registry->addComponent<TerrainComponent>(entity, terrain);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::TerrainComponent, existing, &terrain);
            registry->getComponent<TerrainComponent>(entity) = terrain;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::LightComponent, true);
    if (compNode[compName]) {
        LightComponent* existing = registry->findComponent<LightComponent>(entity);
        LightComponent light = decodeLightComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<LightComponent>())){
            registry->addComponent<LightComponent>(entity, light);
            Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(ComponentType::LightComponent));
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::LightComponent, existing, &light);
            registry->getComponent<LightComponent>(entity) = light;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::Light2DComponent, true);
    if (compNode[compName]) {
        Light2DComponent* existing = registry->findComponent<Light2DComponent>(entity);
        Light2DComponent light2d = decodeLight2DComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<Light2DComponent>())){
            registry->addComponent<Light2DComponent>(entity, light2d);
            Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(ComponentType::Light2DComponent));
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::Light2DComponent, existing, &light2d);
            registry->getComponent<Light2DComponent>(entity) = light2d;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::Occluder2DComponent, true);
    if (compNode[compName]) {
        Occluder2DComponent* existing = registry->findComponent<Occluder2DComponent>(entity);
        Occluder2DComponent occluder2d = decodeOccluder2DComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<Occluder2DComponent>())){
            registry->addComponent<Occluder2DComponent>(entity, occluder2d);
            Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(ComponentType::Occluder2DComponent));
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::Occluder2DComponent, existing, &occluder2d);
            registry->getComponent<Occluder2DComponent>(entity) = occluder2d;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::FogComponent, true);
    if (compNode[compName]) {
        FogComponent* existing = registry->findComponent<FogComponent>(entity);
        FogComponent fog = decodeFogComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<FogComponent>())){
            registry->addComponent<FogComponent>(entity, fog);
            Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(ComponentType::FogComponent));
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::FogComponent, existing, &fog);
            registry->getComponent<FogComponent>(entity) = fog;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::MirrorComponent, true);
    if (compNode[compName]) {
        MirrorComponent* existing = registry->findComponent<MirrorComponent>(entity);
        MirrorComponent mirror = decodeMirrorComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<MirrorComponent>())){
            registry->addComponent<MirrorComponent>(entity, mirror);
            Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(ComponentType::MirrorComponent));
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::MirrorComponent, existing, &mirror);
            registry->getComponent<MirrorComponent>(entity) = mirror;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ReflectionProbeComponent, true);
    if (compNode[compName]) {
        ReflectionProbeComponent* existing = registry->findComponent<ReflectionProbeComponent>(entity);
        ReflectionProbeComponent probe = decodeReflectionProbeComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ReflectionProbeComponent>())){
            registry->addComponent<ReflectionProbeComponent>(entity, probe);
            Catalog::updateEntity(registry, entity, Catalog::getComponentStructuralUpdateFlags(ComponentType::ReflectionProbeComponent));
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ReflectionProbeComponent, existing, &probe);
            registry->getComponent<ReflectionProbeComponent>(entity) = probe;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::CameraComponent, true);
    if (compNode[compName]) {
        CameraComponent* existing = registry->findComponent<CameraComponent>(entity);
        CameraComponent camera = decodeCameraComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<CameraComponent>())){
            registry->addComponent<CameraComponent>(entity, camera);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::CameraComponent, existing, &camera);
            registry->getComponent<CameraComponent>(entity) = camera;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::SoundComponent, true);
    if (compNode[compName]) {
        SoundComponent* existing = registry->findComponent<SoundComponent>(entity);
        SoundComponent audio = decodeSoundComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<SoundComponent>())){
            registry->addComponent<SoundComponent>(entity, audio);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::SoundComponent, existing, &audio);
            registry->getComponent<SoundComponent>(entity) = audio;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ScriptComponent, true);
    if (compNode[compName]) {
        ScriptComponent* existing = registry->findComponent<ScriptComponent>(entity);
        ScriptComponent script = decodeScriptComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ScriptComponent>())){
            registry->addComponent<ScriptComponent>(entity, script);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ScriptComponent, existing, &script);
            registry->getComponent<ScriptComponent>(entity) = script;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::SkyComponent, true);
    if (compNode[compName]) {
        SkyComponent* existing = registry->findComponent<SkyComponent>(entity);
        SkyComponent sky = decodeSkyComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<SkyComponent>())){
            registry->addComponent<SkyComponent>(entity, sky);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::SkyComponent, existing, &sky);
            registry->getComponent<SkyComponent>(entity) = sky;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ModelComponent, true);
    if (compNode[compName]) {
        ModelComponent* existing = registry->findComponent<ModelComponent>(entity);
        ModelComponent model = decodeModelComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ModelComponent>())){
            registry->addComponent<ModelComponent>(entity, model);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ModelComponent, existing, &model);
            registry->getComponent<ModelComponent>(entity) = model;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::Body2DComponent, true);
    if (compNode[compName]) {
        Body2DComponent* existing = registry->findComponent<Body2DComponent>(entity);
        Body2DComponent body = decodeBody2DComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<Body2DComponent>())){
            registry->addComponent<Body2DComponent>(entity, body);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::Body2DComponent, existing, &body);
            registry->getComponent<Body2DComponent>(entity) = body;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::Body3DComponent, true);
    if (compNode[compName]) {
        Body3DComponent* existing = registry->findComponent<Body3DComponent>(entity);
        Transform* bodyTransform = registry->findComponent<Transform>(entity);
        const Vector3 bodyScale = bodyTransform ? bodyTransform->scale : Vector3::UNIT_SCALE;
        Body3DComponent body = decodeBody3DComponent(compNode[compName], existing, bodyScale);
        if (!signature.test(registry->getComponentId<Body3DComponent>())){
            registry->addComponent<Body3DComponent>(entity, body);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::Body3DComponent, existing, &body);
            registry->getComponent<Body3DComponent>(entity) = body;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::Joint2DComponent, true);
    if (compNode[compName]) {
        Joint2DComponent* existing = registry->findComponent<Joint2DComponent>(entity);
        Joint2DComponent joint = decodeJoint2DComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<Joint2DComponent>())){
            registry->addComponent<Joint2DComponent>(entity, joint);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::Joint2DComponent, existing, &joint);
            registry->getComponent<Joint2DComponent>(entity) = joint;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::Joint3DComponent, true);
    if (compNode[compName]) {
        Joint3DComponent* existing = registry->findComponent<Joint3DComponent>(entity);
        Joint3DComponent joint = decodeJoint3DComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<Joint3DComponent>())){
            registry->addComponent<Joint3DComponent>(entity, joint);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::Joint3DComponent, existing, &joint);
            registry->getComponent<Joint3DComponent>(entity) = joint;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ActionComponent, true);
    if (compNode[compName]) {
        ActionComponent* existing = registry->findComponent<ActionComponent>(entity);
        ActionComponent action = decodeActionComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ActionComponent>())){
            registry->addComponent<ActionComponent>(entity, action);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ActionComponent, existing, &action);
            registry->getComponent<ActionComponent>(entity) = action;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::TimedActionComponent, true);
    if (compNode[compName]) {
        TimedActionComponent* existing = registry->findComponent<TimedActionComponent>(entity);
        TimedActionComponent timed = decodeTimedActionComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<TimedActionComponent>())){
            registry->addComponent<TimedActionComponent>(entity, timed);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::TimedActionComponent, existing, &timed);
            registry->getComponent<TimedActionComponent>(entity) = timed;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::PositionActionComponent, true);
    if (compNode[compName]) {
        PositionActionComponent* existing = registry->findComponent<PositionActionComponent>(entity);
        PositionActionComponent posAction = decodePositionActionComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<PositionActionComponent>())){
            registry->addComponent<PositionActionComponent>(entity, posAction);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::PositionActionComponent, existing, &posAction);
            registry->getComponent<PositionActionComponent>(entity) = posAction;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::RotationActionComponent, true);
    if (compNode[compName]) {
        RotationActionComponent* existing = registry->findComponent<RotationActionComponent>(entity);
        RotationActionComponent rotAction = decodeRotationActionComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<RotationActionComponent>())){
            registry->addComponent<RotationActionComponent>(entity, rotAction);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::RotationActionComponent, existing, &rotAction);
            registry->getComponent<RotationActionComponent>(entity) = rotAction;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ScaleActionComponent, true);
    if (compNode[compName]) {
        ScaleActionComponent* existing = registry->findComponent<ScaleActionComponent>(entity);
        ScaleActionComponent scaleAction = decodeScaleActionComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ScaleActionComponent>())){
            registry->addComponent<ScaleActionComponent>(entity, scaleAction);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ScaleActionComponent, existing, &scaleAction);
            registry->getComponent<ScaleActionComponent>(entity) = scaleAction;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::ColorActionComponent, true);
    if (compNode[compName]) {
        ColorActionComponent* existing = registry->findComponent<ColorActionComponent>(entity);
        ColorActionComponent colorAction = decodeColorActionComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ColorActionComponent>())){
            registry->addComponent<ColorActionComponent>(entity, colorAction);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::ColorActionComponent, existing, &colorAction);
            registry->getComponent<ColorActionComponent>(entity) = colorAction;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::AlphaActionComponent, true);
    if (compNode[compName]) {
        AlphaActionComponent* existing = registry->findComponent<AlphaActionComponent>(entity);
        AlphaActionComponent alphaAction = decodeAlphaActionComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<AlphaActionComponent>())){
            registry->addComponent<AlphaActionComponent>(entity, alphaAction);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::AlphaActionComponent, existing, &alphaAction);
            registry->getComponent<AlphaActionComponent>(entity) = alphaAction;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::SpriteAnimationComponent, true);
    if (compNode[compName]) {
        SpriteAnimationComponent* existing = registry->findComponent<SpriteAnimationComponent>(entity);
        SpriteAnimationComponent spriteanim = decodeSpriteAnimationComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<SpriteAnimationComponent>())){
            registry->addComponent<SpriteAnimationComponent>(entity, spriteanim);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::SpriteAnimationComponent, existing, &spriteanim);
            registry->getComponent<SpriteAnimationComponent>(entity) = spriteanim;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::AnimationComponent, true);
    if (compNode[compName]) {
        AnimationComponent* existing = registry->findComponent<AnimationComponent>(entity);
        AnimationComponent animation = decodeAnimationComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<AnimationComponent>())){
            registry->addComponent<AnimationComponent>(entity, animation);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::AnimationComponent, existing, &animation);
            registry->getComponent<AnimationComponent>(entity) = animation;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::BoneComponent, true);
    if (compNode[compName]) {
        BoneComponent* existing = registry->findComponent<BoneComponent>(entity);
        BoneComponent bone = decodeBoneComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<BoneComponent>())){
            registry->addComponent<BoneComponent>(entity, bone);
        }else{
            registry->getComponent<BoneComponent>(entity) = bone;
        }
    }

    compName = Catalog::getComponentName(ComponentType::KeyframeTracksComponent, true);
    if (compNode[compName]) {
        KeyframeTracksComponent* existing = registry->findComponent<KeyframeTracksComponent>(entity);
        KeyframeTracksComponent tracks = decodeKeyframeTracksComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<KeyframeTracksComponent>())){
            registry->addComponent<KeyframeTracksComponent>(entity, tracks);
        }else{
            registry->getComponent<KeyframeTracksComponent>(entity) = tracks;
        }
    }

    compName = Catalog::getComponentName(ComponentType::TranslateTracksComponent, true);
    if (compNode[compName]) {
        TranslateTracksComponent* existing = registry->findComponent<TranslateTracksComponent>(entity);
        TranslateTracksComponent tracks = decodeTranslateTracksComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<TranslateTracksComponent>())){
            registry->addComponent<TranslateTracksComponent>(entity, tracks);
        }else{
            registry->getComponent<TranslateTracksComponent>(entity) = tracks;
        }
    }

    compName = Catalog::getComponentName(ComponentType::RotateTracksComponent, true);
    if (compNode[compName]) {
        RotateTracksComponent* existing = registry->findComponent<RotateTracksComponent>(entity);
        RotateTracksComponent tracks = decodeRotateTracksComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<RotateTracksComponent>())){
            registry->addComponent<RotateTracksComponent>(entity, tracks);
        }else{
            registry->getComponent<RotateTracksComponent>(entity) = tracks;
        }
    }

    compName = Catalog::getComponentName(ComponentType::ScaleTracksComponent, true);
    if (compNode[compName]) {
        ScaleTracksComponent* existing = registry->findComponent<ScaleTracksComponent>(entity);
        ScaleTracksComponent tracks = decodeScaleTracksComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ScaleTracksComponent>())){
            registry->addComponent<ScaleTracksComponent>(entity, tracks);
        }else{
            registry->getComponent<ScaleTracksComponent>(entity) = tracks;
        }
    }

    compName = Catalog::getComponentName(ComponentType::MorphTracksComponent, true);
    if (compNode[compName]) {
        MorphTracksComponent* existing = registry->findComponent<MorphTracksComponent>(entity);
        MorphTracksComponent tracks = decodeMorphTracksComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<MorphTracksComponent>())){
            registry->addComponent<MorphTracksComponent>(entity, tracks);
        }else{
            registry->getComponent<MorphTracksComponent>(entity) = tracks;
        }
    }

    compName = Catalog::getComponentName(ComponentType::ParticlesComponent, true);
    if (compNode[compName]) {
        ParticlesComponent* existing = registry->findComponent<ParticlesComponent>(entity);
        ParticlesComponent particles = decodeParticlesComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<ParticlesComponent>())){
            registry->addComponent<ParticlesComponent>(entity, particles);
        }else{
            registry->getComponent<ParticlesComponent>(entity) = particles;
        }
    }

    compName = Catalog::getComponentName(ComponentType::PointsComponent, true);
    if (compNode[compName]) {
        PointsComponent* existing = registry->findComponent<PointsComponent>(entity);
        PointsComponent pts = decodePointsComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<PointsComponent>())) {
            registry->addComponent<PointsComponent>(entity, pts);
        } else {
            registry->getComponent<PointsComponent>(entity) = pts;
        }
    }

    compName = Catalog::getComponentName(ComponentType::LinesComponent, true);
    if (compNode[compName]) {
        LinesComponent* existing = registry->findComponent<LinesComponent>(entity);
        LinesComponent lines = decodeLinesComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<LinesComponent>())) {
            registry->addComponent<LinesComponent>(entity, lines);
        } else {
            registry->getComponent<LinesComponent>(entity) = lines;
        }
    }

    compName = Catalog::getComponentName(ComponentType::PolygonComponent, true);
    if (compNode[compName]) {
        PolygonComponent* existing = registry->findComponent<PolygonComponent>(entity);
        PolygonComponent polygon = decodePolygonComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<PolygonComponent>())) {
            registry->addComponent<PolygonComponent>(entity, polygon);
        } else {
            registry->getComponent<PolygonComponent>(entity) = polygon;
        }
    }

    compName = Catalog::getComponentName(ComponentType::MeshPolygonComponent, true);
    if (compNode[compName]) {
        MeshPolygonComponent* existing = registry->findComponent<MeshPolygonComponent>(entity);
        MeshPolygonComponent polygon = decodeMeshPolygonComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<MeshPolygonComponent>())) {
            registry->addComponent<MeshPolygonComponent>(entity, polygon);
        } else {
            registry->getComponent<MeshPolygonComponent>(entity) = polygon;
        }
    }

    compName = Catalog::getComponentName(ComponentType::InstancedMeshComponent, true);
    if (compNode[compName]) {
        InstancedMeshComponent* existing = registry->findComponent<InstancedMeshComponent>(entity);
        InstancedMeshComponent instmesh = decodeInstancedMeshComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<InstancedMeshComponent>())){
            registry->addComponent<InstancedMeshComponent>(entity, instmesh);
        }else{
            uint64_t flags = Catalog::getChangedUpdateFlags(ComponentType::InstancedMeshComponent, existing, &instmesh);
            registry->getComponent<InstancedMeshComponent>(entity) = instmesh;
            Catalog::updateEntity(registry, entity, flags);
        }
    }

    compName = Catalog::getComponentName(ComponentType::BundleComponent, true);
    if (compNode[compName]) {
        BundleComponent* existing = registry->findComponent<BundleComponent>(entity);
        BundleComponent bundle = decodeBundleComponent(compNode[compName], existing);
        if (!signature.test(registry->getComponentId<BundleComponent>())){
            registry->addComponent<BundleComponent>(entity, bundle);
        }else{
            registry->getComponent<BundleComponent>(entity) = bundle;
        }
    }
}

YAML::Node editor::Stream::encodeTransform(const Transform& transform) {
    YAML::Node transformNode;

    transformNode["position"] = encodeVector3(transform.position);
    transformNode["rotation"] = encodeQuaternion(transform.rotation);
    transformNode["scale"] = encodeVector3(transform.scale);
    transformNode["worldPosition"] = encodeVector3(transform.worldPosition);
    transformNode["worldRotation"] = encodeQuaternion(transform.worldRotation);
    transformNode["worldScale"] = encodeVector3(transform.worldScale);
    transformNode["localMatrix"] = encodeMatrix4(transform.localMatrix);
    transformNode["modelMatrix"] = encodeMatrix4(transform.modelMatrix);
    transformNode["normalMatrix"] = encodeMatrix4(transform.normalMatrix);
    //transformNode["modelViewProjectionMatrix"] = encodeMatrix4(transform.modelViewProjectionMatrix);
    transformNode["visible"] = transform.visible;
    //transformNode["parent"] = transform.parent;
    //transformNode["distanceToCamera"] = transform.distanceToCamera;
    transformNode["billboardRotation"] = encodeQuaternion(transform.billboardRotation);
    transformNode["billboard"] = transform.billboard;
    transformNode["fakeBillboard"] = transform.fakeBillboard;
    transformNode["cylindricalBillboard"] = transform.cylindricalBillboard;
    //transformNode["needUpdateChildVisibility"] = transform.needUpdateChildVisibility;
    //transformNode["needUpdate"] = transform.needUpdate;

    return transformNode;
}

Transform editor::Stream::decodeTransform(const YAML::Node& node, const Transform* oldTransform) {
    Transform transform;

    // Use old values as defaults if provided
    if (oldTransform) {
        transform = *oldTransform;
    }

    if (node["position"]) transform.position = decodeVector3(node["position"]);
    if (node["rotation"]) transform.rotation = decodeQuaternion(node["rotation"]);
    if (node["scale"]) transform.scale = decodeVector3(node["scale"]);
    if (node["worldPosition"]) transform.worldPosition = decodeVector3(node["worldPosition"]);
    if (node["worldRotation"]) transform.worldRotation = decodeQuaternion(node["worldRotation"]);
    if (node["worldScale"]) transform.worldScale = decodeVector3(node["worldScale"]);
    if (node["localMatrix"]) transform.localMatrix = decodeMatrix4(node["localMatrix"]);
    if (node["modelMatrix"]) transform.modelMatrix = decodeMatrix4(node["modelMatrix"]);
    if (node["normalMatrix"]) transform.normalMatrix = decodeMatrix4(node["normalMatrix"]);
    //if (node["modelViewProjectionMatrix"]) transform.modelViewProjectionMatrix = decodeMatrix4(node["modelViewProjectionMatrix"]);
    if (node["visible"]) transform.visible = node["visible"].as<bool>();
    //transform.parent = node["parent"].as<Entity>();
    //if (node["distanceToCamera"]) transform.distanceToCamera = node["distanceToCamera"].as<float>();
    if (node["billboardRotation"]) transform.billboardRotation = decodeQuaternion(node["billboardRotation"]);
    if (node["billboard"]) transform.billboard = node["billboard"].as<bool>();
    if (node["fakeBillboard"]) transform.fakeBillboard = node["fakeBillboard"].as<bool>();
    if (node["cylindricalBillboard"]) transform.cylindricalBillboard = node["cylindricalBillboard"].as<bool>();
    //transform.needUpdateChildVisibility = node["needUpdateChildVisibility"].as<bool>();
    //transform.needUpdate = node["needUpdate"].as<bool>();

    return transform;
}

YAML::Node editor::Stream::encodeMeshComponent(const MeshComponent& mesh, bool encodeBuffers, bool embedTextureData, bool encodeBones) {
    YAML::Node node;

    //node["loaded"] = mesh.loaded;
    //node["loadCalled"] = mesh.loadCalled;

    if (encodeBuffers){
        node["buffer"] = encodeInterleavedBuffer(mesh.buffer);
        node["indices"] = encodeIndexBuffer(mesh.indices);

        // Encode external buffers
        YAML::Node eBuffersNode;
        for (unsigned int i = 0; i < mesh.numExternalBuffers; i++) {
            eBuffersNode.push_back(encodeExternalBuffer(mesh.eBuffers[i]));
        }
        node["eBuffers"] = eBuffersNode;
    }

    //node["vertexCount"] = mesh.vertexCount;

    // Encode submeshes
    YAML::Node submeshesNode;
    for(unsigned int i = 0; i < mesh.numSubmeshes; i++) {
        submeshesNode.push_back(encodeSubmesh(mesh.submeshes[i], embedTextureData));
    }
    node["submeshes"] = submeshesNode;
    //node["numSubmeshes"] = mesh.numSubmeshes;

    // Model-backed meshes regenerate these matrices from their skeleton on load.
    if (encodeBones) {
        YAML::Node bonesNode;
        for (size_t i = 0; i < mesh.bonesMatrix.size(); i++)
            bonesNode.push_back(encodeMatrix4(mesh.bonesMatrix[i]));
        node["bonesMatrix"] = bonesNode;
    }

    node["normAdjustJoint"] = mesh.normAdjustJoint;
    node["normAdjustWeight"] = mesh.normAdjustWeight;

    // Encode morph weights array
    YAML::Node morphWeightsNode;
    morphWeightsNode.SetStyle(YAML::EmitterStyle::Flow);
    for(int i = 0; i < MAX_MORPHTARGETS; i++) {
        morphWeightsNode.push_back(mesh.morphWeights[i]);
    }
    node["morphWeights"] = morphWeightsNode;

    // Encode AABBs
    node["aabb"] = encodeAABB(mesh.aabb);
    node["verticesAABB"] = encodeAABB(mesh.verticesAABB);
    node["worldAABB"] = encodeAABB(mesh.worldAABB);

    node["receiveLights"] = mesh.receiveLights;
    node["receiveIBL"] = mesh.receiveIBL;
    node["castShadows"] = mesh.castShadows;
    node["receiveShadows"] = mesh.receiveShadows;
    node["renderInReflectionProbes"] = mesh.renderInReflectionProbes;
    node["shadowsBillboard"] = mesh.shadowsBillboard;
    node["transparent"] = mesh.transparent;

    node["cullingMode"] = cullingModeToString(mesh.cullingMode);
    node["windingOrder"] = windingOrderToString(mesh.windingOrder);

    if (!mesh.customShader.empty())
        node["customShader"] = mesh.customShader;

    //node["needUpdateBuffer"] = mesh.needUpdateBuffer;
    //node["needReload"] = mesh.needReload;

    return node;
}

MeshComponent editor::Stream::decodeMeshComponent(const YAML::Node& node, const MeshComponent* oldMesh) {
    MeshComponent mesh;

    // Use old values as defaults if provided
    if (oldMesh) {
        mesh = *oldMesh;
    }

    //mesh.loaded = node["loaded"].as<bool>();
    //mesh.loadCalled = node["loadCalled"].as<bool>();

    // Decode buffers using generic methods
    if (node["buffer"]) {
        decodeInterleavedBuffer(mesh.buffer, node["buffer"]);
    }

    if (node["indices"]) {
        decodeIndexBuffer(mesh.indices, node["indices"]);
    }

    // Decode external buffers
    if (node["eBuffers"]) {
        auto eBuffersNode = node["eBuffers"];
        for (unsigned int i = 0; i < eBuffersNode.size() && i < mesh.eBuffers.size(); i++) {
            decodeExternalBuffer(mesh.eBuffers[i], eBuffersNode[i]);
        }
        mesh.numExternalBuffers = eBuffersNode.size();
    }

    //mesh.vertexCount = node["vertexCount"].as<uint32_t>();

    // Decode submeshes
    if (node["submeshes"]) {
        auto submeshesNode = node["submeshes"];
        for(unsigned int i = 0; i < submeshesNode.size() && i < mesh.submeshes.size(); i++) {
            mesh.submeshes[i] = decodeSubmesh(submeshesNode[i], &mesh.submeshes[i]);
        }
        mesh.numSubmeshes = submeshesNode.size();
    }

    // Decode bones matrix
    if (node["bonesMatrix"]) {
        auto bonesNode = node["bonesMatrix"];
        mesh.bonesMatrix = HybridArray<Matrix4, MAX_BONES>();
        for (size_t i = 0; i < bonesNode.size() &&
                mesh.bonesMatrix.validIndex(static_cast<int>(i)); i++)
            mesh.bonesMatrix[i] = decodeMatrix4(bonesNode[i]);
        mesh.needUpdateBones = bonesNode.size() > 0;
    }

    if (node["normAdjustJoint"]) mesh.normAdjustJoint = node["normAdjustJoint"].as<float>();
    if (node["normAdjustWeight"]) mesh.normAdjustWeight = node["normAdjustWeight"].as<float>();

    // Decode morph weights
    if (node["morphWeights"]) {
        auto morphWeightsNode = node["morphWeights"];
        for(int i = 0; i < MAX_MORPHTARGETS; i++) {
            mesh.morphWeights[i] = morphWeightsNode[i].as<float>();
        }
    }

    if (node["aabb"]) mesh.aabb = decodeAABB(node["aabb"]);
    if (node["verticesAABB"]) mesh.verticesAABB = decodeAABB(node["verticesAABB"]);
    if (node["worldAABB"]) mesh.worldAABB = decodeAABB(node["worldAABB"]);

    if (node["receiveLights"]) mesh.receiveLights = node["receiveLights"].as<bool>();
    if (node["receiveIBL"]) mesh.receiveIBL = node["receiveIBL"].as<bool>();
    if (node["castShadows"]) mesh.castShadows = node["castShadows"].as<bool>();
    if (node["receiveShadows"]) mesh.receiveShadows = node["receiveShadows"].as<bool>();
    if (node["renderInReflectionProbes"]) mesh.renderInReflectionProbes = node["renderInReflectionProbes"].as<bool>();
    if (node["shadowsBillboard"]) mesh.shadowsBillboard = node["shadowsBillboard"].as<bool>();
    if (node["transparent"]) mesh.transparent = node["transparent"].as<bool>();

    if (node["cullingMode"]) mesh.cullingMode = stringToCullingMode(node["cullingMode"].as<std::string>());
    if (node["windingOrder"]) mesh.windingOrder = stringToWindingOrder(node["windingOrder"].as<std::string>());

    if (node["customShader"]) mesh.customShader = node["customShader"].as<std::string>();

    //mesh.needUpdateBuffer = node["needUpdateBuffer"].as<bool>();
    //mesh.needReload = node["needReload"].as<bool>();

    return mesh;
}

YAML::Node editor::Stream::encodeUIComponent(const UIComponent& ui, bool embedTextureData) {
    YAML::Node node;
    //node["loaded"] = ui.loaded;
    //node["loadCalled"] = ui.loadCalled;
    node["buffer"] = encodeBuffer(ui.buffer);
    node["indices"] = encodeBuffer(ui.indices);
    node["minBufferCount"] = ui.minBufferCount;
    node["minIndicesCount"] = ui.minIndicesCount;

    //node["render"] = {}; // ObjectRender not serialized here
    //node["shader"] = {}; // shared_ptr<ShaderRender> not serialized
    //node["shaderProperties"] = ui.shaderProperties;
    //node["slotVSParams"] = ui.slotVSParams;
    //node["slotFSParams"] = ui.slotFSParams;

    node["primitiveType"] = primitiveTypeToString(ui.primitiveType);
    node["vertexCount"] = ui.vertexCount;

    node["aabb"] = encodeAABB(ui.aabb);
    node["worldAABB"] = encodeAABB(ui.worldAABB);

    node["texture"] = encodeTexture(ui.texture, embedTextureData);
    node["color"] = encodeVector4(ui.color);
    // FunctionSubscribe fields are not serializable

    node["automaticFlipY"] = ui.automaticFlipY;
    node["flipY"] = ui.flipY;

    node["pointerMoved"] = ui.pointerMoved;
    node["focused"] = ui.focused;

    if (!ui.customShader.empty())
        node["customShader"] = ui.customShader;

    //node["needReload"] = ui.needReload;
    //node["needUpdateAABB"] = ui.needUpdateAABB;
    //node["needUpdateBuffer"] = ui.needUpdateBuffer;
    //node["needUpdateTexture"] = ui.needUpdateTexture;

    return node;
}

UIComponent editor::Stream::decodeUIComponent(const YAML::Node& node, const UIComponent* oldUI) {
    UIComponent ui;

    // Use old values as defaults if provided
    if (oldUI) {
        ui = *oldUI;
    }

    //ui.loaded = node["loaded"].as<bool>();
    //ui.loadCalled = node["loadCalled"].as<bool>();

    if (node["buffer"]) decodeBuffer(ui.buffer, node["buffer"]);
    if (node["indices"]) decodeBuffer(ui.indices, node["indices"]);
    if (node["minBufferCount"]) ui.minBufferCount = node["minBufferCount"].as<unsigned int>();
    if (node["minIndicesCount"]) ui.minIndicesCount = node["minIndicesCount"].as<unsigned int>();

    // ui.render and ui.shader can't be deserialized here
    // ui.shaderProperties = node["shaderProperties"].as<std::string>();
    //ui.slotVSParams = node["slotVSParams"].as<int>();
    //ui.slotFSParams = node["slotFSParams"].as<int>();

    if (node["primitiveType"]) ui.primitiveType = stringToPrimitiveType(node["primitiveType"].as<std::string>());
    if (node["vertexCount"]) ui.vertexCount = node["vertexCount"].as<unsigned int>();

    if (node["aabb"]) ui.aabb = decodeAABB(node["aabb"]);
    if (node["worldAABB"]) ui.worldAABB = decodeAABB(node["worldAABB"]);

    if (node["texture"]) ui.texture = decodeTexture(node["texture"]);
    if (node["color"]) ui.color = decodeVector4(node["color"]);

    if (node["automaticFlipY"]) ui.automaticFlipY = node["automaticFlipY"].as<bool>();
    if (node["flipY"]) ui.flipY = node["flipY"].as<bool>();

    if (node["pointerMoved"]) ui.pointerMoved = node["pointerMoved"].as<bool>();
    if (node["focused"]) ui.focused = node["focused"].as<bool>();

    if (node["customShader"]) ui.customShader = node["customShader"].as<std::string>();

    //ui.needReload = node["needReload"].as<bool>();
    //ui.needUpdateAABB = node["needUpdateAABB"].as<bool>();
    //ui.needUpdateBuffer = node["needUpdateBuffer"].as<bool>();
    //ui.needUpdateTexture = node["needUpdateTexture"].as<bool>();

    return ui;
}

YAML::Node editor::Stream::encodeButtonComponent(const ButtonComponent& button) {
    YAML::Node node;
    node["label"] = button.label;
    node["textureNormal"] = encodeTexture(button.textureNormal);
    node["textureHovered"] = encodeTexture(button.textureHovered);
    node["texturePressed"] = encodeTexture(button.texturePressed);
    node["textureDisabled"] = encodeTexture(button.textureDisabled);
    node["colorNormal"] = encodeVector4(button.colorNormal);
    node["colorHovered"] = encodeVector4(button.colorHovered);
    node["colorPressed"] = encodeVector4(button.colorPressed);
    node["colorDisabled"] = encodeVector4(button.colorDisabled);
    node["disabled"] = button.disabled;
    return node;
}

ButtonComponent editor::Stream::decodeButtonComponent(const YAML::Node& node, const ButtonComponent* oldButton) {
    ButtonComponent button;

    if (oldButton) {
        button = *oldButton;
    }

    if (node["label"]) button.label = node["label"].as<Entity>();
    if (node["textureNormal"]) button.textureNormal = decodeTexture(node["textureNormal"]);
    if (node["textureHovered"]) button.textureHovered = decodeTexture(node["textureHovered"]);
    if (node["texturePressed"]) button.texturePressed = decodeTexture(node["texturePressed"]);
    if (node["textureDisabled"]) button.textureDisabled = decodeTexture(node["textureDisabled"]);
    if (node["colorNormal"]) button.colorNormal = decodeVector4(node["colorNormal"]);
    if (node["colorHovered"]) button.colorHovered = decodeVector4(node["colorHovered"]);
    if (node["colorPressed"]) button.colorPressed = decodeVector4(node["colorPressed"]);
    if (node["colorDisabled"]) button.colorDisabled = decodeVector4(node["colorDisabled"]);
    if (node["disabled"]) button.disabled = node["disabled"].as<bool>();

    return button;
}

YAML::Node editor::Stream::encodeScrollbarComponent(const ScrollbarComponent& scrollbar) {
    YAML::Node node;
    node["bar"] = scrollbar.bar;
    node["type"] = scrollbarTypeToString(scrollbar.type);
    node["barSize"] = scrollbar.barSize;
    node["step"] = scrollbar.step;
    node["barMarginLeft"] = scrollbar.barMarginLeft;
    node["barMarginRight"] = scrollbar.barMarginRight;
    node["barMarginTop"] = scrollbar.barMarginTop;
    node["barMarginBottom"] = scrollbar.barMarginBottom;
    return node;
}

ScrollbarComponent editor::Stream::decodeScrollbarComponent(const YAML::Node& node, const ScrollbarComponent* oldScrollbar) {
    ScrollbarComponent scrollbar;

    if (oldScrollbar) {
        scrollbar = *oldScrollbar;
    }

    if (node["bar"]) scrollbar.bar = node["bar"].as<Entity>();
    if (node["type"]) scrollbar.type = stringToScrollbarType(node["type"].as<std::string>());
    if (node["barSize"]) scrollbar.barSize = node["barSize"].as<float>();
    if (node["step"]) scrollbar.step = node["step"].as<float>();
    if (node["barMarginLeft"]) scrollbar.barMarginLeft = node["barMarginLeft"].as<int>();
    if (node["barMarginRight"]) scrollbar.barMarginRight = node["barMarginRight"].as<int>();
    if (node["barMarginTop"]) scrollbar.barMarginTop = node["barMarginTop"].as<int>();
    if (node["barMarginBottom"]) scrollbar.barMarginBottom = node["barMarginBottom"].as<int>();

    scrollbar.needUpdateScrollbar = true;

    return scrollbar;
}

YAML::Node editor::Stream::encodeProgressbarComponent(const ProgressbarComponent& progressbar) {
    YAML::Node node;
    node["fill"] = progressbar.fill;
    node["type"] = progressbarTypeToString(progressbar.type);
    node["value"] = progressbar.value;
    node["fillMarginLeft"] = progressbar.fillMarginLeft;
    node["fillMarginRight"] = progressbar.fillMarginRight;
    node["fillMarginTop"] = progressbar.fillMarginTop;
    node["fillMarginBottom"] = progressbar.fillMarginBottom;
    return node;
}

ProgressbarComponent editor::Stream::decodeProgressbarComponent(const YAML::Node& node, const ProgressbarComponent* oldProgressbar) {
    ProgressbarComponent progressbar;

    if (oldProgressbar) {
        progressbar = *oldProgressbar;
    }

    if (node["fill"]) progressbar.fill = node["fill"].as<Entity>();
    if (node["type"]) progressbar.type = stringToProgressbarType(node["type"].as<std::string>());
    if (node["value"]) progressbar.value = node["value"].as<float>();
    if (node["fillMarginLeft"]) progressbar.fillMarginLeft = node["fillMarginLeft"].as<int>();
    if (node["fillMarginRight"]) progressbar.fillMarginRight = node["fillMarginRight"].as<int>();
    if (node["fillMarginTop"]) progressbar.fillMarginTop = node["fillMarginTop"].as<int>();
    if (node["fillMarginBottom"]) progressbar.fillMarginBottom = node["fillMarginBottom"].as<int>();

    progressbar.needUpdateProgressbar = true;

    return progressbar;
}

YAML::Node editor::Stream::encodeTextEditComponent(const TextEditComponent& textedit) {
    YAML::Node node;
    node["text"] = textedit.text;
    node["selection"] = textedit.selection;
    node["cursor"] = textedit.cursor;
    node["cursorBlink"] = textedit.cursorBlink;
    node["cursorWidth"] = textedit.cursorWidth;
    node["cursorColor"] = encodeVector4(textedit.cursorColor);
    node["selectionColor"] = encodeVector4(textedit.selectionColor);
    node["placeholderColor"] = encodeVector4(textedit.placeholderColor);
    node["placeholder"] = textedit.placeholder;
    node["passwordChar"] = std::string(1, textedit.passwordChar);
    node["cursorIndex"] = textedit.cursorIndex;
    node["selectionAnchor"] = textedit.selectionAnchor;
    node["disabled"] = textedit.disabled;
    node["password"] = textedit.password;
    return node;
}

TextEditComponent editor::Stream::decodeTextEditComponent(const YAML::Node& node, const TextEditComponent* oldTextEdit) {
    TextEditComponent textedit;

    if (oldTextEdit) {
        textedit = *oldTextEdit;
    }

    if (node["text"]) textedit.text = node["text"].as<Entity>();
    if (node["selection"]) textedit.selection = node["selection"].as<Entity>();
    if (node["cursor"]) textedit.cursor = node["cursor"].as<Entity>();
    if (node["cursorBlink"]) textedit.cursorBlink = node["cursorBlink"].as<float>();
    if (node["cursorWidth"]) textedit.cursorWidth = node["cursorWidth"].as<float>();
    if (node["cursorColor"]) textedit.cursorColor = decodeVector4(node["cursorColor"]);
    if (node["selectionColor"]) textedit.selectionColor = decodeVector4(node["selectionColor"]);
    if (node["placeholderColor"]) textedit.placeholderColor = decodeVector4(node["placeholderColor"]);
    if (node["placeholder"]) textedit.placeholder = node["placeholder"].as<std::string>();
    if (node["passwordChar"]) {
        std::string passwordChar = node["passwordChar"].as<std::string>();
        textedit.passwordChar = passwordChar.empty() ? '*' : passwordChar[0];
    }
    if (node["cursorIndex"]) textedit.cursorIndex = node["cursorIndex"].as<int>();
    if (node["selectionAnchor"]) textedit.selectionAnchor = node["selectionAnchor"].as<int>();
    if (node["disabled"]) textedit.disabled = node["disabled"].as<bool>();
    if (node["password"]) textedit.password = node["password"].as<bool>();

    textedit.needUpdateTextEdit = true;

    return textedit;
}

YAML::Node editor::Stream::encodePanelComponent(const PanelComponent& panel) {
    YAML::Node node;
    node["headerimage"] = panel.headerimage;
    node["headercontainer"] = panel.headercontainer;
    node["headertext"] = panel.headertext;
    node["titleAnchorPreset"] = anchorPresetToString(panel.titleAnchorPreset);
    node["minWidth"] = panel.minWidth;
    node["minHeight"] = panel.minHeight;
    node["headerMarginLeft"] = panel.headerMarginLeft;
    node["headerMarginTop"] = panel.headerMarginTop;
    node["headerMarginRight"] = panel.headerMarginRight;
    node["headerMarginBottom"] = panel.headerMarginBottom;
    node["defaultHeaderMargin"] = panel.defaultHeaderMargin;
    node["resizeMargin"] = panel.resizeMargin;
    node["canMove"] = panel.canMove;
    node["canResize"] = panel.canResize;
    node["canBringToFront"] = panel.canBringToFront;
    return node;
}

PanelComponent editor::Stream::decodePanelComponent(const YAML::Node& node, const PanelComponent* oldPanel) {
    PanelComponent panel;

    if (oldPanel) {
        panel = *oldPanel;
    }

    if (node["headerimage"]) panel.headerimage = node["headerimage"].as<Entity>();
    if (node["headercontainer"]) panel.headercontainer = node["headercontainer"].as<Entity>();
    if (node["headertext"]) panel.headertext = node["headertext"].as<Entity>();
    if (node["titleAnchorPreset"]) panel.titleAnchorPreset = stringToAnchorPreset(node["titleAnchorPreset"].as<std::string>());
    if (node["minWidth"]) panel.minWidth = node["minWidth"].as<unsigned int>();
    if (node["minHeight"]) panel.minHeight = node["minHeight"].as<unsigned int>();
    if (node["headerMarginLeft"]) panel.headerMarginLeft = node["headerMarginLeft"].as<int>();
    if (node["headerMarginTop"]) panel.headerMarginTop = node["headerMarginTop"].as<int>();
    if (node["headerMarginRight"]) panel.headerMarginRight = node["headerMarginRight"].as<int>();
    if (node["headerMarginBottom"]) panel.headerMarginBottom = node["headerMarginBottom"].as<int>();
    if (node["defaultHeaderMargin"]) panel.defaultHeaderMargin = node["defaultHeaderMargin"].as<bool>();
    if (node["resizeMargin"]) panel.resizeMargin = node["resizeMargin"].as<int>();
    if (node["canMove"]) panel.canMove = node["canMove"].as<bool>();
    if (node["canResize"]) panel.canResize = node["canResize"].as<bool>();
    if (node["canBringToFront"]) panel.canBringToFront = node["canBringToFront"].as<bool>();

    panel.needUpdatePanel = true;

    return panel;
}

YAML::Node editor::Stream::encodeUILayoutComponent(const UILayoutComponent& layout) {
    YAML::Node node;
    node["width"] = layout.width;
    node["height"] = layout.height;
    node["anchorPointLeft"] = layout.anchorPointLeft;
    node["anchorPointTop"] = layout.anchorPointTop;
    node["anchorPointRight"] = layout.anchorPointRight;
    node["anchorPointBottom"] = layout.anchorPointBottom;
    node["anchorOffsetLeft"] = layout.anchorOffsetLeft;
    node["anchorOffsetTop"] = layout.anchorOffsetTop;
    node["anchorOffsetRight"] = layout.anchorOffsetRight;
    node["anchorOffsetBottom"] = layout.anchorOffsetBottom;
    node["positionOffset"] = encodeVector2(layout.positionOffset);
    node["anchorPreset"] = anchorPresetToString(layout.anchorPreset);
    node["usingAnchors"] = layout.usingAnchors;
    node["panel"] = layout.panel;
    node["containerBoxIndex"] = layout.containerBoxIndex;
    node["scissor"] = encodeRect(layout.scissor);
    node["ignoreScissor"] = layout.ignoreScissor;
    node["ignoreEvents"] = layout.ignoreEvents;
    //node["needUpdateSizes"] = layout.needUpdateSizes;
    //node["needUpdateAnchorOffsets"] = layout.needUpdateAnchorOffsets;

    return node;
}

UILayoutComponent editor::Stream::decodeUILayoutComponent(const YAML::Node& node, const UILayoutComponent* oldLayout) {
    UILayoutComponent layout;

    // Use old values as defaults if provided
    if (oldLayout) {
        layout = *oldLayout;
    }

    if (node["width"]) layout.width = node["width"].as<unsigned int>();
    if (node["height"]) layout.height = node["height"].as<unsigned int>();
    if (node["anchorPointLeft"]) layout.anchorPointLeft = node["anchorPointLeft"].as<float>();
    if (node["anchorPointTop"]) layout.anchorPointTop = node["anchorPointTop"].as<float>();
    if (node["anchorPointRight"]) layout.anchorPointRight = node["anchorPointRight"].as<float>();
    if (node["anchorPointBottom"]) layout.anchorPointBottom = node["anchorPointBottom"].as<float>();
    if (node["anchorOffsetLeft"]) layout.anchorOffsetLeft = node["anchorOffsetLeft"].as<int>();
    if (node["anchorOffsetTop"]) layout.anchorOffsetTop = node["anchorOffsetTop"].as<int>();
    if (node["anchorOffsetRight"]) layout.anchorOffsetRight = node["anchorOffsetRight"].as<int>();
    if (node["anchorOffsetBottom"]) layout.anchorOffsetBottom = node["anchorOffsetBottom"].as<int>();
    if (node["positionOffset"]) layout.positionOffset = decodeVector2(node["positionOffset"]);
    if (node["anchorPreset"]) layout.anchorPreset = stringToAnchorPreset(node["anchorPreset"].as<std::string>());
    if (node["usingAnchors"]) layout.usingAnchors = node["usingAnchors"].as<bool>();
    if (node["panel"]) layout.panel = node["panel"].as<Entity>();
    if (node["containerBoxIndex"]) layout.containerBoxIndex = node["containerBoxIndex"].as<int>();
    if (node["scissor"]) layout.scissor = decodeRect(node["scissor"]);
    if (node["ignoreScissor"]) layout.ignoreScissor = node["ignoreScissor"].as<bool>();
    if (node["ignoreEvents"]) layout.ignoreEvents = node["ignoreEvents"].as<bool>();
    //layout.needUpdateSizes = node["needUpdateSizes"].as<bool>();
    //layout.needUpdateAnchorOffsets = node["needUpdateAnchorOffsets"].as<bool>();

    return layout;
}

YAML::Node editor::Stream::encodeUIContainerComponent(const UIContainerComponent& container) {
    YAML::Node node;
    node["type"] = containerTypeToString(container.type);
    node["useAllWrapSpace"] = container.useAllWrapSpace;
    node["wrapCellWidth"] = container.wrapCellWidth;
    node["wrapCellHeight"] = container.wrapCellHeight;
    return node;
}

UIContainerComponent editor::Stream::decodeUIContainerComponent(const YAML::Node& node, const UIContainerComponent* oldContainer) {
    UIContainerComponent container;

    if (oldContainer) {
        container = *oldContainer;
    }

    if (node["type"]) container.type = stringToContainerType(node["type"].as<std::string>());
    if (node["useAllWrapSpace"]) container.useAllWrapSpace = node["useAllWrapSpace"].as<bool>();
    if (node["fillAvailableSpace"]) container.useAllWrapSpace = node["fillAvailableSpace"].as<bool>();
    if (node["wrapCellWidth"]) container.wrapCellWidth = node["wrapCellWidth"].as<unsigned int>();
    if (node["wrapCellHeight"]) container.wrapCellHeight = node["wrapCellHeight"].as<unsigned int>();

    return container;
}

YAML::Node editor::Stream::encodeTextComponent(const TextComponent& text) {
    YAML::Node node;

    // cut after the last font in use, but always written so a merging decode sees a cleared chain
    size_t numFonts = 0;
    for (size_t i = 0; i < text.font.size(); i++){
        if (!text.font[i].empty()) numFonts = i + 1;
    }
    node["font"] = YAML::Node(YAML::NodeType::Sequence);
    node["font"].SetStyle(YAML::EmitterStyle::Flow);
    for (size_t i = 0; i < numFonts; i++){
        node["font"].push_back(text.font[i]);
    }
    node["text"] = text.text;
    node["fontSize"] = text.fontSize;
    node["multiline"] = text.multiline;
    node["maxTextSize"] = text.maxTextSize;
    node["fixedWidth"] = text.fixedWidth;
    node["fixedHeight"] = text.fixedHeight;
    node["pivotBaseline"] = text.pivotBaseline;
    node["pivotCentered"] = text.pivotCentered;

    return node;
}

TextComponent editor::Stream::decodeTextComponent(const YAML::Node& node, const TextComponent* oldText) {
    TextComponent text;

    if (oldText) {
        text = *oldText;
    }

    if (node["font"]){
        text.font = FontArray();
        if (node["font"].IsSequence()){
            for (size_t i = 0; i < node["font"].size() && i < text.font.size(); i++){
                text.font[i] = node["font"][i].as<std::string>();
            }
        }else{
            // before the array: a single font plus a ";" separated fontFallbacks
            text.font[0] = node["font"].as<std::string>();
        }
    }
    if (node["text"]) text.text = node["text"].as<std::string>();
    if (node["fontSize"]) text.fontSize = node["fontSize"].as<unsigned int>();
    if (node["multiline"]) text.multiline = node["multiline"].as<bool>();
    if (node["maxTextSize"]) text.maxTextSize = node["maxTextSize"].as<unsigned int>();
    if (node["fixedWidth"]) text.fixedWidth = node["fixedWidth"].as<bool>();
    if (node["fixedHeight"]) text.fixedHeight = node["fixedHeight"].as<bool>();
    if (node["pivotBaseline"]) text.pivotBaseline = node["pivotBaseline"].as<bool>();
    if (node["pivotCentered"]) text.pivotCentered = node["pivotCentered"].as<bool>();

    return text;
}

YAML::Node editor::Stream::encodeImageComponent(const ImageComponent& image) {
    YAML::Node node;
    node["patchMarginLeft"] = image.patchMarginLeft;
    node["patchMarginRight"] = image.patchMarginRight;
    node["patchMarginTop"] = image.patchMarginTop;
    node["patchMarginBottom"] = image.patchMarginBottom;
    node["textureScaleFactor"] = image.textureScaleFactor;
    //node["needUpdatePatches"] = image.needUpdatePatches;

    return node;
}

ImageComponent editor::Stream::decodeImageComponent(const YAML::Node& node, const ImageComponent* oldImage) {
    ImageComponent image;

    // Use old values as defaults if provided
    if (oldImage) {
        image = *oldImage;
    }

    if (node["patchMarginLeft"]) image.patchMarginLeft = node["patchMarginLeft"].as<int>();
    if (node["patchMarginRight"]) image.patchMarginRight = node["patchMarginRight"].as<int>();
    if (node["patchMarginTop"]) image.patchMarginTop = node["patchMarginTop"].as<int>();
    if (node["patchMarginBottom"]) image.patchMarginBottom = node["patchMarginBottom"].as<int>();
    if (node["textureScaleFactor"]) image.textureScaleFactor = node["textureScaleFactor"].as<float>();
    //image.needUpdatePatches = node["needUpdatePatches"].as<bool>();

    return image;
}

YAML::Node editor::Stream::encodeSpriteComponent(const SpriteComponent& sprite) {
    YAML::Node node;
    node["width"] = sprite.width;
    node["height"] = sprite.height;
    node["automaticFlipY"] = sprite.automaticFlipY;
    node["flipY"] = sprite.flipY;
    node["textureScaleFactor"] = sprite.textureScaleFactor;
    node["pivotPreset"] = pivotPresetToString(sprite.pivotPreset);
    //node["needUpdateSprite"] = sprite.needUpdateSprite;

    YAML::Node framesNode;
    for (unsigned int i = 0; i < sprite.numFramesRect; i++) {
        framesNode.push_back(encodeSpriteFrameData(sprite.framesRect[i]));
    }
    if (framesNode.size() > 0) {
        node["framesRect"] = framesNode;
    }

    return node;
}

SpriteComponent editor::Stream::decodeSpriteComponent(const YAML::Node& node, const SpriteComponent* oldSprite) {
    SpriteComponent sprite;

    // Use old values as defaults if provided
    if (oldSprite) {
        sprite = *oldSprite;
    }

    if (node["width"]) sprite.width = node["width"].as<unsigned int>();
    if (node["height"]) sprite.height = node["height"].as<unsigned int>();
    if (node["automaticFlipY"]) sprite.automaticFlipY = node["automaticFlipY"].as<bool>();
    if (node["flipY"]) sprite.flipY = node["flipY"].as<bool>();
    if (node["textureScaleFactor"]) sprite.textureScaleFactor = node["textureScaleFactor"].as<float>();
    if (node["pivotPreset"]) sprite.pivotPreset = stringToPivotPreset(node["pivotPreset"].as<std::string>());
    //if (node["needUpdateSprite"]) sprite.needUpdateSprite = node["needUpdateSprite"].as<bool>();

    if (node["framesRect"]) {
        const YAML::Node& framesNode = node["framesRect"];

        if (framesNode.IsSequence()) {
            sprite.numFramesRect = 0;
            for (std::size_t i = 0; i < framesNode.size() && i < sprite.framesRect.size(); i++) {
                if (!framesNode[i] || framesNode[i].IsNull()) {
                    continue;
                }

                sprite.framesRect[sprite.numFramesRect] = decodeSpriteFrameData(framesNode[i]);
                sprite.numFramesRect++;
            }
        }
    }

    return sprite;
}

YAML::Node editor::Stream::encodeTilemapComponent(const TilemapComponent& tilemap) {
    YAML::Node node;
    node["width"] = tilemap.width;
    node["height"] = tilemap.height;
    node["automaticFlipY"] = tilemap.automaticFlipY;
    node["flipY"] = tilemap.flipY;
    node["textureScaleFactor"] = tilemap.textureScaleFactor;
    node["reserveTiles"] = tilemap.reserveTiles;

    YAML::Node tilesRectNode;
    for (unsigned int i = 0; i < tilemap.numTilesRect; i++) {
        tilesRectNode.push_back(encodeTileRectData(tilemap.tilesRect[i]));
    }
    if (tilesRectNode.size() > 0) {
        node["tilesRect"] = tilesRectNode;
    }

    YAML::Node tilesNode;
    for (unsigned int i = 0; i < tilemap.numTiles; i++) {
        tilesNode.push_back(encodeTileData(tilemap.tiles[i]));
    }
    if (tilesNode.size() > 0) {
        node["tiles"] = tilesNode;
    }

    return node;
}

TilemapComponent editor::Stream::decodeTilemapComponent(const YAML::Node& node, const TilemapComponent* oldTilemap) {
    TilemapComponent tilemap;

    if (oldTilemap) {
        tilemap = *oldTilemap;
    }

    if (node["width"]) tilemap.width = node["width"].as<unsigned int>();
    if (node["height"]) tilemap.height = node["height"].as<unsigned int>();
    if (node["automaticFlipY"]) tilemap.automaticFlipY = node["automaticFlipY"].as<bool>();
    if (node["flipY"]) tilemap.flipY = node["flipY"].as<bool>();
    if (node["textureScaleFactor"]) tilemap.textureScaleFactor = node["textureScaleFactor"].as<float>();
    if (node["reserveTiles"]) tilemap.reserveTiles = node["reserveTiles"].as<unsigned int>();

    if (node["tilesRect"]) {
        const YAML::Node& tilesRectNode = node["tilesRect"];
        if (tilesRectNode.IsSequence()) {
            tilemap.numTilesRect = 0;
            for (std::size_t i = 0; i < tilesRectNode.size() && i < tilemap.tilesRect.size(); i++) {
                if (!tilesRectNode[i] || tilesRectNode[i].IsNull()) {
                    continue;
                }
                tilemap.tilesRect[tilemap.numTilesRect] = decodeTileRectData(tilesRectNode[i]);
                tilemap.numTilesRect++;
            }
        }
    }

    if (node["tiles"]) {
        const YAML::Node& tilesNode = node["tiles"];
        if (tilesNode.IsSequence()) {
            tilemap.numTiles = 0;
            for (std::size_t i = 0; i < tilesNode.size() && i < tilemap.tiles.size(); i++) {
                if (!tilesNode[i] || tilesNode[i].IsNull()) {
                    continue;
                }
                tilemap.tiles[tilemap.numTiles] = decodeTileData(tilesNode[i]);
                tilemap.numTiles++;
            }
        }
    }

    return tilemap;
}

YAML::Node editor::Stream::encodeTerrainComponent(const TerrainComponent& terrain) {
    YAML::Node node;
    node["heightMap"] = encodeTexture(terrain.heightMap);
    node["blendMap"] = encodeTexture(terrain.blendMap);
    node["textureDetailRed"] = encodeTexture(terrain.textureDetailRed);
    node["textureDetailGreen"] = encodeTexture(terrain.textureDetailGreen);
    node["textureDetailBlue"] = encodeTexture(terrain.textureDetailBlue);
    node["autoSetRanges"] = terrain.autoSetRanges;
    node["offset"] = encodeVector2(terrain.offset);
    node["terrainSize"] = terrain.terrainSize;
    node["maxHeight"] = terrain.maxHeight;
    node["resolution"] = terrain.resolution;
    node["textureBaseTiles"] = terrain.textureBaseTiles;
    node["textureDetailTiles"] = terrain.textureDetailTiles;
    node["rootGridSize"] = terrain.rootGridSize;
    node["levels"] = terrain.levels;

    if (!terrain.ranges.empty()) {
        YAML::Node rangesNode;
        for (float range : terrain.ranges) {
            rangesNode.push_back(range);
        }
        node["ranges"] = rangesNode;
    }

    return node;
}

TerrainComponent editor::Stream::decodeTerrainComponent(const YAML::Node& node, const TerrainComponent* oldTerrain) {
    TerrainComponent terrain;

    if (oldTerrain) {
        terrain = *oldTerrain;
    }

    if (node["heightMap"]) terrain.heightMap = decodeTexture(node["heightMap"]);
    if (node["blendMap"]) terrain.blendMap = decodeTexture(node["blendMap"]);
    if (node["textureDetailRed"]) terrain.textureDetailRed = decodeTexture(node["textureDetailRed"]);
    if (node["textureDetailGreen"]) terrain.textureDetailGreen = decodeTexture(node["textureDetailGreen"]);
    if (node["textureDetailBlue"]) terrain.textureDetailBlue = decodeTexture(node["textureDetailBlue"]);
    if (node["autoSetRanges"]) terrain.autoSetRanges = node["autoSetRanges"].as<bool>();
    if (node["offset"]) terrain.offset = decodeVector2(node["offset"]);
    if (node["terrainSize"]) terrain.terrainSize = node["terrainSize"].as<float>();
    if (node["maxHeight"]) terrain.maxHeight = node["maxHeight"].as<float>();
    if (node["resolution"]) terrain.resolution = node["resolution"].as<float>();
    if (node["textureBaseTiles"]) terrain.textureBaseTiles = node["textureBaseTiles"].as<float>();
    if (node["textureDetailTiles"]) terrain.textureDetailTiles = node["textureDetailTiles"].as<float>();
    if (node["rootGridSize"]) terrain.rootGridSize = node["rootGridSize"].as<int>();
    if (node["levels"]) terrain.levels = node["levels"].as<int>();

    if (node["ranges"] && node["ranges"].IsSequence()) {
        terrain.ranges.clear();
        for (std::size_t i = 0; i < node["ranges"].size(); i++) {
            if (!node["ranges"][i] || node["ranges"][i].IsNull()) {
                continue;
            }
            terrain.ranges.push_back(node["ranges"][i].as<float>());
        }
    }

    terrain.needUpdateTerrain = true;
    terrain.needUpdateTexture = true;
    for (int v = 0; v < MAX_TERRAIN_VIEWS; v++){
        terrain.views[v].needUpdateNodesBuffer = false;
    }
    terrain.heightMapLoaded = false;
    terrain.nodes.clear();
    terrain.numNodes = 0;

    return terrain;
}

YAML::Node editor::Stream::encodeModelComponent(const ModelComponent& model) {
    YAML::Node node;

    if (!model.filename.empty()) {
        node["filename"] = model.filename;
    }

    if (model.mergeStaticMeshes) {
        node["mergeStaticMeshes"] = true;
    }

    node["skeleton"] = static_cast<uint32_t>(model.skeleton);

    if (!model.animations.empty()) {
        YAML::Node animsNode;
        for (const auto& anim : model.animations) {
            animsNode.push_back(static_cast<uint32_t>(anim));
        }
        node["animations"] = animsNode;
    }

    if (!model.bonesIdMapping.empty()) {
        YAML::Node bonesNode;
        for (const auto& bone : model.bonesIdMapping) {
            YAML::Node boneNode;
            boneNode["id"] = bone.first;
            boneNode["entity"] = static_cast<uint32_t>(bone.second);
            bonesNode.push_back(boneNode);
        }
        node["bonesIdMapping"] = bonesNode;
    }

    if (!model.bonesNameMapping.empty()) {
        YAML::Node bonesNode;
        for (const auto& bone : model.bonesNameMapping) {
            YAML::Node boneNode;
            boneNode["name"] = bone.first;
            boneNode["entity"] = static_cast<uint32_t>(bone.second);
            bonesNode.push_back(boneNode);
        }
        node["bonesNameMapping"] = bonesNode;
    }

    if (!model.meshNodesMapping.empty()) {
        YAML::Node meshNodesNode;
        for (const auto& meshNode : model.meshNodesMapping) {
            YAML::Node n;
            n["node"] = meshNode.first;
            n["entity"] = static_cast<uint32_t>(meshNode.second);
            meshNodesNode.push_back(n);
        }
        node["meshNodesMapping"] = meshNodesNode;
    }

    if (!model.nodesIdMapping.empty()) {
        YAML::Node nodesNode;
        for (const auto& mappedNode : model.nodesIdMapping) {
            YAML::Node n;
            n["node"] = mappedNode.first;
            n["entity"] = static_cast<uint32_t>(mappedNode.second);
            nodesNode.push_back(n);
        }
        node["nodesIdMapping"] = nodesNode;
    }

    return node;
}

ModelComponent editor::Stream::decodeModelComponent(const YAML::Node& node, const ModelComponent* oldModel) {
    ModelComponent model;

    if (oldModel) {
        model = *oldModel;
    }

    // Also check for the old modelPath for exact backward compatibility with old save files
    if (node["filename"]) model.filename = node["filename"].as<std::string>();
    if (node["mergeStaticMeshes"]) model.mergeStaticMeshes = node["mergeStaticMeshes"].as<bool>();
    if (node["skeleton"]) model.skeleton = static_cast<Entity>(node["skeleton"].as<uint32_t>());

    if (node["animations"]) {
        model.animations.clear();
        for (const auto& animNode : node["animations"]) {
            model.animations.push_back(static_cast<Entity>(animNode.as<uint32_t>()));
        }
    }

    if (node["bonesIdMapping"]) {
        model.bonesIdMapping.clear();
        for (const auto& boneNode : node["bonesIdMapping"]) {
            int id = boneNode["id"].as<int>();
            Entity boneEntity = static_cast<Entity>(boneNode["entity"].as<uint32_t>());
            model.bonesIdMapping[id] = boneEntity;
        }
    }

    if (node["bonesNameMapping"]) {
        model.bonesNameMapping.clear();
        for (const auto& boneNode : node["bonesNameMapping"]) {
            std::string name = boneNode["name"].as<std::string>();
            Entity boneEntity = static_cast<Entity>(boneNode["entity"].as<uint32_t>());
            model.bonesNameMapping[name] = boneEntity;
        }
    }

    if (node["meshNodesMapping"]) {
        model.meshNodesMapping.clear();
        for (const auto& meshNode : node["meshNodesMapping"]) {
            int nodeIdx = meshNode["node"].as<int>();
            Entity nodeEntity = static_cast<Entity>(meshNode["entity"].as<uint32_t>());
            model.meshNodesMapping[nodeIdx] = nodeEntity;
        }
    }

    model.nodesIdMapping.clear();
    if (node["nodesIdMapping"]) {
        for (const auto& mappedNode : node["nodesIdMapping"]) {
            int nodeIdx = mappedNode["node"].as<int>();
            Entity nodeEntity = static_cast<Entity>(mappedNode["entity"].as<uint32_t>());
            model.nodesIdMapping[nodeIdx] = nodeEntity;
        }
    }

    if (!oldModel || model.filename != oldModel->filename ||
            model.bonesIdMapping != oldModel->bonesIdMapping ||
            model.meshNodesMapping != oldModel->meshNodesMapping ||
            model.nodesIdMapping != oldModel->nodesIdMapping) {
        model.skinBindings.clear();
        model.loadedFilename.clear();
        model.needUpdateModel = true;
    }

    return model;
}

YAML::Node editor::Stream::encodeLightComponent(const LightComponent& light) {
    YAML::Node node;

    node["type"] = lightTypeToString(light.type);
    node["direction"] = encodeVector3(light.direction);
    node["worldDirection"] = encodeVector3(light.worldDirection);
    node["color"] = encodeVector3(light.color);
    node["range"] = light.range;
    node["intensity"] = light.intensity;
    node["innerConeCos"] = light.innerConeCos;
    node["outerConeCos"] = light.outerConeCos;
    node["spotMask"] = encodeTexture(light.spotMask);
    node["shadows"] = light.shadows;
    node["automaticShadowCamera"] = light.automaticShadowCamera;
    node["shadowBias"] = light.shadowBias;
    node["mapResolution"] = light.mapResolution;
    node["shadowCameraNearFar"] = encodeVector2(light.shadowCameraNearFar);
    node["numShadowCascades"] = light.numShadowCascades;

    return node;
}

LightComponent editor::Stream::decodeLightComponent(const YAML::Node& node, const LightComponent* oldLight) {
    LightComponent light;

    // Use old values as defaults if provided
    if (oldLight) {
        light = *oldLight;
    }

    if (node["type"]) light.type = stringToLightType(node["type"].as<std::string>());
    if (node["direction"]) light.direction = decodeVector3(node["direction"]);
    if (node["worldDirection"]) light.worldDirection = decodeVector3(node["worldDirection"]);
    if (node["color"]) light.color = decodeVector3(node["color"]);
    if (node["range"]) light.range = node["range"].as<float>();
    if (node["intensity"]) light.intensity = node["intensity"].as<float>();
    if (node["innerConeCos"]) light.innerConeCos = node["innerConeCos"].as<float>();
    if (node["outerConeCos"]) light.outerConeCos = node["outerConeCos"].as<float>();
    if (node["spotMask"]) light.spotMask = decodeTexture(node["spotMask"]);
    if (node["shadows"]) light.shadows = node["shadows"].as<bool>();
    if (node["automaticShadowCamera"]) light.automaticShadowCamera = node["automaticShadowCamera"].as<bool>();
    if (node["shadowBias"]) light.shadowBias = node["shadowBias"].as<float>();
    if (node["mapResolution"]) light.mapResolution = node["mapResolution"].as<unsigned int>();
    if (node["shadowCameraNearFar"]) light.shadowCameraNearFar = decodeVector2(node["shadowCameraNearFar"]);
    if (node["numShadowCascades"]) light.numShadowCascades = node["numShadowCascades"].as<unsigned int>();

    return light;
}

YAML::Node editor::Stream::encodeLight2DComponent(const Light2DComponent& light) {
    YAML::Node node;

    node["color"] = encodeVector3(light.color);
    node["intensity"] = light.intensity;
    node["range"] = light.range;
    node["falloff"] = light.falloff;
    node["height"] = light.height;
    node["shadows"] = light.shadows;
    node["shadowBias"] = light.shadowBias;
    node["shadowSoftness"] = light.shadowSoftness;
    node["mapResolution"] = light.mapResolution;

    return node;
}

Light2DComponent editor::Stream::decodeLight2DComponent(const YAML::Node& node, const Light2DComponent* oldLight) {
    Light2DComponent light;

    // Use old values as defaults if provided
    if (oldLight) {
        light = *oldLight;
    }

    if (node["color"]) light.color = decodeVector3(node["color"]);
    if (node["intensity"]) light.intensity = node["intensity"].as<float>();
    if (node["range"]) light.range = node["range"].as<float>();
    if (node["falloff"]) light.falloff = node["falloff"].as<float>();
    if (node["height"]) light.height = node["height"].as<float>();
    if (node["shadows"]) light.shadows = node["shadows"].as<bool>();
    if (node["shadowBias"]) light.shadowBias = node["shadowBias"].as<float>();
    if (node["shadowSoftness"]) light.shadowSoftness = node["shadowSoftness"].as<float>();
    if (node["mapResolution"]) light.mapResolution = node["mapResolution"].as<unsigned int>();

    return light;
}

static std::string occluder2DShapeToString(Occluder2DShape shape) {
    switch (shape) {
        case Occluder2DShape::AUTO_QUAD: return "autoquad";
        case Occluder2DShape::POLYGON: return "polygon";
    }
    return "autoquad";
}

static Occluder2DShape stringToOccluder2DShape(const std::string& str) {
    if (str == "polygon") return Occluder2DShape::POLYGON;
    return Occluder2DShape::AUTO_QUAD;
}

YAML::Node editor::Stream::encodeOccluder2DComponent(const Occluder2DComponent& occluder) {
    YAML::Node node;

    node["shape"] = occluder2DShapeToString(occluder.shape);
    node["closed"] = occluder.closed;
    node["enabled"] = occluder.enabled;

    YAML::Node pointsNode(YAML::NodeType::Sequence);
    for (const Vector2& point : occluder.points) {
        pointsNode.push_back(encodeVector2(point));
    }
    node["points"] = pointsNode;

    return node;
}

Occluder2DComponent editor::Stream::decodeOccluder2DComponent(const YAML::Node& node, const Occluder2DComponent* oldOccluder) {
    Occluder2DComponent occluder;

    if (oldOccluder) {
        occluder = *oldOccluder;
    }

    if (node["shape"]) occluder.shape = stringToOccluder2DShape(node["shape"].as<std::string>());
    if (node["closed"]) occluder.closed = node["closed"].as<bool>();
    if (node["enabled"]) occluder.enabled = node["enabled"].as<bool>();
    if (node["points"]) {
        occluder.points.clear();
        for (const auto& pointNode : node["points"]) {
            occluder.points.push_back(decodeVector2(pointNode));
        }
    }

    return occluder;
}

YAML::Node editor::Stream::encodeFogComponent(const FogComponent& fog) {
    YAML::Node node;

    node["type"] = fogTypeToString(fog.type);
    node["color"] = encodeVector3(fog.color);
    node["density"] = fog.density;
    node["linearStart"] = fog.linearStart;
    node["linearEnd"] = fog.linearEnd;

    return node;
}

FogComponent editor::Stream::decodeFogComponent(const YAML::Node& node, const FogComponent* oldFog) {
    FogComponent fog;

    if (oldFog) {
        fog = *oldFog;
    }

    if (node["type"]) fog.type = stringToFogType(node["type"].as<std::string>());
    if (node["color"]) fog.color = decodeVector3(node["color"]);
    if (node["density"]) fog.density = node["density"].as<float>();
    if (node["linearStart"]) fog.linearStart = node["linearStart"].as<float>();
    if (node["linearEnd"]) fog.linearEnd = node["linearEnd"].as<float>();

    return fog;
}

YAML::Node editor::Stream::encodeMirrorComponent(const MirrorComponent& mirror) {
    YAML::Node node;

    // reflectionCamera is an engine-created runtime entity; not serialized
    node["normal"] = encodeVector3(mirror.normal);

    return node;
}

MirrorComponent editor::Stream::decodeMirrorComponent(const YAML::Node& node, const MirrorComponent* oldMirror) {
    MirrorComponent mirror;

    if (oldMirror) {
        mirror = *oldMirror;
    }

    if (node["normal"]) mirror.normal = decodeVector3(node["normal"]);

    return mirror;
}

YAML::Node editor::Stream::encodeReflectionProbeComponent(const ReflectionProbeComponent& probe) {
    YAML::Node node;

    node["mode"] = probe.mode == ReflectionProbeMode::DYNAMIC ? "dynamic" : "static";
    switch (probe.updateMode) {
        case ReflectionProbeUpdateMode::ON_MOVE: node["updateMode"] = "on_move"; break;
        case ReflectionProbeUpdateMode::INTERVAL: node["updateMode"] = "interval"; break;
        case ReflectionProbeUpdateMode::MANUAL: node["updateMode"] = "manual"; break;
        default: node["updateMode"] = "on_load"; break;
    }
    node["boxOffset"] = encodeVector3(probe.boxOffset);
    node["boxSize"] = encodeVector3(probe.boxSize);
    node["blendDistance"] = probe.blendDistance;
    node["intensity"] = probe.intensity;
    node["priority"] = probe.priority;
    node["resolution"] = probe.resolution;
    node["nearClip"] = probe.nearClip;
    node["farClip"] = probe.farClip;
    node["updateInterval"] = probe.updateInterval;
    node["includeSky"] = probe.includeSky;
    node["texture"] = encodeTexture(probe.texture);

    return node;
}

ReflectionProbeComponent editor::Stream::decodeReflectionProbeComponent(const YAML::Node& node, const ReflectionProbeComponent* oldProbe) {
    ReflectionProbeComponent probe;
    if (oldProbe) probe = *oldProbe;

    if (node["mode"]) {
        probe.mode = node["mode"].as<std::string>() == "dynamic" ? ReflectionProbeMode::DYNAMIC : ReflectionProbeMode::STATIC;
    }
    if (node["updateMode"]) {
        const std::string value = node["updateMode"].as<std::string>();
        if (value == "on_move") probe.updateMode = ReflectionProbeUpdateMode::ON_MOVE;
        else if (value == "interval") probe.updateMode = ReflectionProbeUpdateMode::INTERVAL;
        else if (value == "manual") probe.updateMode = ReflectionProbeUpdateMode::MANUAL;
        else probe.updateMode = ReflectionProbeUpdateMode::ON_LOAD;
    }
    if (node["boxOffset"]) probe.boxOffset = decodeVector3(node["boxOffset"]);
    if (node["boxSize"]) probe.boxSize = decodeVector3(node["boxSize"]);
    if (node["blendDistance"]) probe.blendDistance = node["blendDistance"].as<float>();
    if (node["intensity"]) probe.intensity = node["intensity"].as<float>();
    if (node["priority"]) probe.priority = node["priority"].as<int>();
    if (node["resolution"]) probe.resolution = node["resolution"].as<unsigned int>();
    if (node["nearClip"]) probe.nearClip = node["nearClip"].as<float>();
    if (node["farClip"]) probe.farClip = node["farClip"].as<float>();
    if (node["updateInterval"]) probe.updateInterval = node["updateInterval"].as<float>();
    if (node["includeSky"]) probe.includeSky = node["includeSky"].as<bool>();
    if (node["texture"]) probe.texture = decodeTexture(node["texture"]);
    probe.needUpdate = true;

    return probe;
}

YAML::Node editor::Stream::encodeCameraComponent(const CameraComponent& camera) {
    YAML::Node node;

    node["type"] = cameraTypeToString(camera.type);
    encodeFiniteVector3(node, "target", camera.target);
    encodeFiniteVector3(node, "up", camera.up);
    encodeFinite(node, "leftClip", camera.leftClip);
    encodeFinite(node, "rightClip", camera.rightClip);
    encodeFinite(node, "bottomClip", camera.bottomClip);
    encodeFinite(node, "topClip", camera.topClip);
    encodePositiveFinite(node, "yfov", camera.yfov);
    encodePositiveFinite(node, "aspect", camera.aspect);
    encodeFinite(node, "nearClip", camera.nearClip);
    encodeFinite(node, "farClip", camera.farClip);
    node["renderToTexture"] = camera.renderToTexture;
    node["transparentSort"] = camera.transparentSort;
    node["useTarget"] = camera.useTarget;
    node["autoResize"] = camera.autoResize;
    node["framebufferWidth"] = camera.framebufferWidth;
    node["framebufferHeight"] = camera.framebufferHeight;
    node["framebufferFilter"] = textureFilterToString(camera.framebufferFilter);

    return node;
}

CameraComponent editor::Stream::decodeCameraComponent(const YAML::Node& node, const CameraComponent* oldCamera) {
    CameraComponent camera;

    // Use old values as defaults if provided
    if (oldCamera) {
        camera = *oldCamera;
    }

    if (node["type"]) camera.type = stringToCameraType(node["type"].as<std::string>());
    camera.target = decodeFiniteVector3(node["target"], camera.target);
    camera.up = decodeFiniteVector3(node["up"], camera.up);
    camera.leftClip = decodeFinite(node["leftClip"], camera.leftClip);
    camera.rightClip = decodeFinite(node["rightClip"], camera.rightClip);
    camera.bottomClip = decodeFinite(node["bottomClip"], camera.bottomClip);
    camera.topClip = decodeFinite(node["topClip"], camera.topClip);
    camera.yfov = decodePositiveFinite(node["yfov"], camera.yfov);
    camera.aspect = decodePositiveFinite(node["aspect"], camera.aspect);
    camera.nearClip = decodeFinite(node["nearClip"], camera.nearClip);
    camera.farClip = decodeFinite(node["farClip"], camera.farClip);
    if (node["renderToTexture"]) camera.renderToTexture = node["renderToTexture"].as<bool>();
    if (node["transparentSort"]) camera.transparentSort = node["transparentSort"].as<bool>();
    if (node["useTarget"]) camera.useTarget = node["useTarget"].as<bool>();
    if (node["autoResize"]) camera.autoResize = node["autoResize"].as<bool>();
    if (node["framebufferWidth"]) camera.framebufferWidth = node["framebufferWidth"].as<unsigned int>();
    if (node["framebufferHeight"]) camera.framebufferHeight = node["framebufferHeight"].as<unsigned int>();
    if (node["framebufferFilter"]) camera.framebufferFilter = stringToTextureFilter(node["framebufferFilter"].as<std::string>());

    return camera;
}

YAML::Node editor::Stream::encodeSoundComponent(const SoundComponent& audio) {
    YAML::Node node;

    node["state"] = soundStateToString(audio.state);
    if (!audio.filename.empty()) node["filename"] = audio.filename;
    node["enableClocked"] = audio.enableClocked;
    encodeFinite(node, "volume", audio.volume);
    encodeFinite(node, "speed", audio.speed);
    encodeFinite(node, "pan", audio.pan);
    node["looping"] = audio.looping;
    encodeFinite(node, "loopingPoint", audio.loopingPoint);
    node["protectVoice"] = audio.protectVoice;
    node["inaudibleBehaviorMustTick"] = audio.inaudibleBehaviorMustTick;
    node["inaudibleBehaviorKill"] = audio.inaudibleBehaviorKill;
    encodeFinite(node, "minDistance", audio.minDistance);
    encodeFinite(node, "maxDistance", audio.maxDistance);
    node["attenuationModel"] = soundAttenuationToString(audio.attenuationModel);
    encodeFinite(node, "attenuationRolloffFactor", audio.attenuationRolloffFactor);
    encodeFinite(node, "dopplerFactor", audio.dopplerFactor);

    return node;
}

SoundComponent editor::Stream::decodeSoundComponent(const YAML::Node& node, const SoundComponent* oldAudio) {
    SoundComponent audio;
    std::string oldFilename;

    if (oldAudio) {
        audio = *oldAudio;
        oldFilename = oldAudio->filename;
    }

    if (node["state"]) audio.state = stringToSoundState(node["state"].as<std::string>());
    if (node["filename"]) audio.filename = node["filename"].as<std::string>();
    if (node["enableClocked"]) audio.enableClocked = node["enableClocked"].as<bool>();
    if (node["volume"]) audio.volume = decodeFiniteDouble(node["volume"], audio.volume);
    if (node["speed"]) audio.speed = decodeFinite(node["speed"], audio.speed);
    if (node["pan"]) audio.pan = decodeFinite(node["pan"], audio.pan);
    if (node["looping"]) audio.looping = node["looping"].as<bool>();
    if (node["loopingPoint"]) audio.loopingPoint = decodeFiniteDouble(node["loopingPoint"], audio.loopingPoint);
    if (node["protectVoice"]) audio.protectVoice = node["protectVoice"].as<bool>();
    if (node["inaudibleBehaviorMustTick"]) audio.inaudibleBehaviorMustTick = node["inaudibleBehaviorMustTick"].as<bool>();
    if (node["inaudibleBehaviorKill"]) audio.inaudibleBehaviorKill = node["inaudibleBehaviorKill"].as<bool>();
    if (node["minDistance"]) audio.minDistance = decodeFinite(node["minDistance"], audio.minDistance);
    if (node["maxDistance"]) audio.maxDistance = decodeFinite(node["maxDistance"], audio.maxDistance);
    if (node["attenuationModel"]) audio.attenuationModel = stringToSoundAttenuation(node["attenuationModel"].as<std::string>());
    if (node["attenuationRolloffFactor"]) audio.attenuationRolloffFactor = decodeFinite(node["attenuationRolloffFactor"], audio.attenuationRolloffFactor);
    if (node["dopplerFactor"]) audio.dopplerFactor = decodeFinite(node["dopplerFactor"], audio.dopplerFactor);

    bool keepLoadedSample = oldAudio && oldFilename == audio.filename && oldAudio->sample && oldAudio->loaded;
    if (!keepLoadedSample && oldAudio && !oldFilename.empty()) {
        audio.sample.reset();
        SoundPool::remove(oldFilename);
    }
    audio.loaded = keepLoadedSample;
    audio.length = keepLoadedSample ? oldAudio->length : 0;
    audio.handle = 0;
    audio.playingTime = 0;
    audio.lastPosition = Vector3(0, 0, 0);
    audio.startTrigger = false;
    audio.pauseTrigger = false;
    audio.stopTrigger = false;
    audio.needUpdate = true;

    return audio;
}

YAML::Node editor::Stream::encodeScriptComponent(const ScriptComponent& script) {
    YAML::Node node;

    if (!script.scripts.empty()) {
        YAML::Node scriptsNode;
        for (const auto& scriptEntry : script.scripts) {
            YAML::Node scriptNode;

            scriptNode["type"] = scriptTypeToString(scriptEntry.type);

            if (!scriptEntry.path.empty())
                scriptNode["path"] = scriptEntry.path;

            if (!scriptEntry.headerPath.empty())
                scriptNode["headerPath"] = scriptEntry.headerPath;

            if (!scriptEntry.className.empty())
                scriptNode["className"] = scriptEntry.className;

            scriptNode["enabled"] = scriptEntry.enabled;

            if (!scriptEntry.properties.empty()) {
                YAML::Node propsNode;
                for (const auto& prop : scriptEntry.properties) {
                    propsNode.push_back(encodeScriptProperty(prop));
                }
                scriptNode["properties"] = propsNode;
            }

            scriptsNode.push_back(scriptNode);
        }
        node["scripts"] = scriptsNode;
    }

    return node;
}

ScriptComponent editor::Stream::decodeScriptComponent(const YAML::Node& node, const ScriptComponent* oldScript) {
    ScriptComponent script;

    // Use old values as defaults if provided
    if (oldScript) {
        script = *oldScript;
    }

    script.scripts.clear();

    if (!node["scripts"] || !node["scripts"].IsSequence())
        return script;

    for (const auto& scriptNode : node["scripts"]) {
        ScriptEntry entry;

        if (scriptNode["type"]) {
            std::string typeStr = scriptNode["type"].as<std::string>();
            entry.type = stringToScriptType(typeStr);
        }

        if (scriptNode["path"])
            entry.path = scriptNode["path"].as<std::string>();

        if (scriptNode["headerPath"])
            entry.headerPath = scriptNode["headerPath"].as<std::string>();

        if (scriptNode["className"])
            entry.className = scriptNode["className"].as<std::string>();

        if (scriptNode["enabled"])
            entry.enabled = scriptNode["enabled"].as<bool>();

        if (scriptNode["properties"] && scriptNode["properties"].IsSequence()) {
            for (const auto& propNode : scriptNode["properties"]) {
                entry.properties.push_back(decodeScriptProperty(propNode));
            }
        }

        script.scripts.push_back(std::move(entry));
    }

    return script;
}

YAML::Node editor::Stream::encodeSkyComponent(const SkyComponent& sky) {
    YAML::Node node;

    const bool useDefaultTexture = (sky.texture.getId() == "editor:resources:default_sky");
    node["useDefaultTexture"] = useDefaultTexture;
    node["texture"] = encodeTexture(sky.texture);
    node["color"] = encodeVector4(sky.color);
    node["rotation"] = sky.rotation;
    node["visible"] = sky.visible;

    if (!sky.customShader.empty())
        node["customShader"] = sky.customShader;

    return node;
}

SkyComponent editor::Stream::decodeSkyComponent(const YAML::Node& node, const SkyComponent* oldSky) {
    SkyComponent sky;

    if (oldSky) {
        sky = *oldSky;
    }

    const bool useDefaultTexture = node["useDefaultTexture"] ? node["useDefaultTexture"].as<bool>() : false;
    if (useDefaultTexture) {
        ProjectUtils::setDefaultSkyTexture(sky.texture);
    }

    if (node["texture"]) {
        if (useDefaultTexture) {
            const YAML::Node& texNode = node["texture"];
            if (texNode["minFilter"]) sky.texture.setMinFilter(stringToTextureFilter(texNode["minFilter"].as<std::string>()));
            if (texNode["magFilter"]) sky.texture.setMagFilter(stringToTextureFilter(texNode["magFilter"].as<std::string>()));
            if (texNode["wrapU"]) sky.texture.setWrapU(stringToTextureWrap(texNode["wrapU"].as<std::string>()));
            if (texNode["wrapV"]) sky.texture.setWrapV(stringToTextureWrap(texNode["wrapV"].as<std::string>()));
            if (texNode["releaseDataAfterLoad"]) sky.texture.setReleaseDataAfterLoad(texNode["releaseDataAfterLoad"].as<bool>());
        } else {
            sky.texture = decodeTexture(node["texture"]);
        }
    }
    if (node["color"]) sky.color = decodeVector4(node["color"]);
    if (node["rotation"]) sky.rotation = node["rotation"].as<float>();
    if (node["visible"]) sky.visible = node["visible"].as<bool>();

    if (node["customShader"]) sky.customShader = node["customShader"].as<std::string>();

    return sky;
}

// ==============================
// BundleComponent
// ==============================

YAML::Node editor::Stream::encodeBundleComponent(const BundleComponent& bundle) {
    YAML::Node node;
    node["name"] = bundle.name;
    node["path"] = bundle.path;
    return node;
}

BundleComponent editor::Stream::decodeBundleComponent(const YAML::Node& node, const BundleComponent* oldBundle) {
    BundleComponent bundle;

    if (oldBundle) {
        bundle = *oldBundle;
    }

    if (node["name"]) bundle.name = node["name"].as<std::string>();
    if (node["path"]) bundle.path = node["path"].as<std::string>();

    return bundle;
}

// ==============================
// Body/Joint type string converters
// ==============================

std::string editor::Stream::bodyTypeToString(BodyType type) {
    switch (type) {
        case BodyType::STATIC: return "static";
        case BodyType::KINEMATIC: return "kinematic";
        case BodyType::DYNAMIC: return "dynamic";
        default: return "static";
    }
}

BodyType editor::Stream::stringToBodyType(const std::string& str) {
    if (str == "kinematic") return BodyType::KINEMATIC;
    if (str == "dynamic") return BodyType::DYNAMIC;
    return BodyType::STATIC;
}

std::string editor::Stream::body3DMotionQualityToString(Body3DMotionQuality motionQuality) {
    switch (motionQuality) {
        case Body3DMotionQuality::LINEAR_CAST: return "linear_cast";
        case Body3DMotionQuality::DISCRETE:
        default: return "discrete";
    }
}

Body3DMotionQuality editor::Stream::stringToBody3DMotionQuality(const std::string& str) {
    if (str == "linear_cast") return Body3DMotionQuality::LINEAR_CAST;
    return Body3DMotionQuality::DISCRETE;
}

std::string editor::Stream::shape2DTypeToString(Shape2DType type) {
    switch (type) {
        case Shape2DType::POLYGON: return "polygon";
        case Shape2DType::CIRCLE: return "circle";
        case Shape2DType::CAPSULE: return "capsule";
        case Shape2DType::SEGMENT: return "segment";
        case Shape2DType::CHAIN: return "chain";
        default: return "polygon";
    }
}

Shape2DType editor::Stream::stringToShape2DType(const std::string& str) {
    if (str == "circle") return Shape2DType::CIRCLE;
    if (str == "capsule") return Shape2DType::CAPSULE;
    if (str == "segment") return Shape2DType::SEGMENT;
    if (str == "chain") return Shape2DType::CHAIN;
    return Shape2DType::POLYGON;
}

std::string editor::Stream::shape3DTypeToString(Shape3DType type) {
    switch (type) {
        case Shape3DType::SPHERE: return "sphere";
        case Shape3DType::BOX: return "box";
        case Shape3DType::CAPSULE: return "capsule";
        case Shape3DType::TAPERED_CAPSULE: return "tapered_capsule";
        case Shape3DType::CYLINDER: return "cylinder";
        case Shape3DType::CONVEX_HULL: return "convex_hull";
        case Shape3DType::MESH: return "mesh";
        case Shape3DType::HEIGHTFIELD: return "heightfield";
        default: return "sphere";
    }
}

Shape3DType editor::Stream::stringToShape3DType(const std::string& str) {
    if (str == "box") return Shape3DType::BOX;
    if (str == "capsule") return Shape3DType::CAPSULE;
    if (str == "tapered_capsule") return Shape3DType::TAPERED_CAPSULE;
    if (str == "cylinder") return Shape3DType::CYLINDER;
    if (str == "convex_hull") return Shape3DType::CONVEX_HULL;
    if (str == "mesh") return Shape3DType::MESH;
    if (str == "heightfield") return Shape3DType::HEIGHTFIELD;
    return Shape3DType::SPHERE;
}

std::string editor::Stream::joint2DTypeToString(Joint2DType type) {
    switch (type) {
        case Joint2DType::DISTANCE: return "distance";
        case Joint2DType::REVOLUTE: return "revolute";
        case Joint2DType::PRISMATIC: return "prismatic";
        case Joint2DType::MOUSE: return "mouse";
        case Joint2DType::WHEEL: return "wheel";
        case Joint2DType::WELD: return "weld";
        case Joint2DType::MOTOR: return "motor";
        default: return "distance";
    }
}

Joint2DType editor::Stream::stringToJoint2DType(const std::string& str) {
    if (str == "revolute") return Joint2DType::REVOLUTE;
    if (str == "prismatic") return Joint2DType::PRISMATIC;
    if (str == "mouse") return Joint2DType::MOUSE;
    if (str == "wheel") return Joint2DType::WHEEL;
    if (str == "weld") return Joint2DType::WELD;
    if (str == "motor") return Joint2DType::MOTOR;
    return Joint2DType::DISTANCE;
}

std::string editor::Stream::joint3DTypeToString(Joint3DType type) {
    switch (type) {
        case Joint3DType::FIXED: return "fixed";
        case Joint3DType::DISTANCE: return "distance";
        case Joint3DType::POINT: return "point";
        case Joint3DType::HINGE: return "hinge";
        case Joint3DType::CONE: return "cone";
        case Joint3DType::PRISMATIC: return "prismatic";
        case Joint3DType::SWINGTWIST: return "swingtwist";
        case Joint3DType::SIXDOF: return "sixdof";
        case Joint3DType::PATH: return "path";
        case Joint3DType::GEAR: return "gear";
        case Joint3DType::RACKANDPINON: return "rackandpinion";
        case Joint3DType::PULLEY: return "pulley";
        default: return "fixed";
    }
}

Joint3DType editor::Stream::stringToJoint3DType(const std::string& str) {
    if (str == "distance") return Joint3DType::DISTANCE;
    if (str == "point") return Joint3DType::POINT;
    if (str == "hinge") return Joint3DType::HINGE;
    if (str == "cone") return Joint3DType::CONE;
    if (str == "prismatic") return Joint3DType::PRISMATIC;
    if (str == "swingtwist") return Joint3DType::SWINGTWIST;
    if (str == "sixdof") return Joint3DType::SIXDOF;
    if (str == "path") return Joint3DType::PATH;
    if (str == "gear") return Joint3DType::GEAR;
    if (str == "rackandpinion") return Joint3DType::RACKANDPINON;
    if (str == "pulley") return Joint3DType::PULLEY;
    return Joint3DType::FIXED;
}

// ==============================
// Body2DComponent encode/decode
// ==============================

YAML::Node editor::Stream::encodeBody2DComponent(const Body2DComponent& body) {
    YAML::Node node;

    node["type"] = bodyTypeToString(body.type);
    node["numShapes"] = static_cast<unsigned int>(body.numShapes);

    YAML::Node shapesNode;
    for (size_t i = 0; i < body.numShapes; i++) {
        YAML::Node shapeNode;
        shapeNode["type"] = shape2DTypeToString(body.shapes[i].type);
        shapeNode["pointA"] = encodeVector2(body.shapes[i].pointA);
        shapeNode["pointB"] = encodeVector2(body.shapes[i].pointB);
        shapeNode["radius"] = body.shapes[i].radius;
        shapeNode["loop"] = body.shapes[i].loop;

        shapeNode["density"] = body.shapes[i].density;
        shapeNode["friction"] = body.shapes[i].friction;
        shapeNode["restitution"] = body.shapes[i].restitution;
        shapeNode["enableHitEvents"] = body.shapes[i].enableHitEvents;
        shapeNode["contactEvents"] = body.shapes[i].contactEvents;
        shapeNode["preSolveEvents"] = body.shapes[i].preSolveEvents;
        shapeNode["sensorEvents"] = body.shapes[i].sensorEvents;
        shapeNode["categoryBits"] = body.shapes[i].categoryBits;
        shapeNode["maskBits"] = body.shapes[i].maskBits;
        shapeNode["groupIndex"] = body.shapes[i].groupIndex;

        YAML::Node verticesNode;
        for (size_t j = 0; j < body.shapes[i].numVertices; j++) {
            verticesNode.push_back(encodeVector2(body.shapes[i].vertices[j]));
        }
        shapeNode["vertices"] = verticesNode;

        shapesNode.push_back(shapeNode);
    }
    node["shapes"] = shapesNode;

    return node;
}

Body2DComponent editor::Stream::decodeBody2DComponent(const YAML::Node& node, const Body2DComponent* oldBody) {
    Body2DComponent body;

    if (oldBody) {
        body = *oldBody;
    }

    if (node["type"]) body.type = stringToBodyType(node["type"].as<std::string>());
    if (node["numShapes"]) body.numShapes = node["numShapes"].as<unsigned int>();

    if (node["shapes"]) {
        size_t count = node["shapes"].size();
        for (size_t i = 0; i < count; i++) {
            if (node["shapes"][i]["type"]) {
                body.shapes[i].type = stringToShape2DType(node["shapes"][i]["type"].as<std::string>());
            }
            if (node["shapes"][i]["pointA"]) {
                body.shapes[i].pointA = decodeVector2(node["shapes"][i]["pointA"]);
            }
            if (node["shapes"][i]["pointB"]) {
                body.shapes[i].pointB = decodeVector2(node["shapes"][i]["pointB"]);
            }
            if (node["shapes"][i]["radius"]) body.shapes[i].radius = node["shapes"][i]["radius"].as<float>();
            if (node["shapes"][i]["loop"]) body.shapes[i].loop = node["shapes"][i]["loop"].as<bool>();

            if (node["shapes"][i]["density"]) body.shapes[i].density = node["shapes"][i]["density"].as<float>();
            if (node["shapes"][i]["friction"]) body.shapes[i].friction = node["shapes"][i]["friction"].as<float>();
            if (node["shapes"][i]["restitution"]) body.shapes[i].restitution = node["shapes"][i]["restitution"].as<float>();
            if (node["shapes"][i]["enableHitEvents"]) body.shapes[i].enableHitEvents = node["shapes"][i]["enableHitEvents"].as<bool>();
            if (node["shapes"][i]["contactEvents"]) body.shapes[i].contactEvents = node["shapes"][i]["contactEvents"].as<bool>();
            if (node["shapes"][i]["preSolveEvents"]) body.shapes[i].preSolveEvents = node["shapes"][i]["preSolveEvents"].as<bool>();
            if (node["shapes"][i]["sensorEvents"]) body.shapes[i].sensorEvents = node["shapes"][i]["sensorEvents"].as<bool>();
            if (node["shapes"][i]["categoryBits"]) body.shapes[i].categoryBits = node["shapes"][i]["categoryBits"].as<uint16_t>();
            if (node["shapes"][i]["maskBits"]) body.shapes[i].maskBits = node["shapes"][i]["maskBits"].as<uint16_t>();
            if (node["shapes"][i]["groupIndex"]) body.shapes[i].groupIndex = node["shapes"][i]["groupIndex"].as<int16_t>();

            if (node["shapes"][i]["vertices"]) {
                size_t vcount = node["shapes"][i]["vertices"].size();
                body.shapes[i].numVertices = (uint8_t)vcount;
                for (size_t j = 0; j < vcount; j++) {
                    body.shapes[i].vertices[j] = decodeVector2(node["shapes"][i]["vertices"][j]);
                }
            }

            body.shapes[i].shape = b2_nullShapeId;
            body.shapes[i].chain = b2_nullChainId;
        }
    }

    if (oldBody && b2Body_IsValid(oldBody->body)) {
        body.needReloadBody = true;
        body.needUpdateShapes = true;
    }

    return body;
}

// ==============================
// Body3DComponent encode/decode
// ==============================

YAML::Node editor::Stream::encodeBody3DComponent(const Body3DComponent& body) {
    YAML::Node node;

    node["type"] = bodyTypeToString(body.type);
    node["motionQuality"] = body3DMotionQualityToString(body.motionQuality);
    node["sensor"] = body.sensor;
    node["numShapes"] = static_cast<unsigned int>(body.numShapes);

    YAML::Node shapesNode;
    for (size_t i = 0; i < body.numShapes; i++) {
        YAML::Node shapeNode;
        shapeNode["type"] = shape3DTypeToString(body.shapes[i].type);
        shapeNode["position"] = encodeVector3(body.shapes[i].position);
        shapeNode["rotation"] = encodeQuaternion(body.shapes[i].rotation);
        shapeNode["width"] = body.shapes[i].width;
        shapeNode["height"] = body.shapes[i].height;
        shapeNode["depth"] = body.shapes[i].depth;
        shapeNode["radius"] = body.shapes[i].radius;
        shapeNode["halfHeight"] = body.shapes[i].halfHeight;
        shapeNode["topRadius"] = body.shapes[i].topRadius;
        shapeNode["bottomRadius"] = body.shapes[i].bottomRadius;
        shapeNode["density"] = body.shapes[i].density;

        switch (body.shapes[i].source) {
            case Shape3DSource::RAW_VERTICES: shapeNode["source"] = "raw_vertices"; break;
            case Shape3DSource::RAW_MESH: shapeNode["source"] = "raw_mesh"; break;
            case Shape3DSource::ENTITY_MESH: shapeNode["source"] = "entity_mesh"; break;
            case Shape3DSource::ENTITY_HEIGHTFIELD: shapeNode["source"] = "entity_heightfield"; break;
            default: shapeNode["source"] = "none"; break;
        }

        shapeNode["sourceEntity"] = body.shapes[i].sourceEntity;
        shapeNode["samplesSize"] = body.shapes[i].samplesSize;

        YAML::Node verticesNode;
        for (size_t j = 0; j < body.shapes[i].numVertices; j++) {
            verticesNode.push_back(encodeVector3(body.shapes[i].vertices[j]));
        }
        shapeNode["vertices"] = verticesNode;

        YAML::Node indicesNode;
        for (size_t j = 0; j < body.shapes[i].numIndices; j++) {
            indicesNode.push_back(body.shapes[i].indices[j]);
        }
        shapeNode["indices"] = indicesNode;

        shapesNode.push_back(shapeNode);
    }
    node["shapes"] = shapesNode;

    return node;
}

Body3DComponent editor::Stream::decodeBody3DComponent(const YAML::Node& node, const Body3DComponent* oldBody, const Vector3& bodyScale) {
    Body3DComponent body;

    if (oldBody) {
        body = *oldBody;
    }

    if (node["type"]) body.type = stringToBodyType(node["type"].as<std::string>());
    if (node["motionQuality"]) body.motionQuality = stringToBody3DMotionQuality(node["motionQuality"].as<std::string>());
    if (node["sensor"]) body.sensor = node["sensor"].as<bool>();
    if (node["numShapes"]) body.numShapes = node["numShapes"].as<unsigned int>();

    if (node["shapes"]) {
        size_t count = node["shapes"].size();
        for (size_t i = 0; i < count; i++) {
            if (node["shapes"][i]["type"]) {
                body.shapes[i].type = stringToShape3DType(node["shapes"][i]["type"].as<std::string>());
            }
            if (node["shapes"][i]["position"]) {
                body.shapes[i].position = decodeVector3(node["shapes"][i]["position"]);

                const float physicsOffset = (body.shapes[i].position * bodyScale).length();
                if (body.numShapes == 1 && physicsOffset > MAX_SINGLE_SHAPE_PHYSICS_OFFSET) {
                    Log::warn("Body3D shape position after transform scale is too large (%.2f). Physics may be unstable.", physicsOffset);
                }
            }
            if (node["shapes"][i]["rotation"]) {
                body.shapes[i].rotation = decodeQuaternion(node["shapes"][i]["rotation"]);
            }
            if (node["shapes"][i]["width"]) body.shapes[i].width = node["shapes"][i]["width"].as<float>();
            if (node["shapes"][i]["height"]) body.shapes[i].height = node["shapes"][i]["height"].as<float>();
            if (node["shapes"][i]["depth"]) body.shapes[i].depth = node["shapes"][i]["depth"].as<float>();
            if (node["shapes"][i]["radius"]) body.shapes[i].radius = node["shapes"][i]["radius"].as<float>();
            if (node["shapes"][i]["halfHeight"]) body.shapes[i].halfHeight = node["shapes"][i]["halfHeight"].as<float>();
            if (node["shapes"][i]["topRadius"]) body.shapes[i].topRadius = node["shapes"][i]["topRadius"].as<float>();
            if (node["shapes"][i]["bottomRadius"]) body.shapes[i].bottomRadius = node["shapes"][i]["bottomRadius"].as<float>();
            if (node["shapes"][i]["density"]) body.shapes[i].density = node["shapes"][i]["density"].as<float>();

            if (node["shapes"][i]["source"]) {
                std::string source = node["shapes"][i]["source"].as<std::string>();
                if (source == "raw_vertices") body.shapes[i].source = Shape3DSource::RAW_VERTICES;
                else if (source == "raw_mesh") body.shapes[i].source = Shape3DSource::RAW_MESH;
                else if (source == "entity_mesh") body.shapes[i].source = Shape3DSource::ENTITY_MESH;
                else if (source == "entity_heightfield") body.shapes[i].source = Shape3DSource::ENTITY_HEIGHTFIELD;
                else body.shapes[i].source = Shape3DSource::NONE;
            }

            if (body.shapes[i].source == Shape3DSource::NONE) {
                if (body.shapes[i].type == Shape3DType::CONVEX_HULL) {
                    body.shapes[i].source = Shape3DSource::ENTITY_MESH;
                } else if (body.shapes[i].type == Shape3DType::MESH) {
                    body.shapes[i].source = Shape3DSource::ENTITY_MESH;
                } else if (body.shapes[i].type == Shape3DType::HEIGHTFIELD) {
                    body.shapes[i].source = Shape3DSource::ENTITY_HEIGHTFIELD;
                }
            }

            if (node["shapes"][i]["sourceEntity"]) body.shapes[i].sourceEntity = node["shapes"][i]["sourceEntity"].as<Entity>();

            if (node["shapes"][i]["samplesSize"]) body.shapes[i].samplesSize = node["shapes"][i]["samplesSize"].as<unsigned int>();

            if (node["shapes"][i]["vertices"]) {
                size_t vcount = node["shapes"][i]["vertices"].size();
                body.shapes[i].numVertices = (uint16_t)vcount;
                for (size_t j = 0; j < vcount; j++) {
                    body.shapes[i].vertices[j] = decodeVector3(node["shapes"][i]["vertices"][j]);
                }
            }

            if (node["shapes"][i]["indices"]) {
                size_t icount = node["shapes"][i]["indices"].size();
                body.shapes[i].numIndices = (uint16_t)icount;
                for (size_t j = 0; j < icount; j++) {
                    body.shapes[i].indices[j] = node["shapes"][i]["indices"][j].as<uint16_t>();
                }
            }

            body.shapes[i].shape = NULL;
        }
    }

    if (oldBody && !oldBody->body.IsInvalid()) {
        body.needReloadBody = true;
        body.needUpdateShapes = true;
    }

    return body;
}

// ==============================
// Joint2DComponent encode/decode
// ==============================

YAML::Node editor::Stream::encodeJoint2DComponent(const Joint2DComponent& joint) {
    YAML::Node node;

    node["type"] = joint2DTypeToString(joint.type);
    node["bodyA"] = joint.bodyA;
    node["bodyB"] = joint.bodyB;
    node["anchorA"] = encodeVector2(joint.anchorA);
    node["anchorB"] = encodeVector2(joint.anchorB);
    node["axis"] = encodeVector2(joint.axis);
    node["target"] = encodeVector2(joint.target);
    node["autoAnchors"] = joint.autoAnchors;
    node["rope"] = joint.rope;

    return node;
}

Joint2DComponent editor::Stream::decodeJoint2DComponent(const YAML::Node& node, const Joint2DComponent* oldJoint) {
    Joint2DComponent joint;

    if (oldJoint) {
        joint = *oldJoint;
    }

    if (node["type"]) joint.type = stringToJoint2DType(node["type"].as<std::string>());
    if (node["bodyA"]) joint.bodyA = node["bodyA"].as<Entity>();
    if (node["bodyB"]) joint.bodyB = node["bodyB"].as<Entity>();
    if (node["anchorA"]) joint.anchorA = decodeVector2(node["anchorA"]);
    if (node["anchorB"]) joint.anchorB = decodeVector2(node["anchorB"]);
    if (node["axis"]) joint.axis = decodeVector2(node["axis"]);
    if (node["target"]) joint.target = decodeVector2(node["target"]);
    if (node["autoAnchors"]) joint.autoAnchors = node["autoAnchors"].as<bool>();
    if (node["rope"]) joint.rope = node["rope"].as<bool>();

    if (oldJoint && b2Joint_IsValid(oldJoint->joint)) {
        joint.needUpdateJoint = true;
    }

    return joint;
}

// ==============================
// Joint3DComponent encode/decode
// ==============================

YAML::Node editor::Stream::encodeJoint3DComponent(const Joint3DComponent& joint) {
    YAML::Node node;

    node["type"] = joint3DTypeToString(joint.type);
    node["bodyA"] = joint.bodyA;
    node["bodyB"] = joint.bodyB;
    node["anchorA"] = encodeVector3(joint.anchorA);
    node["anchorB"] = encodeVector3(joint.anchorB);
    node["anchor"] = encodeVector3(joint.anchor);
    node["axis"] = encodeVector3(joint.axis);
    node["normal"] = encodeVector3(joint.normal);
    node["twistAxis"] = encodeVector3(joint.twistAxis);
    node["planeAxis"] = encodeVector3(joint.planeAxis);
    node["axisX"] = encodeVector3(joint.axisX);
    node["axisY"] = encodeVector3(joint.axisY);
    node["limitsMin"] = joint.limitsMin;
    node["limitsMax"] = joint.limitsMax;
    node["normalHalfConeAngle"] = joint.normalHalfConeAngle;
    node["planeHalfConeAngle"] = joint.planeHalfConeAngle;
    node["twistMinAngle"] = joint.twistMinAngle;
    node["twistMaxAngle"] = joint.twistMaxAngle;
    node["fixedPointA"] = encodeVector3(joint.fixedPointA);
    node["fixedPointB"] = encodeVector3(joint.fixedPointB);
    node["hingeA"] = joint.hingeA;
    node["hingeB"] = joint.hingeB;
    node["hinge"] = joint.hinge;
    node["slider"] = joint.slider;
    node["numTeethGearA"] = joint.numTeethGearA;
    node["numTeethGearB"] = joint.numTeethGearB;
    node["numTeethRack"] = joint.numTeethRack;
    node["numTeethGear"] = joint.numTeethGear;
    node["rackLength"] = joint.rackLength;
    YAML::Node pathPointsNode;
    for (const Vector3& point : joint.pathPoints){
        pathPointsNode.push_back(encodeVector3(point));
    }
    node["pathPoints"] = pathPointsNode;
    node["pathPosition"] = encodeVector3(joint.pathPosition);
    node["isLooping"] = joint.isLooping;
    node["autoAnchors"] = joint.autoAnchors;

    return node;
}

Joint3DComponent editor::Stream::decodeJoint3DComponent(const YAML::Node& node, const Joint3DComponent* oldJoint) {
    Joint3DComponent joint;

    if (oldJoint) {
        joint = *oldJoint;
    }

    if (node["type"]) joint.type = stringToJoint3DType(node["type"].as<std::string>());
    if (node["bodyA"]) joint.bodyA = node["bodyA"].as<Entity>();
    if (node["bodyB"]) joint.bodyB = node["bodyB"].as<Entity>();
    if (node["anchorA"]) joint.anchorA = decodeVector3(node["anchorA"]);
    if (node["anchorB"]) joint.anchorB = decodeVector3(node["anchorB"]);
    if (node["anchor"]) joint.anchor = decodeVector3(node["anchor"]);
    if (node["axis"]) joint.axis = decodeVector3(node["axis"]);
    if (node["normal"]) joint.normal = decodeVector3(node["normal"]);
    if (node["twistAxis"]) joint.twistAxis = decodeVector3(node["twistAxis"]);
    if (node["planeAxis"]) joint.planeAxis = decodeVector3(node["planeAxis"]);
    if (node["axisX"]) joint.axisX = decodeVector3(node["axisX"]);
    if (node["axisY"]) joint.axisY = decodeVector3(node["axisY"]);
    if (node["limitsMin"]) joint.limitsMin = node["limitsMin"].as<float>();
    if (node["limitsMax"]) joint.limitsMax = node["limitsMax"].as<float>();
    if (node["normalHalfConeAngle"]) joint.normalHalfConeAngle = node["normalHalfConeAngle"].as<float>();
    if (node["planeHalfConeAngle"]) joint.planeHalfConeAngle = node["planeHalfConeAngle"].as<float>();
    if (node["twistMinAngle"]) joint.twistMinAngle = node["twistMinAngle"].as<float>();
    if (node["twistMaxAngle"]) joint.twistMaxAngle = node["twistMaxAngle"].as<float>();
    if (node["fixedPointA"]) joint.fixedPointA = decodeVector3(node["fixedPointA"]);
    if (node["fixedPointB"]) joint.fixedPointB = decodeVector3(node["fixedPointB"]);
    if (node["hingeA"]) joint.hingeA = node["hingeA"].as<Entity>();
    if (node["hingeB"]) joint.hingeB = node["hingeB"].as<Entity>();
    if (node["hinge"]) joint.hinge = node["hinge"].as<Entity>();
    if (node["slider"]) joint.slider = node["slider"].as<Entity>();
    if (node["numTeethGearA"]) joint.numTeethGearA = node["numTeethGearA"].as<int>();
    if (node["numTeethGearB"]) joint.numTeethGearB = node["numTeethGearB"].as<int>();
    if (node["numTeethRack"]) joint.numTeethRack = node["numTeethRack"].as<int>();
    if (node["numTeethGear"]) joint.numTeethGear = node["numTeethGear"].as<int>();
    if (node["rackLength"]) joint.rackLength = node["rackLength"].as<int>();
    if (node["pathPoints"]){
        joint.pathPoints.clear();
        for (const YAML::Node& pointNode : node["pathPoints"]){
            joint.pathPoints.push_back(decodeVector3(pointNode));
        }
    }
    if (node["pathPosition"]) joint.pathPosition = decodeVector3(node["pathPosition"]);
    if (node["isLooping"]) joint.isLooping = node["isLooping"].as<bool>();
    if (node["autoAnchors"]) joint.autoAnchors = node["autoAnchors"].as<bool>();

    if (oldJoint && oldJoint->joint) {
        joint.needUpdateJoint = true;
    }

    return joint;
}

// ── ActionComponent ──

std::string editor::Stream::actionStateToString(ActionState state) {
    switch (state) {
        case ActionState::Running: return "Running";
        case ActionState::Paused: return "Paused";
        case ActionState::Stopped: return "Stopped";
        default: return "Stopped";
    }
}

ActionState editor::Stream::stringToActionState(const std::string& str) {
    if (str == "Running") return ActionState::Running;
    if (str == "Paused") return ActionState::Paused;
    return ActionState::Stopped;
}

YAML::Node editor::Stream::encodeActionComponent(const ActionComponent& action) {
    YAML::Node node;

    node["state"] = actionStateToString(action.state);
    node["speed"] = action.speed;
    node["target"] = action.target;
    node["ownedTarget"] = action.ownedTarget;

    return node;
}

ActionComponent editor::Stream::decodeActionComponent(const YAML::Node& node, const ActionComponent* oldAction) {
    ActionComponent action;

    if (oldAction) {
        action = *oldAction;
    }

    action.timecount = 0;
    action.startTrigger = false;
    action.stopTrigger = false;
    action.pauseTrigger = false;

    if (node["state"]) action.state = stringToActionState(node["state"].as<std::string>());
    if (node["speed"]) action.speed = node["speed"].as<float>();
    if (node["target"]) action.target = node["target"].as<Entity>();
    if (node["ownedTarget"]) action.ownedTarget = node["ownedTarget"].as<bool>();

    return action;
}

// ── TimedActionComponent ──

YAML::Node editor::Stream::encodeTimedActionComponent(const TimedActionComponent& timed) {
    YAML::Node node;

    node["duration"] = timed.duration;
    node["loop"] = timed.loop;
    node["function"] = encodeEase(timed.function);

    return node;
}

TimedActionComponent editor::Stream::decodeTimedActionComponent(const YAML::Node& node, const TimedActionComponent* oldTimed) {
    TimedActionComponent timed;

    if (oldTimed) {
        timed = *oldTimed;
    }

    timed.time = 0;
    timed.value = 0;

    if (node["duration"]) timed.duration = node["duration"].as<float>();
    if (node["loop"]) timed.loop = node["loop"].as<bool>();
    if (node["function"]) timed.function = decodeEase(node["function"], oldTimed ? &oldTimed->function : nullptr);

    return timed;
}

// ── PositionActionComponent ──

YAML::Node editor::Stream::encodePositionActionComponent(const PositionActionComponent& posAction) {
    YAML::Node node;

    node["endPosition"] = encodeVector3(posAction.endPosition);
    node["startPosition"] = encodeVector3(posAction.startPosition);

    return node;
}

PositionActionComponent editor::Stream::decodePositionActionComponent(const YAML::Node& node, const PositionActionComponent* oldPosAction) {
    PositionActionComponent posAction;

    if (oldPosAction) {
        posAction = *oldPosAction;
    }

    if (node["endPosition"]) posAction.endPosition = decodeVector3(node["endPosition"]);
    if (node["startPosition"]) posAction.startPosition = decodeVector3(node["startPosition"]);

    return posAction;
}

// ── RotationActionComponent ──

YAML::Node editor::Stream::encodeRotationActionComponent(const RotationActionComponent& rotAction) {
    YAML::Node node;

    node["endRotation"] = encodeQuaternion(rotAction.endRotation);
    node["startRotation"] = encodeQuaternion(rotAction.startRotation);
    node["shortestPath"] = rotAction.shortestPath;

    return node;
}

RotationActionComponent editor::Stream::decodeRotationActionComponent(const YAML::Node& node, const RotationActionComponent* oldRotAction) {
    RotationActionComponent rotAction;

    if (oldRotAction) {
        rotAction = *oldRotAction;
    }

    if (node["endRotation"]) rotAction.endRotation = decodeQuaternion(node["endRotation"]);
    if (node["startRotation"]) rotAction.startRotation = decodeQuaternion(node["startRotation"]);
    if (node["shortestPath"]) rotAction.shortestPath = node["shortestPath"].as<bool>();

    return rotAction;
}

// ── ScaleActionComponent ──

YAML::Node editor::Stream::encodeScaleActionComponent(const ScaleActionComponent& scaleAction) {
    YAML::Node node;

    node["endScale"] = encodeVector3(scaleAction.endScale);
    node["startScale"] = encodeVector3(scaleAction.startScale);

    return node;
}

ScaleActionComponent editor::Stream::decodeScaleActionComponent(const YAML::Node& node, const ScaleActionComponent* oldScaleAction) {
    ScaleActionComponent scaleAction;

    if (oldScaleAction) {
        scaleAction = *oldScaleAction;
    }

    if (node["endScale"]) scaleAction.endScale = decodeVector3(node["endScale"]);
    if (node["startScale"]) scaleAction.startScale = decodeVector3(node["startScale"]);

    return scaleAction;
}

// ── ColorActionComponent ──

YAML::Node editor::Stream::encodeColorActionComponent(const ColorActionComponent& colorAction) {
    YAML::Node node;

    node["endColor"] = encodeVector3(colorAction.endColor);
    node["startColor"] = encodeVector3(colorAction.startColor);
    node["useSRGB"] = colorAction.useSRGB;

    return node;
}

ColorActionComponent editor::Stream::decodeColorActionComponent(const YAML::Node& node, const ColorActionComponent* oldColorAction) {
    ColorActionComponent colorAction;

    if (oldColorAction) {
        colorAction = *oldColorAction;
    }

    if (node["endColor"]) colorAction.endColor = decodeVector3(node["endColor"]);
    if (node["startColor"]) colorAction.startColor = decodeVector3(node["startColor"]);
    if (node["useSRGB"]) colorAction.useSRGB = node["useSRGB"].as<bool>();

    return colorAction;
}

// ── AlphaActionComponent ──

YAML::Node editor::Stream::encodeAlphaActionComponent(const AlphaActionComponent& alphaAction) {
    YAML::Node node;

    node["endAlpha"] = alphaAction.endAlpha;
    node["startAlpha"] = alphaAction.startAlpha;

    return node;
}

AlphaActionComponent editor::Stream::decodeAlphaActionComponent(const YAML::Node& node, const AlphaActionComponent* oldAlphaAction) {
    AlphaActionComponent alphaAction;

    if (oldAlphaAction) {
        alphaAction = *oldAlphaAction;
    }

    if (node["endAlpha"]) alphaAction.endAlpha = node["endAlpha"].as<float>();
    if (node["startAlpha"]) alphaAction.startAlpha = node["startAlpha"].as<float>();

    return alphaAction;
}

// ── SpriteAnimationComponent ──

YAML::Node editor::Stream::encodeSpriteAnimationComponent(const SpriteAnimationComponent& spriteanim) {
    YAML::Node node;

    node["loop"] = spriteanim.loop;

    YAML::Node framesNode;
    for (unsigned int i = 0; i < spriteanim.framesSize; i++) {
        framesNode.push_back(spriteanim.frames[i]);
    }
    node["frames"] = framesNode;

    YAML::Node framesTimeNode;
    for (unsigned int i = 0; i < spriteanim.framesTimeSize; i++) {
        framesTimeNode.push_back(spriteanim.framesTime[i]);
    }
    node["framesTime"] = framesTimeNode;

    return node;
}

SpriteAnimationComponent editor::Stream::decodeSpriteAnimationComponent(const YAML::Node& node, const SpriteAnimationComponent* oldSpriteanim) {
    SpriteAnimationComponent spriteanim;

    if (oldSpriteanim) {
        spriteanim = *oldSpriteanim;
    }

    spriteanim.frameIndex = 0;
    spriteanim.frameTimeIndex = 0;
    spriteanim.spriteFrameCount = 0;

    if (node["loop"]) spriteanim.loop = node["loop"].as<bool>();

    if (node["frames"]) {
        spriteanim.framesSize = 0;
        for (const YAML::Node& frameNode : node["frames"]) {
            if (!spriteanim.frames.validIndex(spriteanim.framesSize)) break;
            spriteanim.frames[spriteanim.framesSize] = frameNode.as<int>();
            spriteanim.framesSize++;
        }
    }

    if (node["framesTime"]) {
        spriteanim.framesTimeSize = 0;
        for (const YAML::Node& timeNode : node["framesTime"]) {
            if (!spriteanim.framesTime.validIndex(spriteanim.framesTimeSize)) break;
            spriteanim.framesTime[spriteanim.framesTimeSize] = timeNode.as<int>();
            spriteanim.framesTimeSize++;
        }
    }

    return spriteanim;
}

// ── AnimationComponent ──

YAML::Node editor::Stream::encodeAnimationComponent(const AnimationComponent& animation) {
    YAML::Node node;

    node["loop"] = animation.loop;
    node["duration"] = animation.duration;
    node["ownedActions"] = animation.ownedActions;
    node["defaultFadeTime"] = animation.defaultFadeTime;

    YAML::Node actionsNode;
    for (const auto& frame : animation.actions) {
        YAML::Node frameNode;
        frameNode["track"] = frame.track;
        frameNode["startTime"] = frame.startTime;
        frameNode["duration"] = frame.duration;
        frameNode["action"] = frame.action;
        actionsNode.push_back(frameNode);
    }
    node["actions"] = actionsNode;

    return node;
}

AnimationComponent editor::Stream::decodeAnimationComponent(const YAML::Node& node, const AnimationComponent* oldAnimation) {
    AnimationComponent animation;

    if (oldAnimation) {
        animation = *oldAnimation;
    }

    if (node["loop"]) animation.loop = node["loop"].as<bool>();
    if (node["duration"]) animation.duration = node["duration"].as<float>();
    if (node["ownedActions"]) animation.ownedActions = node["ownedActions"].as<bool>();
    if (node["defaultFadeTime"]) animation.defaultFadeTime = node["defaultFadeTime"].as<float>();

    if (node["actions"]) {
        animation.actions.clear();
        for (const YAML::Node& frameNode : node["actions"]) {
            ActionFrame frame;
            if (frameNode["track"]) frame.track = frameNode["track"].as<uint32_t>();
            if (frameNode["startTime"]) frame.startTime = frameNode["startTime"].as<float>();
            if (frameNode["duration"]) frame.duration = frameNode["duration"].as<float>();
            if (frameNode["action"]) frame.action = frameNode["action"].as<Entity>();
            animation.actions.push_back(frame);
        }
    }

    return animation;
}

YAML::Node editor::Stream::encodeBoneComponent(const BoneComponent& bone) {
    YAML::Node node;

    node["model"] = static_cast<uint32_t>(bone.model);
    node["index"] = bone.index;
    node["bindPosition"] = encodeVector3(bone.bindPosition);
    node["bindRotation"] = encodeQuaternion(bone.bindRotation);
    node["bindScale"] = encodeVector3(bone.bindScale);
    node["offsetMatrix"] = encodeMatrix4(bone.offsetMatrix);

    return node;
}

BoneComponent editor::Stream::decodeBoneComponent(const YAML::Node& node, const BoneComponent* oldBone) {
    BoneComponent bone;

    if (oldBone) {
        bone = *oldBone;
    }

    if (node["model"]) bone.model = static_cast<Entity>(node["model"].as<uint32_t>());
    if (node["index"]) bone.index = node["index"].as<int>();
    if (node["bindPosition"]) bone.bindPosition = decodeVector3(node["bindPosition"]);
    if (node["bindRotation"]) bone.bindRotation = decodeQuaternion(node["bindRotation"]);
    if (node["bindScale"]) bone.bindScale = decodeVector3(node["bindScale"]);
    if (node["offsetMatrix"]) bone.offsetMatrix = decodeMatrix4(node["offsetMatrix"]);

    return bone;
}

YAML::Node editor::Stream::encodeKeyframeTracksComponent(const KeyframeTracksComponent& tracks) {
    YAML::Node node;

    node["index"] = tracks.index;
    node["interpolation"] = tracks.interpolation;

    YAML::Node timesNode;
    for (float t : tracks.times) {
        timesNode.push_back(t);
    }
    node["times"] = timesNode;

    // Imported linear tracks keep this list empty, while explicit entries from
    // the Properties editor must survive a save even when they are linear.
    if (!tracks.easings.empty()) {
        YAML::Node easingsNode;
        for (EaseType e : tracks.easings) {
            easingsNode.push_back(easeTypeToString(e));
        }
        node["easings"] = easingsNode;
    }

    return node;
}

KeyframeTracksComponent editor::Stream::decodeKeyframeTracksComponent(const YAML::Node& node, const KeyframeTracksComponent* oldTracks) {
    KeyframeTracksComponent tracks;

    if (oldTracks) {
        tracks = *oldTracks;
    }

    if (node["index"]) tracks.index = node["index"].as<int>();
    if (node["interpolation"]) tracks.interpolation = node["interpolation"].as<float>();

    if (node["times"]) {
        tracks.times.clear();
        for (const YAML::Node& t : node["times"]) {
            tracks.times.push_back(t.as<float>());
        }
    }

    tracks.easings.clear();
    if (node["easings"]) {
        for (const YAML::Node& e : node["easings"]) {
            tracks.easings.push_back(stringToEaseType(e.as<std::string>()));
        }
    }

    return tracks;
}

// Value/tangent list plumbing shared by the keyframe track components below
template<typename T, typename EncodeFn>
static YAML::Node encodeList(const std::vector<T>& values, EncodeFn encodeValue) {
    YAML::Node node;
    for (const auto& v : values) {
        node.push_back(encodeValue(v));
    }
    return node;
}

template<typename T, typename DecodeFn>
static void decodeListInto(const YAML::Node& node, std::vector<T>& values, DecodeFn decodeValue) {
    values.clear();
    for (const YAML::Node& v : node) {
        values.push_back(decodeValue(v));
    }
}

static YAML::Node encodeFloatList(const std::vector<float>& values) {
    YAML::Node node;
    for (float f : values) {
        node.push_back(f);
    }
    return node;
}

static std::vector<float> decodeFloatList(const YAML::Node& node) {
    std::vector<float> values;
    for (const YAML::Node& f : node) {
        values.push_back(f.as<float>());
    }
    return values;
}

// Encodes values plus the optional Hermite tangents (GLTF CUBICSPLINE clips);
// decode clears tangents that are absent from the file
template<typename Tracks, typename EncodeFn>
static YAML::Node encodeTrackLists(const Tracks& tracks, EncodeFn encodeValue) {
    YAML::Node node;
    node["values"] = encodeList(tracks.values, encodeValue);
    if (!tracks.inTangents.empty()) {
        node["inTangents"] = encodeList(tracks.inTangents, encodeValue);
    }
    if (!tracks.outTangents.empty()) {
        node["outTangents"] = encodeList(tracks.outTangents, encodeValue);
    }
    return node;
}

template<typename Tracks, typename DecodeFn>
static void decodeTrackLists(const YAML::Node& node, Tracks& tracks, DecodeFn decodeValue) {
    if (node["values"]) {
        decodeListInto(node["values"], tracks.values, decodeValue);
    }
    tracks.inTangents.clear();
    if (node["inTangents"]) {
        decodeListInto(node["inTangents"], tracks.inTangents, decodeValue);
    }
    tracks.outTangents.clear();
    if (node["outTangents"]) {
        decodeListInto(node["outTangents"], tracks.outTangents, decodeValue);
    }
}

YAML::Node editor::Stream::encodeTranslateTracksComponent(const TranslateTracksComponent& tracks) {
    return encodeTrackLists(tracks, encodeVector3);
}

TranslateTracksComponent editor::Stream::decodeTranslateTracksComponent(const YAML::Node& node, const TranslateTracksComponent* oldTracks) {
    TranslateTracksComponent tracks;

    if (oldTracks) {
        tracks = *oldTracks;
    }

    decodeTrackLists(node, tracks, decodeVector3);

    return tracks;
}

YAML::Node editor::Stream::encodeRotateTracksComponent(const RotateTracksComponent& tracks) {
    return encodeTrackLists(tracks, encodeQuaternion);
}

RotateTracksComponent editor::Stream::decodeRotateTracksComponent(const YAML::Node& node, const RotateTracksComponent* oldTracks) {
    RotateTracksComponent tracks;

    if (oldTracks) {
        tracks = *oldTracks;
    }

    decodeTrackLists(node, tracks, decodeQuaternion);

    return tracks;
}

YAML::Node editor::Stream::encodeScaleTracksComponent(const ScaleTracksComponent& tracks) {
    return encodeTrackLists(tracks, encodeVector3);
}

ScaleTracksComponent editor::Stream::decodeScaleTracksComponent(const YAML::Node& node, const ScaleTracksComponent* oldTracks) {
    ScaleTracksComponent tracks;

    if (oldTracks) {
        tracks = *oldTracks;
    }

    decodeTrackLists(node, tracks, decodeVector3);

    return tracks;
}

YAML::Node editor::Stream::encodeMorphTracksComponent(const MorphTracksComponent& tracks) {
    return encodeTrackLists(tracks, encodeFloatList);
}

MorphTracksComponent editor::Stream::decodeMorphTracksComponent(const YAML::Node& node, const MorphTracksComponent* oldTracks) {
    MorphTracksComponent tracks;

    if (oldTracks) {
        tracks = *oldTracks;
    }

    decodeTrackLists(node, tracks, decodeFloatList);

    return tracks;
}

YAML::Node editor::Stream::encodeParticlesComponent(const ParticlesComponent& particles) {
    YAML::Node node;

    node["maxParticles"] = particles.maxParticles;
    node["emitter"] = particles.emitter;
    node["loop"] = particles.loop;
    node["localSpace"] = particles.localSpace;
    node["rate"] = particles.rate;
    node["maxPerUpdate"] = particles.maxPerUpdate;

    YAML::Node bursts;
    for (const auto& b : particles.bursts) {
        YAML::Node bn;
        bn["time"] = b.time;
        bn["minCount"] = b.minCount;
        bn["maxCount"] = b.maxCount;
        bursts.push_back(bn);
    }
    node["bursts"] = bursts;

    YAML::Node lifeInit;
    lifeInit["minLife"] = particles.lifeInitializer.minLife;
    lifeInit["maxLife"] = particles.lifeInitializer.maxLife;
    node["lifeInitializer"] = lifeInit;

    YAML::Node posInit;
    posInit["shape"] = particleEmitterShapeToString(particles.positionInitializer.shape);
    posInit["minPosition"] = encodeVector3(particles.positionInitializer.minPosition);
    posInit["maxPosition"] = encodeVector3(particles.positionInitializer.maxPosition);
    posInit["radius"] = particles.positionInitializer.radius;
    posInit["innerRadius"] = particles.positionInitializer.innerRadius;
    posInit["coneAngle"] = particles.positionInitializer.coneAngle;
    posInit["coneHeight"] = particles.positionInitializer.coneHeight;
    node["positionInitializer"] = posInit;

    YAML::Node posMod;
    posMod["fromTime"] = particles.positionModifier.fromTime;
    posMod["toTime"] = particles.positionModifier.toTime;
    posMod["fromPosition"] = encodeVector3(particles.positionModifier.fromPosition);
    posMod["toPosition"] = encodeVector3(particles.positionModifier.toPosition);
    posMod["function"] = encodeEase(particles.positionModifier.function);
    node["positionModifier"] = posMod;

    YAML::Node velInit;
    velInit["minVelocity"] = encodeVector3(particles.velocityInitializer.minVelocity);
    velInit["maxVelocity"] = encodeVector3(particles.velocityInitializer.maxVelocity);
    node["velocityInitializer"] = velInit;

    YAML::Node velMod;
    velMod["fromTime"] = particles.velocityModifier.fromTime;
    velMod["toTime"] = particles.velocityModifier.toTime;
    velMod["fromVelocity"] = encodeVector3(particles.velocityModifier.fromVelocity);
    velMod["toVelocity"] = encodeVector3(particles.velocityModifier.toVelocity);
    velMod["function"] = encodeEase(particles.velocityModifier.function);
    node["velocityModifier"] = velMod;

    YAML::Node accelInit;
    accelInit["minAcceleration"] = encodeVector3(particles.accelerationInitializer.minAcceleration);
    accelInit["maxAcceleration"] = encodeVector3(particles.accelerationInitializer.maxAcceleration);
    node["accelerationInitializer"] = accelInit;

    YAML::Node accelMod;
    accelMod["fromTime"] = particles.accelerationModifier.fromTime;
    accelMod["toTime"] = particles.accelerationModifier.toTime;
    accelMod["fromAcceleration"] = encodeVector3(particles.accelerationModifier.fromAcceleration);
    accelMod["toAcceleration"] = encodeVector3(particles.accelerationModifier.toAcceleration);
    accelMod["function"] = encodeEase(particles.accelerationModifier.function);
    node["accelerationModifier"] = accelMod;

    YAML::Node colorInit;
    colorInit["minColor"] = encodeVector3(particles.colorInitializer.minColor);
    colorInit["maxColor"] = encodeVector3(particles.colorInitializer.maxColor);
    colorInit["useSRGB"] = particles.colorInitializer.useSRGB;
    node["colorInitializer"] = colorInit;

    YAML::Node colorMod;
    colorMod["fromTime"] = particles.colorModifier.fromTime;
    colorMod["toTime"] = particles.colorModifier.toTime;
    colorMod["fromColor"] = encodeVector3(particles.colorModifier.fromColor);
    colorMod["toColor"] = encodeVector3(particles.colorModifier.toColor);
    colorMod["function"] = encodeEase(particles.colorModifier.function);
    colorMod["useSRGB"] = particles.colorModifier.useSRGB;
    node["colorModifier"] = colorMod;

    YAML::Node colorGrad;
    colorGrad["useSRGB"] = particles.colorGradient.useSRGB;
    YAML::Node colorGradStops;
    for (const auto& s : particles.colorGradient.stops) {
        YAML::Node sn;
        sn["time"] = s.time;
        sn["color"] = encodeVector3(s.color);
        colorGradStops.push_back(sn);
    }
    colorGrad["stops"] = colorGradStops;
    node["colorGradient"] = colorGrad;

    YAML::Node alphaInit;
    alphaInit["minAlpha"] = particles.alphaInitializer.minAlpha;
    alphaInit["maxAlpha"] = particles.alphaInitializer.maxAlpha;
    node["alphaInitializer"] = alphaInit;

    YAML::Node alphaMod;
    alphaMod["fromTime"] = particles.alphaModifier.fromTime;
    alphaMod["toTime"] = particles.alphaModifier.toTime;
    alphaMod["fromAlpha"] = particles.alphaModifier.fromAlpha;
    alphaMod["toAlpha"] = particles.alphaModifier.toAlpha;
    alphaMod["function"] = encodeEase(particles.alphaModifier.function);
    node["alphaModifier"] = alphaMod;

    YAML::Node sizeInit;
    sizeInit["minSize"] = particles.sizeInitializer.minSize;
    sizeInit["maxSize"] = particles.sizeInitializer.maxSize;
    node["sizeInitializer"] = sizeInit;

    YAML::Node sizeMod;
    sizeMod["fromTime"] = particles.sizeModifier.fromTime;
    sizeMod["toTime"] = particles.sizeModifier.toTime;
    sizeMod["fromSize"] = particles.sizeModifier.fromSize;
    sizeMod["toSize"] = particles.sizeModifier.toSize;
    sizeMod["function"] = encodeEase(particles.sizeModifier.function);
    node["sizeModifier"] = sizeMod;

    YAML::Node spriteInit;
    YAML::Node spriteInitFrames;
    for (int f : particles.spriteInitializer.frames) spriteInitFrames.push_back(f);
    spriteInit["frames"] = spriteInitFrames;
    node["spriteInitializer"] = spriteInit;

    YAML::Node spriteMod;
    spriteMod["fromTime"] = particles.spriteModifier.fromTime;
    spriteMod["toTime"] = particles.spriteModifier.toTime;
    spriteMod["function"] = encodeEase(particles.spriteModifier.function);
    YAML::Node spriteModFrames;
    for (int f : particles.spriteModifier.frames) spriteModFrames.push_back(f);
    spriteMod["frames"] = spriteModFrames;
    node["spriteModifier"] = spriteMod;

    YAML::Node rotInit;
    rotInit["minRotation"] = encodeQuaternion(particles.rotationInitializer.minRotation);
    rotInit["maxRotation"] = encodeQuaternion(particles.rotationInitializer.maxRotation);
    rotInit["shortestPath"] = particles.rotationInitializer.shortestPath;
    node["rotationInitializer"] = rotInit;

    YAML::Node rotMod;
    rotMod["fromTime"] = particles.rotationModifier.fromTime;
    rotMod["toTime"] = particles.rotationModifier.toTime;
    rotMod["fromRotation"] = encodeQuaternion(particles.rotationModifier.fromRotation);
    rotMod["toRotation"] = encodeQuaternion(particles.rotationModifier.toRotation);
    rotMod["function"] = encodeEase(particles.rotationModifier.function);
    rotMod["shortestPath"] = particles.rotationModifier.shortestPath;
    node["rotationModifier"] = rotMod;

    YAML::Node scaleInit;
    scaleInit["minScale"] = encodeVector3(particles.scaleInitializer.minScale);
    scaleInit["maxScale"] = encodeVector3(particles.scaleInitializer.maxScale);
    scaleInit["linearSort"] = particles.scaleInitializer.linearSort;
    node["scaleInitializer"] = scaleInit;

    YAML::Node scaleMod;
    scaleMod["fromTime"] = particles.scaleModifier.fromTime;
    scaleMod["toTime"] = particles.scaleModifier.toTime;
    scaleMod["fromScale"] = encodeVector3(particles.scaleModifier.fromScale);
    scaleMod["toScale"] = encodeVector3(particles.scaleModifier.toScale);
    scaleMod["function"] = encodeEase(particles.scaleModifier.function);
    node["scaleModifier"] = scaleMod;

    return node;
}

ParticlesComponent editor::Stream::decodeParticlesComponent(const YAML::Node& node, const ParticlesComponent* oldParticles) {
    ParticlesComponent particles;
    if (oldParticles) { particles = *oldParticles; }

    if (node["maxParticles"]) particles.maxParticles = node["maxParticles"].as<unsigned int>();
    if (node["emitter"]) particles.emitter = node["emitter"].as<bool>();
    if (node["loop"]) particles.loop = node["loop"].as<bool>();
    if (node["localSpace"]) particles.localSpace = node["localSpace"].as<bool>();
    if (node["rate"]) particles.rate = node["rate"].as<int>();
    if (node["maxPerUpdate"]) particles.maxPerUpdate = node["maxPerUpdate"].as<int>();

    if (node["bursts"] && node["bursts"].IsSequence()) {
        particles.bursts.clear();
        for (const auto& bn : node["bursts"]) {
            ParticleBurst b;
            if (bn["time"]) b.time = bn["time"].as<float>();
            if (bn["minCount"]) b.minCount = bn["minCount"].as<int>();
            if (bn["maxCount"]) b.maxCount = bn["maxCount"].as<int>();
            particles.bursts.push_back(b);
        }
    }

    if (node["lifeInitializer"]) {
        const YAML::Node& n = node["lifeInitializer"];
        if (n["minLife"]) particles.lifeInitializer.minLife = n["minLife"].as<float>();
        if (n["maxLife"]) particles.lifeInitializer.maxLife = n["maxLife"].as<float>();
    }
    if (node["positionInitializer"]) {
        const YAML::Node& n = node["positionInitializer"];
        if (n["shape"]) particles.positionInitializer.shape = stringToParticleEmitterShape(n["shape"].as<std::string>());
        if (n["minPosition"]) particles.positionInitializer.minPosition = decodeVector3(n["minPosition"]);
        if (n["maxPosition"]) particles.positionInitializer.maxPosition = decodeVector3(n["maxPosition"]);
        if (n["radius"]) particles.positionInitializer.radius = n["radius"].as<float>();
        if (n["innerRadius"]) particles.positionInitializer.innerRadius = n["innerRadius"].as<float>();
        if (n["coneAngle"]) particles.positionInitializer.coneAngle = n["coneAngle"].as<float>();
        if (n["coneHeight"]) particles.positionInitializer.coneHeight = n["coneHeight"].as<float>();
    }
    if (node["positionModifier"]) {
        const YAML::Node& n = node["positionModifier"];
        if (n["fromTime"]) particles.positionModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.positionModifier.toTime = n["toTime"].as<float>();
        if (n["fromPosition"]) particles.positionModifier.fromPosition = decodeVector3(n["fromPosition"]);
        if (n["toPosition"]) particles.positionModifier.toPosition = decodeVector3(n["toPosition"]);
        if (n["function"]) particles.positionModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->positionModifier.function : nullptr);
    }
    if (node["velocityInitializer"]) {
        const YAML::Node& n = node["velocityInitializer"];
        if (n["minVelocity"]) particles.velocityInitializer.minVelocity = decodeVector3(n["minVelocity"]);
        if (n["maxVelocity"]) particles.velocityInitializer.maxVelocity = decodeVector3(n["maxVelocity"]);
    }
    if (node["velocityModifier"]) {
        const YAML::Node& n = node["velocityModifier"];
        if (n["fromTime"]) particles.velocityModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.velocityModifier.toTime = n["toTime"].as<float>();
        if (n["fromVelocity"]) particles.velocityModifier.fromVelocity = decodeVector3(n["fromVelocity"]);
        if (n["toVelocity"]) particles.velocityModifier.toVelocity = decodeVector3(n["toVelocity"]);
        if (n["function"]) particles.velocityModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->velocityModifier.function : nullptr);
    }
    if (node["accelerationInitializer"]) {
        const YAML::Node& n = node["accelerationInitializer"];
        if (n["minAcceleration"]) particles.accelerationInitializer.minAcceleration = decodeVector3(n["minAcceleration"]);
        if (n["maxAcceleration"]) particles.accelerationInitializer.maxAcceleration = decodeVector3(n["maxAcceleration"]);
    }
    if (node["accelerationModifier"]) {
        const YAML::Node& n = node["accelerationModifier"];
        if (n["fromTime"]) particles.accelerationModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.accelerationModifier.toTime = n["toTime"].as<float>();
        if (n["fromAcceleration"]) particles.accelerationModifier.fromAcceleration = decodeVector3(n["fromAcceleration"]);
        if (n["toAcceleration"]) particles.accelerationModifier.toAcceleration = decodeVector3(n["toAcceleration"]);
        if (n["function"]) particles.accelerationModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->accelerationModifier.function : nullptr);
    }
    if (node["colorInitializer"]) {
        const YAML::Node& n = node["colorInitializer"];
        if (n["minColor"]) particles.colorInitializer.minColor = decodeVector3(n["minColor"]);
        if (n["maxColor"]) particles.colorInitializer.maxColor = decodeVector3(n["maxColor"]);
        if (n["useSRGB"]) particles.colorInitializer.useSRGB = n["useSRGB"].as<bool>();
    }
    if (node["colorModifier"]) {
        const YAML::Node& n = node["colorModifier"];
        if (n["fromTime"]) particles.colorModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.colorModifier.toTime = n["toTime"].as<float>();
        if (n["fromColor"]) particles.colorModifier.fromColor = decodeVector3(n["fromColor"]);
        if (n["toColor"]) particles.colorModifier.toColor = decodeVector3(n["toColor"]);
        if (n["function"]) particles.colorModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->colorModifier.function : nullptr);
        if (n["useSRGB"]) particles.colorModifier.useSRGB = n["useSRGB"].as<bool>();
    }
    if (node["colorGradient"]) {
        const YAML::Node& n = node["colorGradient"];
        if (n["useSRGB"]) particles.colorGradient.useSRGB = n["useSRGB"].as<bool>();
        if (n["stops"] && n["stops"].IsSequence()) {
            particles.colorGradient.stops.clear();
            for (const auto& sn : n["stops"]) {
                ParticleColorGradientStop s;
                if (sn["time"]) s.time = sn["time"].as<float>();
                if (sn["color"]) s.color = decodeVector3(sn["color"]);
                particles.colorGradient.stops.push_back(s);
            }
            particles.colorGradient.normalize();
        }
    }
    if (node["alphaInitializer"]) {
        const YAML::Node& n = node["alphaInitializer"];
        if (n["minAlpha"]) particles.alphaInitializer.minAlpha = n["minAlpha"].as<float>();
        if (n["maxAlpha"]) particles.alphaInitializer.maxAlpha = n["maxAlpha"].as<float>();
    }
    if (node["alphaModifier"]) {
        const YAML::Node& n = node["alphaModifier"];
        if (n["fromTime"]) particles.alphaModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.alphaModifier.toTime = n["toTime"].as<float>();
        if (n["fromAlpha"]) particles.alphaModifier.fromAlpha = n["fromAlpha"].as<float>();
        if (n["toAlpha"]) particles.alphaModifier.toAlpha = n["toAlpha"].as<float>();
        if (n["function"]) particles.alphaModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->alphaModifier.function : nullptr);
    }
    if (node["sizeInitializer"]) {
        const YAML::Node& n = node["sizeInitializer"];
        if (n["minSize"]) particles.sizeInitializer.minSize = n["minSize"].as<float>();
        if (n["maxSize"]) particles.sizeInitializer.maxSize = n["maxSize"].as<float>();
    }
    if (node["sizeModifier"]) {
        const YAML::Node& n = node["sizeModifier"];
        if (n["fromTime"]) particles.sizeModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.sizeModifier.toTime = n["toTime"].as<float>();
        if (n["fromSize"]) particles.sizeModifier.fromSize = n["fromSize"].as<float>();
        if (n["toSize"]) particles.sizeModifier.toSize = n["toSize"].as<float>();
        if (n["function"]) particles.sizeModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->sizeModifier.function : nullptr);
    }
    if (node["spriteInitializer"]) {
        const YAML::Node& n = node["spriteInitializer"];
        if (n["frames"]) {
            particles.spriteInitializer.frames.clear();
            for (const YAML::Node& f : n["frames"]) particles.spriteInitializer.frames.push_back(f.as<int>());
        }
    }
    if (node["spriteModifier"]) {
        const YAML::Node& n = node["spriteModifier"];
        if (n["fromTime"]) particles.spriteModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.spriteModifier.toTime = n["toTime"].as<float>();
        if (n["function"]) particles.spriteModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->spriteModifier.function : nullptr);
        if (n["frames"]) {
            particles.spriteModifier.frames.clear();
            for (const YAML::Node& f : n["frames"]) particles.spriteModifier.frames.push_back(f.as<int>());
        }
    }
    if (node["rotationInitializer"]) {
        const YAML::Node& n = node["rotationInitializer"];
        if (n["minRotation"]) particles.rotationInitializer.minRotation = decodeQuaternion(n["minRotation"]);
        if (n["maxRotation"]) particles.rotationInitializer.maxRotation = decodeQuaternion(n["maxRotation"]);
        if (n["shortestPath"]) particles.rotationInitializer.shortestPath = n["shortestPath"].as<bool>();
    }
    if (node["rotationModifier"]) {
        const YAML::Node& n = node["rotationModifier"];
        if (n["fromTime"]) particles.rotationModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.rotationModifier.toTime = n["toTime"].as<float>();
        if (n["fromRotation"]) particles.rotationModifier.fromRotation = decodeQuaternion(n["fromRotation"]);
        if (n["toRotation"]) particles.rotationModifier.toRotation = decodeQuaternion(n["toRotation"]);
        if (n["function"]) particles.rotationModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->rotationModifier.function : nullptr);
        if (n["shortestPath"]) particles.rotationModifier.shortestPath = n["shortestPath"].as<bool>();
    }
    if (node["scaleInitializer"]) {
        const YAML::Node& n = node["scaleInitializer"];
        if (n["minScale"]) particles.scaleInitializer.minScale = decodeVector3(n["minScale"]);
        if (n["maxScale"]) particles.scaleInitializer.maxScale = decodeVector3(n["maxScale"]);
        if (n["linearSort"]) particles.scaleInitializer.linearSort = n["linearSort"].as<bool>();
    }
    if (node["scaleModifier"]) {
        const YAML::Node& n = node["scaleModifier"];
        if (n["fromTime"]) particles.scaleModifier.fromTime = n["fromTime"].as<float>();
        if (n["toTime"]) particles.scaleModifier.toTime = n["toTime"].as<float>();
        if (n["fromScale"]) particles.scaleModifier.fromScale = decodeVector3(n["fromScale"]);
        if (n["toScale"]) particles.scaleModifier.toScale = decodeVector3(n["toScale"]);
        if (n["function"]) particles.scaleModifier.function = decodeEase(n["function"], oldParticles ? &oldParticles->scaleModifier.function : nullptr);
    }

    // Reset runtime fields
    particles.particles.clear();
    particles.newParticlesCount = 0;
    particles.lastUsedParticle = 0;

    return particles;
}

YAML::Node editor::Stream::encodeInstancedMeshComponent(const InstancedMeshComponent& instmesh) {
    YAML::Node node;

    node["maxInstances"] = instmesh.maxInstances;
    node["instancedBillboard"] = instmesh.instancedBillboard;
    node["instancedCylindricalBillboard"] = instmesh.instancedCylindricalBillboard;

    YAML::Node instancesNode;
    for (const InstanceData& inst : instmesh.instances) {
        YAML::Node instNode;
        instNode["position"] = encodeVector3(inst.position);
        instNode["rotation"] = encodeQuaternion(inst.rotation);
        instNode["scale"] = encodeVector3(inst.scale);
        instNode["color"] = encodeVector4(inst.color);
        instNode["textureRect"] = encodeRect(inst.textureRect);
        instNode["visible"] = inst.visible;
        instancesNode.push_back(instNode);
    }
    node["instances"] = instancesNode;

    return node;
}

InstancedMeshComponent editor::Stream::decodeInstancedMeshComponent(const YAML::Node& node, const InstancedMeshComponent* oldInstmesh) {
    InstancedMeshComponent instmesh;
    if (oldInstmesh) { instmesh = *oldInstmesh; }

    if (node["maxInstances"]) instmesh.maxInstances = node["maxInstances"].as<unsigned int>();
    if (node["instancedBillboard"]) instmesh.instancedBillboard = node["instancedBillboard"].as<bool>();
    if (node["instancedCylindricalBillboard"]) instmesh.instancedCylindricalBillboard = node["instancedCylindricalBillboard"].as<bool>();

    if (node["instances"]) {
        instmesh.instances.clear();
        for (const YAML::Node& instNode : node["instances"]) {
            InstanceData inst;
            if (instNode["position"]) inst.position = decodeVector3(instNode["position"]);
            if (instNode["rotation"]) inst.rotation = decodeQuaternion(instNode["rotation"]);
            if (instNode["scale"]) inst.scale = decodeVector3(instNode["scale"]);
            if (instNode["color"]) inst.color = decodeVector4(instNode["color"]);
            if (instNode["textureRect"]) inst.textureRect = decodeRect(instNode["textureRect"]);
            if (instNode["visible"]) inst.visible = instNode["visible"].as<bool>();
            instmesh.instances.push_back(inst);
        }
    }

    // Reset runtime fields
    instmesh.renderInstances.clear();
    instmesh.numVisible = 0;
    instmesh.needUpdateBuffer = false;
    instmesh.needUpdateInstances = true;

    return instmesh;
}

YAML::Node editor::Stream::encodePointsComponent(const PointsComponent& points) {
    YAML::Node node;

    node["maxPoints"] = points.maxPoints;
    node["transparent"] = points.transparent;
    node["autoTransparency"] = points.autoTransparency;
    node["texture"] = encodeTexture(points.texture);

    YAML::Node framesNode;
    for (unsigned int i = 0; i < points.numFramesRect; i++) {
        framesNode.push_back(encodeSpriteFrameData(points.framesRect[i]));
    }
    if (framesNode.size() > 0) {
        node["framesRect"] = framesNode;
    }

    YAML::Node pointsNode;
    for (const PointData& pt : points.points) {
        YAML::Node ptNode;
        ptNode["position"] = encodeVector3(pt.position);
        ptNode["color"] = encodeVector4(pt.color);
        ptNode["size"] = pt.size;
        ptNode["rotation"] = pt.rotation;
        ptNode["textureRect"] = encodeRect(pt.textureRect);
        ptNode["visible"] = pt.visible;
        pointsNode.push_back(ptNode);
    }
    node["points"] = pointsNode;

    if (!points.customShader.empty())
        node["customShader"] = points.customShader;

    return node;
}

PointsComponent editor::Stream::decodePointsComponent(const YAML::Node& node, const PointsComponent* oldPoints) {
    PointsComponent points;
    if (oldPoints) { points = *oldPoints; }

    if (node["maxPoints"]) points.maxPoints = node["maxPoints"].as<unsigned int>();
    if (node["transparent"]) points.transparent = node["transparent"].as<bool>();
    if (node["autoTransparency"]) points.autoTransparency = node["autoTransparency"].as<bool>();
    if (node["texture"]) points.texture = decodeTexture(node["texture"]);

    if (node["framesRect"]) {
        points.numFramesRect = 0;
        for (const YAML::Node& frameNode : node["framesRect"]) {
            if (points.numFramesRect < (unsigned int)points.framesRect.size()) {
                points.framesRect[points.numFramesRect] = decodeSpriteFrameData(frameNode);
                points.numFramesRect++;
            }
        }
    }

    if (node["points"]) {
        points.points.clear();
        for (const YAML::Node& ptNode : node["points"]) {
            PointData pt;
            if (ptNode["position"]) pt.position = decodeVector3(ptNode["position"]);
            if (ptNode["color"]) pt.color = decodeVector4(ptNode["color"]);
            if (ptNode["size"]) pt.size = ptNode["size"].as<float>();
            if (ptNode["rotation"]) pt.rotation = ptNode["rotation"].as<float>();
            if (ptNode["textureRect"]) pt.textureRect = decodeRect(ptNode["textureRect"]);
            if (ptNode["visible"]) pt.visible = ptNode["visible"].as<bool>();
            points.points.push_back(pt);
        }
    }

    if (node["customShader"]) points.customShader = node["customShader"].as<std::string>();

    // Reset runtime fields
    points.renderPoints.clear();
    points.numVisible = 0;
    points.loaded = false;
    points.loadCalled = false;
    points.needUpdate = true;
    points.needUpdateBuffer = false;
    points.needUpdateTexture = true;
    points.needReload = false;

    return points;
}

YAML::Node editor::Stream::encodeLinesComponent(const LinesComponent& lines) {
    YAML::Node node;

    node["maxLines"] = lines.maxLines;

    YAML::Node linesNode;
    for (const LineData& line : lines.lines) {
        YAML::Node lineNode;
        lineNode["pointA"] = encodeVector3(line.pointA);
        lineNode["colorA"] = encodeVector4(line.colorA);
        lineNode["pointB"] = encodeVector3(line.pointB);
        lineNode["colorB"] = encodeVector4(line.colorB);
        linesNode.push_back(lineNode);
    }
    node["lines"] = linesNode;

    if (!lines.customShader.empty())
        node["customShader"] = lines.customShader;

    return node;
}

LinesComponent editor::Stream::decodeLinesComponent(const YAML::Node& node, const LinesComponent* oldLines) {
    LinesComponent lines;
    if (oldLines) { lines = *oldLines; }

    if (node["maxLines"]) lines.maxLines = node["maxLines"].as<unsigned int>();

    if (node["lines"]) {
        lines.lines.clear();
        for (const YAML::Node& lineNode : node["lines"]) {
            LineData line;
            if (lineNode["pointA"]) line.pointA = decodeVector3(lineNode["pointA"]);
            if (lineNode["colorA"]) line.colorA = decodeVector4(lineNode["colorA"]);
            if (lineNode["pointB"]) line.pointB = decodeVector3(lineNode["pointB"]);
            if (lineNode["colorB"]) line.colorB = decodeVector4(lineNode["colorB"]);
            lines.lines.push_back(line);
        }
    }

    if (lines.lines.size() > lines.maxLines) {
        lines.maxLines = static_cast<unsigned int>(lines.lines.size());
    }

    if (node["customShader"]) lines.customShader = node["customShader"].as<std::string>();

    lines.loaded = false;
    lines.loadCalled = false;
    lines.needUpdateBuffer = false;
    lines.needReload = false;

    return lines;
}

YAML::Node editor::Stream::encodePolygonComponent(const PolygonComponent& polygon) {
    YAML::Node node;

    YAML::Node pointsNode;
    for (const PolygonPoint& point : polygon.points) {
        YAML::Node pointNode;
        pointNode["position"] = encodeVector3(point.position);
        pointNode["color"] = encodeVector4(point.color);
        pointsNode.push_back(pointNode);
    }
    node["points"] = pointsNode;

    return node;
}

PolygonComponent editor::Stream::decodePolygonComponent(const YAML::Node& node, const PolygonComponent* oldPolygon) {
    PolygonComponent polygon;
    if (oldPolygon) { polygon = *oldPolygon; }

    if (node["points"]) {
        polygon.points.clear();
        for (const YAML::Node& pointNode : node["points"]) {
            PolygonPoint point;
            if (pointNode["position"]) point.position = decodeVector3(pointNode["position"]);
            if (pointNode["color"]) point.color = decodeVector4(pointNode["color"]);
            polygon.points.push_back(point);
        }
    }

    polygon.needUpdatePolygon = true;

    return polygon;
}

YAML::Node editor::Stream::encodeMeshPolygonComponent(const MeshPolygonComponent& polygon) {
    YAML::Node node;

    node["width"] = polygon.width;
    node["height"] = polygon.height;
    node["automaticFlipY"] = polygon.automaticFlipY;
    node["flipY"] = polygon.flipY;

    YAML::Node pointsNode;
    for (const PolygonPoint& point : polygon.points) {
        YAML::Node pointNode;
        pointNode["position"] = encodeVector3(point.position);
        pointNode["color"] = encodeVector4(point.color);
        pointsNode.push_back(pointNode);
    }
    node["points"] = pointsNode;

    return node;
}

MeshPolygonComponent editor::Stream::decodeMeshPolygonComponent(const YAML::Node& node, const MeshPolygonComponent* oldPolygon) {
    MeshPolygonComponent polygon;
    if (oldPolygon) { polygon = *oldPolygon; }

    if (node["width"]) polygon.width = node["width"].as<unsigned int>();
    if (node["height"]) polygon.height = node["height"].as<unsigned int>();
    if (node["automaticFlipY"]) polygon.automaticFlipY = node["automaticFlipY"].as<bool>();
    if (node["flipY"]) polygon.flipY = node["flipY"].as<bool>();

    if (node["points"]) {
        polygon.points.clear();
        for (const YAML::Node& pointNode : node["points"]) {
            PolygonPoint point;
            if (pointNode["position"]) point.position = decodeVector3(pointNode["position"]);
            if (pointNode["color"]) point.color = decodeVector4(pointNode["color"]);
            polygon.points.push_back(point);
        }
    }

    polygon.needUpdatePolygon = true;

    return polygon;
}
