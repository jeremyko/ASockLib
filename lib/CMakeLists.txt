
add_library (ASockLib CumBuffer.h ASockBase.hpp ASockBase.cpp AClientSocketTCP.hpp AClientSocketTCP.cpp AServerSocketTCP.hpp AServerSocketTCP.cpp)

target_include_directories (ASockLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if(WIN32)
    set(ADDITIONAL_LIBRARIES wsock32)

elseif(APPLE)
    #os x
#elseif(UNIX AND NOT APPLE)
elseif ( CMAKE_SYSTEM_NAME MATCHES "Linux")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

