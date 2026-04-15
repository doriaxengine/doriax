file(GLOB_RECURSE HEADERS "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.hpp" "${SOURCE_DIR}/*.inl")

foreach(HEADER ${HEADERS})
    file(RELATIVE_PATH REL_PATH ${SOURCE_DIR} ${HEADER})
    get_filename_component(DIR_PATH ${REL_PATH} DIRECTORY)
    file(MAKE_DIRECTORY "${DEST_DIR}/${DIR_PATH}")
    file(COPY ${HEADER} DESTINATION "${DEST_DIR}/${DIR_PATH}")
endforeach()