#version 450

// A fork may declare a u_fs_customParams block: its members are filled by name from the
// mesh's shader uniforms, the same values the color fork reads. Two names are written by
// the engine instead: "time" (seconds since startup) and "resolution" (xy = shadow slot
// or depth target size, zw = 1 / size). Do not mix int and float members: GL uploads the
// block typed after its first member.
//
//   uniform u_fs_customParams {
//       float time;
//       float dissolve;
//   } customParams;

out vec4 frag_color;
in vec2 v_projZW;
// in the vertex stage's order: GL pairs the stages by location
#ifdef USE_SEE_THROUGH
    in vec3 v_worldPosition;
#endif

#if defined(HAS_TEXTURE)
    uniform texture2D u_depthTexture;
    uniform sampler u_depth_smp;
    in vec2 v_uv1;
#endif

#if defined(ALPHA_MASK)
    uniform u_fs_depthMaterial {
        vec4 alphaParams; // x = baseColorFactor.a, y = alphaCutoff
    } depthMaterial;
#endif

#include "includes/depth_util.glsl"
#ifdef USE_SEE_THROUGH
    #include "includes/see_through.glsl"
#endif

void main() {
    #ifdef USE_SEE_THROUGH
        seeThroughDiscard(v_worldPosition);
    #endif
    #if defined(ALPHA_MASK)
        float alpha = depthMaterial.alphaParams.x;
    #endif
    #if defined(HAS_TEXTURE)
        vec4 texColor = texture(sampler2D(u_depthTexture, u_depth_smp), v_uv1);

        #if defined(ALPHA_MASK)
            alpha *= texColor.a;
        #endif
    #endif
    #if defined(ALPHA_MASK)
        if (alpha < depthMaterial.alphaParams.y) {
            discard;
        }
    #endif

    // gl_FragCoord.z is in [0,1] range
    //frag_color = encodeDepth(gl_FragCoord.z);

    // Higher precision equivalent of gl_FragCoord.z in some platforms. See Three.js depth_frag.glsl.js
	frag_color = encodeDepth(0.5 * v_projZW[0] / v_projZW[1] + 0.5);
}
