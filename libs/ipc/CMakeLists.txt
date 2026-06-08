add_library(ipc STATIC src/shm_provider.cpp)
target_include_directories(ipc PUBLIC include)
target_link_libraries(ipc PRIVATE cerebellum_project_options
                                  cerebellum_compiler_warnings)
