#version 450

// Declare values in comments, as "// @param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.
//
// sumWaterWaves in RenderSystem.cpp repeats this wave sum for height queries: change both.

uniform u_vs_waterParams {
    mat4 modelMatrix;
    mat4 viewProjectionMatrix; // Y-flipped for offscreen passes on GL
    vec4 waves[4];             // xy = direction on world XZ, z = amplitude, w = wavelength
    vec4 phases;               // phase of each wave (radians)
    vec4 params;               // x = crest steepness
} waterParams;

in vec3 a_position;

// in the same order as water.frag, which matches them by location
out vec3 v_position;
out vec3 v_normal;
out float v_waveHeight;

#include "includes/parameters_vs.glsl"

const float TWO_PI = 6.283185307179586;

// Gerstner wave at the world point p, adding up the surface derivatives
vec3 gerstnerWave(vec4 wave, float phase, vec2 p, float steepness, inout vec3 tangent, inout vec3 binormal){
    vec2 d = wave.xy;
    float a = wave.z;
    float k = TWO_PI / wave.w;
    float f = k * dot(d, p) - phase;
    float s = sin(f);
    float c = cos(f);
    float qa = steepness * a;

    tangent += vec3(-qa * k * d.x * d.x * s, a * k * d.x * c, -qa * k * d.x * d.y * s);
    binormal += vec3(-qa * k * d.x * d.y * s, a * k * d.y * c, -qa * k * d.y * d.y * s);

    return vec3(qa * d.x * c, a * s, qa * d.y * c);
}

void main(){
    loadShaderParameters();

    vec4 worldPos = waterParams.modelMatrix * vec4(a_position, 1.0);
    worldPos.y = waterParams.modelMatrix[3].y; // level even when the entity is tilted
    vec2 p = worldPos.xz;

    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 0.0, 1.0);
    // unrolled, GL targets cannot index the phases vector
    float steepness = waterParams.params.x;
    vec3 offset = gerstnerWave(waterParams.waves[0], waterParams.phases.x, p, steepness, tangent, binormal);
    offset += gerstnerWave(waterParams.waves[1], waterParams.phases.y, p, steepness, tangent, binormal);
    offset += gerstnerWave(waterParams.waves[2], waterParams.phases.z, p, steepness, tangent, binormal);
    offset += gerstnerWave(waterParams.waves[3], waterParams.phases.w, p, steepness, tangent, binormal);

    worldPos.xyz += offset;

    v_position = worldPos.xyz;
    v_normal = normalize(cross(binormal, tangent));
    v_waveHeight = offset.y;

    gl_Position = waterParams.viewProjectionMatrix * worldPos;
    #ifdef IS_VULKAN
        // GL [-1,1] to Vulkan [0,1] depth range (spirv-cross fixup_clipspace equivalent)
        gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
    #endif
}
