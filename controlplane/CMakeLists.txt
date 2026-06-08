add_executable(cerebellum_controlplane
    src/monitor/monitor_component.cpp
    src/api/stats_handler.cpp
    src/api/backends_handler.cpp
    src/health/health_checker.cpp
    src/stats/stats_aggregator.cpp
    src/main.cpp)

target_include_directories(cerebellum_controlplane PRIVATE include)

target_link_libraries(cerebellum_controlplane PRIVATE
    cerebellum_project_options
    cerebellum_compiler_warnings
    common
    config_loader
    ipc
    userver::core)
