uniform u_fs_fog {
    vec4 color_type;
    vec4 density_start_end;
    vec4 eye_skyBlend;   // the eye of the pass, and how much the fog takes the sky's color
    vec4 skyEnvironment; // the sky's tint, and its rotation in w (radians)
} fog;

// the lit variants already read the sky's prefiltered map
#ifndef USE_IBL
    uniform textureCube u_GGXEnvTexture;
    uniform sampler u_GGXEnv_smp;
#endif

// the sky behind position, sharper the thicker the fog, so far things fade into what is behind them
vec3 getFogSky(vec3 position, float thickness){
    vec3 d = normalize(position - fog.eye_skyBlend.xyz);
    // the air glows as the horizon does, never as the ground below it
    d = normalize(vec3(d.x, max(d.y, 0.05), d.z));
    float c = cos(fog.skyEnvironment.w);
    float s = sin(fog.skyEnvironment.w);
    d = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    vec3 sky = sRGBToLinear(textureLod(samplerCube(u_GGXEnvTexture, u_GGXEnv_smp), d, (1.0 - thickness) * 3.0).rgb);
    return sky * fog.skyEnvironment.rgb;
}

vec3 getFogColor(vec3 color, vec3 position){
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

    if (fog.eye_skyBlend.w > 0.0)
        fogColor = mix(fogColor, getFogSky(position, 1.0 - fogFactor), fog.eye_skyBlend.w);

    return mix(fogColor, color, fogFactor);
}
