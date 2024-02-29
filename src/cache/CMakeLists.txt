file(GLOB_RECURSE BIO_CACHE_SRCS *.cpp *.h)
add_library(bio_cache STATIC ${BIO_CACHE_SRCS})