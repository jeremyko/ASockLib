cmake_minimum_required (VERSION 3.1) 

project (ASockLib)

include_directories(lib/include)

set(CMAKE_CXX_STANDARD 11)

if(WIN32)
    set(ADDITIONAL_LIBRARIES wsock32)
elseif(APPLE)
    add_compile_options( -Wall -pthread )
    add_subdirectory (lib/src/_nix)
elseif ( CMAKE_SYSTEM_NAME MATCHES "Linux")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -g")
    add_compile_options( -Wall -pthread )
    add_subdirectory (lib/src/_nix)
endif()

add_subdirectory (sample/ipc_echo_example/inheritance)
add_subdirectory (sample/ipc_echo_example/composition)
add_subdirectory (sample/tcp_echo_example/inheritance)
add_subdirectory (sample/tcp_echo_example/composition)
add_subdirectory (sample/udp_echo_example/inheritance)
add_subdirectory (sample/udp_echo_example/composition)

