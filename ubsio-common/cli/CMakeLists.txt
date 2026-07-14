cmake_minimum_required(VERSION 3.16)

project(mms_cli C)

set(CMAKE_C_STANDARD 99)

option(CLI_ENABLE_ASAN_UBSAN "Build CLI tools with ASan and UBSan" OFF)
if (CLI_ENABLE_ASAN_UBSAN)
    if (NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "CLI_ENABLE_ASAN_UBSAN requires GCC or Clang.")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

include_directories(include)
add_subdirectory(src)
