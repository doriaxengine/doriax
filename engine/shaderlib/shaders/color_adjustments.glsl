// Brightness, contrast, saturation, hue and tint
#pragma shader postprocess

#pragma stage fragment
#pragma param float brightness = 1 [0, 2] "Multiplies the shades; 1 keeps them"
#pragma param float contrast = 1 [0, 2] "Spreads the shades away from mid gray; 1 keeps them"
#pragma param float saturation = 1 [0, 2] "0 is gray, above 1 more vivid"
#pragma param float hueShift = 0 [-180, 180] "Turns every hue, in degrees"
#pragma param vec3 tint = 1, 1, 1 color "Multiplies the colors"
#include "effects/color_adjustments.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return colorAdjustments(color, params.brightness, params.contrast, params.saturation, params.hueShift, params.tint);
}
