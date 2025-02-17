set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server_tcp_eo server.cpp)
add_executable(client_tcp_eo client.cpp)
target_link_libraries(server_tcp_eo LINK_PUBLIC asock)
target_link_libraries(client_tcp_eo LINK_PUBLIC asock)


