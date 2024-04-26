file(GLOB_RECURSE BIO_NET_SRCS *.cpp *.h)
add_library(bio_net STATIC ${BIO_NET_SRCS})