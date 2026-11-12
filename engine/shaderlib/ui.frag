#version 450

// Declare values as "#pragma param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.

uniform u_fs_uiParams {
    vec4 color;
} uiParams;

out vec4 g_finalColor;

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 v_color;
#endif
#ifdef HAS_VERTEX_COLOR_VEC4
    in vec4 v_color;
#endif

#if defined(HAS_TEXTURE) || defined(HAS_FONTATLAS_TEXTURE)
    uniform texture2D u_uiTexture;
    uniform sampler u_ui_smp;
#endif
#if defined(HAS_TEXTURE) || defined(HAS_FONTATLAS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
    in vec2 v_uv1;
#endif
#ifdef HOOK_FRAGMENT_POSITION
    in vec3 v_hookPosition;
#endif

#include "includes/srgb.glsl"

#include "includes/parameters_fs.glsl"
#ifdef USE_FRAGMENT_HOOKS
    #include "includes/hooks_color_fs.glsl"
#endif

vec4 getVertexColor(){
   vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

    #ifdef HAS_VERTEX_COLOR_VEC3
        color.rgb = v_color.rgb;
    #endif
    #ifdef HAS_VERTEX_COLOR_VEC4
        color = v_color;
    #endif

   return color;
}

vec4 getBaseColor(){
    vec4 baseColor = uiParams.color;
    #ifdef HAS_TEXTURE
        baseColor *= sRGBToLinear(texture(sampler2D(u_uiTexture, u_ui_smp), v_uv1));
    #endif
    #ifdef HAS_FONTATLAS_TEXTURE
        baseColor *= vec4(1.0, 1.0, 1.0, texture(sampler2D(u_uiTexture, u_ui_smp), v_uv1).r);
    #endif
    return baseColor * getVertexColor();
}

#ifdef USE_FRAGMENT_HOOKS
    // the parts of the built-in color, sampled once
    FragmentContext getUIContext(){
        FragmentContext context;
        context.worldPosition = vec3(0.0);
        context.uv1 = vec2(0.0);
        #ifdef HOOK_FRAGMENT_POSITION
            context.worldPosition = v_hookPosition;
        #endif
        #if defined(HAS_TEXTURE) || defined(HAS_FONTATLAS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
            context.uv1 = v_uv1;
        #endif
        context.color = getVertexColor();
        context.textureColor = vec4(1.0);
        #ifdef HAS_TEXTURE
            context.textureColor = sRGBToLinear(texture(sampler2D(u_uiTexture, u_ui_smp), v_uv1));
        #endif
        #ifdef HAS_FONTATLAS_TEXTURE
            context.textureColor.a = texture(sampler2D(u_uiTexture, u_ui_smp), v_uv1).r;
        #endif
        context.tint = uiParams.color;
        context.pixel = gl_FragCoord.xy;
        return context;
    }
#endif

void main() {  
    loadShaderParameters();

    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context = getUIContext();
        vec4 color = context.tint * context.textureColor * context.color;
        color.a *= keepHookInputs(context);
        color = getColor(context, color);
    #else
        vec4 color = getBaseColor();
    #endif

    g_finalColor = (vec4(linearTosRGB(color.rgb), color.a));
}