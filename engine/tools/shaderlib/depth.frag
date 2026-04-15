#version 450

out vec4 frag_color;
in vec2 v_projZW;

#if defined(HAS_TEXTURE)
    uniform texture2D u_depthTexture;
    uniform sampler u_depth_smp;
    in vec2 v_uv1;
#endif

#include "includes/depth_util.glsl"

void main() {
    #if defined(HAS_TEXTURE)
        vec4 texColor = texture(sampler2D(u_depthTexture, u_depth_smp), v_uv1);

        // Check the alpha value; discard the fragment if the alpha is below a threshold
        if (texColor.a < 0.5) {
            discard;
        }
    #endif

    // gl_FragCoord.z is in [0,1] range
    //frag_color = encodeDepth(gl_FragCoord.z);

    // Higher precision equivalent of gl_FragCoord.z in some platforms. See Three.js depth_frag.glsl.js
	frag_color = encodeDepth(0.5 * v_projZW[0] / v_projZW[1] + 0.5);
}