// Shows the image in large square pixels, with fewer shades and ordered dithering if asked

// 0, 0.5, 0.75 or 0.25 over a 2x2 cell
float pixelateBayer2(vec2 cell){
    cell = floor(cell);
    return fract(dot(cell, vec2(0.5, cell.y * 0.75)));
}

// a 4x4 ordered dither threshold, centered in 0 to 1
float pixelateBayer(vec2 cell){
    return pixelateBayer2(cell * 0.5) * 0.25 + pixelateBayer2(cell) + 0.03125;
}

vec4 pixelate(vec2 uv, vec4 color, float pixelSize, int colorLevels, float dither){
    // set for 1080 lines, in whole pixels
    float size = max(floor(pixelSize * Engine.resolution.y / 1080.0 + 0.5), 1.0);
    // from the top left, so the blocks line up with the view
    vec2 cell = floor(uv * Engine.resolution.xy / size);
    // the pixel at the middle of the block
    vec2 blockUV = (cell * size + floor(size * 0.5) + 0.5) * Engine.resolution.zw;
    vec4 block = size > 1.0 ? sceneColor(blockUV) : color;
    if (colorLevels < 2)
        return block;

    // in display values, where the shades look evenly spaced
    float steps = float(colorLevels - 1);
    vec3 shade = linearTosRGB(block.rgb) * steps + (pixelateBayer(cell) - 0.5) * dither;
    return vec4(sRGBToLinear(clamp(floor(shade + 0.5) / steps, 0.0, 1.0)), block.a);
}
