include_directories(include)

add_library (ASockLib 
            ASockBase.cpp 
            AClientSocketTCP.cpp 
            AServerSocketTCP.cpp)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

