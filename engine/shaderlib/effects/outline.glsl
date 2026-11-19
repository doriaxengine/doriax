// Draws lines along silhouettes and creases, found in the scene depth, and along color changes

// 1 / depth runs straight across any flat surface on screen; how far it jumps off that line, relative to the center
float outlineJump(float before, float center, float after){
    return abs(center / before + center / after - 2.0);
}

// how far the slope of 1 / depth turns at the center, as an angle
float outlineTurn(float before, float center, float after, float scale){
    return abs(atan((center / after - 1.0) * scale) - atan((1.0 - center / before) * scale));
}

float outlineLuma(vec2 uv){
    return dot(linearTosRGB(sceneColor(uv).rgb), vec3(0.2126, 0.7152, 0.0722));
}

vec4 outline(vec2 uv, vec4 color, vec3 lineColor, float thickness, float depthSensitivity, float colorSensitivity, float fadeDistance){
    // set for 1080 lines, in whole pixels as the depth is not filtered
    float offset = max(floor(thickness * 0.5 * Engine.resolution.y / 1080.0 + 0.5), 1.0);
    vec2 dx = vec2(offset * Engine.resolution.z, 0.0);
    vec2 dy = vec2(0.0, offset * Engine.resolution.w);

    float center = sceneDepth(uv);
    float left = sceneDepth(uv - dx);
    float right = sceneDepth(uv + dx);
    float up = sceneDepth(uv - dy);
    float down = sceneDepth(uv + dy);

    float edge = 0.0;
    if (depthSensitivity > 0.0){
        // a jump in depth is a silhouette
        float jump = max(outlineJump(left, center, right), outlineJump(up, center, down));
        float jumpThreshold = mix(0.2, 0.01, depthSensitivity);
        // a turn of the surface is a crease; slopes per view height read the same at any resolution
        float scale = Engine.resolution.y / offset;
        float turn = max(outlineTurn(left, center, right, scale), outlineTurn(up, center, down, scale));
        float turnThreshold = mix(1.2, 0.1, depthSensitivity);
        edge = max(smoothstep(jumpThreshold, jumpThreshold * 1.5, jump), smoothstep(turnThreshold, turnThreshold * 1.25, turn));
    }

    if (colorSensitivity > 0.0){
        vec2 gradient = vec2(outlineLuma(uv + dx) - outlineLuma(uv - dx), outlineLuma(uv + dy) - outlineLuma(uv - dy));
        float threshold = mix(0.5, 0.02, colorSensitivity);
        edge = max(edge, smoothstep(threshold, threshold * 2.0, length(gradient)));
    }

    // from the nearest tap, so a line's side over the sky fades with its object
    if (fadeDistance > 0.0){
        float nearest = min(center, min(min(left, right), min(up, down)));
        edge *= 1.0 - smoothstep(fadeDistance * 0.5, fadeDistance, nearest);
    }

    return vec4(sRGBToLinear(mix(linearTosRGB(color.rgb), lineColor, edge)), color.a);
}
