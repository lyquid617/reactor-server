cmake_minimum_required(VERSION 3.14)
project(ThreadPoolTest)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/repo/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

set(VCPKG_TARGET_TRIPLET "arm64-linux" CACHE STRING "Vcpkg target triplet")

find_package(GTest REQUIRED)

# this is like an object
add_executable(test_threadpool test.cpp)

add_library(threadpool INTERFACE)

target_include_directories(threadpool INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(test_threadpool PRIVATE threadpool GTest::gtest_main)

enable_testing()
add_test(NAME AllTests COMMAND test_threadpool)