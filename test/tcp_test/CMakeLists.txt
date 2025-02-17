# gtest requires c++14. AsockLib does not.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_tcp tcp_test.cpp)
if(APPLE)
  add_definitions(-std=c++14)
  include_directories(/usr/local/include)
  find_library(GTEST_LIBRARY
    NAMES gtest
    PATHS /usr/lib /usr/local/lib
  )
  if(GTEST_LIBRARY)
    target_link_libraries(test_tcp LINK_PUBLIC ${GTEST_LIBRARY} asock)
  endif()
else()
target_link_libraries(test_tcp gtest asock)
endif()

