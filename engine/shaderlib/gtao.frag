#version 450

// Ground truth ambient occlusion (GTAO) from horizons along screen-space slices

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_depthTexture;
uniform sampler u_depth_smp;
uniform texture2D u_gbufferTexture;   // SSR G-buffer view-space normal (oct .rg); used when flags.x > 0.5
uniform sampler u_gbuffer_smp;

uniform u_fs_gtaoParams {
    vec4 params;      // radius, thin occluders, texels per unit at 1, texel y/x in view
    vec4 screen;      // xy = 1 / depth size, zw = depth texels per AO texel
    vec4 ndcToView;   // view xy = (ndc * xy + zw) * distance
    vec4 depthParams; // view z = (x * ndc + y) / (z * ndc + w)
    vec4 flags;       // x = G-buffer normal, y = orthographic
} gtao;

#include "includes/depth_util.glsl"
#include "includes/octahedral.glsl"

#define SLICES 2
#define STEPS 3
#define FALLOFF 0.3 // share of the radius where occluders fade out
#define MAX_SCREEN_RADIUS 0.25 // share of the depth height, keeps taps close
#define FIRST_TAP 1.3 // texels, so no tap lands on the center texel
#define AO_TERM_SCALE 1.5 // slices can pass 1, the blur scales back

const float PI = 3.14159265;
const float HALF_PI = 1.57079633;

float toDistance(float depth01){
    // the cleared background decodes a little past 1.0
    float ndc = min(depth01, 1.0) * 2.0 - 1.0;
    vec4 p = gtao.depthParams;
    return -(p.x * ndc + p.y) / (p.z * ndc + p.w);
}

float viewDistance(vec2 uv){
    return toDistance(decodeDepth(textureLod(sampler2D(u_depthTexture, u_depth_smp), uv, 0.0)));
}

vec3 viewPosition(vec2 uv, float dist){
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, depthUVToNDCY(uv.y));
    return vec3((ndc * gtao.ndcToView.xy + gtao.ndcToView.zw) * mix(dist, 1.0, gtao.flags.y), -dist);
}

// 0 past the depth texture, where nothing is known
float onScreen(vec2 uv){
    vec2 d = abs(uv - 0.5);
    return step(max(d.x, d.y), 0.5);
}

float fastAcos(float x){
    float ax = min(abs(x), 1.0);
    float r = (-0.156583 * ax + HALF_PI) * sqrt(1.0 - ax);
    return x >= 0.0 ? r : PI - r;
}

// 4x4 pattern, as wide as the blur
float bayer(ivec2 p){
    const int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return (float(m[(p.y & 3) * 4 + (p.x & 3)]) + 0.5) / 16.0;
}

void main(){
    // the depth texel under this AO texel, as the blur reads it
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(ivec2((vec2(pixel) + 0.5) * gtao.screen.zw)) + 0.5) * gtao.screen.xy;

    float depth01 = decodeDepth(textureLod(sampler2D(u_depthTexture, u_depth_smp), uv, 0.0));
    if (depth01 >= 1.0){
        frag_color = vec4(vec3(1.0 / AO_TERM_SCALE), 1.0);
        return;
    }
    float dist = toDistance(depth01);
    vec3 P = viewPosition(uv, dist);
    vec3 V = gtao.flags.y > 0.5 ? vec3(0.0, 0.0, 1.0) : normalize(-P);

    vec3 N;
    if (gtao.flags.x > 0.5){
        N = octDecode(textureLod(sampler2D(u_gbufferTexture, u_gbuffer_smp), uv, 0.0).rg);
    }else{
        // nearer neighbour per axis, so edges keep their normal
        vec2 tx = gtao.screen.xy;
        vec3 Pl = viewPosition(uv - vec2(tx.x, 0.0), viewDistance(uv - vec2(tx.x, 0.0)));
        vec3 Pr = viewPosition(uv + vec2(tx.x, 0.0), viewDistance(uv + vec2(tx.x, 0.0)));
        vec3 Pd = viewPosition(uv - vec2(0.0, tx.y), viewDistance(uv - vec2(0.0, tx.y)));
        vec3 Pu = viewPosition(uv + vec2(0.0, tx.y), viewDistance(uv + vec2(0.0, tx.y)));
        vec3 dpdx = (abs(Pr.z - P.z) < abs(P.z - Pl.z)) ? (Pr - P) : (P - Pl);
        vec3 dpdy = (abs(Pu.z - P.z) < abs(P.z - Pd.z)) ? (Pu - P) : (P - Pd);
        N = normalize(cross(dpdx, dpdy));
        // its sign follows the backend's y
        if (dot(N, V) < 0.0) N = -N;
    }

    float radius = gtao.params.x;
    float pixelRadius = radius * gtao.params.z / mix(dist, 1.0, gtao.flags.y);
    // under a texel in every direction
    if (pixelRadius < min(1.0, abs(gtao.params.w))){
        frag_color = vec4(vec3(1.0 / AO_TERM_SCALE), 1.0);
        return;
    }
    float maxRadius = MAX_SCREEN_RADIUS / gtao.screen.y;
    float falloffMul = -1.0 / (FALLOFF * radius);
    float falloffAdd = 1.0 / FALLOFF;

    float noiseSlice = bayer(pixel);
    float noiseStep = bayer(pixel.yx + ivec2(1, 2));
    // depth texture rows run down the view everywhere but GL
    float ySign = (depthUVToNDCY(1.0) - depthUVToNDCY(0.0)) * 0.5;

    // slices even in view, so stretched texels keep the average
    float phi = noiseSlice * (PI / float(SLICES));
    vec2 viewDir = vec2(cos(phi), sin(phi));
    const vec2 turn = vec2(cos(PI / float(SLICES)), sin(PI / float(SLICES)));

    float visibility = 0.0;
    for (int slice = 0; slice < SLICES; slice++){
        vec2 dir = vec2(viewDir.x, viewDir.y / gtao.params.w);
        float sliceRadius = max(min(pixelRadius * length(dir), maxRadius), FIRST_TAP);
        dir = normalize(dir);

        vec3 directionVec = vec3(viewDir.x, viewDir.y * ySign, 0.0);
        vec3 orthoDirectionVec = directionVec - dot(directionVec, V) * V;
        vec3 axisVec = normalize(cross(orthoDirectionVec, V));
        vec3 projectedNormal = N - axisVec * dot(N, axisVec);
        float projectedLength = length(projectedNormal);
        float cosNorm = clamp(dot(projectedNormal, V) / max(projectedLength, 1e-5), 0.0, 1.0);
        float n = sign(dot(orthoDirectionVec, projectedNormal)) * fastAcos(cosNorm);
        float sinN = sin(n);

        // the tangent plane: cos(n + HALF_PI) and cos(n - HALF_PI)
        float lowHorizonCos0 = -sinN;
        float lowHorizonCos1 = sinN;
        float horizonCos0 = lowHorizonCos0;
        float horizonCos1 = lowHorizonCos1;
        for (int step = 0; step < STEPS; step++){
            float s = (float(step) + fract(noiseStep + float(slice + step * STEPS) * 0.618034)) / float(STEPS);
            vec2 offset = round(mix(FIRST_TAP, sliceRadius, s * s) * dir) * gtao.screen.xy;
            vec2 uv0 = uv + offset;
            vec2 uv1 = uv - offset;
            vec3 delta0 = viewPosition(uv0, viewDistance(uv0)) - P;
            vec3 delta1 = viewPosition(uv1, viewDistance(uv1)) - P;
            // occluders in front of the point fade sooner, as thin ones
            float fall0 = length(vec3(delta0.xy, delta0.z + max(delta0.z, 0.0) * gtao.params.y));
            float fall1 = length(vec3(delta1.xy, delta1.z + max(delta1.z, 0.0) * gtao.params.y));
            float weight0 = clamp(fall0 * falloffMul + falloffAdd, 0.0, 1.0) * onScreen(uv0);
            float weight1 = clamp(fall1 * falloffMul + falloffAdd, 0.0, 1.0) * onScreen(uv1);
            horizonCos0 = max(horizonCos0, mix(lowHorizonCos0, dot(delta0, V) / max(length(delta0), 1e-6), weight0));
            horizonCos1 = max(horizonCos1, mix(lowHorizonCos1, dot(delta1, V) / max(length(delta1), 1e-6), weight1));
        }

        float h0 = n + clamp(-fastAcos(horizonCos1) - n, -HALF_PI, HALF_PI);
        float h1 = n + clamp(fastAcos(horizonCos0) - n, -HALF_PI, HALF_PI);
        visibility += projectedLength * (2.0 * cosNorm + 2.0 * (h0 + h1) * sinN - cos(2.0 * h0 - n) - cos(2.0 * h1 - n)) * 0.25;
        // the next slice, without its own sin and cos
        viewDir = vec2(viewDir.x * turn.x - viewDir.y * turn.y, viewDir.x * turn.y + viewDir.y * turn.x);
    }

    // linear: the blur averages the slices, then applies intensity
    frag_color = vec4(vec3(min(max(visibility / float(SLICES), 0.0) / AO_TERM_SCALE, 1.0)), 1.0);
}
