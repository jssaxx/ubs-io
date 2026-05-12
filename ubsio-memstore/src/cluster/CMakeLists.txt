file(GLOB_RECURSE MMS_CLUSTER_SRCS *.c *.cpp *.h)

add_library(mms_cluster STATIC ${MMS_CLUSTER_SRCS})
target_compile_options(mms_cluster PUBLIC -D_GNU_SOURCE -DTHREADED)

target_include_directories(mms_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/client)
target_include_directories(mms_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_include_directories(mms_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_include_directories(mms_cluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server/pt)
