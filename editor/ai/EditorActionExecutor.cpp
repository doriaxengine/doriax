// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "EditorActionExecutor.h"

#include "AiPathUtils.h"
#include "AiEngineApiContext.h"
#include "AiTerrainCapability.h"
#include "AppSettings.h"
#include "EditorActionRegistry.h"
#include "EditorHost.h"
#include "HttpClient.h"

#include "Out.h"
#include "Project.h"
#include "Catalog.h"
#include "Exporter.h"
#include "Stream.h"
#include "render/SceneRender.h"
#include "util/CxxStandards.h"
#include "util/ProjectUtils.h"
#include "util/FileUtils.h"
#include "util/TextEncoding.h"
#include "util/ScriptParser.h"
#include "component/Body2DComponent.h"
#include "component/Body3DComponent.h"
#include "component/MeshComponent.h"
#include "component/ScriptComponent.h"
#include "component/TerrainComponent.h"
#include "command/CommandHandle.h"
#include "command/CommandHistory.h"
#include "component/CameraComponent.h"
#include "command/type/AddChildSceneCmd.h"
#include "command/type/AddComponentCmd.h"
#include "command/type/AddEntityToBundleCmd.h"
#include "command/type/ComponentToBundleLocalCmd.h"
#include "command/type/ComponentToBundleSharedCmd.h"
#include "command/type/CopyFileCmd.h"
#include "command/type/CreateDirCmd.h"
#include "command/type/CreateEntityBundleCmd.h"
#include "command/type/CreateEntityCmd.h"
#include "command/type/CreateMaterialFileCmd.h"
#include "command/type/ForkShaderCmd.h"
#include "command/type/DeleteEntityCmd.h"
#include "command/type/DeleteFileCmd.h"
#include "command/type/DuplicateEntityCmd.h"
#include "command/type/EntityNameCmd.h"
#include "command/type/ImportEntityBundleCmd.h"
#include "command/type/LinkMaterialCmd.h"
#include "command/type/ModelLoadCmd.h"
#include "command/type/MoveEntityOrderCmd.h"
#include "command/type/ObjectTransformCmd.h"
#include "command/type/PropertyCmd.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/RemoveChildSceneCmd.h"
#include "command/type/RemoveComponentCmd.h"
#include "command/type/RemoveEntityFromBundleCmd.h"
#include "command/type/RenameFileCmd.h"
#include "command/type/SceneNameCmd.h"
#include "command/type/ScenePropertyCmd.h"
#include "command/type/SetChildSceneStartActiveCmd.h"
#include "command/type/SetMainCameraCmd.h"
#include "command/type/MeshChangeCmd.h"
#include "command/type/UnlinkMaterialCmd.h"
#include "component/AnimationComponent.h"
#include "component/KeyframeTracksComponent.h"
#include "component/TilemapComponent.h"
#include "subsystem/ActionSystem.h"
#include "subsystem/MeshSystem.h"
#include "util/ShapeParameters.h"
#include "texture/Texture.h"
#include "util/Util.h"
#include "window/CodeEditor.h"
#include "window/ResourcesWindow.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace doriax::editor::ai {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isScriptIdentifierStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}

bool isScriptIdentifierContinuation(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

int luaLongBracketEquals(const std::string& content, size_t position) {
    if (position >= content.size() || content[position] != '[') {
        return -1;
    }

    size_t cursor = position + 1;
    while (cursor < content.size() && content[cursor] == '=') {
        ++cursor;
    }
    return cursor < content.size() && content[cursor] == '['
        ? static_cast<int>(cursor - position - 1)
        : -1;
}

size_t skipLuaLongBracket(const std::string& content, size_t position, int equals) {
    const std::string closing = "]" + std::string(static_cast<size_t>(equals), '=') + "]";
    const size_t contentStart = position + static_cast<size_t>(equals) + 2;
    const size_t closingPosition = content.find(closing, contentStart);
    return closingPosition == std::string::npos
        ? content.size()
        : closingPosition + closing.size();
}

std::vector<std::string> tokenizeLuaForValidation(const std::string& content) {
    // Validation only cares about executable tokens. Skip all Lua string and
    // comment forms so examples or explanations cannot look like real calls.
    std::vector<std::string> tokens;
    size_t cursor = 0;
    while (cursor < content.size()) {
        const unsigned char current = static_cast<unsigned char>(content[cursor]);
        if (std::isspace(current)) {
            ++cursor;
            continue;
        }

        if (content[cursor] == '-' && cursor + 1 < content.size() &&
            content[cursor + 1] == '-') {
            const size_t commentBody = cursor + 2;
            const int longCommentEquals = luaLongBracketEquals(content, commentBody);
            if (longCommentEquals >= 0) {
                cursor = skipLuaLongBracket(content, commentBody, longCommentEquals);
            } else {
                const size_t newline = content.find('\n', commentBody);
                cursor = newline == std::string::npos ? content.size() : newline + 1;
            }
            continue;
        }

        if (content[cursor] == '\'' || content[cursor] == '"') {
            const char quote = content[cursor++];
            while (cursor < content.size()) {
                if (content[cursor] == '\\' && cursor + 1 < content.size()) {
                    cursor += 2;
                } else if (content[cursor++] == quote) {
                    break;
                }
            }
            continue;
        }

        const int longStringEquals = luaLongBracketEquals(content, cursor);
        if (longStringEquals >= 0) {
            cursor = skipLuaLongBracket(content, cursor, longStringEquals);
            continue;
        }

        if (isScriptIdentifierStart(current)) {
            const size_t start = cursor++;
            while (cursor < content.size() &&
                   isScriptIdentifierContinuation(
                       static_cast<unsigned char>(content[cursor]))) {
                ++cursor;
            }
            tokens.push_back(lower(content.substr(start, cursor - start)));
            continue;
        }

        tokens.emplace_back(1, content[cursor++]);
    }
    return tokens;
}

std::string sanitizeCppForValidation(const std::string& content) {
    std::string sanitized;
    sanitized.reserve(content.size());
    size_t cursor = 0;
    while (cursor < content.size()) {
        if (content[cursor] == '/' && cursor + 1 < content.size() &&
            content[cursor + 1] == '/') {
            const size_t newline = content.find('\n', cursor + 2);
            if (newline == std::string::npos) {
                break;
            }
            sanitized.push_back('\n');
            cursor = newline + 1;
            continue;
        }

        if (content[cursor] == '/' && cursor + 1 < content.size() &&
            content[cursor + 1] == '*') {
            const size_t closing = content.find("*/", cursor + 2);
            cursor = closing == std::string::npos ? content.size() : closing + 2;
            sanitized.push_back(' ');
            continue;
        }

        if (content[cursor] == '\'' || content[cursor] == '"') {
            const char quote = content[cursor++];
            sanitized.push_back(' ');
            while (cursor < content.size()) {
                if (content[cursor] == '\\' && cursor + 1 < content.size()) {
                    cursor += 2;
                } else if (content[cursor++] == quote) {
                    break;
                }
            }
            sanitized.push_back(' ');
            continue;
        }

        sanitized.push_back(content[cursor++]);
    }
    return sanitized;
}

std::string compactCppForValidation(const std::string& content) {
    std::string compact = lower(sanitizeCppForValidation(content));
    compact.erase(std::remove_if(compact.begin(), compact.end(), [](unsigned char c) {
        return std::isspace(c);
    }), compact.end());
    return compact;
}

bool isContinuousPhysicsMutationName(const std::string& token) {
    return token == "applyforce" ||
           token == "applyforcetocenter" ||
           token == "applytorque";
}

bool luaCallbackContainsContinuousPhysicsMutation(const std::string& content,
                                                  const std::string& callback) {
    const std::vector<std::string> tokens = tokenizeLuaForValidation(content);
    for (size_t functionIndex = 0; functionIndex < tokens.size(); ++functionIndex) {
        if (tokens[functionIndex] != "function") {
            continue;
        }

        // Assignment form: Klass.onUpdate = function(self) ... end, where the
        // name sits before the "function" keyword instead of after it.
        bool isTargetCallback = functionIndex >= 3 &&
            tokens[functionIndex - 1] == "=" &&
            tokens[functionIndex - 2] == callback &&
            (tokens[functionIndex - 3] == "." || tokens[functionIndex - 3] == ":");

        size_t bodyStart = functionIndex + 1;
        for (; bodyStart + 2 < tokens.size() && tokens[bodyStart] != "(";
             ++bodyStart) {
            if ((tokens[bodyStart] == ":" || tokens[bodyStart] == ".") &&
                tokens[bodyStart + 1] == callback &&
                tokens[bodyStart + 2] == "(") {
                isTargetCallback = true;
            }
        }
        if (!isTargetCallback || bodyStart >= tokens.size()) {
            continue;
        }

        // Follow the callback to its matching end instead of stopping at the
        // next function token; local functions may legitimately be nested.
        std::vector<std::string> blockStack = {"function"};
        int pendingLoopDo = 0;
        for (size_t i = bodyStart + 1; i < tokens.size() && !blockStack.empty(); ++i) {
            if (isContinuousPhysicsMutationName(tokens[i]) &&
                i > 0 && i + 1 < tokens.size() &&
                (tokens[i - 1] == ":" || tokens[i - 1] == ".") &&
                tokens[i + 1] == "(") {
                return true;
            }

            if (tokens[i] == "function" || tokens[i] == "if") {
                blockStack.push_back(tokens[i]);
            } else if (tokens[i] == "for" || tokens[i] == "while") {
                blockStack.push_back(tokens[i]);
                ++pendingLoopDo;
            } else if (tokens[i] == "repeat") {
                blockStack.push_back(tokens[i]);
            } else if (tokens[i] == "do") {
                if (pendingLoopDo > 0) {
                    --pendingLoopDo;
                } else {
                    blockStack.push_back(tokens[i]);
                }
            } else if (tokens[i] == "until") {
                if (!blockStack.empty() && blockStack.back() == "repeat") {
                    blockStack.pop_back();
                }
            } else if (tokens[i] == "end") {
                blockStack.pop_back();
            }
        }
    }
    return false;
}

bool containsContinuousPhysicsMutation(const std::string& content) {
    static const char* calls[] = {
        "applyforce(",
        "applyforcetocenter(",
        "applytorque("
    };
    for (const char* call : calls) {
        if (content.find(call) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool cppCallbackContainsContinuousPhysicsMutation(const std::string& compactContent,
                                                  const std::string& callback) {
    // Whitespace is already stripped, so the leading "void" is what separates a
    // definition from a call site. Both spellings must be covered: the split
    // .h/.cpp skeleton writes void Klass::onUpdate(){...}, but the same body is
    // just as valid written inline in the header as void onUpdate(){...}.
    const std::string markers[] = {"::" + callback + "(", "void" + callback + "("};
    for (const std::string& marker : markers) {
        size_t searchFrom = 0;
        while (true) {
            const size_t markerPos = compactContent.find(marker, searchFrom);
            if (markerPos == std::string::npos) {
                break;
            }

            size_t cursor = markerPos + marker.size();
            searchFrom = cursor;

            int parenDepth = 1;
            while (cursor < compactContent.size() && parenDepth > 0) {
                if (compactContent[cursor] == '(') {
                    ++parenDepth;
                } else if (compactContent[cursor] == ')') {
                    --parenDepth;
                }
                ++cursor;
            }

            // Step over trailing specifiers such as const/override/noexcept.
            while (cursor < compactContent.size() &&
                   isScriptIdentifierContinuation(
                       static_cast<unsigned char>(compactContent[cursor]))) {
                ++cursor;
            }

            // A declaration (void onUpdate();) has no body to inspect, and its
            // next brace belongs to some unrelated function.
            if (cursor >= compactContent.size() || compactContent[cursor] != '{') {
                continue;
            }

            const size_t bodyStart = cursor;
            int braceDepth = 0;
            for (; cursor < compactContent.size(); ++cursor) {
                if (compactContent[cursor] == '{') {
                    ++braceDepth;
                } else if (compactContent[cursor] == '}') {
                    --braceDepth;
                    if (braceDepth == 0) {
                        break;
                    }
                }
            }

            if (containsContinuousPhysicsMutation(
                    compactContent.substr(bodyStart, cursor - bodyStart + 1))) {
                return true;
            }
            searchFrom = cursor;
        }
    }
    return false;
}

std::string sceneTypeName(SceneType type) {
    switch (type) {
        case SceneType::SCENE_3D: return "3D";
        case SceneType::SCENE_2D: return "2D";
        case SceneType::SCENE_UI: return "UI";
    }
    return "Unknown";
}

bool parseVector3(const Json& value, Vector3& out) {
    if (!value.is_object()) return false;
    if (value.contains("x") && value["x"].is_number()) out.x = value["x"].get<float>();
    if (value.contains("y") && value["y"].is_number()) out.y = value["y"].get<float>();
    if (value.contains("z") && value["z"].is_number()) out.z = value["z"].get<float>();
    return true;
}

bool parseVector2(const Json& value, Vector2& out) {
    if (!value.is_object()) return false;
    if (value.contains("x") && value["x"].is_number()) out.x = value["x"].get<float>();
    if (value.contains("y") && value["y"].is_number()) out.y = value["y"].get<float>();
    return true;
}

bool parseVector4(const Json& value, Vector4& out) {
    if (!value.is_object()) return false;
    if (value.contains("x") && value["x"].is_number()) out.x = value["x"].get<float>();
    if (value.contains("y") && value["y"].is_number()) out.y = value["y"].get<float>();
    if (value.contains("z") && value["z"].is_number()) out.z = value["z"].get<float>();
    if (value.contains("w") && value["w"].is_number()) out.w = value["w"].get<float>();
    return true;
}

bool parseQuaternion(const Json& value, Quaternion& out) {
    if (!value.is_object()) return false;
    if (value.contains("w") && value["w"].is_number()) out.w = value["w"].get<float>();
    if (value.contains("x") && value["x"].is_number()) out.x = value["x"].get<float>();
    if (value.contains("y") && value["y"].is_number()) out.y = value["y"].get<float>();
    if (value.contains("z") && value["z"].is_number()) out.z = value["z"].get<float>();
    return true;
}

// Far from unit length it is more likely Euler angles than rounding
bool parseRotation(const Json& value, Quaternion& out) {
    if (!parseQuaternion(value, out) || std::fabs(out.norm() - 1.0f) > 0.2f) return false;
    out.normalize();
    return true;
}

// Strict ease-name parse: Stream::stringToEaseType maps unknown names to LINEAR,
// so accept LINEAR results only when the input really spells "linear".
bool parseEaseName(const std::string& name, EaseType& out) {
    out = Stream::stringToEaseType(name);
    if (out != EaseType::LINEAR) return true;

    std::string normalized;
    normalized.reserve(name.size());
    for (char ch : name) {
        normalized.push_back(ch == '-' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return normalized == "LINEAR";
}

// One {x, y, z} object per key, missing fields keep the fallback
bool parseKeyVectors(const Json& list, size_t keys, const Vector3& fallback, std::vector<Vector3>& values) {
    if (!list.is_array() || list.size() != keys) return false;
    for (const Json& item : list) {
        Vector3 value = fallback;
        if (!parseVector3(item, value)) return false;
        values.push_back(value);
    }
    return true;
}

// One quaternion or Euler angles (degrees) object per key
bool parseKeyRotations(const Json& list, size_t keys, bool euler, std::vector<Quaternion>& values) {
    if (!list.is_array() || list.size() != keys) return false;
    for (const Json& item : list) {
        Quaternion value;
        if (euler) {
            Vector3 angles;
            if (!parseVector3(item, angles)) return false;
            value = Quaternion(angles.x, angles.y, angles.z);
        } else if (!parseQuaternion(item, value) || value.norm() <= FLT_EPSILON) {
            return false;
        }
        values.push_back(value.normalized());
    }
    return true;
}

bool parseBodyType(const std::string& value, BodyType& out) {
    const std::string token = lower(value);
    if (token == "static") {
        out = BodyType::STATIC;
        return true;
    }
    if (token == "kinematic") {
        out = BodyType::KINEMATIC;
        return true;
    }
    if (token == "dynamic") {
        out = BodyType::DYNAMIC;
        return true;
    }
    return false;
}

bool parseBody3DMotionQuality(const std::string& value, Body3DMotionQuality& out) {
    const std::string token = lower(value);
    if (token == "discrete") {
        out = Body3DMotionQuality::DISCRETE;
        return true;
    }
    if (token == "linear_cast") {
        out = Body3DMotionQuality::LINEAR_CAST;
        return true;
    }
    return false;
}

bool parseBodyShape3DType(const std::string& value, Shape3DType& out) {
    const std::string token = lower(value);
    if (token == "box") {
        out = Shape3DType::BOX;
        return true;
    }
    if (token == "sphere") {
        out = Shape3DType::SPHERE;
        return true;
    }
    if (token == "capsule") {
        out = Shape3DType::CAPSULE;
        return true;
    }
    if (token == "tapered_capsule") {
        out = Shape3DType::TAPERED_CAPSULE;
        return true;
    }
    if (token == "cylinder") {
        out = Shape3DType::CYLINDER;
        return true;
    }
    if (token == "convex_hull") {
        out = Shape3DType::CONVEX_HULL;
        return true;
    }
    if (token == "mesh") {
        out = Shape3DType::MESH;
        return true;
    }
    return false;
}

// How PhysicsSystem scales a collision shape: boxes per axis, round shapes by one aggregate
// factor. Mirrored here so a fitted shape lands on the dimensions the body is actually built with.
Vector3 absScale(const Vector3& scale) {
    return Vector3(std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z));
}

float maxScaleXZ(const Vector3& scale) {
    return std::max(scale.x, scale.z);
}

float maxScaleXYZ(const Vector3& scale) {
    return std::max(scale.x, std::max(scale.y, scale.z));
}

// convex_hull and mesh derive their geometry from a mesh instead of from dimensions
bool isMeshDerivedShape3DType(Shape3DType type) {
    return type == Shape3DType::CONVEX_HULL || type == Shape3DType::MESH;
}

// A mesh with no geometry still has an AABB, it just has no size. A plane is legitimately flat on
// one axis, so only a fully degenerate box counts as nothing to measure.
bool hasMeasurableBounds(const AABB& aabb) {
    return !aabb.isNull() && !aabb.isInfinite() && aabb.getSize() != Vector3::ZERO;
}

// Jolt drops a convex shape whose smallest half extent is below its convex radius (0.05), which
// would leave flat geometry like a ground plane with no collider. Twice that, after scaling.
constexpr float kMinFittedScaledSize = 0.2f;

// Size a primitive shape to the mesh's measured local bounds, and centre it on them so
// off-origin geometry lines up. Sizes stay local because PhysicsSystem applies the scale.
bool fitShapeToMeshBounds(const AABB& aabb, const Vector3& entityScale, Shape3D& shape) {
    if (!hasMeasurableBounds(aabb)) {
        return false;
    }

    const Vector3 scale = absScale(entityScale);

    // a size measured in world units, expressed back in the local units the shape stores
    auto toLocal = [](float world, float axisScale) {
        return axisScale > 0.0f ? world / axisScale : world;
    };

    const Vector3 measured = aabb.getSize();
    const Vector3 size(std::max(measured.x, toLocal(kMinFittedScaledSize, scale.x)),
                       std::max(measured.y, toLocal(kMinFittedScaledSize, scale.y)),
                       std::max(measured.z, toLocal(kMinFittedScaledSize, scale.z)));
    const Vector3 half = size * 0.5f;

    // A box is scaled per axis, so its local half extents are already right. Round shapes take a
    // single aggregate factor instead, so their radius has to be measured in world units and
    // divided back through that same factor or a non-uniform scale blows it up.
    const Vector3 worldHalf = half * scale;
    const float radialWorld = std::max(worldHalf.x, worldHalf.z);

    switch (shape.type) {
        case Shape3DType::BOX:
            shape.width = size.x;
            shape.height = size.y;
            shape.depth = size.z;
            break;
        case Shape3DType::SPHERE:
            shape.radius = toLocal(std::max(worldHalf.x, std::max(worldHalf.y, worldHalf.z)),
                                   maxScaleXYZ(scale));
            break;
        case Shape3DType::CYLINDER:
            shape.radius = toLocal(radialWorld, maxScaleXZ(scale));
            shape.halfHeight = half.y;
            break;
        case Shape3DType::CAPSULE:
            // total height is 2 * (halfHeight + radius), so the caps come out of the measured
            // height; a mesh no taller than it is wide leaves none and Jolt makes it a sphere
            shape.radius = toLocal(radialWorld, maxScaleXZ(scale));
            shape.halfHeight = toLocal(std::max(0.0f, worldHalf.y - radialWorld), scale.y);
            break;
        case Shape3DType::TAPERED_CAPSULE:
            shape.topRadius = toLocal(radialWorld, maxScaleXZ(scale));
            shape.bottomRadius = shape.topRadius;
            shape.halfHeight = toLocal(std::max(0.0f, worldHalf.y - radialWorld), scale.y);
            break;
        default:
            return false;
    }

    shape.position = aabb.getCenter();
    return true;
}

bool readPositiveFloat(const Json& arguments, const char* field, float& target, std::string& error) {
    if (!arguments.contains(field)) return true;
    if (!arguments[field].is_number()) {
        error = std::string(field) + " must be a number.";
        return false;
    }
    const float value = arguments[field].get<float>();
    if (!(value > 0.0f)) {
        error = std::string(field) + " must be greater than zero.";
        return false;
    }
    target = value;
    return true;
}

bool readNonNegativeFloat(const Json& arguments, const char* field, float& target, std::string& error) {
    if (!arguments.contains(field)) return true;
    if (!arguments[field].is_number()) {
        error = std::string(field) + " must be a number.";
        return false;
    }
    const float value = arguments[field].get<float>();
    if (value < 0.0f) {
        error = std::string(field) + " cannot be negative.";
        return false;
    }
    target = value;
    return true;
}

bool parseShape2DType(const std::string& value, Shape2DType& out) {
    const std::string token = lower(value);
    if (token == "box" || token == "polygon") {
        out = Shape2DType::POLYGON;
        return true;
    }
    if (token == "circle") {
        out = Shape2DType::CIRCLE;
        return true;
    }
    if (token == "capsule") {
        out = Shape2DType::CAPSULE;
        return true;
    }
    if (token == "segment") {
        out = Shape2DType::SEGMENT;
        return true;
    }
    if (token == "chain") {
        out = Shape2DType::CHAIN;
        return true;
    }
    return false;
}

bool parseVector2Array(const Json& value, HybridArray<Vector2, MAX_SHAPE_POINTS_2D>& out,
                       uint8_t& count, std::string& error) {
    if (!value.is_array()) {
        error = "points must be an array of Vector2 objects.";
        return false;
    }
    if (value.size() > UINT8_MAX) {
        error = "points exceeds the maximum number of 2D shape points.";
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        Vector2 point = Vector2::ZERO;
        if (!parseVector2(value[i], point)) {
            error = "Each point must be a Vector2 object.";
            return false;
        }
        out[i] = point;
    }
    count = static_cast<uint8_t>(value.size());
    return true;
}

Json vector2Json(const Vector2& value) {
    return Json{{"x", value.x}, {"y", value.y}};
}

Json vector3Json(const Vector3& value) {
    return Json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

Json vector4Json(const Vector4& value) {
    return Json{{"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w}};
}

Json shaderUniformValuesJson(const ShaderUniformValues& values) {
    Json json = Json::object();
    for (const auto& uniform : values) json[uniform.first] = vector4Json(uniform.second);
    return json;
}

Json quaternionJson(const Quaternion& value) {
    return Json{{"w", value.w}, {"x", value.x}, {"y", value.y}, {"z", value.z}};
}

Json loadingSettingsJson(const LoadingSettings& loading) {
    return Json{{"scene_id", loading.sceneId}, {"delay", loading.delay}, {"timeout", loading.timeout}, {"async_loading", loading.asyncLoading}};
}

// Measured local bounds of an entity's mesh. Colliders are authored in these same local units;
// world_size is reported too so it is clear the engine applies the scale rather than the caller.
void addMeshBounds(Json& data, Scene* scene, Entity entity) {
    MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
    if (!mesh || !hasMeasurableBounds(mesh->aabb)) {
        return;
    }

    Transform* transform = scene->findComponent<Transform>(entity);
    const Vector3 localSize = mesh->aabb.getSize();
    const Vector3 scale = transform ? transform->worldScale : Vector3::UNIT_SCALE;

    data["mesh_bounds"] = Json{
        {"local_size", vector3Json(localSize)},
        {"local_center", vector3Json(mesh->aabb.getCenter())},
        {"world_scale", vector3Json(scale)},
        {"world_size", vector3Json(localSize * absScale(scale))}
    };
}

uint32_t resolveSceneId(Project* project, const Json& args) {
    if (args.contains("scene_id") && args["scene_id"].is_number_unsigned()) {
        return args["scene_id"].get<uint32_t>();
    }
    if (args.contains("scene_id") && args["scene_id"].is_number_integer()) {
        int value = args["scene_id"].get<int>();
        if (value >= 0) return static_cast<uint32_t>(value);
    }
    return project ? project->getSelectedSceneId() : NULL_PROJECT_SCENE;
}

Entity resolveEntity(SceneProject* sceneProject, const Json& args) {
    if (!sceneProject || !sceneProject->scene) return NULL_ENTITY;
    // Scripts can destroy entities during Play
    Scene* scene = sceneProject->scene;
    if (args.contains("entity_id") && args["entity_id"].is_number_unsigned()) {
        Entity entity = args["entity_id"].get<Entity>();
        for (Entity existing : sceneProject->entities) {
            if (existing == entity && scene->isEntityCreated(entity)) return entity;
        }
    }
    if (args.contains("entity_id") && args["entity_id"].is_number_integer()) {
        int64_t raw = args["entity_id"].get<int64_t>();
        if (raw >= 0) {
            Entity entity = static_cast<Entity>(raw);
            for (Entity existing : sceneProject->entities) {
                if (existing == entity && scene->isEntityCreated(entity)) return entity;
            }
        }
    }
    if (args.contains("entity_name") && args["entity_name"].is_string()) {
        const std::string name = args["entity_name"].get<std::string>();
        for (Entity entity : sceneProject->entities) {
            if (scene->isEntityCreated(entity) && scene->getEntityName(entity) == name) {
                return entity;
            }
        }
    }
    return NULL_ENTITY;
}

Entity resolveEntityByKeys(SceneProject* sceneProject, const Json& args,
                           const char* idKey, const char* nameKey) {
    if (!sceneProject || !sceneProject->scene) return NULL_ENTITY;
    if (args.contains(idKey)) {
        Json tmp = Json::object();
        tmp["entity_id"] = args[idKey];
        return resolveEntity(sceneProject, tmp);
    }
    if (args.contains(nameKey) && args[nameKey].is_string()) {
        Json tmp = Json::object();
        tmp["entity_name"] = args[nameKey];
        return resolveEntity(sceneProject, tmp);
    }
    return NULL_ENTITY;
}

bool parseComponentType(const std::string& name, ComponentType& out) {
    try {
        out = Catalog::getComponentType(name);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseEntityType(const std::string& typeName, EntityCreationType& type) {
    static const std::unordered_map<std::string, EntityCreationType> map = {
        {"empty", EntityCreationType::EMPTY},
        {"object", EntityCreationType::OBJECT},
        {"box", EntityCreationType::BOX},
        {"plane", EntityCreationType::PLANE},
        {"wall", EntityCreationType::WALL},
        {"mirror", EntityCreationType::MIRROR},
        {"sphere", EntityCreationType::SPHERE},
        {"cylinder", EntityCreationType::CYLINDER},
        {"capsule", EntityCreationType::CAPSULE},
        {"torus", EntityCreationType::TORUS},
        {"image", EntityCreationType::IMAGE},
        {"sprite", EntityCreationType::SPRITE},
        {"tilemap", EntityCreationType::TILEMAP},
        {"text", EntityCreationType::TEXT},
        {"button", EntityCreationType::BUTTON},
        {"scrollbar", EntityCreationType::SCROLLBAR},
        {"progressbar", EntityCreationType::PROGRESSBAR},
        {"textedit", EntityCreationType::TEXTEDIT},
        {"panel", EntityCreationType::PANEL},
        {"polygon", EntityCreationType::POLYGON},
        {"container", EntityCreationType::CONTAINER},
        {"camera", EntityCreationType::CAMERA},
        {"point_light", EntityCreationType::POINT_LIGHT},
        {"directional_light", EntityCreationType::DIRECTIONAL_LIGHT},
        {"spot_light", EntityCreationType::SPOT_LIGHT},
        {"light_2d", EntityCreationType::POINT_LIGHT_2D},
        {"occluder_2d", EntityCreationType::OCCLUDER_2D},
        {"joint2d", EntityCreationType::JOINT2D},
        {"joint3d", EntityCreationType::JOINT3D},
        {"body2d", EntityCreationType::BODY2D},
        {"body3d", EntityCreationType::BODY3D},
        {"sky", EntityCreationType::SKY},
        {"fog", EntityCreationType::FOG},
        {"sound", EntityCreationType::SOUND},
        {"sound_3d", EntityCreationType::SOUND_3D},
        {"animation", EntityCreationType::ANIMATION},
        {"sprite_animation", EntityCreationType::SPRITE_ANIMATION},
        {"position_action", EntityCreationType::POSITION_ACTION},
        {"rotation_action", EntityCreationType::ROTATION_ACTION},
        {"scale_action", EntityCreationType::SCALE_ACTION},
        {"color_action", EntityCreationType::COLOR_ACTION},
        {"alpha_action", EntityCreationType::ALPHA_ACTION},
        {"translate_tracks", EntityCreationType::TRANSLATE_TRACKS},
        {"rotate_tracks", EntityCreationType::ROTATE_TRACKS},
        {"scale_tracks", EntityCreationType::SCALE_TRACKS},
        {"model", EntityCreationType::MODEL},
        {"particles", EntityCreationType::PARTICLES},
        {"points", EntityCreationType::POINTS},
        {"lines", EntityCreationType::LINES},
        {"mesh_polygon", EntityCreationType::MESH_POLYGON},
        {"terrain", EntityCreationType::TERRAIN},
        {"reflection_probe", EntityCreationType::REFLECTION_PROBE},
        {"water", EntityCreationType::WATER},
        {"water_exclusion", EntityCreationType::WATER_EXCLUSION}
    };
    auto it = map.find(lower(typeName));
    if (it == map.end()) return false;
    type = it->second;
    return true;
}

bool hasComponent(EntityRegistry* registry, Entity entity, ComponentType component) {
    if (!registry || entity == NULL_ENTITY) return false;
    std::vector<ComponentType> components = Catalog::findComponents(registry, entity);
    return std::find(components.begin(), components.end(), component) != components.end();
}

// Maps a renderable component to its forkable built-in shader type.
bool shaderTypeForComponent(ComponentType component, ShaderType& out) {
    switch (component) {
        case ComponentType::MeshComponent:   out = ShaderType::MESH;   return true;
        case ComponentType::UIComponent:     out = ShaderType::UI;     return true;
        case ComponentType::PointsComponent: out = ShaderType::POINTS; return true;
        case ComponentType::LinesComponent:  out = ShaderType::LINES;  return true;
        case ComponentType::SkyComponent:    out = ShaderType::SKYBOX; return true;
        case ComponentType::WaterComponent:  out = ShaderType::WATER;  return true;
        default: return false;
    }
}

// Parses the "mesh"/"ui"/"sky"/"points"/"lines"/"water" shader_type argument used to fork or
// address a scene's default shader (as opposed to a specific entity's component).
bool parseShaderTypeName(const std::string& name, ShaderType& out) {
    static const std::unordered_map<std::string, ShaderType> map = {
        {"mesh", ShaderType::MESH}, {"ui", ShaderType::UI}, {"sky", ShaderType::SKYBOX},
        {"points", ShaderType::POINTS}, {"lines", ShaderType::LINES}, {"water", ShaderType::WATER}
    };
    auto it = map.find(lower(name));
    if (it == map.end()) return false;
    out = it->second;
    return true;
}

// The Catalog/ScenePropertyCmd property name that stores a scene's default shader path
// for a given type (see editor::Catalog::get/setSceneProperty).
const char* scenePropertyNameForShaderType(ShaderType type) {
    switch (type) {
        case ShaderType::MESH:   return "default_mesh_shader";
        case ShaderType::UI:     return "default_ui_shader";
        case ShaderType::SKYBOX: return "default_sky_shader";
        case ShaderType::POINTS: return "default_points_shader";
        case ShaderType::LINES:  return "default_lines_shader";
        case ShaderType::WATER:  return "default_water_shader";
        default: return "";
    }
}

// Picks the entity's first renderable component that supports custom shaders.
bool detectForkableComponent(EntityRegistry* registry, Entity entity, ComponentType& component, ShaderType& shaderType) {
    for (ComponentType candidate : {ComponentType::MeshComponent, ComponentType::UIComponent,
                                    ComponentType::PointsComponent, ComponentType::LinesComponent,
                                    ComponentType::SkyComponent, ComponentType::WaterComponent}) {
        if (hasComponent(registry, entity, candidate)) {
            component = candidate;
            shaderTypeForComponent(candidate, shaderType);
            return true;
        }
    }
    return false;
}

// The shader-capable component addressed by entity_id/entity_name, explicit or auto-detected.
bool resolveShaderComponent(SceneProject* sceneProject, const Json& args, Entity& entity,
                            ComponentType& component, ShaderType& shaderType, std::string& error) {
    entity = resolveEntity(sceneProject, args);
    if (entity == NULL_ENTITY) { error = "Entity not found."; return false; }

    const std::string componentArg = args.value("component", "");
    if (!componentArg.empty()) {
        if (!parseComponentType(componentArg, component)) { error = "Unknown component type."; return false; }
        if (!shaderTypeForComponent(component, shaderType)) {
            error = "Component does not support custom shaders. Use Mesh/UI/Points/Lines/Sky/Water.";
            return false;
        }
        if (!hasComponent(sceneProject->scene, entity, component)) {
            error = "Entity has no " + componentArg + ".";
            return false;
        }
    } else if (!detectForkableComponent(sceneProject->scene, entity, component, shaderType)) {
        error = "Entity has no shader-capable component (Mesh/UI/Points/Lines/Sky/Water).";
        return false;
    }
    return true;
}

bool parseSceneType(const std::string& typeName, SceneType& type) {
    std::string token = lower(typeName);
    if (token == "3d" || token == "scene_3d") {
        type = SceneType::SCENE_3D;
        return true;
    }
    if (token == "2d" || token == "scene_2d") {
        type = SceneType::SCENE_2D;
        return true;
    }
    if (token == "ui" || token == "scene_ui") {
        type = SceneType::SCENE_UI;
        return true;
    }
    return false;
}

std::set<ShaderBackend> parseBackendList(const std::string& text, std::string& error) {
    const std::vector<ShaderBackend>& allBackends = ShaderPool::getShaderBackends();

    std::set<ShaderBackend> backends;
    std::stringstream ss(text.empty() ? "all" : text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](unsigned char c) {
            return !std::isspace(c);
        }));
        token.erase(std::find_if(token.rbegin(), token.rend(), [](unsigned char c) {
            return !std::isspace(c);
        }).base(), token.end());
        if (token.empty()) continue;
        if (lower(token) == "all") {
            backends.insert(allBackends.begin(), allBackends.end());
            continue;
        }
        ShaderBackend backend;
        if (!ShaderPool::parseShaderBackend(token, backend)) {
            error = "Unknown graphic backend: " + token;
            return {};
        }
        backends.insert(backend);
    }
    if (backends.empty()) {
        backends.insert(allBackends.begin(), allBackends.end());
    }
    return backends;
}

bool parseShaderOutputFormat(const std::string& value, ShaderOutputFormat& out) {
    const std::string token = lower(value.empty() ? "header" : value);
    if (token == "binary" || token == "sdat") {
        out = ShaderOutputFormat::Binary;
        return true;
    }
    if (token == "header") {
        out = ShaderOutputFormat::Header;
        return true;
    }
    if (token == "json") {
        out = ShaderOutputFormat::Json;
        return true;
    }
    return false;
}

bool parseScriptType(const std::string& value, ScriptType& out) {
    const std::string token = lower(value);
    if (token == "lua" || token == "script_lua") {
        out = ScriptType::LUA;
        return true;
    }
    if (token == "cpp" || token == "cpp_subclass" || token == "subclass" ||
            token == "cpp_script_class" || token == "script_class") {
        out = ScriptType::CPP;
        return true;
    }
    return false;
}

bool parseScriptCreationType(const std::string& value, ScriptType& out, bool& cppSubclass) {
    const std::string token = lower(value);
    if (token == "lua" || token == "script_lua") {
        out = ScriptType::LUA;
        cppSubclass = false;
        return true;
    }
    if (token == "cpp_subclass" || token == "subclass") {
        out = ScriptType::CPP;
        cppSubclass = true;
        return true;
    }
    if (token == "cpp_script_class" || token == "script_class") {
        out = ScriptType::CPP;
        cppSubclass = false;
        return true;
    }
    return false;
}

std::string scriptTypeExtension(ScriptType type, bool header) {
    if (type == ScriptType::LUA) return ".lua";
    return header ? ".h" : ".cpp";
}

std::string scriptTypeName(ScriptType type) {
    switch (type) {
        case ScriptType::LUA: return "lua";
        case ScriptType::CPP: return "cpp";
    }
    return "unknown";
}

std::string sanitizeIdentifier(const std::string& value, const std::string& fallback) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            result.push_back(c);
        }
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front()))) {
        result = fallback;
    }
    return result;
}

std::string doriaxLuaScriptGuide(const std::string& className) {
    return "Doriax Lua scripts are returned tables, not Dori.Script objects. "
           "Use: local " + className + " = { properties = {} }; "
           "function " + className + ":init() ... end; return " + className + ". "
           "Editor-exposed properties go in the properties array as { name = \"speed\", displayName = \"Speed\", type = \"float\", default = 5.0 } "
           "(type strings: bool, int, float, string, vector2, vector3, vector4, color3, color4, entity, or a wrapper name such as Text, Image, Object or Animation for an entity of that type); at runtime read/write them as self.<name>, e.g. self.speed. "
           "A script drives entities already placed in the scene and does not build them: expose each one as an entity property pointed at it with set_component_property on ScriptComponent (scripts[i].<name>, entity_value), place what appears later with Transform visible off on it and on every child placed under it, since the saved flag does not cascade, and show it with obj.visible = true, which does reach the children. Spawn at runtime only what cannot be placed in advance, as a bundle with BundleManager.createBundle. "
           "Hiding leaves an entity's physics body active, so suspend it in the first onFixedUpdate, which runs before the first physics step (bodies do not exist yet during init): turn a Body2D off with body.enabled = false; for a Body3D, keep its body.type and body.sensor, then set body.sensor = true and, if it is dynamic, body.type = BodyType.KINEMATIC and body.linearVelocity = Vector3(0, 0, 0) so gravity cannot move it. On reveal turn the Body2D back on or restore the Body3D's type and sensor flag, and ignore the entity's contacts while it is hidden. "
           "Authored motion (spins, bobs, doors, fades, UI transitions, sprite frames) is a native animation the script only plays, e.g. a property of type Animation with self.anim:start() or self.anim:restart(), or model:playAnimation(name, fadeTime) for a model's clips; never write per-frame tween code for it. "
           "The engine injects self.scene and self.entity; self.entity is a numeric Entity id, not an object receiver. For an existing cube/shape, "
           "use local shape = Shape(self.scene, self.entity); shape:setColor(1.0, 0.0, 0.0, 1.0). "
           "There is no Transform (or other component) global in Lua: to read or move an entity, wrap it with local obj = Object(self.scene, self.entity) "
           "then use obj.position / obj:setPosition(...) and similar (check the Object binding via read_engine_source for exact members). "
           "For keyboard/mouse input use the Input class (Input.isKeyPressed(Input.KEY_LEFT), etc.). Scale continuous direct non-physics movement by Engine.deltatime, but never multiply physics forces or torques by delta time; "
           "gamepads use Input.isGamepadButtonPressed(gamepad, Input.GAMEPAD_BUTTON_A) and Input.getGamepadAxis(gamepad, Input.GAMEPAD_AXIS_LEFT_X); gamepad ids are sparse, so iterate connected pads by dense index with local id = Input.getGamepadId(i) for i in 0..Input.numGamepads()-1 rather than assuming ids are 0..n-1. "
           "Choose the update callback by timing domain. Variable-frame UI, camera, and direct non-physics movement use RegisterEngineEvent(self, \"onUpdate\") and " + className + ":onUpdate(). Continuously applied body forces or torques MUST use RegisterEngineEvent(self, \"onFixedUpdate\") and " + className + ":onFixedUpdate(), because each physics world accumulates force calls until its next fixed step, so putting them in onUpdate makes effective force depend on render FPS; velocity control and physics-driving input polling belong there too. "
           "Inside onFixedUpdate a physics body must be moved through the body itself, never by writing the entity transform: the body is synced from the transform before the step and written back after it, so such a write is silently discarded. The bindings expose these as properties, not methods: body.position and body.rotation on Body3D, body.position and body.angle on Body2D. Entities with no body have no such restriction. "
           "Bodies are drawn interpolated between fixed steps, so a camera or anything else following a body reads obj:getWorldPosition(), not body.position, which moves once per step. "
           "The force calls differ by body type and are not interchangeable: Body3D uses body:applyForce(force) for a centre force, body:applyForce(force, point), and body:applyTorque(torque), and has NO applyForceToCenter; Body2D uses body:applyForceToCenter(force, wake), body:applyForce(force, point, wake) with no single-argument form, and body:applyTorque(torque, wake). "
           "A dynamic body's mass comes from its shape and density, not from an authored number, and 3D and 2D differ: 3D mass is volume * density with default density 1000 (kg/m3), so a radius-1 sphere is about 4189 kg and needs roughly 4189 N per 1 m/s2, while 2D mass is area * density with default density 1 (2D sizes are points scaled by pointsToMeterScale2D, 64 by default). Compute forces as mass * desired acceleration instead of guessing a magnitude, reading the mass as the Lua PROPERTY body.mass on Body3D (Body2D uses the method body:getMass() instead), and if the motion needs an implausible force, lower the shape density or shrink the shape rather than inflating the force. "
           "valid event names: onUpdate, onFixedUpdate, onDraw, onMouseDown, onMouseUp, onMouseMove, onKeyDown, onKeyUp, onTouchStart, onTouchMove, onTouchEnd, "
           "onGamepadConnect, onGamepadDisconnect, onGamepadButtonDown, onGamepadButtonUp, onGamepadAxisMove. "
           "For component/UI events use the global RegisterEvent(self, eventObject, \"method\"), getting the event object from a wrapper, e.g. local b = Button(self.scene, self.entity); RegisterEvent(self, b:getButtonComponent().onPress, \"onPress\"). "
           "Engine state uses Lua properties, e.g. local dt = Engine.deltatime (not getDeltatime). No manual unregister is needed; the engine cleans up events when the script is destroyed. "
           "Angles passed to engine APIs (Quaternion(angle, axis), rotateView, ...) are in the engine's default unit, DEGREES unless Engine.setUseDegrees(false) was called, so do not pass radians; math.sin/math.cos still need radians, so wrap with math.rad. "
           "A camera orients by its target, not its rotation: useTarget defaults to true and both cam:setTarget(x, y, z) and the cam.target property re-enable it, so while it is on the view is lookAt(position, target, up) and setting the camera entity's rotation does nothing (call cam:disableTarget() first to drive the view from the rotation, where forward = rotation * Vector3(0, 0, -1)). "
           "For a third-person/orbit camera, place it with spherical coordinates around the target: local p, y = math.rad(pitch), math.rad(yaw); local offset = Vector3(math.cos(p)*math.sin(y), math.sin(p), math.cos(p)*math.cos(y)); cam:setPosition(target + offset * distance); cam.target = target -- a positive pitch then puts the camera ABOVE the target. Never compute the position as target - forward*distance from a rotated forward vector; rotations are right-handed, so a positive pitch tilts the VIEW up and that formula puts the camera below the target. "
           "To print or log, use Log.print(\"text \" .. tostring(value)) (Log.warn/Log.error for severity), not bare print(). "
           "Use ONLY APIs that actually exist in the Doriax engine source: search_engine_api is a quick index, and search_engine_source/read_engine_source read the real Lua bindings "
           "(e.g. core/script/binding/CoreClassesLua.cpp, ObjectClassesLua.cpp). Confirm exact names there before writing them; if a symbol is not in that source it does not exist, so never invent APIs or borrow them from other engines.";
}

std::string doriaxCppScriptGuide(bool cppSubclass, const std::string& parentClass) {
    std::ostringstream guide;
    guide << "Doriax C++ scripts compile against flat engine API headers copied to .doriax/engine-api. "
          << "Use quoted includes such as \"Mesh.h\", \"ScriptBase.h\", \"Engine.h\", and \"ScriptProperty.h\". "
          << "Never use #include <core/...> or #include \"core/...\". "
          << "A Doriax script class needs no class-declaration or reflection macro (no D_OBJECT/GDCLASS/GENERATED_BODY/Q_OBJECT): just inherit the base class and declare a (doriax::Scene*, doriax::Entity) constructor. Prefer editing the class skeleton create_script already generated rather than rewriting it from scratch. ";
    if (!cppSubclass) {
        guide << "cpp_script_class inherits doriax::ScriptBase for general entity logic. "
              << "Use REGISTER_ENGINE_EVENT(onUpdate) and void onUpdate() for variable-frame logic, or "
              << "REGISTER_ENGINE_EVENT(onFixedUpdate) and void onFixedUpdate() for physics-step logic; "
              << "pair the chosen event with the matching UNREGISTER_ENGINE_EVENT in the destructor. "
              << "ScriptBase exposes getScene()/getEntity(); construct a runtime wrapper such as Object(getScene(), getEntity()) to control the entity.";
    } else {
        guide << "cpp_subclass inherits doriax::" << parentClass
              << " and calls engine methods (e.g. setColor) directly on this. "
              << "Register variable-frame callbacks with REGISTER_ENGINE_EVENT(onUpdate), or physics-step callbacks with REGISTER_ENGINE_EVENT(onFixedUpdate), and unregister the matching event in the destructor. "
              << "For mesh/cube color on a Mesh entity, prefer cpp_subclass extending Mesh (or Shape), not ScriptBase.";
    }
    guide << " Continuous Body2D/Body3D forces or torques MUST run in onFixedUpdate, since each physics world accumulates force calls until its next fixed step and applying them in onUpdate is render-frame-rate-dependent; velocity control and physics-driving input polling belong there too. Do not multiply forces or torques by Engine::getDeltatime(); the physics engine integrates them over the fixed step. "
          << "Bodies are drawn interpolated between fixed steps, so a camera or anything else following a body reads Object::getWorldPosition(), not Body3D::getPosition(), which moves once per step. "
          << "A dynamic body's mass comes from its shape and density, not from an authored number, and 3D and 2D differ: 3D mass is volume * density with default density 1000 (kg/m3), so a radius-1 sphere is about 4189 kg and needs roughly 4189 N per 1 m/s2, while 2D mass is area * density with default density 1 (2D sizes are points scaled by pointsToMeterScale2D, 64 by default). Compute forces as mass * desired acceleration using getMass() instead of guessing a magnitude, and if the motion needs an implausible force, lower the shape density or shrink the shape rather than inflating the force (Body3D::setMass writes only the live body and is lost on reload). "
          << "Use ONLY C++ symbols (types, methods, macros, enums, overloads) that exist in the Doriax engine source; if it is not in the source it does not exist, so never invent it or borrow it from another engine. For exact signatures, overloads, enum values, and macros, grep the real headers with search_engine_source and read them with read_engine_source (e.g. core/Input.h, core/math/Quaternion.h) instead of guessing from search_engine_api, which uses Lua-style names and omits C++ overloads. Watch the common trap: keyboard key codes in C++ are D_KEY_* macros from \"Input.h\" (Input::KEY_* is Lua-only), so look up the exact macro and any constructor overloads (e.g. Quaternion) in source before using them. "
          << "Verified entity-control recipe: on an Object wrapper (Object obj(getScene(), getEntity()), or directly on this in a cpp_subclass) read obj.getPosition()/obj.getRotation() and write obj.setPosition(x, y, z)/obj.setRotation(q). Object has NO getForward()/getDirection(); get a facing vector by rotating a basis vector with the rotation, e.g. Vector3 forward = obj.getRotation() * Vector3(0, 0, -1) (Quaternion also has xAxis()/yAxis()/zAxis()). Build an axis-angle rotation with the ANGLE FIRST: Quaternion(float angle, const Vector3& axis), e.g. Quaternion(speed * Engine::getDeltatime(), Vector3(0, 1, 0)) -- never Quaternion(axis, angle). "
          << "Angle arguments are in the engine's default unit, DEGREES unless Engine::setUseDegrees(false) was called: Quaternion(angle, axis), Quaternion::fromEulerAngles, Camera::rotateView and friends all run their angle through Angle::defaultToRad, so do not pass radians to them. Include \"Angle.h\" and convert with Angle::degToDefault/Angle::radToDefault; std::sin/std::cos always need radians, so feed them Angle::defaultToRad(angle). "
          << "Camera orientation comes from its target, not its rotation: CameraComponent.useTarget defaults to true and Camera::setTarget re-enables it, so while it is on the view matrix is lookAt(position, target, up) and calling setRotation() on the camera entity has no effect on the view (call cam.disableTarget() first to drive the view from the rotation, where forward = rotation * Vector3(0, 0, -1)). For a third-person/orbit camera, place the camera with spherical coordinates around the target: float p = Angle::defaultToRad(pitch), y = Angle::defaultToRad(yaw); Vector3 offset(cosf(p)*sinf(y), sinf(p), cosf(p)*cosf(y)); cam.setPosition(target + offset*distance); cam.setTarget(target); -- a positive pitch then places the camera ABOVE the target, and the horizontal movement basis is forward = Vector3(-sinf(y), 0, -cosf(y)), right = Vector3(cosf(y), 0, -sinf(y)). Never derive the camera position as target - forward*distance from a rotated forward vector: rotations are right-handed, so a positive pitch tilts the VIEW up and that formula puts the camera below the target, forcing a bogus negative pitch to compensate. "
          << "To print or log, include \"Log.h\" and call Log::print(\"pos %f %f %f\", p.x, p.y, p.z) (Log::warn/Log::error for severity); Engine has no log method, and do not use printf or std::cout. "
          << "For component/UI events (button press, click, scrollbar change, etc.) subscribe in the constructor with REGISTER_COMPONENT_EVENT(Component, event, method) or its shortcuts REGISTER_UI_EVENT/REGISTER_BUTTON_EVENT/REGISTER_SCROLLBAR_EVENT/REGISTER_PANEL_EVENT, and pair each with its UNREGISTER_* in the destructor; search_engine_api for the exact macro and the component's event names. "
          << "Use search_engine_api for a quick symbol overview, but confirm exact signatures against the real source (search_engine_source/read_engine_source) before writing engine API code. "
          << "DPROPERTY(\"Name\") exposes a public member as an editor property (optional; removing it clears stale editor properties); DPROPERTY(\"Name\", Type) forces the editor type such as Color3/Color4. "
          << "A script drives entities already placed in the scene and does not build them: expose each one as a DPROPERTY pointer member (doriax::Object*, doriax::Text*, doriax::Animation*, ...) pointed at it with set_component_property on ScriptComponent (scripts[i].<name>, entity_value), place what appears later with Transform visible off on it and on every child placed under it, since the saved flag does not cascade, and show it with setVisible(true), which does reach the children. Spawn at runtime only what cannot be placed in advance, as a bundle with BundleManager::createBundle. "
          << "Hiding leaves an entity's physics body active, so suspend it in the first onFixedUpdate, which runs before the first physics step (bodies do not exist yet in the constructor): turn a Body2D off with setEnabled(false); for a Body3D, keep its getType() and isSensor(), then setIsSensor(true) and, if it is dynamic, setType(BodyType::KINEMATIC) and setLinearVelocity(Vector3(0, 0, 0)) so gravity cannot move it. On reveal turn the Body2D back on or restore the Body3D's type and sensor flag, and ignore the entity's contacts while it is hidden. "
          << "Authored motion (spins, bobs, doors, fades, UI transitions, sprite frames) is a native animation the script only plays, e.g. a doriax::Animation* property with start() or restart(), or Model::playAnimation(name, fadeTime) for a model's clips; never write per-frame tween code for it. "
          << "After writing the .h and .cpp, verify the script actually works: start the scene with control_play_mode (action=start), then read_output_log to check for a C++ build failure (compiler errors) or a runtime 'Script crash'. If it is still building, read_output_log again. If there are errors, stop play, fix with update_script_file, and verify again -- repeat until the log is clean, then stop play, before telling the user the script is ready.";
    return guide.str();
}

std::string validateDoriaxLuaScriptContent(const std::string& content) {
    const std::string contentLower = lower(content);

    struct ForbiddenLuaPattern {
        const char* token;
        const char* message;
    };
    static const ForbiddenLuaPattern patterns[] = {
        {"dori.", "Doriax Lua scripts do not use a Dori namespace."},
        {"script:new", "Doriax Lua scripts do not derive from Script:new(). Return a plain module table instead."},
        {"on_start", "Doriax Lua scripts use init() for startup, not on_start()."},
        {"on_scene_start", "Doriax Lua scripts use init() for startup, not on_scene_start()."},
        {"self.entity:", "self.entity is a numeric Entity id, not an object receiver. Wrap it with a runtime class such as Shape(self.scene, self.entity)."},
        {"self.entity.", "self.entity is a numeric Entity id, not an object. Pass it to runtime wrappers; do not access methods/properties on it."},
        {"get_entity", "Doriax Lua scripts receive self.entity directly; there is no get_entity() helper."},
        {"getentity", "Doriax Lua scripts receive self.entity directly; there is no getEntity() helper."},
        {"get_component", "Doriax Lua scripts should use runtime wrappers returned by search_engine_api, not get_component()."},
        {"getcomponent", "Doriax Lua scripts should use runtime wrappers returned by search_engine_api, not getComponent()."},
        {"transform(self", "There is no Transform global in Doriax Lua; components are not constructed like Transform(self.scene, self.entity). To read or move an entity use local obj = Object(self.scene, self.entity); read obj.position and write obj:setPosition(x, y, z)."},
        {"set_property", "Doriax Lua scripts should call runtime APIs, not editor-style set_property()."},
        {"setproperty", "Doriax Lua scripts should call runtime APIs, not editor-style setProperty()."},
        {"vector4.new", "Doriax Lua constructors use Vector4(...), not Vector4.new(...)."},
        {"engine.log", "Engine has no log method. To print, use Log.print(\"text \" .. tostring(value)) (Log.warn/Log.error for severity)."},
        {"engine:log", "Engine has no log method. To print, use Log.print(\"text \" .. tostring(value)) (Log.warn/Log.error for severity)."}
    };

    for (const auto& pattern : patterns) {
        if (contentLower.find(pattern.token) != std::string::npos) {
            return std::string(pattern.message) + " Call search_engine_api and rewrite using the Doriax Lua table pattern.";
        }
    }

    if (contentLower.find("return ") == std::string::npos) {
        return "Doriax Lua modules must return their script table.";
    }

    if (luaCallbackContainsContinuousPhysicsMutation(content, "onupdate")) {
        return "Continuous Body2D/Body3D forces and torques must run in onFixedUpdate, not onUpdate. "
               "Each physics world accumulates force calls until its next fixed step, so onUpdate makes the effective "
               "force depend on render FPS. RegisterEngineEvent(self, \"onFixedUpdate\"), move the physics code "
               "to the matching :onFixedUpdate() callback, and do not multiply force or torque by Engine.deltatime.";
    }

    return {};
}

std::string validateDoriaxCppScriptContent(const std::string& content) {
    const std::string contentLower = lower(content);
    const std::string compactContent = compactCppForValidation(content);

    struct ForbiddenCppPattern {
        const char* token;
        const char* message;
    };
    static const ForbiddenCppPattern patterns[] = {
        {"<core/", "C++ scripts must use flat quoted headers such as \"Mesh.h\", not <core/...>."},
        {"\"core/", "C++ scripts must use flat quoted headers such as \"Mesh.h\", not \"core/...\"."},
        {"get_component<", "Doriax C++ scripts do not use get_component. Use cpp_subclass and inherit the runtime class (e.g. Mesh) instead."},
        {"oninit(", "Doriax C++ scripts use onUpdate() with REGISTER_ENGINE_EVENT, not onInit()."},
        {"void oninit", "Doriax C++ scripts use onUpdate() with REGISTER_ENGINE_EVENT, not onInit()."},
        {"on_start", "Doriax C++ scripts use onUpdate() with REGISTER_ENGINE_EVENT, not on_start()."},
        {"onstart(", "Doriax C++ scripts use onUpdate() with REGISTER_ENGINE_EVENT, not onStart()."},
        {"d_object(", "Doriax C++ script classes use no class-declaration or reflection macro. Delete D_OBJECT; just inherit the base class (e.g. doriax::ScriptBase) and declare a (doriax::Scene*, doriax::Entity) constructor. DPROPERTY only marks editor properties."},
        {"gdclass(", "Doriax is not Godot: there is no GDCLASS macro. Inherit the base class (e.g. doriax::ScriptBase) and declare a (doriax::Scene*, doriax::Entity) constructor; no class-registration macro exists."},
        {"generated_body(", "Doriax is not Unreal: there is no GENERATED_BODY macro. Inherit the base class (e.g. doriax::ScriptBase) and declare a (doriax::Scene*, doriax::Entity) constructor; no class-registration macro exists."},
        {"q_object", "Doriax script classes use no Q_OBJECT or any class-registration macro. Inherit the base class (e.g. doriax::ScriptBase) and declare a (doriax::Scene*, doriax::Entity) constructor."},
        {"engine::log", "Engine has no log method. Include \"Log.h\" and use Log::print(\"value %f\", x) (Log::warn/Log::error for severity)."}
    };

    for (const auto& pattern : patterns) {
        if (contentLower.find(pattern.token) != std::string::npos) {
            return std::string(pattern.message) + " Call search_engine_api and rewrite using the Doriax C++ script pattern.";
        }
    }

    if (cppCallbackContainsContinuousPhysicsMutation(compactContent, "onupdate")) {
        return "Continuous Body2D/Body3D forces and torques must run in onFixedUpdate, not onUpdate. "
               "Each physics world accumulates force calls until its next fixed step, so onUpdate makes the effective "
               "force depend on render FPS. Register and implement onFixedUpdate, unregister the matching event "
               "in the destructor, and do not multiply force or torque by Engine::getDeltatime().";
    }

    return {};
}

ActionResult okResult(const std::string& message, Json data = Json::object()) {
    return {true, message, std::move(data)};
}

ActionResult failResult(const std::string& message) {
    return {false, message, Json::object()};
}

ActionResult expectedMissResult(const std::string& message) {
    return {false, message, Json::object(), ActionFailureSeverity::ExpectedMiss};
}

ActionResult warningResult(const std::string& message) {
    return {false, message, Json::object(), ActionFailureSeverity::Warning};
}

std::string fileTypeFromPath(const fs::path& path) {
    const std::string value = FileUtils::pathToUtf8(path);
    if (Util::isModelFile(value)) return "model";
    if (Util::isImageFile(value)) return "image";
    if (Util::isMaterialFile(value)) return "material";
    if (Util::isSceneFile(value)) return "scene";
    if (Util::isScriptFile(value)) return "script";
    if (Util::isAudioFile(value)) return "audio";
    if (Util::isFontFile(value)) return "font";
    return "file";
}

std::string propertyTypeName(PropertyType type) {
    switch (type) {
        case PropertyType::Bool: return "bool";
        case PropertyType::String: return "string";
        case PropertyType::Float: return "float";
        case PropertyType::Double: return "double";
        case PropertyType::Vector2: return "vector2";
        case PropertyType::Vector3: return "vector3";
        case PropertyType::Vector4: return "vector4";
        case PropertyType::Quat: return "quaternion";
        case PropertyType::Int: return "int";
        case PropertyType::UInt: return "uint";
        case PropertyType::Material: return "material";
        case PropertyType::Texture: return "texture";
        case PropertyType::Font: return "font";
        case PropertyType::Enum: return "enum";
        case PropertyType::Ease: return "ease";
        case PropertyType::Custom: return "custom";
        case PropertyType::Entity: return "entity";
        case PropertyType::EntityReference: return "entity_reference";
    }
    return "unknown";
}

// Paths the AI sends and receives are project-relative, like list_resources and the
// file actions report them; asset references are stored relative to the assets root.
std::string assetPathFromAi(Project* project, const fs::path& projectRelative) {
    return project->normalizeToAssetsRelative(project->getProjectPath() / projectRelative).generic_string();
}

// Drag and drop refuses files outside the assets root: the exporter never copies them.
bool isInsideAssets(Project* project, const fs::path& projectRelative) {
    return project->isInsideAssetsPath(project->getProjectPath() / projectRelative);
}

std::string outsideAssetsError(Project* project, const std::string& argument) {
    return argument + " must be inside the assets directory (\"" +
           project->getAssetsDir().generic_string() + "\").";
}

std::string assetPathToAi(Project* project, const std::string& stored) {
    if (stored.empty()) return stored;
    return FileUtils::pathToGenericUtf8(project->normalizeToProjectRelative(project->resolveAssetPath(stored)));
}

// String properties holding an asset reference instead of plain text
bool isAssetPathProperty(const std::string& propertyName) {
    return propertyName == "filename";
}

Json propertyValueToJson(Project* project, const std::string& propertyName, const PropertyData& property) {
    if (!property.ref) return Json();
    switch (property.type) {
        case PropertyType::Bool:
            return *static_cast<bool*>(property.ref);
        case PropertyType::String: {
            const std::string& value = *static_cast<std::string*>(property.ref);
            return isAssetPathProperty(propertyName) ? assetPathToAi(project, value) : value;
        }
        case PropertyType::Float:
            return *static_cast<float*>(property.ref);
        case PropertyType::Double:
            return *static_cast<double*>(property.ref);
        case PropertyType::Vector2:
            return vector2Json(*static_cast<Vector2*>(property.ref));
        case PropertyType::Vector3:
            return vector3Json(*static_cast<Vector3*>(property.ref));
        case PropertyType::Vector4:
            return vector4Json(*static_cast<Vector4*>(property.ref));
        case PropertyType::Quat:
            return quaternionJson(*static_cast<Quaternion*>(property.ref));
        case PropertyType::Int:
        case PropertyType::Enum:
            return *static_cast<int*>(property.ref);
        case PropertyType::Ease:
            return Stream::easeTypeToString(static_cast<Ease*>(property.ref)->getType());
        case PropertyType::UInt:
            return *static_cast<unsigned int*>(property.ref);
        case PropertyType::Font: {
            // the chain in use: the main font first, then its fallbacks
            const FontArray& fonts = *static_cast<FontArray*>(property.ref);
            size_t numFonts = 0;
            for (size_t i = 0; i < fonts.size(); i++) {
                if (!fonts[i].empty()) numFonts = i + 1;
            }
            Json list = Json::array();
            for (size_t i = 0; i < numFonts; i++) {
                list.push_back(assetPathToAi(project, fonts[i]));
            }
            return list;
        }
        case PropertyType::Texture: {
            // Report scaled SVG sources in the round-trippable "<path>?svgScale=N" form:
            // Texture(path) absorbs the suffix, so the model can echo it back into
            // texture_path (and tweak the scale) without a dedicated field.
            const Texture* tex = static_cast<Texture*>(property.ref);
            return TextureData::buildSvgScalePath(assetPathToAi(project, tex->getPath()), tex->getSvgScale());
        }
        case PropertyType::Entity:
            return *static_cast<Entity*>(property.ref);
        case PropertyType::EntityReference: {
            const EntityReference& ref = *static_cast<EntityReference*>(property.ref);
            return Json{{"entity", ref.entity}, {"scene_id", ref.sceneId}};
        }
        case PropertyType::Material: {
            const Material& material = *static_cast<Material*>(property.ref);
            return Json{{"name", material.name},
                        {"baseColorFactor", vector4Json(material.baseColorFactor)},
                        {"metallicFactor", material.metallicFactor},
                        {"roughnessFactor", material.roughnessFactor},
                        {"emissiveFactor", vector3Json(material.emissiveFactor)},
                        {"baseColorTexture", TextureData::buildSvgScalePath(
                            assetPathToAi(project, material.baseColorTexture.getPath()),
                            material.baseColorTexture.getSvgScale())}};
        }
        case PropertyType::Custom:
            if (propertyName == "shaderUniforms") {
                return shaderUniformValuesJson(*static_cast<ShaderUniformValues*>(property.ref));
            }
            if (propertyName == "bursts") {
                Json bursts = Json::array();
                for (const ParticleBurst& burst : *static_cast<std::vector<ParticleBurst>*>(property.ref)) {
                    bursts.push_back({{"time", burst.time}, {"min_count", burst.minCount}, {"max_count", burst.maxCount}});
                }
                return bursts;
            }
            if (propertyName == "colorGradient") {
                const ParticleColorGradient& gradient = *static_cast<ParticleColorGradient*>(property.ref);
                Json stops = Json::array();
                for (const ParticleColorGradientStop& stop : gradient.stops) {
                    stops.push_back({{"time", stop.time}, {"color", vector3Json(stop.color)}});
                }
                return Json{{"use_srgb", gradient.useSRGB}, {"stops", stops}};
            }
            return Json{{"unsupported", "custom"}};
    }
    return Json();
}

bool safeRelativePath(Project* project, const Json& args, const char* key, fs::path& rel, std::string& error,
                      bool requireExists = false) {
    if (!args.contains(key) || !args[key].is_string()) {
        error = std::string(key) + " is required.";
        return false;
    }
    rel = FileUtils::pathFromUtf8(args[key].get<std::string>()).lexically_normal();
    if (rel.empty()) rel = ".";
    if (!PathUtils::isSafeRelativePath(rel)) {
        error = std::string(key) + " must be a safe project-relative path.";
        return false;
    }
    if (requireExists && project && !fs::exists(project->getProjectPath() / rel)) {
        error = std::string(key) + " does not exist in the project.";
        return false;
    }
    return true;
}

// Same as safeRelativePath, but "." (the project root) is a valid destination
bool safeTargetDir(Project* project, const Json& args, fs::path& rel, std::string& error) {
    if (args.contains("target_dir") && args["target_dir"].is_string()) {
        const fs::path dir = FileUtils::pathFromUtf8(args["target_dir"].get<std::string>()).lexically_normal();
        if (dir.empty() || dir == ".") {
            rel.clear();
            return true;
        }
    }
    return safeRelativePath(project, args, "target_dir", rel, error);
}

constexpr const char* kClosedParentScene = "The parent scene is closed, so the change would be lost when it opens. Open it with open_scene first.";
constexpr const char* kUnsavedScriptEdits = "A script open in the code editor has unsaved edits. Ask the user to save or discard them first.";
constexpr const char* kProjectChangeBusy = "The editor is already switching projects. Try again once it has loaded.";
constexpr const char* kRefusedUndo = "The last command cannot be undone now. Check read_output_log for the reason.";
constexpr const char* kRefusedRedo = "The next command no longer applies. Check read_output_log for the reason.";
constexpr const char* kRefusedCommand = "The editor refused the change. Check read_output_log for the reason.";

// isAnyScenePlaying() is also true while a scene saves or loads
std::string busyError(Project* project, const std::string& action) {
    if (!project->isPlaySessionActive()) {
        for (const SceneProject& sceneProject : project->getScenes()) {
            if (sceneProject.playState == ScenePlayState::SAVING || sceneProject.playState == ScenePlayState::LOADING) {
                return "Scene " + sceneProject.name + " is still saving or loading; retry " + action + " once it finishes.";
            }
        }
    }
    return "Stop play mode before " + action + ".";
}

// Catalog lists every submesh slot, even unused ones
bool isUnusedSubmeshProperty(Scene* scene, Entity entity, const std::string& propName) {
    if (propName.rfind("submeshes[", 0) != 0) return false;
    const MeshComponent* mesh = scene->findComponent<MeshComponent>(entity);
    return mesh && std::stoul(propName.substr(10)) >= mesh->numSubmeshes;
}

// Empty or new directory outside the current project, like the Save Project dialog
bool prepareNewProjectDirectory(Project* project, const Json& args, fs::path& path, std::string& error) {
    path = FileUtils::pathFromUtf8(args.value("path", "")).lexically_normal();
    if (!path.has_filename()) path = path.parent_path();
    if (!path.is_absolute()) {
        error = "path must be an absolute directory.";
        return false;
    }
    std::error_code ec;
    if (fs::exists(path, ec) && (!fs::is_directory(path, ec) || !fs::is_empty(path, ec))) {
        error = "path must be an empty directory or not exist yet: " + FileUtils::pathToUtf8(path);
        return false;
    }
    // Canonical, so a symlinked temp path can't hide inside the project
    std::error_code pathEc, projectEc;
    const fs::path target = fs::weakly_canonical(path, pathEc);
    const fs::path current = fs::weakly_canonical(project->getProjectPath(), projectEc);
    if (pathEc || projectEc) {
        error = "Could not resolve " + FileUtils::pathToUtf8(path) + ".";
        return false;
    }
    const fs::path relative = target.lexically_relative(current);
    if (!relative.empty() && *relative.begin() != "..") {
        error = "path cannot be inside the current project.";
        return false;
    }
    // saveProjectToPath only creates the last level
    fs::create_directories(path, ec);
    if (ec) {
        error = "Could not create " + FileUtils::pathToUtf8(path) + ": " + ec.message();
        return false;
    }
    return true;
}

bool hasUnsavedScriptEdits() {
    CodeEditor* codeEditor = getEditorHost().getCodeEditor();
    return codeEditor && codeEditor->hasUnsavedChanges();
}

// Refused where the menus would ask about unsaved work
bool canSwitchProject(Project* project, std::string& error) {
    error.clear();
    if (project->isAnyScenePlaying()) {
        error = busyError(project, "switching projects");
    } else if (hasUnsavedScriptEdits()) {
        error = kUnsavedScriptEdits;
    } else if (project->hasScenesUnsavedChanges()) {
        error = "The current project has unsaved scene changes. Save each changed scene with save_scene first, passing path for one that has no file yet.";
    } else if (project->isTempUnsavedProject()) {
        error = "The current project is a temporary one in the system temp folder. Keep it by moving it out with save_project and path first.";
    }
    return error.empty();
}

// Json::value() throws when the key is present with another type.
std::string optionalString(const Json& args, const char* key, const char* fallback = "") {
    const auto it = args.find(key);
    return (it == args.end() || !it->is_string()) ? std::string(fallback) : it->get<std::string>();
}

// Lua entries are stored relative to the Lua root, so they resolve through it first
fs::path scriptPath(Project* project, const ScriptEntry& entry) {
    if (entry.path.empty() || entry.type != ScriptType::LUA) {
        return entry.path;
    }
    return project->normalizeToProjectRelative(project->resolveLuaPath(entry.path));
}

// Script entries may store absolute paths, so compare them project-relative. Compared as
// paths, the ANSI entries match the UTF-8 paths the AI sends.
fs::path normalizeScriptPath(Project* project, fs::path path) {
    if (path.is_absolute() && project) {
        std::error_code ec;
        fs::path relative = fs::relative(path, project->getProjectPath(), ec);
        if (!ec) path = relative;
    }
    return path.lexically_normal();
}

// Catalog exposes scripts as an opaque Custom property, so entries are listed here instead.
Json scriptEntriesJson(Project* project, const std::vector<ScriptEntry>& scripts) {
    Json entries = Json::array();
    for (size_t i = 0; i < scripts.size(); i++) {
        const ScriptEntry& script = scripts[i];
        Json entry = {{"index", i},
                      {"type", scriptTypeName(script.type)},
                      {"class_name", script.className},
                      {"path", FileUtils::pathToGenericUtf8(scriptPath(project, script))},
                      {"header_path", FileUtils::pathToGenericUtf8(script.headerPath)},
                      {"enabled", script.enabled}};
        if (script.type == ScriptType::CPP && !script.headerPath.empty() &&
                !script.className.empty()) {
            fs::path headerPath = script.headerPath;
            if (headerPath.is_relative()) headerPath = project->getProjectPath() / headerPath;
            const std::optional<bool> inheritsScriptBase =
                ScriptParser::inheritsScriptBase(headerPath, script.className);
            if (inheritsScriptBase) {
                entry["inferred_cpp_kind"] = *inheritsScriptBase
                    ? "cpp_script_class"
                    : "cpp_subclass";
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool resolveScriptIndex(Project* project, const std::vector<ScriptEntry>& scripts, const Json& args,
                        size_t& outIndex, std::string& error) {
    const auto indexIt = args.find("index");
    if (indexIt != args.end() && indexIt->is_number_integer()) {
        const int64_t index = indexIt->get<int64_t>();
        if (index < 0 || static_cast<size_t>(index) >= scripts.size()) {
            error = "index is out of range.";
            return false;
        }
        outIndex = static_cast<size_t>(index);
        return true;
    }

    const std::string className = optionalString(args, "class_name");
    const fs::path path = className.empty()
        ? normalizeScriptPath(project, FileUtils::pathFromUtf8(optionalString(args, "path"))) : fs::path();
    if (className.empty() && path.empty()) {
        error = "Select the script entry with index, class_name, or path.";
        return false;
    }

    std::vector<size_t> matches;
    for (size_t i = 0; i < scripts.size(); i++) {
        const bool match = className.empty()
            ? (normalizeScriptPath(project, scriptPath(project, scripts[i])) == path ||
               normalizeScriptPath(project, scripts[i].headerPath) == path)
            : scripts[i].className == className;
        if (match) matches.push_back(i);
    }
    if (matches.empty()) {
        error = "No script entry matches the selector.";
        return false;
    }
    if (matches.size() > 1) {
        error = "Selector matches " + std::to_string(matches.size()) + " script entries; pass index instead.";
        return false;
    }
    outIndex = matches.front();
    return true;
}

bool valueFieldPresent(const Json& args, const char* key) {
    return args.contains(key) && !args[key].is_null();
}

float optionalNumber(const Json& args, const char* key, float fallback) {
    const auto it = args.find(key);
    return (it == args.end() || !it->is_number()) ? fallback : it->get<float>();
}

// Particles bursts and colorGradient, each set as a whole list
Command* buildParticleListCommand(Project* project, uint32_t sceneId, Entity entity, ComponentType component,
                                  const std::string& propertyName, const Json& args, std::string& error,
                                  std::function<void()> onChanged) {
    if (propertyName == "bursts") {
        if (!valueFieldPresent(args, "bursts_value") || !args["bursts_value"].is_array()) {
            error = "Property requires bursts_value.";
            return nullptr;
        }
        std::vector<ParticleBurst> bursts;
        for (const Json& item : args["bursts_value"]) {
            if (!item.is_object()) {
                error = "Each burst must be an object.";
                return nullptr;
            }
            ParticleBurst burst;
            burst.time = std::max(0.0f, optionalNumber(item, "time", 0.0f));
            burst.minCount = std::max(0, static_cast<int>(optionalNumber(item, "min_count", 10.0f)));
            burst.maxCount = std::max(burst.minCount, static_cast<int>(optionalNumber(item, "max_count", static_cast<float>(burst.minCount))));
            bursts.push_back(burst);
        }
        return new PropertyCmd<std::vector<ParticleBurst>>(project, sceneId, entity, component, propertyName, bursts, onChanged);
    }

    if (!valueFieldPresent(args, "gradient_value") || !args["gradient_value"].is_object()) {
        error = "Property requires gradient_value.";
        return nullptr;
    }
    const Json& value = args["gradient_value"];
    if (!value.contains("stops") || !value["stops"].is_array()) {
        error = "gradient_value needs stops: [{time, color: {x, y, z}}].";
        return nullptr;
    }
    ParticleColorGradient gradient;
    if (value.contains("use_srgb") && value["use_srgb"].is_boolean()) {
        gradient.useSRGB = value["use_srgb"].get<bool>();
    }
    for (const Json& item : value["stops"]) {
        ParticleColorGradientStop stop;
        if (!item.is_object() || !item.contains("color") || !parseVector3(item["color"], stop.color)) {
            error = "Each gradient stop needs a time and a color {x, y, z}.";
            return nullptr;
        }
        stop.time = optionalNumber(item, "time", 0.0f);
        gradient.stops.push_back(stop);
    }
    gradient.normalize();
    return new PropertyCmd<ParticleColorGradient>(project, sceneId, entity, component, propertyName, gradient, onChanged);
}

Command* buildPropertyCommand(Project* project, uint32_t sceneId, Entity entity, ComponentType component,
                              const std::string& propertyName, const PropertyData& property,
                              const Json& args, std::string& error, std::function<void()> onChanged = nullptr) {
    switch (property.type) {
        case PropertyType::Bool:
            if (!valueFieldPresent(args, "bool_value") || !args["bool_value"].is_boolean()) {
                error = "Property requires bool_value.";
                return nullptr;
            }
            return new PropertyCmd<bool>(project, sceneId, entity, component, propertyName, args["bool_value"].get<bool>(), onChanged);
        case PropertyType::String:
            if (!valueFieldPresent(args, "string_value") || !args["string_value"].is_string()) {
                error = "Property requires string_value.";
                return nullptr;
            }
            {
                std::string value = args["string_value"].get<std::string>();
                if (isAssetPathProperty(propertyName) && !value.empty()) {
                    const fs::path rel = FileUtils::pathFromUtf8(value);
                    if (!PathUtils::isSafeRelativePath(rel)) {
                        error = propertyName + " must be a safe project-relative path.";
                        return nullptr;
                    }
                    if (!isInsideAssets(project, rel)) {
                        error = outsideAssetsError(project, propertyName);
                        return nullptr;
                    }
                    value = assetPathFromAi(project, rel);
                }
                return new PropertyCmd<std::string>(project, sceneId, entity, component, propertyName, value, onChanged);
            }
        case PropertyType::Float: {
            if (!valueFieldPresent(args, "number_value") || !args["number_value"].is_number()) {
                error = "Property requires number_value.";
                return nullptr;
            }
            return new PropertyCmd<float>(project, sceneId, entity, component, propertyName, args["number_value"].get<float>(), onChanged);
        }
        case PropertyType::Double: {
            if (!valueFieldPresent(args, "number_value") || !args["number_value"].is_number()) {
                error = "Property requires number_value.";
                return nullptr;
            }
            return new PropertyCmd<double>(project, sceneId, entity, component, propertyName, args["number_value"].get<double>(), onChanged);
        }
        case PropertyType::Vector2: {
            Vector2 value = Vector2::ZERO;
            if (!valueFieldPresent(args, "vector2_value") || !parseVector2(args["vector2_value"], value)) {
                error = "Property requires vector2_value.";
                return nullptr;
            }
            return new PropertyCmd<Vector2>(project, sceneId, entity, component, propertyName, value, onChanged);
        }
        case PropertyType::Vector3: {
            Vector3 value = Vector3::ZERO;
            if (!valueFieldPresent(args, "vector3_value") || !parseVector3(args["vector3_value"], value)) {
                error = "Property requires vector3_value.";
                return nullptr;
            }
            return new PropertyCmd<Vector3>(project, sceneId, entity, component, propertyName, value, onChanged);
        }
        case PropertyType::Vector4: {
            Vector4 value = Vector4::ZERO;
            if (!valueFieldPresent(args, "vector4_value") || !parseVector4(args["vector4_value"], value)) {
                error = "Property requires vector4_value.";
                return nullptr;
            }
            return new PropertyCmd<Vector4>(project, sceneId, entity, component, propertyName, value, onChanged);
        }
        case PropertyType::Quat: {
            Quaternion value = Quaternion::IDENTITY;
            if (!valueFieldPresent(args, "quat_value")) {
                error = "Property requires quat_value.";
                return nullptr;
            }
            if (!parseRotation(args["quat_value"], value)) {
                error = "quat_value must be a unit quaternion, not Euler angles.";
                return nullptr;
            }
            return new PropertyCmd<Quaternion>(project, sceneId, entity, component, propertyName, value, onChanged);
        }
        case PropertyType::Int:
        case PropertyType::Enum:
            if (!valueFieldPresent(args, "int_value") || !args["int_value"].is_number_integer()) {
                error = "Property requires int_value.";
                return nullptr;
            }
            return new PropertyCmd<int>(project, sceneId, entity, component, propertyName, args["int_value"].get<int>(), onChanged);
        case PropertyType::Ease: {
            EaseType type;
            if (!valueFieldPresent(args, "string_value") || !args["string_value"].is_string() ||
                    !parseEaseName(args["string_value"].get<std::string>(), type) || type == EaseType::CUSTOM) {
                error = "Property requires string_value with an ease name, e.g. SINE_IN_OUT.";
                return nullptr;
            }
            return new PropertyCmd<Ease>(project, sceneId, entity, component, propertyName, Ease(type), onChanged);
        }
        case PropertyType::UInt:
            if (!valueFieldPresent(args, "int_value") || !args["int_value"].is_number_integer()) {
                error = "Property requires int_value.";
                return nullptr;
            }
            return new PropertyCmd<unsigned int>(project, sceneId, entity, component, propertyName,
                                                static_cast<unsigned int>(std::max(0, args["int_value"].get<int>())), onChanged);
        case PropertyType::Font: {
            // font_paths sets the whole chain, string_value only the main font
            FontArray fonts;
            if (property.ref) {
                fonts = *static_cast<FontArray*>(property.ref);
            }

            std::vector<std::string> requested;
            bool wholeChain = valueFieldPresent(args, "font_paths");
            if (wholeChain) {
                if (!args["font_paths"].is_array() || args["font_paths"].size() > fonts.size()) {
                    error = "font_paths takes up to " + std::to_string(fonts.size()) + " project-relative font paths.";
                    return nullptr;
                }
                for (const Json& item : args["font_paths"]) {
                    if (!item.is_string()) {
                        error = "font_paths must hold strings.";
                        return nullptr;
                    }
                    requested.push_back(item.get<std::string>());
                }
            } else if (valueFieldPresent(args, "string_value") && args["string_value"].is_string()) {
                requested.push_back(args["string_value"].get<std::string>());
            } else {
                error = "Property requires font_paths or string_value.";
                return nullptr;
            }

            for (size_t i = 0; i < fonts.size(); i++) {
                if (i >= requested.size()) {
                    if (wholeChain) fonts[i].clear();
                    continue;
                }
                std::string value = requested[i];
                if (!value.empty()) {
                    const fs::path rel = FileUtils::pathFromUtf8(value);
                    if (!PathUtils::isSafeRelativePath(rel)) {
                        error = propertyName + " must hold safe project-relative paths.";
                        return nullptr;
                    }
                    if (!isInsideAssets(project, rel)) {
                        error = outsideAssetsError(project, propertyName);
                        return nullptr;
                    }
                    value = assetPathFromAi(project, rel);
                }
                fonts[i] = value;
            }

            return new PropertyCmd<FontArray>(project, sceneId, entity, component, propertyName, fonts, onChanged);
        }
        case PropertyType::Texture: {
            if (!valueFieldPresent(args, "texture_path") || !args["texture_path"].is_string()) {
                error = "Property requires texture_path.";
                return nullptr;
            }
            std::vector<std::string> paths;
            std::istringstream texturePath(args["texture_path"].get<std::string>());
            for (std::string path; std::getline(texturePath, path, '|');) {
                fs::path rel = FileUtils::pathFromUtf8(path);
                if (!PathUtils::isSafeRelativePath(rel)) {
                    error = "texture_path must be a safe project-relative path.";
                    return nullptr;
                }
                if (!isInsideAssets(project, rel)) {
                    error = outsideAssetsError(project, "texture_path");
                    return nullptr;
                }
                paths.push_back(assetPathFromAi(project, rel));
            }
            // a cubemap takes one cross-layout image or its six faces
            const bool cubeMap = propertyName == "texture" &&
                (component == ComponentType::SkyComponent || component == ComponentType::ReflectionProbeComponent);
            if (paths.size() != 1 && !(cubeMap && paths.size() == 6)) {
                error = "texture_path takes one path, or six faces joined by '|' for a cubemap.";
                return nullptr;
            }
            Texture texture;
            if (!cubeMap) {
                texture = Texture(paths[0]);
            } else if (paths.size() == 1) {
                texture.setCubeMap(paths[0]);
            } else {
                for (size_t face = 0; face < paths.size(); face++) {
                    texture.setCubePath(face, paths[face]);
                }
            }
            return new PropertyCmd<Texture>(project, sceneId, entity, component, propertyName, texture, onChanged);
        }
        case PropertyType::Entity:
            if (!valueFieldPresent(args, "entity_value") || !args["entity_value"].is_number_integer()) {
                error = "Property requires entity_value.";
                return nullptr;
            }
            return new PropertyCmd<Entity>(project, sceneId, entity, component, propertyName,
                                           static_cast<Entity>(std::max(0, args["entity_value"].get<int>())), onChanged);
        case PropertyType::EntityReference: {
            if (!valueFieldPresent(args, "entity_value") || !args["entity_value"].is_number_integer()) {
                error = "Property requires entity_value.";
                return nullptr;
            }
            EntityReference ref;
            ref.entity = static_cast<Entity>(std::max(0, args["entity_value"].get<int>()));
            // the same scene is stored as 0, like the Properties panel does
            int refSceneId = args.value("entity_scene_id", 0);
            ref.sceneId = (refSceneId == static_cast<int>(sceneId)) ? 0 : static_cast<uint32_t>(std::max(0, refSceneId));
            return new PropertyCmd<EntityReference>(project, sceneId, entity, component, propertyName, ref, onChanged);
        }
        case PropertyType::Material:
        case PropertyType::Custom:
            if (propertyName == "bursts" || propertyName == "colorGradient") {
                return buildParticleListCommand(project, sceneId, entity, component, propertyName, args, error, onChanged);
            }
            error = "Property type " + propertyTypeName(property.type) + " is not supported by generic set_component_property.";
            return nullptr;
    }
    error = "Unsupported property type.";
    return nullptr;
}

std::string filenameFromUrl(const std::string& url, const std::string& fallback) {
    std::string trimmed = url;
    size_t query = trimmed.find('?');
    if (query != std::string::npos) trimmed = trimmed.substr(0, query);
    size_t slash = trimmed.find_last_of('/');
    std::string name = slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
    if (name.empty() || name.find('\\') != std::string::npos || name.find('/') != std::string::npos) {
        name = fallback;
    }
    return name;
}

// Files a .gltf references, as {path, url}. Poly Haven keeps them in other folders and
// lists them in its files API; other uris resolve against the .gltf url.
std::vector<std::pair<std::string, std::string>> gltfDependencies(const HttpClient* httpClient, const Json& arguments,
                                                                  const fs::path& gltfFile, const std::atomic<bool>* cancel) {
    std::vector<std::pair<std::string, std::string>> files;
    const std::string url = arguments.value("download_url", "");

    if (lower(arguments.value("provider", "")) == "polyhaven") {
        HttpResponse response = httpClient->get("https://api.polyhaven.com/files/" + HttpClient::urlEncode(arguments.value("asset_id", "")),
            {"Accept: application/json", "User-Agent: DoriaxEditorAI/1.0"}, cancel);
        Json root = Json::parse(response.body, nullptr, false);
        if (root.is_object() && root.contains("gltf")) {
            for (const Json& resolution : root["gltf"]) {
                const Json gltf = resolution.value("gltf", Json::object());
                if (gltf.value("url", "") != url) continue;
                const Json include = gltf.value("include", Json::object());
                for (auto it = include.begin(); it != include.end(); ++it) {
                    files.emplace_back(it.key(), it.value().value("url", ""));
                }
                return files;
            }
        }
    }

    std::ifstream in(gltfFile);
    Json gltf = Json::parse(in, nullptr, false);
    if (!gltf.is_object()) return files;

    const std::string base = url.substr(0, url.find_last_of('/') + 1);
    for (const char* key : {"buffers", "images"}) {
        for (const Json& item : gltf.value(key, Json::array())) {
            const std::string uri = item.value("uri", "");
            if (!uri.empty() && uri.rfind("data:", 0) != 0 && uri.find("://") == std::string::npos) {
                files.emplace_back(uri, base + uri);
            }
        }
    }
    return files;
}

Entity resolveTerrainEntity(Project* project, SceneProject* sceneProject, const Json& args) {
    Entity entity = resolveEntity(sceneProject, args);
    if (entity != NULL_ENTITY &&
        sceneProject->scene->findComponent<TerrainComponent>(entity)) {
        return entity;
    }

    const std::vector<Entity>& selected = project->getSelectedEntities(sceneProject->id);
    if (selected.size() == 1 &&
        sceneProject->scene->findComponent<TerrainComponent>(selected[0])) {
        return selected[0];
    }

    return NULL_ENTITY;
}

bool parseGeometryType(const std::string& name, int& out) {
    static const std::unordered_map<std::string, int> map = {
        {"plane", 0}, {"box", 1}, {"sphere", 2}, {"cylinder", 3},
        {"capsule", 4}, {"torus", 5}, {"wall", 6}
    };
    auto it = map.find(lower(name));
    if (it == map.end()) return false;
    out = it->second;
    return true;
}

bool parseGeometryTypeValue(const Json& args, ShapeParameters& shape, std::string& error) {
    if (!args.contains("geometry_type")) return true;

    int geometryType = shape.geometryType;
    if (args["geometry_type"].is_string()) {
        if (!parseGeometryType(args["geometry_type"].get<std::string>(), geometryType)) {
            error = "Unsupported geometry_type. Use plane, box, sphere, cylinder, capsule, torus, or wall.";
            return false;
        }
    } else if (args["geometry_type"].is_number_integer()) {
        geometryType = args["geometry_type"].get<int>();
        if (geometryType < 0 || geometryType > 6) {
            error = "geometry_type integer must be between 0 and 6.";
            return false;
        }
    } else {
        error = "geometry_type must be a string or integer.";
        return false;
    }

    shape.geometryType = geometryType;
    return true;
}

float clampedFloatArg(const Json& args, const char* key, float current, float minValue, float maxValue) {
    if (!args.contains(key) || !args[key].is_number()) return current;
    float value = args[key].get<float>();
    if (!std::isfinite(value)) return current;
    return std::clamp(value, minValue, maxValue);
}

unsigned int clampedUIntArg(const Json& args, const char* key, unsigned int current,
                            unsigned int minValue, unsigned int maxValue) {
    if (!args.contains(key) || !args[key].is_number_integer()) return current;
    int64_t value = args[key].get<int64_t>();
    if (value < static_cast<int64_t>(minValue)) return minValue;
    if (value > static_cast<int64_t>(maxValue)) return maxValue;
    return static_cast<unsigned int>(value);
}

void applyShapeParameters(const Json& args, ShapeParameters& shape) {
    constexpr float kMinSize = 0.1f;
    constexpr float kMaxSize = 100.0f;
    constexpr unsigned int kMaxSegments = 100;

    shape.planeWidth = clampedFloatArg(args, "plane_width", shape.planeWidth, kMinSize, kMaxSize);
    shape.planeDepth = clampedFloatArg(args, "plane_depth", shape.planeDepth, kMinSize, kMaxSize);
    shape.planeTiles = clampedUIntArg(args, "plane_tiles", shape.planeTiles, 1, kMaxSegments);
    shape.wallWidth = clampedFloatArg(args, "wall_width", shape.wallWidth, kMinSize, kMaxSize);
    shape.wallHeight = clampedFloatArg(args, "wall_height", shape.wallHeight, kMinSize, kMaxSize);
    shape.wallTiles = clampedUIntArg(args, "wall_tiles", shape.wallTiles, 1, kMaxSegments);
    shape.boxWidth = clampedFloatArg(args, "box_width", shape.boxWidth, kMinSize, kMaxSize);
    shape.boxHeight = clampedFloatArg(args, "box_height", shape.boxHeight, kMinSize, kMaxSize);
    shape.boxDepth = clampedFloatArg(args, "box_depth", shape.boxDepth, kMinSize, kMaxSize);
    shape.boxTiles = clampedUIntArg(args, "box_tiles", shape.boxTiles, 1, kMaxSegments);
    shape.sphereRadius = clampedFloatArg(args, "sphere_radius", shape.sphereRadius, kMinSize, kMaxSize);
    shape.sphereSlices = clampedUIntArg(args, "sphere_slices", shape.sphereSlices, 3, kMaxSegments);
    shape.sphereStacks = clampedUIntArg(args, "sphere_stacks", shape.sphereStacks, 3, kMaxSegments);
    shape.cylinderBaseRadius = clampedFloatArg(args, "cylinder_base_radius", shape.cylinderBaseRadius, kMinSize, kMaxSize);
    shape.cylinderTopRadius = clampedFloatArg(args, "cylinder_top_radius", shape.cylinderTopRadius, kMinSize, kMaxSize);
    shape.cylinderHeight = clampedFloatArg(args, "cylinder_height", shape.cylinderHeight, kMinSize, kMaxSize);
    shape.cylinderSlices = clampedUIntArg(args, "cylinder_slices", shape.cylinderSlices, 3, kMaxSegments);
    shape.cylinderStacks = clampedUIntArg(args, "cylinder_stacks", shape.cylinderStacks, 1, kMaxSegments);
    shape.capsuleBaseRadius = clampedFloatArg(args, "capsule_base_radius", shape.capsuleBaseRadius, kMinSize, kMaxSize);
    shape.capsuleTopRadius = clampedFloatArg(args, "capsule_top_radius", shape.capsuleTopRadius, kMinSize, kMaxSize);
    shape.capsuleHeight = clampedFloatArg(args, "capsule_height", shape.capsuleHeight, kMinSize, kMaxSize);
    shape.capsuleSlices = clampedUIntArg(args, "capsule_slices", shape.capsuleSlices, 3, kMaxSegments);
    shape.capsuleStacks = clampedUIntArg(args, "capsule_stacks", shape.capsuleStacks, 1, kMaxSegments);
    shape.torusRadius = clampedFloatArg(args, "torus_radius", shape.torusRadius, kMinSize, kMaxSize);
    shape.torusRingRadius = clampedFloatArg(args, "torus_ring_radius", shape.torusRingRadius, kMinSize, kMaxSize);
    shape.torusSides = clampedUIntArg(args, "torus_sides", shape.torusSides, 3, kMaxSegments);
    shape.torusRings = clampedUIntArg(args, "torus_rings", shape.torusRings, 3, kMaxSegments);
}

void updateMeshShape(MeshComponent& meshComp, MeshSystem* meshSys, const ShapeParameters& shapeParams) {
    switch (shapeParams.geometryType) {
        case 0:
            meshSys->createPlane(meshComp, shapeParams.planeWidth, shapeParams.planeDepth, shapeParams.planeTiles);
            break;
        case 1:
            meshSys->createBox(meshComp, shapeParams.boxWidth, shapeParams.boxHeight, shapeParams.boxDepth, shapeParams.boxTiles);
            break;
        case 2:
            meshSys->createSphere(meshComp, shapeParams.sphereRadius, shapeParams.sphereSlices, shapeParams.sphereStacks);
            break;
        case 3:
            meshSys->createCylinder(meshComp, shapeParams.cylinderBaseRadius, shapeParams.cylinderTopRadius,
                                  shapeParams.cylinderHeight, shapeParams.cylinderSlices, shapeParams.cylinderStacks);
            break;
        case 4:
            meshSys->createCapsule(meshComp, shapeParams.capsuleBaseRadius, shapeParams.capsuleTopRadius,
                                   shapeParams.capsuleHeight, shapeParams.capsuleSlices, shapeParams.capsuleStacks);
            break;
        case 5:
            meshSys->createTorus(meshComp, shapeParams.torusRadius, shapeParams.torusRingRadius,
                                 shapeParams.torusSides, shapeParams.torusRings);
            break;
        case 6:
            meshSys->createWall(meshComp, shapeParams.wallWidth, shapeParams.wallHeight, shapeParams.wallTiles);
            break;
    }
}

bool isReadableTextResource(const std::string& ext) {
    static const std::set<std::string> allowed = {
        ".lua", ".cpp", ".h", ".hpp", ".material", ".scene", ".yaml", ".yml",
        ".json", ".txt", ".glsl", ".vert", ".frag", ".hlsl", ".md", ".csv", ".ini", ".cfg",
        ".cmake"
    };
    return allowed.count(ext) > 0;
}

Json sceneReadableProperties(SceneProject* sceneProject) {
    Scene* scene = sceneProject->scene;
    Json props = Json::object();
    props["background_color"] = {{"type", "vector4"}, {"value", vector4Json(Catalog::getSceneProperty<Vector4>(scene, "background_color"))}};
    props["global_illumination_color"] = {{"type", "vector3"}, {"value", vector3Json(Catalog::getSceneProperty<Vector3>(scene, "global_illumination_color"))}};
    props["global_illumination_intensity"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "global_illumination_intensity")}};
    props["light_state"] = {{"type", "int"}, {"value", static_cast<int>(Catalog::getSceneProperty<LightState>(scene, "light_state"))}};
    props["ambient_light_2d_color"] = {{"type", "vector3"}, {"value", vector3Json(Catalog::getSceneProperty<Vector3>(scene, "ambient_light_2d_color"))}};
    props["ambient_light_2d_intensity"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ambient_light_2d_intensity")}};
    props["shadows_quality"] = {{"type", "int"}, {"value", static_cast<int>(Catalog::getSceneProperty<ShadowQuality>(scene, "shadows_quality"))}};
    props["shadows_2d_quality"] = {{"type", "int"}, {"value", static_cast<int>(Catalog::getSceneProperty<ShadowQuality>(scene, "shadows_2d_quality"))}};
    props["physics_gravity_2d"] = {{"type", "vector2"}, {"value", vector2Json(Catalog::getSceneProperty<Vector2>(scene, "physics_gravity_2d"))}};
    props["physics_gravity_3d"] = {{"type", "vector3"}, {"value", vector3Json(Catalog::getSceneProperty<Vector3>(scene, "physics_gravity_3d"))}};
    props["mesh_lod_enabled"] = {{"type", "bool"}, {"value", Catalog::getSceneProperty<bool>(scene, "mesh_lod_enabled")}};
    props["mesh_lod_threshold"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "mesh_lod_threshold")}};
    props["depth_prepass_enabled"] = {{"type", "bool"}, {"value", Catalog::getSceneProperty<bool>(scene, "depth_prepass_enabled")}};
    props["ssao_enabled"] = {{"type", "bool"}, {"value", Catalog::getSceneProperty<bool>(scene, "ssao_enabled")}};
    props["ssao_radius"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ssao_radius")}};
    props["ssao_intensity"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ssao_intensity")}};
    props["ssao_bias"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ssao_bias")}};
    props["ssao_debug"] = {{"type", "bool"}, {"value", Catalog::getSceneProperty<bool>(scene, "ssao_debug")}};
    props["ssr_enabled"] = {{"type", "bool"}, {"value", Catalog::getSceneProperty<bool>(scene, "ssr_enabled")}};
    props["ssr_max_distance"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ssr_max_distance")}};
    props["ssr_thickness"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ssr_thickness")}};
    props["ssr_intensity"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ssr_intensity")}};
    props["ssr_blur"] = {{"type", "float"}, {"value", Catalog::getSceneProperty<float>(scene, "ssr_blur")}};
    props["ssr_max_steps"] = {{"type", "int"}, {"value", Catalog::getSceneProperty<int>(scene, "ssr_max_steps")}};
    props["ssr_debug_mode"] = {{"type", "int"}, {"value", Catalog::getSceneProperty<int>(scene, "ssr_debug_mode")}};
    props["fixed_resolution_enabled"] = {{"type", "bool"}, {"value", Catalog::getSceneProperty<bool>(scene, "fixed_resolution_enabled")}};
    props["fixed_resolution_width"] = {{"type", "int"}, {"value", Catalog::getSceneProperty<int>(scene, "fixed_resolution_width")}};
    props["fixed_resolution_height"] = {{"type", "int"}, {"value", Catalog::getSceneProperty<int>(scene, "fixed_resolution_height")}};
    props["fixed_resolution_filter"] = {{"type", "int"}, {"value", static_cast<int>(Catalog::getSceneProperty<TextureFilter>(scene, "fixed_resolution_filter") == TextureFilter::LINEAR ? 1 : 0)}};
    props["default_mesh_shader"] = {{"type", "string"}, {"value", Catalog::getSceneProperty<std::string>(scene, "default_mesh_shader")}};
    props["default_ui_shader"] = {{"type", "string"}, {"value", Catalog::getSceneProperty<std::string>(scene, "default_ui_shader")}};
    props["default_sky_shader"] = {{"type", "string"}, {"value", Catalog::getSceneProperty<std::string>(scene, "default_sky_shader")}};
    props["default_points_shader"] = {{"type", "string"}, {"value", Catalog::getSceneProperty<std::string>(scene, "default_points_shader")}};
    props["default_lines_shader"] = {{"type", "string"}, {"value", Catalog::getSceneProperty<std::string>(scene, "default_lines_shader")}};
    props["default_water_shader"] = {{"type", "string"}, {"value", Catalog::getSceneProperty<std::string>(scene, "default_water_shader")}};
    return props;
}

bool readWholeFile(const fs::path& path, std::string& content) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool writeWholeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << content;
    out.close();
    return static_cast<bool>(out);
}

void refreshMaterialEdit(Project* project, ResourcesWindow* resourcesWindow, const fs::path& fullPath) {
    if (project) {
        project->refreshLinkedMaterials(true);
    }
    if (resourcesWindow) {
        resourcesWindow->notifyResourceFileChanged(fullPath);
        resourcesWindow->refreshCurrentDirectory();
    }
}

bool normalizeMaterialPayload(const fs::path& rel, const std::string& content,
                              std::string& normalized, std::string& error) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node || !node.IsMap()) {
            error = "Material content must be a YAML map.";
            return false;
        }

        Material material = Stream::decodeMaterial(node);
        material.name = rel.lexically_normal().generic_string();
        normalized = YAML::Dump(Stream::encodeMaterial(material));
        return true;
    } catch (const std::exception& e) {
        error = std::string("Material content is invalid: ") + e.what();
        return false;
    }
}

class ReplaceMaterialFileCmd : public Command {
public:
    ReplaceMaterialFileCmd(Project* project, ResourcesWindow* resourcesWindow,
                           fs::path fullPath, std::string newContent)
        : project(project)
        , resourcesWindow(resourcesWindow)
        , fullPath(std::move(fullPath))
        , newContent(std::move(newContent)) {}

    bool execute() override {
        if (!capturedOldContent) {
            oldExists = fs::exists(fullPath) && !fs::is_directory(fullPath);
            if (oldExists && !readWholeFile(fullPath, oldContent)) {
                return false;
            }
            capturedOldContent = true;
        }

        if (!writeWholeFile(fullPath, newContent)) {
            return false;
        }

        refreshMaterialEdit(project, resourcesWindow, fullPath);
        return true;
    }

    void undo() override {
        if (oldExists) {
            writeWholeFile(fullPath, oldContent);
        } else {
            std::error_code ec;
            fs::remove(fullPath, ec);
        }
        refreshMaterialEdit(project, resourcesWindow, fullPath);
    }

    bool mergeWith(Command*) override {
        return false;
    }

private:
    Project* project;
    ResourcesWindow* resourcesWindow;
    fs::path fullPath;
    std::string newContent;
    std::string oldContent;
    bool oldExists = false;
    bool capturedOldContent = false;
};

class RemoveProjectSceneCmd : public Command {
public:
    RemoveProjectSceneCmd(Project* project, uint32_t sceneId)
        : project(project)
        , sceneId(sceneId) {}

    bool execute() override {
        if (!project || project->isAnyScenePlaying() || project->getScenes().size() <= 1) {
            return false;
        }

        uint32_t targetSceneId = sceneId;
        SceneProject* sceneProject = project->getScene(targetSceneId);
        if (!sceneProject && !scenePath.empty()) {
            targetSceneId = project->findSceneByPath(scenePath);
            sceneProject = project->getScene(targetSceneId);
        }
        if (!sceneProject || sceneProject->filepath.empty()) {
            return false;
        }
        if (project->hasSceneUnsavedChanges(sceneProject->id)) {
            return false;
        }

        sceneId = targetSceneId;
        scenePath = sceneProject->filepath.lexically_normal();
        sceneName = sceneProject->name;
        wasOpened = sceneProject->opened;
        previousSelectedScene = project->getSelectedSceneId();

        project->removeScene(sceneId);
        if (project->getScene(sceneId) || project->findSceneByPath(scenePath) != NULL_PROJECT_SCENE) {
            return false;
        }

        if (!project->saveProjectFile()) {
            restoreScene();
            return false;
        }

        removed = true;
        return true;
    }

    void undo() override {
        if (!removed) return;
        restoreScene();
    }

    bool mergeWith(Command*) override {
        return false;
    }

private:
    void restoreScene() {
        if (!project || scenePath.empty()) return;
        if (project->findSceneByPath(scenePath) == NULL_PROJECT_SCENE) {
            project->loadScene(scenePath, wasOpened, true, wasOpened);
        }

        uint32_t restoredSceneId = project->findSceneByPath(scenePath);
        if (previousSelectedScene != NULL_PROJECT_SCENE && project->getScene(previousSelectedScene)) {
            project->setSelectedSceneId(previousSelectedScene);
        } else if (restoredSceneId != NULL_PROJECT_SCENE) {
            project->setSelectedSceneId(restoredSceneId);
        }
        project->saveProjectFile();
    }

    Project* project;
    uint32_t sceneId;
    fs::path scenePath;
    std::string sceneName;
    bool wasOpened = false;
    bool removed = false;
    uint32_t previousSelectedScene = NULL_PROJECT_SCENE;
};

} // namespace

EditorActionExecutor::EditorActionExecutor(Project* project, ResourcesWindow* resourcesWindow, const HttpClient* httpClient)
    : project(project)
    , resourcesWindow(resourcesWindow)
    , httpClient(httpClient) {
}

std::string EditorActionExecutor::resultText(const ActionResult& result) {
    std::string text = result.success ? result.message : ("Error: " + result.message);
    if (!result.data.empty()) {
        // Replace, not throw: file reads and logs can carry invalid UTF-8.
        text += "\n" + result.data.dump(2, ' ', false, Json::error_handler_t::replace);
    }
    return text;
}

void EditorActionExecutor::logFailure(const ActionResult& result, const std::string& source) {
    if (result.success) return;
    switch (result.failureSeverity) {
        case ActionFailureSeverity::ExpectedMiss:
            break;
        case ActionFailureSeverity::Warning:
            Out::warning("%s action warning: %s", source.c_str(), result.message.c_str());
            break;
        case ActionFailureSeverity::Error:
            Out::error("%s action failed: %s", source.c_str(), result.message.c_str());
            break;
    }
}

bool EditorActionExecutor::isBlockedBySave(Project* project, const std::string& name) {
    return project->isAnySceneSaving() && !EditorActionRegistry::isReadOnly(name);
}

ActionResult EditorActionExecutor::execute(const std::string& name,
                                           const Json& arguments,
                                           const std::atomic<bool>* cancel) {
    // A wrong-typed argument throws on the main thread, terminating the editor.
    try {
        return dispatch(name, arguments, cancel);
    } catch (const std::exception& e) {
        return failResult("Action " + name + " failed: " + e.what() +
                          ". Check that each argument uses its documented type.");
    }
}

ActionResult EditorActionExecutor::dispatch(const std::string& name,
                                            const Json& arguments,
                                            const std::atomic<bool>* cancel) {
    ValidationResult validation = EditorActionRegistry::validate(name, arguments);
    if (!validation.ok) {
        if (EditorActionRegistry::hasTool(name) &&
            EditorActionRegistry::isReadOnly(name)) {
            return warningResult(validation.error);
        }
        return failResult(validation.error);
    }

    if (!project) {
        return failResult("No project is available.");
    }

    if (isBlockedBySave(project, name)) {
        return failResult(busyError(project, name));
    }

    if (name == "get_project_summary") return getProjectSummary();
    if (name == "search_resources") return searchResources(arguments);
    if (name == "list_scene_entities") return listSceneEntities(arguments);
    if (name == "inspect_entity") return inspectEntity(arguments);
    if (name == "inspect_component") return inspectComponent(arguments);
    if (name == "list_component_types") return listComponentTypes();
    if (name == "search_engine_api") return searchEngineApi(arguments);
    if (name == "search_engine_source") return searchEngineSource(arguments);
    if (name == "read_engine_source") return readEngineSource(arguments);
    if (name == "read_output_log") return readOutputLog(arguments);
    if (name == "create_entity") return createEntity(arguments);
    if (name == "set_entity_transform") return setEntityTransform(arguments);
    if (name == "rename_entity") return renameEntity(arguments);
    if (name == "delete_entity") return deleteEntity(arguments);
    if (name == "duplicate_entity") return duplicateEntity(arguments);
    if (name == "reparent_entity") return reparentEntity(arguments);
    if (name == "move_entity_order") return moveEntityOrder(arguments);
    if (name == "select_entities") return selectEntities(arguments);
    if (name == "add_component") return addComponent(arguments);
    if (name == "remove_component") return removeComponent(arguments);
    if (name == "add_body3d_shape") return addBody3DShape(arguments);
    if (name == "add_body2d_shape") return addBody2DShape(arguments);
    if (name == "set_component_property") return setComponentProperty(arguments);
    if (name == "set_texture_settings") return setTextureSettings(arguments);
    if (name == "add_project_scene") return addProjectScene(arguments);
    if (name == "create_scene") return createScene(arguments);
    if (name == "rename_scene") return renameScene(arguments);
    if (name == "save_scene") return saveScene(arguments);
    if (name == "save_all_scenes") return saveAllScenes();
    if (name == "set_start_scene") return setStartScene(arguments);
    if (name == "set_scene_property") return setSceneProperty(arguments);
    if (name == "control_play_mode") return controlPlayMode(arguments);
    if (name == "add_child_scene") return addChildScene(arguments);
    if (name == "remove_child_scene") return removeChildScene(arguments);
    if (name == "set_child_scene_start_active") return setChildSceneStartActive(arguments);
    if (name == "load_child_scene_inline") return loadChildSceneInline(arguments);
    if (name == "create_folder") return createFolder(arguments);
    if (name == "rename_resource") return renameResource(arguments);
    if (name == "delete_resource") return deleteResource(arguments);
    if (name == "import_resource_file") return importResourceFile(arguments);
    if (name == "link_material") return linkMaterial(arguments);
    if (name == "unlink_material") return unlinkMaterial(arguments);
    if (name == "create_material_file") return createMaterialFile(arguments);
    if (name == "set_terrain_textures") return setTerrainTextures(arguments);
    if (name == "set_terrain_foliage_layer") return setTerrainFoliageLayer(arguments);
    if (name == "set_editor_camera") return setEditorCamera(arguments);
    if (name == "create_terrain_blendmap") return createTerrainBlendmap(arguments);
    if (name == "add_terrain_collision") return addTerrainCollision(arguments);
    if (name == "create_script") return createScript(arguments);
    if (name == "attach_script") return attachScript(arguments);
    if (name == "update_script_entry") return updateScriptEntry(arguments);
    if (name == "remove_script_entry") return removeScriptEntry(arguments);
    if (name == "update_script_file") return updateScriptFile(arguments);
    if (name == "create_source_file") return createSourceFile(arguments);
    if (name == "set_project_script_dirs") return setProjectScriptDirs(arguments);
    if (name == "set_project_asset_dirs") return setProjectAssetDirs(arguments);
    if (name == "check_cpp_build_tools") return checkCppBuildTools();
    if (name == "set_project_cxx_standard") return setProjectCxxStandard(arguments);
    if (name == "update_project_build_file") return updateProjectBuildFile(arguments);
    if (name == "create_bundle_from_entity") return createBundleFromEntity(arguments);
    if (name == "import_bundle_instance") return importBundleInstance(arguments);
    if (name == "add_entity_to_bundle") return addEntityToBundle(arguments);
    if (name == "remove_entity_from_bundle") return removeEntityFromBundle(arguments);
    if (name == "make_bundle_component_unique") return makeBundleComponentUnique(arguments);
    if (name == "revert_bundle_component") return revertBundleComponent(arguments);
    if (name == "set_standalone_bundle") return setStandaloneBundle(arguments);
    if (name == "export_project") return exportProject(arguments, cancel);
    if (name == "set_export_shaders") return setExportShaders(arguments);
    if (name == "generate_shaders") return generateShaders(arguments, cancel);
    if (name == "fork_shader") return forkShader(arguments);
    if (name == "write_shader_file") return writeShaderFile(arguments);
    if (name == "inspect_shader_uniforms") return inspectShaderUniforms(arguments);
    if (name == "set_shader_uniform") return setShaderUniform(arguments);
    if (name == "create_terrain_heightmap") return createTerrainHeightmap(arguments);
    if (name == "import_project_model") return importProjectModel(arguments);
    if (name == "reload_project_model") return reloadProjectModel(arguments);
    if (name == "search_curated_assets") return searchCuratedAssets(arguments, cancel);
    if (name == "download_curated_asset") return downloadCuratedAsset(arguments, cancel);
    if (name == "read_resource_file") return readResourceFile(arguments);
    if (name == "set_main_camera") return setMainCamera(arguments);
    if (name == "open_scene") return openScene(arguments);
    if (name == "select_scene") return selectScene(arguments);
    if (name == "regenerate_mesh_geometry") return regenerateMeshGeometry(arguments);
    if (name == "delete_scene") return deleteScene(arguments);
    if (name == "save_project") return saveProject(arguments);
    if (name == "set_project_settings") return setProjectSettings(arguments);
    if (name == "create_project") return createProject(arguments);
    if (name == "open_project") return openProject(arguments);
    if (name == "copy_resource") return copyResource(arguments);
    if (name == "update_material_file") return updateMaterialFile(arguments);
    if (name == "set_component_properties") return setComponentProperties(arguments);
    if (name == "inspect_scene") return inspectScene(arguments);
    if (name == "add_tilemap_tile") return addTilemapTile(arguments);
    if (name == "remove_tilemap_tile") return removeTilemapTile(arguments);
    if (name == "duplicate_tilemap_tile") return duplicateTilemapTile(arguments);
    if (name == "add_animation_action") return addAnimationAction(arguments);
    if (name == "remove_animation_action") return removeAnimationAction(arguments);
    if (name == "set_keyframe_times") return setKeyframeTimes(arguments);
    if (name == "set_keyframe_easing") return setKeyframeEasing(arguments);
    if (name == "set_keyframe_track") return setKeyframeTrack(arguments);
    if (name == "undo_editor") return undoEditor(arguments);
    if (name == "redo_editor") return redoEditor(arguments);

    return failResult("Unknown action.");
}

ActionResult EditorActionExecutor::getProjectSummary() {
    Json data;
    data["name"] = project->getName();
    data["path"] = FileUtils::pathToUtf8(project->getProjectPath());
    data["selected_scene_id"] = project->getSelectedSceneId();
    data["start_scene_id"] = project->getStartSceneId();
    data["assets_dir"] = FileUtils::pathToGenericUtf8(project->getAssetsDir());
    data["lua_dir"] = FileUtils::pathToGenericUtf8(project->getLuaDir());
    data["script_dirs"] = Json::array();
    for (const fs::path& scriptDir : project->getScriptDirs()) {
        data["script_dirs"].push_back(FileUtils::pathToGenericUtf8(scriptDir));
    }
    data["cxx_standard"] = project->getCxxStandard();
    data["canvas"] = {{"width", project->getCanvasWidth()}, {"height", project->getCanvasHeight()}};
    data["scaling_mode"] = Stream::scalingModeToString(project->getScalingMode());
    data["window"] = {{"width", project->getWindowWidth()}, {"height", project->getWindowHeight()}};
    data["loading"] = loadingSettingsJson(project->getLoadingSettings());
    data["standalone_bundles"] = Json::array();
    for (const fs::path& bundlePath : project->getStandaloneBundles()) {
        data["standalone_bundles"].push_back(FileUtils::pathToGenericUtf8(bundlePath));
    }
    data["scenes"] = Json::array();
    for (const auto& scene : project->getScenes()) {
        data["scenes"].push_back({
            {"id", scene.id},
            {"name", scene.name},
            {"type", sceneTypeName(scene.sceneType)},
            {"opened", scene.opened},
            {"modified", scene.isModified},
            {"main_camera", scene.mainCamera},
            {"filepath", FileUtils::pathToGenericUtf8(scene.filepath)},
            {"entity_count", scene.entities.size()},
            {"selected_entities", scene.selectedEntities.size()}
        });
    }
    return okResult("Read project summary.", data);
}

ActionResult EditorActionExecutor::searchResources(const Json& arguments) {
    const std::string query = lower(optionalString(arguments, "query"));
    const std::string wantedType = lower(optionalString(arguments, "type", "any"));
    const auto maxIt = arguments.find("max_results");
    int maxResults = (maxIt == arguments.end() || !maxIt->is_number_integer())
        ? 20 : maxIt->get<int>();
    maxResults = std::max(1, std::min(50, maxResults));

    const fs::path projectPath = project->getProjectPath();
    if (projectPath.empty() || !fs::exists(projectPath)) {
        return failResult("Project path does not exist.");
    }

    Json matches = Json::array();
    std::error_code ec;
    for (fs::recursive_directory_iterator it(projectPath, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec && static_cast<int>(matches.size()) < maxResults;
         it.increment(ec)) {
        const fs::path path = it->path();
        if (it->is_directory(ec)) {
            if (path.filename() == ".doriax" || path.filename() == "build") {
                it.disable_recursion_pending();
            }
            continue;
        }

        fs::path rel = fs::relative(path, projectPath, ec);
        if (ec) continue;
        const std::string relText = FileUtils::pathToGenericUtf8(rel);
        if (lower(relText).find(query) == std::string::npos) continue;
        const std::string type = fileTypeFromPath(rel);
        if (wantedType != "any" && !wantedType.empty() && type != wantedType) continue;

        matches.push_back({{"path", relText}, {"type", type}});
    }

    // The walk stops at maxResults, so a full page may be hiding more files.
    const bool truncated = static_cast<int>(matches.size()) >= maxResults;
    std::string summary = "Found " + std::to_string(matches.size()) + " resource(s).";
    if (truncated) {
        summary += " Result limit reached; narrow the query/type or raise max_results for more.";
    }
    return okResult(summary, Json{{"matches", matches}, {"truncated", truncated}});
}

ActionResult EditorActionExecutor::listSceneEntities(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return expectedMissResult("Scene not found.");
    }

    Json entities = Json::array();
    for (Entity entity : sceneProject->entities) {
        // Scripts can destroy entities during Play
        if (!sceneProject->scene->isEntityCreated(entity)) continue;
        Json components = Json::array();
        for (ComponentType type : Catalog::findComponents(sceneProject->scene, entity)) {
            components.push_back(Catalog::getComponentName(type, true));
        }
        entities.push_back({
            {"id", entity},
            {"name", sceneProject->scene->getEntityName(entity)},
            {"components", components}
        });
    }

    return okResult("Listed entities in scene " + sceneProject->name + ".",
                    Json{{"scene_id", sceneId}, {"scene_name", sceneProject->name}, {"entities", entities}});
}

ActionResult EditorActionExecutor::inspectEntity(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return expectedMissResult("Scene not found.");
    }

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) {
        return expectedMissResult("Entity not found.");
    }

    Json data = {
        {"scene_id", sceneId},
        {"entity_id", entity},
        {"name", sceneProject->scene->getEntityName(entity)},
        {"selected", project->isSelectedEntity(sceneId, entity)},
        {"in_bundle", project->isEntityInBundle(sceneId, entity)}
    };

    Transform* transform = sceneProject->scene->findComponent<Transform>(entity);
    if (transform) {
        data["transform"] = {
            {"position", vector3Json(transform->position)},
            {"rotation", quaternionJson(transform->rotation)},
            {"scale", vector3Json(transform->scale)},
            {"parent", transform->parent},
            {"visible", transform->visible}
        };
    }

    addMeshBounds(data, sceneProject->scene, entity);

    data["components"] = Json::array();
    const bool includeProperties = arguments.value("include_properties", false);
    for (ComponentType type : Catalog::findComponents(sceneProject->scene, entity)) {
        Json comp = {
            {"name", Catalog::getComponentName(type)},
            {"short_name", Catalog::getComponentName(type, true)}
        };
        if (includeProperties) {
            Json props = Json::object();
            for (const auto& [propName, prop] : Catalog::findEntityProperties(sceneProject->scene, entity, type)) {
                if (isUnusedSubmeshProperty(sceneProject->scene, entity, propName)) continue;
                props[propName] = {{"type", propertyTypeName(prop.type)}, {"value", propertyValueToJson(project, propName, prop)}};
            }
            comp["properties"] = props;
        }
        if (type == ComponentType::ScriptComponent) {
            comp["scripts"] = scriptEntriesJson(project, sceneProject->scene->getComponent<ScriptComponent>(entity).scripts);
        }
        data["components"].push_back(comp);
    }

    return okResult("Inspected entity.", data);
}

ActionResult EditorActionExecutor::inspectComponent(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return expectedMissResult("Scene not found.");
    }

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) {
        return expectedMissResult("Entity not found.");
    }

    ComponentType component;
    if (!parseComponentType(arguments.value("component", ""), component)) {
        return warningResult("Unknown component type.");
    }

    auto properties = Catalog::findEntityProperties(sceneProject->scene, entity, component);
    if (properties.empty()) {
        return expectedMissResult(
            "Component not found on entity or has no inspectable properties.");
    }

    Json props = Json::object();
    for (const auto& [propName, prop] : properties) {
        if (isUnusedSubmeshProperty(sceneProject->scene, entity, propName)) continue;
        props[propName] = {{"type", propertyTypeName(prop.type)}, {"value", propertyValueToJson(project, propName, prop)}};
    }

    Json data = {{"scene_id", sceneId},
                 {"entity_id", entity},
                 {"component", Catalog::getComponentName(component)},
                 {"properties", props}};

    if (component == ComponentType::MeshComponent) {
        addMeshBounds(data, sceneProject->scene, entity);
    }
    if (component == ComponentType::ScriptComponent) {
        data["scripts"] = scriptEntriesJson(project, sceneProject->scene->getComponent<ScriptComponent>(entity).scripts);
    }

    return okResult("Inspected component.", data);
}

ActionResult EditorActionExecutor::listComponentTypes() {
    Json components = Json::array();
    for (int i = static_cast<int>(ComponentType::Transform); i <= static_cast<int>(ComponentType::SkinComponent); ++i) {
        ComponentType type = static_cast<ComponentType>(i);
        std::string name = Catalog::getComponentName(type);
        if (!name.empty()) {
            components.push_back({{"name", name}, {"short_name", Catalog::getComponentName(type, true)}});
        }
    }
    return okResult("Listed component types.", Json{{"components", components}});
}

ActionResult EditorActionExecutor::searchEngineApi(const Json& arguments) {
    const std::string query = arguments.value("query", "");
    const std::string parent = arguments.value("parent", "");
    int maxResults = arguments.value("max_results", 16);
    maxResults = std::max(1, std::min(50, maxResults));

    Json data = AiEngineApiContext::search(query, parent, maxResults);
    return okResult("Searched engine API.", data);
}

ActionResult EditorActionExecutor::createEntity(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return failResult("Scene not found.");
    }

    EntityCreationType type = EntityCreationType::EMPTY;
    if (!parseEntityType(arguments.value("type", "empty"), type)) {
        return failResult("Unsupported entity type: " + arguments.value("type", ""));
    }

    Entity parent = NULL_ENTITY;
    if (arguments.contains("parent_id")) {
        parent = resolveEntityByKeys(sceneProject, arguments, "parent_id", "parent_name");
        if (parent == NULL_ENTITY) return failResult("Parent entity not found.");
        if (!sceneProject->scene->findComponent<Transform>(parent)) {
            return failResult("Parent entity has no Transform. Create it as type 'object' instead of 'empty'.");
        }
    }

    auto* cmd = parent != NULL_ENTITY
        ? new CreateEntityCmd(project, sceneId, arguments.value("name", "Entity"), type, parent)
        : new CreateEntityCmd(project, sceneId, arguments.value("name", "Entity"), type);
    if (arguments.contains("position")) {
        Vector3 position = Vector3::ZERO;
        if (parseVector3(arguments["position"], position)) {
            cmd->addProperty<Vector3>(ComponentType::Transform, "position", position);
        }
    }
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(cmd)) {
        return failResult("Failed to create the entity.");
    }

    return okResult("Created entity through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", cmd->getEntity()}});
}

ActionResult EditorActionExecutor::setEntityTransform(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return failResult("Scene not found.");
    }

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) {
        return failResult("Entity not found.");
    }

    Transform* transform = sceneProject->scene->findComponent<Transform>(entity);
    if (!transform) {
        return failResult("Entity has no Transform component.");
    }

    Vector3 position = transform->position;
    Quaternion rotation = transform->rotation;
    Vector3 scale = transform->scale;
    if (arguments.contains("position")) parseVector3(arguments["position"], position);
    if (arguments.contains("rotation") && !parseRotation(arguments["rotation"], rotation)) {
        return failResult("rotation must be a unit quaternion, not Euler angles.");
    }
    if (arguments.contains("scale")) parseVector3(arguments["scale"], scale);

    CommandHandle::get(sceneId)->addCommandNoMerge(
        new ObjectTransformCmd(project, sceneId, entity, position, rotation, scale));

    return okResult("Updated entity transform through the command history.");
}

ActionResult EditorActionExecutor::renameEntity(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    CommandHandle::get(sceneId)->addCommandNoMerge(new EntityNameCmd(project, sceneId, entity, arguments.value("new_name", "")));
    return okResult("Renamed entity through the command history.");
}

ActionResult EditorActionExecutor::deleteEntity(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new DeleteEntityCmd(project, sceneId, entity))) {
        return failResult(kRefusedCommand);
    }
    return okResult("Deleted entity through the command history.");
}

ActionResult EditorActionExecutor::duplicateEntity(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    auto* cmd = new DuplicateEntityCmd(project, sceneId, std::vector<Entity>{entity});
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(cmd)) {
        return failResult("Failed to duplicate the entity.");
    }

    Json created = Json::array();
    for (Entity createdEntity : cmd->getCreatedEntities()) created.push_back(createdEntity);
    return okResult("Duplicated entity through the command history.", Json{{"created_entities", created}});
}

ActionResult EditorActionExecutor::reparentEntity(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    Entity parent = NULL_ENTITY;
    if (arguments.contains("parent_id") || arguments.contains("parent_name")) {
        parent = resolveEntityByKeys(sceneProject, arguments, "parent_id", "parent_name");
        if (parent == NULL_ENTITY && arguments.value("parent_id", 0) != 0) return failResult("Parent entity not found.");
    }

    if (parent != NULL_ENTITY) {
        if (!sceneProject->scene->findComponent<Transform>(entity) || !sceneProject->scene->findComponent<Transform>(parent)) {
            return failResult("Both entities need a Transform to be parented.");
        }
        if (!CommandHandle::get(sceneId)->addCommandNoMerge(new MoveEntityOrderCmd(project, sceneId, entity, parent, InsertionType::INTO))) {
            return failResult(kRefusedCommand);
        }
        return okResult("Reparented entity through the command history.");
    }

    Entity rootTarget = NULL_ENTITY;
    for (Entity candidate : sceneProject->entities) {
        if (candidate != entity && sceneProject->scene->findComponent<Transform>(candidate)) {
            Transform* transform = sceneProject->scene->findComponent<Transform>(candidate);
            if (transform && transform->parent == NULL_ENTITY) {
                rootTarget = candidate;
                break;
            }
        }
    }
    if (rootTarget == NULL_ENTITY) {
        return failResult("Cannot move to scene root because there is no root-level target to order against.");
    }
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new MoveEntityOrderCmd(project, sceneId, entity, rootTarget, InsertionType::BEFORE))) {
        return failResult(kRefusedCommand);
    }
    return okResult("Moved entity to scene root through the command history.");
}

ActionResult EditorActionExecutor::moveEntityOrder(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    Entity target = resolveEntityByKeys(sceneProject, arguments, "target_id", "target_name");
    if (entity == NULL_ENTITY || target == NULL_ENTITY) return failResult("Entity or target not found.");

    std::string insertion = lower(arguments.value("insertion", ""));
    InsertionType type = InsertionType::AFTER;
    if (insertion == "before") type = InsertionType::BEFORE;
    else if (insertion == "after") type = InsertionType::AFTER;
    else if (insertion == "child_first" || insertion == "child_last" || insertion == "into") type = InsertionType::INTO;
    else return failResult("Unsupported insertion. Use before, after, child_first, or child_last.");

    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new MoveEntityOrderCmd(project, sceneId, entity, target, type))) {
        return failResult(kRefusedCommand);
    }
    return okResult("Moved entity order through the command history.");
}

ActionResult EditorActionExecutor::selectEntities(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    if (arguments.value("clear", false)) {
        project->clearSelectedEntities(sceneId);
        return okResult("Cleared selection.");
    }

    std::vector<Entity> selected;
    if (arguments.contains("entity_ids") && arguments["entity_ids"].is_array()) {
        for (const Json& idNode : arguments["entity_ids"]) {
            Json tmp = Json::object();
            tmp["entity_id"] = idNode;
            Entity entity = resolveEntity(sceneProject, tmp);
            if (entity == NULL_ENTITY) return failResult("Entity not found in entity_ids.");
            selected.push_back(entity);
        }
    } else if (arguments.contains("entity_names") && arguments["entity_names"].is_array()) {
        for (const Json& nameNode : arguments["entity_names"]) {
            Json tmp = Json::object();
            tmp["entity_name"] = nameNode;
            Entity entity = resolveEntity(sceneProject, tmp);
            if (entity == NULL_ENTITY) return failResult("Entity not found in entity_names.");
            selected.push_back(entity);
        }
    } else {
        Entity entity = resolveEntity(sceneProject, arguments);
        if (entity == NULL_ENTITY) return failResult("Entity not found.");
        selected.push_back(entity);
    }

    if (selected.empty()) return failResult("No entities to select.");
    project->clearSelectedEntities(sceneId);
    for (Entity entity : selected) {
        project->addSelectedEntity(sceneId, entity);
    }

    Json ids = Json::array();
    for (Entity entity : selected) ids.push_back(entity);
    return okResult("Updated entity selection.", Json{{"scene_id", sceneId}, {"entity_ids", ids}});
}

ActionResult EditorActionExecutor::addComponent(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    ComponentType component;
    if (!parseComponentType(arguments.value("component", ""), component)) return failResult("Unknown component type.");
    if (hasComponent(sceneProject->scene, entity, component)) {
        return failResult("Entity already has component.");
    }
    CommandHandle::get(sceneId)->addCommandNoMerge(new AddComponentCmd(project, sceneId, entity, component));
    return okResult("Added component through the command history.");
}

ActionResult EditorActionExecutor::removeComponent(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    ComponentType component;
    if (!parseComponentType(arguments.value("component", ""), component)) return failResult("Unknown component type.");
    if (!hasComponent(sceneProject->scene, entity, component)) {
        return failResult("Entity does not have component.");
    }
    CommandHandle::get(sceneId)->addCommandNoMerge(new RemoveComponentCmd(project, sceneId, entity, component));
    return okResult("Removed component through the command history.");
}

ActionResult EditorActionExecutor::addBody3DShape(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    Shape3D shape;
    if (!parseBodyShape3DType(arguments.value("shape_type", ""), shape.type)) {
        return failResult("Unsupported shape_type. Use box, sphere, capsule, tapered_capsule, cylinder, convex_hull, or mesh.");
    }

    MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entity);

    if (isMeshDerivedShape3DType(shape.type)) {
        if (!meshComp) {
            return failResult("convex_hull and mesh shapes derive their geometry from the entity's mesh.");
        }
        shape.source = Shape3DSource::ENTITY_MESH;
        shape.sourceEntity = entity;
    }

    // A caller cannot see the mesh's local dimensions, so fit the shape to the measured bounds
    // first and let any explicit argument below override a single axis of that.
    const bool fitExplicit = arguments.contains("fit_to_mesh");
    if (fitExplicit && !arguments["fit_to_mesh"].is_boolean()) {
        return failResult("fit_to_mesh must be a boolean.");
    }

    bool fitted = false;
    if (arguments.value("fit_to_mesh", true) && !isMeshDerivedShape3DType(shape.type)) {
        Transform* transform = sceneProject->scene->findComponent<Transform>(entity);
        fitted = meshComp && transform && fitShapeToMeshBounds(meshComp->aabb, transform->worldScale, shape);
        if (!fitted && fitExplicit) {
            return failResult("fit_to_mesh needs an entity mesh with measurable bounds.");
        }
    }

    std::string error;
    if (!readPositiveFloat(arguments, "width", shape.width, error) ||
        !readPositiveFloat(arguments, "height", shape.height, error) ||
        !readPositiveFloat(arguments, "depth", shape.depth, error) ||
        !readPositiveFloat(arguments, "radius", shape.radius, error) ||
        !readPositiveFloat(arguments, "half_height", shape.halfHeight, error) ||
        !readPositiveFloat(arguments, "top_radius", shape.topRadius, error) ||
        !readPositiveFloat(arguments, "bottom_radius", shape.bottomRadius, error) ||
        !readPositiveFloat(arguments, "density", shape.density, error)) {
        return failResult(error);
    }
    if (arguments.contains("position") && !parseVector3(arguments["position"], shape.position)) {
        return failResult("position must be a Vector3 object.");
    }
    if (arguments.contains("rotation") && !parseRotation(arguments["rotation"], shape.rotation)) {
        return failResult("rotation must be a unit quaternion, not Euler angles.");
    }

    BodyType requestedBodyType = BodyType::STATIC;
    if (arguments.contains("body_type")) {
        if (!arguments["body_type"].is_string() ||
            !parseBodyType(arguments["body_type"].get<std::string>(), requestedBodyType)) {
            return failResult("body_type must be static, kinematic, or dynamic.");
        }
    }

    Body3DMotionQuality requestedMotionQuality = Body3DMotionQuality::DISCRETE;
    if (arguments.contains("motion_quality")) {
        if (!arguments["motion_quality"].is_string() ||
            !parseBody3DMotionQuality(arguments["motion_quality"].get<std::string>(), requestedMotionQuality)) {
            return failResult("motion_quality must be discrete or linear_cast.");
        }
    }

    Body3DComponent* body = sceneProject->scene->findComponent<Body3DComponent>(entity);
    const size_t shapeIndex = body ? body->numShapes : 0;

    auto* multiCmd = new MultiPropertyCmd();
    if (!body) {
        multiCmd->addCommand(std::make_unique<AddComponentCmd>(project, sceneId, entity, ComponentType::Body3DComponent));
    }
    if (arguments.contains("body_type")) {
        multiCmd->addPropertyCmd<BodyType>(project, sceneId, entity, ComponentType::Body3DComponent,
                                           "type", requestedBodyType);
    }
    if (arguments.contains("motion_quality")) {
        multiCmd->addPropertyCmd<Body3DMotionQuality>(project, sceneId, entity, ComponentType::Body3DComponent,
                                                       "motionQuality", requestedMotionQuality);
    }
    multiCmd->addPropertyCmd<Shape3D>(project, sceneId, entity, ComponentType::Body3DComponent,
                                      "shapes[" + std::to_string(shapeIndex) + "]", shape);
    multiCmd->addPropertyCmd<size_t>(project, sceneId, entity, ComponentType::Body3DComponent,
                                     "numShapes", shapeIndex + 1);
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);

    // fitted_to_mesh is the one result the caller cannot infer from its own arguments
    return okResult("Added a Body3D shape through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity},
                         {"body_added", body == nullptr}, {"shape_index", shapeIndex},
                         {"shape_type", arguments.value("shape_type", "")},
                         {"fitted_to_mesh", fitted}});
}

ActionResult EditorActionExecutor::addBody2DShape(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    const std::string shapeName = lower(arguments.value("shape_type", ""));
    Shape2D shape;
    if (!parseShape2DType(shapeName, shape.type)) {
        return failResult("Unsupported shape_type. Use box, circle, capsule, segment, polygon, or chain.");
    }

    std::string error;
    if (!readNonNegativeFloat(arguments, "density", shape.density, error) ||
        !readNonNegativeFloat(arguments, "friction", shape.friction, error) ||
        !readNonNegativeFloat(arguments, "restitution", shape.restitution, error)) {
        return failResult(error);
    }
    if (shape.restitution > 1.0f) return failResult("restitution must be between 0 and 1.");
    if (arguments.contains("center") && !parseVector2(arguments["center"], shape.pointA)) {
        return failResult("center must be a Vector2 object.");
    }
    if (arguments.contains("point_a") && !parseVector2(arguments["point_a"], shape.pointA)) {
        return failResult("point_a must be a Vector2 object.");
    }
    if (arguments.contains("point_b") && !parseVector2(arguments["point_b"], shape.pointB)) {
        return failResult("point_b must be a Vector2 object.");
    }

    if (shapeName == "box") {
        float width = 0.0f;
        float height = 0.0f;
        if (!readPositiveFloat(arguments, "width", width, error) ||
            !readPositiveFloat(arguments, "height", height, error)) {
            return failResult(error);
        }
        if (!arguments.contains("width") || !arguments.contains("height")) {
            return failResult("box requires width and height.");
        }
        const float halfWidth = width * 0.5f;
        const float halfHeight = height * 0.5f;
        shape.numVertices = 4;
        shape.vertices[0] = shape.pointA + Vector2(-halfWidth, -halfHeight);
        shape.vertices[1] = shape.pointA + Vector2(halfWidth, -halfHeight);
        shape.vertices[2] = shape.pointA + Vector2(halfWidth, halfHeight);
        shape.vertices[3] = shape.pointA + Vector2(-halfWidth, halfHeight);
    } else if (shapeName == "circle") {
        if (!readPositiveFloat(arguments, "radius", shape.radius, error)) return failResult(error);
        if (!arguments.contains("radius")) return failResult("circle requires radius.");
    } else if (shapeName == "capsule") {
        if (!readPositiveFloat(arguments, "radius", shape.radius, error)) return failResult(error);
        if (!arguments.contains("radius") || !arguments.contains("point_a") || !arguments.contains("point_b")) {
            return failResult("capsule requires radius, point_a, and point_b.");
        }
        if (shape.pointA == shape.pointB) return failResult("capsule point_a and point_b must differ.");
    } else if (shapeName == "segment") {
        if (!arguments.contains("point_a") || !arguments.contains("point_b")) {
            return failResult("segment requires point_a and point_b.");
        }
        if (shape.pointA == shape.pointB) return failResult("segment point_a and point_b must differ.");
    } else {
        if (!arguments.contains("points") || !parseVector2Array(arguments["points"], shape.vertices, shape.numVertices, error)) {
            return failResult(arguments.contains("points") ? error : shapeName + " requires points.");
        }
        const uint8_t minPoints = shapeName == "polygon" ? 3 : 4;
        if (shape.numVertices < minPoints) {
            return failResult(shapeName + " requires at least " + std::to_string(minPoints) + " points.");
        }
        shape.loop = arguments.value("loop", false);
    }

    BodyType requestedBodyType = BodyType::STATIC;
    if (arguments.contains("body_type")) {
        if (!arguments["body_type"].is_string() ||
            !parseBodyType(arguments["body_type"].get<std::string>(), requestedBodyType)) {
            return failResult("body_type must be static, kinematic, or dynamic.");
        }
    }

    Body2DComponent* body = sceneProject->scene->findComponent<Body2DComponent>(entity);
    const size_t shapeIndex = body ? body->numShapes : 0;

    auto* multiCmd = new MultiPropertyCmd();
    if (!body) {
        multiCmd->addCommand(std::make_unique<AddComponentCmd>(project, sceneId, entity, ComponentType::Body2DComponent));
    }
    if (arguments.contains("body_type")) {
        multiCmd->addPropertyCmd<BodyType>(project, sceneId, entity, ComponentType::Body2DComponent,
                                           "type", requestedBodyType);
    }
    multiCmd->addPropertyCmd<Shape2D>(project, sceneId, entity, ComponentType::Body2DComponent,
                                      "shapes[" + std::to_string(shapeIndex) + "]", shape);
    multiCmd->addPropertyCmd<size_t>(project, sceneId, entity, ComponentType::Body2DComponent,
                                     "numShapes", shapeIndex + 1);
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);

    return okResult("Added a Body2D shape through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity},
                         {"body_added", body == nullptr}, {"shape_index", shapeIndex},
                         {"shape_type", shapeName}});
}

ActionResult EditorActionExecutor::setComponentProperty(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    ComponentType component;
    if (!parseComponentType(arguments.value("component", ""), component)) return failResult("Unknown component type.");

    auto properties = Catalog::findEntityProperties(sceneProject->scene, entity, component);
    const std::string propertyName = arguments.value("property", "");
    auto it = properties.find(propertyName);
    if (it == properties.end() || !it->second.ref) return failResult("Property not found or not editable.");

    std::string error;
    if (!validateCustomShaderValue(propertyName, arguments, error)) return failResult(error);

    std::unique_ptr<Command> cmd(buildPropertyCommand(project, sceneId, entity, component, propertyName, it->second, arguments, error));
    if (!cmd) return failResult(error);
    CommandHandle::get(sceneId)->addCommandNoMerge(cmd.release());
    return okResult("Set component property through the command history.");
}

ActionResult EditorActionExecutor::setTextureSettings(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    ComponentType component;
    if (!parseComponentType(arguments.value("component", ""), component)) return failResult("Unknown component type.");

    auto properties = Catalog::findEntityProperties(sceneProject->scene, entity, component);
    const std::string propertyName = arguments.value("property", "");
    auto it = properties.find(propertyName);
    if (it == properties.end() || !it->second.ref) return failResult("Property not found or not editable.");
    if (it->second.type != PropertyType::Texture) return failResult("Property is not a Texture.");

    Texture* current = static_cast<Texture*>(it->second.ref);
    Texture modified = *current;
    std::string error;

    // Reject unknown enum strings instead of silently defaulting: the Stream converters
    // fall back to linear/repeat, so a value only counts when it round-trips.
    auto applyFilter = [&](const char* field, bool magnification) -> bool {
        if (!arguments.contains(field)) return true;
        if (!arguments[field].is_string()) {
            error = std::string(field) + " must be a string.";
            return false;
        }
        const std::string value = arguments[field].get<std::string>();
        TextureFilter filter = Stream::stringToTextureFilter(value);
        if (Stream::textureFilterToString(filter) != value) {
            error = "Invalid " + std::string(field) + ": " + value;
            return false;
        }
        if (magnification && filter != TextureFilter::NEAREST && filter != TextureFilter::LINEAR) {
            error = "mag_filter accepts only nearest or linear.";
            return false;
        }
        if (magnification) modified.setMagFilter(filter); else modified.setMinFilter(filter);
        return true;
    };
    auto applyWrap = [&](const char* field, bool vertical) -> bool {
        if (!arguments.contains(field)) return true;
        if (!arguments[field].is_string()) {
            error = std::string(field) + " must be a string.";
            return false;
        }
        const std::string value = arguments[field].get<std::string>();
        TextureWrap wrap = Stream::stringToTextureWrap(value);
        if (Stream::textureWrapToString(wrap) != value) {
            error = "Invalid " + std::string(field) + ": " + value;
            return false;
        }
        if (vertical) modified.setWrapV(wrap); else modified.setWrapU(wrap);
        return true;
    };

    if (!applyFilter("min_filter", false)) return failResult(error);
    if (!applyFilter("mag_filter", true)) return failResult(error);
    if (!applyWrap("wrap_u", false)) return failResult(error);
    if (!applyWrap("wrap_v", true)) return failResult(error);

    if (arguments.contains("svg_scale")) {
        if (!arguments["svg_scale"].is_number()) return failResult("svg_scale must be a number.");
        float scale = arguments["svg_scale"].get<float>();
        if (!(scale > 0.0f)) return failResult("svg_scale must be greater than zero.");
        if (!TextureData::hasSvgExtension(modified.getPath().c_str())) {
            return failResult("svg_scale only applies to .svg texture sources.");
        }
        modified.setSvgScale(scale);
    }

    if (modified == *current) {
        return okResult("Texture settings already match the requested values.");
    }

    CommandHandle::get(sceneId)->addCommandNoMerge(new PropertyCmd<Texture>(
        project, sceneId, entity, component, propertyName, modified));
    return okResult("Texture settings applied through the command history.");
}

ActionResult EditorActionExecutor::addProjectScene(const Json& arguments) {
    std::string error;
    fs::path sceneRel;
    if (!safeRelativePath(project, arguments, "scene_path", sceneRel, error, true)) return failResult(error);
    if (!Util::isSceneFile(sceneRel.string())) return failResult("scene_path must point to a .scene file.");
    if (project->isAnyScenePlaying()) {
        return failResult(busyError(project, "changing the project scenes"));
    }

    const uint32_t existingSceneId = project->findSceneByPath(sceneRel);
    if (existingSceneId != NULL_PROJECT_SCENE) {
        return okResult("Scene is already part of the project.",
                        Json{{"scene_path", FileUtils::pathToGenericUtf8(sceneRel)}, {"scene_id", existingSceneId}});
    }

    if (!fs::exists(project->getProjectPath() / sceneRel)) {
        return failResult("scene_path does not exist.");
    }

    // Closed, so it is listed without loading its render data
    project->loadScene(sceneRel, false, true, false);

    const uint32_t sceneId = project->findSceneByPath(sceneRel);
    if (sceneId == NULL_PROJECT_SCENE) {
        return failResult("Failed to add the scene to the project.");
    }

    project->saveProjectFile();
    const SceneProject* sceneProject = project->getScene(sceneId);
    return okResult("Added the scene to the project.",
                    Json{{"scene_path", FileUtils::pathToGenericUtf8(sceneRel)},
                         {"scene_id", sceneId},
                         {"name", sceneProject ? sceneProject->name : std::string()}});
}

ActionResult EditorActionExecutor::createScene(const Json& arguments) {
    SceneType type;
    if (!parseSceneType(arguments.value("type", ""), type)) {
        return failResult("Unsupported scene type. Use 3d, 2d, or ui.");
    }

    if (project->isAnyScenePlaying()) {
        return failResult(busyError(project, "creating a scene"));
    }
    // createNewScene would ask about them in a dialog
    if (project->hasSceneUnsavedChanges(project->getSelectedSceneId())) {
        return failResult("The selected scene or one of its child scenes has unsaved changes. Save it first with save_scene, passing path if it has no file yet.");
    }

    const std::string requestedName = arguments.value("name", "Scene");
    project->createNewScene(requestedName, type);
    const uint32_t sceneId = project->getSelectedSceneId();
    SceneProject* created = project->getScene(sceneId);
    const std::string actualName = created ? created->name : requestedName;
    return okResult("Created new scene.", Json{{"scene_id", sceneId},
                                               {"name", actualName},
                                               {"type", sceneTypeName(type)}});
}

ActionResult EditorActionExecutor::renameScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) return failResult("Scene not found.");

    CommandHandle::get(sceneId)->addCommandNoMerge(new SceneNameCmd(project, sceneId, arguments.value("new_name", "")));
    return okResult("Renamed scene through the command history.");
}

ActionResult EditorActionExecutor::saveScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) return failResult("Scene not found.");
    if (sceneProject->playState != ScenePlayState::STOPPED) {
        return failResult(busyError(project, "saving the scene"));
    }

    if (arguments.contains("path") && arguments["path"].is_string()) {
        std::string error;
        fs::path rel;
        if (!safeRelativePath(project, arguments, "path", rel, error)) return failResult(error);
        if (rel.extension() != ".scene") return failResult("path must be a .scene file.");
        if (!sceneProject->filepath.empty() && sceneProject->filepath.lexically_normal() != rel) {
            return failResult("The scene is already saved as " + FileUtils::pathToGenericUtf8(sceneProject->filepath) + "; save it without path.");
        }
        if (sceneProject->filepath.empty()) {
            const fs::path fullPath = project->getProjectPath() / rel;
            if (fs::exists(fullPath)) {
                return failResult("A file already exists at " + FileUtils::pathToGenericUtf8(rel) + ".");
            }
            std::error_code ec;
            fs::create_directories(fullPath.parent_path(), ec);
            // saveScene writes it there, like the save dialog
            sceneProject->filepath = rel;
        }
    }

    project->saveScene(sceneId);
    if (sceneProject->filepath.empty()) {
        return warningResult("The scene has no file yet, so the editor asked the user where to save it. Pass path to save it directly.");
    }
    return okResult("Saving scene.", Json{{"scene_id", sceneId}, {"path", FileUtils::pathToGenericUtf8(sceneProject->filepath)}});
}

ActionResult EditorActionExecutor::saveAllScenes() {
    project->saveAllScenes();
    return okResult("Requested save for all modified scenes.");
}

ActionResult EditorActionExecutor::setStartScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    if (!project->getScene(sceneId)) return failResult("Scene not found.");

    project->setStartSceneId(sceneId);
    project->saveProjectFile();
    return okResult("Set project startup scene.", Json{{"scene_id", sceneId}});
}

ActionResult EditorActionExecutor::setSceneProperty(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    const std::string property = arguments.value("property", "");
    Command* cmd = nullptr;
    if (property == "background_color") {
        Vector4 value = Catalog::getSceneProperty<Vector4>(sceneProject->scene, property);
        if (!parseVector4(arguments.value("vector4_value", Json::object()), value)) {
            return failResult("background_color requires vector4_value.");
        }
        cmd = new ScenePropertyCmd<Vector4>(project, sceneId, property, value);
    } else if (property == "global_illumination_color" || property == "ambient_light_2d_color") {
        Vector3 value = Catalog::getSceneProperty<Vector3>(sceneProject->scene, property);
        if (!parseVector3(arguments.value("vector3_value", Json::object()), value)) {
            return failResult(property + " requires vector3_value.");
        }
        cmd = new ScenePropertyCmd<Vector3>(project, sceneId, property, value);
    } else if (property == "physics_gravity_2d") {
        Vector2 value = Catalog::getSceneProperty<Vector2>(sceneProject->scene, property);
        if (!parseVector2(arguments.value("vector2_value", Json::object()), value)) {
            return failResult("physics_gravity_2d requires vector2_value.");
        }
        cmd = new ScenePropertyCmd<Vector2>(project, sceneId, property, value);
    } else if (property == "physics_gravity_3d") {
        Vector3 value = Catalog::getSceneProperty<Vector3>(sceneProject->scene, property);
        if (!parseVector3(arguments.value("vector3_value", Json::object()), value)) {
            return failResult("physics_gravity_3d requires vector3_value.");
        }
        cmd = new ScenePropertyCmd<Vector3>(project, sceneId, property, value);
    } else if (property == "ssao_enabled" || property == "mesh_lod_enabled" || property == "depth_prepass_enabled" ||
               property == "ssao_debug" || property == "ssr_enabled" ||
               property == "fixed_resolution_enabled") {
        if (!arguments.contains("bool_value") || !arguments["bool_value"].is_boolean()) {
            return failResult(property + " requires bool_value.");
        }
        cmd = new ScenePropertyCmd<bool>(project, sceneId, property, arguments["bool_value"].get<bool>());
    } else if (property == "global_illumination_intensity" || property == "ambient_light_2d_intensity" ||
               property == "ssao_radius" || property == "mesh_lod_threshold" ||
               property == "ssao_intensity" || property == "ssao_bias" ||
               property == "ssr_max_distance" || property == "ssr_thickness" ||
               property == "ssr_intensity" || property == "ssr_blur") {
        if (!arguments.contains("number_value") || !arguments["number_value"].is_number()) {
            return failResult(property + " requires number_value.");
        }
        cmd = new ScenePropertyCmd<float>(project, sceneId, property, arguments["number_value"].get<float>());
    } else if (property == "ssr_max_steps" || property == "ssr_debug_mode" ||
               property == "fixed_resolution_width" || property == "fixed_resolution_height") {
        if (!arguments.contains("int_value") || !arguments["int_value"].is_number_integer()) {
            return failResult(property + " requires int_value.");
        }
        cmd = new ScenePropertyCmd<int>(project, sceneId, property, arguments["int_value"].get<int>());
    } else if (property == "fixed_resolution_filter") {
        if (!arguments.contains("int_value") || !arguments["int_value"].is_number_integer()) {
            return failResult("fixed_resolution_filter requires int_value (0=nearest, 1=linear).");
        }
        const int filterValue = arguments["int_value"].get<int>();
        if (filterValue != 0 && filterValue != 1) {
            return failResult("fixed_resolution_filter must be 0 (nearest) or 1 (linear).");
        }
        cmd = new ScenePropertyCmd<TextureFilter>(project, sceneId, property,
                                                  filterValue == 1 ? TextureFilter::LINEAR : TextureFilter::NEAREST);
    } else if (property == "light_state") {
        if (!arguments.contains("int_value") || !arguments["int_value"].is_number_integer()) {
            return failResult("light_state requires int_value.");
        }
        cmd = new ScenePropertyCmd<LightState>(project, sceneId, property,
                                               static_cast<LightState>(arguments["int_value"].get<int>()));
    } else if (property == "shadows_quality" || property == "shadows_2d_quality") {
        if (!arguments.contains("int_value") || !arguments["int_value"].is_number_integer()) {
            return failResult(property + " requires int_value (0=none, 1=low, 2=medium, 3=high).");
        }
        cmd = new ScenePropertyCmd<ShadowQuality>(project, sceneId, property,
                                                  static_cast<ShadowQuality>(arguments["int_value"].get<int>()));
    } else if (property == "default_mesh_shader" || property == "default_ui_shader" ||
               property == "default_sky_shader" || property == "default_points_shader" ||
               property == "default_lines_shader" || property == "default_water_shader") {
        if (!arguments.contains("string_value") || !arguments["string_value"].is_string()) {
            return failResult(property + " requires string_value (custom shader base path, empty for built-in).");
        }
        std::string error;
        if (!validateCustomShaderValue(property, arguments, error)) return failResult(error);
        cmd = new ScenePropertyCmd<std::string>(project, sceneId, property, arguments["string_value"].get<std::string>());
    } else {
        return failResult("Unsupported scene property: " + property);
    }

    CommandHandle::get(sceneId)->addCommandNoMerge(cmd);
    return okResult("Set scene property through the command history.");
}

ActionResult EditorActionExecutor::controlPlayMode(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    if (!project->getScene(sceneId)) return failResult("Scene not found.");

    const std::string action = lower(arguments.value("action", ""));
    if (action == "start" && !project->getScene(sceneId)->scene) return failResult("Scene is not open. Use open_scene first.");

    if (action == "start") project->start(sceneId);
    else if (action == "pause") project->pause(sceneId);
    else if (action == "resume") project->resume(sceneId);
    else if (action == "stop") project->stop(sceneId);
    else return failResult("Unsupported play mode action. Use start, pause, resume, or stop.");

    return okResult("Sent play mode command.", Json{{"scene_id", sceneId}, {"action", action}});
}

ActionResult EditorActionExecutor::addChildScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    const uint32_t childSceneId = static_cast<uint32_t>(arguments.value("child_scene_id", 0));
    if (!project->getScene(sceneId) || !project->getScene(childSceneId)) return failResult("Parent or child scene not found.");
    if (!project->getScene(sceneId)->scene) return failResult(kClosedParentScene);
    if (sceneId == childSceneId) return failResult("A scene cannot be its own child.");
    if (project->wouldCreateChildSceneCycle(sceneId, childSceneId)) return failResult("The child scene already contains this scene, which would create a cycle.");
    if (project->hasChildScene(sceneId, childSceneId)) return failResult("Child scene is already attached to this scene.");

    const bool startActive = arguments.value("start_active", true);
    if (startActive) {
        CommandHandle::get(sceneId)->addCommandNoMerge(new AddChildSceneCmd(project, sceneId, childSceneId));
    } else {
        auto* multiCmd = new MultiPropertyCmd();
        multiCmd->addCommand(std::make_unique<AddChildSceneCmd>(project, sceneId, childSceneId));
        multiCmd->addCommand(std::make_unique<SetChildSceneStartActiveCmd>(project, sceneId, childSceneId, false));
        CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);
    }
    return okResult("Added child scene through the command history.");
}

ActionResult EditorActionExecutor::removeChildScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    const uint32_t childSceneId = static_cast<uint32_t>(arguments.value("child_scene_id", 0));
    if (!project->getScene(sceneId)) return failResult("Scene not found.");
    if (!project->getScene(sceneId)->scene) return failResult(kClosedParentScene);
    if (!project->hasChildScene(sceneId, childSceneId)) return failResult("Child scene is not attached to this scene.");

    CommandHandle::get(sceneId)->addCommandNoMerge(new RemoveChildSceneCmd(project, sceneId, childSceneId));
    return okResult("Removed child scene through the command history.");
}

ActionResult EditorActionExecutor::setChildSceneStartActive(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    const uint32_t childSceneId = static_cast<uint32_t>(arguments.value("child_scene_id", 0));
    if (!project->getScene(sceneId)) return failResult("Scene not found.");
    if (!project->getScene(sceneId)->scene) return failResult(kClosedParentScene);
    if (!project->hasChildScene(sceneId, childSceneId)) return failResult("Child scene is not attached to this scene.");

    CommandHandle::get(sceneId)->addCommandNoMerge(
        new SetChildSceneStartActiveCmd(project, sceneId, childSceneId, arguments.value("start_active", true)));
    return okResult("Updated child scene Start active through the command history.");
}

ActionResult EditorActionExecutor::loadChildSceneInline(const Json& arguments) {
    const uint32_t childSceneId = static_cast<uint32_t>(arguments.value("child_scene_id", 0));
    if (!project->getScene(childSceneId)) return failResult("Child scene not found.");

    const bool load = arguments.value("load", true);
    if (load) {
        if (!project->loadChildSceneInline(childSceneId)) return failResult("Failed to load child scene inline.");
    } else {
        project->unloadChildSceneInline(childSceneId);
    }
    return okResult(load ? "Loaded child scene inline." : "Unloaded child scene inline.");
}

ActionResult EditorActionExecutor::createFolder(const Json& arguments) {
    fs::path parentRel = FileUtils::pathFromUtf8(arguments.value("parent_path", std::string()));
    fs::path parentFull = project->getProjectPath();
    if (!parentRel.empty()) {
        parentRel = parentRel.lexically_normal();
        if (!PathUtils::isSafeRelativePath(parentRel)) {
            return failResult("parent_path must be a safe project-relative directory.");
        }
        parentFull /= parentRel;
    }
    if (!fs::exists(parentFull) || !fs::is_directory(parentFull)) {
        return failResult("Parent directory does not exist.");
    }

    const std::string name = FileUtils::pathFromUtf8(arguments.value("name", "")).string();
    if (name.empty() || fs::path(name).filename().string() != name) {
        return failResult("Folder name must be a single safe path segment.");
    }
    project->getProjectCommandHistory()->addCommandNoMerge(new CreateDirCmd(name, parentFull.string()));
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    return okResult("Created folder through the project command history.");
}

ActionResult EditorActionExecutor::renameResource(const Json& arguments) {
    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "path", rel, error, true)) return failResult(error);

    const std::string newName = FileUtils::pathFromUtf8(arguments.value("new_name", "")).string();
    if (newName.empty() || fs::path(newName).filename().string() != newName) {
        return failResult("new_name must be a single filename, not a path.");
    }

    fs::path full = project->getProjectPath() / rel;
    project->getProjectCommandHistory()->addCommandNoMerge(
        new RenameFileCmd(project, rel.filename().string(), newName, full.parent_path().string()));
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    return okResult("Renamed resource through the project command history.");
}

ActionResult EditorActionExecutor::deleteResource(const Json& arguments) {
    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "path", rel, error, true)) return failResult(error);
    if (rel.empty() || rel == ".doriax" || rel.begin()->string() == ".doriax") {
        return failResult("Refusing to delete internal project metadata.");
    }

    fs::path full = project->getProjectPath() / rel;
    project->getProjectCommandHistory()->addCommandNoMerge(
        new DeleteFileCmd(project, std::vector<fs::path>{full}, project->getProjectPath()));
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    return okResult("Deleted resource through the project command history.");
}

ActionResult EditorActionExecutor::importResourceFile(const Json& arguments) {
    fs::path source = FileUtils::pathFromUtf8(arguments.value("source_path", ""));
    if (source.empty() || source.is_relative() || !fs::exists(source)) {
        return failResult("source_path must be an existing absolute path.");
    }

    std::string error;
    fs::path targetRel;
    if (!safeTargetDir(project, arguments, targetRel, error)) return failResult(error);
    fs::path targetFull = project->getProjectPath() / targetRel;
    std::error_code ec;
    fs::create_directories(targetFull, ec);
    if (ec) return failResult("Failed to create destination directory: " + ec.message());

    project->getProjectCommandHistory()->addCommandNoMerge(
        new CopyFileCmd(project, std::vector<std::string>{source.string()}, targetFull.string(), true));
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    return okResult("Imported resource file through the project command history.");
}

ActionResult EditorActionExecutor::linkMaterial(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    if (!hasComponent(sceneProject->scene, entity, ComponentType::MeshComponent)) return failResult("Entity has no Mesh component.");

    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "material_path", rel, error, true)) return failResult(error);
    if (!Util::isMaterialFile(rel.string())) return failResult("material_path must point to a .material file.");

    Material material;
    try {
        material = Stream::decodeMaterial(TextEncoding::loadYamlFile(project->getProjectPath() / rel));
    } catch (const std::exception& e) {
        return failResult(std::string("Failed to load material: ") + e.what());
    }
    material.name = rel.generic_string();

    const unsigned int submeshIndex = static_cast<unsigned int>(std::max(0, arguments.value("submesh_index", 0)));
    const std::string propertyName = "submeshes[" + std::to_string(submeshIndex) + "].material";
    if (!Catalog::getProperty(sceneProject->scene, entity, ComponentType::MeshComponent, propertyName).ref) {
        return failResult("Submesh material property not found.");
    }

    CommandHandle::get(sceneId)->addCommandNoMerge(
        new LinkMaterialCmd(project, sceneId, entity, ComponentType::MeshComponent, propertyName, submeshIndex, material));
    return okResult("Linked material through the command history.");
}

ActionResult EditorActionExecutor::unlinkMaterial(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    if (!hasComponent(sceneProject->scene, entity, ComponentType::MeshComponent)) return failResult("Entity has no Mesh component.");

    const unsigned int submeshIndex = static_cast<unsigned int>(std::max(0, arguments.value("submesh_index", 0)));
    const std::string propertyName = "submeshes[" + std::to_string(submeshIndex) + "].material";
    if (!Catalog::getProperty(sceneProject->scene, entity, ComponentType::MeshComponent, propertyName).ref) {
        return failResult("Submesh material property not found.");
    }

    CommandHandle::get(sceneId)->addCommandNoMerge(
        new UnlinkMaterialCmd(project, sceneId, ComponentType::MeshComponent, propertyName, submeshIndex, std::vector<Entity>{entity}));
    return okResult("Unlinked material through the command history.");
}

ActionResult EditorActionExecutor::createMaterialFile(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    if (!hasComponent(sceneProject->scene, entity, ComponentType::MeshComponent)) return failResult("Entity has no Mesh component.");

    std::string error;
    fs::path targetRel;
    if (!safeTargetDir(project, arguments, targetRel, error)) return failResult(error);
    fs::path targetFull = project->getProjectPath() / targetRel;
    std::error_code ec;
    fs::create_directories(targetFull, ec);
    if (ec) return failResult("Failed to create target directory: " + ec.message());

    const unsigned int submeshIndex = static_cast<unsigned int>(std::max(0, arguments.value("submesh_index", 0)));
    const std::string propertyName = "submeshes[" + std::to_string(submeshIndex) + "].material";
    PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, ComponentType::MeshComponent, propertyName);
    if (!prop.ref) return failResult("Submesh material property not found.");

    Material material = *static_cast<Material*>(prop.ref);
    std::string payload = YAML::Dump(Stream::encodeMaterial(material));
    MaterialPayload sourceMaterial{0x4D54524C, sceneId, entity, submeshIndex};
    project->getProjectCommandHistory()->addCommandNoMerge(
        new CreateMaterialFileCmd(project, targetFull, payload.data(), payload.size(), &sourceMaterial));
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    return okResult("Created material file through the project command history.");
}

ActionResult EditorActionExecutor::setTerrainTextures(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveTerrainEntity(project, sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Terrain entity not found.");

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entity);
    std::vector<Texture> blendMaps = terrain.blendMaps;
    std::vector<TerrainSurfaceLayer> layers = terrain.surfaceLayers;
    bool blendMapsChanged = false;
    bool layersChanged = false;

    auto terrainChanged = [project = this->project, sceneId, entity](bool height) {
        return [project, sceneId, entity, height]() {
            SceneProject* sp = project ? project->getScene(sceneId) : nullptr;
            TerrainComponent* terrain = sp && sp->scene ? sp->scene->findComponent<TerrainComponent>(entity) : nullptr;
            if (!terrain) return;
            // only set, a blend map in the same call must not cancel the height rebuild
            if (height) {
                terrain->heightMapLoaded = false;
                terrain->needUpdateTerrain = true;
            }
            terrain->needUpdateTexture = true;
        };
    };

    auto* multiCmd = new MultiPropertyCmd();
    int changed = 0;
    std::string error;

    // Resolves an argument to an assets-relative texture, or reports why it cannot
    auto readTexture = [&](const char* arg, Texture& out) -> bool {
        if (!arguments.contains(arg) || !arguments[arg].is_string()) return false;
        fs::path rel = FileUtils::pathFromUtf8(arguments[arg].get<std::string>());
        if (!PathUtils::isSafeRelativePath(rel) || !fs::exists(project->getProjectPath() / rel)) {
            error = std::string(arg) + " must be an existing safe project-relative path.";
            return false;
        }
        if (!isInsideAssets(project, rel)) {
            error = outsideAssetsError(project, arg);
            return false;
        }
        out = Texture(assetPathFromAi(project, rel));
        changed++;
        return true;
    };

    Texture heightMap;
    if (readTexture("heightmap_path", heightMap)) {
        multiCmd->addPropertyCmd<Texture>(project, sceneId, entity, ComponentType::TerrainComponent,
                                          "heightMap", heightMap, terrainChanged(true));
    }

    Texture blendMap;
    if (readTexture("blendmap_path", blendMap)) {
        int slot = 0;
        if (arguments.contains("blendmap_index")) {
            slot = arguments["blendmap_index"].is_number_integer() ? arguments["blendmap_index"].get<int>() : -1;
            if (slot < 0 || slot >= MAX_TERRAIN_BLENDMAPS) {
                delete multiCmd;
                return failResult("blendmap_index must be between 0 and " + std::to_string(MAX_TERRAIN_BLENDMAPS - 1) + ".");
            }
        }
        // The whole vector is set at once, so filling a slot can also grow it
        if (blendMaps.size() <= (size_t)slot) blendMaps.resize(slot + 1);
        blendMaps[slot] = blendMap;
        blendMapsChanged = true;
    }

    // The first three layers, under the names they had when a blend map was the only one
    const char* detailArgs[] = {"detail_red_path", "detail_green_path", "detail_blue_path"};
    for (int i = 0; i < 3; i++) {
        Texture color;
        if (!readTexture(detailArgs[i], color)) continue;
        if (layers.size() <= (size_t)i) layers.resize(i + 1);
        layers[i].colorTexture = color;
        layersChanged = true;
    }

    // A single layer, with every map a painted surface can hold
    if (arguments.contains("layer_index")) {
        const int index = arguments["layer_index"].is_number_integer() ? arguments["layer_index"].get<int>() : -1;
        if (index < 0 || index >= MAX_TERRAIN_LAYERS) {
            delete multiCmd;
            return failResult("layer_index must be between 0 and " + std::to_string(MAX_TERRAIN_LAYERS - 1) + ".");
        }
        if (layers.size() <= (size_t)index) layers.resize(index + 1);
        TerrainSurfaceLayer& layer = layers[index];

        if (arguments.contains("layer_pbr")) {
            if (!arguments["layer_pbr"].is_boolean()) {
                delete multiCmd;
                return failResult("layer_pbr must be a boolean.");
            }
            layer.pbr = arguments["layer_pbr"].get<bool>();
            layersChanged = true;
            changed++;
        }

        struct LayerMap { const char* arg; Texture TerrainSurfaceLayer::* map; };
        static const LayerMap layerMaps[] = {
            {"layer_color_path", &TerrainSurfaceLayer::colorTexture},
            {"layer_normal_path", &TerrainSurfaceLayer::normalTexture},
            {"layer_roughness_path", &TerrainSurfaceLayer::roughnessTexture},
            {"layer_metallic_path", &TerrainSurfaceLayer::metallicTexture},
            {"layer_occlusion_path", &TerrainSurfaceLayer::occlusionTexture},
            {"layer_height_path", &TerrainSurfaceLayer::heightTexture}
        };
        for (const LayerMap& entry : layerMaps) {
            if (readTexture(entry.arg, layer.*entry.map)) {
                layersChanged = true;
            }
        }

        if (arguments.contains("layer_uv_scale")) {
            const Json& v = arguments["layer_uv_scale"];
            if (!v.is_object() || !v.value("x", Json()).is_number() || !v.value("y", Json()).is_number()) {
                delete multiCmd;
                return failResult("layer_uv_scale must be an object with numeric x and y.");
            }
            layer.uvScale = Vector2(v["x"].get<float>(), v["y"].get<float>());
            layersChanged = true;
            changed++;
        }
        struct LayerFactor { const char* arg; float TerrainSurfaceLayer::* factor; };
        static const LayerFactor layerFactors[] = {
            {"layer_normal_strength", &TerrainSurfaceLayer::normalStrength},
            {"layer_roughness_factor", &TerrainSurfaceLayer::roughnessFactor}
        };
        for (const LayerFactor& entry : layerFactors) {
            if (!arguments.contains(entry.arg)) continue;
            if (!arguments[entry.arg].is_number()) {
                delete multiCmd;
                return failResult(std::string(entry.arg) + " must be a number.");
            }
            layer.*entry.factor = arguments[entry.arg].get<float>();
            layersChanged = true;
            changed++;
        }
    }

    if (!error.empty()) {
        delete multiCmd;
        return failResult(error);
    }

    if (blendMapsChanged) {
        multiCmd->addPropertyCmd<std::vector<Texture>>(project, sceneId, entity, ComponentType::TerrainComponent,
                                                       "blendMaps", blendMaps, terrainChanged(false));
    }
    if (layersChanged) {
        multiCmd->addPropertyCmd<std::vector<TerrainSurfaceLayer>>(project, sceneId, entity, ComponentType::TerrainComponent,
                                                                   "surfaceLayers", layers, terrainChanged(false));
    }

    if (changed == 0) {
        delete multiCmd;
        return failResult("Provide at least one terrain texture path.");
    }
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);
    return okResult("Set terrain texture paths through the command history.");
}

ActionResult EditorActionExecutor::setTerrainFoliageLayer(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveTerrainEntity(project, sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Terrain entity not found.");

    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entity);
    std::vector<TerrainFoliageLayer> layers = terrain.foliageLayers;

    int index = -1;
    if (arguments.contains("layer_index")) {
        index = arguments["layer_index"].is_number_integer() ? arguments["layer_index"].get<int>() : -1;
        if (index < 0 || index >= (int)layers.size()) {
            return failResult("layer_index must name an existing foliage layer (0-" + std::to_string((int)layers.size() - 1) +
                              "). Omit it to append a layer.");
        }
    }

    const bool remove = arguments.value("remove", false);
    std::string message;
    if (remove) {
        if (index < 0) return failResult("remove needs layer_index.");
        layers.erase(layers.begin() + index);
        message = "Removed terrain foliage layer " + std::to_string(index) + " through the command history.";
    } else {
        if (index < 0) {
            layers.emplace_back();
            index = (int)layers.size() - 1;
        }
        TerrainFoliageLayer& layer = layers[index];

        auto readPath = [&](const char* arg, std::string& out) -> std::string {
            if (!arguments.contains(arg)) return "";
            if (!arguments[arg].is_string()) return std::string(arg) + " must be a string.";
            fs::path rel = FileUtils::pathFromUtf8(arguments[arg].get<std::string>());
            if (rel.empty()) { out.clear(); return ""; }
            if (!PathUtils::isSafeRelativePath(rel) || !fs::exists(project->getProjectPath() / rel)) {
                return std::string(arg) + " must be an existing safe project-relative path.";
            }
            if (!isInsideAssets(project, rel)) return outsideAssetsError(project, arg);
            out = assetPathFromAi(project, rel);
            return "";
        };

        std::string error = readPath("mesh_path", layer.meshPath);
        if (!error.empty()) return failResult(error);

        std::string densityPath;
        error = readPath("density_map_path", densityPath);
        if (!error.empty()) return failResult(error);
        if (arguments.contains("density_map_path")) {
            layer.densityMap = densityPath.empty() ? Texture() : Texture(densityPath);
        }

        struct FloatField { const char* arg; float TerrainFoliageLayer::* field; };
        static const FloatField floatFields[] = {
            {"density", &TerrainFoliageLayer::density},
            {"min_scale", &TerrainFoliageLayer::minScale},
            {"max_scale", &TerrainFoliageLayer::maxScale},
            {"rotation_jitter", &TerrainFoliageLayer::rotationJitter},
            {"align_to_normal", &TerrainFoliageLayer::alignToNormal},
            {"min_slope", &TerrainFoliageLayer::minSlope},
            {"max_slope", &TerrainFoliageLayer::maxSlope},
            {"min_height", &TerrainFoliageLayer::minHeight},
            {"max_height", &TerrainFoliageLayer::maxHeight},
            {"draw_distance", &TerrainFoliageLayer::drawDistance},
            {"min_draw_distance", &TerrainFoliageLayer::minDrawDistance}
        };
        for (const FloatField& entry : floatFields) {
            if (!arguments.contains(entry.arg)) continue;
            if (!arguments[entry.arg].is_number()) return failResult(std::string(entry.arg) + " must be a number.");
            layer.*entry.field = arguments[entry.arg].get<float>();
        }
        if (arguments.contains("seed")) {
            if (!arguments["seed"].is_number_integer() || arguments["seed"].get<long long>() < 0) {
                return failResult("seed must be a non-negative integer.");
            }
            layer.seed = arguments["seed"].get<unsigned int>();
        }
        if (arguments.contains("cast_shadows")) {
            if (!arguments["cast_shadows"].is_boolean()) return failResult("cast_shadows must be a boolean.");
            layer.castShadows = arguments["cast_shadows"].get<bool>();
        }
        if (layer.minScale > layer.maxScale) return failResult("min_scale must not exceed max_scale.");
        message = "Set terrain foliage layer " + std::to_string(index) + " through the command history.";
    }

    auto onChanged = [project = this->project, sceneId, entity]() {
        SceneProject* sp = project ? project->getScene(sceneId) : nullptr;
        TerrainComponent* t = sp && sp->scene ? sp->scene->findComponent<TerrainComponent>(entity) : nullptr;
        if (t) t->needUpdateFoliage = true;
    };
    auto* multiCmd = new MultiPropertyCmd();
    multiCmd->addPropertyCmd<std::vector<TerrainFoliageLayer>>(project, sceneId, entity, ComponentType::TerrainComponent,
                                                               "foliageLayers", layers, onChanged);
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);

    Json data = {{"layer_index", remove ? -1 : index}, {"layer_count", (int)layers.size()}};
    return okResult(message, data);
}

ActionResult EditorActionExecutor::setEditorCamera(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    if (sceneProject->sceneType != SceneType::SCENE_3D) return failResult("set_editor_camera works on 3D scenes.");
    if (!sceneProject->sceneRender || !sceneProject->sceneRender->getCamera()) {
        return failResult("The scene has no Scene view yet. Open it with open_scene first.");
    }

    auto readVector = [&](const char* arg, Vector3& out) -> bool {
        if (!arguments.contains(arg) || !arguments[arg].is_object()) return false;
        const Json& v = arguments[arg];
        if (!v.value("x", Json()).is_number() || !v.value("y", Json()).is_number() || !v.value("z", Json()).is_number()) return false;
        out = Vector3(v["x"].get<float>(), v["y"].get<float>(), v["z"].get<float>());
        return true;
    };
    Vector3 position, target;
    if (!readVector("position", position) || !readVector("target", target)) {
        return failResult("position and target need numeric x, y and z.");
    }
    if ((target - position).length() < 1e-4f) return failResult("target must differ from position.");

    const bool gameView = !sceneProject->sceneRender->isCameraDetached() &&
        (sceneProject->playState == ScenePlayState::PLAYING || sceneProject->playState == ScenePlayState::PAUSED);
    if (gameView) {
        return failResult("The Scene view shows the game camera while playing. Detach the camera (F8) or stop play mode first.");
    }
    // a preview keeps showing the scene camera
    const bool leftPreview = sceneProject->sceneRender->isPreviewCameraActive();
    if (leftPreview) sceneProject->sceneRender->clearPreviewCamera();

    Camera* camera = sceneProject->sceneRender->getCamera();
    camera->setPosition(position);
    // straight up or down would make the default up vector parallel to the view
    const Vector3 dir = (target - position).normalized();
    if (std::abs(dir.y) > 0.99f) {
        camera->setUp(0, 0, dir.y > 0 ? -1.0f : 1.0f);
    } else {
        camera->setUp(0, 1, 0);
    }
    camera->setTarget(target);
    sceneProject->needUpdateRender = true;
    Backend::getApp().requestRedraw();
    return okResult(leftPreview ? "Left the camera preview and moved the editor camera." : "Moved the editor camera.");
}

ActionResult EditorActionExecutor::createTerrainBlendmap(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveTerrainEntity(project, sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Terrain entity not found.");

    int resolution = arguments.value("resolution", project->getTerrainEditorSettings().blendMapResolution);
    resolution = std::max(2, std::min(2048, resolution));
    TerrainBlendmapRequest request;
    request.resolution = resolution;
    request.red = arguments.value("red", 0.0f);
    request.green = arguments.value("green", 0.0f);
    request.blue = arguments.value("blue", 0.0f);

    TerrainBlendmapResult generated = AiTerrainCapability::createBlendmap(project, sceneId, entity, request);
    if (!generated.success) {
        return failResult(generated.error.empty() ? "Failed to create terrain blendmap." : generated.error);
    }

    auto onChanged = [project = this->project, sceneId, entity]() {
        SceneProject* sp = project ? project->getScene(sceneId) : nullptr;
        TerrainComponent* terrain = sp && sp->scene ? sp->scene->findComponent<TerrainComponent>(entity) : nullptr;
        if (!terrain) return;
        terrain->needUpdateTexture = true;
    };
    // The whole vector is set at once, so the first map can also create the slot
    std::vector<Texture> blendMaps = sceneProject->scene->getComponent<TerrainComponent>(entity).blendMaps;
    if (blendMaps.empty()) {
        blendMaps.resize(1);
    }
    blendMaps[0] = Texture(generated.relativePath);
    CommandHandle::get(sceneId)->addCommandNoMerge(
        new PropertyCmd<std::vector<Texture>>(project, sceneId, entity, ComponentType::TerrainComponent,
                                              "blendMaps", blendMaps, onChanged));
    if (resourcesWindow) resourcesWindow->requestThumbnailGeneration(generated.fullPath, true);
    return okResult("Created terrain blendmap through the command history.",
                    Json{{"path", generated.relativePath}, {"resolution", resolution}});
}

ActionResult EditorActionExecutor::addTerrainCollision(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveTerrainEntity(project, sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Terrain entity not found.");

    Body3DComponent* body = sceneProject->scene->findComponent<Body3DComponent>(entity);
    const size_t shapeIdx = body ? body->numShapes : 0;

    Shape3D shape;
    shape.type = Shape3DType::HEIGHTFIELD;
    shape.source = Shape3DSource::ENTITY_HEIGHTFIELD;
    shape.sourceEntity = entity;
    shape.samplesSize = 256;

    auto* multiCmd = new MultiPropertyCmd();
    if (!body) {
        multiCmd->addCommand(std::make_unique<AddComponentCmd>(project, sceneId, entity, ComponentType::Body3DComponent));
    }
    multiCmd->addPropertyCmd<Shape3D>(project, sceneId, entity, ComponentType::Body3DComponent,
                                      "shapes[" + std::to_string(shapeIdx) + "]", shape);
    multiCmd->addPropertyCmd<size_t>(project, sceneId, entity, ComponentType::Body3DComponent,
                                     "numShapes", shapeIdx + 1);
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);
    return okResult("Added terrain heightfield collision through the command history.");
}

ActionResult EditorActionExecutor::createScript(const Json& arguments) {
    ScriptType type;
    bool cppSubclass = false;
    if (!parseScriptCreationType(arguments.value("type", ""), type, cppSubclass)) {
        return failResult("Unsupported script type. Use lua, cpp_subclass, or cpp_script_class.");
    }

    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    const std::string className = sanitizeIdentifier(arguments.value("class_name", "NewScript"), "NewScript");
    fs::path targetRel = FileUtils::pathFromUtf8(arguments.value("target_dir", std::string("scripts")));
    if (!PathUtils::isSafeRelativePath(targetRel)) return failResult("target_dir must be a safe project-relative directory.");
    fs::path targetFull = project->getProjectPath() / targetRel;
    std::error_code ec;
    fs::create_directories(targetFull, ec);
    if (ec) return failResult("Failed to create script directory: " + ec.message());

    fs::path sourceFull = PathUtils::uniqueChildPath(targetFull, className, scriptTypeExtension(type, false));
    fs::path headerFull;
    if (type != ScriptType::LUA) {
        headerFull = PathUtils::uniqueChildPath(targetFull, className, scriptTypeExtension(type, true));
    }

    std::string parentClass = "EntityHandle";
    bool parentDerivesMesh = false;
    if (cppSubclass) {
        ProjectUtils::EntityClassInfo parentInfo = ProjectUtils::getEntityClassInfo(sceneProject->scene, entity);
        parentClass = parentInfo.name;
        parentDerivesMesh = parentInfo.derivesMesh;
    } else if (type != ScriptType::LUA) {
        parentClass = "ScriptBase";
    }

    if (type == ScriptType::LUA) {
        std::ofstream f(sourceFull, std::ios::binary | std::ios::trunc);
        if (!f) return failResult("Failed to create Lua script file.");
        f << "-- " << className << ".lua\n\n";
        f << "local " << className << " = {\n";
        f << "    properties = {\n";
        f << "        { name = \"speed\", displayName = \"Speed\", type = \"float\", default = 5.0 },\n";
        f << "        { name = \"isActive\", displayName = \"Is Active\", type = \"bool\", default = true }\n";
        f << "    }\n";
        f << "}\n\n";
        f << "function " << className << ":init()\n";
        f << "    RegisterEngineEvent(self, \"onUpdate\")\n";
        f << "end\n\n";
        f << "function " << className << ":onUpdate()\n";
        f << "    if not self.isActive then return end\n";
        f << "end\n\n";
        f << "return " << className << "\n";
    } else {
        std::ofstream h(headerFull, std::ios::binary | std::ios::trunc);
        std::ofstream c(sourceFull, std::ios::binary | std::ios::trunc);
        if (!h || !c) return failResult("Failed to create C++ script files.");
        h << "#pragma once\n\n";
        if (cppSubclass) {
            h << "#include \"Shape.h\"\n";
            h << "#include \"" << parentClass << ".h\"\n";
        } else {
            h << "#include \"ScriptBase.h\"\n";
        }
        h << "#include \"Engine.h\"\n";
        h << "#include \"ScriptProperty.h\"\n\n";
        h << "class " << className << " : public doriax::" << parentClass << " {\n";
        h << "public:\n";
        h << "    DPROPERTY(\"Is Active\")\n";
        h << "    bool isActive = true;\n\n";
        h << "    " << className << "(doriax::Scene* scene, doriax::Entity entity);\n";
        h << "    ~" << className << "();\n\n";
        h << "    void onUpdate();\n";
        h << "};\n";

        c << "#include \"" << headerFull.filename().string() << "\"\n\n";
        c << "using namespace doriax;\n\n";
        c << className << "::" << className << "(Scene* scene, Entity entity): " << parentClass << "(scene, entity) {\n";
        c << "    REGISTER_ENGINE_EVENT(onUpdate);\n";
        c << "}\n\n";
        c << className << "::~" << className << "() {\n";
        c << "    UNREGISTER_ENGINE_EVENT(onUpdate);\n";
        c << "}\n\n";
        c << "void " << className << "::onUpdate() {\n";
        c << "    if (!isActive) return;\n";
        c << "}\n";
    }

    Json attachArgs = arguments;
    attachArgs["path"] = FileUtils::pathToGenericUtf8(fs::relative(sourceFull, project->getProjectPath(), ec));
    attachArgs["class_name"] = className;
    if (!headerFull.empty()) {
        attachArgs["header_path"] = FileUtils::pathToGenericUtf8(fs::relative(headerFull, project->getProjectPath(), ec));
    }
    ActionResult attached = attachScript(attachArgs);
    if (!attached.success) return attached;
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    Json data = {
        {"path", attachArgs["path"]},
        {"header_path", attachArgs.value("header_path", "")},
        {"class_name", className},
        {"type", arguments.value("type", "")}
    };
    if (type == ScriptType::LUA) {
        data["lua_script_guide"] = doriaxLuaScriptGuide(className);
        data["lua_startup_method"] = "init";
        data["lua_runtime_context"] = Json{{"scene", "self.scene"}, {"entity", "self.entity"}};
        data["lua_cube_color_example"] =
            "local shape = Shape(self.scene, self.entity)\n"
            "shape:setColor(1.0, 0.0, 0.0, 1.0)";
        data["next_step"] =
            "If the user asked for behavior, call search_engine_api for required runtime wrappers/methods, then update_script_file with complete Lua contents.";
    } else {
        data["cpp_script_guide"] = doriaxCppScriptGuide(cppSubclass, parentClass);
        data["cpp_startup_method"] =
            "Use onUpdate for variable-frame non-physics logic or onFixedUpdate for physics-step logic, "
            "with matching REGISTER_ENGINE_EVENT/UNREGISTER_ENGINE_EVENT calls";
        if (parentDerivesMesh) {
            data["recommended_type"] = "cpp_subclass";
            data["cpp_cube_color_example"] =
                "#include \"" + className + ".h\"\n\n"
                "using namespace doriax;\n\n"
                + className + "::" + className + "(Scene* scene, Entity entity): " + parentClass + "(scene, entity) {\n"
                "    REGISTER_ENGINE_EVENT(onUpdate);\n"
                "    setColor(1.0f, 0.0f, 0.0f, 1.0f);\n"
                "}\n\n"
                + className + "::~" + className + "() {\n"
                "    UNREGISTER_ENGINE_EVENT(onUpdate);\n"
                "}\n";
        }
        data["next_step"] =
            "If the user asked for behavior, call search_engine_api for required methods, then update_script_file with complete .h and .cpp contents using flat headers (e.g. \"Mesh.h\").";
    }
    return okResult("Created and attached script.", data);
}

ActionResult EditorActionExecutor::attachScript(const Json& arguments) {
    ScriptType type;
    if (!parseScriptType(arguments.value("type", ""), type)) {
        return failResult("Unsupported script type. Use lua or cpp.");
    }

    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    std::string error;
    fs::path sourceRel;
    if (!safeRelativePath(project, arguments, "path", sourceRel, error, true)) return failResult(error);
    fs::path headerRel;
    if (type != ScriptType::LUA) {
        if (!safeRelativePath(project, arguments, "header_path", headerRel, error, true)) return failResult(error);
    }

    ScriptEntry entry;
    entry.type = type;
    entry.enabled = true;
    // Lua entries resolve through "lua://", C++ entries are project-relative build inputs
    entry.path = (type == ScriptType::LUA)
        ? project->normalizeToLuaRelative(project->getProjectPath() / sourceRel).generic_string()
        : sourceRel.generic_string();
    entry.headerPath = headerRel.empty() ? "" : headerRel.generic_string();
    entry.className = sanitizeIdentifier(arguments.value("class_name", "NewScript"), "NewScript");

    const bool hasScript = hasComponent(sceneProject->scene, entity, ComponentType::ScriptComponent);
    std::vector<ScriptEntry> scripts;
    if (hasScript) {
        scripts = sceneProject->scene->getComponent<ScriptComponent>(entity).scripts;
    }

    // The same file, or the same class, is one attachment: re-attaching a renamed script must
    // repoint its entry instead of leaving a stale duplicate behind.
    size_t index = scripts.size();
    for (size_t i = 0; i < scripts.size(); i++) {
        if (normalizeScriptPath(project, scriptPath(project, scripts[i])) == normalizeScriptPath(project, scriptPath(project, entry)) ||
            (scripts[i].type == entry.type && scripts[i].className == entry.className)) {
            index = i;
            break;
        }
    }

    const bool replaced = index < scripts.size();
    if (replaced) {
        // Keeps the entry enabled state and its property values.
        scripts[index].type = entry.type;
        scripts[index].path = entry.path;
        scripts[index].headerPath = entry.headerPath;
        scripts[index].className = entry.className;
    } else {
        scripts.push_back(entry);
    }
    project->updateScriptProperties(sceneProject, entity, scripts);

    auto* multiCmd = new MultiPropertyCmd();
    if (!hasScript) {
        multiCmd->addCommand(std::make_unique<AddComponentCmd>(project, sceneId, entity, ComponentType::ScriptComponent));
    }
    multiCmd->addCommand(std::make_unique<PropertyCmd<std::vector<ScriptEntry>>>(
        project, sceneId, entity, ComponentType::ScriptComponent, "scripts", scripts));
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);
    return okResult(replaced ? "Script was already attached, updated its entry through the command history."
                             : "Attached script through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity},
                         {"script_index", replaced ? index : scripts.size() - 1}});
}

ActionResult EditorActionExecutor::updateScriptEntry(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    ScriptComponent* component = sceneProject->scene->findComponent<ScriptComponent>(entity);
    if (!component) return failResult("Entity has no ScriptComponent.");

    std::vector<ScriptEntry> scripts = component->scripts;
    size_t index = 0;
    std::string error;
    if (!resolveScriptIndex(project, scripts, arguments, index, error)) return failResult(error);

    ScriptEntry& entry = scripts[index];

    if (arguments.contains("new_class_name")) {
        const std::string className = sanitizeIdentifier(optionalString(arguments, "new_class_name"), "");
        if (className.empty()) return failResult("new_class_name must be a valid class identifier.");
        entry.className = className;
    }
    if (arguments.contains("new_path")) {
        fs::path sourceRel;
        if (!safeRelativePath(project, arguments, "new_path", sourceRel, error, true)) return failResult(error);
        const bool validSource = (entry.type == ScriptType::LUA)
            ? Util::isLuaFile(sourceRel.string()) : Util::isSourceFile(sourceRel.string());
        if (!validSource) return failResult("new_path must be a .lua file for Lua entries and a .cpp file for C++ entries.");
        entry.path = (entry.type == ScriptType::LUA)
            ? project->normalizeToLuaRelative(project->getProjectPath() / sourceRel).generic_string()
            : sourceRel.generic_string();
    }
    if (arguments.contains("new_header_path")) {
        if (entry.type == ScriptType::LUA) return failResult("Lua script entries have no header file.");
        fs::path headerRel;
        if (!safeRelativePath(project, arguments, "new_header_path", headerRel, error, true)) return failResult(error);
        if (!Util::isHeaderFile(headerRel.string())) return failResult("new_header_path must be a header file.");
        entry.headerPath = headerRel.generic_string();
    }

    // Reloads the exposed properties from the files the entry now points at.
    project->updateScriptProperties(sceneProject, entity, scripts);
    CommandHandle::get(sceneId)->addCommandNoMerge(new PropertyCmd<std::vector<ScriptEntry>>(
        project, sceneId, entity, ComponentType::ScriptComponent, "scripts", scripts));
    return okResult("Updated script entry through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"script_index", index}});
}

ActionResult EditorActionExecutor::removeScriptEntry(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    ScriptComponent* component = sceneProject->scene->findComponent<ScriptComponent>(entity);
    if (!component) return failResult("Entity has no ScriptComponent.");

    std::vector<ScriptEntry> scripts = component->scripts;
    size_t index = 0;
    std::string error;
    if (!resolveScriptIndex(project, scripts, arguments, index, error)) return failResult(error);

    const std::string className = scripts[index].className;
    scripts.erase(scripts.begin() + static_cast<std::ptrdiff_t>(index));
    project->updateScriptProperties(sceneProject, entity, scripts);
    CommandHandle::get(sceneId)->addCommandNoMerge(new PropertyCmd<std::vector<ScriptEntry>>(
        project, sceneId, entity, ComponentType::ScriptComponent, "scripts", scripts));
    return okResult("Removed script " + className + " through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"removed_script_index", index}});
}

ActionResult EditorActionExecutor::updateScriptFile(const Json& arguments) {
    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "path", rel, error, true)) {
        return failResult(error);
    }

    const std::string ext = lower(rel.extension().string());
    if (ext != ".lua" && ext != ".cpp" && ext != ".h" && ext != ".hpp") {
        return failResult("path must point to a .lua, .cpp, .h, or .hpp script file.");
    }

    const std::string content = arguments.value("content", "");
    constexpr size_t kMaxScriptBytes = 2 * 1024 * 1024;
    if (content.size() > kMaxScriptBytes) {
        return failResult("Script content is too large for an AI edit.");
    }
    if (ext == ".lua") {
        std::string validationError = validateDoriaxLuaScriptContent(content);
        if (!validationError.empty()) {
            return failResult(validationError);
        }
    } else if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
        std::string validationError = validateDoriaxCppScriptContent(content);
        if (!validationError.empty()) {
            return failResult(validationError);
        }
    }

    const fs::path fullPath = project->getProjectPath() / rel;
    std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return failResult("Failed to open script file for writing.");
    }
    out << content;
    out.close();
    if (!out) {
        return failResult("Failed to write script file.");
    }

    const fs::path relPath = rel.lexically_normal();
    int refreshedComponents = 0;
    for (auto& sceneProject : project->getScenes()) {
        if (!sceneProject.scene) continue;

        for (Entity entity : sceneProject.entities) {
            ScriptComponent* scriptComponent = sceneProject.scene->findComponent<ScriptComponent>(entity);
            if (!scriptComponent) continue;

            bool matchesFile = false;
            for (const ScriptEntry& scriptEntry : scriptComponent->scripts) {
                if (!scriptEntry.path.empty() && normalizeScriptPath(project, scriptPath(project, scriptEntry)) == relPath) {
                    matchesFile = true;
                    break;
                }
                if (!scriptEntry.headerPath.empty() && normalizeScriptPath(project, scriptEntry.headerPath) == relPath) {
                    matchesFile = true;
                    break;
                }
            }
            if (!matchesFile) continue;

            std::vector<ScriptEntry> scripts = scriptComponent->scripts;
            if (ext == ".h" || ext == ".hpp") {
                project->updateScriptProperties(&sceneProject, entity, scripts, content, fullPath.string());
            } else {
                project->updateScriptProperties(&sceneProject, entity, scripts);
            }
            PropertyCmd<std::vector<ScriptEntry>> propertyCmd(
                project, sceneProject.id, entity, ComponentType::ScriptComponent, "scripts", scripts);
            propertyCmd.execute();
            refreshedComponents++;
        }
    }

    if (resourcesWindow) {
        resourcesWindow->refreshCurrentDirectory();
    }
    Out::info("AI updated script file: %s", relPath.generic_string().c_str());

    const std::string verifyStep =
        "Verify this change before telling the user it works: start the scene with "
        "control_play_mode (action=start), then read_output_log to check for a C++ build "
        "failure or a runtime 'Script crash'. If it is still building, read_output_log again. "
        "If there are errors, fix them with update_script_file and verify again; when the log "
        "is clean, stop play with control_play_mode (action=stop).";

    return okResult("Updated script file. Now verify it by running the scene (see next_step).",
                    Json{{"path", FileUtils::pathToGenericUtf8(relPath)},
                         {"bytes", content.size()},
                         {"refreshed_script_components", refreshedComponents},
                         {"next_step", verifyStep}});
}

ActionResult EditorActionExecutor::createSourceFile(const Json& arguments) {
    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "path", rel, error, false)) {
        return failResult(error);
    }

    const std::string ext = lower(rel.extension().string());
    if (ext != ".cpp" && ext != ".h" && ext != ".hpp") {
        return failResult("path must point to a .cpp, .h, or .hpp file.");
    }

    const fs::path fullPath = project->getProjectPath() / rel;
    if (fs::exists(fullPath)) {
        return failResult("File already exists; replace its contents with update_script_file instead.");
    }

    const std::string content = arguments.value("content", "");
    constexpr size_t kMaxSourceBytes = 2 * 1024 * 1024;
    if (content.empty()) {
        return failResult("create_source_file requires non-empty content.");
    }
    if (content.size() > kMaxSourceBytes) {
        return failResult("Source content is too large for an AI edit.");
    }
    std::string validationError = validateDoriaxCppScriptContent(content);
    if (!validationError.empty()) {
        return failResult(validationError);
    }

    std::error_code ec;
    fs::create_directories(fullPath.parent_path(), ec);
    std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return failResult("Failed to open source file for writing.");
    }
    out << content;
    out.close();
    if (!out) {
        return failResult("Failed to write source file.");
    }

    if (resourcesWindow) {
        resourcesWindow->refreshCurrentDirectory();
    }
    Out::info("AI created source file: %s", rel.generic_string().c_str());

    // A header under a root only joins the include path; a source becomes a translation unit.
    const bool inScriptDir = project->isInsideScriptDirs(fullPath);
    std::string nextStep;
    if (ext == ".h" || ext == ".hpp") {
        nextStep = inScriptDir
            ? "Its directory is on the include path, so scripts include it by name. A header is not compiled "
              "on its own; verify it through a source that includes it."
            : "Its directory is not on the include path, so scripts reach it only through a relative include. "
              "Add its root with set_project_script_dirs to include it by name.";
    } else {
        nextStep = inScriptDir
            ? "It compiles as its own translation unit. Verify it with control_play_mode (action=start) and "
              "read_output_log, then stop play. Play only builds C++ when an enabled C++ script is attached to "
              "an entity, so in a Lua-only project nothing is compiled until export."
            : "The editor build never compiles it there; it only reaches the build through an attached script "
              "that includes it. Add its root with set_project_script_dirs to compile it on its own.";
    }

    return okResult("Created source file (see next_step).",
                    Json{{"path", FileUtils::pathToGenericUtf8(rel)},
                         {"bytes", content.size()},
                         {"in_script_dir", inScriptDir},
                         {"next_step", nextStep}});
}

ActionResult EditorActionExecutor::setProjectScriptDirs(const Json& arguments) {
    const auto it = arguments.find("directories");
    if (it == arguments.end() || !it->is_array()) {
        return failResult("set_project_script_dirs requires a directories array.");
    }

    // Project Settings stores the project root as "." and an outside folder whole; both forms
    // fail isSafeRelativePath, so they can only be repeated back here, never added.
    const std::vector<fs::path>& configured = project->getScriptDirs();
    const auto isConfigured = [&configured](const fs::path& directory) {
        for (const fs::path& current : configured) {
            if (current.lexically_normal() == directory) return true;
        }
        return false;
    };

    std::vector<fs::path> directories;
    for (const Json& entry : *it) {
        if (!entry.is_string()) {
            return failResult("directories must contain only strings.");
        }
        const std::string raw = entry.get<std::string>();
        fs::path directory = FileUtils::pathFromUtf8(raw).lexically_normal();
        if (!PathUtils::isSafeRelativePath(directory) && !isConfigured(directory)) {
            return failResult("A new script directory must be project-relative and inside the project: " + raw +
                              ". The project root and outside directories are added by the user in Project Settings; "
                              "pass those back only as get_project_summary reported them.");
        }

        std::error_code ec;
        if (!fs::is_directory(directory.is_absolute() ? directory : project->getProjectPath() / directory, ec)) {
            return failResult("Script directory does not exist: " + FileUtils::pathToGenericUtf8(directory) +
                              ". Create it with create_folder first.");
        }
        if (std::find(directories.begin(), directories.end(), directory) == directories.end()) {
            directories.push_back(std::move(directory));
        }
    }

    Json scriptDirs = Json::array();
    for (const fs::path& directory : directories) {
        scriptDirs.push_back(FileUtils::pathToGenericUtf8(directory));
    }

    project->setScriptDirs(std::move(directories));
    if (!project->saveProjectFile()) {
        return failResult("Failed to save the project with the new script directories.");
    }

    return okResult(scriptDirs.empty()
                        ? "Cleared the project script directories."
                        : "Set the project script directories. The next play or export builds with them.",
                    Json{{"script_dirs", scriptDirs}});
}

ActionResult EditorActionExecutor::setProjectAssetDirs(const Json& arguments) {
    if (project->isAnyScenePlaying()) {
        return failResult(busyError(project, "changing the asset directories"));
    }
    // The change saves every open buffer
    if (hasUnsavedScriptEdits()) {
        return failResult(kUnsavedScriptEdits);
    }

    fs::path assetsDir = project->getAssetsDir();
    fs::path luaDir = project->getLuaDir();
    // "." is the project root, which isSafeRelativePath rejects
    const auto readDir = [&](const char* key, fs::path& directory, std::string& error) {
        const auto it = arguments.find(key);
        if (it == arguments.end() || !it->is_string()) return true;
        fs::path value = FileUtils::pathFromUtf8(it->get<std::string>()).lexically_normal();
        if (!value.empty() && !value.has_filename()) value = value.parent_path();
        if (value.empty() || value == ".") {
            directory = ".";
            return true;
        }
        if (!PathUtils::isSafeRelativePath(value)) {
            error = std::string(key) + " must be . or a project-relative directory inside the project.";
            return false;
        }
        for (const fs::path& part : value) {
            if (!part.empty() && part.string().front() == '.') {
                error = std::string(key) + " cannot be a hidden directory: the editor and exports skip those.";
                return false;
            }
        }
        // Links can point outside the project
        std::error_code linkEc;
        const fs::path projectRoot = fs::weakly_canonical(project->getProjectPath(), linkEc);
        const fs::path inside = fs::weakly_canonical(project->getProjectPath() / value, linkEc).lexically_relative(projectRoot);
        if (linkEc || inside.empty() || *inside.begin() == "..") {
            error = std::string(key) + " leads outside the project through a link.";
            return false;
        }
        directory = value;
        return true;
    };
    std::string error;
    if (!readDir("assets_dir", assetsDir, error) || !readDir("lua_dir", luaDir, error)) {
        return failResult(error);
    }

    if (!project->changeAssetRoots(assetsDir, luaDir, &error)) {
        return failResult(error + ".");
    }
    if (!project->saveProjectFile()) {
        return failResult("Failed to save the project with the new directories.");
    }

    return okResult("Set the project directories. Referenced files moved and their references were rewritten; "
                    "read_output_log lists each move. Files nothing references stayed where they were.",
                    Json{{"assets_dir", FileUtils::pathToGenericUtf8(project->getAssetsDir())},
                         {"lua_dir", FileUtils::pathToGenericUtf8(project->getLuaDir())}});
}

ActionResult EditorActionExecutor::checkCppBuildTools() {
    // The same check play runs before building C++ scripts
    const LocalBuildSettings settings = AppSettings::getBuildSettings(project->getProjectPath() / "project.yaml");
    const bool useDefaultKit = settings.cCompiler.empty() && settings.cxxCompiler.empty() && settings.generator.empty();
    CMakeKit kit;
    kit.cCompiler = settings.cCompiler;
    kit.cxxCompiler = settings.cxxCompiler;
    kit.generator = settings.generator;
    const std::string missing = Generator::checkBuildTools(useDefaultKit, &kit);
    if (!missing.empty()) {
        return okResult("C++ scripts cannot build on this machine, so use Lua unless the user installs what is missing.",
                        Json{{"ready", false}, {"missing", missing}});
    }

    const auto shown = [](const std::string& value) { return value.empty() ? std::string("default") : value; };
    return okResult("C++ build tools found. The first play after writing a C++ script shows whether they compile; if it keeps failing, use Lua.",
                    Json{{"ready", true}, {"c_compiler", shown(kit.cCompiler)}, {"cxx_compiler", shown(kit.cxxCompiler)},
                         {"generator", shown(kit.generator)}});
}

ActionResult EditorActionExecutor::setProjectCxxStandard(const Json& arguments) {
    if (!arguments.contains("cxx_standard") || !arguments["cxx_standard"].is_number_integer()) {
        return failResult("set_project_cxx_standard requires cxx_standard (" + cxxStandardList() + ").");
    }

    const Json& value = arguments["cxx_standard"];
    if (value < cxxStandards[0] || value > cxxStandards[std::size(cxxStandards) - 1]
            || !isSupportedCxxStandard(value.get<int>())) {
        return failResult("cxx_standard must be " + cxxStandardList() + ".");
    }

    const int previousStandard = project->getCxxStandard();
    project->setCxxStandard(value.get<int>());
    if (!project->saveProjectFile()) {
        project->setCxxStandard(previousStandard);
        return failResult("Failed to save the project with the new C++ standard.");
    }

    return okResult("Set the project C++ standard to C++" + std::to_string(project->getCxxStandard())
                        + ". The next play or export builds with it.",
                    Json{{"cxx_standard", project->getCxxStandard()}});
}

ActionResult EditorActionExecutor::updateProjectBuildFile(const Json& arguments) {
    const std::string content = arguments.value("content", "");
    constexpr size_t kMaxBuildFileBytes = 256 * 1024;
    if (content.empty()) {
        return failResult("update_project_build_file requires non-empty content.");
    }
    if (content.size() > kMaxBuildFileBytes) {
        return failResult("ProjectBuild.cmake content is too large for an AI edit.");
    }

    // The target name differs between the editor and exported builds.
    if (content.find("target_") != std::string::npos && content.find("${DORIAX_TARGET}") == std::string::npos) {
        return failResult("ProjectBuild.cmake must attach target settings to ${DORIAX_TARGET}, and use "
                          "${DORIAX_SCRIPTS_DIR} as the root of script paths.");
    }

    const fs::path fullPath = project->getProjectPath() / "ProjectBuild.cmake";
    std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return failResult("Failed to open ProjectBuild.cmake for writing.");
    }
    out << content;
    out.close();
    if (!out) {
        return failResult("Failed to write ProjectBuild.cmake.");
    }

    if (resourcesWindow) {
        resourcesWindow->refreshCurrentDirectory();
    }
    Out::info("AI updated ProjectBuild.cmake");

    const std::string verifyStep =
        "Verify it before telling the user it works: start the scene with control_play_mode (action=start), "
        "then read_output_log for CMake or compiler errors; fix them here and verify again, then stop play. "
        "Play only configures CMake when an enabled C++ script is attached to an entity, so a Lua-only project "
        "first reads this file on export.";

    return okResult("Updated ProjectBuild.cmake (see next_step).",
                    Json{{"path", "ProjectBuild.cmake"},
                         {"bytes", content.size()},
                         {"next_step", verifyStep}});
}

ActionResult EditorActionExecutor::createBundleFromEntity(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    std::string error;
    fs::path bundleRel;
    if (!safeRelativePath(project, arguments, "bundle_path", bundleRel, error, false)) return failResult(error);
    if (!Util::isBundleFile(bundleRel.string())) return failResult("bundle_path must end with .bundle.");
    if (fs::exists(project->getProjectPath() / bundleRel)) return failResult("Bundle file already exists.");

    // with its action children, as when the entity is dragged to Resources
    std::vector<Entity> members{entity};
    std::vector<Entity> virtualChildren = ProjectUtils::getVirtualChildren(sceneProject->scene, members);
    members.insert(members.end(), virtualChildren.begin(), virtualChildren.end());
    YAML::Node node = Stream::encodeEntitySelection(members, sceneProject->scene, project, sceneProject);
    if (!node || !node.IsMap()) return failResult("Failed to encode entity selection.");
    fs::create_directories((project->getProjectPath() / bundleRel).parent_path());

    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new CreateEntityBundleCmd(project, sceneId, bundleRel, node))) {
        return failResult(kRefusedCommand);
    }
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    return okResult("Created bundle from entity through the command history.");
}

ActionResult EditorActionExecutor::importBundleInstance(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    std::string error;
    fs::path bundleRel;
    if (!safeRelativePath(project, arguments, "bundle_path", bundleRel, error, true)) return failResult(error);
    if (!Util::isBundleFile(bundleRel.string())) return failResult("bundle_path must point to a .bundle file.");

    Entity parent = NULL_ENTITY;
    if (arguments.contains("parent_id")) {
        parent = resolveEntityByKeys(sceneProject, arguments, "parent_id", "parent_name");
        if (parent == NULL_ENTITY) return failResult("Parent entity not found.");
    }

    auto* cmd = new ImportEntityBundleCmd(project, sceneId, bundleRel, parent, true);
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(cmd)) {
        return failResult("Failed to import " + FileUtils::pathToGenericUtf8(bundleRel) + ".");
    }

    Entity root = cmd->getRootEntity();
    Json imported = Json::array();
    for (Entity importedEntity : cmd->getImportedEntities()) imported.push_back(importedEntity);
    return okResult("Imported bundle instance through the command history.", Json{
        {"instance_root_id", root},
        {"instance_root_name", sceneProject->scene->getEntityName(root)},
        {"imported_entities", imported}});
}

ActionResult EditorActionExecutor::addEntityToBundle(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    Entity bundleRoot = resolveEntityByKeys(sceneProject, arguments, "bundle_root_id", "bundle_root_name");
    if (bundleRoot == NULL_ENTITY) return failResult("Bundle root entity not found.");

    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new AddEntityToBundleCmd(project, sceneId, entity, bundleRoot))) {
        return failResult(kRefusedCommand);
    }
    return okResult("Added entity to bundle through the command history.");
}

ActionResult EditorActionExecutor::removeEntityFromBundle(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    if (!project->isEntityInBundle(sceneId, entity)) return failResult("Entity is not part of a bundle.");

    Transform* transform = sceneProject->scene->findComponent<Transform>(entity);
    Entity parent = transform ? transform->parent : NULL_ENTITY;
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new RemoveEntityFromBundleCmd(project, sceneId, entity, parent))) {
        return failResult(kRefusedCommand);
    }
    return okResult("Removed entity from bundle through the command history.");
}

ActionResult EditorActionExecutor::makeBundleComponentUnique(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    ComponentType component;
    if (!parseComponentType(arguments.value("component", ""), component)) return failResult("Unknown component type.");

    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new ComponentToBundleLocalCmd(project, sceneId, entity, component))) {
        return failResult("The component is already a local override.");
    }
    return okResult("Made bundle component unique through the command history.");
}

ActionResult EditorActionExecutor::revertBundleComponent(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    ComponentType component;
    if (!parseComponentType(arguments.value("component", ""), component)) return failResult("Unknown component type.");

    if (!CommandHandle::get(sceneId)->addCommandNoMerge(new ComponentToBundleSharedCmd(project, sceneId, entity, component))) {
        return failResult("The component has no local override to revert.");
    }
    return okResult("Reverted bundle component through the command history.");
}

ActionResult EditorActionExecutor::setStandaloneBundle(const Json& arguments) {
    std::string error;
    fs::path bundleRel;
    if (!safeRelativePath(project, arguments, "bundle_path", bundleRel, error, true)) return failResult(error);
    if (!Util::isBundleFile(bundleRel.string())) return failResult("bundle_path must point to a .bundle file.");

    if (arguments.contains("standalone") && !arguments["standalone"].is_boolean()) {
        return failResult("standalone must be a boolean.");
    }
    const bool standalone = arguments.value("standalone", true);

    const EntityBundle* bundle = project->getEntityBundle(bundleRel);
    if (standalone && bundle && !bundle->instances.empty()) {
        return failResult("Bundle is instantiated in a scene and is already built, it does not need to be standalone.");
    }

    std::vector<fs::path> bundlePaths = project->getStandaloneBundles();
    auto it = std::find(bundlePaths.begin(), bundlePaths.end(), bundleRel);
    const bool listed = it != bundlePaths.end();

    if (standalone == listed) {
        return okResult(listed ? "Bundle is already standalone." : "Bundle is not standalone.");
    }

    if (standalone) {
        bundlePaths.push_back(bundleRel);
    } else {
        bundlePaths.erase(it);
    }

    project->setStandaloneBundles(std::move(bundlePaths));
    project->saveProjectFile();
    return okResult(standalone ? "Added the bundle to the standalone bundles." : "Removed the bundle from the standalone bundles.",
        Json{{"bundle_path", FileUtils::pathToGenericUtf8(bundleRel)}, {"standalone", standalone}});
}

ActionResult EditorActionExecutor::exportProject(const Json& arguments, const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) return failResult("Export cancelled.");
    ExportConfig config;
    config.targetDir = FileUtils::pathFromUtf8(arguments.value("target_dir", ""));
    // Stored references are relative to these roots, so they are not overridable
    config.assetsDir = project->getAssetsPath();
    config.luaDir = project->getLuaPath();
    config.startSceneId = static_cast<uint32_t>(arguments.value("start_scene_id", static_cast<int>(project->getStartSceneId())));
    config.packNativeResources = project->shouldPackNativeResources();
    // the overrides saved with the project, as the Export window applies them
    config.shaderAdditions = project->getShaderOverrides().additions;
    config.shaderExclusions = project->getShaderOverrides().exclusions;
    std::string error;
    config.selectedBackends = parseBackendList(arguments.value("backends", "all"), error);
    if (!error.empty()) return failResult(error);

    Exporter exporter;
    const bool success = exporter.exportProject(project, config);
    ExportProgress progress = exporter.getProgress();
    if (!success) {
        return failResult(progress.errorMessage.empty() ? "Export failed." : progress.errorMessage);
    }
    return okResult("Exported project.", Json{{"target_dir", FileUtils::pathToGenericUtf8(config.targetDir)}});
}

ActionResult EditorActionExecutor::setExportShaders(const Json& arguments) {
    // type[:properties][@forkBasePath]
    auto parseSpecs = [&](const char* field, bool requireFiles, std::vector<ShaderKey>& keys, std::string& error) {
        if (!arguments.contains(field)) return true;
        const Json& list = arguments[field];
        if (!list.is_array()) {
            error = std::string(field) + " must be an array of shader specs.";
            return false;
        }
        for (const Json& item : list) {
            if (!item.is_string()) {
                error = std::string(field) + " must hold strings such as mesh:4184 or ssao.";
                return false;
            }
            const std::string spec = item.get<std::string>();
            const size_t customSeparator = spec.find('@');
            const std::string builtIn = spec.substr(0, customSeparator);
            const size_t separator = builtIn.find(':');
            ShaderType type;
            if (!ShaderPool::parseShaderTypeToken(builtIn.substr(0, separator), type)) {
                error = "Unknown shader type in '" + spec + "'.";
                return false;
            }
            uint32_t properties = 0;
            if (separator != std::string::npos) {
                const std::string mask = builtIn.substr(separator + 1);
                size_t used = 0;
                unsigned long long value = 0;
                try {
                    value = std::stoull(mask, &used, 0);
                } catch (...) {
                    used = 0;
                }
                if (mask.empty() || used != mask.size() || value > 0xFFFFFFFFull) {
                    error = "Invalid property mask in '" + spec + "'.";
                    return false;
                }
                properties = static_cast<uint32_t>(value);
            }
            uint16_t customId = 0;
            if (customSeparator != std::string::npos) {
                const std::string customShader = spec.substr(customSeparator + 1);
                if (customShader.empty()) {
                    error = "Missing custom shader path after '@' in '" + spec + "'.";
                    return false;
                }
                if (requireFiles) {
                    const Util::CustomShaderPaths paths = Util::resolveCustomShaderPaths(customShader);
                    if (!fs::exists(project->getProjectPath() / paths.vert) || !fs::exists(project->getProjectPath() / paths.frag)) {
                        error = "'" + spec + "' references missing files (" + paths.vert + ", " + paths.frag + ").";
                        return false;
                    }
                }
                customId = ShaderPool::registerCustomShader(customShader);
            }
            keys.push_back(ShaderPool::normalizeKey(ShaderPool::getShaderKey(type, properties, customId)));
        }
        return true;
    };

    if (arguments.contains("clear") && !arguments["clear"].is_boolean()) {
        return failResult("clear must be a boolean.");
    }
    std::vector<ShaderKey> add, remove, exclude, include;
    std::string error;
    if (!parseSpecs("add", true, add, error) || !parseSpecs("remove", false, remove, error) ||
        !parseSpecs("exclude", false, exclude, error) || !parseSpecs("include", false, include, error)) {
        return failResult(error);
    }

    auto erasePaired = [](std::set<ShaderKey>& keys, ShaderKey key) {
        for (ShaderKey variant : Exporter::getPairedShaderKeys(key)) keys.erase(variant);
    };

    ShaderOverrides& shaders = project->getShaderOverrides();
    const ShaderOverrides previous = shaders;
    const bool clear = arguments.value("clear", false);
    const bool changing = clear || !add.empty() || !remove.empty() || !exclude.empty() || !include.empty();
    if (clear) {
        shaders.additions.clear();
        shaders.exclusions.clear();
    }
    for (ShaderKey key : add) {
        shaders.additions.insert(key);
        erasePaired(shaders.exclusions, key);
    }
    for (ShaderKey key : remove) {
        erasePaired(shaders.additions, key);
    }
    for (ShaderKey key : exclude) {
        shaders.exclusions.insert(key);
        erasePaired(shaders.additions, key);
    }
    for (ShaderKey key : include) {
        erasePaired(shaders.exclusions, key);
    }
    if (changing && !project->saveProjectFile()) {
        shaders = previous;
        return failResult("Failed to save the project with the new export shader overrides.");
    }

    auto specs = [](const std::set<ShaderKey>& keys) {
        Json list = Json::array();
        for (ShaderKey key : keys) {
            std::string spec = ShaderPool::getShaderTypeName(ShaderPool::getShaderTypeFromKey(key), true);
            const uint32_t properties = ShaderPool::getPropertiesFromKey(key);
            if (properties) spec += ":" + std::to_string(properties);
            const uint16_t customId = ShaderPool::getCustomIdFromKey(key);
            if (customId) spec += "@" + ShaderPool::getCustomShaderName(customId);
            list.push_back(spec);
        }
        return list;
    };
    return okResult(changing ? "Updated the export shader overrides." : "Read the export shader overrides.",
        Json{{"additions", specs(shaders.additions)}, {"exclusions", specs(shaders.exclusions)}});
}

ActionResult EditorActionExecutor::generateShaders(const Json& arguments, const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) return failResult("Shader generation cancelled.");
    ExportConfig config;
    config.targetDir = FileUtils::pathFromUtf8(arguments.value("target_dir", ""));
    std::string error;
    config.selectedBackends = parseBackendList(arguments.value("backends", "all"), error);
    if (!error.empty()) return failResult(error);
    if (!parseShaderOutputFormat(arguments.value("format", "header"), config.shaderOutputFormat)) {
        return failResult("Unsupported shader output format. Use binary, header, or json.");
    }
    for (const SceneProject& sceneProject : project->getScenes()) {
        config.selectedShaderKeys.insert(sceneProject.shaderKeys.begin(), sceneProject.shaderKeys.end());
    }
    if (config.selectedShaderKeys.empty()) {
        return failResult("No scene shader keys are available. Save or render scenes before generating shaders.");
    }

    Exporter exporter;
    const bool success = exporter.generateShaders(config);
    ExportProgress progress = exporter.getProgress();
    if (!success) {
        return failResult(progress.errorMessage.empty() ? "Shader generation failed." : progress.errorMessage);
    }
    return okResult("Generated shaders.", Json{{"target_dir", FileUtils::pathToGenericUtf8(config.targetDir)}});
}

bool EditorActionExecutor::validateCustomShaderValue(const std::string& propertyName, const Json& args, std::string& error) const {
    // Every shader-path property (per-component customShader, or a scene's per-type
    // default) is a project-relative base path; both must reference real files.
    static const std::set<std::string> shaderPathProperties = {
        "customShader", "customDepthShader", "customUnderwaterShader", "default_mesh_shader", "default_ui_shader",
        "default_sky_shader", "default_points_shader", "default_lines_shader", "default_water_shader"
    };
    if (!shaderPathProperties.count(propertyName)) {
        return true;
    }
    const std::string value = args.value("string_value", "");
    if (value.empty()) {
        return true; // empty resets to the built-in shader
    }

    Util::CustomShaderPaths paths = Util::resolveCustomShaderPaths(value);
    const fs::path projectPath = project->getProjectPath();
    if (!fs::exists(projectPath / paths.vert) || !fs::exists(projectPath / paths.frag)) {
        error = propertyName + " references missing files (" + paths.vert + ", " + paths.frag +
                "). Use fork_shader to create a fork, or write_shader_file to author the .vert/.frag, before setting " + propertyName + ".";
        return false;
    }
    return true;
}

ActionResult EditorActionExecutor::forkShader(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    const bool hasEntity = arguments.contains("entity_id") ||
                           (arguments.contains("entity_name") && arguments["entity_name"].is_string());
    const std::string shaderTypeArg = arguments.value("shader_type", "");
    const bool forkIncludes = arguments.value("fork_includes", false);
    const fs::path targetDirectory = FileUtils::pathFromUtf8(arguments.value(
        "directory", FileUtils::pathToGenericUtf8(ProjectUtils::defaultShaderForkDir()))).lexically_normal();
    if (targetDirectory != "." && !PathUtils::isSafeRelativePath(targetDirectory)) {
        return failResult("directory must be a safe project-relative path.");
    }

    // No entity selector: fork a scene-wide default shader for the given type instead of a
    // specific component (priority: component customShader > scene default > built-in).
    if (!hasEntity) {
        ShaderType shaderType;
        if (!parseShaderTypeName(shaderTypeArg, shaderType)) {
            return failResult("fork_shader requires entity_id/entity_name, or shader_type "
                              "(mesh, ui, sky, points, lines, or water) to fork a scene default.");
        }
        const std::string scenePropertyName = scenePropertyNameForShaderType(shaderType);

        // Re-forking would orphan the previous fork's files; require an explicit reset first.
        const std::string current = Catalog::getSceneProperty<std::string>(sceneProject->scene, scenePropertyName);
        if (!current.empty()) {
            return failResult("Scene already has a default " + shaderTypeArg + " shader (" + current +
                              "). Edit it with write_shader_file, or clear " + scenePropertyName +
                              " to reset before forking again.");
        }

        const std::string desiredName = sceneProject->name + " " + shaderTypeArg;
        const std::string forkName = ProjectUtils::makeUniqueShaderName(
            project->getProjectPath() / targetDirectory, shaderType, desiredName);
        auto* forkCmd = new ForkShaderCmd(project, sceneProject, shaderType, scenePropertyName,
                                          targetDirectory, forkName, forkIncludes);
        if (!forkCmd->isValid()) {
            const std::string error = forkCmd->getError();
            delete forkCmd;
            return failResult(error.empty() ? "Failed to plan the shader fork." : error);
        }

        const std::string base = forkCmd->getBase();
        CommandHandle::get(sceneId)->addCommandNoMerge(forkCmd);

        // The command writes the files as part of execute(); confirm so the AI gets accurate feedback.
        if (!fs::exists(project->getProjectPath() / (base + ".vert")) ||
            !fs::exists(project->getProjectPath() / (base + ".frag"))) {
            return failResult("Shader fork did not write its source files.");
        }

        if (resourcesWindow) {
            resourcesWindow->refreshCurrentDirectory();
        }
        Out::info("AI forked scene default shader: %s", base.c_str());

        return okResult("Forked scene default shader through the command history.",
                        Json{{"custom_shader", base},
                             {"vert_path", base + ".vert"},
                             {"frag_path", base + ".frag"},
                             {"scene_property", scenePropertyName}});
    }

    Entity entity;
    ComponentType component;
    ShaderType shaderType;
    std::string error;
    if (!resolveShaderComponent(sceneProject, arguments, entity, component, shaderType, error)) return failResult(error);

    // a mesh's shadow/SSAO depth pass and a water's underwater fade have forks of their own
    std::string propertyName = "customShader";
    if (lower(shaderTypeArg) == "depth") {
        if (component != ComponentType::MeshComponent) return failResult("Only a MeshComponent has a depth shader.");
        shaderType = ShaderType::DEPTH;
        propertyName = "customDepthShader";
    }else if (lower(shaderTypeArg) == "underwater") {
        if (component != ComponentType::WaterComponent) return failResult("Only a WaterComponent has an underwater shader.");
        shaderType = ShaderType::UNDERWATER;
        propertyName = "customUnderwaterShader";
    }

    // Re-forking would orphan the previous fork's files; require an explicit reset first.
    if (std::string* current = Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, component, propertyName)) {
        if (!current->empty()) {
            return failResult("Component already uses a custom shader (" + *current +
                              "). Edit it with write_shader_file, or clear " + propertyName + " to reset before forking again.");
        }
    }

    std::string desiredName = sceneProject->scene->getEntityName(entity);
    if (shaderType == ShaderType::DEPTH) desiredName += "_depth";
    const std::string forkName = ProjectUtils::makeUniqueShaderName(
        project->getProjectPath() / targetDirectory, shaderType, desiredName);
    auto* forkCmd = new ForkShaderCmd(project, sceneId, entity, component, shaderType,
                                      targetDirectory, forkName, forkIncludes);
    if (!forkCmd->isValid()) {
        const std::string error = forkCmd->getError();
        delete forkCmd;
        return failResult(error.empty() ? "Failed to plan the shader fork." : error);
    }

    const std::string base = forkCmd->getBase();
    CommandHandle::get(sceneId)->addCommandNoMerge(forkCmd);

    // The command writes the files as part of execute(); confirm so the AI gets accurate feedback.
    if (!fs::exists(project->getProjectPath() / (base + ".vert")) ||
        !fs::exists(project->getProjectPath() / (base + ".frag"))) {
        return failResult("Shader fork did not write its source files.");
    }

    if (resourcesWindow) {
        resourcesWindow->refreshCurrentDirectory();
    }
    Out::info("AI forked shader: %s", base.c_str());

    return okResult("Forked shader through the command history.",
                    Json{{"custom_shader", base},
                         {"property", propertyName},
                         {"vert_path", base + ".vert"},
                         {"frag_path", base + ".frag"}});
}

namespace {

const char* shaderUniformTypeName(ShaderUniformType type) {
    switch (type) {
        case ShaderUniformType::FLOAT:  return "float";
        case ShaderUniformType::FLOAT2: return "vec2";
        case ShaderUniformType::FLOAT3: return "vec3";
        case ShaderUniformType::FLOAT4: return "vec4";
        case ShaderUniformType::INT:    return "int";
        case ShaderUniformType::INT2:   return "ivec2";
        case ShaderUniformType::INT3:   return "ivec3";
        case ShaderUniformType::INT4:   return "ivec4";
        default: return "other";
    }
}

// one entry per member name; engine-written names are flagged
void appendShaderUniformMembers(Json& members, const CustomUniformBlock& block, const ShaderUniformValues& values) {
    for (const ShaderUniform& uniform : block.members) {
        bool listed = false;
        for (const Json& existing : members) {
            if (existing.value("name", "") == uniform.name) {
                listed = true;
                break;
            }
        }
        if (listed) continue;

        const bool reserved = ShaderUniforms::isReserved(uniform.name);
        const bool editable = ShaderUniforms::isEditable(uniform);
        Json member = {{"name", uniform.name},
                       {"type", shaderUniformTypeName(uniform.type)},
                       {"engine_written", reserved},
                       {"editable", editable && !reserved}};
        if (uniform.arrayCount > 1) {
            member["array_count"] = uniform.arrayCount;
        }
        if (!reserved && editable) {
            member["value"] = vector4Json(ShaderUniforms::get(values, uniform.name));
            member["has_value"] = ShaderUniforms::has(values, uniform.name);
        }
        members.push_back(member);
    }
}

void collectComponentShaderUniformMembers(Json& members, Json& warnings, EntityRegistry* registry, Entity entity,
                                          ComponentType component, const ShaderUniformValues& values) {
    std::vector<const CustomUniformBlock*> blocks = Catalog::getShaderUniformBlocks(registry, entity, component);
    for (const CustomUniformBlock* block : blocks) {
        appendShaderUniformMembers(members, *block, values);
    }
    warnings = Catalog::getShaderUniformWarnings(blocks);
}

// reflected from the pool; a pass still building or failed lists none
void collectPassShaderUniformMembers(Json& members, Json& warnings, const PostProcessPass& pass, bool& buildFailed) {
    uint16_t customId = ShaderPool::registerCustomShader(pass.shader);
    buildFailed = customId != 0 && ShaderPool::isShaderBuildFailed(ShaderType::POSTPROCESS, 0, customId);
    std::shared_ptr<ShaderRender> shader = ShaderPool::get(ShaderType::POSTPROCESS, 0, customId);
    warnings = Json::array();
    if (!buildFailed && shader && shader->isCreated()) {
        CustomUniformBlock block;
        block.resolve(shader->shaderData, "u_fs_postParams");
        appendShaderUniformMembers(members, block, pass.uniforms);
        warnings = Catalog::getShaderUniformWarnings({&block});
    }
}

// the value is stored either way; the note says when the shader cannot use it
std::string shaderUniformSetNote(const Json& members, const std::string& name, bool& declared) {
    declared = false;
    for (const Json& member : members) {
        if (member.value("name", "") == name) {
            declared = true;
            if (!member.value("editable", false)) {
                return " Note: '" + name + "' is a matrix or array member, which takes no value and stays zero; "
                       "declare one scalar or vector member per value instead.";
            }
            return "";
        }
    }
    return " Note: the loaded shader declares no member named '" + name +
           "' (it may still be building, or the name differs); the value is kept and applies once declared.";
}

bool parseShaderUniformValue(const Json& args, Vector4& out, std::string& error) {
    if (args.contains("vector4_value")) {
        if (!parseVector4(args["vector4_value"], out)) { error = "vector4_value must have x, y, z and w."; return false; }
        return true;
    }
    if (args.contains("vector3_value")) {
        Vector3 v;
        if (!parseVector3(args["vector3_value"], v)) { error = "vector3_value must have x, y and z."; return false; }
        out = Vector4(v.x, v.y, v.z, 0.0f);
        return true;
    }
    if (args.contains("vector2_value")) {
        Vector2 v;
        if (!parseVector2(args["vector2_value"], v)) { error = "vector2_value must have x and y."; return false; }
        out = Vector4(v.x, v.y, 0.0f, 0.0f);
        return true;
    }
    if (args.contains("number_value") && args["number_value"].is_number()) {
        out = Vector4(args["number_value"].get<float>(), 0.0f, 0.0f, 0.0f);
        return true;
    }
    error = "set_shader_uniform requires number_value, vector2_value, vector3_value or vector4_value.";
    return false;
}

} // namespace

ActionResult EditorActionExecutor::inspectShaderUniforms(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return expectedMissResult("Scene not found.");

    Json members = Json::array();

    if (arguments.contains("pass_index")) {
        const std::vector<PostProcessPass>& passes = sceneProject->scene->getPostProcessPasses();
        const int passIndex = arguments.value("pass_index", -1);
        if (passIndex < 0 || passIndex >= (int)passes.size()) {
            return expectedMissResult("Post-process pass not found; the scene has " + std::to_string(passes.size()) + " pass(es).");
        }
        const PostProcessPass& pass = passes[passIndex];

        Json warnings;
        bool buildFailed = false;
        collectPassShaderUniformMembers(members, warnings, pass, buildFailed);

        return okResult("Inspected post-process pass uniforms.",
                        Json{{"scene_id", sceneId},
                             {"pass_index", passIndex},
                             {"shader", pass.shader},
                             {"enabled", pass.enabled},
                             {"build_failed", buildFailed},
                             {"members", members},
                             {"warnings", warnings},
                             {"values", shaderUniformValuesJson(pass.uniforms)}});
    }

    Entity entity;
    ComponentType component;
    ShaderType shaderType;
    std::string error;
    if (!resolveShaderComponent(sceneProject, arguments, entity, component, shaderType, error)) return expectedMissResult(error);

    ShaderUniformValues* valuesRef = Catalog::getPropertyRef<ShaderUniformValues>(sceneProject->scene, entity, component, "shaderUniforms");
    std::string* customShader = Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, component, "customShader");
    if (!valuesRef || !customShader) return failResult("Component has no shader uniforms.");

    const std::string effectiveShader = customShader->empty()
        ? sceneProject->scene->getDefaultCustomShader(shaderType) : *customShader;

    Json warnings;
    collectComponentShaderUniformMembers(members, warnings, sceneProject->scene, entity, component, *valuesRef);

    Json result{{"scene_id", sceneId},
                {"entity_id", entity},
                {"component", Catalog::getComponentName(component)},
                {"custom_shader", *customShader},
                {"effective_shader", effectiveShader},
                {"build_failed", Catalog::isCustomShaderBuildFailed(sceneProject->scene, entity, component)},
                {"members", members},
                {"warnings", warnings},
                {"values", shaderUniformValuesJson(*valuesRef)}};
    // a mesh's depth fork declares blocks of its own, listed in the same members
    if (std::string* customDepthShader = Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, component, "customDepthShader")) {
        result["custom_depth_shader"] = *customDepthShader;
        result["depth_build_failed"] = Catalog::isCustomDepthShaderBuildFailed(sceneProject->scene, entity);
    }
    // and so does a water's underwater fork
    if (std::string* customUnderwaterShader = Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, component, "customUnderwaterShader")) {
        result["custom_underwater_shader"] = *customUnderwaterShader;
        result["underwater_build_failed"] = Catalog::isCustomUnderwaterShaderBuildFailed(sceneProject->scene, entity);
    }

    return okResult("Inspected shader uniforms.", result);
}

ActionResult EditorActionExecutor::setShaderUniform(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    const std::string name = arguments.value("name", "");
    if (name.empty()) return failResult("set_shader_uniform requires name.");
    if (ShaderUniforms::isReserved(name)) {
        return failResult("'" + name + "' is written by the engine every frame and cannot be set; rename the shader member to drive it yourself.");
    }

    const bool remove = arguments.value("remove", false);
    Vector4 value;
    std::string error;
    if (!remove && !parseShaderUniformValue(arguments, value, error)) return failResult(error);

    if (arguments.contains("pass_index")) {
        std::vector<PostProcessPass> passes = sceneProject->scene->getPostProcessPasses();
        const int passIndex = arguments.value("pass_index", -1);
        if (passIndex < 0 || passIndex >= (int)passes.size()) {
            return failResult("Post-process pass not found; the scene has " + std::to_string(passes.size()) + " pass(es).");
        }
        if (remove) {
            if (!passes[passIndex].removeUniform(name)) return expectedMissResult("Pass has no value named '" + name + "'.");
        } else {
            passes[passIndex].setUniform(name, value);
        }
        CommandHandle::get(sceneId)->addCommandNoMerge(
            new ScenePropertyCmd<std::vector<PostProcessPass>>(project, sceneId, "post_process", passes));

        Json declared = Json::array();
        Json warnings;
        bool buildFailed = false;
        collectPassShaderUniformMembers(declared, warnings, passes[passIndex], buildFailed);
        bool declaredMember = false;
        std::string message = remove ? "Removed post-process uniform through the command history."
                                     : "Set post-process uniform through the command history.";
        if (!remove) message += shaderUniformSetNote(declared, name, declaredMember);
        return okResult(message, Json{{"pass_index", passIndex}, {"name", name}, {"declared", declaredMember}});
    }

    Entity entity;
    ComponentType component;
    ShaderType shaderType;
    if (!resolveShaderComponent(sceneProject, arguments, entity, component, shaderType, error)) return failResult(error);

    ShaderUniformValues* valuesRef = Catalog::getPropertyRef<ShaderUniformValues>(sceneProject->scene, entity, component, "shaderUniforms");
    if (!valuesRef) return failResult("Component has no shader uniforms.");

    ShaderUniformValues values = *valuesRef;
    if (remove) {
        if (!ShaderUniforms::remove(values, name)) return expectedMissResult("Component has no value named '" + name + "'.");
    } else {
        ShaderUniforms::set(values, name, value);
    }
    CommandHandle::get(sceneId)->addCommandNoMerge(
        new PropertyCmd<ShaderUniformValues>(project, sceneId, entity, component, "shaderUniforms", values));

    Json declared = Json::array();
    Json warnings;
    collectComponentShaderUniformMembers(declared, warnings, sceneProject->scene, entity, component, values);
    bool declaredMember = false;
    std::string message = remove ? "Removed shader uniform through the command history."
                                 : "Set shader uniform through the command history.";
    if (!remove) message += shaderUniformSetNote(declared, name, declaredMember);
    return okResult(message, Json{{"entity_id", entity},
                                  {"component", Catalog::getComponentName(component)},
                                  {"name", name},
                                  {"declared", declaredMember}});
}

ActionResult EditorActionExecutor::writeShaderFile(const Json& arguments) {
    std::string error;
    fs::path rel;
    // A shader source may be edited (forked entry point) or newly created (shared include).
    if (!safeRelativePath(project, arguments, "path", rel, error, false)) {
        return failResult(error);
    }

    const std::string ext = lower(rel.extension().string());
    if (ext != ".vert" && ext != ".frag" && ext != ".glsl") {
        return failResult("path must point to a .vert, .frag, or .glsl shader file.");
    }

    const std::string content = arguments.value("content", "");
    constexpr size_t kMaxShaderBytes = 2 * 1024 * 1024;
    if (content.size() > kMaxShaderBytes) {
        return failResult("Shader content is too large for an AI edit.");
    }

    const fs::path fullPath = project->getProjectPath() / rel;
    std::error_code ec;
    fs::create_directories(fullPath.parent_path(), ec);
    std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return failResult("Failed to open shader file for writing.");
    }
    out << content;
    out.close();
    if (!out) {
        return failResult("Failed to write shader file.");
    }

    // Editing a forked source (or shared include) invalidates compiled shaders so the
    // viewport recompiles them; mirrors CodeEditor saving a .vert/.frag/.glsl.
    project->invalidateCustomShaders();

    if (resourcesWindow) {
        resourcesWindow->refreshCurrentDirectory();
    }
    Out::info("AI wrote shader file: %s", rel.generic_string().c_str());

    return okResult("Wrote shader file.",
                    Json{{"path", FileUtils::pathToGenericUtf8(rel)},
                         {"bytes", content.size()}});
}


ActionResult EditorActionExecutor::createTerrainHeightmap(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return failResult("Scene not found.");
    }
    if (sceneProject->sceneType != SceneType::SCENE_3D) {
        return failResult("Terrain heightmaps are only supported in 3D scenes.");
    }

    Entity entity = resolveTerrainEntity(project, sceneProject, arguments);
    if (entity == NULL_ENTITY) {
        return failResult("Terrain entity not found. Provide entity_id/entity_name or select a terrain.");
    }

    TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(entity);
    if (!terrain) {
        return failResult("Entity is not a terrain.");
    }

    int resolution = arguments.value("resolution", project->getTerrainEditorSettings().heightMapResolution);
    resolution = std::max(2, std::min(2048, resolution));

    std::string mode = arguments.value("mode", "middle");
    if (mode.empty()) mode = "middle";
    float baseHeight = arguments.value("base_height", mode == "flat" ? 0.0f : 0.5f);
    if (mode == "middle") {
        baseHeight = 0.5f;
    }
    float amplitude = arguments.value("amplitude", 0.35f);
    float frequency = arguments.value("frequency", 4.0f);
    int octaves = arguments.value("octaves", 4);

    uint32_t seed = 0;
    if (arguments.contains("seed") && arguments["seed"].is_number_integer()) {
        seed = static_cast<uint32_t>(arguments["seed"].get<int64_t>());
    } else if (arguments.contains("seed") && arguments["seed"].is_number_unsigned()) {
        seed = arguments["seed"].get<uint32_t>();
    } else {
        seed = std::random_device{}();
    }

    TerrainHeightmapRequest request;
    request.resolution = resolution;
    request.mode = mode;
    request.seed = seed;
    request.baseHeight = baseHeight;
    request.amplitude = amplitude;
    request.frequency = frequency;
    request.octaves = octaves;

    TerrainHeightmapResult generated = AiTerrainCapability::createHeightmap(project, sceneId, entity, request);
    if (!generated.success) {
        return failResult(generated.error.empty() ? "Failed to write generated terrain heightmap."
                                                  : generated.error);
    }
    Texture heightTexture = generated.texture;
    const std::string relativePath = generated.relativePath;

    auto onChanged = [project = this->project, sceneId, entity]() {
        SceneProject* sceneProject = project ? project->getScene(sceneId) : nullptr;
        if (!sceneProject || !sceneProject->scene) return;
        TerrainComponent* terrain = sceneProject->scene->findComponent<TerrainComponent>(entity);
        if (!terrain) return;
        terrain->heightMapLoaded = false;
        terrain->needUpdateTerrain = true;
        terrain->needUpdateTexture = true;
        terrain->heightMap.invalidateRender();
    };

    CommandHandle::get(sceneId)->addCommandNoMerge(
        new PropertyCmd<Texture>(project, sceneId, entity, ComponentType::TerrainComponent,
                                 "heightMap", heightTexture, onChanged));

    return okResult("Created terrain heightmap through the command history.",
                    Json{{"scene_id", sceneId},
                         {"entity_id", entity},
                         {"entity_name", sceneProject->scene->getEntityName(entity)},
                         {"path", relativePath},
                         {"resolution", resolution},
                         {"mode", mode},
                         {"seed", seed}});
}

ActionResult EditorActionExecutor::importProjectModel(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return failResult("Scene not found.");
    }
    if (sceneProject->sceneType != SceneType::SCENE_3D) {
        return failResult("Model import is only supported in 3D scenes.");
    }

    fs::path relPath = FileUtils::pathFromUtf8(arguments.value("model_path", ""));
    if (!PathUtils::isSafeRelativePath(relPath) || !Util::isModelFile(relPath.string())) {
        return failResult("model_path must be a safe project-relative .gltf, .glb, .obj, or .fbx file.");
    }
    fs::path fullPath = project->getProjectPath() / relPath;
    if (!fs::exists(fullPath)) {
        return failResult("Model file does not exist in the project.");
    }
    if (!isInsideAssets(project, relPath)) {
        return failResult(outsideAssetsError(project, "model_path"));
    }

    Vector3 position = Vector3::ZERO;
    if (arguments.contains("position")) parseVector3(arguments["position"], position);
    std::string entityName = arguments.value("entity_name", relPath.stem().string());
    if (entityName.empty()) entityName = "Model";

    ModelLoadCmd* cmd = new ModelLoadCmd(project, sceneId, entityName, position, assetPathFromAi(project, relPath));
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(cmd)) {
        return failResult("Failed to import the model.");
    }

    if (resourcesWindow) {
        resourcesWindow->requestThumbnailGeneration(fullPath, false);
    }

    return okResult("Imported project model through ModelLoadCmd.",
                    Json{{"scene_id", sceneId}, {"entity_id", cmd->getEntity()}});
}

ActionResult EditorActionExecutor::reloadProjectModel(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return failResult("Scene not found.");
    }
    if (project->isAnyScenePlaying()) {
        return failResult(busyError(project, "reloading a model"));
    }
    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) {
        return failResult("Entity not found.");
    }
    ModelComponent* model = sceneProject->scene->findComponent<ModelComponent>(entity);
    if (!model || model->filename.empty()) {
        return failResult("The entity has no model file to reload.");
    }

    const std::string filename = model->filename;
    // an earlier load still running here would finish on top of the reload
    if (sceneProject->scene->getSystem<MeshSystem>()->isAsyncModelLoadPending(entity, filename)) {
        return failResult("The model is still loading. Retry once it finishes.");
    }
    // read before anything changes, so a missing or broken file leaves the model as it is
    if (!project->prepareModelFile(filename) || !MeshSystem::reparseModelFile(filename)) {
        return failResult("Can't read the model file. Check read_output_log for the reason.");
    }
    if (!CommandHandle::get(sceneId)->addCommandNoMerge(
            new ModelLoadCmd(project, sceneId, entity, filename, model->mergeStaticMeshes))) {
        return failResult("Failed to reload the model.");
    }

    return okResult("Reloaded the model through ModelLoadCmd.",
                    Json{{"scene_id", sceneId},
                         {"entity_id", entity},
                         {"model_path", assetPathToAi(project, filename)}});
}

ActionResult EditorActionExecutor::searchCuratedAssets(const Json& arguments, const std::atomic<bool>* cancel) {
    if (!httpClient) {
        return failResult("HTTP client is unavailable.");
    }

    const std::string provider = lower(arguments.value("provider", ""));
    const std::string query = lower(arguments.value("query", ""));
    int maxResults = std::max(1, std::min(12, arguments.value("max_results", 6)));
    Json results = Json::array();

    if (provider == "polyhaven") {
        HttpResponse response = httpClient->get("https://api.polyhaven.com/assets?t=models",
            {"Accept: application/json", "User-Agent: DoriaxEditorAI/1.0"}, cancel);
        if (!response.error.empty()) return failResult(response.error);
        if (response.status < 200 || response.status >= 300) {
            return failResult("Poly Haven returned HTTP " + std::to_string(response.status));
        }
        Json root = Json::parse(response.body, nullptr, false);
        if (!root.is_object()) return failResult("Unexpected Poly Haven response.");
        for (auto it = root.begin(); it != root.end() && static_cast<int>(results.size()) < maxResults; ++it) {
            const std::string id = it.key();
            const Json& meta = it.value();
            std::string name = meta.value("name", id);
            std::string haystack = lower(id + " " + name + " " + meta.value("categories", Json::array()).dump() + " " + meta.value("tags", Json::array()).dump());
            if (haystack.find(query) == std::string::npos) continue;
            std::string author;
            const Json authors = meta.value("authors", Json::object());
            for (auto a = authors.begin(); a != authors.end(); ++a) {
                author += (author.empty() ? "" : ", ") + a.key();
            }
            results.push_back({
                {"provider", "polyhaven"},
                {"asset_id", id},
                {"title", name},
                {"author", author},
                {"license", "CC0 / Poly Haven license terms"},
                {"source_url", "https://polyhaven.com/a/" + id},
                {"download_url", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/" + id + "/" + id + "_1k.gltf"},
                {"polycount", meta.value("polycount", 0)}
            });
        }
        return okResult("Searched Poly Haven. API commercial usage may require custom licensing.", Json{{"results", results}});
    }

    if (provider == "sketchfab") {
        std::string url = "https://api.sketchfab.com/v3/search?type=models&downloadable=true&q=" +
                          HttpClient::urlEncode(query);
        HttpResponse response = httpClient->get(url, {"Accept: application/json"}, cancel);
        if (!response.error.empty()) return failResult(response.error);
        if (response.status < 200 || response.status >= 300) {
            return failResult("Sketchfab returned HTTP " + std::to_string(response.status) +
                              ". Search may require user authentication.");
        }
        Json root = Json::parse(response.body, nullptr, false);
        if (!root.is_object()) return failResult("Unexpected Sketchfab response.");
        const Json& items = root.value("results", Json::array());
        if (items.is_array()) {
            for (const auto& item : items) {
                if (static_cast<int>(results.size()) >= maxResults) break;
                results.push_back({
                    {"provider", "sketchfab"},
                    {"asset_id", item.value("uid", "")},
                    {"title", item.value("name", "")},
                    {"author", item.value("user", Json::object()).value("displayName", "")},
                    {"license", item.value("license", Json::object()).value("label", "")},
                    {"source_url", item.value("viewerUrl", "")},
                    {"download_note", "Sketchfab Download API requires end-user OAuth before download URLs can be used."}
                });
            }
        }
        return okResult("Searched Sketchfab metadata. Downloads require Sketchfab user OAuth.", Json{{"results", results}});
    }

    return warningResult("Unsupported curated provider: " + provider);
}

ActionResult EditorActionExecutor::downloadCuratedAsset(const Json& arguments, const std::atomic<bool>* cancel) {
    if (!httpClient) {
        return failResult("HTTP client is unavailable.");
    }

    const std::string url = arguments.value("download_url", "");
    std::string title = arguments.value("title", "Asset");
    std::string slug = arguments.value("slug", "");
    if (slug.empty()) slug = PathUtils::slugify(title);
    slug = PathUtils::slugify(slug);

    std::string filename = filenameFromUrl(url, slug + ".glb");
    fs::path filenamePath(filename);
    if (!Util::isModelFile(filenamePath.string())) {
        return failResult("Only direct .gltf, .glb, .obj, and .fbx downloads are supported in v1. Archive extraction is intentionally not enabled yet.");
    }

    fs::path stagingDir = project->getProjectInternalPath() / "ai" / "staging" / slug;
    fs::path importDir = project->getAssetsPath() / "ai_imports" / slug;
    fs::path stagingFile = stagingDir / filenamePath.filename();
    fs::path importedFile = PathUtils::uniqueChildPath(importDir, filenamePath.stem().string(), filenamePath.extension().string());

    HttpResponse response = httpClient->downloadToFile(url, stagingFile, {"Accept: */*"}, cancel);
    if (!response.error.empty()) return failResult(response.error);
    if (response.status < 200 || response.status >= 300) {
        return failResult("Download returned HTTP " + std::to_string(response.status));
    }

    std::error_code ec;
    fs::create_directories(importDir, ec);
    if (ec) return failResult("Failed to create import directory: " + ec.message());
    fs::copy_file(stagingFile, importedFile, fs::copy_options::overwrite_existing, ec);
    if (ec) return failResult("Failed to copy downloaded asset into project: " + ec.message());

    if (lower(importedFile.extension().string()) == ".gltf") {
        for (const auto& [relative, fileUrl] : gltfDependencies(httpClient, arguments, stagingFile, cancel)) {
            if (!PathUtils::isSafeRelativePath(relative)) {
                return failResult("The glTF references an unsafe path: " + relative);
            }
            HttpResponse dependency = httpClient->downloadToFile(fileUrl, importDir / relative, {"Accept: */*"}, cancel);
            if (!dependency.error.empty()) return failResult(dependency.error);
            if (dependency.status < 200 || dependency.status >= 300) {
                return failResult("Download of " + relative + " returned HTTP " + std::to_string(dependency.status));
            }
        }
    }

    fs::path attributionPath = project->getProjectInternalPath() / "ai" / "asset_attributions.yaml";
    YAML::Node root;
    if (fs::exists(attributionPath)) {
        try {
            root = TextEncoding::loadYamlFile(attributionPath);
        } catch (...) {
            root = YAML::Node(YAML::NodeType::Map);
        }
    }
    if (!root["assets"]) root["assets"] = YAML::Node(YAML::NodeType::Sequence);
    YAML::Node entry;
    entry["provider"] = arguments.value("provider", "");
    entry["asset_id"] = arguments.value("asset_id", "");
    entry["title"] = title;
    entry["author"] = arguments.value("author", "");
    entry["license"] = arguments.value("license", "");
    entry["source_url"] = arguments.value("source_url", "");
    entry["local_path"] = fs::relative(importedFile, project->getProjectPath()).generic_string();
    root["assets"].push_back(entry);
    fs::create_directories(attributionPath.parent_path(), ec);
    std::ofstream out(attributionPath, std::ios::binary);
    out << YAML::Dump(root);

    if (resourcesWindow) {
        resourcesWindow->refreshCurrentDirectory();
        resourcesWindow->requestThumbnailGeneration(importedFile, true);
    }

    Json data = {
        {"imported_path", FileUtils::pathToGenericUtf8(fs::relative(importedFile, project->getProjectPath()))},
        {"attribution_path", FileUtils::pathToGenericUtf8(fs::relative(attributionPath, project->getProjectPath()))}
    };

    if (arguments.contains("scene_id") || arguments.contains("entity_name") || arguments.contains("position")) {
        Json importArgs = {
            {"scene_id", resolveSceneId(project, arguments)},
            {"model_path", data["imported_path"].get<std::string>()},
            {"entity_name", arguments.value("entity_name", title)}
        };
        if (arguments.contains("position")) {
            importArgs["position"] = arguments["position"];
        }
        ActionResult importResult = importProjectModel(importArgs);
        data["import_result"] = importResult.message;
        if (!importResult.success) {
            return okResult("Downloaded asset and recorded attribution, but import was skipped: " + importResult.message, data);
        }
    }

    Out::success("AI imported curated asset '%s'", title.c_str());
    return okResult("Downloaded curated asset and recorded attribution.", data);
}

ActionResult EditorActionExecutor::readResourceFile(const Json& arguments) {
    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "path", rel, error, false)) {
        return warningResult(error);
    }

    const std::string ext = lower(rel.extension().string());
    if (!isReadableTextResource(ext)) {
        return warningResult(
            "path extension is not supported for read_resource_file.");
    }

    const fs::path projectRoot = project->getProjectPath();
    if (projectRoot.empty() || !fs::exists(projectRoot)) {
        return failResult("Project path does not exist.");
    }
    const fs::path fullPath = projectRoot / rel;
    if (!fs::exists(fullPath)) {
        return expectedMissResult(
            "Resource file not found: " + FileUtils::pathToGenericUtf8(rel));
    }
    std::ifstream in(fullPath, std::ios::binary);
    if (!in) {
        return failResult(
            "Failed to open resource file: " + FileUtils::pathToGenericUtf8(rel));
    }

    constexpr size_t kMaxBytes = 512 * 1024;
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (content.size() > kMaxBytes) {
        return warningResult("Resource file is too large to read (max 512 KB).");
    }

    return okResult("Read resource file.",
                    Json{{"path", FileUtils::pathToGenericUtf8(rel.lexically_normal())},
                         {"bytes", content.size()},
                         {"content", content}});
}

static bool isEngineSourceExt(const std::string& ext) {
    static const std::set<std::string> allowed = {
        ".h", ".hpp", ".inl", ".cpp", ".cc", ".c", ".lua"
    };
    return allowed.count(ext) > 0;
}

ActionResult EditorActionExecutor::searchEngineSource(const Json& arguments) {
    const std::string rawQuery = arguments.value("query", "");
    if (rawQuery.empty()) return failResult("search_engine_source requires a non-empty query.");
    const std::string query = lower(rawQuery);
    const std::string pathFilter = lower(arguments.value("path_filter", ""));
    int maxResults = arguments.value("max_results", 40);
    maxResults = std::max(1, std::min(80, maxResults));

    const fs::path engineRoot = FileUtils::getEngineDir();
    if (!fs::exists(engineRoot)) {
        return failResult("Engine source not found at: " + engineRoot.generic_string());
    }
    // core/ holds the scriptable API surface (math, object, input, components, bindings);
    // skip the huge third-party libs/ tree so results stay fast and relevant.
    fs::path searchRoot = engineRoot / "core";
    if (!fs::exists(searchRoot)) searchRoot = engineRoot;

    Json matches = Json::array();
    std::error_code ec;
    for (fs::recursive_directory_iterator it(searchRoot, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec && static_cast<int>(matches.size()) < maxResults;
         it.increment(ec)) {
        const fs::path path = it->path();
        if (it->is_directory(ec)) {
            const std::string dir = path.filename().string();
            if (dir == "libs" || dir == "build" || dir == ".git") it.disable_recursion_pending();
            continue;
        }
        if (!isEngineSourceExt(lower(path.extension().string()))) continue;

        fs::path rel = fs::relative(path, engineRoot, ec);
        if (ec) continue;
        const std::string relText = rel.generic_string();
        if (!pathFilter.empty() && lower(relText).find(pathFilter) == std::string::npos) continue;

        std::string content;
        if (!readWholeFile(path, content)) continue;
        if (content.size() > 1024 * 1024) continue; // skip very large files

        std::istringstream stream(content);
        std::string line;
        int lineNo = 0;
        while (std::getline(stream, line) && static_cast<int>(matches.size()) < maxResults) {
            ++lineNo;
            if (lower(line).find(query) == std::string::npos) continue;
            size_t start = line.find_first_not_of(" \t");
            std::string trimmed = (start == std::string::npos) ? line : line.substr(start);
            if (trimmed.size() > 240) trimmed = trimmed.substr(0, 240) + " ...";
            matches.push_back({{"path", relText}, {"line", lineNo}, {"text", trimmed}});
        }
    }

    return okResult("Found " + std::to_string(matches.size()) +
                    " engine source match(es). Use read_engine_source for full context.",
                    Json{{"matches", matches}});
}

ActionResult EditorActionExecutor::readEngineSource(const Json& arguments) {
    if (!arguments.contains("path") || !arguments["path"].is_string()) {
        return failResult("read_engine_source requires path.");
    }
    fs::path rel = FileUtils::pathFromUtf8(arguments["path"].get<std::string>()).lexically_normal();
    if (!PathUtils::isSafeRelativePath(rel)) {
        return warningResult(
            "path must be a safe engine-source-relative path, "
            "e.g. core/math/Quaternion.h.");
    }
    if (!isEngineSourceExt(lower(rel.extension().string()))) {
        return warningResult(
            "read_engine_source only reads engine source files "
            "(.h, .hpp, .inl, .cpp, .lua).");
    }

    const fs::path engineRoot = FileUtils::getEngineDir();
    if (!fs::exists(engineRoot)) {
        return failResult("Engine source not found at: " + engineRoot.generic_string());
    }
    const fs::path fullPath = engineRoot / rel;
    if (!fs::exists(fullPath)) {
        return expectedMissResult(
            "Engine source file not found: " + rel.generic_string());
    }

    std::string content;
    if (!readWholeFile(fullPath, content)) return failResult("Failed to open engine source file.");

    const int startLine = arguments.value("start_line", 0);
    const int endLine = arguments.value("end_line", 0);
    if (startLine > 0 || endLine > 0) {
        std::istringstream stream(content);
        std::string line;
        int lineNo = 0;
        std::string slice;
        while (std::getline(stream, line)) {
            ++lineNo;
            if (startLine > 0 && lineNo < startLine) continue;
            if (endLine > 0 && lineNo > endLine) break;
            slice += line;
            slice += '\n';
        }
        content = slice;
    }

    constexpr size_t kMaxBytes = 256 * 1024;
    if (content.size() > kMaxBytes) {
        content = content.substr(0, kMaxBytes) + "\n... (truncated)";
    }

    return okResult("Read engine source file.",
                    Json{{"path", FileUtils::pathToGenericUtf8(rel)}, {"content", content}});
}

ActionResult EditorActionExecutor::readOutputLog(const Json& arguments) {
    int maxLines = arguments.value("max_lines", 150);
    maxLines = std::max(1, std::min(600, maxLines));
    const bool errorsOnly = arguments.value("errors_only", false);

    std::string log = Out::getRecentLog(static_cast<size_t>(maxLines), errorsOnly);

    constexpr size_t kMaxBytes = 24 * 1024;
    if (log.size() > kMaxBytes) {
        log = "... (older output truncated)\n" + log.substr(log.size() - kMaxBytes);
    }

    return okResult("Recent editor output log.", Json{{"log", log}});
}

ActionResult EditorActionExecutor::setMainCamera(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity cameraEntity = NULL_ENTITY;
    if (arguments.value("clear", false)) {
        cameraEntity = NULL_ENTITY;
    } else {
        cameraEntity = resolveEntity(sceneProject, arguments);
        if (cameraEntity == NULL_ENTITY) return failResult("Camera entity not found.");
        if (!sceneProject->scene->findComponent<CameraComponent>(cameraEntity)) {
            return failResult("Entity does not have a Camera component.");
        }
    }

    CommandHandle::get(sceneId)->addCommandNoMerge(new SetMainCameraCmd(project, sceneId, cameraEntity));
    return okResult("Set main camera through the command history.",
                    Json{{"scene_id", sceneId}, {"main_camera", cameraEntity}});
}

ActionResult EditorActionExecutor::openScene(const Json& arguments) {
    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "scene_path", rel, error, true)) {
        return failResult(error);
    }
    if (!Util::isSceneFile(rel.string())) {
        return failResult("scene_path must point to a .scene file.");
    }
    if (project->isAnyScenePlaying()) {
        return failResult(busyError(project, "opening a scene"));
    }

    const bool closePrevious = arguments.value("close_previous", false);
    project->openScene(rel, closePrevious);
    // a focused scene window would select its own scene back
    getEditorHost().requestScenePlayFocus(project->getSelectedSceneId());
    return okResult("Requested scene open.",
                    Json{{"scene_path", FileUtils::pathToGenericUtf8(rel.lexically_normal())},
                         {"selected_scene_id", project->getSelectedSceneId()}});
}

ActionResult EditorActionExecutor::selectScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) return failResult("Scene not found.");
    if (!sceneProject->opened) return failResult("Scene is not open. Use open_scene first.");

    project->setSelectedSceneId(sceneId);
    // a focused scene window would select its own scene back
    getEditorHost().requestScenePlayFocus(sceneId);
    return okResult("Selected scene tab.", Json{{"scene_id", sceneId}, {"scene_name", sceneProject->name}});
}

ActionResult EditorActionExecutor::regenerateMeshGeometry(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    if (!hasComponent(sceneProject->scene, entity, ComponentType::MeshComponent)) {
        return failResult("Entity has no Mesh component.");
    }

    // Any mesh may be replaced by a primitive except one whose geometry is regenerated for it:
    // from a model source on load, or from its own component in the MeshSystem::update sweep,
    // which would throw the primitive away as soon as that component is next dirtied.
    Signature signature = sceneProject->scene->getSignature(entity);
    if (Stream::isModelBackedMesh(entity, sceneProject->scene, signature)) {
        return failResult("Entity geometry comes from a model file and is rebuilt from that source on load.");
    }
    if (Stream::isGeneratedMesh(sceneProject->scene, signature)) {
        return failResult("Entity geometry is generated from its sprite, tilemap, mesh polygon, or terrain component and is rebuilt on load.");
    }

    MeshComponent meshComp = sceneProject->scene->getComponent<MeshComponent>(entity);

    ShapeParameters shapeParams;
    std::string geometryError;
    if (!parseGeometryTypeValue(arguments, shapeParams, geometryError)) {
        return failResult(geometryError);
    }
    applyShapeParameters(arguments, shapeParams);

    std::shared_ptr<MeshSystem> meshSys = sceneProject->scene->getSystem<MeshSystem>();
    if (!meshSys) return failResult("Mesh system is unavailable.");
    updateMeshShape(meshComp, meshSys.get(), shapeParams);
    CommandHandle::get(sceneId)->addCommandNoMerge(new MeshChangeCmd(project, sceneId, entity, meshComp));

    // Geometry is rebuilt from scratch, so omitted parameters fell back to defaults rather than
    // keeping their previous values. The resulting bounds make that visible.
    Json data = {{"scene_id", sceneId}, {"entity_id", entity}, {"geometry_type", shapeParams.geometryType}};
    addMeshBounds(data, sceneProject->scene, entity);

    return okResult("Regenerated mesh geometry through the command history.", data);
}

ActionResult EditorActionExecutor::deleteScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject) return failResult("Scene not found.");
    if (project->getScenes().size() <= 1) {
        return failResult("Cannot delete the last scene in the project.");
    }
    if (project->isAnyScenePlaying()) {
        return failResult(busyError(project, "deleting a scene"));
    }
    if (sceneProject->filepath.empty()) {
        return failResult("Cannot delete an unsaved scene through AI because it cannot be restored on undo.");
    }
    if (project->hasSceneUnsavedChanges(sceneId)) {
        return failResult("Save the scene and its dependent resources before deleting it through AI.");
    }

    const std::string sceneName = sceneProject->name;
    const fs::path scenePath = sceneProject->filepath.lexically_normal();
    project->getProjectCommandHistory()->addCommandNoMerge(new RemoveProjectSceneCmd(project, sceneId));
    if (project->getScene(sceneId) || project->findSceneByPath(scenePath) != NULL_PROJECT_SCENE) {
        return failResult("Failed to remove scene.");
    }
    return okResult("Removed scene from project through the project command history.",
                    Json{{"removed_scene_id", sceneId}, {"removed_scene_name", sceneName},
                         {"scene_path", FileUtils::pathToGenericUtf8(scenePath)},
                         {"selected_scene_id", project->getSelectedSceneId()}});
}

ActionResult EditorActionExecutor::saveProject(const Json& arguments) {
    if (arguments.contains("path") && arguments["path"].is_string()) {
        if (!project->isTempProject()) {
            return failResult("The project already lives at " + FileUtils::pathToUtf8(project->getProjectPath()) + "; save it without path.");
        }
        if (project->isAnyScenePlaying()) {
            return failResult(busyError(project, "moving the project"));
        }
        // Open buffers would keep pointing at the temp folder
        if (hasUnsavedScriptEdits()) {
            return failResult(kUnsavedScriptEdits);
        }
        std::string error;
        fs::path path;
        if (!prepareNewProjectDirectory(project, arguments, path, error)) return failResult(error);

        const std::string name = optionalString(arguments, "name");
        if (!name.empty()) {
            project->setName(name);
        }
        if (!project->saveProjectToPath(path)) {
            return failResult("Failed to save the project to " + FileUtils::pathToUtf8(path) + ".");
        }
        return okResult("Moved the project out of the temp folder.", Json{{"path", FileUtils::pathToUtf8(path)}});
    }

    if (!project->saveProject(false)) {
        return failResult("Failed to save project.");
    }
    return okResult("Saved project.", Json{{"path", FileUtils::pathToUtf8(project->getProjectPath())}});
}

ActionResult EditorActionExecutor::setProjectSettings(const Json& arguments) {
    // Checked before anything changes, a rejected request keeps the project as it was
    if (arguments.contains("loading_scene_id")) {
        const uint32_t loadingSceneId = arguments["loading_scene_id"].get<uint32_t>();
        if (loadingSceneId != NULL_PROJECT_SCENE && !project->getScene(loadingSceneId)) {
            return failResult("loading_scene_id " + std::to_string(loadingSceneId) + " is not a scene of this project.");
        }
    }
    if (arguments.contains("scaling_mode") && arguments["scaling_mode"].is_string()) {
        const std::string mode = lower(arguments["scaling_mode"].get<std::string>());
        const Scaling scaling = Stream::stringToScalingMode(mode);
        if (Stream::scalingModeToString(scaling) != mode) {
            return failResult("Unknown scaling_mode: " + mode + ". Use fitwidth, fitheight, letterbox, crop, stretch, or native.");
        }
        project->setScalingMode(scaling);
    }
    if (arguments.contains("texture_strategy") && arguments["texture_strategy"].is_string()) {
        const std::string strategy = lower(arguments["texture_strategy"].get<std::string>());
        const TextureStrategy textureStrategy = Stream::stringToTextureStrategy(strategy);
        if (Stream::textureStrategyToString(textureStrategy) != strategy) {
            return failResult("Unknown texture_strategy: " + strategy + ". Use none, fit, or resize.");
        }
        project->setTextureStrategy(textureStrategy);
    }
    if (arguments.contains("canvas_width") || arguments.contains("canvas_height")) {
        project->setCanvasSize(arguments.value("canvas_width", project->getCanvasWidth()),
                               arguments.value("canvas_height", project->getCanvasHeight()));
    }
    if (arguments.contains("window_width") || arguments.contains("window_height")) {
        project->setWindowSize(arguments.value("window_width", project->getWindowWidth()),
                               arguments.value("window_height", project->getWindowHeight()));
    }
    if (arguments.contains("vsync")) project->setVSyncEnabled(arguments["vsync"].get<bool>());

    LoadingSettings& loading = project->getLoadingSettings();
    if (arguments.contains("loading_scene_id")) loading.sceneId = arguments["loading_scene_id"].get<uint32_t>();
    if (arguments.contains("loading_delay")) loading.delay = arguments["loading_delay"].get<float>();
    if (arguments.contains("loading_timeout")) loading.timeout = arguments["loading_timeout"].get<float>();
    if (arguments.contains("async_loading")) loading.asyncLoading = arguments["async_loading"].get<bool>();

    WebProjectSettings& web = project->getWebProjectSettings();
    if (arguments.contains("web_resize_canvas")) web.resizeCanvasToWindow = arguments["web_resize_canvas"].get<bool>();
    if (arguments.contains("web_hide_emscripten_ui")) web.hideEmscriptenUI = arguments["web_hide_emscripten_ui"].get<bool>();

    if (!project->saveProjectFile()) {
        return failResult("Failed to save the project settings.");
    }

    return okResult("Changed project settings.", Json{
        {"canvas", {{"width", project->getCanvasWidth()}, {"height", project->getCanvasHeight()}}},
        {"scaling_mode", Stream::scalingModeToString(project->getScalingMode())},
        {"texture_strategy", Stream::textureStrategyToString(project->getTextureStrategy())},
        {"window", {{"width", project->getWindowWidth()}, {"height", project->getWindowHeight()}}},
        {"vsync", project->isVSyncEnabled()},
        {"loading", loadingSettingsJson(project->getLoadingSettings())},
        {"web", {{"resize_canvas", web.resizeCanvasToWindow}, {"hide_emscripten_ui", web.hideEmscriptenUI}}}});
}

ActionResult EditorActionExecutor::createProject(const Json& arguments) {
    std::string error;
    fs::path path;
    if (!canSwitchProject(project, error) || !prepareNewProjectDirectory(project, arguments, path, error)) {
        return failResult(error);
    }
    std::string name = optionalString(arguments, "name");
    if (name.empty()) {
        name = FileUtils::pathToUtf8(path.filename());
    }

    Project* target = project;
    const bool queued = getEditorHost().requestProjectChange([target, path, name]() {
        // A unique temp name keeps the current temp project's files
        if (target->createTempProject("DoriaxProject" + std::to_string(std::random_device{}()), true)) {
            target->setName(name);
            target->saveProjectToPath(path);
        }
    });
    if (!queued) {
        return failResult(kProjectChangeBusy);
    }
    return okResult("Creating the project, which opens after this call. Save its scene with save_scene and path.",
                    Json{{"path", FileUtils::pathToUtf8(path)}, {"name", name}});
}

ActionResult EditorActionExecutor::openProject(const Json& arguments) {
    std::string error;
    if (!canSwitchProject(project, error)) return failResult(error);
    const fs::path path = FileUtils::pathFromUtf8(arguments.value("path", "")).lexically_normal();
    if (!path.is_absolute() || !fs::exists(path / "project.yaml")) {
        return failResult("path must be an absolute project directory holding project.yaml.");
    }

    Project* target = project;
    if (!getEditorHost().requestProjectChange([target, path]() { target->loadProject(path); })) {
        return failResult(kProjectChangeBusy);
    }
    return okResult("Opening the project, which loads after this call.", Json{{"path", FileUtils::pathToUtf8(path)}});
}

ActionResult EditorActionExecutor::copyResource(const Json& arguments) {
    std::string error;
    fs::path sourceRel;
    fs::path targetRel;
    if (!safeRelativePath(project, arguments, "source_path", sourceRel, error, true)) return failResult(error);
    if (!safeTargetDir(project, arguments, targetRel, error)) return failResult(error);

    const fs::path sourceFull = project->getProjectPath() / sourceRel;
    const fs::path targetFull = project->getProjectPath() / targetRel;
    if (!fs::exists(sourceFull)) return failResult("source_path does not exist.");
    std::error_code ec;
    fs::create_directories(targetFull, ec);
    if (ec) return failResult("Failed to create target_dir: " + ec.message());

    project->getProjectCommandHistory()->addCommandNoMerge(
        new CopyFileCmd(project, std::vector<std::string>{sourceFull.string()}, targetFull.string(), true));
    if (resourcesWindow) resourcesWindow->refreshCurrentDirectory();
    return okResult("Copied resource through the project command history.",
                    Json{{"source_path", FileUtils::pathToGenericUtf8(sourceRel)},
                         {"target_dir", FileUtils::pathToGenericUtf8(targetRel)}});
}

ActionResult EditorActionExecutor::updateMaterialFile(const Json& arguments) {
    std::string error;
    fs::path rel;
    if (!safeRelativePath(project, arguments, "path", rel, error, true)) return failResult(error);
    if (!Util::isMaterialFile(rel.string())) {
        return failResult("path must point to a .material file.");
    }

    const std::string content = arguments.value("content", "");
    constexpr size_t kMaxBytes = 512 * 1024;
    if (content.size() > kMaxBytes) {
        return failResult("Material content is too large for an AI edit.");
    }

    const fs::path fullPath = project->getProjectPath() / rel;
    std::string normalizedContent;
    std::string materialError;
    if (!normalizeMaterialPayload(rel, content, normalizedContent, materialError)) {
        return failResult(materialError);
    }

    project->getProjectCommandHistory()->addCommandNoMerge(
        new ReplaceMaterialFileCmd(project, resourcesWindow, fullPath, normalizedContent));

    std::string writtenContent;
    if (!readWholeFile(fullPath, writtenContent) || writtenContent != normalizedContent) {
        return failResult("Failed to update material file through the project command history.");
    }

    Out::info("AI updated material file: %s", rel.generic_string().c_str());
    return okResult("Updated material file through the project command history.",
                    Json{{"path", FileUtils::pathToGenericUtf8(rel.lexically_normal())}, {"bytes", normalizedContent.size()}});
}

ActionResult EditorActionExecutor::setComponentProperties(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");
    if (!arguments.contains("properties") || !arguments["properties"].is_array() || arguments["properties"].empty()) {
        return failResult("properties must be a non-empty array.");
    }

    auto* multiCmd = new MultiPropertyCmd();
    int applied = 0;
    for (const Json& item : arguments["properties"]) {
        if (!item.is_object()) return failResult("Each properties entry must be an object.");
        ComponentType component;
        if (!parseComponentType(item.value("component", ""), component)) {
            delete multiCmd;
            return failResult("Unknown component in properties array.");
        }
        const std::string propertyName = item.value("property", "");
        if (propertyName.empty()) {
            delete multiCmd;
            return failResult("Each properties entry requires property.");
        }

        auto props = Catalog::findEntityProperties(sceneProject->scene, entity, component);
        auto it = props.find(propertyName);
        if (it == props.end()) {
            delete multiCmd;
            return failResult("Property not found: " + propertyName);
        }

        std::string validationError;
        if (!validateCustomShaderValue(propertyName, item, validationError)) {
            delete multiCmd;
            return failResult(validationError);
        }

        std::string buildError;
        Command* cmd = buildPropertyCommand(project, sceneId, entity, component, propertyName, it->second, item, buildError);
        if (!cmd) {
            delete multiCmd;
            return failResult(buildError.empty() ? ("Failed to build property command for " + propertyName) : buildError);
        }
        multiCmd->addCommand(std::unique_ptr<Command>(cmd));
        ++applied;
    }

    multiCmd->setNoMerge();
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);
    return okResult("Set component properties through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"applied_count", applied}});
}

ActionResult EditorActionExecutor::inspectScene(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) {
        return expectedMissResult("Scene not found.");
    }

    Json childScenes = Json::array();
    for (uint32_t childId : project->getChildScenes(sceneId)) {
        SceneProject* child = project->getScene(childId);
        childScenes.push_back({
            {"scene_id", childId},
            {"name", child ? child->name : ""},
            {"start_active", project->isChildSceneStartActive(sceneId, childId)}
        });
    }

    Json data = {
        {"scene_id", sceneId},
        {"name", sceneProject->name},
        {"type", sceneTypeName(sceneProject->sceneType)},
        {"opened", sceneProject->opened},
        {"modified", sceneProject->isModified},
        {"filepath", FileUtils::pathToGenericUtf8(sceneProject->filepath)},
        {"main_camera", sceneProject->mainCamera},
        {"entity_count", sceneProject->entities.size()},
        {"selected_entities", project->getSelectedEntities(sceneId)},
        {"child_scenes", childScenes},
        {"properties", sceneReadableProperties(sceneProject)}
    };
    return okResult("Inspected scene.", data);
}

ActionResult EditorActionExecutor::addTilemapTile(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    if (!tilemap) return failResult("Entity has no Tilemap component.");

    const int rectIndex = arguments.value("rect_index", 0);
    if (rectIndex < 0 || static_cast<unsigned int>(rectIndex) >= tilemap->numTilesRect) {
        return failResult("rect_index is out of range.");
    }

    const unsigned int freeSlot = tilemap->numTiles;
    if (!tilemap->tiles.validIndex(static_cast<int>(freeSlot))) {
        return failResult("Tilemap tile array is full.");
    }

    const TileRectData& rectData = tilemap->tilesRect[rectIndex];
    Vector2 position(rectData.rect.getX(), rectData.rect.getY());
    if (arguments.contains("position")) {
        if (!parseVector2(arguments["position"], position)) return failResult("position must be a vector2 object.");
    }
    float tileW = rectData.rect.getWidth();
    float tileH = rectData.rect.getHeight();
    if (arguments.contains("width") && arguments["width"].is_number()) tileW = arguments["width"].get<float>();
    if (arguments.contains("height") && arguments["height"].is_number()) tileH = arguments["height"].get<float>();

    const std::string tilePrefix = "tiles[" + std::to_string(freeSlot) + "]";
    auto* multiCmd = new MultiPropertyCmd();
    multiCmd->addPropertyCmd<std::string>(project, sceneId, entity, ComponentType::TilemapComponent,
                                          tilePrefix + ".name", rectData.name);
    multiCmd->addPropertyCmd<int>(project, sceneId, entity, ComponentType::TilemapComponent,
                                  tilePrefix + ".rectId", rectIndex);
    multiCmd->addPropertyCmd<Vector2>(project, sceneId, entity, ComponentType::TilemapComponent,
                                      tilePrefix + ".position", position);
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, ComponentType::TilemapComponent,
                                    tilePrefix + ".width", tileW);
    multiCmd->addPropertyCmd<float>(project, sceneId, entity, ComponentType::TilemapComponent,
                                    tilePrefix + ".height", tileH);
    multiCmd->addPropertyCmd<unsigned int>(project, sceneId, entity, ComponentType::TilemapComponent,
                                           "numTiles", freeSlot + 1);
    multiCmd->setNoMerge();
    CommandHandle::get(sceneId)->addCommandNoMerge(multiCmd);
    return okResult("Added tilemap tile through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"tile_index", freeSlot}});
}

ActionResult EditorActionExecutor::removeTilemapTile(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    const unsigned int tileIndex = static_cast<unsigned int>(std::max(0, arguments.value("tile_index", 0)));
    Command* cmd = ProjectUtils::buildDeleteTileCmd(project, sceneId, entity, tileIndex);
    if (!cmd) return failResult("Failed to remove tile. Check tile_index and Tilemap component.");
    CommandHandle::get(sceneId)->addCommandNoMerge(cmd);
    return okResult("Removed tilemap tile through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"tile_index", tileIndex}});
}

ActionResult EditorActionExecutor::duplicateTilemapTile(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    const unsigned int tileIndex = static_cast<unsigned int>(std::max(0, arguments.value("tile_index", 0)));
    TilemapComponent* tilemap = sceneProject->scene->findComponent<TilemapComponent>(entity);
    const unsigned int newIndex = tilemap ? tilemap->numTiles : 0;
    Command* cmd = ProjectUtils::buildDuplicateTileCmd(project, sceneId, entity, tileIndex);
    if (!cmd) return failResult("Failed to duplicate tile. Check tile_index and Tilemap capacity.");
    CommandHandle::get(sceneId)->addCommandNoMerge(cmd);
    return okResult("Duplicated tilemap tile through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity},
                         {"source_tile_index", tileIndex}, {"new_tile_index", newIndex}});
}

ActionResult EditorActionExecutor::addAnimationAction(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    AnimationComponent* anim = sceneProject->scene->findComponent<AnimationComponent>(entity);
    if (!anim) return failResult("Entity has no Animation component.");

    ActionFrame frame;
    frame.startTime = arguments.value("start_time", 0.0f);
    frame.duration = arguments.value("duration", 0.0f); // 0 = auto (the action's own duration)
    frame.track = static_cast<uint32_t>(std::max(0, arguments.value("track", 0)));
    if (arguments.contains("action_entity_id")) {
        frame.action = static_cast<Entity>(std::max(0, arguments.value("action_entity_id", 0)));
    } else if (arguments.contains("action_entity_name")) {
        Json tmp = Json::object();
        tmp["entity_name"] = arguments["action_entity_name"];
        frame.action = resolveEntity(sceneProject, tmp);
    }

    ActionSystem* actionSystem = sceneProject->scene->getSystem<ActionSystem>().get();
    if (frame.action != NULL_ENTITY && actionSystem->isAnimationReachable(frame.action, entity)) {
        return failResult("This action would create an animation cycle (the animation would contain itself directly or through nested animations).");
    }

    std::vector<ActionFrame> actions = anim->actions;
    for (const auto& existing : actions) {
        if (existing.track != frame.track) continue;
        const float startA = existing.startTime;
        const float endA = startA + actionSystem->getFrameDuration(existing);
        const float startB = frame.startTime;
        const float endB = startB + actionSystem->getFrameDuration(frame);
        if (std::max(startA, startB) < std::min(endA, endB)) {
            return failResult("New action overlaps an existing action on the same track.");
        }
    }
    actions.push_back(frame);

    CommandHandle::get(sceneId)->addCommandNoMerge(new PropertyCmd<std::vector<ActionFrame>>(
        project, sceneId, entity, ComponentType::AnimationComponent, "actions", actions));
    return okResult("Added animation action through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"action_index", actions.size() - 1}});
}

ActionResult EditorActionExecutor::removeAnimationAction(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    AnimationComponent* anim = sceneProject->scene->findComponent<AnimationComponent>(entity);
    if (!anim) return failResult("Entity has no Animation component.");

    const size_t actionIndex = static_cast<size_t>(std::max(0, arguments.value("action_index", 0)));
    if (actionIndex >= anim->actions.size()) return failResult("action_index is out of range.");

    std::vector<ActionFrame> actions = anim->actions;
    actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(actionIndex));
    CommandHandle::get(sceneId)->addCommandNoMerge(new PropertyCmd<std::vector<ActionFrame>>(
        project, sceneId, entity, ComponentType::AnimationComponent, "actions", actions));
    return okResult("Removed animation action through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"removed_action_index", actionIndex}});
}

ActionResult EditorActionExecutor::setKeyframeTimes(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    KeyframeTracksComponent* keyframes = sceneProject->scene->findComponent<KeyframeTracksComponent>(entity);
    if (!keyframes) return failResult("Entity has no KeyframeTracks component.");

    std::vector<float> times;
    if (arguments.contains("times") && arguments["times"].is_array()) {
        for (const Json& value : arguments["times"]) {
            if (!value.is_number()) return failResult("times must contain numbers.");
            times.push_back(value.get<float>());
        }
    } else if (arguments.value("append", false)) {
        times = keyframes->times;
        times.push_back(arguments.value("time", times.empty() ? 0.0f : times.back()));
    } else {
        return failResult("Provide times array or append=true with time.");
    }

    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
    multiCmd->addPropertyCmd<std::vector<float>>(
        project, sceneId, entity, ComponentType::KeyframeTracksComponent, "times", times);

    // Keep per-segment easings aligned with the new key count (trim extras;
    // missing entries mean linear; all-linear lists collapse to empty)
    size_t newSegments = times.size() > 1 ? times.size() - 1 : 0;
    std::vector<EaseType> newEasings = keyframes->easings;
    if (newEasings.size() > newSegments) {
        newEasings.resize(newSegments);
    }
    if (std::all_of(newEasings.begin(), newEasings.end(), [](EaseType e){ return e == EaseType::LINEAR; })) {
        newEasings.clear();
    }
    if (newEasings != keyframes->easings) {
        multiCmd->addPropertyCmd<std::vector<EaseType>>(
            project, sceneId, entity, ComponentType::KeyframeTracksComponent, "easings", newEasings);
    }

    multiCmd->setNoMerge();
    CommandHandle::get(sceneId)->addCommand(multiCmd);
    return okResult("Set keyframe times through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"count", times.size()}});
}

ActionResult EditorActionExecutor::setKeyframeEasing(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    KeyframeTracksComponent* keyframes = sceneProject->scene->findComponent<KeyframeTracksComponent>(entity);
    if (!keyframes) return failResult("Entity has no KeyframeTracks component.");

    size_t segments = keyframes->times.size() > 1 ? keyframes->times.size() - 1 : 0;
    if (segments == 0) return failResult("Track needs at least two keyframe times before easing can be set.");

    std::vector<EaseType> newEasings = keyframes->easings;
    newEasings.resize(segments, EaseType::LINEAR);

    if (arguments.contains("easings") && arguments["easings"].is_array()) {
        size_t i = 0;
        for (const Json& value : arguments["easings"]) {
            if (i >= segments) break;
            if (!value.is_string()) return failResult("easings must contain ease names (e.g. LINEAR, QUAD_IN_OUT).");
            EaseType parsed;
            if (!parseEaseName(value.get<std::string>(), parsed)) {
                return failResult("Unknown ease name: " + value.get<std::string>() + " (use names like LINEAR, QUAD_IN_OUT, BOUNCE_OUT).");
            }
            newEasings[i++] = parsed;
        }
    } else if (arguments.contains("segment") && arguments.contains("ease")) {
        int segment = arguments.value("segment", -1);
        if (segment < 0 || (size_t)segment >= segments) {
            return failResult("segment out of range: track has " + std::to_string(segments) + " segments (key i to key i+1).");
        }
        if (!arguments["ease"].is_string()) return failResult("ease must be a name (e.g. LINEAR, QUAD_IN_OUT, BOUNCE_OUT).");
        EaseType parsed;
        if (!parseEaseName(arguments["ease"].get<std::string>(), parsed)) {
            return failResult("Unknown ease name: " + arguments["ease"].get<std::string>() + " (use names like LINEAR, QUAD_IN_OUT, BOUNCE_OUT).");
        }
        newEasings[segment] = parsed;
    } else {
        return failResult("Provide easings array or segment + ease.");
    }

    // CUSTOM has no per-segment function storage; treat it as linear
    for (EaseType& e : newEasings) {
        if (e == EaseType::CUSTOM) e = EaseType::LINEAR;
    }

    // all-linear lists collapse to empty (keeps files compact)
    if (std::all_of(newEasings.begin(), newEasings.end(), [](EaseType e){ return e == EaseType::LINEAR; })) {
        newEasings.clear();
    }

    CommandHandle::get(sceneId)->addCommandNoMerge(new PropertyCmd<std::vector<EaseType>>(
        project, sceneId, entity, ComponentType::KeyframeTracksComponent, "easings", newEasings));

    Json easingsJson = Json::array();
    for (EaseType e : newEasings) easingsJson.push_back(Stream::easeTypeToString(e));
    return okResult("Set keyframe easing through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"segments", segments}, {"easings", easingsJson}});
}

ActionResult EditorActionExecutor::setKeyframeTrack(const Json& arguments) {
    uint32_t sceneId = resolveSceneId(project, arguments);
    SceneProject* sceneProject = project->getScene(sceneId);
    if (!sceneProject || !sceneProject->scene) return failResult("Scene not found.");
    Scene* scene = sceneProject->scene;

    Entity entity = resolveEntity(sceneProject, arguments);
    if (entity == NULL_ENTITY) return failResult("Entity not found.");

    KeyframeTracksComponent* keyframes = scene->findComponent<KeyframeTracksComponent>(entity);
    if (!keyframes) return failResult("Entity has no KeyframeTracks component.");

    std::vector<float> times;
    if (arguments.contains("times") && arguments["times"].is_array()) {
        for (const Json& value : arguments["times"]) {
            if (!value.is_number() || (!times.empty() && value.get<float>() < times.back())) {
                return failResult("times must be numbers in ascending order.");
            }
            times.push_back(value.get<float>());
        }
    }
    if (times.empty()) return failResult("times needs at least one key.");

    std::vector<Vector3> positions;
    std::vector<Quaternion> rotations;
    std::vector<Vector3> scales;
    bool euler = valueFieldPresent(arguments, "rotations_euler");
    const char* rotationsKey = euler ? "rotations_euler" : "rotations";
    if (euler && valueFieldPresent(arguments, "rotations")) return failResult("Pass rotations or rotations_euler, not both.");

    if (valueFieldPresent(arguments, "positions") && !parseKeyVectors(arguments["positions"], times.size(), Vector3::ZERO, positions)) {
        return failResult("positions needs one {x, y, z} object per time.");
    }
    if (valueFieldPresent(arguments, rotationsKey) && !parseKeyRotations(arguments[rotationsKey], times.size(), euler, rotations)) {
        return failResult(std::string(rotationsKey) + " needs one rotation object per time.");
    }
    if (valueFieldPresent(arguments, "scales") && !parseKeyVectors(arguments["scales"], times.size(), Vector3::UNIT_SCALE, scales)) {
        return failResult("scales needs one {x, y, z} object per time.");
    }
    if (positions.empty() && rotations.empty() && scales.empty()) {
        return failResult("Pass positions, rotations, rotations_euler or scales.");
    }

    TranslateTracksComponent* translate = scene->findComponent<TranslateTracksComponent>(entity);
    RotateTracksComponent* rotate = scene->findComponent<RotateTracksComponent>(entity);
    ScaleTracksComponent* scale = scene->findComponent<ScaleTracksComponent>(entity);
    if ((!positions.empty() && !translate) || (!rotations.empty() && !rotate) || (!scales.empty() && !scale)) {
        return failResult("Entity has no track component for some of the values.");
    }
    // a track left as it is freezes with a different number of keys
    if ((translate && positions.empty() && translate->values.size() != times.size()) ||
        (rotate && rotations.empty() && rotate->values.size() != times.size()) ||
        (scale && scales.empty() && scale->values.size() != times.size())) {
        return failResult("Every track of the entity needs one value per time.");
    }

    size_t segments = times.size() - 1;
    std::vector<EaseType> easings = keyframes->easings;
    if (valueFieldPresent(arguments, "easings")) {
        if (!arguments["easings"].is_array() || arguments["easings"].size() > segments) {
            return failResult("easings takes at most one ease name per segment (key i to key i+1).");
        }
        easings.clear();
        for (const Json& value : arguments["easings"]) {
            EaseType ease;
            if (!value.is_string() || !parseEaseName(value.get<std::string>(), ease)) {
                return failResult("Unknown ease name in easings (use names like LINEAR, SINE_IN_OUT, BOUNCE_OUT).");
            }
            easings.push_back(ease == EaseType::CUSTOM ? EaseType::LINEAR : ease);
        }
    }
    if (easings.size() > segments) easings.resize(segments);
    if (std::all_of(easings.begin(), easings.end(), [](EaseType e){ return e == EaseType::LINEAR; })) {
        easings.clear();
    }

    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
    multiCmd->addPropertyCmd<std::vector<float>>(
        project, sceneId, entity, ComponentType::KeyframeTracksComponent, "times", times);
    if (easings != keyframes->easings) {
        multiCmd->addPropertyCmd<std::vector<EaseType>>(
            project, sceneId, entity, ComponentType::KeyframeTracksComponent, "easings", easings);
    }

    // the Hermite tangents of GLTF cubic clips belong to the old keys
    auto setValues = [&](ComponentType type, const auto& values) {
        using Values = std::decay_t<decltype(values)>;
        multiCmd->addPropertyCmd<Values>(project, sceneId, entity, type, "values", values);
        multiCmd->addPropertyCmd<Values>(project, sceneId, entity, type, "inTangents", Values());
        multiCmd->addPropertyCmd<Values>(project, sceneId, entity, type, "outTangents", Values());
    };
    if (!positions.empty()) setValues(ComponentType::TranslateTracksComponent, positions);
    if (!rotations.empty()) setValues(ComponentType::RotateTracksComponent, rotations);
    if (!scales.empty()) setValues(ComponentType::ScaleTracksComponent, scales);

    multiCmd->setNoMerge();
    CommandHandle::get(sceneId)->addCommand(multiCmd);
    return okResult("Set keyframe track through the command history.",
                    Json{{"scene_id", sceneId}, {"entity_id", entity}, {"count", times.size()}});
}

ActionResult EditorActionExecutor::undoEditor(const Json& arguments) {
    const std::string scope = lower(arguments.value("scope", "scene"));
    if (scope == "project") {
        CommandHistory* history = project->getProjectCommandHistory();
        if (!history || !history->canUndo()) return failResult("Nothing to undo in project history.");
        if (!history->undo()) return failResult(kRefusedUndo);
        return okResult("Undid last project-level command.");
    }

    uint32_t sceneId = resolveSceneId(project, arguments);
    CommandHistory* history = CommandHandle::get(sceneId);
    if (!history || !history->canUndo()) return failResult("Nothing to undo in scene history.");
    if (!history->undo()) return failResult(kRefusedUndo);
    return okResult("Undid last scene command.", Json{{"scene_id", sceneId}});
}

ActionResult EditorActionExecutor::redoEditor(const Json& arguments) {
    const std::string scope = lower(arguments.value("scope", "scene"));
    if (scope == "project") {
        CommandHistory* history = project->getProjectCommandHistory();
        if (!history || !history->canRedo()) return failResult("Nothing to redo in project history.");
        if (!history->redo()) return failResult(kRefusedRedo);
        return okResult("Redid last project-level command.");
    }

    uint32_t sceneId = resolveSceneId(project, arguments);
    CommandHistory* history = CommandHandle::get(sceneId);
    if (!history || !history->canRedo()) return failResult("Nothing to redo in scene history.");
    if (!history->redo()) return failResult(kRefusedRedo);
    return okResult("Redid last scene command.", Json{{"scene_id", sceneId}});
}

} // namespace doriax::editor::ai
