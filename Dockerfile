# syntax=docker/dockerfile:1

# =============================================================================
# voiceqas — stack de build (jul/2026)
#
#   Linguagem : C++26  (-std=c++26)
#   Compilador: GCC 15  (imagem gcc:15-bookworm)
#   CMake     : >= 3.31 (pip)
#   Deps      : vcpkg manifest + baseline fixo (vcpkg-configuration.json)
#
#   Cache BuildKit (rebuild incremental):
#     - vcpkg downloads / buildtrees / packages / bincache
#     - vcpkg_installed (manifest install root)
#     - build/_deps (FetchContent: sherpa-onnx, onnxruntime, etc.)
#     - ccache
#
#   Overrides fixos (vcpkg.json):
#     nlohmann-json  3.12.0
#     cpp-httplib    0.48.0
#     yaml-cpp       0.9.0
#
#   GoogleTest 1.17.0 — apenas com -DVOICEQAS_BUILD_TESTS=ON (feature test)
# =============================================================================

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

# -----------------------------------------------------------------------------
# Stage 1: toolchain + vcpkg (commit = baseline fixo)
# -----------------------------------------------------------------------------
FROM gcc:${GCC_VERSION}-bookworm AS vcpkg-base

ARG CMAKE_MIN_VERSION
ARG VCPKG_BASELINE

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DEFAULT_TRIPLET=x64-linux \
    VCPKG_FEATURE_FLAGS=manifests,versions \
    CMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake

RUN apt-get update && apt-get install -y --no-install-recommends \
        ninja-build \
        git \
        curl \
        zip \
        unzip \
        tar \
        pkg-config \
        ca-certificates \
        python3-pip \
        ccache \
    && pip3 install --break-system-packages "cmake>=${CMAKE_MIN_VERSION}" \
    && rm -rf /var/lib/apt/lists/*

RUN echo "=== toolchain ===" \
    && cmake --version \
    && ninja --version \
    && g++ --version | head -1 \
    && ccache --version | head -1

# Clone completo: shallow clone quebra version overrides que referenciam git-trees antigos
RUN git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && cd "${VCPKG_ROOT}" \
    && git checkout "${VCPKG_BASELINE}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics

# -----------------------------------------------------------------------------
# Stage 2: compilação (layers: vcpkg deps → scaffolding → código)
# -----------------------------------------------------------------------------
FROM vcpkg-base AS builder

ARG GCC_VERSION
ARG CPP_STD
ARG VCPKG_BASELINE
ARG NLOHMANN_JSON_VERSION
ARG CPP_HTTPLIB_VERSION
ARG YAML_CPP_VERSION
ARG GTEST_VERSION
ARG VOICEQAS_STT_CUDA
ARG VOICEQAS_BUILD_TESTS=OFF

ENV CMAKE_MAKE_PROGRAM=/usr/bin/ninja \
    CMAKE_CXX_COMPILER=/usr/local/bin/g++ \
    CMAKE_C_COMPILER=/usr/local/bin/gcc \
    CCACHE_DIR=/root/.ccache \
    CCACHE_MAXSIZE=2G \
    CMAKE_CXX_COMPILER_LAUNCHER=ccache \
    CMAKE_C_COMPILER_LAUNCHER=ccache \
    VCPKG_BINARY_SOURCES=clear;files,/opt/vcpkg/bincache,readwrite \
    VCPKG_INSTALLED_DIR=/src/vcpkg_installed

SHELL ["/bin/bash", "-eo", "pipefail", "-c"]

WORKDIR /src

# --- Layer A: manifest vcpkg (invalida só quando vcpkg.json / baseline mudam) ---
COPY vcpkg.json vcpkg-configuration.json ./

RUN --mount=type=cache,target=/opt/vcpkg/downloads,id=voiceqas-vcpkg-dl \
    --mount=type=cache,target=/opt/vcpkg/buildtrees,id=voiceqas-vcpkg-bt \
    --mount=type=cache,target=/opt/vcpkg/packages,id=voiceqas-vcpkg-pkg \
    --mount=type=cache,target=/opt/vcpkg/bincache,id=voiceqas-vcpkg-bin \
    --mount=type=cache,target=/src/vcpkg_installed,id=voiceqas-vcpkg-installed \
    bash -c 'VCPKG_FEATURE_ARGS=(); \
      if [[ "${VOICEQAS_BUILD_TESTS}" == "ON" ]]; then VCPKG_FEATURE_ARGS+=(--x-feature=test); fi; \
      "${VCPKG_ROOT}/vcpkg" install \
        --triplet "${VCPKG_DEFAULT_TRIPLET}" \
        --x-manifest-root=/src \
        --x-install-root=/src/vcpkg_installed \
        "${VCPKG_FEATURE_ARGS[@]}"'

# --- Layer B: scaffolding CMake (muda raramente) ---
COPY CMakeLists.txt ./
COPY third_party/ third_party/
COPY proto/ proto/

# --- Layer C: código da aplicação ---
COPY include/ include/
COPY src/ src/
COPY tests/ tests/

# --- Configure + build (STT embutido obrigatório) ---
RUN --mount=type=cache,target=/opt/vcpkg/downloads,id=voiceqas-vcpkg-dl \
    --mount=type=cache,target=/opt/vcpkg/buildtrees,id=voiceqas-vcpkg-bt \
    --mount=type=cache,target=/opt/vcpkg/packages,id=voiceqas-vcpkg-pkg \
    --mount=type=cache,target=/opt/vcpkg/bincache,id=voiceqas-vcpkg-bin \
    --mount=type=cache,target=/src/vcpkg_installed,id=voiceqas-vcpkg-installed \
    --mount=type=cache,target=/src/build/_deps,id=voiceqas-fetchcontent-shared-ort \
    --mount=type=cache,target=/root/.ccache,id=voiceqas-ccache \
    bash -c 'cmake -B build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DVOICEQAS_BUILD_TESTS="${VOICEQAS_BUILD_TESTS}" \
        -DVOICEQAS_ENABLE_STT=ON \
        -DVOICEQAS_STT_CUDA=${VOICEQAS_STT_CUDA} \
        -DVCPKG_INSTALLED_DIR=/src/vcpkg_installed \
    && cmake --build build -j"$(nproc)" \
    && if [[ "${VOICEQAS_BUILD_TESTS}" == "ON" ]]; then cd build && ctest --output-on-failure; fi'

# Metadados de versões resolvidas no build
RUN { \
  echo "# voiceqas build versions"; \
  echo "generated_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
  echo "cpp_standard: ${CPP_STD}"; \
  echo "vcpkg_baseline: ${VCPKG_BASELINE}"; \
  g++ --version | head -1 | sed 's/^/gcc: /'; \
  cmake --version | head -1 | sed 's/^/cmake: /'; \
  echo "vcpkg_triplet: ${VCPKG_DEFAULT_TRIPLET}"; \
  echo ""; \
  echo "# pinned overrides (vcpkg.json)"; \
  echo "nlohmann-json: ${NLOHMANN_JSON_VERSION}"; \
  echo "cpp-httplib: ${CPP_HTTPLIB_VERSION}"; \
  echo "yaml-cpp: ${YAML_CPP_VERSION}"; \
  echo "gtest: ${GTEST_VERSION} (build-time feature test only)"; \
  echo ""; \
  echo "# resolved by vcpkg"; \
  "${VCPKG_ROOT}/vcpkg" list | grep -E '^(grpc|protobuf|boost-beast|nlohmann-json|cpp-httplib|yaml-cpp|bcg729):' || true; \
} > /src/build/dependency-versions.txt && cat /src/build/dependency-versions.txt

# Empacota binário + libs de runtime (_deps é cache mount — montar aqui também)
RUN --mount=type=cache,target=/src/build/_deps,id=voiceqas-fetchcontent-shared-ort \
mkdir -p /runtime/bin /runtime/lib /runtime/share && \
cp /src/build/voiceqas-server /runtime/bin/ && \
cp /src/build/dependency-versions.txt /runtime/share/ && \
find /src/build/_deps -name 'libonnxruntime*.so*' -exec cp -Ln {} /runtime/lib/ \; 2>/dev/null || true && \
find /src/build -name 'libonnxruntime*.so*' -exec cp -Ln {} /runtime/lib/ \; 2>/dev/null || true && \
find /src/build/lib -maxdepth 1 -name 'libsherpa-onnx*.so*' -exec cp -Ln {} /runtime/lib/ \; 2>/dev/null || true && \
if [[ -d "/src/vcpkg_installed/${VCPKG_DEFAULT_TRIPLET}/lib" ]]; then \
  cp -a "/src/vcpkg_installed/${VCPKG_DEFAULT_TRIPLET}/lib/"*.so* /runtime/lib/ 2>/dev/null || true; \
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

# Libs CUDA empacotadas no runtime (Docker Desktop/WSL nem sempre monta libcublas via toolkit).
FROM nvidia/cuda:12.9.2-cudnn-runtime-ubuntu22.04 AS cuda-libs

# -----------------------------------------------------------------------------
# Stage 3: runtime mínimo
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
    if [[ "${VOICEQAS_STT_CUDA}" == "1" ]]; then \
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
