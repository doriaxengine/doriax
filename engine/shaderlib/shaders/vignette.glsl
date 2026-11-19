// Darkens or tints the image toward its corners
#pragma shader postprocess

#pragma stage fragment
#pragma param float intensity = 0.4 [0, 1] "Strength at the corners"
#pragma param float radius = 0.5 [0, 1.5] "Where the darkening starts, 1 at the corners"
#pragma param float softness = 0.5 [0.01, 2] "Distance over which it reaches full strength"
#pragma param float roundness = 0 [0, 1] "0 follows the shape of the view, 1 is a circle"
#pragma param vec3 tint = 0, 0, 0 color "Color of the corners"
#include "effects/vignette.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return vignette(context.uv, color, params.intensity, params.radius, params.softness, params.roundness, params.tint);
}
