// the material maps the G-buffer samples: a surface hook can replace them
#if defined(HAS_BASECOLOR_TEXTURE) && !defined(SURFACE_REPLACES_BASECOLOR_MAP)
    #define USE_BASECOLOR_MAP
#endif
#if defined(HAS_METALLICROUGHNESS_TEXTURE) && !defined(SURFACE_REPLACES_METALLICROUGHNESS_MAP)
    #define USE_METALLICROUGHNESS_MAP
#endif
// terrain has no UV set to sample it with
#if defined(HAS_OCCLUSION_TEXTURE) && !defined(SURFACE_REPLACES_OCCLUSION_MAP) && !defined(HAS_TERRAIN)
    #define USE_OCCLUSION_MAP
#endif
