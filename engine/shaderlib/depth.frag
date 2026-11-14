#version 450

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
#ifdef HOOK_FRAGMENT_INSTANCE
    flat in vec4 v_hookInstance;
#endif
#ifdef HOOK_FRAGMENT_UV2
    in vec2 v_hookUV2;
#endif
#ifdef HOOK_TANGENT
    in vec4 v_hookTangent;
#endif
#ifdef USE_VARYINGS
    #include "shader_varyings_fs.glsl"
#endif

#if defined(ALPHA_MASK)
    uniform u_fs_depthMaterial {
        vec4 alphaParams; // x = baseColorFactor.a, y = alphaCutoff
        #ifdef USE_SURFACE
            vec4 baseColorFactor;
            vec4 surfaceFactors; // x = metallic, y = roughness, z = 1 when unlit
            vec4 emissiveFactor;
        #endif
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
        #ifdef HOOK_FRAGMENT_UV2
            context.uv2 = v_hookUV2;
        #endif
        #ifdef HOOK_FRAGMENT_TANGENT
            #ifdef HOOK_TANGENT
                setTangents(context, v_hookTangent.xyz, cross(v_hookNormal, v_hookTangent.xyz) * v_hookTangent.w);
            #else
                setDerivedTangents(context);
            #endif
        #endif
        #ifdef HOOK_FRAGMENT_COLOR
            context.color = v_hookColor;
        #endif
        #ifdef HOOK_FRAGMENT_INSTANCE
            context.instance = v_hookInstance;
        #endif
        return context;
    }
#endif

void main() {
    loadShaderParameters();
    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context = getDepthContext();
    #endif
    #ifdef USE_COVERAGE
        applyCoverage(context);
    #endif
    #if defined(ALPHA_MASK)
        float alpha = depthMaterial.alphaParams.x;
    #endif
    #if defined(HAS_TEXTURE) && !defined(SURFACE_REPLACES_BASECOLOR_MAP)
        vec4 texColor = texture(sampler2D(u_depthTexture, u_depth_smp), v_uv1);

        #if defined(ALPHA_MASK)
            alpha *= texColor.a;
        #endif
    #endif
    #ifdef USE_SURFACE
        // only its alpha cuts here, as in the color pass
        Surface surface;
        surface.baseColor = depthMaterial.baseColorFactor.rgb;
        #if defined(HAS_TEXTURE) && !defined(SURFACE_REPLACES_BASECOLOR_MAP)
            surface.baseColor *= sRGBToLinear(texColor).rgb;
        #endif
        surface.alpha = alpha;
        #ifdef HOOK_FRAGMENT_COLOR
            // the vertex and instance color tint it, as in the color pass
            surface.baseColor *= context.color.rgb;
            surface.alpha *= context.color.a;
        #endif
        #ifdef HOOK_FRAGMENT_NORMAL
            vec3 normal = context.normal;
        #else
            // without normals, the flat one the color pass makes
            vec3 normal = getFlatNormal(context.worldPosition, context.view);
        #endif
        // an unlit color pass gives no normal, but this placeholder
        surface.normal = depthMaterial.surfaceFactors.z > 0.5 ? vec3(0.0, 1.0, 0.0) : normal;
        // factors only, the maps are not sampled here
        surface.metallic = depthMaterial.surfaceFactors.x;
        surface.roughness = depthMaterial.surfaceFactors.y;
        surface.occlusion = 1.0;
        surface.emissive = depthMaterial.emissiveFactor.rgb;
        getSurface(context, surface);
        alpha = surface.alpha * keepHookInputs(context);
    #endif
    #if defined(ALPHA_MASK)
        if (alpha < depthMaterial.alphaParams.y) {
            discard;
        }
    #endif

    // gl_FragCoord.z is in [0,1] range
    //frag_color = encodeDepth(gl_FragCoord.z);

    // Higher precision equivalent of gl_FragCoord.z in some platforms. See Three.js depth_frag.glsl.js
    #ifdef USE_DEPTH_OFFSET
        float depth = getHookDepth(context);
        gl_FragDepth = depth;
        frag_color = encodeDepth(depth);
    #else
        frag_color = encodeDepth(0.5 * v_projZW[0] / v_projZW[1] + 0.5);
    #endif
}
