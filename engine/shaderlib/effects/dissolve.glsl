// Coverage of a noise pattern that stays put in world space
float dissolve(vec3 worldPosition, float amount, float scale, float edge){
    float noise = coverageNoise(worldPosition * scale);
    edge = max(edge, 0.0001);
    float threshold = amount * (1.0 + edge) - edge;
    return smoothstep(threshold, threshold + edge, noise);
}
