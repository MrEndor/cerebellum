include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")

if (CEREBELLUM_BUILD_DATAPLANE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
endif ()

CPMAddPackage(NAME yaml-cpp GITHUB_REPOSITORY jbeder/yaml-cpp GIT_TAG 0.8.0
        OPTIONS "YAML_CPP_BUILD_TESTS OFF" "YAML_CPP_BUILD_TOOLS OFF"
        "CMAKE_POLICY_VERSION_MINIMUM 3.5")
if (TARGET yaml-cpp)
    target_compile_options(yaml-cpp PRIVATE -include cstdint)
endif ()
if (CEREBELLUM_BUILD_CONTROLPLANE)
    find_package(userver QUIET COMPONENTS core)
    if (NOT userver_FOUND)
        set(_cere_saved_module_path "${CMAKE_MODULE_PATH}")
        list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
        CPMAddPackage(NAME userver
                GITHUB_REPOSITORY userver-framework/userver
                GIT_TAG v3.0
                OPTIONS
                "USERVER_FEATURE_MONGODB OFF"
                "USERVER_FEATURE_POSTGRESQL OFF"
                "USERVER_FEATURE_REDIS OFF"
                "USERVER_FEATURE_GRPC OFF"
                "USERVER_FEATURE_CLICKHOUSE OFF"
                "USERVER_FEATURE_RABBITMQ OFF"
                "USERVER_FEATURE_KAFKA OFF"
                "USERVER_FEATURE_MYSQL OFF"
                "USERVER_FEATURE_TESTSUITE OFF"
                "USERVER_FEATURE_UTEST OFF"
                "USERVER_FEATURE_JEMALLOC OFF"
                "USERVER_FEATURE_STACKTRACE OFF"
                "USERVER_IS_THE_ROOT_PROJECT OFF")
        set(CMAKE_MODULE_PATH "${_cere_saved_module_path}")
        foreach (_uv userver-core userver-universal)
            if (TARGET ${_uv})
                set_target_properties(${_uv} PROPERTIES SYSTEM TRUE)
            endif ()
        endforeach ()
    endif ()
endif ()

CPMAddPackage(NAME CLI11 GITHUB_REPOSITORY CLIUtils/CLI11 GIT_TAG v2.4.2)
if (TARGET CLI11)
    set_target_properties(CLI11 PROPERTIES SYSTEM TRUE)
endif ()

if (CEREBELLUM_BUILD_TESTS)
    CPMAddPackage(NAME Catch2 GITHUB_REPOSITORY catchorg/Catch2 GIT_TAG v3.6.0
            OPTIONS "CATCH_INSTALL_DOCS OFF")
    if (Catch2_ADDED)
        list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
    endif ()
endif ()
