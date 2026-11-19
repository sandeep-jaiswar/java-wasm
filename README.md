# java-wasm

Verified Java design-pattern generation that runs in the browser (WebGPU/WASM) and natively.
The idea follows Stockfish: a fixed opening book, a neural generator, and a verifier that
rejects bad candidates before anything is shown.

## Architecture

| Layer | Role | Implementation | License |
|---|---|---|---|
| Opening book | Deterministic GoF templates | `catalog/patterns.json` | MIT |
| Verifier | Syntax and structure checks, one linear pass | `web-tree-sitter` 0.27.2 + `tree-sitter-java` 0.23.5 (WASM) | MIT + MIT |
| Engine | O(1) lookup, O(n) fill, best-of-N with early exit | `src/engine.mjs` | MIT |
| Generator (backbone) | Open-weight code model | Pluggable `sampler` hook; not bundled | Use MIT weights only |

Verification walks the syntax tree once with an explicit stack, so it runs in O(n) time
on source size, and it stores declared names in Sets for O(1) lookups. Each parse tree is
freed after use.

## Quick start

```bash
npm install
npm test        # 8 tests: templates, syntax and structure rejection, best-of-N, input checks
npm run build   # writes a static bundle to dist/
npm run serve   # serves dist/ on http://localhost:8080
```

The demo page (`web/index.html`) has two functions: generate and verify a pattern, and check
any Java source for syntax errors. Everything runs in the page, with no server calls.

## Using it from code

```js
import { indexCatalog, generate } from './src/engine.mjs';
import catalog from './catalog/patterns.json' with { type: 'json' };

const index = indexCatalog(catalog);
const result = await generate(index, 'singleton', { Name: 'Config' }, {
  opts: { runtimeWasm: '/vendor/web-tree-sitter.wasm', javaWasm: '/vendor/tree-sitter-java.wasm' },
});
if (result.ok) console.log(result.source);
```

## Adding a model

`generate(index, id, params, { sampler })` calls `sampler({ pattern, params, template })`
and expects an array of Java source strings. Its output goes through the same verifier as
the templates, so a bad sample is rejected rather than shown.

Options that fit the license requirement:

- **llama.cpp** (MIT), compiled to WASM or native, loading a GGUF file.
- **ONNX Runtime Web** (MIT), using its WebGPU execution provider on an ONNX export.

Not used: WebLLM and transformers.js, which are Apache-2.0.

Candidate MIT models include Phi-family models and `ricdomolm/mini-coder-1.7b` (MIT, built on
Qwen3-1.7B). Check the license on each model page before shipping, because a base model may
carry a different license.

## Status

- Done: catalog (Singleton, Strategy, Observer), verifier, engine, tests, browser build, demo page.
- Not included: model weights. The neural sampler is a hook only.
- Not yet tested: the browser page itself. The tests run under Node.
- Limit: the verifier checks syntax and declared structure. It does not run `javac`, so it
  cannot catch type errors. Add a server-side compile check for that.

## References

- P-MART: open-source Java projects that implement design patterns, useful for more catalog
  entries and evaluation.
- `samujjwaal/intellij-dp-generator-plugin`: GoF patterns generated with JavaPoet. Check its
  license before reusing any code.

## License

MIT. See `LICENSE`.
