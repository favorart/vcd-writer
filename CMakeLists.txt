cmake_minimum_required(VERSION 3.21)
project(vcdwriter LANGUAGES CXX)

# Build options
option(VCDWRITER_BUILD_MAIN "Build the main executable" ON)
option(VCDWRITER_BUILD_TESTS "Build unit tests" ON)
option(VCDWRITER_BUILD_BENCH "Build the benchmark" OFF)
option(VCDWRITER_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)

# C++ settings: std::format requires C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# export all symbols of the shared library on Windows (no __declspec annotations in the code)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Paths
set(SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(TEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/test")
# generator expression: multi-config generators (Visual Studio, Xcode) do not append a per-config subdirectory
set(BUILD_PATH "$<1:${CMAKE_BINARY_DIR}>")

# Fail early with a clear message if the compiler/standard library has no std::format
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX20_STANDARD_COMPILE_OPTION}")
check_cxx_source_compiles("
  #include <format>
  int main() { return std::format(\"{}\", 42).size() == 2 ? 0 : 1; }
" VCDWRITER_HAS_STD_FORMAT)
unset(CMAKE_REQUIRED_FLAGS)
if (NOT VCDWRITER_HAS_STD_FORMAT)
  message(FATAL_ERROR "vcdwriter requires a C++20 compiler with <format> "
                      "(GCC >= 13, Clang >= 17 with libc++, MSVC >= 19.29)")
endif()

# Dependencies
include(FetchContent)

# GoogleTest (optional)
if (VCDWRITER_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  # use the same runtime library as the project on MSVC
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
endif()

set(SOURCE_FILES
  "${SRC_PATH}/vcd_writer.cpp"
  "${SRC_PATH}/vcd_header.cpp"
  "${SRC_PATH}/vcd_variable.cpp"
  "${SRC_PATH}/vcd_utils.cpp"
)

# Common settings of the project targets: C++20, warnings, sanitizers
function(vcdwriter_setup_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_PATH}
    ARCHIVE_OUTPUT_DIRECTORY ${BUILD_PATH}
    LIBRARY_OUTPUT_DIRECTORY ${BUILD_PATH}
  )
  if (MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
    # std::localtime and friends are standard C++, silence MSVC "unsafe CRT function" warnings
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if (VCDWRITER_SANITIZE)
      target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
      target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

# Shared library
add_library(vcdwriter_shared SHARED ${SOURCE_FILES})
target_include_directories(vcdwriter_shared PUBLIC ${INCLUDE_PATH})
vcdwriter_setup_target(vcdwriter_shared)
add_library(vcdwriter::vcdwriter_shared ALIAS vcdwriter_shared)

# Static library
add_library(vcdwriter_static STATIC ${SOURCE_FILES})
target_include_directories(vcdwriter_static PUBLIC ${INCLUDE_PATH})
vcdwriter_setup_target(vcdwriter_static)
add_library(vcdwriter::vcdwriter_static ALIAS vcdwriter_static)

# Main executable (optional)
if (VCDWRITER_BUILD_MAIN AND EXISTS "${TEST_PATH}/main.cpp")
  add_executable(main_exec "${TEST_PATH}/main.cpp")
  target_link_libraries(main_exec PRIVATE vcdwriter_static)
  vcdwriter_setup_target(main_exec)
endif()

# Unit tests (optional)
if (VCDWRITER_BUILD_TESTS AND EXISTS "${TEST_PATH}/vcd_tests.cpp")
  add_executable(test_exec "${TEST_PATH}/vcd_tests.cpp")
  # vcd_tests.cpp has its own main(), so GTest::gtest_main is not linked
  target_link_libraries(test_exec PRIVATE vcdwriter_static GTest::gtest)
  vcdwriter_setup_target(test_exec)
  add_test(NAME vcdwriter_tests COMMAND test_exec WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# Benchmark (optional)
if (VCDWRITER_BUILD_BENCH)
  add_executable(vcd_bench "${CMAKE_CURRENT_SOURCE_DIR}/bench/vcd_bench.cpp")
  target_link_libraries(vcd_bench PRIVATE vcdwriter_static)
  vcdwriter_setup_target(vcd_bench)
endif()
