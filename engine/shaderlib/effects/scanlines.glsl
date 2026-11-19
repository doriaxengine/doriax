// Dark lines across the image, as on an old screen
vec4 scanlines(vec2 uv, vec4 color, float lines, float intensity, float scroll){
    float wave = 0.5 + 0.5 * cos((uv.y * lines + Engine.time * scroll) * 6.2831853);
    // lines only a pixel or two apart would shimmer
    float visible = smoothstep(1.5, 3.0, Engine.resolution.y / max(lines, 1.0));
    float shade = 1.0 - intensity * visible * (1.0 - wave);
    return vec4(color.rgb * sRGBToLinear(vec3(shade)), color.a);
}
