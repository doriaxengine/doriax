// Moves a vertex by the mesh's vertexOffsetShader, the same in the color, depth and G-buffer passes
#include "includes/parameters_vs.glsl"

// seconds since the scene started, held while the editor pauses it
float getEffectTime(){
    return effectVS.effect_offset.x;
}

// the mesh's offset file, mapped in by the shader builder
#include "vertex_offset_function.glsl"

vec3 getModelOffset(vec3 localPosition, vec3 worldPosition){
    vec3 offset = getVertexOffset(localPosition, worldPosition);
    float limit = effectVS.effect_offset.y;
    if (limit > 0.0 && length(offset) > limit){
        offset = normalize(offset) * limit;
    }

    return mat3(effectVS.effect_worldToModel) * offset;
}
