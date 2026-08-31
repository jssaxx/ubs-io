cmake_minimum_required(VERSION 3.10)

project(decrypt_example)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -Wextra")

add_library(decrypt SHARED decrypt_example.cpp)

set_target_properties(decrypt PROPERTIES
        OUTPUT_NAME "decrypt"
        PREFIX "lib"
        SUFFIX ".so"
)

install(TARGETS decrypt
        LIBRARY DESTINATION lib
)

