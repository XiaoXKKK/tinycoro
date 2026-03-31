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

# macOS requires _XOPEN_SOURCE for ucontext APIs (deprecated since 10.6 but still functional)
if(APPLE)
  add_compile_definitions(_XOPEN_SOURCE=600)
  add_compile_options(-Wno-deprecated-declarations)
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

# Tests — use FetchContent to automatically pull GoogleTest if not installed
enable_testing()
find_package(GTest QUIET)
if(GTest_FOUND)
  message(STATUS "Found system GoogleTest")
else()
  message(STATUS "Fetching GoogleTest via FetchContent")
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  set(GTest_FOUND TRUE)
  # Alias to match find_package targets
  if(NOT TARGET GTest::GTest)
    add_library(GTest::GTest ALIAS gtest)
    add_library(GTest::Main  ALIAS gtest_main)
  endif()
endif()

file(GLOB TEST_SRCS "tests/*.cpp")
add_executable(run_tests ${TEST_SRCS})
target_link_libraries(run_tests PRIVATE tinycoro GTest::GTest GTest::Main)
add_test(NAME unit_tests COMMAND run_tests)
