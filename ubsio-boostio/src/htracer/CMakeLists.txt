aux_source_directory(common LIBS_SRCS)
aux_source_directory(manager LIBS_SRCS)
aux_source_directory(service LIBS_SRCS)
aux_source_directory(. LIBS_SRCS)

add_library(htracer STATIC ${LIBS_SRCS})
target_compile_options(htracer PUBLIC -D_GNU_SOURCE)

#referenced header file
target_include_directories(htracer PRIVATE common)
target_include_directories(htracer PRIVATE manager)
target_include_directories(htracer PRIVATE service)
target_include_directories(htracer PRIVATE .)
