file(GLOB_RECURSE BIO_NET_SRCS *.cpp *.h)
add_library(bio_net STATIC ${BIO_NET_SRCS})

if (OPEN_TEST_TOOLS STREQUAL "ON")
    include_directories(
            ${SRC_DIR}/../test/tools/cli/include/
            ${SRC_DIR}/../test/tools/diagnose/cli_sdk/
            ${SRC_DIR}/../test/tools/diagnose/cli_htracer/
            ${SRC_DIR}/../test/tools/bio_tp/
            ${SRC_DIR}/../test/tools/tracepoint/include
    )
endif()