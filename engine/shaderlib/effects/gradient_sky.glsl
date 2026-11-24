// A sky of three colors meeting at the horizon, with the sun's disc and glow
vec3 gradientSky(vec3 direction, vec3 sunDirection, vec3 sunColor, vec3 zenith, vec3 horizon, vec3 ground,
        float skyCurve, float groundCurve, float sunSize, float sunGlow, float sunGlowSize){
    vec3 d = normalize(direction);
    // 0 at the horizon, 1 straight up or down
    float elevation = asin(clamp(d.y, -1.0, 1.0)) * 0.63661977;
    vec3 color = elevation >= 0.0 ? mix(horizon, zenith, pow(elevation, skyCurve)) : mix(horizon, ground, pow(-elevation, groundCurve));

    // the angle to the sun, precise near it
    float angle = atan(length(cross(d, sunDirection)), dot(d, sunDirection));
    float radius = radians(0.5 * sunSize);
    float edge = fwidth(angle);
    float disc = sunSize > 0.0 ? 1.0 - smoothstep(radius - edge, radius + edge, angle) : 0.0;
    float glow = sunGlow * exp(-3.0 * angle / radians(max(sunGlowSize, 0.1)));
    // the glow takes the sun's hue, at most as bright as white
    vec3 glowColor = sunColor / max(max(max(sunColor.r, sunColor.g), sunColor.b), 1.0);
    return color + sunColor * disc + glowColor * glow;
}
