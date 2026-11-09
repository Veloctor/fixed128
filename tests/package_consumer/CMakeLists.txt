# Checks that an installed fixed128 can be consumed with find_package (run by CI, not part of the main build).
cmake_minimum_required(VERSION 3.20)
project(fixed128_package_consumer LANGUAGES CXX)

find_package(fixed128 REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE fixed128::fixed128)
