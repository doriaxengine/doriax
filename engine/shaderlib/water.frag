#version 450

// Declare values as "#pragma param float amount = 0.5 [0, 1]", and read them as
// params.amount once main() has called loadShaderParameters(). Engine.time and
// Engine.resolution (xy size, zw 1 / size) come from the engine.

in vec3 v_position;
in vec3 v_normal;
in float v_waveHeight;

out vec4 g_finalColor;

uniform u_fs_waterParams {
    mat4 viewProjection;           // logical, finds the scene depth under the fragment
    mat4 invViewProjection;        // logical, unprojects the scene depth
    mat4 reflectionViewProjection; // logical, of the planar reflection camera
    vec4 shallowColor;             // rgb (linear), w = depth fade
    vec4 deepColor;                // rgb (linear), w = 1 when the scene depth is valid
    vec4 foamColor;                // rgb (linear), w = shore foam depth
    vec4 ripples;                  // x = 1 / tile size, y = strength, z = wave height, w = crest foam
    vec4 rippleOffsets;            // xy = first layer, zw = second layer (tiles)
    vec4 surface;                  // x = reflectivity, y = specular, z = roughness, w = reflection distortion
    vec4 envColor;                 // rgb = sky tint (linear), w = sky rotation (radians)
    vec4 eyePos;                   // xyz = eye of this pass, w = time (seconds)
    vec4 flags;                    // x = scene lights on, y = IBL ambient available, z = planar reflection, w = eye inside the water
    vec4 refraction;               // x = distortion, y = 1 when this pass has the scene copy
    vec4 refractionRect;           // xy = view origin, zw = view size, in scene copy uv
    vec4 caustics;                 // x = strength, y = 1 / cell size
    vec4 waveScale;                // x = longest wavelength, yz = its direction on world XZ, w = wavelengths its crests have run
    vec4 exclusionCount;           // x = volumes keeping the water out
    vec4 exclusionRows[MAX_WATER_EXCLUSIONS * 3]; // world to 0-1 across the volume, three rows each
    vec4 exclusionShapes[MAX_WATER_EXCLUSIONS];   // x = 0 box, 1 sphere, 2 hull; y = hull map layer
    vec4 wakeArea;                 // xy = world corner of the wake map, z = 1 / its world size, w = 1 when active
    vec4 wakeParams;               // x = wave slope scale, y = 1 / texels across
} water;

// same layout as fs_lighting_t (RenderSystem.h) and mesh.frag
uniform u_fs_lighting {
    vec4 direction_range[MAX_LIGHTS]; //direction.xyz and range.w
    vec4 color_intensity[MAX_LIGHTS]; //color.xyz and intensity.w
    vec4 position_type[MAX_LIGHTS]; //position.xyz and type.w
    vec4 inCone_ouCone_shadows_cascades[MAX_LIGHTS]; //innerConeCos.x, outerConeCos.y, shadowMapIndex.z, numCascades.w
    vec4 spotUp_maskAspect[MAX_LIGHTS]; //spot projection up.xyz; mask width/height in .w (0 = default circle)
    vec4 eyePos; //eyePos.xyz
    vec4 cameraDir; //camera backward axis.xyz
    vec4 globalIllum; //globalColor.xyz and globalIntensity.w
    vec4 envColor; //environment color.rgb (linear) and environment rotation.w (radians)
    vec4 viewportInfo; //1.0/viewportSize.xy in .xy, rendering-flipped flag in .w
} lighting;

uniform texture2D u_waterNormalTexture;
uniform sampler u_waterNormal_smp;

uniform textureCube u_lambertianEnvTexture;
uniform sampler u_lambertianEnv_smp;

uniform texture2D u_spotMaskAtlas;
uniform sampler u_spotMaskAtlas_smp;

uniform textureCube u_skyTexture;
uniform sampler u_sky_smp;

// per hull: r = bottom, g = top (0-1 of its height), b < 0.5 inside its vertical walls
uniform texture2DArray u_waterExclusionMaps;
uniform sampler u_waterExclusionMaps_smp;

// r = foam, g = wave height, repeating: each world texel is at its position modulo the size
uniform texture2D u_waterWakeTexture;
uniform sampler u_waterWake_smp;

#ifdef USE_PLANAR_REFLECTION
    uniform texture2D u_reflectionTexture;
    uniform sampler u_reflection_smp;
#endif

#ifdef USE_SCENE_DEPTH
    uniform texture2D u_depthTexture;
    uniform sampler u_depth_smp;
#endif

#ifdef USE_REFRACTION
    uniform texture2D u_refractionTexture;
    uniform sampler u_refraction_smp;
#endif

#ifdef USE_SHADOWS
    uniform u_fs_shadows {
        vec4 bias_texSize_nearFar[MAX_SHADOW_ATLAS_SLOTS];
        vec4 atlasRect[MAX_SHADOW_ATLAS_SLOTS];
    } uShadows;

    // shadows.glsl projects v_position here, so no per-slot varyings are interpolated
    uniform u_fs_shadow_projection {
        mat4 lightVPMatrix[MAX_SHADOW_ATLAS_SLOTS];
        vec4 shadowParams[MAX_SHADOW_ATLAS_SLOTS]; // normalBias in .x, splitBlend in .y
    } uShadowProjection;
    #define SHADOW_FRAGMENT_PROJECTION
    #define SHADOW_OFFSET_NORMAL v_normal

    uniform u_fs_point_shadows {
        vec4 bias_texSize_nearFar[MAX_POINT_SHADOW_ATLAS_SLOTS];
        vec4 atlasRect[MAX_POINT_SHADOW_ATLAS_SLOTS];
    } uPointShadows;

    uniform texture2D u_shadowAtlas;
    uniform samplerShadow u_shadowAtlas_smp;
    uniform texture2D u_shadowPointAtlas;
    uniform sampler u_shadowPointAtlas_smp;
#endif

#include "includes/parameters_fs.glsl"

const float M_PI = 3.141592653589793;
const float WATER_F0 = 0.02;
const float FOAM_CELLS = 3.0; // per meter
const float CREST_REPEAT = 468.0; // wavelengths before the whitecaps repeat, as in RenderSystem

float clampedDot(vec3 x, vec3 y){
    return clamp(dot(x, y), 0.0, 1.0);
}

#include "includes/srgb.glsl"
#include "includes/brdf.glsl"
#include "includes/punctual.glsl"

#ifdef USE_FRAGMENT_HOOKS
    #include "includes/hooks_water_fs.glsl"
#endif
#if defined(USE_SCENE_DEPTH) || defined(USE_SHADOWS)
    #include "includes/depth_util.glsl"
#endif
#ifdef USE_SHADOWS
    #include "includes/shadows.glsl"
#endif
#ifdef HAS_FOG
    #include "includes/fog.glsl"
#endif

// same sky rotation as ibl.glsl
vec3 rotateEnv(vec3 dir){
    float angle = water.envColor.w;
    float c = cos(angle);
    float s = sin(angle);
    return vec3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);
}

float hash12(vec2 p){
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

// distance to the edge between the two nearest drifting cells
float cellEdge(vec2 p, float phase, float drift){
    vec2 cell = floor(p);
    vec2 f = fract(p);
    float nearest = 8.0;
    float second = 8.0;
    for (int y = -1; y <= 1; y++){
        for (int x = -1; x <= 1; x++){
            vec2 o = vec2(float(x), float(y));
            vec2 seed = vec2(hash12(cell + o), hash12(cell + o + 17.31));
            vec2 d = o + 0.5 + drift * sin(phase + 6.2831 * seed) - f;
            float dist = dot(d, d);
            second = (dist < nearest) ? nearest : min(second, dist);
            nearest = min(nearest, dist);
        }
    }
    return sqrt(second) - sqrt(nearest);
}

// density the foam needs to stay, lowest on the cell edges
float foamPattern(vec2 p, float time){
    p += 0.3 * vec2(valueNoise(p * 0.7 + time * 0.11), valueNoise(p * 0.7 - time * 0.09 + 5.3));
    float inside = clamp(cellEdge(p, time * 0.5, 0.38) * 1.6, 0.0, 1.0);
    float flecks = valueNoise(p * 3.7 + vec2(time * 0.21, -time * 0.13));
    return clamp(0.12 + 0.62 * inside + 0.3 * flecks, 0.0, 1.0);
}

// share of a noise kept, its cells under a pixel would flicker
float noiseDetail(float cellsPerPixel){
    return 1.0 - smoothstep(0.1, 0.45, cellsPerPixel);
}

// value noise repeating every period cells along x
float valueNoiseRepeatX(vec2 p, float period){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    // half a cell in, so a rounded mod never lands on the period
    vec2 x = mod(i.x + vec2(0.5, 1.5), period);
    return mix(mix(hash12(vec2(x.x, i.y)), hash12(vec2(x.y, i.y)), u.x),
               mix(hash12(vec2(x.x, i.y + 1.0)), hash12(vec2(x.y, i.y + 1.0)), u.x), u.y);
}

// q in wavelengths; cells.x in CREST_REPEAT along the wave, cells.y per wavelength across
float crestNoise(vec2 q, vec2 cells, vec2 offset){
    return valueNoiseRepeatX(q * vec2(cells.x / CREST_REPEAT, cells.y) + offset, cells.x);
}

// streaks along the crests of the longest wave, with thinner foam behind;
// under a pixel they lose detail to cover, the foam they average to
float getWhitecaps(vec2 p, float crest, float amount, float footprint, float time, out float detail, out float cover){
    float size = water.waveScale.x;
    vec2 along = water.waveScale.yz;
    vec2 q = vec2(dot(p, along), dot(p, vec2(-along.y, along.x))) / size;
    q.x -= water.waveScale.w;
    float pixel = footprint / size;
    // rougher patches drift slower
    float rough = mix(0.5, crestNoise(q, vec2(520.0, 1.0 / 0.9), vec2(time * 0.08, 0.0)), noiseDetail(pixel / 0.9));
    float level = 0.95 - 0.5 * amount - 0.2 * (rough - 0.5) - 0.2 * (crest - 0.5);
    detail = noiseDetail(pixel / 0.03);
    // fitted to the average of the streaks
    cover = (1.0 - detail) * pow(1.0 - smoothstep(0.1, 0.94, level), 1.25);
    if (detail <= 0.0)
        return 0.0;
    // bend the streaks
    q += 0.05 * vec2(crestNoise(q, vec2(3600.0, 1.0 / 0.13), vec2(0.0)), crestNoise(q, vec2(3600.0, 1.0 / 0.13), vec2(7.3)));
    float ragged = 0.2 * mix(0.5, crestNoise(q, vec2(29640.0, 1.9 / 0.03), vec2(time * 0.3)), noiseDetail(pixel * 1.9 / 0.03));
    float front = 0.8 * crestNoise(q, vec2(15600.0, 1.0 / 0.085), vec2(0.0, time * 0.07)) + ragged;
    float behind = 0.8 * crestNoise(q - vec2(0.02, 0.0), vec2(15600.0, 1.0 / 0.085), vec2(0.0, (time - 0.5) * 0.07)) + ragged;
    float spill = smoothstep(level - 0.07, level + 0.07, front);
    return max(spill, 0.5 * smoothstep(level - 0.07, level + 0.07, behind)) * 0.65;
}

// x = density the foam needs to stay, y = edge width
vec2 getFoamCells(vec2 p, float footprint, float time){
    // cells under a pixel blend into an even cover
    float detail = noiseDetail(footprint * FOAM_CELLS);
    float pattern = mix(0.55, foamPattern(p * FOAM_CELLS, time), detail);
    return vec2(pattern, 0.16 + 0.2 * (1.0 - detail));
}

// foam cover of a density
float getFoam(float density, vec2 cells){
    return smoothstep(cells.x - cells.y, cells.x + cells.y, density * 1.12) * mix(0.45, 0.95, density) * smoothstep(0.0, 0.3, density);
}

// a web of light where drifting cells meet, in [0, 1]
float causticCells(vec2 p, float time){
    return 1.0 - smoothstep(0.0, 0.2, cellEdge(p, time * 0.8, 0.4));
}

// sunlight focused by the waves on the floor, two layers hide the repetition
float getCaustics(vec2 p, float time){
    p += 0.35 * vec2(valueNoise(p * 0.45 + time * 0.07), valueNoise(p * 0.45 - time * 0.05 + 9.1));
    float a = causticCells(p, time);
    float b = causticCells(p * 1.37 + vec2(3.7, 8.1), time * 1.17 + 2.0);
    float web = 0.5 * (a + b);
    return web * sqrt(web) * 2.0;
}

vec3 skyColor(vec3 direction){
    return sRGBToLinear(texture(samplerCube(u_skyTexture, u_sky_smp), rotateEnv(direction)).rgb) * water.envColor.rgb;
}

// per channel: the clearest one of the shallow color fades like the opacity of the
// non-refracting look, the others faster, which tints what is seen through the water
vec3 getExtinction(float depthFade){
    vec3 tint = clamp(water.shallowColor.rgb, vec3(0.001), vec3(0.999));
    return (2.0 / depthFade) * log(tint) / log(max(tint.r, max(tint.g, tint.b)));
}

// the horizontal tilt of the surface as it moves this point on screen, in NDC
// scaled by the distance, so the bend follows the camera and keeps its size
vec2 getScreenTilt(vec3 n, vec4 clip){
    vec4 tilt = water.viewProjection * vec4(n.x, 0.0, n.z, 0.0);
    return tilt.xy - clip.xy / clip.w * tilt.w;
}

// the scene behind the water in the copy taken before it, bent by the surface tilt;
// w is the water crossed by the ray that was used, floorPoint.w = 1 where it hit the scene
vec4 getSceneBehind(vec2 screenTilt, vec4 clip, float through, out vec4 floorPoint){
    floorPoint = vec4(0.0);
#ifdef USE_REFRACTION
    vec2 ndc = clip.xy / clip.w;
    // shallow water bends little, which keeps the shore line in place
    float bend = 2.0 * water.refraction.x * clamp(through / max(water.shallowColor.w, 0.001), 0.0, 1.0);
    vec2 bent = clamp(ndc + screenTilt * bend, -1.0, 1.0);
    #ifdef USE_SCENE_DEPTH
        if (water.deepColor.w > 0.5){
            float depth = decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), vec2(bent.x * 0.5 + 0.5, ndcYToDepthUV(bent.y))));
            vec4 hit = water.invViewProjection * vec4(bent, depth * 2.0 - 1.0, 1.0);
            vec3 hitPosition = hit.xyz / hit.w;
            if (dot(hitPosition - v_position, v_position - water.eyePos.xyz) < 0.0 || hitPosition.y > v_position.y){
                // landed in front of the water or above its surface, the straight ray is used
                bent = ndc;
                depth = decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), vec2(ndc.x * 0.5 + 0.5, ndcYToDepthUV(ndc.y))));
                hit = water.invViewProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);
                hitPosition = hit.xyz / hit.w;
            }else{
                through = (depth < 0.9999) ? length(hitPosition - v_position) : 1.0e4;
            }
            if (depth < 0.9999){
                floorPoint = vec4(hitPosition, 1.0);
            }
        }
    #endif
    vec2 uv = water.refractionRect.xy + vec2(bent.x * 0.5 + 0.5, 0.5 - bent.y * 0.5) * water.refractionRect.zw;
    return vec4(sRGBToLinear(texture(sampler2D(u_refractionTexture, u_refraction_smp), uv).rgb), through);
#else
    return vec4(0.0, 0.0, 0.0, through);
#endif
}

#ifdef USE_SHADOWS
// share of the light the shadow maps let through, as in mesh.frag
float getShadowVisibility(Light light, vec3 pointToLight, float NdotL){
    if (!light.shadows)
        return 1.0;
    if (light.type == LightType_Spot)
        return 1.0 - shadowCalculationPCF(light.shadowMapIndex, NdotL);
    if (light.type == LightType_Directional){
        // cascades are fitted to the main camera
        float viewDepth = dot(lighting.cameraDir.xyz, lighting.eyePos.xyz - v_position);
        return 1.0 - shadowCascadedCalculationPCF(light.shadowMapIndex, light.numShadowCascades, viewDepth, NdotL);
    }
    return 1.0 - shadowCubeCalculationPCF(light.shadowMapIndex, -pointToLight, NdotL);
}
#endif

// two drifting ripple layers, blended in tangent space (z up)
vec3 getRippleNormal(vec2 worldXZ){
    vec2 uv = worldXZ * water.ripples.x;
    vec2 uv2 = mat2(0.8, -0.6, 0.6, 0.8) * uv * 0.71;
    vec3 n1 = texture(sampler2D(u_waterNormalTexture, u_waterNormal_smp), uv + water.rippleOffsets.xy).xyz * 2.0 - 1.0;
    vec3 n2 = texture(sampler2D(u_waterNormalTexture, u_waterNormal_smp), uv2 + water.rippleOffsets.zw).xyz * 2.0 - 1.0;
    return normalize(vec3(n1.xy + n2.xy, n1.z * n2.z));
}

// one level, so it needs no derivatives inside a branch
vec2 sampleWake(vec2 uv){
    return textureLod(sampler2D(u_waterWakeTexture, u_waterWake_smp), uv, 0.0).rg;
}

// foam and wave height, faded at the map edges; slope is the wave tilt along x and z
vec2 getWake(vec2 worldXZ, out vec2 slope){
    slope = vec2(0.0);
    if (water.wakeArea.w < 0.5)
        return vec2(0.0);

    vec2 local = (worldXZ - water.wakeArea.xy) * water.wakeArea.z;
    float inside = smoothstep(0.0, 0.08, min(min(local.x, 1.0 - local.x), min(local.y, 1.0 - local.y)));
    if (inside <= 0.0)
        return vec2(0.0);

    vec2 uv = worldXZ * water.wakeArea.z;
    vec2 texel = vec2(water.wakeParams.y, 0.0);
    float dx = sampleWake(uv + texel.xy).g - sampleWake(uv - texel.xy).g;
    float dz = sampleWake(uv + texel.yx).g - sampleWake(uv - texel.yx).g;
    slope = vec2(dx, dz) * water.wakeParams.x * inside;
    return sampleWake(uv) * inside;
}

bool isExcluded(vec3 position){
    vec4 p = vec4(position, 1.0);
    for (int i = 0; i < int(water.exclusionCount.x); i++){
        vec3 q = vec3(dot(water.exclusionRows[i * 3], p), dot(water.exclusionRows[i * 3 + 1], p), dot(water.exclusionRows[i * 3 + 2], p));
        if (any(lessThan(q, vec3(0.0))) || any(greaterThan(q, vec3(1.0))))
            continue;

        vec4 shape = water.exclusionShapes[i];
        if (shape.x < 0.5)
            return true;
        if (shape.x < 1.5){
            if (dot(q - 0.5, q - 0.5) <= 0.25)
                return true;
        }else{
            vec4 hull = textureLod(sampler2DArray(u_waterExclusionMaps, u_waterExclusionMaps_smp), vec3(q.xz, shape.y), 0.0);
            if (q.y >= hull.r && q.y <= hull.g && hull.b <= 0.5)
                return true;
        }
    }
    return false;
}

void main(){
    loadShaderParameters();

    vec3 toEye = water.eyePos.xyz - v_position;
    float eyeDistance = length(toEye);
    // pixel size on the surface, taken before any branch
    float footprint = length(fwidth(v_position.xz));
    vec3 v = toEye / max(eyeDistance, 0.0001);
    // logical, so screen lookups ignore the viewport origin and the target size
    vec4 clip = water.viewProjection * vec4(v_position, 1.0);

    // waves narrower than a few pixels would only sparkle, the roughness takes them over
    float waveDetail = 1.0 - smoothstep(0.04, 0.35, footprint / max(water.waveScale.x, 0.01));
    vec3 waveNormal = normalize(mix(vec3(0.0, 1.0, 0.0), v_normal, waveDetail));

    vec2 wakeSlope;
    vec2 wake = getWake(v_position.xz, wakeSlope);

    // distant ripples fade, they would only shimmer; wake foam calms them
    vec3 ripple = getRippleNormal(v_position.xz);
    float tileSize = 1.0 / max(water.ripples.x, 0.0001);
    float rippleStrength = water.ripples.y * mix(1.0, 0.3, smoothstep(tileSize * 15.0, tileSize * 120.0, eyeDistance));
    rippleStrength *= 1.0 - 0.6 * clamp(wake.r * 3.0, 0.0, 1.0);
    vec3 n = normalize(waveNormal + vec3(ripple.x, 0.0, ripple.y) * (rippleStrength / max(ripple.z, 0.1)) - vec3(wakeSlope.x, 0.0, wakeSlope.y));

    // not gl_FrontFacing, which flips with the winding of offscreen passes; a grazing eye
    // above the water also sees the backs of waves facing away from it
    bool underwater = dot(v_normal, toEye) < 0.0 && water.eyePos.y < v_position.y;
    if (underwater){
        n = -n;
    }
    float NdotV = clampedDot(n, v);

    // light reaching the water body, without ripples so they do not speckle it
    vec3 ambient;
    if (water.flags.y > 0.5){
        ambient = sRGBToLinear(texture(samplerCube(u_lambertianEnvTexture, u_lambertianEnv_smp), rotateEnv(vec3(0.0, 1.0, 0.0)))).rgb * water.envColor.rgb;
    }else{
        ambient = lighting.globalIllum.rgb * lighting.globalIllum.w;
    }

    vec3 bodyLight = ambient;
    vec3 specular = vec3(0.0);
    vec3 scatter = vec3(0.0);
    float sunlight = 0.0; // directional light on the surface, for the caustics
    float roughness = mix(max(water.surface.z, 0.3), water.surface.z, waveDetail);
    float alphaRoughness = max(roughness * roughness, 0.0005);
    float crest = clamp(v_waveHeight / max(water.ripples.z, 0.001), 0.0, 1.0);

    if (water.flags.x > 0.5){
        for (int i = 0; i < MAX_LIGHTS; ++i){
            Light light = Light(
                int(lighting.position_type[i].w),
                lighting.direction_range[i].xyz,
                lighting.color_intensity[i].xyz,
                lighting.position_type[i].xyz,
                lighting.direction_range[i].w,
                lighting.color_intensity[i].w,
                lighting.inCone_ouCone_shadows_cascades[i].x,
                lighting.inCone_ouCone_shadows_cascades[i].y,
                lighting.spotUp_maskAspect[i].xyz,
                lighting.spotUp_maskAspect[i].w,
                (lighting.inCone_ouCone_shadows_cascades[i].z < 0.0) ? false : true,
                int(lighting.inCone_ouCone_shadows_cascades[i].z),
                int(lighting.inCone_ouCone_shadows_cascades[i].w)
            );

            if (light.intensity <= 0.0){
                continue;
            }

            vec3 pointToLight;
            if (light.type != LightType_Directional){
                pointToLight = light.position - v_position;
            }else{
                pointToLight = -light.direction;
            }

            vec3 l = normalize(pointToLight);
            vec3 intensity = getLighIntensity(light, pointToLight, i);
            #ifdef USE_SHADOWS
                // biased by the wave normal, the ripples would speckle the shadow edge
                intensity *= getShadowVisibility(light, pointToLight, clampedDot(normalize(v_normal), l));
            #endif

            bodyLight += intensity * max(l.y, 0.0) / M_PI;
            if (light.type == LightType_Directional){
                sunlight += dot(intensity, vec3(0.2126, 0.7152, 0.0722)) * max(l.y, 0.0);
            }

            if (!underwater){
                vec3 h = normalize(l + v);
                float NdotL = clampedDot(n, l);
                if (NdotL > 0.0){
                    specular += intensity * NdotL * BRDF_specularGGX(vec3(WATER_F0), vec3(1.0), alphaRoughness,
                        clampedDot(v, h), NdotL, NdotV, clampedDot(n, h));
                }

                // light through the thin crests facing the eye
                scatter += intensity * pow(clampedDot(v, -l), 4.0) * crest * max(l.y, 0.0) / M_PI;
            }
        }
        specular *= water.surface.y;
    }else{
        bodyLight = vec3(1.0);
    }

    // water crossed along the view ray before the opaque scene
    float pathDepth = 1.0e4;
    bool hasDepth = false;
    #ifdef USE_SCENE_DEPTH
        if (water.deepColor.w > 0.5){
            vec2 ndc = clip.xy / clip.w;
            float sceneDepth = decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), vec2(ndc.x * 0.5 + 0.5, ndcYToDepthUV(ndc.y))));
            if (sceneDepth < 0.9999){
                vec4 behind = water.invViewProjection * vec4(ndc, sceneDepth * 2.0 - 1.0, 1.0);
                pathDepth = length(behind.xyz / behind.w - v_position);
            }
            hasDepth = true;
        }
    #endif

    float depthFade = max(water.shallowColor.w, 0.001);
    float deepness;
    float opacity;
    if (hasDepth){
        deepness = 1.0 - exp(-2.5 * pathDepth / depthFade);
        opacity = 1.0 - exp(-2.0 * pathDepth / depthFade);
    }else{
        // without depth, a deep body that clears a little when looked into
        deepness = mix(0.8, 1.0, 1.0 - NdotV);
        opacity = mix(0.85, 1.0, 1.0 - NdotV);
    }
    vec3 body = mix(water.shallowColor.rgb, water.deepColor.rgb, deepness);

    // foam density from the shore, whitecaps and wake
    float density = 0.0;
    if (hasDepth && water.foamColor.w > 0.0){
        float shore = 1.0 - clamp(pathDepth / water.foamColor.w, 0.0, 1.0);
        density = shore * shore;
    }
    density = max(density, min(wake.r * 1.5, 1.0) * smoothstep(0.0, 0.03, wake.r));
    float whitecaps = 0.0;
    float crestDetail = 0.0;
    float crestCover = 0.0;
    if (water.ripples.w > 0.001){
        whitecaps = getWhitecaps(v_position.xz, crest, water.ripples.w, footprint, water.eyePos.w, crestDetail, crestCover);
    }
    float foam = 0.0;
    if (max(density, max(whitecaps, crestCover)) > 0.001){
        vec2 cells = getFoamCells(v_position.xz, footprint, water.eyePos.w);
        // 0.65 is a full streak
        foam = max(getFoam(density, cells), crestDetail * getFoam(whitecaps, cells) + crestCover * getFoam(0.65, cells));
    }

    vec3 color;
    float alpha;
    // the scene seen through the water, and the share of the pixel the water itself fills
    vec3 transmitted = vec3(0.0);
    vec3 coverage = vec3(1.0);

    if (!underwater){
        vec2 screenTilt = getScreenTilt(n, clip);
        vec3 r = reflect(-v, n);
        r.y = abs(r.y); // ripples bend some rays under the horizon
        vec3 reflection = skyColor(r);
        #ifdef USE_PLANAR_REFLECTION
            if (water.flags.z > 0.5){
                vec4 reflectionClip = water.reflectionViewProjection * vec4(v_position, 1.0);
                vec2 reflectionUV = vec2(reflectionClip.x / reflectionClip.w * 0.5 + 0.5,
                                         0.5 - reflectionClip.y / reflectionClip.w * 0.5);
                reflectionUV += vec2(screenTilt.x, -screenTilt.y) * water.surface.w;
                reflectionUV = clamp(reflectionUV, vec2(0.001), vec2(0.999));
                reflection = sRGBToLinear(texture(sampler2D(u_reflectionTexture, u_reflection_smp), reflectionUV).rgb);
            }
        #endif

        float fresnel = WATER_F0 + (1.0 - WATER_F0) * pow(1.0 - NdotV, 5.0);
        fresnel = clamp(fresnel * water.surface.x, 0.0, 1.0);

        if (water.refraction.y > 0.5){
            // the scene behind fades as the water gets deeper
            float through = hasDepth ? pathDepth : depthFade * mix(1.0, 3.0, 1.0 - NdotV);
            vec4 floorPoint;
            vec4 behind = getSceneBehind(screenTilt, clip, through, floorPoint);
            if (floorPoint.w > 0.5 && water.caustics.x > 0.0){
                // brightest in shallow water, gone where the cells would be under a pixel
                float below = v_position.y - floorPoint.y;
                float cellsPerPixel = footprint * length(floorPoint.xyz - water.eyePos.xyz) / max(eyeDistance, 0.001) * water.caustics.y;
                float fade = smoothstep(0.05, 0.6, below) * exp(-below * 0.1) * (1.0 - smoothstep(0.08, 0.35, cellsPerPixel));
                float pattern = getCaustics(floorPoint.xz * water.caustics.y, water.eyePos.w);
                behind.rgb *= 1.0 + water.caustics.x * pattern * fade * clamp(sunlight * 0.5, 0.0, 1.0);
            }
            vec3 transmittance = exp(-getExtinction(depthFade) * behind.w);

            vec3 transmission = transmittance * (1.0 - fresnel) * (1.0 - foam);
            transmitted = behind.rgb * transmission;
            coverage = 1.0 - transmission;

            vec3 waterLight = water.deepColor.rgb * bodyLight * (1.0 - transmittance) * (1.0 - fresnel)
                            + water.shallowColor.rgb * scatter
                            + reflection * fresnel
                            + specular;
            color = mix(waterLight, water.foamColor.rgb * bodyLight, foam);
            alpha = 1.0;
        }else{
            vec3 waterLight = body * bodyLight * opacity * (1.0 - fresnel)
                            + water.shallowColor.rgb * scatter
                            + reflection * fresnel
                            + specular;
            vec3 emitted = mix(waterLight, water.foamColor.rgb * bodyLight, foam);

            float seeThrough = (1.0 - opacity) * (1.0 - fresnel) * (1.0 - foam);
            alpha = 1.0 - seeThrough;
            // raise alpha so blending does not clip bright highlights
            vec3 clampedEmitted = clamp(emitted, 0.0, 1.0);
            alpha = max(alpha, max(clampedEmitted.r, max(clampedEmitted.g, clampedEmitted.b)));
            color = (alpha > 0.0001) ? emitted / alpha : vec3(0.0);
        }
    }else{
        // from below the sky shows only through Snell's window, the rest mirrors the depths
        vec3 below = normalize(mix(-v_normal, n, 0.35));
        float cosView = clampedDot(below, v);
        float window = smoothstep(0.62, 0.72, cosView);
        vec3 refracted = refract(-v, below, 1.33);
        vec3 overhead = (dot(refracted, refracted) > 0.0) ? skyColor(normalize(refracted)) : water.envColor.rgb;
        vec3 depths = water.deepColor.rgb * bodyLight;
        color = mix(depths, overhead * mix(vec3(1.0), water.shallowColor.rgb, 0.4), window);
        color = mix(color, water.foamColor.rgb * bodyLight, foam * 0.3);
        alpha = 1.0;
    }

    if (water.flags.w > 0.5){
        // the eye is in the water, which fades the surface as underwater.frag fades the scene
        vec3 transmittance = exp(-getExtinction(depthFade) * eyeDistance);
        color = (color + transmitted) * transmittance + water.deepColor.rgb * bodyLight * (1.0 - transmittance);
        transmitted = vec3(0.0);
        coverage = vec3(1.0);
    }

    #ifdef HAS_FOG
        // the scene seen through was fogged when it was drawn, the water fogs only its own share
        color = getFogColor(color) - getFogColor(vec3(0.0)) * (1.0 - coverage);
    #endif

    // after the derivatives, which a discarded neighbor would spoil
    if (isExcluded(v_position)){
        discard;
    }

    #ifdef USE_FRAGMENT_HOOKS
        FragmentContext context;
        context.worldPosition = v_position;
        context.normal = n;
        context.view = v;
        context.pixel = gl_FragCoord.xy;
        vec4 hooked = getColor(context, vec4(color + transmitted, alpha));
        g_finalColor = vec4(linearTosRGB(hooked.rgb), hooked.a);
    #else
        g_finalColor = vec4(linearTosRGB(color + transmitted), alpha);
    #endif
}
