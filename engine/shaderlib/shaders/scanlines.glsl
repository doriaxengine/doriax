// Dark lines across the image, as on an old screen
#pragma shader postprocess

#pragma stage fragment
#pragma param float lines = 240 [32, 1080] "Lines over the height of the view"
#pragma param float intensity = 0.35 [0, 1] "How dark the gaps between lines get"
#pragma param float scroll = 0 [-10, 10] "Lines per second the pattern moves down"
#include "effects/scanlines.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return scanlines(context.uv, color, params.lines, params.intensity, params.scroll);
}
