#!/usr/bin/env bash
# Build local com auto-reload: recompila e reinicia voiceqas-server ao salvar src/include.
#
# Uso:
#   ./scripts/dev.sh
#   CONFIG=config/voiceqas.example.yaml ./scripts/dev.sh
#   BUILD_TYPE=RelWithDebInfo ./scripts/dev.sh
#
# Requer um watcher (instale um):
#   sudo apt install entr          # recomendado
#   sudo apt install inotify-tools # fallback
#   cargo install watchexec-cli    # alternativa
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONFIG="${CONFIG:-config/voiceqas.example.yaml}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
WATCH_DIRS=(include src)

log() { printf '>> %s\n' "$*"; }

ensure_configured() {
  if [[ ! -f build/build.ninja ]]; then
    log "Primeira configuração (cmake + deps vcpkg)..."
    CMAKE_BUILD_TYPE="$BUILD_TYPE" ./scripts/build.sh
    return
  fi

  # Sincroniza build type se mudou
  local current_type
  current_type="$(grep -m1 '^CMAKE_BUILD_TYPE:' build/CMakeCache.txt 2>/dev/null | awk -F= '{print $2}' || true)"
  if [[ -n "$current_type" && "$current_type" != "$BUILD_TYPE" ]]; then
    log "BUILD_TYPE mudou ($current_type → $BUILD_TYPE), reconfigurando..."
    CMAKE_BUILD_TYPE="$BUILD_TYPE" ./scripts/build.sh
  fi
}

rebuild_server() {
  cmake --build build -j"$(nproc)" -- voiceqas-server
}

run_server() {
  exec ./build/voiceqas-server --config "$CONFIG"
}

cycle() {
  rebuild_server
  run_server
}

watch_sources() {
  local watch_cmd=(
    bash -lc
    "cd '$ROOT' && cmake --build build -j\$(nproc) -- voiceqas-server && exec ./build/voiceqas-server --config '$CONFIG'"
  )

  if command -v watchexec >/dev/null 2>&1; then
    log "Watcher: watchexec (Ctrl+C para sair)"
    exec watchexec -r --clear \
      -e cpp -e hpp -e h \
      -w include -w src \
      "${watch_cmd[@]}"
  fi

  if command -v entr >/dev/null 2>&1; then
    log "Watcher: entr (Ctrl+C para sair)"
    find "${WATCH_DIRS[@]}" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 \
      | sort -z \
      | xargs -0 entr -r "${watch_cmd[@]}"
  fi

  if command -v inotifywait >/dev/null 2>&1; then
    log "Watcher: inotifywait (Ctrl+C para sair)"
    rebuild_server
    while true; do
      ./build/voiceqas-server --config "$CONFIG" &
      local pid=$!
      inotifywait -r -q -e modify,create,delete,move "${WATCH_DIRS[@]}"
      kill -TERM "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      rebuild_server
    done
  fi

  cat >&2 <<'EOF'
Nenhum file watcher encontrado. Instale um de:

  sudo apt install entr
  sudo apt install inotify-tools
  cargo install watchexec-cli

Rodando uma vez sem auto-reload...
EOF
  cycle
}

main() {
  if [[ ! -f "$CONFIG" ]]; then
    echo "Config não encontrada: $CONFIG" >&2
    exit 1
  fi

  ensure_configured
  log "Auto-reload ativo — config: $CONFIG | build: $BUILD_TYPE"
  log "Monitorando: ${WATCH_DIRS[*]}"
  watch_sources
}

main "$@"
