add_executable(phase2_smoke_test
  phase2_smoke_test.cpp
)

target_compile_features(phase2_smoke_test PRIVATE cxx_std_17)
target_link_libraries(phase2_smoke_test PRIVATE benchmark_ffi)

add_test(NAME phase2_smoke_test COMMAND phase2_smoke_test)

add_executable(topology_grouping_test
  topology_grouping_test.cpp
)

target_compile_features(topology_grouping_test PRIVATE cxx_std_17)
target_link_libraries(topology_grouping_test PRIVATE benchmark_core)

add_test(NAME topology_grouping_test COMMAND topology_grouping_test)


