file(GLOB_RECURSE MMS_CACHE_SRCS *.c *.cpp *.h)

add_library(mms_cache STATIC ${MMS_CACHE_SRCS})
target_compile_options(mms_cache PUBLIC -D_GNU_SOURCE -DTHREADED)
