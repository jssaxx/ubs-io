# build_mockcpp.cmake
if(TARGET mockcpp::mockcpp)
    return()
endif()

include(FetchContent)
include(ProcessorCount)

# --- Configuration ---
set(_MS_LIB_CACHE "${PROJECT_BUILD_PATH}/_deps")
file(MAKE_DIRECTORY ${_MS_LIB_CACHE})

ProcessorCount(N)
set(THNUM 8)
if(N GREATER 0 AND N LESS THNUM)
    set(THNUM ${N})
endif()

# --- Paths & Metadata ---
set(MOCKCPP_VERSION "2.7")

string(SHA256 CONFIG_HASH
        "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_GENERATOR}"
        )

# --- Download ---
FetchContent_Declare(
        _mockcpp_src
        GIT_REPOSITORY https://gitcode.com/Ascend/mockcpp.git
        GIT_TAG v${MOCKCPP_VERSION}
)
FetchContent_Populate(_mockcpp_src)

# --- Patch: apply ARM64 support patch ---
set(PATCH_FILE "${PROJECT_SOURCE_DIR}/test/external_libs/mockcpp_support_arm64.patch")

# 检查是否已打过补丁（通过是否存在新增的 ARM64 文件）
if(EXISTS "${_mockcpp_src_SOURCE_DIR}/src/JmpCodeAARCH64.h")
    message(STATUS "ARM64 patch already applied, skipping.")
else()
    if(EXISTS "${PATCH_FILE}")
        message(STATUS "Applying mockcpp patch: ${PATCH_FILE}")
        # Normalize line endings (keep your existing logic)
        file(READ "${PATCH_FILE}" _PATCH_CONTENT)
        string(REGEX MATCHALL "diff --git a/([A-Za-z0-9/._-]+)" _MATCHES "${_PATCH_CONTENT}")
        foreach(_MATCH IN LISTS _MATCHES)
            string(REGEX REPLACE "diff --git a/(.+)" "\\1" _FILE "${_MATCH}")
            if(EXISTS "${_mockcpp_src_SOURCE_DIR}/${_FILE}")
                execute_process(
                        COMMAND sed -i "s/\\r\$//" "${_mockcpp_src_SOURCE_DIR}/${_FILE}"
                        WORKING_DIRECTORY "${_mockcpp_src_SOURCE_DIR}"
                )
            endif()
        endforeach()

        # Apply patch with better error handling
        execute_process(
                COMMAND patch -p1 --verbose
                INPUT_FILE "${PATCH_FILE}"
                WORKING_DIRECTORY "${_mockcpp_src_SOURCE_DIR}"
                RESULT_VARIABLE PATCH_RESULT
                OUTPUT_VARIABLE PATCH_OUTPUT
                ERROR_VARIABLE PATCH_ERROR
        )

        if(NOT PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR
                    "Failed to apply mockcpp patch!\n"
                    "Patch file: ${PATCH_FILE}\n"
                    "Exit code: ${PATCH_RESULT}\n"
                    "Output:\n${PATCH_OUTPUT}\n${PATCH_ERROR}"
                    )
        else()
            message(STATUS "Patch applied successfully.")
        endif()
    else()
        message(WARNING "Patch file not found: ${PATCH_FILE}")
    endif()
endif()

# Step 3: 独立构建（关键！）
set(SUB_BUILD "${CMAKE_BINARY_DIR}/_deps/mockcpp-build")
file(REMOVE_RECURSE "${SUB_BUILD}")
file(MAKE_DIRECTORY "${SUB_BUILD}")

# 调用 cmake 时显式指定 -S (source) 和 -B (build)
execute_process(
        COMMAND
        ${CMAKE_COMMAND}
        -S "${_mockcpp_src_SOURCE_DIR}"     # 子模块源码根
        -B "${SUB_BUILD}"                   # 子模块构建根
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_TESTING=OFF
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/_deps/mockcpp-install
        -DCMAKE_CXX_FLAGS=-D_GLIBCXX_USE_CXX11_ABI=0
        RESULT_VARIABLE res
)

if(res)
    message(FATAL_ERROR "Configure failed")
endif()

# 构建
execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${SUB_BUILD}" --target install -j8
)

# --- Copy to final PROJECT_OUTPUT_PATH/mockcpp/ ---
set(FINAL_MOCKCPP_DIR "${PROJECT_OUTPUT_PATH}/boostio_test_tools/mockcpp")

# Remove old version
file(REMOVE_RECURSE "${FINAL_MOCKCPP_DIR}")
file(MAKE_DIRECTORY "${FINAL_MOCKCPP_DIR}")

# 确保源目录存在
if(EXISTS "${CMAKE_BINARY_DIR}/_deps/mockcpp-install")
    file(COPY "${CMAKE_BINARY_DIR}/_deps/mockcpp-install/" DESTINATION "${FINAL_MOCKCPP_DIR}")
endif()
