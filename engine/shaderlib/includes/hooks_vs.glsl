// Vertex hooks of the material's shader file, the same in every pass
#include "includes/parameters_vs.glsl"
#include "includes/instance_normal.glsl"
#include "includes/srgb.glsl"

struct VertexContext {
    vec3 localPosition;   // after morph, skin and terrain
    vec3 worldPosition;   // before the offset
    vec3 normal;          // world space; (0, 1, 0) without normals
    vec2 uv1;             // as the mesh stores it, 0 without the set
    vec4 color;           // vertex color times instance color, 1 without either
};

#ifdef HOOK_NORMAL
    // skinned and instanced, without morph targets, so that every pass agrees
    vec3 getHookNormal(mat4 boneTransform){
        vec3 normal = getSkinNormal(a_normal, boneTransform);
        #ifdef HAS_INSTANCING
            normal = instanceNormal(mat3(i_matrix_col1.xyz, i_matrix_col2.xyz, i_matrix_col3.xyz), normal);
        #endif
        return normalize(mat3(HOOK_NORMAL_MATRIX) * normal);
    }
#endif

#ifdef HOOK_COLOR
    vec4 getHookColor(){
        vec4 color = vec4(1.0);
        #if defined(HAS_VERTEX_COLOR_VEC3)
            color.rgb = a_color;
        #elif defined(HAS_VERTEX_COLOR_VEC4)
            color = a_color;
        #endif
        #ifdef HAS_INSTANCING
            color *= i_color;
        #endif
        return color;
    }
#endif

#ifdef USE_VERTEX_HOOKS
    // the vertex stage of the shader file, mapped in by the shader builder
    #include "shader_hooks_vs.glsl"

    VertexContext getVertexContext(vec3 localPosition, vec3 worldPosition, mat4 boneTransform){
        VertexContext context;
        context.localPosition = localPosition;
        context.worldPosition = worldPosition;
        context.normal = vec3(0.0, 1.0, 0.0);
        context.uv1 = vec2(0.0);
        context.color = vec4(1.0);
        #ifdef HOOK_NORMAL
            context.normal = getHookNormal(boneTransform);
        #endif
        #ifdef HOOK_UV1
            context.uv1 = a_texcoord1;
        #endif
        #ifdef HOOK_COLOR
            context.color = getHookColor();
        #endif
        return context;
    }
#endif

#ifdef USE_CLIP_POSITION
    // GL offscreen targets render with y flipped, the hook always sees y up
    vec4 getHookClipPosition(VertexContext context, vec4 clipPosition){
        vec4 flip = vec4(1.0, paramsVS.effect_flipY, 1.0, 1.0);
        return getClipPosition(context, clipPosition * flip) * flip;
    }
#endif

#ifdef USE_VERTEX_OFFSET
    vec3 getModelOffset(VertexContext context){
        vec3 offset = getVertexOffset(context);
        float limit = paramsVS.effect_offsetMax;
        if (limit > 0.0 && length(offset) > limit){
            offset = normalize(offset) * limit;
        }

        return mat3(paramsVS.effect_worldToModel) * offset;
    }
#endif
