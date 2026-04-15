uniform u_fs_fog {
    vec4 color_type;
    vec4 density_start_end;
} fog;


vec3 getFogColor(vec3 color){
    int fogType = int(fog.color_type.w);
    vec3 fogColor = fog.color_type.xyz;
    float fogDensity = fog.density_start_end.x;
    float fogLinearStart = fog.density_start_end.z;
    float fogLinearEnd = fog.density_start_end.w;

    float fogFactor = 0.0;
    const float LOG2 = 1.442695;
    float fogDist = (gl_FragCoord.z / gl_FragCoord.w);
    if (fogType == 0){
        fogFactor = (fogLinearEnd - fogDist)/(fogLinearEnd - fogLinearStart);
    }else if (fogType == 1){
        fogFactor = exp2( -fogDensity * fogDist * LOG2);
    }else if (fogType == 2){
        fogFactor = exp2( -fogDensity * fogDensity * fogDist * fogDist * LOG2);
    }
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    return mix(fogColor, color, fogFactor);
}