add_executable(cerebellum_tests
    test_flow_key.cpp
    test_backend_pool.cpp
    test_control_view.cpp
    test_port_pool.cpp
    test_nat_table.cpp
    test_graph.cpp
    test_net_types.cpp
)

target_include_directories(cerebellum_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/dataplane/include)

target_link_libraries(cerebellum_tests PRIVATE
    cerebellum_project_options
    cerebellum_compiler_warnings
    graph_engine
    common
    ipc
    Catch2::Catch2WithMain)

cerebellum_enable_sanitizers(cerebellum_tests)
cerebellum_enable_coverage(cerebellum_tests)

include(CTest)
include(Catch)
catch_discover_tests(cerebellum_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
