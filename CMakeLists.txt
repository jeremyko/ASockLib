cmake_minimum_required (VERSION 2.8) 

project (ASockLib)

include_directories(include)

if(WIN32)
elseif(APPLE)
    add_subdirectory (src/_nix)
elseif ( CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_subdirectory (src/_nix)
endif()

add_subdirectory (sample/ipc_echo_example/inheritance)
add_subdirectory (sample/ipc_echo_example/composition)
add_subdirectory (sample/tcp_echo_example/inheritance)
add_subdirectory (sample/tcp_echo_example/composition)

