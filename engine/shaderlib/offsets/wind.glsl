// Sways the mesh in the scene wind
#include "includes/wind.glsl"

uniform u_vs_offsetParams {
    float windSway;       //bend in 10 m/s of wind, in world units
    float windSwayHeight; //model height of the full bend, 0 moves the whole mesh
} windParams;

vec3 getVertexOffset(vec3 localPosition, vec3 worldPosition){
    return getWindSway(localPosition, worldPosition, windParams.windSway, windParams.windSwayHeight);
}
