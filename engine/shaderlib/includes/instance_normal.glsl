#ifndef INSTANCE_NORMAL
#define INSTANCE_NORMAL

// Instance transforms contain rotation and possibly nonuniform scale. Normals need
// the inverse transpose; applying the instance position matrix changes their angle.
vec3 instanceNormal(mat3 m, vec3 n) {
    vec3 c0 = cross(m[1], m[2]);
    vec3 c1 = cross(m[2], m[0]);
    vec3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    if (abs(det) < 0.00000001) return n;
    // Magnitude is discarded by the caller's normalization. Retain the sign for
    // mirrored instances without an expensive matrix inverse.
    return mat3(c0, c1, c2) * n * (det < 0.0 ? -1.0 : 1.0);
}

#endif
