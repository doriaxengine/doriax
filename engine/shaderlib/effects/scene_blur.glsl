// The scene behind, averaged over a disc on a golden angle spiral of taps
vec4 sceneBlur(vec2 uv, float radius, int samples){
    // set for 1080 lines, so the blur covers the same share of any view
    float pixels = radius * Engine.resolution.y / 1080.0;
    int taps = clamp(samples, 1, 64);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 64; i++){
        if (i >= taps)
            break;
        float angle = float(i) * 2.39996323;
        vec2 offset = vec2(cos(angle), sin(angle)) * pixels * sqrt((float(i) + 0.5) / float(taps));
        sum += sceneColor(uv + offset * Engine.resolution.zw);
    }
    return sum / float(taps);
}
