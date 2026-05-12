file(GLOB_RECURSE MMS_MEMORY_SRCS *.cpp *.h)
add_library(mms_memory STATIC ${MMS_MEMORY_SRCS})
