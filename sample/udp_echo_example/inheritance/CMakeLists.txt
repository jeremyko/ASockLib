set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server_udp_inh server.cpp)
add_executable(client_udp_inh client.cpp)
target_link_libraries(server_udp_inh LINK_PUBLIC asock)
target_link_libraries(client_udp_inh LINK_PUBLIC asock)

