# gtest requires c++14. AsockLib does not.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  add_definitions(-std=c++14)
endif()

add_executable(test_ipc ipc_test.cpp)
target_link_libraries(test_ipc gtest asock)


