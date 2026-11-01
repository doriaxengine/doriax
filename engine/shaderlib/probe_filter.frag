#version 450

// Reflection probe prefilter, one face of one mip level per pass. Downsampling builds a box
// mip chain of the capture; the GGX mode importance samples that chain into one roughness per
// level (level / ENV_GGX_LODS), with N = V = R like IBLEnvironment does for the sky.

in vec2 v_texcoord;
out vec4 frag_color;

uniform textureCube u_probeSourceTexture;
uniform sampler u_probeSource_smp;

uniform u_fs_probeFilterParams {
    vec4 params; // x = roughness, y = cube face, z = samples (taps per side when downsampling), w = 1 to downsample
    vec4 source; // x = destination level size, y = source base size, z = source mip count, w = source level when downsampling
} filterParams;

#include "includes/srgb.glsl"

const float PI = 3.14159265359;
const int MAX_SAMPLES = 128;
const int MAX_TAPS = 16;

// cubemap direction of a face texel, faces ordered +X, -X, +Y, -Y, +Z, -Z
vec3 faceDirection(int face, vec2 st){
    vec2 c = st * 2.0 - 1.0;
    if (face == 0) return vec3(1.0, -c.y, -c.x);
    if (face == 1) return vec3(-1.0, -c.y, c.x);
    if (face == 2) return vec3(c.x, 1.0, c.y);
    if (face == 3) return vec3(c.x, -1.0, -c.y);
    if (face == 4) return vec3(c.x, -c.y, 1.0);
    return vec3(-c.x, -c.y, -1.0);
}

float radicalInverse(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec3 sampleSource(vec3 direction, float lod){
    return sRGBToLinear(textureLod(samplerCube(u_probeSourceTexture, u_probeSource_smp), direction, lod).rgb);
}

vec3 downsample(int face){
    // taps x taps bilinear reads over the footprint of this texel in the source
    int taps = int(filterParams.params.z + 0.5);
    float texel = 1.0 / filterParams.source.x;
    vec3 sum = vec3(0.0);
    for (int j = 0; j < MAX_TAPS; j++){
        if (j >= taps) break;
        for (int i = 0; i < MAX_TAPS; i++){
            if (i >= taps) break;
            vec2 offset = (vec2(float(i), float(j)) + 0.5) / float(taps) - 0.5;
            sum += sampleSource(faceDirection(face, v_texcoord + offset * texel), filterParams.source.w);
        }
    }
    return sum / float(taps * taps);
}

vec3 filterGGX(int face, float roughness){
    vec3 n = normalize(faceDirection(face, v_texcoord));
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);

    int count = int(filterParams.params.z + 0.5);
    float sourceSize = filterParams.source.y;
    float texelSolidAngle = 4.0 * PI / (6.0 * sourceSize * sourceSize);
    float maxLod = filterParams.source.z - 1.0;

    vec3 color = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < MAX_SAMPLES; i++){
        if (i >= count) break;
        vec2 xi = vec2(float(i) / float(count), radicalInverse(uint(i)));
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        vec3 h = normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + n * cosTheta);
        vec3 l = 2.0 * dot(n, h) * h - n;
        float ndotl = dot(n, l);
        if (ndotl > 0.0){
            // with N = V the sample pdf is D / 4; the source mip follows its solid angle
            float d = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
            float pdf = a2 / (PI * d * d) * 0.25 + 0.0001;
            float sampleSolidAngle = 1.0 / (float(count) * pdf);
            float lod = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, maxLod);
            color += sampleSource(l, lod) * ndotl;
            weight += ndotl;
        }
    }
    return color / max(weight, 0.0001);
}

void main(){
    int face = int(filterParams.params.y + 0.5);
    vec3 color;
    if (filterParams.params.w > 0.5){
        color = downsample(face);
    }else if (filterParams.params.x <= 0.0){
        // mirror level: a copy from the source level of the same size
        color = sampleSource(faceDirection(face, v_texcoord), max(0.0, log2(filterParams.source.y / filterParams.source.x)));
    }else{
        color = filterGGX(face, filterParams.params.x);
    }
    frag_color = vec4(linearTosRGB(color), 1.0);
}
