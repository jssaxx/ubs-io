# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# ubs-io is licensed under the Mulan PSL v2.
include(FetchContent)
if(NOT FETCHCONTENT_SOURCE_DIR_XDS)
    find_package(Git REQUIRED)
endif()

set(xds_repository https://atomgit.com/openeuler/kernel.git)
set(xds_branch openEuler-26.09)
FetchContent_Declare(xds
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/xds-openeuler-src"
    DOWNLOAD_COMMAND "${GIT_EXECUTABLE}" clone --depth=1 --single-branch --filter=blob:none
        --no-tags --no-checkout --branch "${xds_branch}" "${xds_repository}" <SOURCE_DIR>
        COMMAND "${GIT_EXECUTABLE}" -C <SOURCE_DIR> sparse-checkout set --no-cone
            /tools/lib/nds/file_p2p/ /tools/include/uapi/linux/nds_p2p.h
        COMMAND "${GIT_EXECUTABLE}" -C <SOURCE_DIR> checkout "${xds_branch}"
    UPDATE_COMMAND "${GIT_EXECUTABLE}" -C <SOURCE_DIR> fetch --depth=1 --no-tags --filter=blob:none
        origin "refs/heads/${xds_branch}"
        COMMAND "${GIT_EXECUTABLE}" -C <SOURCE_DIR> checkout --detach FETCH_HEAD
)
FetchContent_MakeAvailable(xds)

set(xds_runtime "${xds_SOURCE_DIR}/tools/lib/nds/file_p2p")
set(xds_uapi "${xds_SOURCE_DIR}/tools/include/uapi")

function(preprocess_xds_abi name header)
    add_custom_command(OUTPUT "${name}.i"
        COMMAND "${CMAKE_C_COMPILER}" -E -P -dD -x c "${header}" -o "${name}.i"
        DEPENDS "${header}"
        VERBATIM)
endfunction()

preprocess_xds_abi(xds-upstream "${xds_runtime}/nds_api.h")
preprocess_xds_abi(xds-abi "${PROJECT_SOURCE_DIR}/src/nds_api.h")

add_custom_target(xds_abi_check
    COMMAND "${CMAKE_COMMAND}" -E compare_files xds-upstream.i xds-abi.i
    DEPENDS xds-upstream.i xds-abi.i
    VERBATIM)

add_library(xds_runtime STATIC
    "${xds_runtime}/nds_api.c"
    "${xds_runtime}/p2p_common.c"
)
add_dependencies(xds_runtime xds_abi_check)
set_target_properties(xds_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(xds_runtime PRIVATE "${xds_uapi}")
target_compile_options(xds_runtime PRIVATE
    -Wall -Wextra -Werror -fstack-protector-strong -fno-omit-frame-pointer -ftrapv
)
