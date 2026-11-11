#version 450

// Built-in post-process pass: a passthrough of the scene color. It is the template a
// user fork starts from and the fallback when a fork fails to build.
//
// A fork may declare any subset of the pass inputs; only the ones it uses are bound:
// u_sceneColorTexture (previous pass output), u_depthTexture (camera-space packed
// depth), u_gbufferTexture (view-space normal + roughness/metallic, SSR only) and
// u_ssaoTexture. Depth and the G-buffer keep the logical orientation of the depth
// pre-pass, so flip v_texcoord.y on GL when sampling them (see ssr.frag).
//
// Declare values as "#pragma param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(); the pass shows them in the
// scene post-process list. Engine.time and Engine.resolution (xy size, zw 1 / size) come
// from the engine.

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_sceneColorTexture;
uniform sampler u_sceneColor_smp;

#include "includes/parameters_fs.glsl"

void main(){
    loadShaderParameters();

    frag_color = texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), v_texcoord);
}
