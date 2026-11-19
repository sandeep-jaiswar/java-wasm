// Structural verifier for Java source, built on tree-sitter (MIT).
// Single linear pass over the syntax tree: O(n) in source size, O(1) lookups via Sets.
import { Parser, Language } from 'web-tree-sitter';

const DECL_TYPES = new Set([
  'class_declaration',
  'interface_declaration',
  'enum_declaration',
  'record_declaration',
]);

let parserPromise;

// wasmPath: URL or filesystem path to tree-sitter.wasm (runtime) and tree-sitter-java.wasm (grammar).
export function getParser({ runtimeWasm, javaWasm } = {}) {
  parserPromise ??= (async () => {
    await Parser.init(runtimeWasm ? { locateFile: () => runtimeWasm } : undefined);
    const parser = new Parser();
    parser.setLanguage(await Language.load(javaWasm));
    return parser;
  })();
  return parserPromise;
}

// Returns { ok, errors, types:Set, methods:Set }.
export async function analyze(source, opts) {
  const parser = await getParser(opts);
  const tree = parser.parse(source);
  const types = new Set();
  const methods = new Set();
  const errors = [];

  // Iterative DFS with an explicit stack (no recursion-depth limit).
  const stack = [tree.rootNode];
  while (stack.length) {
    const node = stack.pop();
    if (node.type === 'ERROR' || node.isMissing) {
      errors.push({ type: node.type, row: node.startPosition.row + 1, missing: node.isMissing });
    }
    if (DECL_TYPES.has(node.type)) {
      const name = node.childForFieldName('name');
      if (name) types.add(name.text);
    } else if (node.type === 'method_declaration') {
      const name = node.childForFieldName('name');
      if (name) methods.add(name.text);
    }
    for (let i = node.childCount - 1; i >= 0; i--) stack.push(node.child(i));
  }
  tree.delete();
  return { ok: errors.length === 0, errors, types, methods };
}

// Checks required declarations after analysis. Returns a list of problems (empty = pass).
export function checkRequirements(analysis, requires) {
  const problems = [];
  for (const t of requires.types ?? []) {
    if (!analysis.types.has(t)) problems.push(`missing type ${t}`);
  }
  for (const m of requires.methods ?? []) {
    if (!analysis.methods.has(m)) problems.push(`missing method ${m}`);
  }
  return problems;
}
