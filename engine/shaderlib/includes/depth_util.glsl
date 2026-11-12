#ifndef DEPTH_UTIL
#define DEPTH_UTIL

// Encoding / decoding [0..1] floats into 8 bit/channel RGBA
// Note that 1.0 will not be encoded properly
vec4 encodeDepth(float v) {
    vec4 enc = vec4(1.0, 255.0, 65025.0, 16581375.0) * v;
    enc = fract(enc);
    enc -= enc.yzww * vec4(1.0/255.0,1.0/255.0,1.0/255.0,0.0);
    return enc;
}

float decodeDepth(vec4 rgba) {
    return dot(rgba, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));
}

// Depth / G-buffer targets keep their native orientation, so texture row 0 holds
// ndc.y = -1 on GL and +1 everywhere else. Screen-space passes must convert, or
// the reconstructed view position is mirrored against the G-buffer normal.
float depthUVToNDCY(float v) {
    #ifdef IS_GLSL
        return v * 2.0 - 1.0;
    #else
        return 1.0 - v * 2.0;
    #endif
}

float ndcYToDepthUV(float ndcY) {
    #ifdef IS_GLSL
        return ndcY * 0.5 + 0.5;
    #else
        return 0.5 - ndcY * 0.5;
    #endif
}

#endif
