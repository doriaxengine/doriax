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

// Per-texture UV set selection (glTF texCoord). When a second UV set is present
// the renderer supplies u_fs_texCoordSets telling each texture which set to sample
// (0 = v_uv1, 1 = v_uv2). Without a second set everything samples v_uv1.
#ifdef HAS_UV_SET2
    uniform u_fs_texCoordSets {
        ivec4 set0; // x=baseColor, y=metallicRoughness, z=occlusion, w=emissive
        ivec4 set1; // x=normal
    } texCoordSets;

    // set1.w is always 0; reading it keeps set1 active when no normal map uses it
    vec2 selectUV(int set)        { return (set + texCoordSets.set1.w == 1) ? v_uv2 : v_uv1; }
    vec2 getBaseColorUV()         { return selectUV(texCoordSets.set0.x); }
    vec2 getMetallicRoughnessUV() { return selectUV(texCoordSets.set0.y); }
    vec2 getOcclusionUV()         { return selectUV(texCoordSets.set0.z); }
    vec2 getEmissiveUV()          { return selectUV(texCoordSets.set0.w); }
    vec2 getNormalUV()            { return selectUV(texCoordSets.set1.x); }
#elif defined(HAS_UV_SET1)
    vec2 getBaseColorUV()         { return v_uv1; }
    vec2 getMetallicRoughnessUV() { return v_uv1; }
    vec2 getOcclusionUV()         { return v_uv1; }
    vec2 getEmissiveUV()          { return v_uv1; }
    vec2 getNormalUV()            { return v_uv1; }
#endif

vec4 getBaseColor(){
    vec4 baseColor = pbrParams.baseColorFactor;
    #if defined(HAS_UV_SET1) || defined(HAS_UV_SET2)
        baseColor *= sRGBToLinear(texture(sampler2D(u_baseColorTexture, u_baseColor_smp), getBaseColorUV()));
    #endif
    return baseColor * getVertexColor();
}
#ifndef MATERIAL_UNLIT
    MaterialInfo getMetallicRoughnessInfo(MaterialInfo info, float f0_ior){
        info.metallic = pbrParams.metallicFactor;
        info.perceptualRoughness = pbrParams.roughnessFactor;
        // Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
        // This layout intentionally reserves the 'r' channel for (optional) occlusion map data
        #if defined(HAS_UV_SET1) || defined(HAS_UV_SET2)
            vec4 mrSample = texture(sampler2D(u_metallicRoughnessTexture, u_metallicRoughness_smp), getMetallicRoughnessUV());
            info.perceptualRoughness *= mrSample.g;
            info.metallic *= mrSample.b;
        #endif
        // Achromatic f0 based on IOR.
        vec3 f0 = vec3(f0_ior);
        info.albedoColor = mix(info.baseColor.rgb * (vec3(1.0) - f0),  vec3(0), info.metallic);
        info.f0 = mix(f0, info.baseColor.rgb, info.metallic);
        return info;
    }

    #if defined(HAS_UV_SET1) || defined(HAS_UV_SET2)
        vec4 getOcclusionTexture(){
            return texture(sampler2D(u_occlusionTexture, u_occlusion_smp), getOcclusionUV());
        }

        vec4 getEmissiveTexture(){
            return texture(sampler2D(u_emissiveTexture, u_emissive_smp), getEmissiveUV());
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
            // flat, toward the camera: the slope's cross product turns with the target's y axis,
            // which is not the same on every backend and target
            ng = normalize(cross(dFdx(v_position), dFdy(v_position)));
            vec3 toCamera = lighting.eyePos.w > 0.5 ? lighting.eyePos.xyz - v_position : lighting.cameraDir.xyz;
            if (dot(ng, toCamera) < 0.0)
                ng = -ng;
            // a back face takes the reverse below as a vertex normal does, and so still faces the camera
            if (!gl_FrontFacing)
                ng = -ng;
        #endif
        #if defined(HAS_UV_SET1) || defined(HAS_UV_SET2)
            vec2 normalUV = getNormalUV();
            vec3 uv_dx = dFdx(vec3(normalUV, 0.0));
            vec3 uv_dy = dFdy(vec3(normalUV, 0.0));
            vec3 t_ = (uv_dy.t * dFdx(v_position) - uv_dx.t * dFdy(v_position)) /
                (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);

            t = normalize(t_ - ng * dot(ng, t_));
            b = cross(ng, t);
        #endif
    #endif

    // Compute pertubed normals:
    #if defined(HAS_NORMAL_MAP) && !defined(SURFACE_REPLACES_NORMAL_MAP)
        n = texture(sampler2D(u_normalTexture, u_normal_smp), getNormalUV()).rgb * 2.0 - vec3(1.0);
        n *= vec3(normalScale, normalScale, 1.0);
        n = mat3(t, b, ng) * normalize(n);
    #else
        n = ng;
    #endif
    // Double-sided glTF materials shade the reverse side with reversed normals.
    // Face culling already removes backfaces for single-sided materials.
    if (!gl_FrontFacing) {
        n = -n;
        ng = -ng;
        t = -t;
        b = -b;
    }
    n = normalize(n);
    NormalInfo info;
    info.ng = ng;
    info.t = t;
    info.b = b;
    info.n = n;
    return info;
}
#endif
