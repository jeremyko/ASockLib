add_executable (ipc_server_inher echo_server.cpp)
add_executable (ipc_client_inher client.cpp)
 
target_link_libraries (ipc_server_inher LINK_PUBLIC ASockLib)
target_link_libraries (ipc_client_inher LINK_PUBLIC ASockLib)

#set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if(WIN32)
    set(ADDITIONAL_LIBRARIES wsock32)
elseif(APPLE)
    #os x
elseif ( CMAKE_SYSTEM_NAME MATCHES "Linux")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()


