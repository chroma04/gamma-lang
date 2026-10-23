// Tokenizes the sample file with the TextMate grammar and checks selected scopes.
// Also tokenizes the standard library and checks that nothing is marked illegal.
// Run with `npm test`.

import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const vsctm = require("vscode-textmate");
const oniguruma = require("vscode-oniguruma");

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");

const wasmPath = require.resolve("vscode-oniguruma/release/onig.wasm");
const wasm = fs.readFileSync(wasmPath).buffer;
const onigLib = oniguruma.loadWASM(wasm).then(() => ({
  createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
  createOnigString: (s) => new oniguruma.OnigString(s),
}));

const registry = new vsctm.Registry({
  onigLib,
  loadGrammar: async (scopeName) => {
    if (scopeName !== "source.gamma") return null;
    const file = path.join(root, "syntaxes", "gamma.tmLanguage.json");
    return vsctm.parseRawGrammar(fs.readFileSync(file, "utf8"), file);
  },
});

const grammar = await registry.loadGrammar("source.gamma");

/** Returns [{ text, scopes, line }] for every token of `source`. */
function tokenize(source) {
  const out = [];
  let ruleStack = vsctm.INITIAL;
  source.split(/\r?\n/).forEach((line, i) => {
    const result = grammar.tokenizeLine(line, ruleStack);
    for (const t of result.tokens) {
      out.push({ text: line.slice(t.startIndex, t.endIndex), scopes: t.scopes, line: i + 1 });
    }
    ruleStack = result.ruleStack;
  });
  return out;
}

/** The token covering the first occurrence of `needle` in the sample. */
function tokenAt(needle) {
  const lines = sample.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const col = lines[i].indexOf(needle);
    if (col < 0) continue;
    let pos = 0;
    for (const t of tokens.filter((t) => t.line === i + 1)) {
      if (col >= pos && col < pos + t.text.length) return t;
      pos += t.text.length;
    }
  }
  return undefined;
}

let failures = 0;
function fail(msg) {
  failures++;
  console.error("FAIL: " + msg);
}

// ---- Selected scopes in the fixture ----

const sample = fs.readFileSync(path.join(here, "fixtures", "sample.gm"), "utf8");
const tokens = tokenize(sample);

// [text, expected scope, occurrence index]. The scope matches a token scope exactly or as a prefix.
// A text starting with "at:" names the token that covers that text, for literals split into
// several tokens (such as a number and its suffix).
const expectations = [
  ["// line comment", "comment.line.double-slash.gamma"],
  ["/// doc comment", "comment.line.documentation.gamma"],
  ["at:nested", "comment.block.gamma"],
  ["at:still comment", "comment.block.gamma"],
  ["Point", "entity.name.type.gamma"],
  ["struct", "storage.type.gamma"],
  ["f64", "support.type.primitive.gamma"],
  ["union", "storage.type.gamma"],
  ["Circle", "entity.name.type.gamma", 0],
  ["Log", "entity.name.type.gamma"],
  ["effect", "storage.type.gamma"],
  ["esc", "storage.modifier.operation.gamma"],
  ["#", "punctuation.definition.attribute.gamma"],
  ["inline", "entity.other.attribute-name.gamma"],
  ["add", "entity.name.function.gamma"],
  ["main", "entity.name.function.gamma"],
  ["fn", "storage.type.function.gamma"],
  ["->", "keyword.operator.arrow.gamma"],
  ["=>", "keyword.operator.arrow.gamma"],
  ["+%", "keyword.operator.arithmetic.wrapping.gamma"],
  ["+|=", "keyword.operator.assignment.compound.gamma"],
  ["<<%", "keyword.operator.arithmetic.wrapping.gamma"],
  ["needs", "keyword.other.needs.gamma"],
  ["Out", "entity.name.type.effect.gamma"],
  ["Err", "entity.name.type.effect.gamma"],
  ["IoError", "support.type.builtin.gamma"],
  ["meta.effect-set", "meta.effect-set.gamma"],
  ["at:1_000u32", "constant.numeric.integer.decimal.gamma"],
  ["u32", "storage.type.numeric.suffix.gamma"],
  ["0x1p4", "constant.numeric.float.hex.gamma"],
  ["at:1f32", "constant.numeric.float.gamma"],
  ["at:0xFF_u8", "constant.numeric.integer.hex.gamma"],
  ["at:3.5e-2f64", "constant.numeric.float.gamma"],
  ["at:4j", "constant.numeric.imaginary.gamma"],
  ["at:2.5j32", "constant.numeric.imaginary.gamma"],
  ["'a'", "string.quoted.single.char.gamma"],
  ["'\\n'", "string.quoted.single.char.gamma"],
  ["\\t", "constant.character.escape.gamma"],
  ["{}", "constant.other.placeholder.gamma", 0],
  ["{:>8.2}", "constant.other.placeholder.gamma"],
  ["{{", "constant.character.escape.brace.gamma"],
  ["c", "storage.type.string.prefix.gamma"],
  ["at:`C:\\dir", "string.quoted.other.raw.gamma"],
  ["@", "punctuation.definition.intrinsic.gamma"],
  ["size_of", "support.function.intrinsic.gamma"],
  ["const", "storage.modifier.const.gamma"],
  ["null", "constant.language.gamma"],
  ["if", "keyword.control.gamma"],
  ["is not", "keyword.operator.word.gamma"],
  ["and", "keyword.operator.word.gamma"],
  ["not", "keyword.operator.word.gamma"],
  ["in", "keyword.operator.word.gamma"],
  ["return", "keyword.control.gamma"],
  ["outer", "entity.name.label.gamma", 0],
  ["for", "keyword.control.gamma"],
  ["continue", "keyword.control.gamma"],
  ["match", "keyword.control.gamma"],
  ["Circle", "constant.other.enum-member.gamma", 1],
  ["handler", "storage.type.handler.gamma"],
  ["test", "storage.type.test.gamma"],
  ["bench", "storage.type.test.gamma"],
  ["print", "entity.name.function.call.gamma"],
  ["expect_eq", "entity.name.function.call.gamma"],
];

for (const [text, scope, nth = 0] of expectations) {
  if (text === "meta.effect-set") {
    if (!tokens.some((t) => t.scopes.includes(scope))) fail(`no token in scope ${scope}`);
    continue;
  }
  if (text.startsWith("at:")) {
    const needle = text.slice(3);
    const tok = tokenAt(needle);
    if (!tok) fail(`text ${JSON.stringify(needle)} not found`);
    else if (!tok.scopes.some((s) => s === scope || s.startsWith(scope + "."))) {
      fail(`text ${JSON.stringify(needle)} (line ${tok.line}) has scopes [${tok.scopes.join(", ")}], wanted ${scope}`);
    }
    continue;
  }
  const matches = tokens.filter((t) => t.text === text);
  const tok = matches[nth] ?? matches[0];
  if (!tok) {
    fail(`token ${JSON.stringify(text)} not found`);
  } else if (!tok.scopes.some((s) => s === scope || s.startsWith(scope + "."))) {
    fail(`token ${JSON.stringify(text)} (line ${tok.line}) has scopes [${tok.scopes.join(", ")}], wanted ${scope}`);
  }
}

// `.Circle(r)` in a pattern is a variant; the `Circle` in the union declaration is a type name.
const variants = tokens.filter((t) => t.text === "Circle");
if (!variants.some((t) => t.scopes.includes("constant.other.enum-member.gamma"))) {
  fail("`.Circle` is not scoped as an enum member");
}

// Negative checks.
const forbidden = [
  ["x", "comment"],
  ["1_000u32", "string"],
];
for (const [text, scope] of forbidden) {
  for (const t of tokens.filter((t) => t.text === text)) {
    if (t.scopes.some((s) => s.startsWith(scope))) fail(`token ${JSON.stringify(text)} must not be in ${scope}`);
  }
}

// ---- Nothing illegal in real code ----

function* gmFiles(dir) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) yield* gmFiles(p);
    else if (entry.name.endsWith(".gm")) yield p;
  }
}

const libDir = path.resolve(root, "..", "..", "lib");
let checked = 0;
if (fs.existsSync(libDir)) {
  for (const file of gmFiles(libDir)) {
    checked++;
    for (const t of tokenize(fs.readFileSync(file, "utf8"))) {
      if (t.scopes.some((s) => s.startsWith("invalid"))) {
        fail(`${path.relative(libDir, file)}:${t.line}: illegal token ${JSON.stringify(t.text)}`);
      }
    }
  }
}

if (failures > 0) {
  console.error(`${failures} failure(s)`);
  process.exit(1);
}
console.log(`ok: ${expectations.length} scope checks, ${checked} library files without illegal tokens`);
