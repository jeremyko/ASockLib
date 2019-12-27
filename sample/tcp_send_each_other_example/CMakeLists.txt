add_executable (tcp_eo_server server.cpp)
add_executable (tcp_eo_client client.cpp)
 
target_link_libraries (tcp_eo_server LINK_PUBLIC ASockLib)
target_link_libraries (tcp_eo_client LINK_PUBLIC ASockLib)


