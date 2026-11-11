// The scene wind's bend of a vertex, in world units
#pragma scene vec3 Wind.velocity "Air velocity in m/s"
#pragma scene float Wind.travel runtime "Meters of air blown past"
vec3 windSway(vec3 localPosition, vec3 worldPosition, float sway, float swayHeight){
    float bend = 1.0;
    if (swayHeight > 0.0){
        bend = clamp(localPosition.y / swayHeight, 0.0, 1.0);
        bend *= bend;
    }

    vec3 air = Wind.velocity;
    float speed = length(air);
    if (speed < 0.001)
        return vec3(0.0);
    vec3 downwind = air / speed;
    vec2 level = length(air.xz) > 0.001 ? normalize(air.xz) : vec2(1.0, 0.0);
    vec3 crosswind = vec3(-level.y, 0.0, level.x);

    // the lean grows faster than the wind
    float strength = speed * 0.1;
    float force = min(strength * sqrt(strength), 3.0);

    // gust fronts roll downwind, broken across it
    float along = dot(worldPosition.xz, level) - Wind.travel;
    float across = dot(worldPosition.xz, crosswind.xz);
    float gust = 0.5 + 0.28 * sin(along * 0.21 + 1.3 * sin(across * 0.13)) + 0.22 * sin(along * 0.57 + across * 0.31 + 2.0);

    // spring, swing and shiver paced by the air blown past
    float spring = sin(Wind.travel * 0.33 + dot(worldPosition.xz, vec2(0.37, 0.23)));
    float swing = sin(Wind.travel * 0.26 + dot(worldPosition.xz, vec2(-0.29, 0.41)));
    float shiver = sin(Wind.travel * 1.7 + dot(worldPosition, vec3(4.1, 3.3, 5.7)));

    float lean = force * (0.3 + 0.7 * gust);
    vec3 offset = downwind * (lean * (1.0 + 0.3 * spring)) + crosswind * (lean * 0.25 * swing);
    offset += (crosswind + vec3(0.0, 1.0, 0.0)) * (0.08 * min(strength, 2.0) * lean * shiver);
    return offset * (sway * bend);
}
