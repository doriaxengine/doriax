cmake_minimum_required(VERSION 3.15)

# Source files used for both library and executable
set(SHADERCOMPILER_SOURCES
    args.cc
    input.cc
    glslang.cc
    spirvcross.cc
)

# Build as a static library
add_library(shadercompiler STATIC ${SHADERCOMPILER_SOURCES})

target_link_libraries(shadercompiler PRIVATE
    glslang
    OSDependent 
    SPIRV 
    SPVRemapper 
    spirv-cross-core 
    spirv-cross-cpp 
    spirv-cross-glsl 
    spirv-cross-hlsl 
    spirv-cross-reflect
    spirv-cross-util 
    spirv-cross-msl
)

if (MSVC)
    if (ENABLE_D3D11_COMPILER)
        add_definitions(-DD3D11_COMPILER)
        target_link_libraries(shadercompiler PRIVATE d3dcompiler)
    endif()
endif()

set_target_properties(
    shadercompiler
    PROPERTIES
    CXX_STANDARD 17
)

install(TARGETS shadercompiler
        CONFIGURATIONS Release MinSizeRel
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)