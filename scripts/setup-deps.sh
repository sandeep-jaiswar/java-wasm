#!/usr/bin/env bash
# Installs the build dependencies that live outside the repo.
#   - Emscripten 3.1.6 via emsdk, into .deps/emsdk (needed only for the WASM build)
# nlohmann/json and tree-sitter are fetched or vendored by CMake; nothing to do here.
set -euo pipefail

EMSDK_VERSION="3.1.6"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMSDK_DIR="$ROOT/.deps/emsdk"

if [ ! -d "$EMSDK_DIR/.git" ]; then
    mkdir -p "$ROOT/.deps"
    git clone --depth 1 --branch "$EMSDK_VERSION" https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi

"$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"
"$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION"

echo
echo "Done. Before the WASM build, load the toolchain in each shell:"
echo "  source \"$EMSDK_DIR/emsdk_env.sh\""
