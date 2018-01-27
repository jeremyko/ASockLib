add_executable (udp_server_inher echo_server.cpp)
add_executable (udp_client_inher client.cpp)
 
target_link_libraries (udp_server_inher LINK_PUBLIC ASockLib)
target_link_libraries (udp_client_inher LINK_PUBLIC ASockLib)

