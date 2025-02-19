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
include(GNUInstallDirs)
target_include_directories(
    ${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${${PROJECT_NAME}_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

option(JEREMYKO_ASOCK_INSTALL "install target" ${JEREMYKO_ASOCK_IS_MAIN})
if(JEREMYKO_ASOCK_INSTALL)
  install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  # (Generate and) install the target import file
  install(
    EXPORT asockTargets
    NAMESPACE asock::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asock
  )

  # Generate the package version file
  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/asockConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
  )

  # Generate the package configuration file, that allows other
  # CMake projects to find the library with find_package()
  configure_package_config_file(
    cmake/asockConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/asockConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asock
  )

# Install the package version and configuration files
  install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/asockConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/asockConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asock
  )

  # Install readme and license
  install(
    FILES
    README.md
    LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/asock
  )
endif()
