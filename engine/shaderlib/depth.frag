#version 450

// Declare values as "#pragma param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.

out vec4 frag_color;
in vec2 v_projZW;
// in the vertex stage's order: GL pairs the stages by location
#ifdef USE_FRAGMENT_HOOKS
    in vec3 v_worldPosition;
#endif

#if defined(HAS_TEXTURE)
    uniform texture2D u_depthTexture;
    uniform sampler u_depth_smp;
#endif
#if defined(HAS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
    in vec2 v_uv1;
#endif
#ifdef HOOK_FRAGMENT_NORMAL
    in vec3 v_hookNormal;
#endif
#ifdef HOOK_FRAGMENT_COLOR
    in vec4 v_hookColor;
#endif

#if defined(ALPHA_MASK)
    uniform u_fs_depthMaterial {
        vec4 alphaParams; // x = baseColorFactor.a, y = alphaCutoff
    } depthMaterial;
#endif

#include "includes/depth_util.glsl"
#include "includes/parameters_fs.glsl"
#ifdef USE_FRAGMENT_HOOKS
    #include "includes/hooks_fs.glsl"

    FragmentContext getDepthContext(){
        FragmentContext context = getFragmentContext(v_worldPosition);
        #ifdef HOOK_FRAGMENT_UV1
            context.uv1 = v_uv1;
        #endif
        #ifdef HOOK_FRAGMENT_NORMAL
            context.normal = normalize(gl_FrontFacing ? v_hookNormal : -v_hookNormal);
        #endif
        #ifdef HOOK_FRAGMENT_COLOR
            context.color = v_hookColor;
        #endif
        return context;
    }
#endif

void main() {
    loadShaderParameters();
    #ifdef USE_COVERAGE
        applyCoverage(getDepthContext());
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
