// WASM smoke test for the browser bundle.
// Usage: node tests/wasm_smoke.mjs <dist-dir>   (dist-dir holds wb.mjs, wb.wasm, catalog/)
// Exits non-zero if any case fails.

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const MAX_INPUT_BYTES = 4 * 1024 * 1024;

const dist = process.argv[2];
if (!dist) {
  console.error('usage: node tests/wasm_smoke.mjs <dist-dir>');
  process.exit(2);
}

let failures = 0;
function report(name, ok, detail = '') {
  if (!ok) failures++;
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? ` - ${detail}` : ''}`);
}

function check(name, fn) {
  try {
    const r = fn();
    if (r && r.ok === false && r.msg) {
      report(name, false, r.msg);
    } else {
      report(name, r === true || (r && r.ok === true), r && r.detail ? r.detail : '');
    }
  } catch (err) {
    report(name, false, `threw: ${err && err.message ? err.message : err}`);
  }
}

// Mirrors web/app.mjs: copy text to the WASM heap, call fn(ptr, byteLength), free the buffer.
function withHeapString(Module, text, fn) {
  const len = Module.lengthBytesUTF8(text);
  const ptr = Module._malloc(len + 1);
  if (!ptr) throw new Error('malloc failed');
  try {
    Module.stringToUTF8(text, ptr, len + 1);
    return fn(ptr, len);
  } finally {
    Module._free(ptr);
  }
}

// Builds a Java class with `count` methods. Each method is about 35 bytes.
function bigClass(count) {
  const parts = ['public class Big {\n'];
  for (let i = 0; i < count; i++) parts.push(`  void m${i}() { int x = ${i}; }\n`);
  parts.push('}\n');
  return parts.join('');
}

const { default: createModule } = await import(pathToFileURL(path.join(dist, 'wb.mjs')).href);
const wasmBinary = fs.readFileSync(path.join(dist, 'wb.wasm'));
const Module = await createModule({ wasmBinary });

const init = Module.cwrap('wb_init', 'number', ['number', 'number']);
const generate = Module.cwrap('wb_generate', 'string', ['string', 'string']);
const checkRaw = Module.cwrap('wb_check', 'string', ['number', 'number']);
const parse = (s) => JSON.parse(s);
const check2 = (text) => parse(withHeapString(Module, text, (p, n) => checkRaw(p, n)));

const catalogText = fs.readFileSync(path.join(dist, 'catalog', 'patterns.json'), 'utf8');
const catalog = JSON.parse(catalogText).patterns;

check('init succeeds on catalog', () => withHeapString(Module, catalogText, (p, n) => init(p, n)) === 1);

check('init rejects input over the 4 MiB cap', () => {
  // Valid JSON padded past the cap; must return 0 without trapping.
  const padded = '{"version":1,"patterns":[],"pad":"' + 'x'.repeat(MAX_INPUT_BYTES) + '"}';
  return withHeapString(Module, padded, (p, n) => init(p, n)) === 0;
});

// The rejected init must not have cleared the catalog; re-init so later cases start clean.
check('re-init after rejected input', () => withHeapString(Module, catalogText, (p, n) => init(p, n)) === 1);

for (const p of catalog) {
  check(`generate ${p.id} ok:true`, () => {
    const r = parse(generate(p.id, 'Demo'));
    return { ok: r.ok === true, detail: r.ok ? '' : JSON.stringify(r).slice(0, 200), msg: r.ok ? '' : JSON.stringify(r).slice(0, 200) };
  });
}

check('generate rejects unknown pattern id', () => {
  const r = parse(generate('no-such-pattern', 'Demo'));
  return r.ok === false && typeof r.error === 'string';
});

check('generate rejects bad class name "1X"', () => {
  const r = parse(generate(catalog[0].id, '1X'));
  return r.ok === false && typeof r.error === 'string';
});

check('check accepts a valid class', () => {
  const r = check2('public class A {\n  void f() {}\n}\n');
  return r.ok === true && Array.isArray(r.errors) && r.errors.length === 0;
});

check('check flags a broken class with at least one error', () => {
  const r = check2('class A { void f( {');
  return r.ok === false && r.errors.length >= 1 && typeof r.errors[0].line === 'number';
});

check('check: heap path handles ~3.3 MiB (past the old ~1.8 MB stack crash)', () => {
  // About 100k methods, under the 4 MiB cap.
  const src = bigClass(100000);
  const bytes = Buffer.byteLength(src, 'utf8');
  if (bytes > MAX_INPUT_BYTES) return { ok: false, msg: `test input ${bytes} bytes exceeds cap` };
  const r = check2(src);
  return { ok: r.ok === true, detail: `${(bytes / 1048576).toFixed(2)} MiB`, msg: r.ok ? '' : JSON.stringify(r).slice(0, 200) };
});

check('check: input over 4 MiB cap returns "input too large" (no trap)', () => {
  // 400,000 methods, about 17 MB.
  const src = bigClass(400000);
  const bytes = Buffer.byteLength(src, 'utf8');
  const r = check2(src);
  return r.ok === false && r.error === 'input too large' && bytes > MAX_INPUT_BYTES
    ? { ok: true, detail: `${(bytes / 1048576).toFixed(1)} MiB` }
    : { ok: false, msg: `bytes=${bytes} result=${JSON.stringify(r).slice(0, 200)}` };
});

// Boundary: exactly the cap is accepted, one byte more is rejected. Both run in WASM.
function paddedTo(bytes) {
  const head = 'public class A {}\n// ';
  return head + 'x'.repeat(bytes - Buffer.byteLength(head, 'utf8'));
}

check('check: exactly 4 MiB (cap) is accepted and parsed', () => {
  const src = paddedTo(MAX_INPUT_BYTES);
  const r = check2(src);
  return { ok: r.ok === true, detail: `${Buffer.byteLength(src, 'utf8')} bytes`, msg: r.ok ? '' : JSON.stringify(r).slice(0, 200) };
});

check('check: 4 MiB + 1 byte is rejected as "input too large"', () => {
  const r = check2(paddedTo(MAX_INPUT_BYTES + 1));
  return r.ok === false && r.error === 'input too large';
});

check('module still works after the cap error', () => {
  const r = check2('public class A {}\n');
  const g = parse(generate(catalog[0].id, 'Demo'));
  return r.ok === true && g.ok === true;
});

console.log(failures === 0 ? 'ALL PASS' : `${failures} FAILED`);
process.exit(failures === 0 ? 0 : 1);
