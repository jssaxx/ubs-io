file(GLOB_RECURSE BIO_NET_SRCS *.cpp *.h)
add_library(bio_net STATIC ${BIO_NET_SRCS})
target_link_libraries(bio_net PUBLIC hcom_headers ${CMAKE_DL_LIBS})
