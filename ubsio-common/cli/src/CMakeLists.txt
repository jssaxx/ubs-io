set(CLI_COMMON_SRCS
    common/cli_common.c
)

add_executable(cli_client ${CLI_COMMON_SRCS} client/cli_client.c)
target_include_directories(cli_client PRIVATE common client ../include)
target_link_libraries(cli_client PUBLIC pthread)
target_link_options(cli_client PRIVATE -pie)

add_executable(cli_server ${CLI_COMMON_SRCS} server/cli_server.c)
target_include_directories(cli_server PRIVATE common server ../include)
target_link_libraries(cli_server PUBLIC pthread)
target_link_options(cli_server PRIVATE -pie)

add_library(cli_agent_static STATIC ${CLI_COMMON_SRCS} agent/cli_agent.c)
set_target_properties(cli_agent_static PROPERTIES OUTPUT_NAME cli_agent)
target_include_directories(cli_agent_static PRIVATE common agent ../include)
target_include_directories(cli_agent_static PUBLIC ../include)
target_link_libraries(cli_agent_static PUBLIC pthread)

add_library(cli_agent_shared SHARED ${CLI_COMMON_SRCS} agent/cli_agent.c)
set_target_properties(cli_agent_shared PROPERTIES OUTPUT_NAME cli_agent)
target_include_directories(cli_agent_shared PRIVATE common agent ../include)
target_include_directories(cli_agent_shared PUBLIC ../include)
target_link_libraries(cli_agent_shared PUBLIC pthread)
