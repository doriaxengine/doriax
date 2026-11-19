// An old CRT screen: a curved tube, scanlines, a phosphor mask, a little color bleed and dark corners

// where a point of the view lands on the curved tube
vec2 crtCurve(vec2 uv, float curvature){
    vec2 centered = uv * 2.0 - 1.0;
    centered *= 1.0 + curvature * (centered.yx * centered.yx);
    return centered * 0.5 + 0.5;
}

vec4 crt(vec2 uv, vec2 pixel, float curvature, float lineShade, float lines, float mask, float bleed, float cornerShade, float brightness){
    vec2 tube = crtCurve(uv, curvature);
    // black around the tube, with an edge one pixel soft
    vec2 edge = min(tube, 1.0 - tube) * Engine.resolution.xy;
    float inside = clamp(min(edge.x, edge.y), 0.0, 1.0);
    if (inside <= 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);

    // red and blue bleed sideways, in shares of the view height
    vec2 shift = vec2(bleed * Engine.resolution.y * Engine.resolution.z, 0.0);
    vec4 center = sceneColor(tube);
    vec3 shown = linearTosRGB(vec3(sceneColor(tube + shift).r, center.g, sceneColor(tube - shift).b));

    // the lines follow the tube; a pixel or two apart they would shimmer
    float wave = 0.5 + 0.5 * cos(tube.y * lines * 6.2831853);
    float visible = smoothstep(1.5, 3.0, Engine.resolution.y / max(lines, 1.0));
    shown *= 1.0 - lineShade * visible * (1.0 - wave);

    // red, green and blue phosphor stripes, wider on big views
    float stripe = mod(floor(pixel.x / max(1.0, floor(Engine.resolution.y / 1080.0 + 0.5))), 3.0);
    vec3 phosphor = vec3(step(stripe, 0.5), step(0.5, stripe) * step(stripe, 1.5), step(1.5, stripe));
    shown *= mix(vec3(1.0), phosphor, mask);

    vec2 corner = tube * (1.0 - tube);
    shown *= pow(clamp(corner.x * corner.y * 16.0, 0.0, 1.0), cornerShade * 0.5);

    shown = clamp(shown * brightness, 0.0, 1.0) * inside;
    return vec4(sRGBToLinear(shown), mix(1.0, center.a, inside));
}
