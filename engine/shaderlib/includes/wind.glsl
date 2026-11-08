// Bends a vertex toward the scene wind, the same in the color and depth passes
uniform u_vs_wind {
    vec4 wind; //full bend in model space.xyz, time.w
    vec4 sway; //height of the full bend.x (0 = whole mesh), world wind direction.yz, air travelled.w
} windParams;

vec3 getWindOffset(vec3 localPosition, vec3 worldPosition){
    float bend = 1.0;
    if (windParams.sway.x > 0.0){
        bend = clamp(localPosition.y / windParams.sway.x, 0.0, 1.0);
        bend *= bend;
    }

    float time = windParams.wind.w;
    float along = dot(worldPosition.xz, windParams.sway.yz);
    float gust = 0.5 + 0.5 * sin((along - windParams.sway.w) * 0.3);
    float wobble = 0.25 * sin(time * 1.9 + dot(worldPosition.xz, vec2(0.37, 0.23)));
    float flutter = 0.1 * sin(time * 9.0 + dot(worldPosition, vec3(4.1, 3.3, 5.7)));

    vec3 offset = windParams.wind.xyz * bend;
    return offset * (0.4 + 0.6 * gust + wobble) + vec3(0.0, flutter * length(offset), 0.0);
}
