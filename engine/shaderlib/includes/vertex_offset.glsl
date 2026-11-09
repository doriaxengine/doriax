// Moves a vertex by the mesh's vertexOffsetShader, the same in the color, depth and G-buffer passes
#include "includes/parameters_vs.glsl"

// the mesh's offset file, mapped in by the shader builder
#include "vertex_offset_function.glsl"

vec3 getModelOffset(vec3 localPosition, vec3 worldPosition){
    vec3 offset = getVertexOffset(localPosition, worldPosition);
    float limit = paramsVS.effect_offsetMax;
    if (limit > 0.0 && length(offset) > limit){
        offset = normalize(offset) * limit;
    }

    return mat3(paramsVS.effect_worldToModel) * offset;
}
