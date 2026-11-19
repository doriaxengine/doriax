// Large square pixels, with fewer shades and dithering if asked
#pragma shader postprocess

#pragma stage fragment
#pragma param float pixelSize = 4 [1, 64] "Block size, in pixels of a 1080-line view"
#pragma param int colorLevels = 0 [0, 64] "Shades per color channel; 0 keeps them all"
#pragma param float dither = 0 [0, 1] "Ordered dithering between the shades"
#include "effects/pixelate.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return pixelate(context.uv, color, params.pixelSize, params.colorLevels, params.dither);
}
