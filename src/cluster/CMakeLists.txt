file(GLOB_RECURSE BIO_CLUSTER_SRCS *.c *.cpp *.h)

add_library(bio_cluster STATIC ${BIO_CLUSTER_SRCS})
target_compile_options(bio_cluster PUBLIC -D_GNU_SOURCE -DTHREADED)

target_include_directories(bio_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/client)
target_include_directories(bio_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_include_directories(bio_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_include_directories(bio_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server/pt)