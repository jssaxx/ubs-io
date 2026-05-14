set(LIBART_SRC_DIR "${PROJECT_3RDPARTY_BIN_DIR}/libart/src")
file(GLOB_RECURSE MMS_CACHE_SRCS *.c *.cpp *.h)

add_library(mms_cache STATIC ${MMS_CACHE_SRCS} ${LIBART_SRC_DIR}/art.c)
target_compile_options(mms_cache PUBLIC -D_GNU_SOURCE -DTHREADED)
