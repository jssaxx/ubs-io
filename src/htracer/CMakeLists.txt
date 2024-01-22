#source files
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/common LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/manager LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/service LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} LIBS_SRCS)

add_library(htracer SHARED ${LIBS_SRCS})
target_compile_options(htracer PUBLIC -D_GNU_SOURCE -D__DPAX_LINUX_USR__  -Werror=reorder -Werror=parentheses -Werror=return-type -Werror=unused-variable)
set_target_properties(htracer PROPERTIES OUTPUT_NAME "htracer")

#referenced header file
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/manager)
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/service)
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS htracer DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)