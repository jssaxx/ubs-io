#source files
aux_source_directory(common COMMON_LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/debug                                    COMMON_LIBS_SRCS)

aux_source_directory(src CLIENT_LIB_SRCS)
include_directories(common)
include_directories(include)

message("-- Build io_interceptor lib.")

set(INTECEPTOR_CLEINT_DIR common
        include
        src)

add_library(ock_interceptor_object OBJECT ${COMMON_LIBS_SRCS} ${CLIENT_LIB_SRCS})
target_include_directories(ock_interceptor_object PUBLIC common
        include
        src)

add_library(ock_interceptor_static STATIC $<TARGET_OBJECTS:ock_interceptor_object>)

set_target_properties(ock_interceptor_static PROPERTIES OUTPUT_NAME "ock_interceptor_static")
target_link_libraries(ock_interceptor_static
        pthread dl
        boundscheck_lib)
set_target_properties(ock_interceptor_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

add_library(ock_interceptor_shared SHARED $<TARGET_OBJECTS:ock_interceptor_object>)
target_link_libraries(ock_interceptor_shared
        pthread dl
        boundscheck_lib)
set_target_properties(ock_interceptor_shared PROPERTIES OUTPUT_NAME "ock_interceptor")

install(TARGETS ock_interceptor_shared DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)