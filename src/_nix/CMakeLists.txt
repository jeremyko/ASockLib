
add_library (ASockLib 
            ASock.cpp 
            )

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

