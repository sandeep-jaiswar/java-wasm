// Builds a static, serverless browser bundle into dist/.
// Copies the engine sources, the catalog, the demo page, and the two WASM binaries
// (tree-sitter runtime and Java grammar). No bundler: the page uses an import map.
import { cpSync, mkdirSync, rmSync, copyFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist');

rmSync(dist, { recursive: true, force: true });
mkdirSync(join(dist, 'vendor'), { recursive: true });

cpSync(join(root, 'src'), join(dist, 'src'), { recursive: true });
cpSync(join(root, 'catalog'), join(dist, 'catalog'), { recursive: true });
copyFileSync(join(root, 'web/index.html'), join(dist, 'index.html'));
copyFileSync(join(root, 'web/app.mjs'), join(dist, 'app.mjs'));

const vendor = [
  ['node_modules/web-tree-sitter/web-tree-sitter.js', 'vendor/web-tree-sitter.js'],
  ['node_modules/web-tree-sitter/web-tree-sitter.wasm', 'vendor/web-tree-sitter.wasm'],
  ['node_modules/tree-sitter-java/tree-sitter-java.wasm', 'vendor/tree-sitter-java.wasm'],
];
for (const [from, to] of vendor) {
  const src = join(root, from);
  if (!existsSync(src)) throw new Error(`missing ${from}; run npm install`);
  copyFileSync(src, join(dist, to));
}

console.log('built dist/ (serve it with any static server, e.g. npx http-server dist)');
