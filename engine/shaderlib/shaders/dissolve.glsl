// Dissolves the mesh into a noise pattern that stays put in world space
#pragma shader mesh

#pragma stage fragment
#pragma param float dissolve = 0 [0, 1] "Share of the mesh that is gone"
#pragma param float dissolveScale = 4 [0.01, 100] "Noise cells per world unit"
#pragma param float dissolveEdge = 0.05 [0, 0.5] "Width of the dithered edge"
#pragma coverage_shadows apply
#include "effects/dissolve.glsl"

float getCoverage(FragmentContext context){
    return dissolve(context.worldPosition, params.dissolve, params.dissolveScale, params.dissolveEdge);
}
