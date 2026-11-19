import { indexCatalog, generate } from './src/engine.mjs';
import { analyze } from './src/verify.mjs';

// Asset paths are relative to the page, so the bundle works from any static host.
const opts = {
  runtimeWasm: new URL('./vendor/web-tree-sitter.wasm', import.meta.url).href,
  javaWasm: new URL('./vendor/tree-sitter-java.wasm', import.meta.url).href,
};

const catalog = await (await fetch(new URL('./catalog/patterns.json', import.meta.url))).json();
const index = indexCatalog(catalog);

const $ = (id) => document.getElementById(id);

for (const p of catalog.patterns) {
  $('pattern').append(new Option(p.name, p.id));
}

const describeProblem = (r) =>
  r.reason === 'syntax'
    ? `syntax errors at line(s) ${r.details.map((e) => e.row).join(', ')}`
    : `structure: ${r.details.join('; ')}`;

$('generate').addEventListener('click', async () => {
  const status = $('gen-status');
  status.textContent = 'Verifying…';
  $('gen-output').textContent = '';
  try {
    const result = await generate(index, $('pattern').value, { Name: $('name').value.trim() }, { opts });
    if (result.ok) {
      status.innerHTML = '<span class="ok">Verified</span>';
      $('gen-output').textContent = result.source;
    } else {
      status.innerHTML = `<span class="bad">Rejected</span> ${result.rejected.map(describeProblem).join(' | ')}`;
    }
  } catch (err) {
    status.innerHTML = `<span class="bad">Error</span> ${err.message}`;
  }
});

$('check').addEventListener('click', async () => {
  const status = $('check-status');
  const { ok, errors } = await analyze($('check-src').value, opts);
  status.innerHTML = ok
    ? '<span class="ok">No syntax errors</span>'
    : `<span class="bad">${errors.length} syntax error(s)</span> at line(s) ${errors.map((e) => e.row).join(', ')}`;
});
