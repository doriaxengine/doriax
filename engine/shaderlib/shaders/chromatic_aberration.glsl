// Splits red and blue apart toward the edges of the view, as through a cheap lens
#pragma shader postprocess

#pragma stage fragment
#pragma param float strength = 0.003 [0, 0.05] "Split at the top and bottom edges, as a share of the view height"
#pragma param float radial = 1 [0, 1] "0 shifts the whole image sideways, 1 grows the split from the center"
#include "effects/chromatic_aberration.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return chromaticAberration(context.uv, color, params.strength, params.radial);
}
