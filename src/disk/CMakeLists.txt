file(GLOB_RECURSE COMMON_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/common/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp"
        )

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)$")
    message(STATUS "Building bdm for arm...")
    file(GLOB_RECURSE ARCH_SRCS
            "${CMAKE_CURRENT_SOURCE_DIR}/arm/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/arm/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/arm/*.cpp"
            )
    set(PLAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/arm")
else()
    message(STATUS "Building bdm for x86...")
    file(GLOB_RECURSE ARCH_SRCS
            "${CMAKE_CURRENT_SOURCE_DIR}/x86/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/x86/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/x86/*.cpp"
            )
    set(PLAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/x86")
endif()

add_library(bio_bdm STATIC ${COMMON_SRCS} ${ARCH_SRCS})
target_include_directories(bio_bdm PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common>
        $<BUILD_INTERFACE:${PLAT_INCLUDE_DIR}>
        )
target_compile_options(bio_bdm PUBLIC -D_GNU_SOURCE)