#version 450

// Declare values as "#pragma param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.

out vec4 frag_color;

in vec3 uv;

uniform textureCube u_skyTexture;
uniform sampler u_sky_smp;

uniform u_fs_skyParams {
    vec4 color; //sRGB
} skyParams;

#include "includes/parameters_fs.glsl"

void main(){
    loadShaderParameters();

    frag_color = skyParams.color * texture(samplerCube(u_skyTexture, u_sky_smp), uv);
}