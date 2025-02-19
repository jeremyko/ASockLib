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

option(JEREMYKO_ASOCK_INSTALL "install target" ${JEREMYKO_ASOCK_IS_MAIN})
if(JEREMYKO_ASOCK_INSTALL)

  target_include_directories(
      ${PROJECT_NAME} INTERFACE
      $<BUILD_INTERFACE:${${PROJECT_NAME}_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

  target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_11)

  install(TARGETS ${PROJECT_NAME}
          EXPORT ${PROJECT_NAME}_Targets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file("${PROJECT_NAME}ConfigVersion.cmake"
                                  VERSION ${PROJECT_VERSION}
                                  COMPATIBILITY SameMajorVersion)

  configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  INSTALL_DESTINATION
  ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

  install(EXPORT ${PROJECT_NAME}_Targets
          FILE ${PROJECT_NAME}Targets.cmake
          NAMESPACE ${PROJECT_NAME}::
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

  install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
              "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

  install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/asock DESTINATION include)

  if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
    include(CPack)
  endif()
endif()
