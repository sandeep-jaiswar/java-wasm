# Third-party components

| Component | Version / commit | License | How it is used |
|---|---|---|---|
| tree-sitter runtime (C) | npm `tree-sitter` 0.25.0, vendored under `third_party/tree-sitter` | MIT | Compiled into `tree_sitter` |
| tree-sitter-java grammar | npm `tree-sitter-java` 0.23.5, vendored under `third_party/tree-sitter-java` | MIT | `parser.c` compiled into `tree_sitter_java` |
| nlohmann/json | `098eebd4` (fetched) | MIT | Catalog parsing and WASM JSON output |
| Emscripten | 3.1.6 via emsdk, installed to `.deps/emsdk` by `scripts/setup-deps.sh` | MIT / NCSA | Builds the browser bundle (`WB_WASM=ON`) |

Vendored sources keep their original license files where the upstream package provided them.
