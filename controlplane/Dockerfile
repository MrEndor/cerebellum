# syntax=docker/dockerfile:1
FROM ghcr.io/userver-framework/ubuntu-24.04-userver:latest AS builder

ENV CCACHE_DIR=/ccache
ENV CPM_SOURCE_CACHE=/cpm

SHELL ["/bin/bash", "-eo", "pipefail", "-c"]

WORKDIR /src

COPY . .

RUN --mount=type=cache,target=/ccache \
    --mount=type=cache,target=/cpm \
    cmake --preset release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCEREBELLUM_BUILD_DATAPLANE=OFF \
 && cmake --build --preset release --target cerebellum_controlplane \
 && strip build/controlplane/cerebellum_controlplane

FROM ubuntu:24.04

SHELL ["/bin/bash", "-eo", "pipefail", "-c"]

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      tini \
      curl \
      libboost-filesystem1.83.0 \
      libboost-iostreams1.83.0 \
      libboost-program-options1.83.0 \
      libboost-stacktrace1.83.0 \
      libcctz2 \
      libcrypto++8t64 \
      libcurl4t64 \
      libev4t64 \
      libfmt9 \
      libnghttp2-14 \
      libre2-10 \
      libjemalloc2 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 cere

COPY --from=builder /src/build/controlplane/cerebellum_controlplane /usr/local/bin/
COPY controlplane/static_config.yaml /etc/cerebellum/static_config.yaml
COPY config.yaml /etc/cerebellum/config.yaml

USER cere

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD curl -fsS http://127.0.0.1:8080/api/v1/stats -o /dev/null || exit 1

ENTRYPOINT ["tini", "--", "cerebellum_controlplane", "--config", "/etc/cerebellum/static_config.yaml"]
