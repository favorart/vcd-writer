cmake_minimum_required(VERSION 3.21)
project(vcdwriter)

# Build options
option(VCDWRITER_BUILD_MAIN "Build the main executable" ON)
option(VCDWRITER_BUILD_TESTS "Build unit tests" ON)

# C++ settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Paths
set(SRC_PATH "${CMAKE_SOURCE_DIR}/src")
set(INCLUDE_PATH "${CMAKE_SOURCE_DIR}/include")
set(TEST_PATH "${CMAKE_SOURCE_DIR}/test")
set(BUILD_PATH "${CMAKE_BINARY_DIR}")

include_directories(${INCLUDE_PATH})

# Dependencies
include(FetchContent)

# Microsoft GSL
FetchContent_Declare(
  GSL
  GIT_REPOSITORY https://github.com/microsoft/GSL.git
  GIT_TAG v4.0.0
)
FetchContent_MakeAvailable(GSL)

# GoogleTest (optional)
if (VCDWRITER_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  FetchContent_MakeAvailable(googletest)
  enable_testing()
endif()

# Shared library
add_library(vcdwriter_shared SHARED ${SOURCE_FILES})
target_include_directories(vcdwriter_shared PUBLIC ${INCLUDE_PATH})
target_link_libraries(vcdwriter_shared PUBLIC GSL)
add_library(vcdwriter::vcdwriter_shared ALIAS vcdwriter_shared)

# Static library
add_library(vcdwriter_static STATIC ${SOURCE_FILES})
target_include_directories(vcdwriter_static PUBLIC ${INCLUDE_PATH})
target_link_libraries(vcdwriter_static PUBLIC GSL)

# Output directories
set_target_properties(
  vcdwriter_shared vcdwriter_static
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_PATH}
    ARCHIVE_OUTPUT_DIRECTORY ${BUILD_PATH}
    LIBRARY_OUTPUT_DIRECTORY ${BUILD_PATH}
)

# Main executable (optional)
if (VCDWRITER_BUILD_MAIN AND EXISTS "${TEST_PATH}/main.cpp")
  add_executable(main_exec "${TEST_PATH}/main.cpp")
  target_link_libraries(main_exec PRIVATE vcdwriter_shared)
  set_target_properties(main_exec PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD_PATH})
endif()

# Unit tests (optional)
if (VCDWRITER_BUILD_TESTS AND EXISTS "${TEST_PATH}/vcd_tests.cpp")
  add_executable(test_exec "${TEST_PATH}/vcd_tests.cpp")
  target_link_libraries(test_exec PRIVATE vcdwriter_shared GTest::gtest GTest::gtest_main pthread)
  add_test(NAME vcdwriter_tests COMMAND test_exec)
  set_target_properties(test_exec PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD_PATH})
endif()

