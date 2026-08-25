cmake_minimum_required(VERSION 3.18)
project(cpu_benchmark LANGUAGES C CXX)

option(BM_BUILD_TESTS "Build native smoke tests" ON)

add_subdirectory(native)

