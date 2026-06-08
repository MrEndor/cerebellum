add_library(graph_engine STATIC
    src/graph/frame_pool.cpp
    src/graph/graph.cpp
    src/graph/dispatcher.cpp)
target_include_directories(graph_engine PUBLIC include)
target_link_libraries(graph_engine PRIVATE
    cerebellum_project_options
    cerebellum_compiler_warnings)

add_executable(cerebellum_dataplane
    src/main.cpp
    src/io/eal.cpp
    src/io/dpdk_port.cpp
    src/nodes/rx_node.cpp
    src/nodes/ip4_parse_node.cpp
    src/nodes/arp_node.cpp
    src/nodes/steer_node.cpp
    src/nodes/lb_node.cpp
    src/nodes/dnat_node.cpp
    src/nodes/ether_rewrite_node.cpp
    src/nodes/tx_node.cpp)
target_include_directories(cerebellum_dataplane PRIVATE include)
target_link_libraries(cerebellum_dataplane PRIVATE
    cerebellum_project_options
    cerebellum_compiler_warnings
    graph_engine
    common
    ipc
    config_loader
    CLI11::CLI11
    PkgConfig::DPDK)

if(CEREBELLUM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
