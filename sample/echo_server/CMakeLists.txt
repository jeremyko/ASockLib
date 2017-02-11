add_executable (server echo_server.cpp msg_defines.h)
add_executable (client client.cpp msg_defines.h)
 
target_link_libraries (server LINK_PUBLIC ASockLib)
target_link_libraries (client LINK_PUBLIC ASockLib)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if(WIN32)
    set(ADDITIONAL_LIBRARIES wsock32)
elseif(APPLE)
    #os x
elseif ( CMAKE_SYSTEM_NAME MATCHES "Linux")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()


