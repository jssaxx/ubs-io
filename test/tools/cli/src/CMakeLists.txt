
add_executable(cli_client common/cli_common.c common/cli_crc32.c client/cli_client.c)
target_include_directories(cli_client PRIVATE common client)
target_link_libraries(cli_client PUBLIC pthread)

add_executable(cli_server common/cli_common.c common/cli_crc32.c server/cli_server.c)
target_include_directories(cli_server PRIVATE common server)
target_link_libraries(cli_server PUBLIC pthread)

add_library(cli_agent_static STATIC common/cli_common.c common/cli_crc32.c agent/cli_getopt.c agent/cli_agent.c)
set_target_properties(cli_agent_static PROPERTIES OUTPUT_NAME cli_agent)
target_include_directories(cli_agent_static PRIVATE common agent)
target_link_libraries(cli_agent_static PUBLIC pthread)
target_include_directories(cli_agent_static PUBLIC ../include)

add_library(cli_agent_shared SHARED common/cli_common.c common/cli_crc32.c agent/cli_getopt.c agent/cli_agent.c)
set_target_properties(cli_agent_shared PROPERTIES OUTPUT_NAME cli_agent)
target_include_directories(cli_agent_shared PRIVATE common agent)
target_link_libraries(cli_agent_shared PUBLIC pthread)
target_include_directories(cli_agent_shared PUBLIC ../include)