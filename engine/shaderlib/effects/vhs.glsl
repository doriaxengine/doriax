// A worn VHS tape: wobbling lines, smeared color, snow, a rolling tracking band and dropouts
float vhsHash(vec3 p){
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// smooth noise along one axis
float vhsNoise(float x, float seed){
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(vhsHash(vec3(i, seed, 0.0)), vhsHash(vec3(i + 1.0, seed, 0.0)), f);
}

float vhsLuma(vec3 shown){
    return dot(shown, vec3(0.299, 0.587, 0.114));
}

// the I and Q color axes of NTSC
vec2 vhsChroma(vec3 shown){
    return vec2(dot(shown, vec3(0.596, -0.274, -0.322)), dot(shown, vec3(0.211, -0.523, 0.312)));
}

vec4 vhs(vec2 uv, float distortion, float colorBleed, float noise, float tracking, float dropouts){
    float time = Engine.time;
    float frame = mod(floor(time * 30.0), 4096.0);
    // a tape holds about 240 lines, whatever the view
    float line = floor(uv.y * 240.0);
    // one pixel of a 1080-line view, across
    float unit = Engine.resolution.y * Engine.resolution.z / 1080.0;

    // the lines wobble sideways
    float wobble = (vhsNoise(uv.y * 40.0 + time * 6.0, 1.0) - 0.5) * 0.004 * distortion;
    wobble += sin(uv.y * 3.0 + time * 1.3) * 0.0015 * distortion;

    // a band of torn lines rolls down
    float bandOffset = abs(uv.y - fract(time * 0.07));
    float band = (1.0 - smoothstep(0.0, 0.06, min(bandOffset, 1.0 - bandOffset))) * tracking;
    wobble += band * (vhsHash(vec3(line, frame, 2.0)) - 0.5) * 0.06;

    vec2 at = vec2(uv.x + wobble, uv.y);
    vec4 center = sceneColor(at);

    // soft brightness
    float luma = vhsLuma(linearTosRGB(center.rgb)) * 0.5
        + vhsLuma(linearTosRGB(sceneColor(at - vec2(unit * 1.5, 0.0)).rgb)) * 0.25
        + vhsLuma(linearTosRGB(sceneColor(at + vec2(unit * 1.5, 0.0)).rgb)) * 0.25;

    // color smeared to the right
    vec2 smear = vec2(colorBleed * Engine.resolution.y * Engine.resolution.z * 0.25, 0.0);
    vec2 chroma = vec2(0.0);
    for (int i = 1; i <= 4; i++)
        chroma += vhsChroma(linearTosRGB(sceneColor(at - smear * float(i)).rgb));
    chroma *= 0.25;

    // snow in short streaks, thicker in the band
    float snow = vhsHash(vec3(floor(at.x / (unit * 3.0)), line, frame)) - 0.5;
    luma += snow * (noise * 0.3 + band * 0.5);

    // white dropouts: a streak on a few lines that fades to the right
    float dropout = step(1.0 - dropouts * 0.008, vhsHash(vec3(line, frame, 7.0)));
    float along = (at.x - vhsHash(vec3(line, frame, 8.0))) / (0.05 + 0.25 * vhsHash(vec3(line, frame, 9.0)));
    float streak = step(0.0, along) * step(along, 1.0) * (1.0 - along);
    luma += dropout * streak * (0.4 + 0.6 * vhsHash(vec3(floor(at.x / unit), line, frame)));

    vec3 shown = vec3(luma + 0.956 * chroma.x + 0.621 * chroma.y,
                      luma - 0.272 * chroma.x - 0.647 * chroma.y,
                      luma - 1.106 * chroma.x + 1.703 * chroma.y);
    return vec4(sRGBToLinear(clamp(shown, 0.0, 1.0)), center.a);
}
