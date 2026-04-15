#version 450

#ifndef MATERIAL_UNLIT
    in vec3 v_position;
#endif

#ifdef HAS_NORMALS
#ifdef HAS_TANGENTS
    in mat3 v_tbn;
#else
    in vec3 v_normal;
#endif
#endif

#ifdef HAS_UV_SET1
    in vec2 v_uv1;
#endif

#ifdef HAS_UV_SET2
    in vec2 v_uv2;
#endif

#ifdef HAS_VERTEX_COLOR_VEC3
    in vec3 v_color;
#endif
#if defined(HAS_VERTEX_COLOR_VEC4) || defined(HAS_INSTANCING)
    in vec4 v_color;
#endif

out vec4 g_finalColor;

#if defined(HAS_UV_SET1) || defined(HAS_UV_SET2)
    uniform texture2D u_baseColorTexture;
    uniform sampler u_baseColor_smp;
    #ifndef MATERIAL_UNLIT
        uniform texture2D u_metallicRoughnessTexture;
        uniform texture2D u_occlusionTexture;
        uniform texture2D u_emissiveTexture;
        uniform sampler u_metallicRoughness_smp;
        uniform sampler u_occlusion_smp;
        uniform sampler u_emissive_smp;
        #ifdef HAS_NORMAL_MAP
            uniform texture2D u_normalTexture;
            uniform sampler u_normal_smp;
        #endif
    #endif
#endif

uniform u_fs_pbrParams {
    //Metallic material
    vec4 baseColorFactor;
    #ifndef MATERIAL_UNLIT
        float metallicFactor;
        float roughnessFactor;
        vec3 emissiveFactor;
    #endif
} pbrParams;

#ifdef USE_PUNCTUAL
    uniform u_fs_lighting {
        vec4 direction_range[MAX_LIGHTS]; //direction.xyz and range.w
        vec4 color_intensity[MAX_LIGHTS]; //color.xyz and intensity.w
        vec4 position_type[MAX_LIGHTS]; //position.xyz and type.w
        vec4 inCone_ouCone_shadows_cascades[MAX_LIGHTS]; //innerConeCos.x, outerConeCos.y, shadowMapIndex.z (-1.0 if no shadow), numCascades.w
        vec4 eyePos; //eyePos.xyz
        vec4 cameraDir; //camera backward axis.xyz
        vec4 globalIllum; //globalColor.xyz and globalIntensity.w
    } lighting;
#endif

#ifdef USE_SHADOWS

    uniform u_fs_shadows {
        vec4 bias_texSize_nearFar[MAX_SHADOWSMAP + MAX_SHADOWSCUBEMAP];
    } uShadows;

    in vec4 v_lightProjPos[MAX_SHADOWSMAP];

    #if MAX_SHADOWSMAP >= 1
    uniform texture2D u_shadowMap1;
    uniform sampler u_shadowMap1_smp;
    #endif
    #if MAX_SHADOWSMAP >= 2
    uniform texture2D u_shadowMap2;
    uniform sampler u_shadowMap2_smp;
    #endif
    #if MAX_SHADOWSMAP >= 3
    uniform texture2D u_shadowMap3;
    uniform sampler u_shadowMap3_smp;
    #endif
    #if MAX_SHADOWSMAP >= 4
    uniform texture2D u_shadowMap4;
    uniform sampler u_shadowMap4_smp;
    #endif
    #if MAX_SHADOWSMAP >= 5
    uniform texture2D u_shadowMap5;
    uniform sampler u_shadowMap5_smp;
    #endif
    #if MAX_SHADOWSMAP >= 6
    uniform texture2D u_shadowMap6;
    uniform sampler u_shadowMap6_smp;
    #endif
    #if MAX_SHADOWSMAP >= 7
    uniform texture2D u_shadowMap7;
    uniform sampler u_shadowMap7_smp;
    #endif
    #if MAX_SHADOWSMAP >= 8
    uniform texture2D u_shadowMap8;
    uniform sampler u_shadowMap8_smp;
    #endif

    #if MAX_SHADOWSCUBEMAP >= 1
    uniform textureCube u_shadowCubeMap1;
    uniform sampler u_shadowCubeMap1_smp;
    #endif
    #if MAX_SHADOWSCUBEMAP >= 2
    uniform textureCube u_shadowCubeMap2;
    uniform sampler u_shadowCubeMap2_smp;
    #endif
    #if MAX_SHADOWSCUBEMAP >= 3
    uniform textureCube u_shadowCubeMap3;
    uniform sampler u_shadowCubeMap3_smp;
    #endif
    #if MAX_SHADOWSCUBEMAP >= 4
    uniform textureCube u_shadowCubeMap4;
    uniform sampler u_shadowCubeMap4_smp;
    #endif
#endif

struct MaterialInfo{
    float perceptualRoughness;      // roughness value, as authored by the model creator (input to shader)
    vec3 f0;                        // full reflectance color (n incidence angle)

    float alphaRoughness;           // roughness mapped to a more linear change in the roughness (proposed by [2])
    vec3 albedoColor;

    vec3 f90;                       // reflectance color at grazing angle
    float metallic;

    vec3 n;
    vec3 baseColor; // getBaseColor()

    //float sheenRoughnessFactor;
    //vec3 sheenColorFactor;

    //vec3 clearcoatF0;
    //vec3 clearcoatF90;
    //float clearcoatFactor;
    //vec3 clearcoatNormal;
    //float clearcoatRoughness;

    //float transmissionFactor;
};

struct NormalInfo {
    vec3 ng;   // Geometric normal
    vec3 n;    // Pertubed normal
    vec3 t;    // Pertubed tangent
    vec3 b;    // Pertubed bitangent
};

const float normalScale = 1.0;
const float occlusionStrength = 1.0;

const float M_PI = 3.141592653589793;

#include "includes/pbr.glsl"
#include "includes/brdf.glsl"
#ifdef USE_PUNCTUAL
    #include "includes/punctual.glsl"
#endif
#ifdef USE_SHADOWS
    #include "includes/depth_util.glsl"
    #include "includes/shadows.glsl"
#endif
#ifdef HAS_TERRAIN
    #include "includes/terrain_fs.glsl"
#endif
#ifdef HAS_FOG
    #include "includes/fog.glsl"
#endif

void main() {
    vec4 baseColor = getBaseColor();

    #ifdef HAS_TERRAIN
        baseColor = getTerrainColor(baseColor);
    #endif

    #ifdef MATERIAL_UNLIT
        #ifdef HAS_FOG
            baseColor.rgb = getFogColor(baseColor.rgb);
        #endif
        g_finalColor = (vec4(linearTosRGB(baseColor.rgb), baseColor.a));
        return;
    #endif

    #ifndef MATERIAL_UNLIT
        NormalInfo normalInfo = getNormalInfo();
        vec3 n = normalInfo.n;
        vec3 t = normalInfo.t;
        vec3 b = normalInfo.b;

        MaterialInfo materialInfo = {0.0, vec3(0.0), 0.0, vec3(0.0), vec3(0.0), 0.0, vec3(0.0), vec3(0.0)};
        materialInfo.baseColor = baseColor.rgb;

        // The default index of refraction of 1.5 yields a dielectric normal incidence reflectance of 0.04.
        float ior = 1.5;
        float f0_ior = 0.04;

        materialInfo = getMetallicRoughnessInfo(materialInfo, f0_ior);

        materialInfo.perceptualRoughness = clamp(materialInfo.perceptualRoughness, 0.0, 1.0);
        materialInfo.metallic = clamp(materialInfo.metallic, 0.0, 1.0);

        // Roughness is authored as perceptual roughness; as is convention,
        // convert to material roughness by squaring the perceptual roughness.
        materialInfo.alphaRoughness = materialInfo.perceptualRoughness * materialInfo.perceptualRoughness;

        // Compute reflectance.
        float reflectance = max(max(materialInfo.f0.r, materialInfo.f0.g), materialInfo.f0.b);

        // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
        materialInfo.f90 = vec3(clamp(reflectance * 50.0, 0.0, 1.0));

        materialInfo.n = n;

        // LIGHTING
        vec3 f_specular = vec3(0.0);
        vec3 f_diffuse = vec3(0.0);
        vec3 f_emissive = vec3(0.0);

        #ifdef USE_IBL
            //TODO
        #else
            #ifdef USE_PUNCTUAL
                // simple global illumination
                f_diffuse += lighting.globalIllum.xyz * lighting.globalIllum.w * baseColor.rgb;
            #endif
        #endif

        #ifdef HAS_UV_SET1
            float ao = getOcclusionTexture().r;
            f_diffuse = mix(f_diffuse, f_diffuse * ao, occlusionStrength);
            // apply ambient occlusion too all lighting that is not punctual
            f_specular = mix(f_specular, f_specular * ao, occlusionStrength);
        #endif

        // Apply light sources
        #ifdef USE_PUNCTUAL
            vec3 toEye = lighting.eyePos.xyz - v_position;
            vec3 v = normalize(toEye);

            for (int i = 0; i < MAX_LIGHTS; ++i){

                //Cannot be in function to avoid GLES2 index errors
                //TODO: check this again, removed GLES2 support
                Light light = Light(
                    int(lighting.position_type[i].w),
                    lighting.direction_range[i].xyz,
                    lighting.color_intensity[i].xyz,
                    lighting.position_type[i].xyz,
                    lighting.direction_range[i].w,
                    lighting.color_intensity[i].w,
                    lighting.inCone_ouCone_shadows_cascades[i].x,
                    lighting.inCone_ouCone_shadows_cascades[i].y,
                    (lighting.inCone_ouCone_shadows_cascades[i].z < 0.0)?false:true,
                    int(lighting.inCone_ouCone_shadows_cascades[i].z),
                    int(lighting.inCone_ouCone_shadows_cascades[i].w)
                ); 

                if (light.intensity > 0.0){

                    vec3 pointToLight;
                    if(light.type != LightType_Directional) {
                        pointToLight = light.position - v_position;
                    } else {
                        pointToLight = -light.direction;
                    }

                    vec3 l = normalize(pointToLight);   // Direction from surface point to light
                    vec3 h = normalize(l + v);          // Direction of the vector between l and v, called halfway vector
                    float NdotL = clampedDot(n, l);
                    float NdotV = clampedDot(n, v);
                    float NdotH = clampedDot(n, h);
                    float LdotH = clampedDot(l, h);
                    float VdotH = clampedDot(v, h);

                    float shadow = 1.0;
                    #ifdef USE_SHADOWS
                        if (light.shadows){
                            if(light.type == LightType_Spot){ 
                                shadow = 1.0 - shadowCalculationPCF(light.shadowMapIndex, NdotL);
                            }else if(light.type == LightType_Directional){
                                float viewDepth = dot(lighting.cameraDir.xyz, toEye);
                                shadow = 1.0 - shadowCascadedCalculationPCF(light.shadowMapIndex, light.numShadowCascades, viewDepth, NdotL);
                            }else if(light.type == LightType_Point){
                                shadow = 1.0 - shadowCubeCalculationPCF(light.shadowMapIndex, -pointToLight, NdotL);
                            }
                        }
                    #endif

                    if (NdotL > 0.0 || NdotV > 0.0){
                        // Calculation of analytical light
                        // https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#acknowledgments AppendixB
                        vec3 intensity = getLighIntensity(light, pointToLight);
                        f_diffuse += shadow * intensity * NdotL *  BRDF_lambertian(materialInfo.f0, materialInfo.f90, materialInfo.albedoColor, VdotH);
                        f_specular += shadow * intensity * NdotL * BRDF_specularGGX(materialInfo.f0, materialInfo.f90, materialInfo.alphaRoughness, VdotH, NdotL, NdotV, NdotH);
                    }
                }
            }
        #endif

        f_emissive = pbrParams.emissiveFactor;
        #ifdef HAS_UV_SET1
            f_emissive *= sRGBToLinear(getEmissiveTexture().rgb);
        #endif

        vec3 color = f_emissive + f_diffuse + f_specular;

        #ifdef HAS_FOG
            color.rgb = getFogColor(color.rgb);
        #endif

        g_finalColor = vec4(linearTosRGB(color.rgb), baseColor.a);
    #endif
}
