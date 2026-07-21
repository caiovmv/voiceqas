# syntax=docker/dockerfile:1
#
# voiceqas APP image — compiles only your code against voiceqas-build-base.
#
# Prerequisite (once, or when vcpkg.json changes):
#   docker build -f Dockerfile.base -t voiceqas-build-base:local .
#   ./scripts/build-base.ps1
#
# Then:
#   docker compose build voiceqas
#
# =============================================================================
# Stack (jul/2026): C++26, GCC 15, CMake >= 3.31, vcpkg pinned in base image
# =============================================================================

ARG BASE_IMAGE=voiceqas-build-base:local
ARG GCC_VERSION=15
ARG CMAKE_MIN_VERSION=3.31
ARG CPP_STD=c++26
ARG VCPKG_BASELINE=d87340acc46bdeda386037b38aca30136e667e47
ARG DEBIAN_RUNTIME=trixie-slim

ARG NLOHMANN_JSON_VERSION=3.12.0
ARG CPP_HTTPLIB_VERSION=0.48.0
ARG YAML_CPP_VERSION=0.9.0
ARG GTEST_VERSION=1.17.0
ARG VOICEQAS_STT_CUDA=0
ARG VOICEQAS_PERF_BADASS=OFF

# -----------------------------------------------------------------------------
# Stage 1: compile app against baked vcpkg install
# -----------------------------------------------------------------------------
FROM ${BASE_IMAGE} AS builder

ARG GCC_VERSION
ARG CPP_STD
ARG VCPKG_BASELINE
ARG NLOHMANN_JSON_VERSION
ARG CPP_HTTPLIB_VERSION
ARG YAML_CPP_VERSION
ARG GTEST_VERSION
ARG VOICEQAS_STT_CUDA
ARG VOICEQAS_PERF_BADASS=OFF
ARG VOICEQAS_BUILD_TESTS=OFF

ENV VOICEQAS_BUILD_TESTS=${VOICEQAS_BUILD_TESTS} \
    VCPKG_INSTALLED_DIR=/opt/vcpkg-installed

SHELL ["/bin/bash", "-eo", "pipefail", "-c"]

WORKDIR /src

# scaffolding (rarely changes)
COPY CMakeLists.txt ./
COPY third_party/ third_party/
COPY proto/ proto/

# application code (invalidates build on every code change)
COPY include/ include/
COPY src/ src/
COPY tests/ tests/

# No vcpkg install here — deps live in BASE_IMAGE at /opt/vcpkg-installed
RUN --mount=type=cache,target=/src/build/_deps,id=voiceqas-fetchcontent-shared-ort \
    --mount=type=cache,target=/root/.ccache,id=voiceqas-ccache \
    bash -c 'CMAKE_EXTRA=(); \
      if [[ "${VOICEQAS_BUILD_TESTS}" == "ON" ]]; then CMAKE_EXTRA+=(-DVCPKG_MANIFEST_FEATURES=test); fi; \
      cmake -B build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DVOICEQAS_BUILD_TESTS="${VOICEQAS_BUILD_TESTS}" \
        -DVOICEQAS_ENABLE_STT=ON \
        -DVOICEQAS_STT_CUDA=${VOICEQAS_STT_CUDA} \
        -DVOICEQAS_PERF_BADASS=${VOICEQAS_PERF_BADASS} \
        -DVCPKG_INSTALLED_DIR=/opt/vcpkg-installed \
        -DVCPKG_MANIFEST_INSTALL=OFF \
        "${CMAKE_EXTRA[@]}" \
    && cmake --build build -j"$(nproc)" \
    && if [[ "${VOICEQAS_BUILD_TESTS}" == "ON" ]]; then cd build && ctest --output-on-failure; fi'

RUN { \
  echo "# voiceqas build versions"; \
  echo "generated_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
  echo "cpp_standard: ${CPP_STD}"; \
  echo "vcpkg_baseline: ${VCPKG_BASELINE}"; \
  g++ --version | head -1 | sed 's/^/gcc: /'; \
  cmake --version | head -1 | sed 's/^/cmake: /'; \
  echo "vcpkg_triplet: ${VCPKG_DEFAULT_TRIPLET}"; \
  echo "vcpkg_installed: ${VCPKG_INSTALLED_DIR}"; \
  echo ""; \
  echo "# pinned overrides (vcpkg.json)"; \
  echo "nlohmann-json: ${NLOHMANN_JSON_VERSION}"; \
  echo "cpp-httplib: ${CPP_HTTPLIB_VERSION}"; \
  echo "yaml-cpp: ${YAML_CPP_VERSION}"; \
  echo "gtest: ${GTEST_VERSION} (build-time feature test only)"; \
  echo ""; \
  echo "# resolved by vcpkg"; \
  "${VCPKG_ROOT}/vcpkg" list --x-install-root="${VCPKG_INSTALLED_DIR}" \
    | grep -E '^(grpc|protobuf|boost-beast|nlohmann-json|cpp-httplib|yaml-cpp|bcg729):' || true; \
} > /src/build/dependency-versions.txt && cat /src/build/dependency-versions.txt

RUN --mount=type=cache,target=/src/build/_deps,id=voiceqas-fetchcontent-shared-ort \
mkdir -p /runtime/bin /runtime/lib /runtime/share && \
cp /src/build/voiceqas-server /runtime/bin/ && \
cp /src/build/dependency-versions.txt /runtime/share/ && \
find /src/build/_deps -name 'libonnxruntime*.so*' -exec cp -Ln {} /runtime/lib/ \; 2>/dev/null || true && \
find /src/build -name 'libonnxruntime*.so*' -exec cp -Ln {} /runtime/lib/ \; 2>/dev/null || true && \
find /src/build/lib -maxdepth 1 -name 'libsherpa-onnx*.so*' -exec cp -Ln {} /runtime/lib/ \; 2>/dev/null || true && \
if [[ -d "/opt/vcpkg-installed/${VCPKG_DEFAULT_TRIPLET}/lib" ]]; then \
  cp -a "/opt/vcpkg-installed/${VCPKG_DEFAULT_TRIPLET}/lib/"*.so* /runtime/lib/ 2>/dev/null || true; \
fi && \
while IFS= read -r lib; do \
  [[ -f "${lib}" ]] || continue; \
  case "${lib}" in \
    */libc.so*|*/ld-linux*|*/libm.so*|*/libpthread.so*|*/libdl.so*|*/librt.so*|*/libresolv.so*) continue ;; \
  esac; \
  cp -Ln "${lib}" /runtime/lib/; \
done < <(ldd /src/build/voiceqas-server | awk '/=> \// {print $3}' | sort -u) && \
LIBSTDCPP="$(gcc -print-file-name=libstdc++.so.6)" && \
[[ -f "${LIBSTDCPP}" ]] && cp -Ln "${LIBSTDCPP}" /runtime/lib/ && \
LIBGCC="$(gcc -print-file-name=libgcc_s.so.1)" && \
[[ -f "${LIBGCC}" ]] && cp -Ln "${LIBGCC}" /runtime/lib/

FROM nvidia/cuda:12.9.2-cudnn-runtime-ubuntu22.04 AS cuda-libs

# -----------------------------------------------------------------------------
# Stage 2: runtime
# -----------------------------------------------------------------------------
FROM debian:${DEBIAN_RUNTIME} AS runtime
ARG VOICEQAS_STT_CUDA=0
ARG GCC_VERSION
ARG CPP_STD
ARG CMAKE_MIN_VERSION
ARG VCPKG_BASELINE
ARG NLOHMANN_JSON_VERSION
ARG CPP_HTTPLIB_VERSION
ARG YAML_CPP_VERSION

LABEL org.opencontainers.image.title="voiceqas" \
      org.opencontainers.image.description="Voice quality assessment for SIP/STT (C++26)" \
      voiceqas.cpp-standard="${CPP_STD}" \
      voiceqas.gcc="${GCC_VERSION}" \
      voiceqas.cmake-min="${CMAKE_MIN_VERSION}" \
      voiceqas.vcpkg-baseline="${VCPKG_BASELINE}" \
      voiceqas.nlohmann-json="${NLOHMANN_JSON_VERSION}" \
      voiceqas.cpp-httplib="${CPP_HTTPLIB_VERSION}" \
      voiceqas.yaml-cpp="${YAML_CPP_VERSION}"

ENV DEBIAN_FRONTEND=noninteractive \
    VOICEQAS_WEB_ROOT=/app/web \
    VOICEQAS_OPENAPI_PATH=/app/openapi/voiceqas.yaml \
    LD_LIBRARY_PATH=/usr/local/lib:/usr/local/cuda/lib64

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 -s /usr/sbin/nologin voiceqas

WORKDIR /app

COPY --from=builder /runtime/bin/voiceqas-server /usr/local/bin/voiceqas-server
COPY --from=builder /runtime/lib/ /usr/local/lib/
RUN --mount=type=bind,from=cuda-libs,source=/usr/local/cuda/lib64,target=/cuda-libs,readonly \
    --mount=type=bind,from=cuda-libs,source=/usr/lib/x86_64-linux-gnu,target=/cuda-gnu,readonly \
    if [ "${VOICEQAS_STT_CUDA}" = "1" ]; then \
      mkdir -p /usr/local/cuda/lib64 && \
      cp -a /cuda-libs/. /usr/local/cuda/lib64/ && \
      cp -a /cuda-gnu/libcudnn*.so* /usr/local/cuda/lib64/; \
    fi
COPY --from=builder /runtime/share/dependency-versions.txt /app/dependency-versions.txt
COPY config/voiceqas.example.yaml /etc/voiceqas/voiceqas.yaml
COPY web/ /app/web/
COPY openapi/ /app/openapi/

RUN ldconfig

EXPOSE 8080 8081 50051

USER voiceqas

HEALTHCHECK --interval=15s --timeout=3s --start-period=20s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/health || exit 1

ENTRYPOINT ["/usr/local/bin/voiceqas-server", "--config", "/etc/voiceqas/voiceqas.yaml"]