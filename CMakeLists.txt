cmake_minimum_required(VERSION 3.16)
project(tinycoro VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined")

# Platform check
if(NOT UNIX)
  message(FATAL_ERROR "tinycoro only supports Linux/macOS (epoll or kqueue)")
endif()

# Core library
file(GLOB_RECURSE TINYCORO_SRCS "src/*.cpp")
add_library(tinycoro STATIC ${TINYCORO_SRCS})
target_include_directories(tinycoro PUBLIC include)

# Examples
add_executable(echo_server examples/echo_server.cpp)
target_link_libraries(echo_server PRIVATE tinycoro)

add_executable(http_echo examples/http_echo.cpp)
target_link_libraries(http_echo PRIVATE tinycoro)

# Tests
enable_testing()
find_package(GTest QUIET)
if(GTest_FOUND)
  file(GLOB TEST_SRCS "tests/*.cpp")
  add_executable(run_tests ${TEST_SRCS})
  target_link_libraries(run_tests PRIVATE tinycoro GTest::GTest GTest::Main)
  add_test(NAME unit_tests COMMAND run_tests)
else()
  message(STATUS "GoogleTest not found, skipping tests")
endif()
