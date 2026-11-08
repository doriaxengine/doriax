// Moves a vertex by the mesh's vertexOffsetShader, the same in the color, depth and G-buffer passes
uniform u_vs_vertexOffset {
    mat4 worldToModel; //turns the world offset into model space
    vec4 wind;         //scene wind in m/s.xyz, meters of air blown past.w
    vec4 params;       //scene time.x, longest offset.y (0 = no limit)
} vertexOffset;

// filled by the shader builder with the project file
#include "vertex_offset_function.glsl"

vec3 getModelOffset(vec3 localPosition, vec3 worldPosition){
    vec3 offset = getVertexOffset(localPosition, worldPosition);
    float limit = vertexOffset.params.y;
    if (limit > 0.0 && length(offset) > limit){
        offset = normalize(offset) * limit;
    }

    return mat3(vertexOffset.worldToModel) * offset;
}
