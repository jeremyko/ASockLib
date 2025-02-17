set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server_tcp_inh server.cpp)
add_executable(client_tcp_inh client.cpp)
target_link_libraries(server_tcp_inh LINK_PUBLIC asock)
target_link_libraries(client_tcp_inh LINK_PUBLIC asock)


