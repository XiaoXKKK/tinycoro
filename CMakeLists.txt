cmake_minimum_required(VERSION 3.16)
project(tinycoro VERSION 0.2.0 LANGUAGES CXX)

option(TINYCORO_BUILD_EXAMPLES "Build example servers" ON)
option(TINYCORO_BUILD_TESTS "Build tests" ON)
option(TINYCORO_BUILD_BENCHMARKS "Build microbenchmarks" OFF)
option(TINYCORO_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSanitizer" OFF)
option(TINYCORO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT UNIX)
  message(FATAL_ERROR "tinycoro supports Linux and macOS/BSD only")
endif()

find_package(Threads REQUIRED)

set(TINYCORO_SOURCES
  src/coroutine.cpp
  src/coroutine_pool.cpp
  src/event_loop.cpp
  src/http_parser.cpp
  src/io_context.cpp
  src/scheduler.cpp
  src/tcp_connection.cpp
  src/tcp_server.cpp
  src/tcp_stream.cpp
  src/thread_pool.cpp
)

add_library(tinycoro STATIC ${TINYCORO_SOURCES})
add_library(tinycoro::tinycoro ALIAS tinycoro)
target_include_directories(tinycoro
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(tinycoro PUBLIC Threads::Threads)
target_compile_features(tinycoro PUBLIC cxx_std_17)

if(APPLE)
  target_compile_definitions(tinycoro PUBLIC _XOPEN_SOURCE=600)
  target_compile_options(tinycoro PRIVATE -Wno-deprecated-declarations)
endif()

function(tinycoro_configure_target target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(TINYCORO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  if(TINYCORO_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      message(FATAL_ERROR "Sanitizers require GCC or Clang")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

tinycoro_configure_target(tinycoro)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tinycoro PRIVATE -Wno-cast-function-type)
endif()

if(TINYCORO_BUILD_EXAMPLES)
  foreach(example IN ITEMS echo_server http_echo coro_echo_server coro_http_echo)
    add_executable(${example} examples/${example}.cpp)
    target_link_libraries(${example} PRIVATE tinycoro::tinycoro)
    tinycoro_configure_target(${example})
  endforeach()
endif()

if(TINYCORO_BUILD_BENCHMARKS)
  add_executable(coroutine_switch_bench benchmarks/coroutine_switch.cpp)
  target_link_libraries(coroutine_switch_bench PRIVATE tinycoro::tinycoro)
  tinycoro_configure_target(coroutine_switch_bench)
endif()

if(TINYCORO_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(run_tests
    tests/test_coroutine.cpp
    tests/test_http.cpp
    tests/test_io_context.cpp
    tests/test_pool.cpp
    tests/test_queue.cpp
  )
  target_link_libraries(run_tests PRIVATE tinycoro::tinycoro GTest::gtest_main)
  tinycoro_configure_target(run_tests)
  include(GoogleTest)
  gtest_discover_tests(run_tests DISCOVERY_TIMEOUT 30)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(TINYCORO_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/tinycoro)

configure_package_config_file(
  cmake/tinycoroConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/tinycoroConfig.cmake
  INSTALL_DESTINATION ${TINYCORO_CMAKE_INSTALL_DIR}
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/tinycoroConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(TARGETS tinycoro EXPORT tinycoroTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT tinycoroTargets
  FILE tinycoroTargets.cmake
  NAMESPACE tinycoro::
  DESTINATION ${TINYCORO_CMAKE_INSTALL_DIR}
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/tinycoroConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/tinycoroConfigVersion.cmake
  DESTINATION ${TINYCORO_CMAKE_INSTALL_DIR}
)