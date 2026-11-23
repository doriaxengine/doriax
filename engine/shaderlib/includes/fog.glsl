uniform u_fs_fog {
    vec4 color_type;
    vec4 density_start_end;
    vec4 eye;              // of the pass; w = 1 for a perspective one
    vec4 forward_skyBlend; // where the pass looks, and how much the fog takes the sky's color
    vec4 skyEnvironment;   // the sky's tint, and its rotation in w (radians)
} fog;

#ifdef FOG_SKY
    // the sky behind the fragment, sharper the thicker the fog, so far things fade into what is behind them
    vec3 getFogSky(float thickness){
        // an orthographic pass looks one way everywhere
        vec3 d = fog.eye.w > 0.5 ? normalize(v_position - fog.eye.xyz) : fog.forward_skyBlend.xyz;
        // below the horizon the map holds the ground: the view mirrored above it, blurrier the steeper it looks down
        float down = max(-d.y, 0.0);
        d = normalize(vec3(d.x, abs(d.y) + 0.05, d.z));
        float c = cos(fog.skyEnvironment.w);
        float s = sin(fog.skyEnvironment.w);
        d = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
        // 7 is the map's blurriest mip
        float lod = mix((1.0 - thickness) * 3.0, 7.0, min(2.0 * down, 1.0));
        return sRGBToLinear(FOG_SKY(d, lod)) * fog.skyEnvironment.rgb;
    }
#endif

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

    #ifdef FOG_SKY
        if (fog.forward_skyBlend.w > 0.0)
            fogColor = mix(fogColor, getFogSky(1.0 - fogFactor), fog.forward_skyBlend.w);
    #endif

    return mix(fogColor, color, fogFactor);
}
