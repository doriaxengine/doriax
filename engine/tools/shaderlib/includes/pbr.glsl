// Based on https://github.com/KhronosGroup/glTF-Sample-Viewer shaders

#include "includes/srgb.glsl"

float clampedDot(vec3 x, vec3 y){
    return clamp(dot(x, y), 0.0, 1.0);
}

vec4 getVertexColor(){
   vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

    #ifdef HAS_VERTEX_COLOR_VEC3
        color.rgb = v_color.rgb;
    #endif
    #if defined(HAS_VERTEX_COLOR_VEC4) || defined(HAS_INSTANCING)
        color = v_color;
    #endif

   return color;
}

vec4 getBaseColor(){
    vec4 baseColor = pbrParams.baseColorFactor;
    #ifdef HAS_UV_SET1
        baseColor *= sRGBToLinear(texture(sampler2D(u_baseColorTexture, u_baseColor_smp), v_uv1));
    #endif
    return baseColor * getVertexColor();
}
#ifndef MATERIAL_UNLIT
    MaterialInfo getMetallicRoughnessInfo(MaterialInfo info, float f0_ior){
        info.metallic = pbrParams.metallicFactor;
        info.perceptualRoughness = pbrParams.roughnessFactor;
        // Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
        // This layout intentionally reserves the 'r' channel for (optional) occlusion map data
        #ifdef HAS_UV_SET1
            vec4 mrSample = texture(sampler2D(u_metallicRoughnessTexture, u_metallicRoughness_smp), v_uv1);
            info.perceptualRoughness *= mrSample.g;
            info.metallic *= mrSample.b;
        #endif
        // Achromatic f0 based on IOR.
        vec3 f0 = vec3(f0_ior);
        info.albedoColor = mix(info.baseColor.rgb * (vec3(1.0) - f0),  vec3(0), info.metallic);
        info.f0 = mix(f0, info.baseColor.rgb, info.metallic);
        return info;
    }

    #ifdef HAS_UV_SET1
        vec4 getOcclusionTexture(){
            return texture(sampler2D(u_occlusionTexture, u_occlusion_smp), v_uv1);
        }

        vec4 getEmissiveTexture(){
            return texture(sampler2D(u_emissiveTexture, u_emissive_smp), v_uv1);
        }
    #endif
#endif

#ifndef MATERIAL_UNLIT
// Get normal, tangent and bitangent vectors.
NormalInfo getNormalInfo(){
    vec3 n, t, b, ng;
    // Compute geometrical TBN:
    #ifdef HAS_TANGENTS
        // Trivial TBN computation, present as vertex attribute.
        // Normalize eigenvectors as matrix is linearly interpolated.
        t = normalize(v_tbn[0]);
        b = normalize(v_tbn[1]);
        ng = normalize(v_tbn[2]);
    #else
        // Normals are either present as vertex attributes or approximated.
        #ifdef HAS_NORMALS
            ng = normalize(v_normal);
        #else
            ng = normalize(cross(dFdx(v_position), dFdy(v_position)));
        #endif
        #ifdef HAS_UV_SET1
            vec3 uv_dx = dFdx(vec3(v_uv1, 0.0));
            vec3 uv_dy = dFdy(vec3(v_uv1, 0.0));
            vec3 t_ = (uv_dy.t * dFdx(v_position) - uv_dx.t * dFdy(v_position)) /
                (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);

            t = normalize(t_ - ng * dot(ng, t_));
            b = cross(ng, t);
        #endif
    #endif

    // Compute pertubed normals:
    #ifdef HAS_NORMAL_MAP
        n = texture(sampler2D(u_normalTexture, u_normal_smp), v_uv1).rgb * 2.0 - vec3(1.0);
        n *= vec3(normalScale, normalScale, 1.0);
        n = mat3(t, b, ng) * normalize(n);
    #else
        n = ng;
    #endif
    NormalInfo info;
    info.ng = ng;
    info.t = t;
    info.b = b;
    info.n = n;
    return info;
}
#endif