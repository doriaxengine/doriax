// Brings out fine detail, less where the contrast is already high so edges do not ring, as AMD's CAS does

vec4 sharpen(vec2 uv, vec4 color, float strength){
    vec2 texel = Engine.resolution.zw;
    // in display values, where the eye judges contrast
    vec3 center = linearTosRGB(color.rgb);
    vec3 north = linearTosRGB(sceneColor(uv - vec2(0.0, texel.y)).rgb);
    vec3 south = linearTosRGB(sceneColor(uv + vec2(0.0, texel.y)).rgb);
    vec3 west = linearTosRGB(sceneColor(uv - vec2(texel.x, 0.0)).rgb);
    vec3 east = linearTosRGB(sceneColor(uv + vec2(texel.x, 0.0)).rgb);

    vec3 low = min(center, min(min(north, south), min(west, east)));
    vec3 high = max(center, max(max(north, south), max(west, east)));
    // less where the neighbors already span much of the range
    vec3 amount = sqrt(clamp(min(low, 1.0 - high) / max(high, vec3(0.0001)), 0.0, 1.0));
    vec3 weight = -amount * clamp(strength, 0.0, 1.0) * 0.2;
    vec3 sharpened = (center + (north + south + west + east) * weight) / (1.0 + 4.0 * weight);
    return vec4(sRGBToLinear(clamp(sharpened, 0.0, 1.0)), color.a);
}
