file(GLOB_RECURSE MMS_NET_SRCS *.cpp *.h)
add_library(mms_net STATIC ${MMS_NET_SRCS})
target_link_libraries(mms_net PUBLIC hcom_lib)
