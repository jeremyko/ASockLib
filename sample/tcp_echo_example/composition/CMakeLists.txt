add_executable (tcp_server_compo echo_server.cpp)
add_executable (tcp_client_compo client.cpp)
 
target_link_libraries (tcp_server_compo LINK_PUBLIC ASockLib)
target_link_libraries (tcp_client_compo LINK_PUBLIC ASockLib)


