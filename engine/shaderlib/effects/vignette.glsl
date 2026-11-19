// Darkens or tints the image toward its corners
vec4 vignette(vec2 uv, vec4 color, float intensity, float radius, float softness, float roundness, vec3 tint){
    // 0 follows the shape of the view, 1 is a circle
    float aspect = mix(1.0, Engine.resolution.x * Engine.resolution.w, roundness);
    vec2 offset = (uv - 0.5) * vec2(aspect, 1.0);
    // 1 at the corners
    float reach = length(offset) / length(vec2(0.5 * aspect, 0.5));
    float amount = smoothstep(radius, radius + max(softness, 0.001), reach) * intensity;
    return vec4(sRGBToLinear(mix(linearTosRGB(color.rgb), tint, amount)), color.a);
}
