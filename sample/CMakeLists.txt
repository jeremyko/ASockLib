cmake_minimum_required(VERSION 3.14)

project(asocksample)

set(CMAKE_VERBOSE_MAKEFILE true)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

if(WIN32)
  set(CMAKE_CXX_FLAGS_RELEASE  "/O1 ")
  link_libraries(ws2_32 wsock32)
  add_definitions(/wd4819)
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  add_definitions(-Wall -Werror -Wextra -Wpedantic)
endif()

set(SAMPLE_FILES
  client_tcp_comp.cpp
  client_tcp_eo.cpp
  client_tcp_inh.cpp
  client_udp_comp.cpp
  client_udp_inh.cpp
  server_tcp_comp.cpp
  server_tcp_eo.cpp
  server_tcp_inh.cpp
  server_udp_comp.cpp
  server_udp_inh.cpp
  client_ipc_comp.cpp
  client_ipc_inh.cpp
  server_ipc_comp.cpp
  server_ipc_inh.cpp
)

foreach(samplefile ${SAMPLE_FILES})
  get_filename_component(exename ${samplefile} NAME_WE)
  add_executable(${exename} ${samplefile})
  target_link_libraries(${exename} PRIVATE asock)
endforeach()

