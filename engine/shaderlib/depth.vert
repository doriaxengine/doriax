#version 450

#define DEPTH_SHADER

// mvpMatrix is lightVPMatrix * modelMatrix computed once, so a depth prepass matches
// mesh.vert bit for bit
uniform u_vs_depthParams {
    mat4 modelMatrix;
    mat4 lightVPMatrix;
    mat4 mvpMatrix;
    #ifdef HOOK_NORMAL
        mat4 normalMatrix;
    #endif
} depthParams;

invariant gl_Position;

#ifdef USE_INSTANCE_FADE
    uniform u_vs_fade {
        vec4 fadeRange; //start.x, end.y
        vec4 fadeEye;   //camera in model space.xyz
    } fadeParams;
#endif

in vec3 a_position;
out vec2 v_projZW;
#ifdef USE_FRAGMENT_HOOKS
    out vec3 v_worldPosition;
#endif

#ifdef HAS_INSTANCING
    in vec4 i_matrix_col1;
    in vec4 i_matrix_col2;
    in vec4 i_matrix_col3;
    in vec4 i_matrix_col4;
    #include "includes/instance_matrix.glsl"
#endif

// the alpha mask texture, or the hooks reading the UV set
#if defined(HAS_TEXTURE) || defined(HOOK_UV1)
    in vec2 a_texcoord1;
#endif
#if defined(HAS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
    out vec2 v_uv1;
#endif

#ifdef HOOK_NORMAL
    in vec3 a_normal;
#endif
#ifdef HOOK_COLOR
    #if defined(HAS_VERTEX_COLOR_VEC3)
        in vec3 a_color;
    #elif defined(HAS_VERTEX_COLOR_VEC4)
        in vec4 a_color;
    #endif
    #ifdef HAS_INSTANCING
        in vec4 i_color;
    #endif
#endif
#ifdef HOOK_FRAGMENT_NORMAL
    out vec3 v_hookNormal;
#endif
#ifdef HOOK_FRAGMENT_COLOR
    out vec4 v_hookColor;
#endif
#ifdef HOOK_FRAGMENT_INSTANCE
    flat out vec4 v_hookInstance;
#endif
#ifdef USE_VARYINGS
    #include "shader_varyings_vs.glsl"
#endif

#include "includes/parameters_vs.glsl"
#include "includes/skinning.glsl"
#include "includes/morphtarget.glsl"
#ifdef HAS_TERRAIN
    #include "includes/terrain_vs.glsl"
#endif
#if defined(USE_VERTEX_HOOKS) || defined(USE_FRAGMENT_HOOKS)
    #define HOOK_NORMAL_MATRIX depthParams.normalMatrix
    #include "includes/hooks_vs.glsl"
#endif

vec4 getPosition(mat4 boneTransform){
    vec3 pos = a_position;

    pos = getMorphPosition(pos);
    pos = getSkinPosition(pos, getBoneTransform());
    #ifdef HAS_TERRAIN
        pos = getTerrainPosition(pos, depthParams.modelMatrix);
    #endif

    return vec4(pos, 1.0);
}

void main() {
    loadShaderParameters();

    mat4 boneTransform = getBoneTransform();

    vec4 localPos = getPosition(boneTransform);
    #ifdef HAS_INSTANCING
        mat4 instanceMatrix = getInstanceMatrix();
        vec4 pos = instanceMatrix * localPos;
    #else
        vec4 pos = localPos;
    #endif

    #if defined(HAS_TEXTURE) || defined(HOOK_FRAGMENT_UV1)
        v_uv1 = a_texcoord1;
    #endif

    #ifdef USE_VERTEX_HOOKS
        VertexContext context = getVertexContext(localPos.xyz, (depthParams.modelMatrix * pos).xyz, boneTransform);
    #endif
    // before the fade, so a faded instance still shrinks to nothing
    #ifdef USE_VERTEX_OFFSET
        pos.xyz += getModelOffset(context);
    #endif
    #ifdef USE_VARYINGS
        Varyings varyings;
        getVaryings(context, varyings);
        writeVaryings(varyings);
    #endif

    #ifdef USE_INSTANCE_FADE
        // Matches mesh.vert exactly, so a shrinking instance shrinks its shadow in step.
        vec3 fadeOrigin = i_matrix_col4.xyz;
        float fadeVisible = 1.0;
        if (fadeParams.fadeRange.y > fadeParams.fadeRange.x){
            float fadeDistance = length(fadeParams.fadeEye.xz - fadeOrigin.xz);
            fadeVisible = clamp((fadeParams.fadeRange.y - fadeDistance) /
                (fadeParams.fadeRange.y - fadeParams.fadeRange.x), 0.0, 1.0);
        }
        pos.xyz = mix(fadeOrigin, pos.xyz, fadeVisible);
    #endif

    gl_Position = depthParams.mvpMatrix * pos;
    // in the camera's depth, not in shadow maps
    #ifdef USE_CLIP_POSITION
        if (Engine.pass != PASS_SHADOW)
            gl_Position = getHookClipPosition(context, gl_Position);
    #endif

    v_projZW = gl_Position.zw;
    #ifdef USE_FRAGMENT_HOOKS
        vec4 worldPosition = depthParams.modelMatrix * pos;
        v_worldPosition = worldPosition.xyz / worldPosition.w;
    #endif
    #ifdef HOOK_FRAGMENT_NORMAL
        v_hookNormal = getHookNormal(boneTransform);
    #endif
    #ifdef HOOK_FRAGMENT_COLOR
        v_hookColor = getHookColor();
    #endif
    #ifdef HOOK_FRAGMENT_INSTANCE
        v_hookInstance = getInstanceCustom();
    #endif
    // No Y flip here: the target keeps its native orientation and the atlas lookup
    // (getShadowAtlasUV) converts the coordinate when sampling.
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}