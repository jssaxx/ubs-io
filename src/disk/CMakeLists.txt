file(GLOB_RECURSE BIO_BDM_SRCS *.c *.cpp *.h)

add_library(bio_bdm STATIC ${BIO_BDM_SRCS})
target_compile_options(bio_bdm PUBLIC -D_GNU_SOURCE)
