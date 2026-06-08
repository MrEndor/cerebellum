add_library(common STATIC src/net_types.cpp)
target_include_directories(common PUBLIC include)
target_link_libraries(common PRIVATE cerebellum_project_options
                                     cerebellum_compiler_warnings)

add_library(config_loader STATIC src/config_loader.cpp)
target_link_libraries(config_loader
    PUBLIC  common
    PRIVATE cerebellum_project_options
            cerebellum_compiler_warnings
            yaml-cpp::yaml-cpp)
