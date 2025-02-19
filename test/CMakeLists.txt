cmake_minimum_required(VERSION 3.14)

project(asocktest)

set(CMAKE_VERBOSE_MAKEFILE true)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

#set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)
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
  add_definitions(-Wall -Werror -Wextra -Wpedantic)
endif()

if(WIN32)
  set(TEST_FILES
    buffer_test.cpp
    tcp_test.cpp
    udp_test.cpp
  )
elseif(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(TEST_FILES
    buffer_test.cpp
    tcp_test.cpp
    udp_test.cpp
    ipc_test.cpp
  )
endif()

enable_testing()
find_package(GTest CONFIG REQUIRED)

foreach(testfile ${TEST_FILES})
  get_filename_component(exename ${testfile} NAME_WE)
  add_executable(${exename} ${testfile})
  target_link_libraries(${exename} PRIVATE GTest::gtest GTest::gtest_main asock)
  add_test(AllTestsInMain ${exename})
endforeach()

