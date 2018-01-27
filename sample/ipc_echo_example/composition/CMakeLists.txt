add_executable (ipc_server_compo echo_server.cpp)
add_executable (ipc_client_compo client.cpp )
 
target_link_libraries (ipc_server_compo LINK_PUBLIC ASockLib)
target_link_libraries (ipc_client_compo LINK_PUBLIC ASockLib)



