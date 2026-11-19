// "Opening book" + verifier engine.
// 1. Look the pattern up in the catalog (O(1) via Map).
// 2. Produce candidates: deterministic template fill (N=1), or model samples (N>1) if a sampler is given.
// 3. Verify each candidate; return the first that passes (cheapest candidates first).
import { analyze, checkRequirements } from './verify.mjs';

export function indexCatalog(catalog) {
  return new Map(catalog.patterns.map((p) => [p.id, p]));
}

// Fills {{Key}} placeholders. Single pass with a regex: O(len(template)).
export function fill(template, params) {
  return template.replace(/\{\{(\w+)\}\}/g, (_, key) => {
    if (!(key in params)) throw new Error(`missing param ${key}`);
    return params[key];
  });
}

export function isValidIdentifier(s) {
  return /^[A-Za-z_]\w*$/.test(s);
}

// sampler (optional): async ({ pattern, params, template }) => string[] of candidate sources.
export async function generate(index, patternId, params, { sampler, opts } = {}) {
  const pattern = index.get(patternId);
  if (!pattern) throw new Error(`unknown pattern ${patternId}`);
  for (const p of pattern.params) {
    if (!isValidIdentifier(params[p] ?? '')) throw new Error(`invalid identifier for ${p}`);
  }

  const deterministic = fill(pattern.template, params);
  const candidates = sampler
    ? await sampler({ pattern, params, template: deterministic })
    : [deterministic];

  const requires = {
    types: pattern.requires.types.map((t) => fill(t, params)),
    methods: pattern.requires.methods,
  };

  const rejected = [];
  for (const source of candidates) {
    const analysis = await analyze(source, opts);
    if (!analysis.ok) {
      rejected.push({ reason: 'syntax', details: analysis.errors });
      continue;
    }
    const problems = checkRequirements(analysis, requires);
    if (problems.length) {
      rejected.push({ reason: 'structure', details: problems });
      continue;
    }
    return { ok: true, source, rejected };
  }
  return { ok: false, source: null, rejected };
}
