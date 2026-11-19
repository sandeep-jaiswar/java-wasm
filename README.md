# java-wasm

The C++ core for verified Java design-pattern generation. One implementation serves both the
browser (WebAssembly) and native apps such as `workbench`.

## Layout

```
catalog/patterns.json    Pattern templates (single source of truth)
src/core/                C++17 core, no GUI or filesystem requirement
  catalog.*              Parses catalog JSON; O(1) lookup by id
  verify.*               tree-sitter Java verifier; one iterative O(n) pass
  engine.*               Template fill and best-of-N with early exit
src/wasm/api.cpp         C ABI for the browser: wb_init, wb_generate, wb_check
web/                     Browser shell (index.html, app.mjs); no verification logic
tests/test_core.cpp      Core tests (20 checks)
third_party/             Vendored tree-sitter runtime and Java grammar (MIT)
```

## Native build and tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Browser build (WebAssembly)

Emscripten 3.1.6 is a project dependency. Install it once into `.deps/emsdk` (gitignored):

```bash
./scripts/setup-deps.sh
source .deps/emsdk/emsdk_env.sh      # puts emcc on PATH for this shell
```

Then build:

```bash
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DWB_WASM=ON -DWB_BUILD_TESTS=OFF
cmake --build build-wasm -j
python3 -m http.server -d build-wasm/dist 8080    # open http://localhost:8080
```

The output in `build-wasm/dist/` is `wb.mjs`, `wb.wasm`, the web shell, and the catalog.

### C ABI

| Function | Returns |
|---|---|
| `wb_init(catalog_json)` | `1` on success, `0` on failure |
| `wb_generate(pattern_id, class_name)` | JSON: `{ok, source, rejections}` or `{ok:false, error}` |
| `wb_check(source)` | JSON: `{ok, errors:[{line,type,missing}]}` |

The returned string is in a buffer that the next call overwrites, so copy it right away.

## Using the core from another C++ project

```cmake
include(FetchContent)
FetchContent_Declare(java_wasm
    GIT_REPOSITORY https://github.com/sandeep-jaiswar/java-wasm.git
    GIT_TAG        <commit>)
set(WB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(java_wasm)
target_link_libraries(my_app PRIVATE wb_core)
```

## Adding a model

`wb::generate` takes an optional `Sampler` that returns candidate Java sources. Every candidate
goes through the same verifier as the templates. Plan: llama.cpp (MIT) with an MIT model. Model
weights are not bundled.

## Status

- Done: native core, tests (20 checks), WASM build, browser demo, smoke-tested in headless Chrome.
- Not included: model weights and the sampler backend.
- Limit: the verifier checks syntax and declared structure. It does not run `javac`. On desktop, you can run `javac` as a subprocess.

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
