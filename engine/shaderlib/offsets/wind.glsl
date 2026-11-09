// Sways the mesh in the scene wind, with gusts that roll downwind across the ground
// @param float windSway = 0.3 [0, 5] "Bend in 10 m/s of wind, in world units"
// @param float windSwayHeight = 0 [0, 50] "Model height of the full bend; 0 moves the whole mesh"
#include "includes/wind.glsl"

vec3 getVertexOffset(vec3 localPosition, vec3 worldPosition){
    return getWindSway(localPosition, worldPosition, params.windSway, params.windSwayHeight);
}
