// Moves a vertex by the getVertexOffset() of the mesh's shader, the same in every pass
#include "includes/parameters_vs.glsl"

struct VertexContext {
    vec3 localPosition;   // after morph, skin and terrain
    vec3 worldPosition;   // before the offset
};

// the vertex stage of the mesh's shader, mapped in by the shader builder
#include "shader_hooks_vs.glsl"

vec3 getModelOffset(vec3 localPosition, vec3 worldPosition){
    VertexContext context;
    context.localPosition = localPosition;
    context.worldPosition = worldPosition;

    vec3 offset = getVertexOffset(context);
    float limit = paramsVS.effect_offsetMax;
    if (limit > 0.0 && length(offset) > limit){
        offset = normalize(offset) * limit;
    }

    return mat3(paramsVS.effect_worldToModel) * offset;
}
