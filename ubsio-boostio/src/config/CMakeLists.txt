file(GLOB_RECURSE BIO_CONF_SRCS *.cpp *.h)
add_library(bio_conf STATIC ${BIO_CONF_SRCS})