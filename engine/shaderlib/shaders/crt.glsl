// An old CRT screen: a curved tube, scanlines, a phosphor mask, a little color bleed and dark corners
#pragma shader postprocess

#pragma stage fragment
#pragma param float curvature = 0.15 [0, 0.5] "How much the tube bulges"
#pragma param float scanlines = 0.3 [0, 1] "How dark the gaps between lines get"
#pragma param float lines = 240 [32, 1080] "Lines over the height of the view"
#pragma param float mask = 0.25 [0, 1] "Strength of the red, green and blue phosphor stripes"
#pragma param float bleed = 0.002 [0, 0.02] "Sideways split of red and blue, as a share of the view height"
#pragma param float vignette = 0.3 [0, 1] "How dark the corners get"
#pragma param float brightness = 1.15 [0.5, 2] "Makes up for the light the lines and the mask take"
#include "effects/crt.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return crt(context.uv, context.pixel, params.curvature, params.scanlines, params.lines, params.mask,
        params.bleed, params.vignette, params.brightness);
}
