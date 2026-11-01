aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} MESHOPTIMIZER_SRCS)

add_library(
    meshoptimizer

    STATIC

    ${MESHOPTIMIZER_SRCS}
)
