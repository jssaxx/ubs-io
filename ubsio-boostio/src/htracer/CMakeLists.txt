aux_source_directory(common LIBS_SRCS)
aux_source_directory(manager LIBS_SRCS)
aux_source_directory(service LIBS_SRCS)
aux_source_directory(. LIBS_SRCS)

add_library(htracer SHARED ${LIBS_SRCS})
target_compile_options(htracer PUBLIC -D_GNU_SOURCE)
install(TARGETS htracer DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

#referenced header file
target_include_directories(htracer PRIVATE common)
target_include_directories(htracer PRIVATE manager)
target_include_directories(htracer PRIVATE service)
target_include_directories(htracer PRIVATE .)
