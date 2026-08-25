find_package(Threads REQUIRED)

add_subdirectory(benchmark_core)
add_subdirectory(benchmark_ffi)

if(BM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
