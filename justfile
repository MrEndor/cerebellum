set shell := ["bash", "-eou", "pipefail", "-c"]

default:
    @just --list

build:
    cmake --preset release
    cmake --build --preset release --parallel

test:
    cmake --preset debug-asan
    cmake --build --preset debug-asan --parallel
    ctest --preset debug-asan

coverage:
    cmake --preset coverage
    cmake --build --preset coverage --parallel
    ctest --preset coverage
    cmake --build --preset coverage --target coverage_report

fmt:
    find dataplane controlplane libs -name '*.hpp' -o -name '*.cpp' \
        | xargs --no-run-if-empty clang-format -i

fmt-check:
    find dataplane controlplane libs -name '*.hpp' -o -name '*.cpp' \
        | xargs --no-run-if-empty clang-format --dry-run --Werror

lint:
    find dataplane controlplane libs -name '.hpp' -o -name '*.cpp' \
        | xargs --no-run-if-empty clang-tidy -p build

up:
    docker compose up --build

down:
    docker compose down

clean:
    rm -rf build build-test build-cov
