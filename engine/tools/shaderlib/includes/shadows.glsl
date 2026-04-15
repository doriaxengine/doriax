struct Shadow{
    float maxBias;
    float minBias;
    vec2 mapSize;
    vec2 nearFar;

    vec4 lightProjPos;
};

Shadow getShadow2DConf(int index){
    for (int i = 0; i < MAX_SHADOWSMAP; i++){
        if (i == index){
            return Shadow(
                uShadows.bias_texSize_nearFar[i].x,
                uShadows.bias_texSize_nearFar[i].x / 10.0,
                uShadows.bias_texSize_nearFar[i].yy,
                uShadows.bias_texSize_nearFar[i].zw,
                v_lightProjPos[i]
            );
        }
    }

    return Shadow(0.0, 0.0, vec2(0.0), vec2(0.0), vec4(0.0));
}

Shadow getShadowCubeConf(int index){
    for (int i = MAX_SHADOWSMAP; i < (MAX_SHADOWSMAP + MAX_SHADOWSCUBEMAP); i++){
        if (i == index){
            return Shadow(
                uShadows.bias_texSize_nearFar[i].x,
                uShadows.bias_texSize_nearFar[i].x / 10.0,
                uShadows.bias_texSize_nearFar[i].yy,
                uShadows.bias_texSize_nearFar[i].zw,
                vec4(0.0)
            );
        }
    }

    return Shadow(0.0, 0.0, vec2(0.0), vec2(0.0), vec4(0.0));
}

vec4 getShadowMap(int index, vec2 coords) {
    if (index == 0){
        #if MAX_SHADOWSMAP >= 1
        return texture(sampler2D(u_shadowMap1, u_shadowMap1_smp), coords);
        #endif
    }else if (index == 1){
        #if MAX_SHADOWSMAP >= 2
        return texture(sampler2D(u_shadowMap2, u_shadowMap2_smp), coords);
        #endif
    }else if (index == 2){
        #if MAX_SHADOWSMAP >= 3
        return texture(sampler2D(u_shadowMap3, u_shadowMap3_smp), coords);
        #endif
    }else if (index == 3){
        #if MAX_SHADOWSMAP >= 4
        return texture(sampler2D(u_shadowMap4, u_shadowMap4_smp), coords);
        #endif
    }else if (index == 4){
        #if MAX_SHADOWSMAP >= 5
        return texture(sampler2D(u_shadowMap5, u_shadowMap5_smp), coords);
        #endif
    }else if (index == 5){
        #if MAX_SHADOWSMAP >= 6
        return texture(sampler2D(u_shadowMap6, u_shadowMap6_smp), coords);
        #endif
    }else if (index == 6){
        #if MAX_SHADOWSMAP >= 7
        return texture(sampler2D(u_shadowMap7, u_shadowMap7_smp), coords);
        #endif
    }else if (index == 7){
        #if MAX_SHADOWSMAP >= 8
        return texture(sampler2D(u_shadowMap8, u_shadowMap8_smp), coords);
        #endif
    }

    return vec4(0.0);
}

vec4 getShadowCubeMap(int index, vec3 coords) {
    index -= MAX_SHADOWSMAP;
    if (index == 0){
        #if MAX_SHADOWSCUBEMAP >= 1
        return texture(samplerCube(u_shadowCubeMap1, u_shadowCubeMap1_smp), coords);
        #endif
    }else if (index == 1){
        #if MAX_SHADOWSCUBEMAP >= 2
        return texture(samplerCube(u_shadowCubeMap2, u_shadowCubeMap2_smp), coords);
        #endif
    }else if (index == 2){
        #if MAX_SHADOWSCUBEMAP >= 3
        return texture(samplerCube(u_shadowCubeMap3, u_shadowCubeMap3_smp), coords);
        #endif
    }else if (index == 3){
        #if MAX_SHADOWSCUBEMAP >= 4
        return texture(samplerCube(u_shadowCubeMap4, u_shadowCubeMap4_smp), coords);
        #endif
    }

    return vec4(0.0);
}

float shadowCompare(int shadowMapIndex, float currentDepth, float bias, vec2 texCoords){
    float closestDepth = decodeDepth(getShadowMap(shadowMapIndex, texCoords));

    return currentDepth - bias > closestDepth  ? 1.0 : 0.0;
}

float shadowCalculationAux(int shadowMapIndex, Shadow shadowConf, float NdotL){
    float shadow = 0.0;

    vec3 proj_coords = shadowConf.lightProjPos.xyz / shadowConf.lightProjPos.w;

    // proj_coords is in [-1,1] range, transform to [0,1] range
    proj_coords = proj_coords * 0.5 + 0.5;
    float currentDepth = proj_coords.z;

    float bias = max(shadowConf.maxBias * (1.0 - NdotL), shadowConf.minBias);

    #ifdef USE_SHADOWS_PCF

        vec2 texel_size = 1.0 / shadowConf.mapSize;
        for(int x = -1; x <= 1; ++x) {
            for(int y = -1; y <= 1; ++y) {
                shadow += shadowCompare(shadowMapIndex, currentDepth, bias, proj_coords.xy + vec2(x, y) * texel_size);
            }
        }
        shadow /= 9.0;

    #else

        shadow = shadowCompare(shadowMapIndex, currentDepth, bias, proj_coords.xy);

    #endif

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
            Shadow shadowConf = getShadow2DConf(casShadowMapIndex);

            if ((viewDepth >= shadowConf.nearFar.x) && (viewDepth <= shadowConf.nearFar.y)){
                return shadowCalculationAux(casShadowMapIndex, shadowConf, NdotL);
            }
        }
    }

    return 0.0;
}

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

    #ifdef USE_SHADOWS_PCF

        //float diskRadius = 0.05;
        //float dp = ( length( fragToLight ) - shadowCameraNear ) / ( shadowCameraFar - shadowCameraNear );
        //float diskRadius = (1.0 + dp) / 4.0;
        float diskRadius = length( fragToLight ) * 0.0005;

        // To reduce iterations
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3( 0.f, 0.f, 0.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3( 1.f, 1.f, 1.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3( 1.f,-1.f, 1.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3(-1.f,-1.f, 1.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3(-1.f, 1.f, 1.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3( 1.f, 1.f,-1.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3( 1.f,-1.f,-1.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3(-1.f,-1.f,-1.f) * diskRadius);
        shadow += shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight + vec3(-1.f, 1.f,-1.f) * diskRadius);
        shadow /= float(9);

    #else

        shadow = shadowCubeCompare(shadowMapIndex, currentDepth, bias, fragToLight);

    #endif

    return shadow;
}