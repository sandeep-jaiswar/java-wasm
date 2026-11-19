import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import assert from 'node:assert/strict';
import { indexCatalog, generate, fill } from '../src/engine.mjs';
import { analyze } from '../src/verify.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const catalog = JSON.parse(readFileSync(join(root, 'catalog/patterns.json'), 'utf8'));
const index = indexCatalog(catalog);
const opts = { javaWasm: join(root, 'node_modules/tree-sitter-java/tree-sitter-java.wasm') };

let passed = 0;
async function test(name, fn) {
  await fn();
  passed++;
  console.log(`ok  ${name}`);
}

await test('every catalog template generates valid, structurally correct Java', async () => {
  for (const p of catalog.patterns) {
    const r = await generate(index, p.id, { Name: 'Demo' }, { opts });
    assert.equal(r.ok, true, `${p.id}: ${JSON.stringify(r.rejected)}`);
  }
});

await test('syntax error is rejected by the verifier', async () => {
  const broken = fill(index.get('singleton').template, { Name: 'Demo' }).replace(/\}\s*$/, '');
  const r = await generate(index, 'singleton', { Name: 'Demo' }, { opts, sampler: async () => [broken] });
  assert.equal(r.ok, false);
  assert.equal(r.rejected[0].reason, 'syntax');
});

await test('missing required method is rejected by the structure check', async () => {
  const noGetter = fill(index.get('singleton').template, { Name: 'Demo' }).replace('getInstance()', 'fetch()');
  const r = await generate(index, 'singleton', { Name: 'Demo' }, { opts, sampler: async () => [noGetter] });
  assert.equal(r.ok, false);
  assert.equal(r.rejected[0].reason, 'structure');
  assert.match(r.rejected[0].details.join(' '), /getInstance/);
});

await test('best-of-N returns the first candidate that passes', async () => {
  const good = fill(index.get('strategy').template, { Name: 'Sort' });
  const broken = good.replace(/\}\s*$/, '');
  const r = await generate(index, 'strategy', { Name: 'Sort' }, { opts, sampler: async () => [broken, good] });
  assert.equal(r.ok, true);
  assert.equal(r.source, good);
  assert.equal(r.rejected.length, 1);
});

await test('all-invalid candidates report failure instead of returning bad code', async () => {
  const broken = fill(index.get('observer').template, { Name: 'Feed' }).replace(/\}\s*$/, '');
  const r = await generate(index, 'observer', { Name: 'Feed' }, { opts, sampler: async () => [broken, broken] });
  assert.equal(r.ok, false);
  assert.equal(r.source, null);
  assert.equal(r.rejected.length, 2);
});

await test('invalid identifiers are refused before any work is done', async () => {
  await assert.rejects(generate(index, 'singleton', { Name: 'bad name' }, { opts }), /invalid identifier/);
});

await test('unknown pattern id is refused', async () => {
  await assert.rejects(generate(index, 'visitor', { Name: 'X' }, { opts }), /unknown pattern/);
});

await test('analyze reports declared types and methods', async () => {
  const a = await analyze('class A { void m() {} }', opts);
  assert.equal(a.ok, true);
  assert.ok(a.types.has('A'));
  assert.ok(a.methods.has('m'));
});

console.log(`\n${passed} tests passed`);
