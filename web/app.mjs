// Browser shell: calls the C++ core compiled to WASM. No verification logic lives here.
import createModule from './wb.mjs';

const $ = (id) => document.getElementById(id);

// Replaces the children of `el` with text-only parts. Strings become text nodes;
// objects { cls, text } become a span. Dynamic content never goes through innerHTML.
function setStatus(el, parts) {
  el.replaceChildren(...parts.map((p) => {
    if (typeof p === 'string') return document.createTextNode(p);
    const span = document.createElement('span');
    span.className = p.cls;
    span.textContent = p.text;
    return span;
  }));
}

function showFatal(err) {
  console.error(err);
  const box = $('fatal');
  box.textContent = `The verifier failed to start: ${err && err.message ? err.message : String(err)}`;
  box.hidden = false;
}

// Copies `text` into WASM heap memory as NUL-terminated UTF-8, calls fn(ptr, byteLength),
// and frees the buffer afterwards (even if fn throws).
function withHeapString(Module, text, fn) {
  const len = Module.lengthBytesUTF8(text);
  const ptr = Module._malloc(len + 1);
  if (!ptr) throw new Error('out of memory');
  try {
    Module.stringToUTF8(text, ptr, len + 1);
    return fn(ptr, len);
  } finally {
    Module._free(ptr);
  }
}

// The WASM side reuses its output buffer, so parse the JSON right away.
function parseResult(text) {
  try {
    return JSON.parse(text);
  } catch {
    return { ok: false, error: 'unreadable response from the verifier' };
  }
}

async function main() {
  const Module = await createModule({ locateFile: (file) => new URL(file, import.meta.url).href });
  const init = Module.cwrap('wb_init', 'number', ['number', 'number']);
  const generate = Module.cwrap('wb_generate', 'string', ['string', 'string']);
  const checkRaw = Module.cwrap('wb_check', 'string', ['number', 'number']);

  const catalogText = await (await fetch(new URL('./catalog/patterns.json', import.meta.url))).text();
  if (!withHeapString(Module, catalogText, (ptr, len) => init(ptr, len))) {
    throw new Error('catalog failed to load');
  }

  const catalogPatterns = JSON.parse(catalogText).patterns;
  for (const p of catalogPatterns) $('pattern').append(new Option(p.name, p.id));

  $('generate').addEventListener('click', () => {
    const status = $('gen-status');
    const r = parseResult(generate($('pattern').value, $('name').value.trim()));
    if (r.ok) {
      setStatus(status, [{ cls: 'ok', text: 'Verified' }]);
      $('gen-output').textContent = r.source;
    } else {
      $('gen-output').textContent = '';
      const why = r.error ?? (r.rejections ?? []).join(' | ');
      setStatus(status, [{ cls: 'bad', text: 'Rejected' }, ` ${why}`]);
    }
  });

  $('check').addEventListener('click', () => {
    const status = $('check-status');
    const text = $('check-src').value;
    let r;
    try {
      r = withHeapString(Module, text, (ptr, len) => parseResult(checkRaw(ptr, len)));
    } catch (err) {
      setStatus(status, [{ cls: 'bad', text: 'Not checked' }, ` ${err.message}`]);
      return;
    }
    if (r.error) {
      setStatus(status, [{ cls: 'bad', text: 'Not checked' }, ` ${r.error}`]);
    } else if (r.ok) {
      setStatus(status, [{ cls: 'ok', text: 'No syntax errors' }]);
    } else {
      const lines = r.errors.map((e) => e.line).join(', ');
      setStatus(status, [
        { cls: 'bad', text: `${r.errors.length} syntax error(s)` },
        ` at line(s) ${lines}`,
      ]);
    }
  });
}

main().catch(showFatal);
