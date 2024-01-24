#source files
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/common LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/manager LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/service LIBS_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} LIBS_SRCS)

add_library(htracer STATIC ${LIBS_SRCS})
target_compile_options(htracer PUBLIC -D_GNU_SOURCE)

#referenced header file
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/manager)
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/service)
target_include_directories(htracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
