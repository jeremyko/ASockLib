set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server_udp_comp server.cpp)
add_executable(client_udp_comp client.cpp)
target_link_libraries(server_udp_comp LINK_PUBLIC asock)
target_link_libraries(client_udp_comp LINK_PUBLIC asock)



