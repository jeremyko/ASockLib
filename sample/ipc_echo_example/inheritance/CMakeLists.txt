add_executable (ipc_server_inher echo_server.cpp)
add_executable (ipc_client_inher client.cpp)
 
target_link_libraries (ipc_server_inher LINK_PUBLIC ASockLib)
target_link_libraries (ipc_client_inher LINK_PUBLIC ASockLib)

