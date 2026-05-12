file(GLOB_RECURSE MMS_COMM_SRCS *.cpp *.h)
add_library(mms_common STATIC ${MMS_COMM_SRCS})
