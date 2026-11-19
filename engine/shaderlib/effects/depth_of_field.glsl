// Blurs what lies away from a focus distance, as a camera lens does; reads the scene depth

// blur radius in pixels at a view distance
float depthOfFieldRadius(float depth, float focusDistance, float focusRange, float falloff, float maxRadius){
    return maxRadius * clamp((abs(depth - focusDistance) - focusRange) / max(falloff, 0.001), 0.0, 1.0);
}

vec4 depthOfField(vec2 uv, vec4 color, float focusDistance, float focusRange, float falloff, float blur, int quality){
    // set for 1080 lines, so the blur covers the same share of any view
    float maxRadius = blur * Engine.resolution.y / 1080.0;
    if (maxRadius < 0.5)
        return color;

    float centerDepth = sceneDepth(uv);
    float centerRadius = depthOfFieldRadius(centerDepth, focusDistance, focusRange, falloff, maxRadius);
    vec3 sum = color.rgb;
    float count = 1.0;
    int taps = clamp(quality, 1, 64);
    // a golden angle spiral, inner taps first
    for (int i = 0; i < 64; i++){
        if (i >= taps)
            break;
        float tapRadius = maxRadius * sqrt((float(i) + 0.5) / float(taps));
        float angle = float(i) * 2.39996323;
        vec2 tapUV = uv + vec2(cos(angle), sin(angle)) * tapRadius * Engine.resolution.zw;
        float tapDepth = sceneDepth(tapUV);
        float radius = depthOfFieldRadius(tapDepth, focusDistance, focusRange, falloff, maxRadius);
        // a blurred background does not spill over a sharper surface in front of it
        if (tapDepth > centerDepth)
            radius = min(radius, centerRadius * 2.0);
        // a tap whose blur does not reach this pixel keeps the average as it is
        float reach = smoothstep(tapRadius - 0.5, tapRadius + 0.5, radius);
        sum += mix(sum / count, sceneColor(tapUV).rgb, reach);
        count += 1.0;
    }
    return vec4(sum / count, color.a);
}
