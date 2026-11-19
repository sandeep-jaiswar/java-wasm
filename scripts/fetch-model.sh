#!/usr/bin/env bash
# Downloads the default local model (Qwen2.5-Coder-7B-Instruct, Q4_K_M GGUF, Apache 2.0)
# and checks its SHA-256. Usage: scripts/fetch-model.sh [target-dir]   (default: .deps/models)
# Re-running is cheap: a file that already verifies is kept.
set -euo pipefail

NAME="qwen2.5-coder-7b-instruct-q4_k_m.gguf"
URL="https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/main/$NAME"
SHA256="509287f78cb4d4cf6b3843734733b914b2c158e43e22a7f4bf5e963800894d3c"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="${1:-$ROOT/.deps/models}"
FILE="$DIR/$NAME"
mkdir -p "$DIR"

verify() { echo "$SHA256  $1" | sha256sum -c --status; }

if [ -f "$FILE" ] && verify "$FILE"; then
    echo "model present and verified: $FILE"
    exit 0
fi

curl -L --fail --retry 3 -o "$FILE.part" "$URL"
if ! verify "$FILE.part"; then
    rm -f "$FILE.part"
    echo "checksum mismatch; download removed" >&2
    exit 1
fi
mv "$FILE.part" "$FILE"
echo "model ready: $FILE"
