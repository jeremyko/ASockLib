cmake_minimum_required(VERSION 3.14)

project(ASock)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 11)
#set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)

#ADD_DEFINITIONS ( -DDEBUG_PRINT ) # for terminal debugging

set(CMAKE_VERBOSE_MAKEFILE true)

if(WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /nologo /W3 /Gy /EHsc")
  set(CMAKE_CXX_FLAGS_DEBUG           " /MTd /ZI /Od ")
  set(CMAKE_CXX_FLAGS_RELEASE         " /MT /O2 ")
  link_libraries(ws2_32 wsock32)
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

if(WIN32)
  add_definitions(/wd4244 /wd4324 /wd4819 -D_CRT_SECURE_NO_WARNINGS)
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
  add_definitions(-Wall)
endif()

if(WIN32)
  add_subdirectory(sample/tcp_echo_example/composition)
  add_subdirectory(sample/tcp_echo_example/inheritance)
  add_subdirectory(sample/tcp_send_each_other_example)
  add_subdirectory(sample/udp_echo_example/composition)
  add_subdirectory(sample/udp_echo_example/inheritance)
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
  add_subdirectory(sample/ipc_echo_example/inheritance)
  add_subdirectory(sample/ipc_echo_example/composition)
  add_subdirectory(sample/tcp_echo_example/inheritance)
  add_subdirectory(sample/tcp_echo_example/composition)
  add_subdirectory(sample/udp_echo_example/inheritance)
  add_subdirectory(sample/udp_echo_example/composition)
  add_subdirectory(sample/tcp_send_each_other_example)
endif()
