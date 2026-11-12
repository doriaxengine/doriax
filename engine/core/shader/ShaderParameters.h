// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SHADERPARAMETERS_H
#define SHADERPARAMETERS_H

#include "Export.h"
#include "ShaderData.h"
#include "ShaderUniforms.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Matrix4.h"
#include <string>
#include <vector>
#include <cstdint>

namespace doriax{

    class Scene;

    // resolved values of one scene or one camera, indexed by interned key
    class DORIAX_API ShaderParameterTable{
    private:
        std::vector<Vector4> values;
        std::vector<uint8_t> present;

    public:
        void clear();
        void set(uint32_t key, const Vector4& value);
        void apply(const ShaderUniformValues& entries);
        const Vector4* find(uint32_t key) const;
    };

    class DORIAX_API ShaderParameters{
    public:
        // "Collection.name"
        static std::string getKey(const std::string& collection, const std::string& name);
        static bool splitKey(const std::string& key, std::string& collection, std::string& name);
        // a GLSL identifier, without "__" or a "gl_" prefix
        static bool isValidName(const std::string& name);
        static bool isValidKey(const std::string& collection, const std::string& name);
        // isValidKey, with an error when it is not
        static bool checkKey(const std::string& collection, const std::string& name);

        // process-wide id of a key, so tables index instead of compare
        static uint32_t intern(const std::string& key);

        static const char* getScopeName(ShaderParameterScope scope);
        static int getComponentCount(ShaderUniformType type);
    };

    // what a pass draws, as Engine.pass tells shaders
    enum class ShaderPass{
        COLOR,
        DEPTH,
        SHADOW,
        GBUFFER
    };

    // the camera a pass draws for, as effect functions see it
    struct ShaderEffectView{
        Vector3 eye = Vector3(0.0f, 0.0f, 0.0f);
        Vector3 forward = Vector3(0.0f, 0.0f, -1.0f);
        bool perspective = true;
        // pixel the coverage dither counts from
        Vector2 ditherOrigin = Vector2(0.0f, 0.0f);
        float time = 0.0f;
        ShaderPass pass = ShaderPass::COLOR;
        Matrix4 viewMatrix;
        // the projection's z and w terms of view depth, 0 where depth offsets do not apply
        Vector2 depthProjection = Vector2(0.0f, 0.0f);
        // drawn with the y-flipped matrix of GL offscreen targets
        bool flipY = false;
        const ShaderParameterTable* scene = nullptr;
        const ShaderParameterTable* view = nullptr;
    };

    // declared parameters of one shader stage, packed as std140 at each draw;
    // a material prefix before them is left to the caller
    struct DORIAX_API ShaderParameterBlock{
        struct Binding{
            unsigned int offset = 0;
            ShaderUniformType type = ShaderUniformType::FLOAT4;
            ShaderParameterScope scope = ShaderParameterScope::OBJECT;
            uint32_t key = 0;     // interned key of SCENE and VIEW
            std::string name;     // OBJECT uniform name or ENGINE context id
            Vector4 defaultValue;
        };

        int slot = -1;
        unsigned int size = 0;
        unsigned int firstOffset = 0;
        std::vector<uint8_t> data;
        std::vector<Binding> bindings;
        // engine value bindings, -1 when not declared
        int engineTime = -1;
        int engineResolution = -1;
        int enginePass = -1;
        int worldToModel = -1;
        int offsetMax = -1;
        int eye = -1;
        int forward = -1;
        int pass = -1;
        int worldToView = -1;
        int projection = -1;
        int flipY = -1;
        bool shared = false;     // has SCENE or VIEW members

        bool resolve(ShaderData& shaderData, const std::string& blockName, ShaderStageType stage);
        void clear();

        // own value, else the parent's, the inherited one or the default
        void writeObject(const ShaderUniformValues& values, const ShaderUniformValues* inherited = nullptr, const ShaderUniformValues* parent = nullptr);
        // the engine clock and target size, then the scene and view values
        void writeFrame(const ShaderEffectView& view, const Vector2& resolution);
        void writeVertexContext(const ShaderEffectView& view, const Matrix4& modelMatrix, float maxOffset);
        // coverage false bypasses the effect, fade blends instead of dithering
        void writeFragmentContext(const ShaderEffectView& view, bool coverage, bool fade);
    };

    // writes Wind.travel, the meters of air blown past, so gusts do not jump when the wind changes
    class DORIAX_API WindParameterProvider{
    private:
        double travel = 0.0;

    public:
        void update(Scene* scene, double dt, bool paused);
        void reset();
    };

}

#endif //SHADERPARAMETERS_H
