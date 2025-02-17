cmake_minimum_required(VERSION 3.14)

project(asock
        DESCRIPTION "A C++ header only portable networking library"
        LANGUAGES CXX)

set(PROJECT_AUTHOR "jeremyko")
set(CMAKE_VERBOSE_MAKEFILE true)

add_library(asock INTERFACE)
add_library(asock::asock ALIAS asock)

target_include_directories(asock INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

#set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

#add_definitions( -DDEBUG_PRINT ) # for debugging log

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
  add_definitions(-Werror)
  add_definitions(-Wextra)
  # include_directories(/usr/local/include)
endif()

# --------------------------------------------------------------------- install
# install asock
if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
include(GNUInstallDirs)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/asock
        DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
endif()

# --------------------------------------------------------------------- samples
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

# --------------------------------------------------------------------- tests
# tests
add_subdirectory(test/tcp_test)
add_subdirectory(test/udp_test)
add_subdirectory(test/buffer_test)

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
add_subdirectory(test/ipc_test)
endif()

