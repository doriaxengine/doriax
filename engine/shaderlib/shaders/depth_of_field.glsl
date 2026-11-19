// Blurs what lies nearer or farther than a focus distance, from the scene depth of 3D scenes
#pragma shader postprocess

#pragma stage fragment
#pragma param float focusDistance = 10 [0, 1000] "Distance from the camera that stays sharp, in world units"
#pragma param float focusRange = 5 [0, 1000] "How far before and after the focus distance stays sharp"
#pragma param float falloff = 10 [0.01, 1000] "Distance past the sharp zone where the blur reaches full"
#pragma param float blur = 6 [0, 32] "Largest blur radius, in pixels of a 1080-line view"
#pragma param int quality = 16 [4, 64] "Samples per pixel; more is smoother and slower"
#include "effects/depth_of_field.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return depthOfField(context.uv, color, params.focusDistance, params.focusRange, params.falloff, params.blur, params.quality);
}
