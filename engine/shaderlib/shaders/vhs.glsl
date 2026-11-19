// A worn VHS tape: wobbling lines, smeared color, snow, a rolling tracking band and dropouts
#pragma shader postprocess

#pragma stage fragment
#pragma param float distortion = 0.3 [0, 1] "How far the lines wobble sideways"
#pragma param float colorBleed = 0.004 [0, 0.02] "How far color smears to the right, as a share of the view height"
#pragma param float noise = 0.15 [0, 1] "Snow over the image"
#pragma param float tracking = 0.2 [0, 1] "Strength of the band of torn lines that rolls down"
#pragma param float dropouts = 0.3 [0, 1] "How often white streaks cross a line"
#include "effects/vhs.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return vhs(context.uv, params.distortion, params.colorBleed, params.noise, params.tracking, params.dropouts);
}
