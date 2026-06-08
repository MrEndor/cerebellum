include_guard(GLOBAL)

add_library(cerebellum_project_options INTERFACE)
target_compile_features(cerebellum_project_options INTERFACE cxx_std_23)
set_target_properties(cerebellum_project_options
    PROPERTIES INTERFACE_CXX_EXTENSIONS OFF)

option(CEREBELLUM_BUILD_TESTS       "Build unit tests"               OFF)
option(CEREBELLUM_ENABLE_SANITIZERS "Enable ASAN + UBSAN"            OFF)
option(CEREBELLUM_ENABLE_COVERAGE   "Instrument for coverage"        OFF)

include("${CMAKE_CURRENT_LIST_DIR}/CompilerWarnings.cmake")
target_link_libraries(cerebellum_project_options INTERFACE cerebellum_compiler_warnings)
