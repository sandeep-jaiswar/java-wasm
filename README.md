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
src/llm/                 Optional llama.cpp completion (native only, WB_LLM=ON)
src/wasm/api.cpp         C ABI for the browser: wb_init, wb_generate, wb_check
web/                     Browser shell (index.html, app.mjs); no verification logic
tests/test_core.cpp      Core tests (29 checks)
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
node tests/wasm_smoke.mjs build-wasm/dist          # headless WASM smoke test (15 cases)
python3 -m http.server -d build-wasm/dist 8080    # open http://localhost:8080
```

The output in `build-wasm/dist/` is `wb.mjs`, `wb.wasm`, the web shell, and the catalog.

For a smaller download, add `-DWB_OPTIMIZE_SIZE=ON` (compiles and links with `-Oz -flto`; about 20% smaller raw and 16% smaller gzipped than the default build (474 KB and 150 KB against 589 KB and 178 KB, measured on the current tree)). Use a separate build directory, such as `build-wasm-size`, so the speed-optimized build is kept.

### C ABI

| Function | Returns |
|---|---|
| `wb_init(json, len)` | `1` on success, `0` on failure or when `len` exceeds 4 MiB |
| `wb_generate(pattern_id, class_name)` | JSON: `{ok, source, rejections}` or `{ok:false, error}` |
| `wb_check(src, len)` | JSON: `{ok, errors:[{line,type,missing}]}`, or `{ok:false, error:"input too large"}` over 4 MiB |

Source text (`json`, `src`) is passed as a pointer and byte length in WASM heap memory: allocate
with `_malloc`, write with `stringToUTF8`, and free with `_free`. Do not pass source text through
`cwrap` string arguments. Every export catches exceptions and returns JSON instead.

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

## Local model (native, optional)

The default model is Qwen2.5-Coder-7B-Instruct (Q4_K_M GGUF, Apache 2.0, about 4.7 GB). Fetch it
with a pinned SHA-256 check:

```bash
scripts/fetch-model.sh                 # into .deps/models (default)
scripts/fetch-model.sh /path/to/models # or into another directory
```


`WB_LLM=ON` builds `wb_llm`, a llama.cpp wrapper for greedy text completion. It is native only;
the WASM bundle does not include it. Weights are not bundled. Pass a GGUF file at run time:

```bash
cmake -S . -B build-llm -DCMAKE_BUILD_TYPE=Release -DWB_LLM=ON \
      -DWB_LLM_TEST_MODEL=/path/to/model.gguf
cmake --build build-llm -j && ctest --test-dir build-llm --output-on-failure
```

Tested with Qwen2.5-Coder-1.5B-Instruct (Q4_K_M, CPU). `wb::generate` still takes a `Sampler`, and
every candidate goes through the same verifier as the templates.

## Status

- Done: native core, tests (29 checks), WASM build, browser demo, smoke-tested in headless Chrome.
- Done: native llama.cpp module (`wb_llm`), smoke-tested with a real 1.5B GGUF.
- Not included: model weights, and the WASM build of the model.
- Verified means: the source parses with no errors, and each catalog requirement holds. A plain
  name requires a type or method with that name somewhere in the file. A structured requirement
  (for example `getInstance` in `Demo`, static, returning `Demo`) is checked against the members
  declared by that type, including its static flag, return type and modifiers, and its
  constructors must carry the required modifier.
- Limit: the verifier checks syntax and declared structure. It does not check method bodies,
  types or semantics, and it does not run `javac`. On desktop, you can run `javac` as a subprocess.

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
