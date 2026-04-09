file(GLOB_RECURSE KVC_SRC 
    ${CMAKE_CURRENT_SOURCE_DIR}/kvc/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nds/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp
)

add_library(ubsio_kvc SHARED ${KVC_SRC})

set(UBSIO_BOOSTIO_DIR ${PROJECT_SOURCE_DIR}/../ubsio-boostio)
set(UBSIO_NDS_DIR ${PROJECT_SOURCE_DIR}/../ubsio-nds)

target_include_directories(ubsio_kvc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/kvc
    ${CMAKE_CURRENT_SOURCE_DIR}/nds
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/include
    ${UBSIO_BOOSTIO_DIR}/src/sdk
    ${UBSIO_BOOSTIO_DIR}/src/common
    ${UBSIO_NDS_DIR}/include
)

target_link_directories(ubsio_kvc PRIVATE
    ${UBSIO_BOOSTIO_DIR}/build
)

target_link_libraries(ubsio_kvc
    pthread
    dl
)

set_target_properties(ubsio_kvc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME ubsio_kvc
)

set(TARGET_INSTALL_DIR ${OUTPUT_PATH})
set(TARGET_INSTALL_LIB_DIR ${TARGET_INSTALL_DIR}/lib)
set(TARGET_INSTALL_INCLUDE_DIR ${TARGET_INSTALL_DIR}/include)

install(TARGETS ubsio_kvc 
    LIBRARY DESTINATION ${TARGET_INSTALL_LIB_DIR}
    ARCHIVE DESTINATION ${TARGET_INSTALL_LIB_DIR}
    RUNTIME DESTINATION ${TARGET_INSTALL_LIB_DIR}
)

install(FILES ${PROJECT_SOURCE_DIR}/include/ubsio_kvc.h
    DESTINATION ${TARGET_INSTALL_INCLUDE_DIR}
)
