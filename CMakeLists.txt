cmake_minimum_required (VERSION 2.8) 

project (ASockLib)

file(GLOB_RECURSE INCS "lib/include/*")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/lib/include)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/CumBuffer)

SET(CMAKE_CXX_STANDARD 11) 
#SET(CMAKE_BUILD_TYPE Debug)
SET(CMAKE_BUILD_TYPE Release)

if(WIN32)
   SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /nologo /W3 /Gy /EHsc")
   SET(CMAKE_CXX_FLAGS_DEBUG           " /MTd /ZI /Od ")
   SET(CMAKE_CXX_FLAGS_RELEASE         " /MT  /O3 ")

   link_libraries(ws2_32 wsock32)   
   add_library (ASockLib lib/src/win/ASock.cpp ${INCS} )

elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")   
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

    add_library (ASockLib lib/src/_nix/ASock.cpp ${INCS} )
endif()


if(WIN32)    
    add_definitions(/wd4244 /wd4324)
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")    
    add_definitions(-Wall)
endif()

add_subdirectory (sample/ipc_echo_example/inheritance)
add_subdirectory (sample/ipc_echo_example/composition)
add_subdirectory (sample/tcp_echo_example/inheritance)
add_subdirectory (sample/tcp_echo_example/composition)
add_subdirectory (sample/udp_echo_example/inheritance)
add_subdirectory (sample/udp_echo_example/composition)

