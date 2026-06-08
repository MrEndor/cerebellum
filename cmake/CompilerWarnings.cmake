include_guard(GLOBAL)

add_library(cerebellum_compiler_warnings INTERFACE)
target_compile_options(cerebellum_compiler_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Werror>)
