// Brings out fine detail
#pragma shader postprocess

#pragma stage fragment
#pragma param float strength = 0.5 [0, 1] "How much detail is brought out; 0 leaves the image as it is"
#include "effects/sharpen.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return sharpen(context.uv, color, params.strength);
}
