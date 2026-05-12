file(GLOB_RECURSE MMS_CONF_SRCS *.cpp *.h)
add_library(mms_conf STATIC ${MMS_CONF_SRCS})
