file(GLOB_RECURSE BIO_FLOW_SRCS *.cpp *.h)

add_library(bio_flow STATIC ${BIO_FLOW_SRCS})
