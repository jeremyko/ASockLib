cmake_minimum_required(VERSION 3.14)

project(asock
        VERSION 1.0.0
        DESCRIPTION "A C++ header only portable networking library"
        LANGUAGES CXX)

set(PROJECT_AUTHOR "jeremyko")
set(CMAKE_VERBOSE_MAKEFILE true)

set(JEREMYKO_ASOCK_IS_MAIN OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(JEREMYKO_ASOCK_IS_MAIN ON)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(asock INTERFACE)
add_library(asock::asock ALIAS asock)

# --------------------------------------------------------------------- testing
option(JEREMYKO_ASOCK_BUILD_TESTS "Build tests" ${JEREMYKO_ASOCK_IS_MAIN})
if(JEREMYKO_ASOCK_BUILD_TESTS)
  add_subdirectory(test)
endif()

# --------------------------------------------------------------------- samples
option(JEREMYKO_ASOCK_BUILD_SAMPLES "Build samples" ${JEREMYKO_ASOCK_IS_MAIN})
if(JEREMYKO_ASOCK_BUILD_SAMPLES)
  add_subdirectory(sample)
endif()

# --------------------------------------------------------------------- install
target_include_directories(
    ${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${${PROJECT_NAME}_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

option(JEREMYKO_ASOCK_INSTALL "install target" ${JEREMYKO_ASOCK_IS_MAIN})
if(JEREMYKO_ASOCK_INSTALL)

  install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.*pp")

  install(TARGETS asock asock-headers EXPORT asockTargets)

  install(EXPORT asockTargets
    FILE asock-targets.cmake
    NAMESPACE asock::
    DESTINATION share/cmake/asock)

  write_basic_package_version_file("asock-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/asock-config.cmake.in"
    "${PROJECT_BINARY_DIR}/asock-config.cmake"
    @ONLY)

  install(FILES
    "${PROJECT_BINARY_DIR}/asock-config-version.cmake"
    "${PROJECT_BINARY_DIR}/asock-config.cmake"
    DESTINATION share/cmake/asock)

endif()
