// Moving film grain over the image, fainter in the highlights
#pragma shader postprocess

#pragma stage fragment
#pragma param float amount = 0.08 [0, 1] "How far the grain moves a pixel's brightness"
#pragma param float size = 1.5 [1, 8] "Grain size in pixels"
#pragma param float speed = 24 [0, 60] "New grain patterns per second; 0 keeps one"
#pragma param float highlights = 0.5 [0, 1] "How much less grain the bright parts get"
#include "effects/film_grain.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return filmGrain(context.pixel, color, params.amount, params.size, params.speed, params.highlights);
}
