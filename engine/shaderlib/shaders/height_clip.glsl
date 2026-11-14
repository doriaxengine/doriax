// Cuts away everything above a height, such as the upper floors of a building
#pragma shader mesh

#pragma stage fragment
#pragma coverage_shadows apply
#include "effects/height_clip.glsl"

float getCoverage(FragmentContext context){
    return heightClip(context.worldPosition);
}
