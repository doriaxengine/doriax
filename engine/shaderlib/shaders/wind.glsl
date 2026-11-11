// Sways the mesh in the scene wind, with gusts that roll downwind across the ground
#pragma shader mesh

#pragma stage vertex
#pragma param float windSway = 0.3 [0, 5] "Bend in 10 m/s of wind, in world units"
#pragma param float windSwayHeight = 0 [0, 50] "Model height of the full bend; 0 moves the whole mesh"
#include "effects/wind.glsl"

vec3 getVertexOffset(VertexContext context){
    return windSway(context.localPosition, context.worldPosition, params.windSway, params.windSwayHeight);
}
