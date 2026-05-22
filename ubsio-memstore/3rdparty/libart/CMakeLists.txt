set(ART_INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/libart)
set(ART_CHECK_FILE ${ART_INSTALL_DIR}/src)

if(EXISTS ${ART_CHECK_FILE})
    message("-- libart: has been built, ignored")
    return()
endif()

include(FetchContent)
FetchContent_Declare(
        libart_external
        GIT_REPOSITORY https://gitcode.com/gh_mirrors/li/libart.git
        GIT_TAG        master
        SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/libart"
)

FetchContent_GetProperties(libart_external)
if(NOT libart_external_POPULATED)
    FetchContent_Populate(libart_external)
endif()

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/libart/src" DESTINATION ${ART_INSTALL_DIR})
