#version 450

out vec4 frag_color;

in vec3 uv;

uniform textureCube u_skyTexture;
uniform sampler u_sky_smp;

uniform u_fs_skyParams {
    vec4 color; //sRGB
} skyParams;

void main(){
    frag_color = skyParams.color * texture(samplerCube(u_skyTexture, u_sky_smp), uv);
}