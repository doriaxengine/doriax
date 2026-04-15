//
// (c) 2026 Eduardo Doria.
//

#ifndef SokolShader_h
#define SokolShader_h

#include "render/Render.h"
#include "shader/ShaderData.h"
#include "sokol_gfx.h"

namespace doriax{
    class SokolShader{

    private:
        sg_shader shader;

        int roundup(int val, int round_to);
        sg_uniform_type uniformToSokolType(ShaderUniformType type);
        sg_uniform_type flattenedUniformToSokolType(ShaderUniformType type);
        sg_image_sample_type textureSamplerToSokolType(TextureSamplerType type);
        sg_image_type textureToSokolType(TextureType type);
        sg_sampler_type samplerToSokolType(SamplerType type);

    public:
        SokolShader();
        SokolShader(const SokolShader& rhs);
        SokolShader& operator=(const SokolShader& rhs);

        bool createShader(ShaderData& shaderData);
        void destroyShader();
        bool isCreated();

        sg_shader get();
    };
}


#endif /* SokolShader_h */
