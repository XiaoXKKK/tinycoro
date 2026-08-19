cmake_minimum_required(VERSION 3.16)
project(tinycoro_package_consumer LANGUAGES CXX)

find_package(tinycoro CONFIG REQUIRED)
add_executable(package_consumer main.cpp)
target_link_libraries(package_consumer PRIVATE tinycoro::tinycoro)