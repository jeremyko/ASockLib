add_executable(test_ipc ipc_test.cpp)

# gtest requires c++14. AsockLib does not.
set(CMAKE_CXX_STANDARD 14)
if(APPLE)
  add_definitions(-std=c++14)
endif()

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Linux")
  find_library(GTEST_LIBRARY
    NAMES gtest
    PATHS /usr/lib /usr/local/lib
  )
  if(GTEST_LIBRARY)
    target_link_libraries(test_ipc LINK_PUBLIC ${GTEST_LIBRARY})
  endif()
endif()

