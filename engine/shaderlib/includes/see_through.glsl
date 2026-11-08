// See-through (MeshComponent::seeThrough): a mesh's fragments between the camera and the
// scene's see-through target, inside the cone from the eye to a sphere of `radius` round
// the target, are dithered away, so the target shows through what hides it. The steady
// 4x4 pattern keeps the mesh opaque (no sorting), and the depth pre-pass and G-buffer drop
// the same pixels. Shadow passes send radius 0.

uniform u_fs_seeThrough {
    vec4 target; // world position.xyz, radius.w (0 = off)
    vec4 eye;    // camera position.xyz, share of the pixels dropped at the centre.w
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
    // only in front of the target, fading out over the half radius before it
    float front = 1.0 - smoothstep(targetDistance - radius, targetDistance - 0.5 * radius, along);
    // a cone from the eye keeps the circle round the target one size on screen
    float coneRadius = radius * along / targetDistance;
    float inside = 1.0 - smoothstep(0.5 * coneRadius, coneRadius, length(toFragment - axis * along));
    float drop = front * inside * seeThrough.eye.w;
    if (drop <= 0.0)
        return;
    const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                      3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 cell = ivec2(gl_FragCoord.xy) & 3;
    if (drop > (bayer[cell.y * 4 + cell.x] + 0.5) / 16.0)
        discard;
}
