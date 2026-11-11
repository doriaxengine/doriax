// Dithers the mesh away where it hides a target from the camera, such as a player behind a
// wall. A script tracks the target into the camera's SeeThrough view parameters.
#pragma shader mesh

#pragma stage fragment
#pragma coverage_shadows keep
#include "effects/see_through.glsl"

float getCoverage(FragmentContext context){
    return seeThrough(context);
}
