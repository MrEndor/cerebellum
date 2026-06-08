include_guard(GLOBAL)

function(cerebellum_enable_coverage target)
    if(NOT CEREBELLUM_ENABLE_COVERAGE)
        return()
    endif()
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
endfunction()

if(CEREBELLUM_ENABLE_COVERAGE)
    find_program(GCOVR gcovr)
    if(GCOVR)
        add_custom_target(coverage_report
            COMMAND ${GCOVR}
                --root "${CMAKE_SOURCE_DIR}"
                --filter "${CMAKE_SOURCE_DIR}/dataplane/"
                --filter "${CMAKE_SOURCE_DIR}/controlplane/"
                --filter "${CMAKE_SOURCE_DIR}/libs/"
                --html-details "${CMAKE_BINARY_DIR}/coverage/index.html"
                --print-summary --fail-under-line 70
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Generating coverage report")
    endif()
endif()
