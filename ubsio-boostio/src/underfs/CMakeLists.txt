include_directories(${PROJECT_3RDPARTY_SRC_DIR}/hadoop/)
file(GLOB_RECURSE BIO_UNDERFS_SRCS *.cpp *.h)
add_library(bio_underfs STATIC ${BIO_UNDERFS_SRCS})
target_link_libraries(bio_underfs PRIVATE dl)