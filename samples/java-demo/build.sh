#!/bin/sh
# Compiles the java-demo sources and tests, then runs the test suite.
# Works from any current directory: paths are resolved from this script's location.
set -eu

ROOT=$(cd "$(dirname "$0")" && pwd)
OUT="$ROOT/out"

rm -rf "$OUT/classes" "$OUT/test-classes"
mkdir -p "$OUT/classes" "$OUT/test-classes"

find "$ROOT/src" -name '*.java' > "$OUT/sources.txt"
find "$ROOT/test" -name '*.java' > "$OUT/test-sources.txt"

javac --release 17 -d "$OUT/classes" @"$OUT/sources.txt"
javac --release 17 -cp "$OUT/classes" -d "$OUT/test-classes" @"$OUT/test-sources.txt"

exec java -cp "$OUT/classes:$OUT/test-classes" demo.TestRunner
