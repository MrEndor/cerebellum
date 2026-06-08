# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS builder

ENV CCACHE_DIR=/ccache
ENV CPM_SOURCE_CACHE=/cpm

SHELL ["/bin/bash", "-eo", "pipefail", "-c"]

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      ccache \
      cmake \
      ninja-build \
      pkg-config \
      git \
      ca-certificates \
      libdpdk-dev \
      libzstd-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

RUN --mount=type=cache,target=/ccache \
    --mount=type=cache,target=/cpm \
    cmake --preset release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCEREBELLUM_BUILD_CONTROLPLANE=OFF \
 && cmake --build --preset release --target cerebellum_dataplane \
 && strip build/dataplane/cerebellum_dataplane

FROM ubuntu:24.04

SHELL ["/bin/bash", "-eo", "pipefail", "-c"]

RUN apt-get update \
 && apt-get install -y --no-install-recommends tini \
 && apt-get install -y dpdk librte-net-af-xdp24 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/dataplane/cerebellum_dataplane /usr/local/bin/
COPY config.yaml /etc/cerebellum/config.yaml

ENTRYPOINT ["tini", "--", "cerebellum_dataplane", "--config", "/etc/cerebellum/config.yaml"]
