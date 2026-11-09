#version 450

#define DEPTH_SHADER

// Declare values in comments, as "// @param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.

// mvpMatrix is lightVPMatrix * modelMatrix computed once, so a depth prepass matches
// mesh.vert bit for bit; a fork may still multiply the two itself
uniform u_vs_depthParams {
    mat4 modelMatrix;
    mat4 lightVPMatrix;
    mat4 mvpMatrix;
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
#ifdef USE_COVERAGE
    out vec3 v_worldPosition;
#endif

#ifdef HAS_INSTANCING
    in vec4 i_matrix_col1;
    in vec4 i_matrix_col2;
    in vec4 i_matrix_col3;
    in vec4 i_matrix_col4;
#endif

// the alpha mask texture, or the coverage function reading the UV set
#if defined(HAS_TEXTURE) || defined(HAS_UV_SET1)
    in vec2 a_texcoord1;
    out vec2 v_uv1;
#endif

#include "includes/parameters_vs.glsl"
#include "includes/skinning.glsl"
#include "includes/morphtarget.glsl"
#ifdef HAS_TERRAIN
    #include "includes/terrain_vs.glsl"
#endif
#ifdef USE_VERTEX_OFFSET
    #include "includes/vertex_offset.glsl"
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
        mat4 instanceMatrix = mat4(i_matrix_col1, i_matrix_col2, i_matrix_col3, i_matrix_col4);
        vec4 pos = instanceMatrix * localPos;
    #else
        vec4 pos = localPos;
    #endif

    #if defined(HAS_TEXTURE) || defined(HAS_UV_SET1)
        v_uv1 = a_texcoord1;
    #endif

    // before the fade, so a faded instance still shrinks to nothing
    #ifdef USE_VERTEX_OFFSET
        pos.xyz += getModelOffset(localPos.xyz, (depthParams.modelMatrix * pos).xyz);
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

    v_projZW = gl_Position.zw;
    #ifdef USE_COVERAGE
        vec4 worldPosition = depthParams.modelMatrix * pos;
        v_worldPosition = worldPosition.xyz / worldPosition.w;
    #endif
    // No Y flip here: the target keeps its native orientation and the atlas lookup
    // (getShadowAtlasUV) converts the coordinate when sampling.
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}