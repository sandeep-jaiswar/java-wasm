// Browser shell: calls the C++ core compiled to WASM. No verification logic lives here.
import createModule from './wb.mjs';

const $ = (id) => document.getElementById(id);

const Module = await createModule({ locateFile: (file) => new URL(file, import.meta.url).href });
const init = Module.cwrap('wb_init', 'number', ['string']);
const generate = Module.cwrap('wb_generate', 'string', ['string', 'string']);
const check = Module.cwrap('wb_check', 'string', ['string']);

const catalog = await (await fetch(new URL('./catalog/patterns.json', import.meta.url))).text();
if (!init(catalog)) throw new Error('catalog failed to load');

// The WASM side returns a JSON string in a buffer it reuses; parse it right away.
const catalogPatterns = JSON.parse(catalog).patterns;
for (const p of catalogPatterns) $('pattern').append(new Option(p.name, p.id));

$('generate').addEventListener('click', () => {
  const status = $('gen-status');
  const r = JSON.parse(generate($('pattern').value, $('name').value.trim()));
  if (r.ok) {
    status.innerHTML = '<span class="ok">Verified</span>';
    $('gen-output').textContent = r.source;
  } else {
    $('gen-output').textContent = '';
    const why = r.error ?? r.rejections.join(' | ');
    status.innerHTML = `<span class="bad">Rejected</span> ${why}`;
  }
});

$('check').addEventListener('click', () => {
  const r = JSON.parse(check($('check-src').value));
  $('check-status').innerHTML = r.ok
    ? '<span class="ok">No syntax errors</span>'
    : `<span class="bad">${r.errors.length} syntax error(s)</span> at line(s) ${r.errors.map((e) => e.line).join(', ')}`;
});
