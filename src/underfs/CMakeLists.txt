file(GLOB_RECURSE BIO_UNDERFS_SRCS *.cpp *.h)

include_directories(
            ${SRC_DIR}/../test/tools/bio_tp/
            ${SRC_DIR}/../test/tools/tracepoint/include)

add_library(bio_underfs STATIC ${BIO_UNDERFS_SRCS})