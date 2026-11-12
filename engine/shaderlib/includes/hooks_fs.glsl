// Fragment hooks of the material's shader file, the same in every pass
#include "includes/parameters_fs.glsl"
#include "includes/coverage_helpers.glsl"
#include "includes/srgb.glsl"

struct FragmentContext {
    vec3 worldPosition;
    vec3 normal;        // world space, facing the viewer; (0, 1, 0) without normals
    vec3 tangent;       // world space, along u; from the uv1 derivatives without mesh tangents
    vec3 bitangent;
    vec2 uv1;           // as the mesh stores it, 0 without the set
    vec2 uv2;
    vec4 color;         // vertex color times instance color, 1 without either
    vec4 instance;      // the instance's custom value, 0 without instancing
    vec3 view;          // unit vector to the eye
    vec3 eye;
    vec3 forward;
    bool perspective;
    vec2 pixel;         // from a corner every pass shares
    vec2 screenUV;      // in the camera's view, 0 at the top left; 0 unless read
    bool frontFacing;
    float nearPlane;    // the camera's clip planes, as distances from the eye; 0 unless read
    float farPlane;
    mat3 worldToView;   // turns a world direction into the camera's space; identity unless read
};

// one light reaching the surface, for getLight()
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct LightContext {
    vec3 direction;       // from the surface to the light
    vec3 color;           // color times intensity times attenuation, linear
    float shadow;         // 1 lit, 0 shadowed
    int type;             // LIGHT_DIRECTIONAL, LIGHT_POINT or LIGHT_SPOT
};

struct Surface {
    vec3 baseColor;     // linear
    float alpha;
    vec3 normal;        // world space
    float metallic;
    float roughness;
    float occlusion;
    vec3 emissive;      // linear
};

// the scene drawn before the mesh, in 3D its opaque part; black where there is none
#ifdef HOOK_SCENE_COLOR
    uniform texture2D u_sceneColorTexture;
    uniform sampler u_sceneColor_smp;

    // linear
    vec4 sceneColor(vec2 screenUV){
        vec4 rect = paramsFS.effect_sceneRect;
        return vec4(sRGBToLinear(texture(sampler2D(u_sceneColorTexture, u_sceneColor_smp), rect.xy + screenUV * rect.zw).rgb), 1.0);
    }
#else
    vec4 sceneColor(vec2 screenUV){
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
#endif

#ifdef HOOK_SCENE_DEPTH
    #include "includes/depth_util.glsl"

    uniform texture2D u_depthTexture;
    uniform sampler u_depth_smp;

    // distance from the eye along the view, in world units; the far plane where nothing was drawn
    float sceneDepth(vec2 screenUV){
        float depth = decodeDepth(texture(sampler2D(u_depthTexture, u_depth_smp), vec2(screenUV.x, ndcYToDepthUV(1.0 - screenUV.y * 2.0))));
        depth = min(depth, 1.0) * 2.0 - 1.0;
        vec4 terms = paramsFS.effect_projection;
        return (depth * terms.w - terms.z) / (depth * terms.y - terms.x);
    }
#else
    float sceneDepth(vec2 screenUV){
        return 1.0e4;
    }
#endif

// the fragment stage of the shader file, mapped in by the shader builder
#include "shader_hooks_fs.glsl"

// what every pass knows of a fragment; the pass adds the mesh's data
FragmentContext getFragmentContext(vec3 worldPosition){
    FragmentContext context;
    context.worldPosition = worldPosition;
    context.normal = vec3(0.0, 1.0, 0.0);
    context.tangent = vec3(1.0, 0.0, 0.0);
    context.bitangent = vec3(0.0, 0.0, 1.0);
    context.uv1 = vec2(0.0);
    context.uv2 = vec2(0.0);
    context.color = vec4(1.0);
    context.instance = vec4(0.0);
    context.eye = paramsFS.effect_eye.xyz;
    context.forward = paramsFS.effect_forward;
    context.perspective = paramsFS.effect_eye.w > 0.5;
    context.view = context.perspective ? normalize(context.eye - worldPosition) : -context.forward;
    context.pixel = abs(gl_FragCoord.xy - paramsFS.effect_pass.xy);
    context.screenUV = vec2(0.0);
    #ifdef HOOK_FRAGMENT_SCREEN
        vec4 clip = paramsFS.effect_viewProjection * vec4(worldPosition, 1.0);
        context.screenUV = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
    #endif
    context.frontFacing = gl_FrontFacing;
    context.nearPlane = 0.0;
    context.farPlane = 0.0;
    context.worldToView = mat3(1.0);
    #ifdef HOOK_FRAGMENT_VIEW
        context.worldToView = mat3(paramsFS.effect_worldToView);
    #endif
    #ifdef HOOK_FRAGMENT_DEPTH_RANGE
        // the depth terms sceneDepth reads, at the two ends of the depth range
        vec4 terms = paramsFS.effect_projection;
        // shadow maps have none
        if (terms != vec4(0.0)){
            context.nearPlane = (terms.w + terms.z) / (terms.x + terms.y);
            context.farPlane = (terms.w - terms.z) / (terms.y - terms.x);
        }
    #endif
    #ifdef USE_VARYINGS
        loadVaryings();
    #endif
    return context;
}

// always 1; keeps unused inputs alive so the stages pair
float keepHookInputs(FragmentContext context){
    return (context.worldPosition.x < -3.0e38 || context.uv1.x < -3.0e38 || context.normal.x < -3.0e38 ||
        context.color.x < -3.0e38 || context.instance.x < -3.0e38 || context.uv2.x < -3.0e38 ||
        context.tangent.x < -3.0e38) ? 0.0 : 1.0;
}

#ifdef HOOK_FRAGMENT_TANGENT
    // after the normal; turned with it on back faces, as the material's normal map does
    void setTangents(inout FragmentContext context, vec3 tangent, vec3 bitangent){
        context.tangent = context.frontFacing ? normalize(tangent) : -normalize(tangent);
        context.bitangent = context.frontFacing ? normalize(bitangent) : -normalize(bitangent);
    }

    // along uv1 from screen derivatives, for meshes without tangents
    void setDerivedTangents(inout FragmentContext context){
        vec3 normal = context.frontFacing ? context.normal : -context.normal;
        vec2 uvX = dFdx(context.uv1);
        vec2 uvY = dFdy(context.uv1);
        vec3 tangent = (uvY.y * dFdx(context.worldPosition) - uvX.y * dFdy(context.worldPosition)) * sign(uvX.x * uvY.y - uvY.x * uvX.y);
        tangent -= normal * dot(normal, tangent);
        // no UVs: any frame around the normal
        if (dot(tangent, tangent) < 1.0e-20)
            tangent = cross(abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), normal);
        tangent = normalize(tangent);
        setTangents(context, tangent, cross(normal, tangent));
    }
#endif

#ifdef USE_DEPTH_OFFSET
    // pushed away from the eye; from gl_FragCoord, so every pass agrees
    float getHookDepth(FragmentContext context){
        float offset = max(getDepthOffset(context), 0.0);
        float w = 1.0 / gl_FragCoord.w;
        float z = (gl_FragCoord.z * 2.0 - 1.0) * w - paramsFS.effect_projection.x * offset;
        w -= paramsFS.effect_projection.y * offset;
        return clamp(0.5 * z / w + 0.5, 0.0, 1.0);
    }
#endif

#ifdef USE_COVERAGE
// discards a removed fragment; returns the alpha factor of a blended one
float applyCoverage(FragmentContext context){
    // shadow maps keeping their silhouette
    if (paramsFS.effect_pass.z < 0.5)
        return 1.0;

    float coverage = clamp(getCoverage(context), 0.0, 1.0) * keepHookInputs(context);
    if (paramsFS.effect_pass.w > 0.5){
        if (coverage <= 0.0)
            discard;
        return coverage;
    }
    if (coverage < coverageDitherThreshold(context.pixel))
        discard;
    return 1.0;
}
#endif
