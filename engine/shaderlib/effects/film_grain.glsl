// Moving film grain, fainter in the highlights
float filmGrainHash(vec3 p){
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// smooth noise in cells of size pixels, a new pattern each frame
float filmGrainNoise(vec2 pixel, float size, float frame){
    vec2 cell = (pixel - 0.5) / max(size, 1.0);
    vec2 i = floor(cell);
    vec2 f = fract(cell);
    f = f * f * (3.0 - 2.0 * f);
    float a = filmGrainHash(vec3(i, frame));
    float b = filmGrainHash(vec3(i + vec2(1.0, 0.0), frame));
    float c = filmGrainHash(vec3(i + vec2(0.0, 1.0), frame));
    float d = filmGrainHash(vec3(i + vec2(1.0, 1.0), frame));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec4 filmGrain(vec2 pixel, vec4 color, float amount, float size, float speed, float highlights){
    float frame = mod(floor(Engine.time * speed), 4096.0);
    float grain = filmGrainNoise(pixel, size, frame) - 0.5;
    // the grain sits on the picture as it is shown
    vec3 shown = linearTosRGB(color.rgb);
    float luma = dot(shown, vec3(0.2126, 0.7152, 0.0722));
    shown += grain * 2.0 * amount * (1.0 - highlights * clamp(luma, 0.0, 1.0));
    return vec4(sRGBToLinear(clamp(shown, 0.0, 1.0)), color.a);
}
