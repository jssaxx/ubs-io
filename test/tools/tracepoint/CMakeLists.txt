project(untitled C)

set(CMAKE_C_STANDARD 99)

aux_source_directory(src TRACE_POINT_SRCS)
add_library(tracepoint_static STATIC ${TRACE_POINT_SRCS})
set_target_properties(tracepoint_static PROPERTIES OUTPUT_NAME tracepoint)
target_include_directories(tracepoint_static PUBLIC include)
target_link_libraries(tracepoint_static PUBLIC cli_agent_static)

add_library(tracepoint_shared SHARED ${TRACE_POINT_SRCS})
set_target_properties(tracepoint_shared PROPERTIES OUTPUT_NAME tracepoint)
target_include_directories(tracepoint_shared PUBLIC include)
target_link_libraries(tracepoint_shared PUBLIC cli_agent_shared)