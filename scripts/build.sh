#!/usr/bin/env bash
# Configura vcpkg (baseline fixo) e compila com C++26
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/.vcpkg}"
CONFIG="${ROOT}/vcpkg-configuration.json"

read_vcpkg_baseline() {
  python3 - <<'PY' "$CONFIG"
import json, sys
with open(sys.argv[1]) as f:
    print(json.load(f)["default-registry"]["baseline"])
PY
}

VCPKG_BASELINE="$(read_vcpkg_baseline)"

if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  echo ">> Clonando vcpkg @ ${VCPKG_BASELINE:0:12}..."
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
  git -C "$VCPKG_ROOT" checkout "$VCPKG_BASELINE"
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

cd "$ROOT"

CMAKE_ARGS=(
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
  -DVOICEQAS_ENABLE_STT=ON
  -DVCPKG_INSTALLED_DIR="$ROOT/vcpkg_installed"
)

if command -v ccache >/dev/null 2>&1; then
  export CCACHE_DIR="${CCACHE_DIR:-$ROOT/.ccache}"
  CMAKE_ARGS+=(
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
  )
fi

if [[ "${VOICEQAS_BUILD_TESTS:-1}" == "1" ]]; then
  CMAKE_ARGS+=(-DVOICEQAS_BUILD_TESTS=ON -DVCPKG_MANIFEST_FEATURES=test)
else
  CMAKE_ARGS+=(-DVOICEQAS_BUILD_TESTS=OFF)
fi

if [[ "${VOICEQAS_COVERAGE:-0}" == "1" ]]; then
  CMAKE_ARGS+=(-DVOICEQAS_COVERAGE=ON -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}")
fi

echo ">> Instalando deps vcpkg (manifest)..."
VCPKG_INSTALL_ARGS=(
  --triplet x64-linux
  --x-manifest-root="$ROOT"
  --x-install-root="$ROOT/vcpkg_installed"
)
if [[ "${VOICEQAS_BUILD_TESTS:-1}" == "1" ]]; then
  VCPKG_INSTALL_ARGS+=(--x-feature=test)
fi
"$VCPKG_ROOT/vcpkg" install "${VCPKG_INSTALL_ARGS[@]}"

cmake -B build -G Ninja "${CMAKE_ARGS[@]}" "$@"
cmake --build build -j"$(nproc)"

if [[ "${VOICEQAS_BUILD_TESTS:-1}" == "1" ]]; then
  "$ROOT/scripts/run-tests.sh"
fi
