add_executable (tcp_server_inher echo_server.cpp)
add_executable (tcp_client_inher client.cpp)
 
target_link_libraries (tcp_server_inher LINK_PUBLIC ASockLib)
target_link_libraries (tcp_client_inher LINK_PUBLIC ASockLib)


