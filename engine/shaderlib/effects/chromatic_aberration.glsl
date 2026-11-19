// Red and blue drift apart toward the edges of the view, as through a cheap lens
vec4 chromaticAberration(vec2 uv, vec4 color, float strength, float radial){
    // in shares of the view height: one sideways shift, or one that grows from the center
    vec2 offset = mix(vec2(strength * Engine.resolution.y * Engine.resolution.z, 0.0), (uv - 0.5) * 2.0 * strength, radial);
    color.r = sceneColor(uv + offset).r;
    color.b = sceneColor(uv - offset).b;
    return color;
}
