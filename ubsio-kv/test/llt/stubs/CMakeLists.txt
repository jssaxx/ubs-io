set(KV_STUB_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/csrc/kvc
    ${PROJECT_SOURCE_DIR}/src/csrc/nds
    ${PROJECT_SOURCE_DIR}/src/csrc/utils
    ${UBSIO_BOOSTIO_INCLUDE_DIR}
)

add_library(kv_bio_stub SHARED kvc/fake_bio_sdk.cpp)
target_include_directories(kv_bio_stub PRIVATE ${KV_STUB_INCLUDE_DIRS})
set_target_properties(kv_bio_stub PROPERTIES
    OUTPUT_NAME bio_sdk
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_library(kv_acl_stub SHARED kvc/fake_acl.cpp)
target_include_directories(kv_acl_stub PRIVATE ${KV_STUB_INCLUDE_DIRS})
set(KV_FAKE_ASCEND_DIR "${CMAKE_BINARY_DIR}/fake_ascend" CACHE INTERNAL "")
set_target_properties(kv_acl_stub PROPERTIES
    OUTPUT_NAME ascendcl
    LIBRARY_OUTPUT_DIRECTORY "${KV_FAKE_ASCEND_DIR}/lib64"
)

add_library(kv_nds_stub SHARED nds/fake_nds.cpp)
target_include_directories(kv_nds_stub PRIVATE ${KV_STUB_INCLUDE_DIRS})
set_target_properties(kv_nds_stub PROPERTIES
    OUTPUT_NAME nds_file
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set(KV_INCOMPLETE_STUB_DIR "${CMAKE_BINARY_DIR}/incomplete" CACHE INTERNAL "")
set(KV_INCOMPLETE_ASCEND_DIR "${CMAKE_BINARY_DIR}/incomplete_ascend" CACHE INTERNAL "")

add_library(kv_bio_incomplete_stub SHARED kvc/incomplete_bio.cpp)
set_target_properties(kv_bio_incomplete_stub PROPERTIES
    OUTPUT_NAME bio_sdk
    LIBRARY_OUTPUT_DIRECTORY "${KV_INCOMPLETE_STUB_DIR}"
)

add_library(kv_acl_incomplete_stub SHARED kvc/incomplete_acl.cpp)
set_target_properties(kv_acl_incomplete_stub PROPERTIES
    OUTPUT_NAME ascendcl
    LIBRARY_OUTPUT_DIRECTORY "${KV_INCOMPLETE_ASCEND_DIR}/lib64"
)

add_library(kv_nds_incomplete_stub SHARED nds/incomplete_nds.cpp)
set_target_properties(kv_nds_incomplete_stub PROPERTIES
    OUTPUT_NAME nds_file
    LIBRARY_OUTPUT_DIRECTORY "${KV_INCOMPLETE_STUB_DIR}"
)

foreach(stub_target kv_bio_stub kv_acl_stub kv_nds_stub)
    target_compile_options(${stub_target} PRIVATE -Wno-unused-parameter)
endforeach()

set(KV_STUB_TARGETS
    kv_bio_stub
    kv_acl_stub
    kv_nds_stub
    CACHE INTERNAL ""
)

set(KV_INCOMPLETE_STUB_TARGETS
    kv_bio_incomplete_stub
    kv_acl_incomplete_stub
    kv_nds_incomplete_stub
    CACHE INTERNAL ""
)
