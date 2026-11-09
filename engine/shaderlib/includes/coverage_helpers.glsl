// Shapes for coverage functions, each returning 1 to keep and 0 to remove
#ifndef COVERAGE_HELPERS
#define COVERAGE_HELPERS

// 1 below height, 0 above it, with a soft band of the given width above the cut
float coverageBelowHeight(vec3 worldPosition, float height, float softness){
    if (softness <= 0.0)
        return worldPosition.y <= height ? 1.0 : 0.0;
    return 1.0 - smoothstep(height, height + softness, worldPosition.y);
}

// 0 inside the sphere, 1 outside, with a soft rim of the given width inside its radius
float coverageOutsideSphere(vec3 worldPosition, vec3 center, float radius, float softness){
    float distanceToCenter = length(worldPosition - center);
    return smoothstep(radius - max(softness, 0.0001), radius, distanceToCenter);
}

// a smooth value noise in [0, 1], for dissolves that stay put in world space
float coverageHash(vec3 p){
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float coverageNoise(vec3 p){
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(coverageHash(i + vec3(0, 0, 0)), coverageHash(i + vec3(1, 0, 0)), u.x),
                   mix(coverageHash(i + vec3(0, 1, 0)), coverageHash(i + vec3(1, 1, 0)), u.x), u.y),
               mix(mix(coverageHash(i + vec3(0, 0, 1)), coverageHash(i + vec3(1, 0, 1)), u.x),
                   mix(coverageHash(i + vec3(0, 1, 1)), coverageHash(i + vec3(1, 1, 1)), u.x), u.y), u.z);
}

// opening in front of a target, a cone from a perspective eye or a cylinder along an orthographic
// view, starting margin before the target; opacity is what stays inside
float coverageViewOpening(vec3 worldPosition, vec3 eye, vec3 forward, bool perspective,
                          vec3 target, float radius, float opacity, float margin){
    if (radius <= 0.0)
        return 1.0;
    vec3 toTarget = target - eye;
    float targetDistance = length(toTarget);
    // an eye inside the opening sees nothing to cut
    if (perspective && targetDistance <= radius)
        return 1.0;
    vec3 axis = perspective ? toTarget / targetDistance : forward;
    float spread = perspective ? 1.0 / targetDistance : 0.0;

    vec3 toFragment = worldPosition - target;
    float along = dot(toFragment, axis);
    float coneRadius = radius * (1.0 + along * spread);
    if (coneRadius <= 0.0)
        return 1.0;
    float safeMargin = max(margin, 0.01);
    float front = 1.0 - smoothstep(-2.0 * safeMargin, -safeMargin, along);
    float inside = 1.0 - smoothstep(0.5 * coneRadius, coneRadius, length(toFragment - axis * along));
    return 1.0 - front * inside * (1.0 - clamp(opacity, 0.0, 1.0));
}

// threshold of a 4x4 ordered dither: a coverage below it is removed at that pixel
float coverageDitherThreshold(vec2 pixel){
    const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                      3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 cell = ivec2(pixel) & 3;
    return (15.5 - bayer[cell.y * 4 + cell.x]) / 16.0;
}

#endif
