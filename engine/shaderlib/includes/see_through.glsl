// Dithers a see-through mesh away in front of the scene's target, the same in the color, depth and G-buffer passes
uniform u_fs_seeThrough {
    vec4 target; //world position.xyz, radius.w (0 = off)
    vec4 eye;    //camera position.xyz, 1 - opacity.w
    vec4 params; //margin before the target.x
} seeThrough;

void seeThroughDiscard(vec3 worldPosition){
    float radius = seeThrough.target.w;
    if (radius <= 0.0)
        return;
    vec3 axis = seeThrough.target.xyz - seeThrough.eye.xyz;
    float targetDistance = length(axis);
    if (targetDistance <= radius)
        return;
    axis /= targetDistance;
    vec3 toFragment = worldPosition - seeThrough.eye.xyz;
    float along = dot(toFragment, axis);
    if (along <= 0.0)
        return;
    // starts at the margin before the target, full at twice that
    float margin = max(seeThrough.params.x, 0.01);
    float front = 1.0 - smoothstep(targetDistance - 2.0 * margin, targetDistance - margin, along);
    // a cone, so the circle keeps its size on screen
    float coneRadius = radius * along / targetDistance;
    float inside = 1.0 - smoothstep(0.5 * coneRadius, coneRadius, length(toFragment - axis * along));
    float drop = front * inside * seeThrough.eye.w;
    const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                      3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 cell = ivec2(gl_FragCoord.xy) & 3;
    if (drop > (bayer[cell.y * 4 + cell.x] + 0.5) / 16.0)
        discard;
}
