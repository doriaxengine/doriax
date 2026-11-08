// Bends a vertex toward the scene wind, the same in the color and depth passes
uniform u_vs_wind {
    vec4 wind; //air velocity in model space (m/s).xyz, time.w
    vec4 sway; //bend at 10 m/s.x, height of the full bend.y (0 = whole mesh), world wind direction.zw
} windParams;

vec3 getWindOffset(vec3 localPosition, vec3 worldPosition){
    float speed = length(windParams.wind.xyz);
    if (speed < 0.01){
        return vec3(0.0);
    }

    float bend = 1.0;
    if (windParams.sway.y > 0.0){
        bend = clamp(localPosition.y / windParams.sway.y, 0.0, 1.0);
        bend *= bend;
    }

    float time = windParams.wind.w;
    float along = dot(worldPosition.xz, windParams.sway.zw);
    float gust = 0.5 + 0.5 * sin((along - time * speed) * 0.3);
    float wobble = 0.25 * sin(time * 1.9 + dot(worldPosition.xz, vec2(0.37, 0.23)));
    float flutter = 0.1 * sin(time * 9.0 + dot(worldPosition, vec3(4.1, 3.3, 5.7)));

    float amount = windParams.sway.x * speed * 0.1 * bend;
    return windParams.wind.xyz / speed * amount * (0.4 + 0.6 * gust + wobble) + vec3(0.0, flutter * amount, 0.0);
}
