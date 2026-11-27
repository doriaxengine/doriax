#version 450

// Ground truth ambient occlusion (GTAO) from horizons along screen-space slices

in vec2 v_texcoord;
out vec4 frag_color;

uniform texture2D u_depthTexture;
uniform sampler u_depth_smp;
uniform texture2D u_gbufferTexture;   // SSR G-buffer view-space normal (oct .rg); used when params.w > 0.5
uniform sampler u_gbuffer_smp;

uniform u_fs_ssaoParams {
    vec4 params;      // radius, intensity, depth texels per AO texel, G-buffer normal
    vec4 screen;      // xy = 1 / depth size, z = texels per unit at 1, w = ortho
    vec4 ndcToView;   // view xy = (ndc * xy + zw) * distance
    vec4 depthParams; // view z = (x * ndc + y) / (z * ndc + w)
} ssao;

#include "includes/depth_util.glsl"
#include "includes/octahedral.glsl"

#define SLICES 2
#define STEPS 3
#define FALLOFF 0.3 // share of the radius where occluders fade out
#define THIN_OCCLUDERS 1.0 // occluders far in front fade sooner, as thin ones
#define AO_TERM_SCALE 1.5 // slices can pass 1, the blur scales back

const float PI = 3.14159265;
const float HALF_PI = 1.57079633;

float toDistance(float depth01){
    // the cleared background decodes a little past 1.0
    float ndc = min(depth01, 1.0) * 2.0 - 1.0;
    vec4 p = ssao.depthParams;
    return -(p.x * ndc + p.y) / (p.z * ndc + p.w);
}

float viewDistance(vec2 uv){
    return toDistance(decodeDepth(textureLod(sampler2D(u_depthTexture, u_depth_smp), uv, 0.0)));
}

vec3 viewPosition(vec2 uv, float dist){
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, depthUVToNDCY(uv.y));
    return vec3((ndc * ssao.ndcToView.xy + ssao.ndcToView.zw) * mix(dist, 1.0, ssao.screen.w), -dist);
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
    // a depth texel center: half size rebuilds what it reads
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(pixel * int(ssao.params.z)) + 0.5) * ssao.screen.xy;

    float depth01 = decodeDepth(textureLod(sampler2D(u_depthTexture, u_depth_smp), uv, 0.0));
    if (depth01 >= 1.0){
        frag_color = vec4(1.0 / AO_TERM_SCALE);
        return;
    }
    float dist = toDistance(depth01);
    vec3 P = viewPosition(uv, dist);
    vec3 V = ssao.screen.w > 0.5 ? vec3(0.0, 0.0, 1.0) : normalize(-P);

    vec3 N;
    if (ssao.params.w > 0.5){
        N = octDecode(texture(sampler2D(u_gbufferTexture, u_gbuffer_smp), uv).rg);
    }else{
        // nearer neighbour per axis, so edges keep their normal
        vec2 tx = ssao.screen.xy;
        vec3 Pl = viewPosition(uv - vec2(tx.x, 0.0), viewDistance(uv - vec2(tx.x, 0.0)));
        vec3 Pr = viewPosition(uv + vec2(tx.x, 0.0), viewDistance(uv + vec2(tx.x, 0.0)));
        vec3 Pd = viewPosition(uv - vec2(0.0, tx.y), viewDistance(uv - vec2(0.0, tx.y)));
        vec3 Pu = viewPosition(uv + vec2(0.0, tx.y), viewDistance(uv + vec2(0.0, tx.y)));
        vec3 dpdx = (abs(Pr.z - P.z) < abs(P.z - Pl.z)) ? (Pr - P) : (P - Pl);
        vec3 dpdy = (abs(Pu.z - P.z) < abs(P.z - Pd.z)) ? (Pu - P) : (P - Pd);
        N = normalize(cross(dpdx, dpdy));
    }
    if (dot(N, V) < 0.0) N = -N;

    float radius = ssao.params.x;
    float screenRadius = radius * ssao.screen.z / mix(dist, 1.0, ssao.screen.w);
    if (screenRadius < 1.0){
        frag_color = vec4(1.0 / AO_TERM_SCALE);
        return;
    }
    float falloffMul = -1.0 / (FALLOFF * radius);
    float falloffAdd = 1.0 / FALLOFF;
    float minS = 1.3 / screenRadius; // first tap off the center texel

    float noiseSlice = bayer(pixel);
    float noiseStep = bayer(pixel.yx + ivec2(1, 2));
    // depth texture rows run down the view everywhere but GL
    float ySign = (depthUVToNDCY(1.0) - depthUVToNDCY(0.0)) * 0.5;

    float visibility = 0.0;
    for (int slice = 0; slice < SLICES; slice++){
        float phi = (float(slice) + noiseSlice) * (PI / float(SLICES));
        vec2 dir = vec2(cos(phi), sin(phi));
        vec3 directionVec = vec3(dir.x, dir.y * ySign, 0.0);
        vec3 orthoDirectionVec = directionVec - dot(directionVec, V) * V;
        vec3 axisVec = normalize(cross(orthoDirectionVec, V));
        vec3 projectedNormal = N - axisVec * dot(N, axisVec);
        float projectedLength = length(projectedNormal);
        float cosNorm = clamp(dot(projectedNormal, V) / max(projectedLength, 1e-5), 0.0, 1.0);
        float n = sign(dot(orthoDirectionVec, projectedNormal)) * fastAcos(cosNorm);

        float lowHorizonCos0 = cos(n + HALF_PI);
        float lowHorizonCos1 = cos(n - HALF_PI);
        float horizonCos0 = lowHorizonCos0;
        float horizonCos1 = lowHorizonCos1;
        for (int step = 0; step < STEPS; step++){
            float s = (float(step) + fract(noiseStep + float(slice + step * STEPS) * 0.618034)) / float(STEPS);
            vec2 offset = round((s * s + minS) * screenRadius * dir) * ssao.screen.xy;
            vec3 delta0 = viewPosition(uv + offset, viewDistance(uv + offset)) - P;
            vec3 delta1 = viewPosition(uv - offset, viewDistance(uv - offset)) - P;
            float len0 = length(delta0);
            float len1 = length(delta1);
            float weight0 = clamp(length(vec3(delta0.xy, delta0.z * (1.0 + THIN_OCCLUDERS))) * falloffMul + falloffAdd, 0.0, 1.0);
            float weight1 = clamp(length(vec3(delta1.xy, delta1.z * (1.0 + THIN_OCCLUDERS))) * falloffMul + falloffAdd, 0.0, 1.0);
            horizonCos0 = max(horizonCos0, mix(lowHorizonCos0, dot(delta0, V) / len0, weight0));
            horizonCos1 = max(horizonCos1, mix(lowHorizonCos1, dot(delta1, V) / len1, weight1));
        }

        float h0 = n + clamp(-fastAcos(horizonCos1) - n, -HALF_PI, HALF_PI);
        float h1 = n + clamp(fastAcos(horizonCos0) - n, -HALF_PI, HALF_PI);
        float sinN = sin(n);
        visibility += projectedLength * (2.0 * cosNorm + 2.0 * (h0 + h1) * sinN - cos(2.0 * h0 - n) - cos(2.0 * h1 - n)) * 0.25;
    }
    visibility = pow(max(visibility / float(SLICES), 0.0), ssao.params.y);

    frag_color = vec4(vec3(min(visibility / AO_TERM_SCALE, 1.0)), 1.0);
}
