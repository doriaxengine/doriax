// Brightness, contrast, saturation, hue and tint, on display values as an image editor does

vec4 colorAdjustments(vec4 color, float brightness, float contrast, float saturation, float hueShift, vec3 tint){
    vec3 shade = linearTosRGB(color.rgb) * brightness;
    shade = (shade - 0.5) * contrast + 0.5;
    shade = mix(vec3(dot(shade, vec3(0.2126, 0.7152, 0.0722))), shade, saturation);

    // turns the colors around the gray axis
    const vec3 gray = vec3(0.57735027);
    float angle = radians(hueShift);
    shade = shade * cos(angle) + cross(gray, shade) * sin(angle) + gray * dot(gray, shade) * (1.0 - cos(angle));

    return vec4(sRGBToLinear(max(shade * tint, vec3(0.0))), color.a);
}
