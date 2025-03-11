cmake_minimum_required(VERSION 3.14)

project(asocktest)

set(CMAKE_VERBOSE_MAKEFILE true)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

if(WIN32)
  link_libraries(ws2_32 wsock32)
  add_definitions(/wd4819)
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  add_definitions(-Wall -Werror -Wextra -Wpedantic)
endif()

find_package(GTest CONFIG REQUIRED)
add_executable(asocktest asocktest.cpp)
target_link_libraries(asocktest PRIVATE GTest::gtest GTest::gtest_main asock)
