file(GLOB_RECURSE BIO_SECURITY_SRCS *.cpp *.h)
add_library(bio_security STATIC ${BIO_SECURITY_SRCS})