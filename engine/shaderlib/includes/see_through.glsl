// Dithers a see-through mesh away in front of the scene's target, the same in the color, depth and G-buffer passes
// target: world position.xyz, radius.w (0 = off)
// axis: view direction.xyz, 1 / target distance.w (0 = orthographic)
// params: margin before the target.x, 1 - opacity.y, pixel origin.zw
void seeThroughDiscard(vec3 worldPosition, vec4 target, vec4 axis, vec4 params){
    if (target.w <= 0.0)
        return;
    vec3 toFragment = worldPosition - target.xyz;
    float along = dot(toFragment, axis.xyz);
    // a cone from the eye keeps the circle one size on screen, a cylinder when orthographic
    float coneRadius = target.w * (1.0 + along * axis.w);
    if (coneRadius <= 0.0)
        return;
    // starts at the margin before the target, full at twice that
    float margin = max(params.x, 0.01);
    float front = 1.0 - smoothstep(-2.0 * margin, -margin, along);
    float inside = 1.0 - smoothstep(0.5 * coneRadius, coneRadius, length(toFragment - axis.xyz * along));
    float drop = front * inside * params.y;
    const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                      3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    // counted from the corner the depth passes start at, so every pass drops the same pixels
    ivec2 cell = ivec2(abs(gl_FragCoord.xy - params.zw)) & 3;
    if (drop > (bayer[cell.y * 4 + cell.x] + 0.5) / 16.0)
        discard;
}
