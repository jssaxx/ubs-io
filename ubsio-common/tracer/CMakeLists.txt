add_library(tracer STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/trace_mark.cpp
)

target_include_directories(tracer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
