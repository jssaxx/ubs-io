file(GLOB_RECURSE MMS_SECURITY_SRCS *.cpp *.h)
add_library(mms_security STATIC ${MMS_SECURITY_SRCS})
