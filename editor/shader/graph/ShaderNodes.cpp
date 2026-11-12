// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderNodes.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace doriax;

namespace {

    editor::NodePin in(const char* name, const char* label, const char* type, editor::GraphValue value = editor::GraphValue()) {
        editor::NodePin pin;
        pin.name = name;
        pin.label = label;
        pin.type = type;
        pin.value = value;
        return pin;
    }

    editor::NodePin out(const char* name, const char* label, const char* type) {
        editor::NodePin pin;
        pin.name = name;
        pin.label = label;
        pin.type = type;
        return pin;
    }

    editor::NodeSetting choice(const char* name, const char* label, std::vector<std::string> options) {
        editor::NodeSetting setting;
        setting.name = name;
        setting.label = label;
        setting.type = "choice";
        setting.value = options.front();
        setting.options = std::move(options);
        return setting;
    }

    editor::NodeSetting setting(const char* name, const char* label, const char* type, editor::GraphValue value = editor::GraphValue()) {
        editor::NodeSetting item;
        item.name = name;
        item.label = label;
        item.type = type;
        item.value = value;
        return item;
    }

    // a setting that names a parameter of one of these types
    editor::NodeSetting parameterSetting(const char* name, const char* label, std::vector<std::string> types) {
        editor::NodeSetting item = setting(name, label, "parameter");
        item.options = std::move(types);
        return item;
    }

    editor::ShaderOutputSlot slot(const char* hook, const char* target, bool vertex = false, bool depthPasses = false) {
        editor::ShaderOutputSlot item;
        item.hook = hook;
        item.target = target;
        item.vertex = vertex;
        item.depthPasses = depthPasses;
        return item;
    }

    editor::ShaderNodes::FunctionPins functionPins;

}

const editor::ShaderNodes& editor::ShaderNodes::get() {
    static ShaderNodes instance;
    return instance;
}

void editor::ShaderNodes::setFunctionPins(FunctionPins lookup) {
    functionPins = std::move(lookup);
}

const editor::NodeType* editor::ShaderNodes::findType(const std::string& id) const {
    return find(id);
}

std::vector<const editor::NodeType*> editor::ShaderNodes::getTypes() const {
    std::vector<const NodeType*> list;
    for (const auto& type : types)
        list.push_back(type.get());
    return list;
}

const editor::ShaderNodeType* editor::ShaderNodes::find(const std::string& id) const {
    auto it = byId.find(id);
    return it != byId.end() ? it->second : nullptr;
}

const editor::ShaderFunction* editor::ShaderNodes::findFunction(const std::string& name) const {
    auto it = functions.find(name);
    return it != functions.end() ? &it->second : nullptr;
}

std::string editor::ShaderNodes::getPinType(const GraphDocument& graph, const GraphNode& node, const NodePin& pin) {
    if (pin.type == "parameter") {
        auto setting = node.settings.find("parameter");
        if (setting != node.settings.end() && setting->second.text.empty())
            if (const GraphParameter* parameter = graph.findParameter((int)setting->second.number()))
                return parameter->type;
        auto valueType = node.settings.find("valueType");
        return valueType != node.settings.end() ? valueType->second.text : "float";
    }
    if (pin.type == "mask") {
        auto mask = node.settings.find("mask");
        static const char* types[] = {"float", "float", "vec2", "vec3", "vec4"};
        return types[std::min<size_t>(mask != node.settings.end() ? mask->second.text.size() : 3, 4)];
    }
    return pin.type;
}

bool editor::ShaderNodes::canLink(const std::string& fromType, const std::string& toType) {
    const bool fromTexture = fromType == "texture2D" || fromType == "textureCube";
    const bool toTexture = toType == "texture2D" || toType == "textureCube";
    if (fromTexture || toTexture || fromType == "bool" || toType == "bool")
        return fromType == toType;
    // a vector feeds no scalar, and a vec2 no longer vector; dynamic pins settle on what they get
    auto size = [](const std::string& type) {
        return type == "vec2" ? 2 : (type == "vec3" ? 3 : (type == "vec4" || type == "color" ? 4 : (type == "dynamic" || type == "parameter" || type == "mask" ? 0 : 1)));
    };
    const int from = size(fromType), to = size(toType);
    if (from == 0 || to == 0)
        return true;
    return from == 1 || from == to || (from > to && to > 1) || (from == 3 && to == 4);
}

bool editor::ShaderNodes::getPins(const GraphNode& node, const ShaderNodeType& type, std::vector<NodePin>& inputs, std::vector<NodePin>& outputs, std::string* error) {
    inputs = type.inputs;
    outputs = type.outputs;
    if (type.id == "function/input" || type.id == "function/output") {
        auto valueType = node.settings.find("valueType");
        const std::string pinType = valueType != node.settings.end() ? valueType->second.text : type.findSetting("valueType")->value.text;
        for (NodePin& pin : inputs)
            pin.type = pinType;
        for (NodePin& pin : outputs)
            pin.type = pinType;
        return true;
    }
    if (type.id == "utility/subgraph") {
        auto file = node.settings.find("file");
        if (file == node.settings.end() || file->second.text.empty() || !functionPins)
            return true;
        if (!functionPins(file->second.text, inputs, outputs)) {
            if (error)
                *error = file->second.text + " is not a function graph of the project";
            return false;
        }
        return true;
    }
    if (type.id != "utility/customFunction")
        return true;

    // "vec3 shade(vec3 color, float amount)"
    auto setting = node.settings.find("signature");
    std::string signature = setting != node.settings.end() ? setting->second.text : type.settings.front().value.text;
    const size_t open = signature.find('(');
    if (open == std::string::npos || signature.find(')', open) == std::string::npos) {
        if (error)
            *error = "a signature reads like vec3 shade(vec3 color, float amount)";
        return false;
    }
    std::replace(signature.begin(), signature.end(), ',', ' ');
    std::replace(signature.begin(), signature.end(), '(', ' ');
    std::replace(signature.begin(), signature.end(), ')', ' ');
    std::istringstream words(signature);
    std::vector<std::string> parts;
    for (std::string word; words >> word;)
        parts.push_back(word);

    static const std::vector<std::string> types = {"float", "vec2", "vec3", "vec4", "int", "bool"};
    auto isType = [](const std::string& word) { return std::find(types.begin(), types.end(), word) != types.end(); };
    auto isName = [](const std::string& word) {
        return !word.empty() && (std::isalpha((unsigned char)word[0]) || word[0] == '_') &&
            std::all_of(word.begin(), word.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '_'; });
    };
    if (parts.size() < 2 || parts.size() % 2 != 0 || !isType(parts[0]) || !isName(parts[1])) {
        if (error)
            *error = "a signature reads like vec3 shade(vec3 color, float amount)";
        return false;
    }
    inputs.clear();
    outputs = {NodePin{"out", "Out", parts[0], GraphValue(), PinKind::DATA}};
    for (size_t i = 2; i + 1 < parts.size(); i += 2) {
        if (!isType(parts[i]) || !isName(parts[i + 1])) {
            if (error)
                *error = "parameter " + parts[i] + " " + parts[i + 1] + " is not a float, vector, int or bool with a name";
            return false;
        }
        inputs.push_back(NodePin{parts[i + 1], parts[i + 1], parts[i], GraphValue(0.0f), PinKind::DATA});
    }
    return true;
}

editor::ShaderNodeType& editor::ShaderNodes::add(const std::string& id, const std::string& name, const std::string& category, const std::string& description,
                                                 std::vector<NodePin> inputs, std::vector<NodePin> outputs, std::vector<std::string> code) {
    auto type = std::make_unique<ShaderNodeType>();
    type->id = id;
    type->name = name;
    type->category = category;
    type->description = description;
    type->inputs = std::move(inputs);
    type->outputs = std::move(outputs);
    type->code = std::move(code);
    ShaderNodeType& added = *type;
    byId[id] = type.get();
    types.push_back(std::move(type));
    return added;
}

editor::ShaderNodes::ShaderNodes() {
    // ---- helpers

    functions["hash"] = {{}, R"(uint graphHashU(uvec2 v){
    uint h = (v.x * 1597334677u) ^ (v.y * 3812015801u);
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return h;
}

// 0 to 1, the same for the same lattice point
float graphCellHash(vec2 cell){
    return float(graphHashU(uvec2(ivec2(cell)))) * (1.0 / 4294967295.0);
}

float graphHash(vec2 p){
    return float(graphHashU(floatBitsToUint(p))) * (1.0 / 4294967295.0);
}
)"};
    functions["valueNoise"] = {{"hash"}, R"(float graphValueNoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(graphCellHash(i), graphCellHash(i + vec2(1.0, 0.0)), u.x),
               mix(graphCellHash(i + vec2(0.0, 1.0)), graphCellHash(i + vec2(1.0, 1.0)), u.x), u.y);
}
)"};
    functions["gradientNoise"] = {{"hash"}, R"(vec2 graphCellGradient(vec2 cell){
    uint h = graphHashU(uvec2(ivec2(cell)));
    float angle = float(h) * (6.28318530718 / 4294967295.0);
    return vec2(cos(angle), sin(angle));
}

float graphGradientNoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(graphCellGradient(i), f);
    float b = dot(graphCellGradient(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(graphCellGradient(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(graphCellGradient(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.7071 + 0.5;
}
)"};
    functions["voronoi"] = {{"hash"}, R"(// distance to the nearest cell point, and a value of that cell
vec2 graphVoronoi(vec2 p, float jitter){
    vec2 i = floor(p);
    vec2 f = fract(p);
    float best = 8.0;
    float cell = 0.0;
    for (int y = -1; y <= 1; y++){
        for (int x = -1; x <= 1; x++){
            vec2 offset = vec2(float(x), float(y));
            uint h = graphHashU(uvec2(ivec2(i + offset)));
            vec2 point = offset + 0.5 + (vec2(float(h & 65535u), float(h >> 16)) * (1.0 / 65535.0) - 0.5) * jitter;
            float distance = length(point - f);
            if (distance < best){
                best = distance;
                cell = float(h) * (1.0 / 4294967295.0);
            }
        }
    }
    return vec2(best, cell);
}
)"};
    functions["hsv"] = {{}, R"(vec3 graphRGBToHSV(vec3 c){
    vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, k.wz), vec4(c.gb, k.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1.0e-10)), d / (q.x + 1.0e-10), q.x);
}

vec3 graphHSVToRGB(vec3 c){
    vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}
)"};
    functions["rotateAxis"] = {{}, R"(vec3 graphRotateAboutAxis(vec3 v, vec3 axis, float angle){
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}
)"};
    functions["rotateUV"] = {{}, R"(vec2 graphRotateUV(vec2 uv, vec2 center, float angle){
    float s = sin(angle);
    float c = cos(angle);
    uv -= center;
    return vec2(uv.x * c - uv.y * s, uv.x * s + uv.y * c) + center;
}
)"};
    functions["polar"] = {{}, R"(vec2 graphPolar(vec2 uv, vec2 center){
    vec2 d = uv - center;
    return vec2(length(d), atan(d.y, d.x) * (1.0 / 6.28318530718) + 0.5);
}
)"};
    functions["twirl"] = {{}, R"(vec2 graphTwirl(vec2 uv, vec2 center, float strength){
    vec2 d = uv - center;
    float angle = strength * length(d);
    float s = sin(angle);
    float c = cos(angle);
    return vec2(d.x * c - d.y * s, d.x * s + d.y * c) + center;
}
)"};
    functions["flipbook"] = {{}, R"(vec2 graphFlipbook(vec2 uv, float columns, float rows, float frame){
    float tile = mod(floor(frame), columns * rows);
    vec2 cell = vec2(mod(tile, columns), floor(tile / columns));
    return (uv + cell) / vec2(columns, rows);
}
)"};

    // ---- outputs: what each input fills in the shader file

    ShaderNodeType& mesh = add("output/mesh", "Mesh Output", "Output", "What the mesh's material draws with.",
        {in("vertexOffset", "Vertex Offset", "vec3"), in("baseColor", "Base Color", "vec3"), in("alpha", "Alpha", "float"),
         in("normal", "Normal", "vec3"), in("metallic", "Metallic", "float"), in("roughness", "Roughness", "float"),
         in("occlusion", "Occlusion", "float"), in("emissive", "Emissive", "vec3"), in("light", "Light", "vec3"),
         in("coverage", "Coverage", "float"), in("depthOffset", "Depth Offset", "float"), in("color", "Final Color", "color")},
        {}, {});
    mesh.shaderTypes = {"mesh"};
    mesh.slots["vertexOffset"] = slot("getVertexOffset", "", true, true);
    mesh.slots["baseColor"] = slot("getSurface", "surface.baseColor");
    mesh.slots["alpha"] = slot("getSurface", "surface.alpha", false, true);
    mesh.slots["normal"] = slot("getSurface", "surface.normal");
    mesh.slots["metallic"] = slot("getSurface", "surface.metallic");
    mesh.slots["roughness"] = slot("getSurface", "surface.roughness");
    mesh.slots["occlusion"] = slot("getSurface", "surface.occlusion");
    mesh.slots["emissive"] = slot("getSurface", "surface.emissive");
    // what each light adds, in place of the built-in shading
    mesh.slots["light"] = slot("getLight", "");
    mesh.slots["coverage"] = slot("getCoverage", "", false, true);
    mesh.slots["depthOffset"] = slot("getDepthOffset", "", false, true);
    mesh.slots["color"] = slot("getColor", "");

    for (const char* type : {"ui", "points", "lines"}) {
        const std::string label = std::string(type) == "ui" ? "UI" : (std::string(type) == "points" ? "Points" : "Lines");
        ShaderNodeType& primitive = add(std::string("output/") + type, label + " Output", "Output", "What a " + label + " component draws with.",
            {in("vertexOffset", "Vertex Offset", "vec3"), in("color", "Color", "color")}, {}, {});
        primitive.shaderTypes = {type};
        primitive.slots["vertexOffset"] = slot("getVertexOffset", "", true);
        primitive.slots["color"] = slot("getColor", "");
    }

    ShaderNodeType& sky = add("output/sky", "Sky Output", "Output", "The sky's color.", {in("color", "Color", "color")}, {}, {});
    sky.shaderTypes = {"sky"};
    sky.slots["color"] = slot("getColor", "");

    ShaderNodeType& water = add("output/water", "Water Output", "Output", "The water's color, and what a camera below its surface sees.",
        {in("color", "Color", "color"), in("underwaterColor", "Underwater Color", "color")}, {}, {});
    water.shaderTypes = {"water"};
    water.slots["color"] = slot("getColor", "");
    water.slots["underwaterColor"] = slot("getUnderwaterColor", "");

    ShaderNodeType& post = add("output/postprocess", "Post-process Output", "Output", "The color of the pass.", {in("color", "Color", "color")}, {}, {});
    post.shaderTypes = {"postprocess"};
    post.slots["color"] = slot("getColor", "");

    // ---- inputs

    auto input = [&](const std::string& id, const std::string& name, const std::string& description, std::vector<NodePin> outputs,
                     std::vector<std::string> code, std::vector<std::string> shaderTypes = {}) -> ShaderNodeType& {
        ShaderNodeType& node = add(id, name, "Input", description, {}, std::move(outputs), std::move(code));
        node.inlined = true;
        node.shaderTypes = std::move(shaderTypes);
        return node;
    };

    ShaderNodeType& uv = input("input/uv", "UV", "A UV set as the mesh stores it.", {out("uv", "UV", "vec2")}, {"@uv1"}, {"mesh", "ui", "points", "lines"});
    uv.settings = {choice("set", "Set", {"1", "2"})};
    uv.variants["1"] = {"@uv1"};
    uv.variants["2"] = {"@uv2"};

    ShaderNodeType& position = input("input/position", "Position", "The fragment's or vertex's position.", {out("position", "Position", "vec3")},
        {"@worldPosition"}, {"mesh", "ui", "points", "lines", "water"});
    position.settings = {choice("space", "Space", {"world", "local"})};
    position.variants["world"] = {"@worldPosition"};
    position.variants["local"] = {"@localPosition"};

    input("input/normal", "Normal", "The world normal, facing the viewer.", {out("normal", "Normal", "vec3")}, {"@normal"}, {"mesh", "water"});
    input("input/tangent", "Tangent", "The world tangent frame, along the UV set's u and v.",
        {out("tangent", "Tangent", "vec3"), out("bitangent", "Bitangent", "vec3")}, {"@tangent", "@bitangent"}, {"mesh"});
    input("input/vertexColor", "Vertex Color", "The vertex color times the instance color.", {out("color", "Color", "vec4")}, {"@color"}, {"mesh", "ui", "points", "lines"});
    input("input/viewDirection", "View Direction", "Unit vector from the surface to the eye.", {out("view", "View", "vec3")}, {"@view"}, {"mesh", "water"});
    input("input/camera", "Camera", "The camera's position and forward axis, whether it is orthographic, and its clip planes as distances.",
        {out("position", "Position", "vec3"), out("forward", "Forward", "vec3"), out("orthographic", "Orthographic", "float"),
         out("near", "Near", "float"), out("far", "Far", "float")},
        {"@eye", "@forward", "(@perspective ? 0.0 : 1.0)", "@nearPlane", "@farPlane"}, {"mesh"});
    input("input/object", "Object", "Where the object is in the world, and its scale; an instance's own with instancing.",
        {out("position", "Position", "vec3"), out("scale", "Scale", "vec3")}, {"@objectPosition", "@objectScale"}, {"mesh"});
    input("input/screen", "Screen Position", "Where the fragment is in the view, 0 at the top left, and its pixel.",
        {out("uv", "UV", "vec2"), out("pixel", "Pixel", "vec2")}, {"@screenUV", "@pixel"});
    input("input/time", "Time", "Seconds of the scene clock.", {out("time", "Time", "float")}, {"Engine.time"});
    input("input/resolution", "Resolution", "The target's size in pixels, and one over it.",
        {out("size", "Size", "vec2"), out("texel", "Texel", "vec2")}, {"Engine.resolution.xy", "Engine.resolution.zw"});
    input("input/instance", "Instance", "The instance's custom value, set per instance.", {out("value", "Value", "vec4")}, {"@instance"}, {"mesh"});
    input("input/frontFacing", "Front Facing", "Whether the fragment is on a front face.", {out("frontFacing", "Front Facing", "bool")}, {"@frontFacing"}, {"mesh"})
        .stage = ShaderNodeStage::FRAGMENT;
    input("input/material", "Material", "The surface the material gives: before this graph changes it in the surface outputs, as changed in Light.",
        {out("baseColor", "Base Color", "vec3"), out("alpha", "Alpha", "float"), out("normal", "Normal", "vec3"), out("metallic", "Metallic", "float"),
         out("roughness", "Roughness", "float"), out("occlusion", "Occlusion", "float"), out("emissive", "Emissive", "vec3")},
        {"%surface.baseColor", "%surface.alpha", "%surface.normal", "%surface.metallic", "%surface.roughness", "%surface.occlusion", "%surface.emissive"}, {"mesh"})
        .inlined = false;   // read before the outputs write the surface
    input("input/color", "Built-in Color", "The color the engine would draw, linear.", {out("color", "Color", "vec4")}, {"%color"});
    input("input/light", "Light", "The light Light answers for, once per light: the direction toward it, its color times intensity and falloff, and its shadow, 1 lit and 0 shadowed.",
        {out("direction", "Direction", "vec3"), out("color", "Color", "vec3"), out("shadow", "Shadow", "float")},
        {"%light.direction", "%light.color", "%light.shadow"}, {"mesh"});
    input("input/texture", "Built-in Texture", "The texture sample and the tint the engine multiplies, linear.",
        {out("color", "Texture Color", "vec4"), out("tint", "Tint", "vec4")}, {"@textureColor", "@tint"}, {"ui", "points", "lines", "sky"});
    input("input/skyDirection", "Sky Direction", "The cube map direction, in the sky's own space.", {out("direction", "Direction", "vec3")}, {"@direction"}, {"sky"});
    input("input/underwater", "Underwater", "The scene before the fade, and the water the view crosses.",
        {out("sceneColor", "Scene Color", "vec4"), out("pathLength", "Path Length", "float")}, {"@sceneColor", "@pathLength"}, {"water"});

    // ---- scene

    auto scene = [&](const std::string& id, const std::string& name, const std::string& description, NodePin output,
                     const std::string& code, std::vector<std::string> shaderTypes) {
        ShaderNodeType& node = add(id, name, "Scene", description, {in("uv", "UV", "vec2", "@screenUV")}, {output}, {code});
        node.stage = ShaderNodeStage::FRAGMENT;
        node.shaderTypes = std::move(shaderTypes);
    };
    scene("scene/color", "Scene Color", "What is drawn behind, linear; a mesh reading it draws after the opaque scene.",
        out("color", "Color", "vec4"), "sceneColor({uv})", {"mesh", "postprocess"});
    scene("scene/depth", "Scene Depth", "Distance from the eye along the view to what is drawn behind, in world units.",
        out("depth", "Depth", "float"), "sceneDepth({uv})", {"mesh", "postprocess"});
    scene("scene/normal", "Scene Normal", "World normal of what is drawn, needs SSR on.", out("normal", "Normal", "vec3"), "sceneNormal({uv})", {"postprocess"});
    scene("scene/occlusion", "Scene Occlusion", "SSAO, 1 without it.", out("occlusion", "Occlusion", "float"), "sceneOcclusion({uv})", {"postprocess"});

    // ---- parameters and constants

    ShaderNodeType& parameter = add("parameter", "Parameter", "Parameter", "A value of the graph, shown in Properties.", {}, {out("out", "Out", "parameter")}, {"%parameter"});
    parameter.settings = {parameterSetting("parameter", "Parameter", {"float", "vec2", "vec3", "vec4", "int", "color"})};
    parameter.inlined = true;

    for (const char* kind : {"scene", "view"}) {
        const bool isScene = std::string(kind) == "scene";
        ShaderNodeType& shared = add(std::string("parameter/") + kind, isScene ? "Scene Parameter" : "View Parameter", "Parameter",
            isScene ? "A value the whole scene shares, set with set_scene_property shader_parameter:Collection.name."
                    : "A value each camera sets, for a camera's shader_parameter:Collection.name.",
            {}, {out("out", "Out", "parameter")}, {"%shared"});
        shared.settings = {choice("valueType", "Type", {"float", "vec2", "vec3", "vec4"}), setting("collection", "Collection", "text"), setting("name", "Name", "text")};
        shared.inlined = true;
    }

    auto constant = [&](const std::string& id, const std::string& name, const char* type, GraphValue value) {
        ShaderNodeType& node = add(id, name, "Parameter", "A fixed value.", {}, {out("out", "Out", type)}, {"%value"});
        node.settings = {setting("value", "Value", "number", value)};
        node.inlined = true;
    };
    constant("constant/float", "Float", "float", 0.0f);
    constant("constant/vec2", "Vector 2", "vec2", std::vector<float>{0.0f, 0.0f});
    constant("constant/vec3", "Vector 3", "vec3", std::vector<float>{0.0f, 0.0f, 0.0f});
    constant("constant/vec4", "Vector 4", "vec4", std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f});
    constant("constant/color", "Color", "color", std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f});
    constant("constant/int", "Integer", "int", 0.0f);
    ShaderNodeType& boolean = add("constant/bool", "Boolean", "Parameter", "True or false.", {}, {out("out", "Out", "bool")}, {"{value}"});
    boolean.settings = {choice("value", "Value", {"true", "false"})};
    boolean.inlined = true;

    // ---- math, on floats and vectors alike

    auto math = [&](const std::string& id, const std::string& name, const std::string& description, std::vector<NodePin> inputs,
                    const std::string& code, const char* outType = "dynamic") -> ShaderNodeType& {
        return add(id, name, "Math", description, std::move(inputs), {out("out", "Out", outType)}, {code});
    };
    auto unary = [&](const std::string& id, const std::string& name, const std::string& description, const std::string& code) -> ShaderNodeType& {
        return math(id, name, description, {in("in", "In", "dynamic", 0.0f)}, code);
    };
    auto binary = [&](const std::string& id, const std::string& name, const std::string& description, const std::string& code,
                      GraphValue a = 0.0f, GraphValue b = 0.0f) -> ShaderNodeType& {
        return math(id, name, description, {in("a", "A", "dynamic", a), in("b", "B", "dynamic", b)}, code);
    };

    binary("math/add", "Add", "A plus B.", "{a} + {b}");
    binary("math/subtract", "Subtract", "A minus B.", "{a} - {b}");
    binary("math/multiply", "Multiply", "A times B, component by component.", "{a} * {b}", 1.0f, 1.0f);
    binary("math/divide", "Divide", "A over B.", "{a} / {b}", 1.0f, 1.0f);
    binary("math/mod", "Modulo", "The remainder of A over B, with B's sign.", "mod({a}, {b})", 0.0f, 1.0f);
    binary("math/min", "Minimum", "The smaller of A and B.", "min({a}, {b})");
    binary("math/max", "Maximum", "The larger of A and B.", "max({a}, {b})");
    binary("math/power", "Power", "A to the power of B.", "pow({a}, {b})", 1.0f, 1.0f);
    binary("math/step", "Step", "1 where B reaches A, else 0.", "step({a}, {b})");
    binary("math/atan2", "Arctangent 2", "The angle of the point (B, A), in radians.", "atan({a}, {b})");
    unary("math/negate", "Negate", "Minus the input.", "-({in})");
    unary("math/abs", "Absolute", "The input without its sign.", "abs({in})");
    unary("math/sign", "Sign", "-1, 0 or 1.", "sign({in})");
    unary("math/floor", "Floor", "Rounded down.", "floor({in})");
    unary("math/ceil", "Ceiling", "Rounded up.", "ceil({in})");
    unary("math/round", "Round", "Rounded to the nearest whole number.", "floor({in} + 0.5)");
    unary("math/fract", "Fraction", "The part after the point.", "fract({in})");
    unary("math/saturate", "Saturate", "Clamped to 0 to 1.", "clamp({in}, 0.0, 1.0)");
    unary("math/oneMinus", "One Minus", "1 minus the input.", "1.0 - {in}");
    unary("math/reciprocal", "Reciprocal", "1 over the input.", "1.0 / {in}");
    unary("math/sqrt", "Square Root", "The square root.", "sqrt({in})");
    unary("math/exp", "Exponential", "e to the input.", "exp({in})");
    unary("math/log", "Logarithm", "The natural logarithm.", "log({in})");
    unary("math/sin", "Sine", "In radians.", "sin({in})");
    unary("math/cos", "Cosine", "In radians.", "cos({in})");
    unary("math/tan", "Tangent", "In radians.", "tan({in})");
    unary("math/asin", "Arcsine", "In radians.", "asin({in})");
    unary("math/acos", "Arccosine", "In radians.", "acos({in})");
    unary("math/atan", "Arctangent", "In radians.", "atan({in})");
    unary("math/ddx", "DDX", "How fast the input changes across the screen, horizontally.", "dFdx({in})").stage = ShaderNodeStage::FRAGMENT;
    unary("math/ddy", "DDY", "How fast the input changes across the screen, vertically.", "dFdy({in})").stage = ShaderNodeStage::FRAGMENT;
    unary("math/fwidth", "Fwidth", "The sum of DDX and DDY, without their signs.", "fwidth({in})").stage = ShaderNodeStage::FRAGMENT;
    math("math/clamp", "Clamp", "The input kept between Min and Max.",
        {in("in", "In", "dynamic", 0.0f), in("min", "Min", "dynamic", 0.0f), in("max", "Max", "dynamic", 1.0f)}, "clamp({in}, {min}, {max})");
    math("math/lerp", "Lerp", "A when T is 0, B when T is 1.",
        {in("a", "A", "dynamic", 0.0f), in("b", "B", "dynamic", 1.0f), in("t", "T", "dynamic", 0.5f)}, "mix({a}, {b}, {t})");
    math("math/smoothstep", "Smoothstep", "0 below Edge 1, 1 above Edge 2, a smooth curve between.",
        {in("edge1", "Edge 1", "dynamic", 0.0f), in("edge2", "Edge 2", "dynamic", 1.0f), in("in", "In", "dynamic", 0.0f)}, "smoothstep({edge1}, {edge2}, {in})");
    math("math/remap", "Remap", "The input moved from one range to another.",
        {in("in", "In", "dynamic", 0.0f), in("fromMin", "From Min", "dynamic", 0.0f), in("fromMax", "From Max", "dynamic", 1.0f),
         in("toMin", "To Min", "dynamic", 0.0f), in("toMax", "To Max", "dynamic", 1.0f)},
        "{toMin} + ({in} - {fromMin}) * ({toMax} - {toMin}) / ({fromMax} - {fromMin})");

    // ---- vectors

    auto vector = [&](const std::string& id, const std::string& name, const std::string& description, std::vector<NodePin> inputs,
                      std::vector<NodePin> outputs, std::vector<std::string> code) -> ShaderNodeType& {
        return add(id, name, "Vector", description, std::move(inputs), std::move(outputs), std::move(code));
    };
    vector("vector/dot", "Dot Product", "The sum of A times B, component by component.",
        {in("a", "A", "dynamic", 0.0f), in("b", "B", "dynamic", 0.0f)}, {out("out", "Out", "float")}, {"dot({a}, {b})"});
    vector("vector/cross", "Cross Product", "A vector square to A and B.",
        {in("a", "A", "vec3", std::vector<float>{1.0f, 0.0f, 0.0f}), in("b", "B", "vec3", std::vector<float>{0.0f, 1.0f, 0.0f})},
        {out("out", "Out", "vec3")}, {"cross({a}, {b})"});
    vector("vector/length", "Length", "How long the vector is.", {in("in", "In", "dynamic", 0.0f)}, {out("out", "Out", "float")}, {"length({in})"});
    vector("vector/distance", "Distance", "How far A is from B.",
        {in("a", "A", "dynamic", 0.0f), in("b", "B", "dynamic", 0.0f)}, {out("out", "Out", "float")}, {"distance({a}, {b})"});
    vector("vector/normalize", "Normalize", "The vector at length 1.", {in("in", "In", "dynamic", 0.0f)}, {out("out", "Out", "dynamic")}, {"normalize({in})"});
    vector("vector/reflect", "Reflect", "The input bounced off a surface with that normal.",
        {in("in", "In", "vec3"), in("normal", "Normal", "vec3", "@normal")}, {out("out", "Out", "vec3")}, {"reflect({in}, {normal})"});
    vector("vector/refract", "Refract", "The input bent through a surface, with the ratio of the indices of refraction.",
        {in("in", "In", "vec3"), in("normal", "Normal", "vec3", "@normal"), in("ratio", "Ratio", "float", 0.75f)},
        {out("out", "Out", "vec3")}, {"refract({in}, {normal}, {ratio})"});
    vector("vector/fresnel", "Fresnel", "Brighter where the surface turns away from the eye.",
        {in("normal", "Normal", "vec3", "@normal"), in("view", "View", "vec3", "@view"), in("power", "Power", "float", 5.0f)},
        {out("out", "Out", "float")}, {"pow(1.0 - clamp(dot({normal}, {view}), 0.0, 1.0), {power})"});
    ShaderNodeType& rotate = vector("vector/rotateAboutAxis", "Rotate About Axis", "The vector turned around an axis, angle in radians.",
        {in("in", "In", "vec3"), in("axis", "Axis", "vec3", std::vector<float>{0.0f, 1.0f, 0.0f}), in("angle", "Angle", "float", 0.0f)},
        {out("out", "Out", "vec3")}, {"graphRotateAboutAxis({in}, {axis}, {angle})"});
    rotate.functions = {"rotateAxis"};
    ShaderNodeType& split = vector("vector/split", "Split", "The components of a vector.",
        {in("in", "In", "vec4")}, {out("x", "X", "float"), out("y", "Y", "float"), out("z", "Z", "float"), out("w", "W", "float")},
        {"{in}.x", "{in}.y", "{in}.z", "{in}.w"});
    split.inlined = true;
    vector("vector/combine", "Combine", "A vector made of components.",
        {in("x", "X", "float", 0.0f), in("y", "Y", "float", 0.0f), in("z", "Z", "float", 0.0f), in("w", "W", "float", 1.0f)},
        {out("out", "Out", "vec4"), out("xyz", "XYZ", "vec3"), out("xy", "XY", "vec2")},
        {"vec4({x}, {y}, {z}, {w})", "vec3({x}, {y}, {z})", "vec2({x}, {y})"});

    // ---- color

    auto color = [&](const std::string& id, const std::string& name, const std::string& description, std::vector<NodePin> inputs,
                     const char* outType, const std::string& code) -> ShaderNodeType& {
        return add(id, name, "Color", description, std::move(inputs), {out("out", "Out", outType)}, {code});
    };
    color("color/srgbToLinear", "sRGB to Linear", "From the gamma of an image to the linear values lighting uses.",
        {in("in", "In", "vec4", std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f})}, "vec4", "sRGBToLinear({in})").include = "includes/srgb.glsl";
    color("color/linearToSrgb", "Linear to sRGB", "From linear values to the gamma of an image.",
        {in("in", "In", "vec4", std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f})}, "vec4", "linearTosRGB({in})").include = "includes/srgb.glsl";
    color("color/luminance", "Luminance", "How bright the color looks.", {in("in", "In", "vec3")}, "float", "dot({in}, vec3(0.2126, 0.7152, 0.0722))");
    color("color/rgbToHsv", "RGB to HSV", "Hue, saturation and value, each 0 to 1.", {in("in", "In", "vec3")}, "vec3", "graphRGBToHSV({in})").functions = {"hsv"};
    color("color/hsvToRgb", "HSV to RGB", "From hue, saturation and value.", {in("in", "In", "vec3")}, "vec3", "graphHSVToRGB({in})").functions = {"hsv"};
    color("color/hueShift", "Hue Shift", "The hue turned by Shift, 1 is a full turn.",
        {in("in", "In", "vec3"), in("shift", "Shift", "float", 0.0f)}, "vec3",
        "graphHSVToRGB(graphRGBToHSV({in}) + vec3({shift}, 0.0, 0.0))").functions = {"hsv"};
    color("color/saturation", "Saturation", "0 is gray, 1 keeps the color, more is stronger.",
        {in("in", "In", "vec3"), in("amount", "Amount", "float", 1.0f)}, "vec3",
        "mix(vec3(dot({in}, vec3(0.2126, 0.7152, 0.0722))), {in}, {amount})");
    color("color/contrast", "Contrast", "Spread away from mid gray, 1 keeps the color.",
        {in("in", "In", "vec3"), in("amount", "Amount", "float", 1.0f)}, "vec3", "({in} - 0.5) * {amount} + 0.5");
    color("color/posterize", "Posterize", "Cut into that many steps.",
        {in("in", "In", "dynamic", 0.0f), in("steps", "Steps", "dynamic", 4.0f)}, "dynamic", "floor({in} * {steps}) / {steps}");
    ShaderNodeType& blend = color("color/blend", "Blend", "B over A, by Opacity, as a layer mode.",
        {in("a", "Base", "vec3"), in("b", "Blend", "vec3"), in("opacity", "Opacity", "float", 1.0f)}, "vec3", "mix({a}, {a} * {b}, {opacity})");
    blend.settings = {choice("mode", "Mode", {"multiply", "screen", "overlay", "add", "subtract", "lighten", "darken"})};
    blend.variants["multiply"] = {"mix({a}, {a} * {b}, {opacity})"};
    blend.variants["screen"] = {"mix({a}, 1.0 - (1.0 - {a}) * (1.0 - {b}), {opacity})"};
    blend.variants["overlay"] = {"mix({a}, mix(2.0 * {a} * {b}, 1.0 - 2.0 * (1.0 - {a}) * (1.0 - {b}), step(0.5, {a})), {opacity})"};
    blend.variants["add"] = {"mix({a}, {a} + {b}, {opacity})"};
    blend.variants["subtract"] = {"mix({a}, {a} - {b}, {opacity})"};
    blend.variants["lighten"] = {"mix({a}, max({a}, {b}), {opacity})"};
    blend.variants["darken"] = {"mix({a}, min({a}, {b}), {opacity})"};
    color("color/gradient", "Gradient", "Color 1 at 0 to Color 2 at 1.",
        {in("t", "T", "float", 0.0f), in("color1", "Color 1", "color", std::vector<float>{0.0f, 0.0f, 0.0f, 1.0f}),
         in("color2", "Color 2", "color", std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f})}, "vec4", "mix({color1}, {color2}, clamp({t}, 0.0, 1.0))");

    // ---- UV

    auto uvNode = [&](const std::string& id, const std::string& name, const std::string& description, std::vector<NodePin> inputs,
                      const std::string& code) -> ShaderNodeType& {
        inputs.insert(inputs.begin(), in("uv", "UV", "vec2", "@uv1"));
        return add(id, name, "UV", description, std::move(inputs), {out("out", "Out", "vec2")}, {code});
    };
    uvNode("uv/tilingOffset", "Tiling and Offset", "UV times Tiling, plus Offset.",
        {in("tiling", "Tiling", "vec2", std::vector<float>{1.0f, 1.0f}), in("offset", "Offset", "vec2", std::vector<float>{0.0f, 0.0f})},
        "{uv} * {tiling} + {offset}");
    uvNode("uv/rotate", "Rotate UV", "UV turned around Center, in radians.",
        {in("center", "Center", "vec2", std::vector<float>{0.5f, 0.5f}), in("angle", "Angle", "float", 0.0f)},
        "graphRotateUV({uv}, {center}, {angle})").functions = {"rotateUV"};
    uvNode("uv/panner", "Panner", "UV moving at Speed over Time.",
        {in("speed", "Speed", "vec2", std::vector<float>{0.1f, 0.0f}), in("time", "Time", "float", "Engine.time")}, "{uv} + {speed} * {time}");
    uvNode("uv/polar", "Polar", "Distance from Center and angle around it, 0 to 1.",
        {in("center", "Center", "vec2", std::vector<float>{0.5f, 0.5f})}, "graphPolar({uv}, {center})").functions = {"polar"};
    uvNode("uv/twirl", "Twirl", "UV turned more the farther it is from Center.",
        {in("center", "Center", "vec2", std::vector<float>{0.5f, 0.5f}), in("strength", "Strength", "float", 10.0f)},
        "graphTwirl({uv}, {center}, {strength})").functions = {"twirl"};
    uvNode("uv/flipbook", "Flipbook", "The tile of a sheet for a frame, left to right, top to bottom.",
        {in("columns", "Columns", "float", 4.0f), in("rows", "Rows", "float", 4.0f), in("frame", "Frame", "float", 0.0f)},
        "graphFlipbook({uv}, {columns}, {rows}, {frame})").functions = {"flipbook"};

    // ---- textures, which name a texture parameter

    ShaderNodeType& sample2D = add("texture/sample2D", "Sample Texture 2D", "Texture",
        "A texture parameter at a UV. Color space turns an image's colors linear; Normal unpacks a normal map to -1 to 1.",
        {in("uv", "UV", "vec2", "@uv1")}, {out("rgba", "RGBA", "vec4")}, {"texture({texture}, {uv})"});
    sample2D.settings = {choice("space", "Space", {"linear", "color", "normal"}), parameterSetting("texture", "Texture", {"texture2D"})};
    sample2D.variants["linear"] = {"texture({texture}, {uv})"};
    sample2D.variants["color"] = {"sRGBToLinear(texture({texture}, {uv}))"};
    sample2D.variants["normal"] = {"vec4(texture({texture}, {uv}).xyz * 2.0 - 1.0, 1.0)"};
    sample2D.include = "includes/srgb.glsl";

    ShaderNodeType& sampleLod = add("texture/sample2DLod", "Sample Texture 2D LOD", "Texture", "A texture parameter at a UV and a mip level.",
        {in("uv", "UV", "vec2", "@uv1"), in("lod", "LOD", "float", 0.0f)}, {out("rgba", "RGBA", "vec4")}, {"textureLod({texture}, {uv}, {lod})"});
    sampleLod.settings = {parameterSetting("texture", "Texture", {"texture2D"})};

    ShaderNodeType& sampleCube = add("texture/sampleCube", "Sample Cubemap", "Texture", "A cube map parameter in a direction.",
        {in("direction", "Direction", "vec3", "@normal")}, {out("rgba", "RGBA", "vec4")}, {"texture({texture}, {direction})"});
    sampleCube.settings = {parameterSetting("texture", "Texture", {"textureCube"})};

    ShaderNodeType& size = add("texture/size", "Texture Size", "Texture", "A texture parameter's size in pixels.",
        {}, {out("size", "Size", "vec2")}, {"vec2(textureSize({texture}, 0))"});
    size.settings = {parameterSetting("texture", "Texture", {"texture2D", "textureCube"})};

    auto normal = [&](const std::string& id, const std::string& name, const std::string& description, std::vector<NodePin> inputs,
                      const std::string& code) -> ShaderNodeType& {
        return add(id, name, "Texture", description, std::move(inputs), {out("out", "Out", "vec3")}, {code});
    };
    normal("texture/normalUnpack", "Normal Unpack", "A normal map's color as a -1 to 1 tangent space normal.",
        {in("in", "In", "vec4", std::vector<float>{0.5f, 0.5f, 1.0f, 1.0f})}, "normalize({in}.xyz * 2.0 - 1.0)");
    normal("texture/normalStrength", "Normal Strength", "A tangent space normal made flatter or stronger.",
        {in("in", "In", "vec3", std::vector<float>{0.0f, 0.0f, 1.0f}), in("strength", "Strength", "float", 1.0f)},
        "normalize(vec3({in}.xy * {strength}, {in}.z))");
    normal("texture/normalBlend", "Normal Blend", "Detail normal B over base normal A, both in tangent space.",
        {in("a", "A", "vec3", std::vector<float>{0.0f, 0.0f, 1.0f}), in("b", "B", "vec3", std::vector<float>{0.0f, 0.0f, 1.0f})},
        "normalize(vec3({a}.xy + {b}.xy, {a}.z * {b}.z))");
    ShaderNodeType& toWorld = normal("texture/normalToWorld", "Normal to World", "A tangent space normal turned into the world, for the surface's Normal.",
        {in("in", "In", "vec3", std::vector<float>{0.0f, 0.0f, 1.0f})}, "normalize(mat3(@tangent, @bitangent, @normal) * {in})");
    toWorld.shaderTypes = {"mesh"};
    toWorld.stage = ShaderNodeStage::FRAGMENT;
    normal("texture/normalFromHeight", "Normal From Height", "A tangent space normal from how a height changes across the screen.",
        {in("height", "Height", "float", 0.0f), in("strength", "Strength", "float", 1.0f)},
        "normalize(vec3(-dFdx({height}) * {strength}, -dFdy({height}) * {strength}, 1.0))").stage = ShaderNodeStage::FRAGMENT;

    // ---- noise

    auto noise = [&](const std::string& id, const std::string& name, const std::string& description, std::vector<NodePin> outputs,
                     std::vector<std::string> code, std::vector<std::string> functions) -> ShaderNodeType& {
        ShaderNodeType& node = add(id, name, "Noise", description,
            {in("uv", "UV", "vec2", "@uv1"), in("scale", "Scale", "float", 10.0f)}, std::move(outputs), std::move(code));
        node.functions = std::move(functions);
        return node;
    };
    noise("noise/hash", "Hash", "A random 0 to 1 for each UV, the same each frame.", {out("out", "Out", "float")}, {"graphHash({uv} * {scale})"}, {"hash"});
    noise("noise/value", "Value Noise", "Smooth blobs, 0 to 1.", {out("out", "Out", "float")}, {"graphValueNoise({uv} * {scale})"}, {"valueNoise"});
    noise("noise/gradient", "Gradient Noise", "Smooth waves, 0 to 1.", {out("out", "Out", "float")}, {"graphGradientNoise({uv} * {scale})"}, {"gradientNoise"});
    ShaderNodeType& voronoi = noise("noise/voronoi", "Voronoi", "Cells: distance to the nearest cell point, and a random value per cell.",
        {out("distance", "Distance", "float"), out("cell", "Cell", "float")}, {"graphVoronoi({uv} * {scale}, {jitter}).x", "graphVoronoi({uv} * {scale}, {jitter}).y"}, {"voronoi"});
    voronoi.inputs.push_back(in("jitter", "Jitter", "float", 1.0f));

    // ---- logic

    ShaderNodeType& compare = add("logic/compare", "Compare", "Logic", "Whether A and B relate as the operation says.",
        {in("a", "A", "float", 0.0f), in("b", "B", "float", 0.0f)}, {out("out", "Out", "bool")}, {"{a} < {b}"});
    compare.settings = {choice("operation", "Operation", {"less", "lessEqual", "greater", "greaterEqual", "equal", "notEqual"})};
    compare.variants["less"] = {"{a} < {b}"};
    compare.variants["lessEqual"] = {"{a} <= {b}"};
    compare.variants["greater"] = {"{a} > {b}"};
    compare.variants["greaterEqual"] = {"{a} >= {b}"};
    compare.variants["equal"] = {"{a} == {b}"};
    compare.variants["notEqual"] = {"{a} != {b}"};
    add("logic/branch", "Branch", "Logic", "True when Condition holds, else False.",
        {in("condition", "Condition", "bool", "true"), in("true", "True", "dynamic", 1.0f), in("false", "False", "dynamic", 0.0f)},
        {out("out", "Out", "dynamic")}, {"{condition} ? {true} : {false}"});
    add("logic/and", "And", "Logic", "Whether both hold.", {in("a", "A", "bool", "true"), in("b", "B", "bool", "true")}, {out("out", "Out", "bool")}, {"{a} && {b}"});
    add("logic/or", "Or", "Logic", "Whether either holds.", {in("a", "A", "bool", "false"), in("b", "B", "bool", "false")}, {out("out", "Out", "bool")}, {"{a} || {b}"});
    add("logic/not", "Not", "Logic", "Whether it does not hold.", {in("in", "In", "bool", "false")}, {out("out", "Out", "bool")}, {"!{in}"});
    ShaderNodeType& pass = add("logic/pass", "Pass Switch", "Logic", "A value for the color pass and another for the depth, shadow and G-buffer passes.",
        {in("color", "Color Pass", "dynamic", 1.0f), in("other", "Other Passes", "dynamic", 0.0f)}, {out("out", "Out", "dynamic")},
        {"(Engine.pass == PASS_COLOR ? {color} : {other})"});
    pass.shaderTypes = {"mesh"};

    // ---- effects, the functions the engine's ready-made shader files call

    ShaderNodeType& wind = add("effect/wind", "Wind Sway", "Effect", "Sways the mesh in the scene wind (Wind.velocity), with gusts.",
        {in("sway", "Sway", "float", 0.3f), in("height", "Height", "float", 0.0f)}, {out("offset", "Offset", "vec3")},
        {"windSway(@localPosition, @worldPosition, {sway}, {height})"});
    wind.include = "effects/wind.glsl";
    wind.stage = ShaderNodeStage::VERTEX;
    wind.shaderTypes = {"mesh"};
    ShaderNodeType& push = add("effect/interaction", "Interaction Push", "Effect", "Bends the mesh away from Interaction.position, which a script moves.",
        {in("strength", "Strength", "float", 1.0f), in("height", "Height", "float", 0.0f)}, {out("offset", "Offset", "vec3")},
        {"interactionPush(@localPosition, @worldPosition, {strength}, {height})"});
    push.include = "effects/interaction.glsl";
    push.stage = ShaderNodeStage::VERTEX;
    push.shaderTypes = {"mesh"};
    ShaderNodeType& seeThrough = add("effect/seeThrough", "See-Through", "Effect", "Opens the mesh around SeeThrough.target, as a coverage.",
        {}, {out("coverage", "Coverage", "float")}, {"seeThrough(context)"});
    seeThrough.include = "effects/see_through.glsl";
    seeThrough.stage = ShaderNodeStage::FRAGMENT;
    seeThrough.shaderTypes = {"mesh"};
    ShaderNodeType& dissolve = add("effect/dissolve", "Dissolve", "Effect", "Eats the mesh away with noise as Amount goes to 1, as a coverage.",
        {in("amount", "Amount", "float", 0.0f), in("scale", "Scale", "float", 4.0f), in("edge", "Edge", "float", 0.05f)},
        {out("coverage", "Coverage", "float")}, {"dissolve(@worldPosition, {amount}, {scale}, {edge})"});
    dissolve.include = "effects/dissolve.glsl";
    dissolve.stage = ShaderNodeStage::FRAGMENT;
    dissolve.shaderTypes = {"mesh"};
    ShaderNodeType& clip = add("effect/heightClip", "Height Clip", "Effect", "Removes what is above Clip.height, as a coverage.",
        {}, {out("coverage", "Coverage", "float")}, {"heightClip(@worldPosition)"});
    clip.include = "effects/height_clip.glsl";
    clip.stage = ShaderNodeStage::FRAGMENT;
    clip.shaderTypes = {"mesh"};
    ShaderNodeType& fade = add("effect/depthFade", "Depth Fade", "Effect", "0 where the mesh meets what is behind it, 1 a Distance away.",
        {in("distance", "Distance", "float", 1.0f)}, {out("out", "Out", "float")},
        {"clamp((sceneDepth(@screenUV) - dot(@worldPosition - @eye, @forward)) / {distance}, 0.0, 1.0)"});
    fade.stage = ShaderNodeStage::FRAGMENT;
    fade.shaderTypes = {"mesh"};
    ShaderNodeType& refraction = add("effect/refraction", "Refraction", "Effect", "What is behind, bent by the normal.",
        {in("normal", "Normal", "vec3", "@normal"), in("strength", "Strength", "float", 0.05f)}, {out("color", "Color", "vec4")},
        {"sceneColor(@screenUV + {normal}.xy * {strength})"});
    refraction.stage = ShaderNodeStage::FRAGMENT;
    refraction.shaderTypes = {"mesh"};
    ShaderNodeType& dither = add("effect/dither", "Dither", "Effect", "A fixed pattern to compare a fading value with, 0 to 1.",
        {}, {out("out", "Out", "float")}, {"coverageDitherThreshold(@pixel)"});
    dither.stage = ShaderNodeStage::FRAGMENT;
    dither.shaderTypes = {"mesh"};

    functions["intersectionFoam"] = {{"valueNoise"}, R"(// 1 in a band where a surface meets what is behind it, the edge broken by moving noise
float graphIntersectionFoam(float gap, vec2 position, float time, float width, float scale, float speed, float breakup){
    float noise = graphValueNoise(position * scale + vec2(time * speed, time * speed * 0.7));
    return 1.0 - smoothstep(0.0, width, gap + (noise - 0.5) * breakup * width);
}
)"};
    ShaderNodeType& foam = add("effect/intersectionFoam", "Intersection Foam", "Effect",
        "1 in a band where the mesh meets what is behind it, its edge broken by moving noise, as foam where water meets rocks.",
        {in("width", "Width", "float", 0.4f), in("scale", "Scale", "float", 3.0f), in("speed", "Speed", "float", 0.4f), in("breakup", "Breakup", "float", 1.0f)},
        {out("out", "Out", "float")},
        {"graphIntersectionFoam(sceneDepth(@screenUV) - dot(@worldPosition - @eye, @forward), @worldPosition.xz, Engine.time, {width}, {scale}, {speed}, {breakup})"});
    foam.functions = {"intersectionFoam"};
    foam.stage = ShaderNodeStage::FRAGMENT;
    foam.shaderTypes = {"mesh"};

    functions["triplanar"] = {{}, R"(// a texture seen from the three axes, blended by how the normal faces them
vec4 graphTriplanar(texture2D graphTexture, sampler graphSampler, vec3 graphPosition, vec3 graphNormal, float graphSharpness){
    vec3 weights = pow(abs(graphNormal), vec3(graphSharpness));
    weights /= max(weights.x + weights.y + weights.z, 1.0e-5);
    return texture(sampler2D(graphTexture, graphSampler), graphPosition.zy) * weights.x +
           texture(sampler2D(graphTexture, graphSampler), graphPosition.xz) * weights.y +
           texture(sampler2D(graphTexture, graphSampler), graphPosition.xy) * weights.z;
}
)"};
    ShaderNodeType& triplanar = add("texture/triplanar", "Triplanar", "Texture",
        "A texture parameter projected along the three world axes, for surfaces without good UVs.",
        {in("position", "Position", "vec3", "@worldPosition"), in("normal", "Normal", "vec3", "@normal"),
         in("scale", "Scale", "float", 1.0f), in("sharpness", "Sharpness", "float", 4.0f)},
        {out("rgba", "RGBA", "vec4")}, {"graphTriplanar({texture}_texture, {texture}_sampler, {position} * {scale}, {normal}, {sharpness})"});
    triplanar.settings = {parameterSetting("texture", "Texture", {"texture2D"})};
    triplanar.functions = {"triplanar"};
    triplanar.stage = ShaderNodeStage::FRAGMENT;

    functions["parallax"] = {{}, R"(// the uv a height map moves away from the eye, in steps down to where the view meets it
vec2 graphParallax(texture2D graphTexture, sampler graphSampler, vec2 graphUV, vec3 graphView, vec3 graphTangent, vec3 graphBitangent, vec3 graphNormal, float graphScale, float graphSteps){
    vec3 toEye = vec3(dot(graphView, graphTangent), dot(graphView, graphBitangent), dot(graphView, graphNormal));
    float count = clamp(graphSteps, 1.0, 32.0);
    vec2 shift = toEye.xy / max(toEye.z, 0.1) * graphScale / count;
    // one gradient for every step, so the loop needs none of its own
    vec2 dx = dFdx(graphUV);
    vec2 dy = dFdy(graphUV);
    vec2 uv = graphUV;
    float layer = 0.0;
    float depth = 1.0 - textureGrad(sampler2D(graphTexture, graphSampler), uv, dx, dy).r;
    for (int i = 0; i < 32; i++) {
        if (float(i) >= count || layer >= depth)
            break;
        uv -= shift;
        layer += 1.0 / count;
        depth = 1.0 - textureGrad(sampler2D(graphTexture, graphSampler), uv, dx, dy).r;
    }
    return uv;
}
)"};
    ShaderNodeType& parallax = add("uv/parallax", "Parallax", "UV",
        "UV moved by a height map as the view slants, so a flat surface looks deep. White in the map is high.",
        {in("uv", "UV", "vec2", "@uv1"), in("scale", "Scale", "float", 0.05f), in("steps", "Steps", "float", 12.0f)},
        {out("uv", "UV", "vec2")},
        {"graphParallax({texture}_texture, {texture}_sampler, {uv}, @view, @tangent, @bitangent, @normal, {scale}, {steps})"});
    parallax.settings = {parameterSetting("texture", "Height Map", {"texture2D"})};
    parallax.functions = {"parallax"};
    parallax.stage = ShaderNodeStage::FRAGMENT;
    parallax.shaderTypes = {"mesh"};

    // tangent space runs along the UV set's u and v and the normal; view space is the camera's, z toward the eye
    ShaderNodeType& transform = add("vector/transform", "Transform", "Vector", "A direction taken from one space to another: world, tangent or view.",
        {in("in", "In", "vec3", std::vector<float>{0.0f, 0.0f, 1.0f})}, {out("out", "Out", "vec3")}, {"{in}"});
    transform.settings = {choice("spaces", "Spaces", {"tangentToWorld", "worldToTangent", "worldToView", "viewToWorld", "tangentToView", "viewToTangent"})};
    transform.variants["tangentToWorld"] = {"({in}.x * @tangent + {in}.y * @bitangent + {in}.z * @normal)"};
    transform.variants["worldToTangent"] = {"vec3(dot({in}, @tangent), dot({in}, @bitangent), dot({in}, @normal))"};
    transform.variants["worldToView"] = {"(@worldToView * {in})"};
    transform.variants["viewToWorld"] = {"({in} * @worldToView)"};
    transform.variants["tangentToView"] = {"(@worldToView * ({in}.x * @tangent + {in}.y * @bitangent + {in}.z * @normal))"};
    transform.variants["viewToTangent"] = {"vec3(dot({in} * @worldToView, @tangent), dot({in} * @worldToView, @bitangent), dot({in} * @worldToView, @normal))"};
    transform.stage = ShaderNodeStage::FRAGMENT;
    transform.shaderTypes = {"mesh"};

    ShaderNodeType& swizzle = add("vector/swizzle", "Swizzle", "Vector", "Components picked and reordered, as zyx or xxy.",
        {in("in", "In", "vec4")}, {out("out", "Out", "mask")}, {"{in}.{mask}"});
    swizzle.settings = {setting("mask", "Mask", "text", "xyz")};
    swizzle.inlined = true;

    // ---- utility

    ShaderNodeType& reroute = add("utility/reroute", "Reroute", "Utility", "Passes its input on, to tidy links.",
        {in("in", "In", "dynamic", 0.0f)}, {out("out", "Out", "dynamic")}, {"{in}"});
    reroute.inlined = true;
    ShaderNodeType& custom = add("utility/customFunction", "Custom Function", "Utility",
        "GLSL of your own: the signature gives the pins, the body runs, or a .glsl file of the project holds the function.",
        {}, {out("out", "Out", "vec3")}, {"%custom"});
    custom.settings = {setting("signature", "Signature", "text", "vec3 shade(vec3 color)"), setting("body", "Body", "text", "return color;"),
                       setting("file", "File", "text")};
    add("utility/vertexToFragment", "Vertex to Fragment", "Utility",
        "Works its input out per vertex and passes it to the fragments, interpolated, where it costs less.",
        {in("in", "In", "dynamic", 0.0f)}, {out("out", "Out", "dynamic")}, {"{in}"}).shaderTypes = {"mesh"};
    ShaderNodeType& subgraph = add("utility/subgraph", "Subgraph", "Utility",
        "Runs a function graph of the project, a .shader of type function: its Function Inputs are the pins on the left, its Function Outputs those on the right.",
        {}, {}, {});
    subgraph.settings = {setting("file", "Function", "function")};

    // ---- functions: what a function graph takes and gives

    const std::vector<std::string> valueTypes = {"float", "vec2", "vec3", "vec4", "color", "int", "bool"};
    ShaderNodeType& functionInput = add("function/input", "Function Input", "Function",
        "A value the calling graph passes in, a pin of its Subgraph node. Default is used when it passes none.",
        {in("default", "Default", "float", 0.0f)}, {out("out", "Out", "float")}, {"{default}"});
    functionInput.settings = {setting("name", "Name", "text", "value"), choice("valueType", "Type", valueTypes)};
    functionInput.shaderTypes = {"function"};
    functionInput.inlined = true;
    ShaderNodeType& functionOutput = add("function/output", "Function Output", "Function",
        "A value the function gives back, a pin on the right of its Subgraph node.",
        {in("value", "Value", "float", 0.0f)}, {}, {});
    functionOutput.settings = {setting("name", "Name", "text", "result"), choice("valueType", "Type", valueTypes)};
    functionOutput.shaderTypes = {"function"};
}
