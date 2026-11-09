// Dissolves the mesh into a noise pattern that stays put in world space
// @param float dissolve = 0 [0, 1] "Share of the mesh that is gone"
// @param float dissolveScale = 4 [0.01, 100] "Noise cells per world unit"
// @param float dissolveEdge = 0.05 [0, 0.5] "Width of the dithered edge"
// @coverage_shadows apply

float getCoverage(CoverageContext context){
    float noise = coverageNoise(context.worldPosition * params.dissolveScale);
    float edge = max(params.dissolveEdge, 0.0001);
    float threshold = params.dissolve * (1.0 + edge) - edge;
    return smoothstep(threshold, threshold + edge, noise);
}
