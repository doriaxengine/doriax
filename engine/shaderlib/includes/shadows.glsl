// The PCF bounds come from a uniform, so every backend clamps them to the range
// the engine can set: a loop the GPU cannot bound is a device hang, not a glitch.
#ifdef IS_HLSL
// HLSL unrolls any loop holding a texture lookup, so the PCF bound has to be a
// constant and its taps multiply with the light and cascade loops around it.
// Capping keeps that compilable: MEDIUM and HIGH quality render as LOW here.
const int MAX_PCF_RADIUS = 1;   // 3x3 kernel
const int MAX_PCF_RINGS = 1;    // center + 1 ring
#else
const int MAX_PCF_RADIUS = 3;   // 7x7 kernel, ShadowQuality::HIGH
const int MAX_PCF_RINGS = 3;    // center + 3 rings, ShadowQuality::HIGH
#endif

struct Shadow{
    float maxBias;
    float minBias;
    vec2 mapSize;
    vec2 nearFar;
    vec4 atlasRect;

    vec4 lightProjPos;
};

#ifdef SHADOW_FRAGMENT_PROJECTION
// affine in position and normal, so projecting the interpolated values is exact
vec4 getLightProjPos(int index){
    vec3 position = v_position;
    #ifdef SHADOW_OFFSET_NORMAL
        position += SHADOW_OFFSET_NORMAL * uShadowProjection.shadowParams[index].x;
    #endif
    return uShadowProjection.lightVPMatrix[index] * vec4(position, 1.0);
}
#else
// shader forks from before the projection moved here still pass one varying per slot
vec4 getLightProjPos(int index){
    for (int i = 0; i < MAX_SHADOW_ATLAS_SLOTS; i++){
        if (i == index){
            return v_lightProjPos[i];
        }
    }
    return vec4(0.0);
}
#endif

Shadow getShadow2DConf(int index){
    return Shadow(
        uShadows.bias_texSize_nearFar[index].x,
        uShadows.bias_texSize_nearFar[index].x / 10.0,
        uShadows.bias_texSize_nearFar[index].yy,
        uShadows.bias_texSize_nearFar[index].zw,
        uShadows.atlasRect[index],
        getLightProjPos(index)
    );
}

#ifndef DIRECTIONAL_LIGHTS_ONLY
Shadow getShadowCubeConf(int baseIndex){
    return Shadow(
        uPointShadows.bias_texSize_nearFar[baseIndex].x,
        uPointShadows.bias_texSize_nearFar[baseIndex].x / 10.0,
        uPointShadows.bias_texSize_nearFar[baseIndex].yy,
        uPointShadows.bias_texSize_nearFar[baseIndex].zw,
        uPointShadows.atlasRect[baseIndex],
        vec4(0.0)
    );
}
#endif

int directionToCubeFace(vec3 dir) {
    vec3 absDir = abs(dir);
    if (absDir.x > absDir.y && absDir.x > absDir.z){
        return dir.x > 0.0 ? 0 : 1;
    }
    if (absDir.y > absDir.z){
        return dir.y > 0.0 ? 2 : 3;
    }
    return dir.z > 0.0 ? 4 : 5;
}

vec2 cubeFaceUV(vec3 dir, int face) {
    vec3 n = normalize(dir);
    vec2 uv;
    if (face == 0){
        uv = vec2(-n.z, -n.y) / abs(n.x);
    }else if (face == 1){
        uv = vec2( n.z, -n.y) / abs(n.x);
    }else if (face == 2){
        uv = vec2( n.x,  n.z) / abs(n.y);
    }else if (face == 3){
        uv = vec2( n.x, -n.z) / abs(n.y);
    }else if (face == 4){
        uv = vec2( n.x, -n.y) / abs(n.z);
    }else{
        uv = vec2(-n.x, -n.y) / abs(n.z);
    }
    return uv * 0.5 + 0.5;
}

vec2 getShadowAtlasUV(vec4 rect, vec2 uv) {
    vec2 atlasUV = rect.xy + uv * rect.zw;
    #ifndef IS_GLSL
        // Slots are placed with sokol's bottom-left viewport convention, but only
        // GL stores and samples render targets bottom-up. The flip is applied to
        // the whole-atlas coordinate, so it relocates the slot as well.
        atlasUV.y = 1.0 - atlasUV.y;
    #endif
    return atlasUV;
}

#ifndef DIRECTIONAL_LIGHTS_ONLY
vec4 getShadowCubeMap(int baseIndex, vec3 coords) {
    vec3 dir = normalize(coords);
    int face = directionToCubeFace(dir);
    vec2 uv = cubeFaceUV(dir, face);
    int slot = baseIndex + face;
    vec4 rect = uPointShadows.atlasRect[slot];
    // half-texel inset keeps the lookup inside this cube-face tile (no cross-face bleed)
    vec2 inset = 0.5 / vec2(uPointShadows.bias_texSize_nearFar[slot].y);
    uv = clamp(uv, inset, 1.0 - inset);
    vec2 atlasUV = getShadowAtlasUV(rect, uv);
    // the atlas has no mips; implicit derivatives in this loop hang the GPU on some GL drivers
    return textureLod(sampler2D(u_shadowPointAtlas, u_shadowPointAtlas_smp), atlasUV, 0.0);
}
#endif

float shadowCompare(int shadowMapIndex, float currentDepth, float bias, vec2 texCoords){
    vec4 rect = uShadows.atlasRect[shadowMapIndex];
    vec2 atlasUV = getShadowAtlasUV(rect, texCoords);
    float visibility = texture(
        sampler2DShadow(u_shadowAtlas, u_shadowAtlas_smp),
        vec3(atlasUV, currentDepth - bias));
    return 1.0 - visibility;
}

float shadowCalculationAux(int shadowMapIndex, Shadow shadowConf, float NdotL){
    float shadow = 0.0;

    vec3 proj_coords = shadowConf.lightProjPos.xyz / shadowConf.lightProjPos.w;

    // proj_coords is in [-1,1] range, transform to [0,1] range
    proj_coords = proj_coords * 0.5 + 0.5;
    float currentDepth = proj_coords.z;

    float bias = max(shadowConf.maxBias * (1.0 - NdotL), shadowConf.minBias);

    // proj_coords.xy is slot-local [0,1]; shadowCompare() applies the atlasRect scale.
    // Clamp every lookup to the slot interior (half-texel inset) so PCF taps near a
    // slot edge can never bleed into a neighbouring atlas tile (another light/cascade).
    vec2 texel_size = 1.0 / shadowConf.mapSize;
    vec2 slotMin = 0.5 * texel_size;
    vec2 slotMax = 1.0 - 0.5 * texel_size;

    // PCF kernel radius from the scene's shadow quality (cameraDir.w), uniform-driven
    // so quality changes need no shader rebuild: 0 = 1 tap, 1 = 3x3, 2 = 5x5, 3 = 7x7
    int pcfRadius = clamp(int(lighting.cameraDir.w), 0, MAX_PCF_RADIUS);

    if (pcfRadius > 0){

        // the (2r+1)^2 box of bilinear taps weighs texels [1-f, 1, .., 1, f] per axis;
        // one bilinear fetch per texel pair, placed by the weights, gives the same result
        vec2 texelPos = proj_coords.xy * shadowConf.mapSize - 0.5;
        vec2 base = floor(texelPos);
        vec2 f = texelPos - base;

        // constant bound: HLSL unrolls any loop holding a texture lookup
        for (int kx = 0; kx <= MAX_PCF_RADIUS; ++kx) {
            if (kx > pcfRadius) break;
            float wx0 = (kx == 0) ? 1.0 - f.x : 1.0;
            float wx1 = (kx == pcfRadius) ? f.x : 1.0;
            float wx = wx0 + wx1;
            float ox = float(-pcfRadius + 2 * kx) + wx1 / wx + 0.5;
            for (int ky = 0; ky <= MAX_PCF_RADIUS; ++ky) {
                if (ky > pcfRadius) break;
                float wy0 = (ky == 0) ? 1.0 - f.y : 1.0;
                float wy1 = (ky == pcfRadius) ? f.y : 1.0;
                float wy = wy0 + wy1;
                float oy = float(-pcfRadius + 2 * ky) + wy1 / wy + 0.5;
                vec2 sampleCoord = clamp((base + vec2(ox, oy)) * texel_size, slotMin, slotMax);
                shadow += wx * wy * shadowCompare(shadowMapIndex, currentDepth, bias, sampleCoord);
            }
        }
        float pcfTaps = float(2 * pcfRadius + 1);
        shadow /= pcfTaps * pcfTaps;

    } else {

        shadow = shadowCompare(shadowMapIndex, currentDepth, bias, clamp(proj_coords.xy, slotMin, slotMax));

    }

    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if(proj_coords.z > 1.0)
        shadow = 0.0;

    return shadow;
}

float shadowCalculationPCF(int shadowMapIndex, float NdotL) {
    Shadow shadowConf = getShadow2DConf(shadowMapIndex);

    return shadowCalculationAux(shadowMapIndex, shadowConf, NdotL);
}

float shadowCascadedCalculationPCF(int shadowMapIndex, int numShadowCascades, float viewDepth, float NdotL) {
    for (int c = 0; c < MAX_SHADOWCASCADES; c++){
        if (c < numShadowCascades){
            int casShadowMapIndex = shadowMapIndex + c;
            // only the chosen cascade is projected
            vec2 nearFar = uShadows.bias_texSize_nearFar[casShadowMapIndex].zw;

            if ((viewDepth >= nearFar.x) && (viewDepth <= nearFar.y)){
                return shadowCalculationAux(casShadowMapIndex, getShadow2DConf(casShadowMapIndex), NdotL);
            }
        }
    }

    return 0.0;
}

#ifndef DIRECTIONAL_LIGHTS_ONLY
// use a calculated near-far, not a real camera near-far
float distanceToDepthValue(vec3 distance, vec2 calcNearFar){
    //https://stackoverflow.com/questions/48654578/omnidirectional-lighting-in-opengl-glsl-4-1
    //https://stackoverflow.com/questions/10786951/omnidirectional-shadow-mapping-with-depth-cubemap    

    vec3 absv = abs(distance);
    float z   = max(absv.x, max(absv.y, absv.z));
    return calcNearFar.x + calcNearFar.y / z;
}

float shadowCubeCompare(int shadowMapIndex, float currentDepth, float bias, vec3 texCoords){
    float closestDepth = decodeDepth(getShadowCubeMap(shadowMapIndex, texCoords));

    if(currentDepth - bias > closestDepth)
        return 1;

    return 0;
}

float shadowCubeCalculationPCF(int shadowMapIndex, vec3 fragToLight, float NdotL) {
    Shadow shadowConf = getShadowCubeConf(shadowMapIndex);

    float currentDepth = distanceToDepthValue(fragToLight, shadowConf.nearFar);

    float shadow = 0.0;

    float bias = max(shadowConf.maxBias * (1.0 - NdotL), shadowConf.minBias);

    // PCF ring count from the scene's shadow quality (cameraDir.w), uniform-driven:
    // 0 = 1 tap, 1 = 9 taps (center + 1 ring), 2 = 17 taps, 3 = 25 taps
    int pcfRings = clamp(int(lighting.cameraDir.w), 0, MAX_PCF_RINGS);

    if (pcfRings > 0){

        const vec3 ringOffsets[8] = vec3[](
            vec3( 1.0, 1.0, 1.0), vec3( 1.0,-1.0, 1.0), vec3(-1.0,-1.0, 1.0), vec3(-1.0, 1.0, 1.0),
            vec3( 1.0, 1.0,-1.0), vec3( 1.0,-1.0,-1.0), vec3(-1.0,-1.0,-1.0), vec3(-1.0, 1.0,-1.0)
        );

        float diskRadius = length( fragToLight ) * 0.0005;

        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight);
        // Constant bound for HLSL, same reasoning as the 2D kernel above
        #ifdef IS_HLSL
        for (int r = 1; r <= MAX_PCF_RINGS; ++r){
        #else
        for (int r = 1; r <= pcfRings; ++r){
        #endif
            float ringRadius = diskRadius * float(r) / float(pcfRings);
            for (int i = 0; i < 8; ++i){
                shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + ringOffsets[i] * ringRadius);
            }
        }
        shadow /= float(1 + 8 * pcfRings);

    } else {

        shadow = shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight);

    }

    return shadow;
}
#endif
