# gtest requires c++14. asock does not.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  add_definitions(-std=c++14)
endif()

add_executable(test_buffer buffer_test.cpp)
target_link_libraries(test_buffer gtest asock)

