// The scene wind's bend of a vertex in world units, for vertex offsets
vec3 getWindSway(vec3 localPosition, vec3 worldPosition, float sway, float swayHeight){
    float bend = 1.0;
    if (swayHeight > 0.0){
        bend = clamp(localPosition.y / swayHeight, 0.0, 1.0);
        bend *= bend;
    }

    vec3 air = vertexOffset.wind.xyz;
    float time = vertexOffset.params.x;
    float ground = length(air.xz);
    float along = ground > 0.0 ? dot(worldPosition.xz, air.xz) / ground : 0.0;
    float gust = 0.5 + 0.5 * sin((along - vertexOffset.wind.w) * 0.3);
    float wobble = 0.25 * sin(time * 1.9 + dot(worldPosition.xz, vec2(0.37, 0.23)));
    float flutter = 0.1 * sin(time * 9.0 + dot(worldPosition, vec3(4.1, 3.3, 5.7)));

    vec3 offset = air * (sway * 0.1 * bend);
    return offset * (0.4 + 0.6 * gust + wobble) + vec3(0.0, flutter * length(offset), 0.0);
}
