#source files
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/common 	                                COMMON_LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/debug                                    COMMON_LIBS_SRCS)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/src                                      CLIENT_LIB_SRCS)
include_directories(${CMAKE_CURRENT_LIST_DIR}/common)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

message("-- Build io_interceptor lib.")

set(INTECEPTOR_CLEINT_DIR ${CMAKE_CURRENT_LIST_DIR}/common
               ${CMAKE_CURRENT_LIST_DIR}/include
               ${CMAKE_CURRENT_LIST_DIR}/src)

add_library(ock_interceptor_object OBJECT ${COMMON_LIBS_SRCS} ${CLIENT_LIB_SRCS})
target_include_directories(ock_interceptor_object PUBLIC ${CMAKE_CURRENT_LIST_DIR}/common
                                                         ${CMAKE_CURRENT_LIST_DIR}/include
                                                         ${CMAKE_CURRENT_LIST_DIR}/src)

add_library(ock_interceptor_static STATIC $<TARGET_OBJECTS:ock_interceptor_object>)
set_target_properties(ock_interceptor_static PROPERTIES OUTPUT_NAME "ock_interceptor_static")
set_target_properties(ock_interceptor_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

add_library(ock_interceptor_shared SHARED $<TARGET_OBJECTS:ock_interceptor_object>)
set_target_properties(ock_interceptor_shared PROPERTIES OUTPUT_NAME "ock_interceptor")
