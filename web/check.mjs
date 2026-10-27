// Runs every program of the playground's examples menu (web/examples/list.txt) in the
// WebAssembly build of the interpreter, as the playground would, and checks that each
// exits with status 0 (or the status its line names: `| exit 134`), and that each is
// formatted as the playground's Format button would format it.
//
//   node web/check.mjs web/play/target/gamma-play.wasm web/play/target/gamma-play-libs.json
//   node web/check.mjs web/playground/target/wasm32-wasip1/release/gamma-playground.wasm
//
// With a libraries file (the JSON object that web/play/build.sh writes) the module is the
// one built from gammac, and it runs inside web/static/play-worker.js, the page's worker,
// with the file system that script serves from the libraries. Without one, the module is the
// Rust interpreter, which embeds its library, and runs under Node's own WASI.
//
// Each program runs in a child process of its own (Node's WASI does not reliably survive
// many instances in one process, and a trap in one must not end the others).

import { WASI } from "node:wasi";
import { readFileSync, openSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { createContext, runInContext } from "node:vm";

const args = process.argv.slice(2);
const oneAt = args.indexOf("--one");
const [wasmPath, libsPath] = oneAt < 0 ? args : args.slice(0, oneAt);
const [mode, path] = oneAt < 0 ? [] : args.slice(oneAt + 1);
if (!wasmPath) {
  console.error("usage: node web/check.mjs <module.wasm> [<libraries.json>]");
  process.exit(2);
}

// Runs `mode` on the program at `path` in the page's worker, with the messages the page
// would receive written to standard output and error.
async function inWorker(mode, path) {
  const sandbox = {
    TextEncoder, TextDecoder, WebAssembly, performance, crypto, Date, Uint8Array, DataView, BigInt,
    // The worker fetches the module and the libraries by URL: here, file names.
    fetch: async (url) => ({
      ok: true,
      headers: { get: () => "" },
      arrayBuffer: async () => { const b = readFileSync(url); return b.buffer.slice(b.byteOffset, b.byteOffset + b.length); },
      json: async () => JSON.parse(readFileSync(url, "utf8")),
    }),
    postMessage(m) {
      if (m.kind === "out") process.stdout.write(m.text);
      else if (m.kind === "err") process.stderr.write(m.text);
      else if (m.kind === "exit") process.exitCode = m.code;
      else if (m.kind === "fail") { process.stderr.write(m.text + "\n"); process.exitCode = 255; }
    },
  };
  createContext(sandbox);
  runInContext(readFileSync(new URL("./static/play-worker.js", import.meta.url), "utf8"), sandbox);
  await sandbox.onmessage({ data: { mode, source: readFileSync(path, "utf8"), wasm: wasmPath, libs: libsPath } });
}

if (oneAt >= 0) {
  // Child: run `mode` on the program at `path`; its output becomes ours.
  if (libsPath) {
    await inWorker(mode, path);
  } else {
    const module = await WebAssembly.compile(readFileSync(wasmPath));
    const wasi = new WASI({ version: "preview1", args: ["gamma-play", mode], env: {}, stdin: openSync(path, "r"), returnOnExit: true });
    const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
    process.exitCode = wasi.start(instance);
  }
} else {
  const libs = libsPath ? [libsPath] : [];
  let failed = 0;
  for (const line of readFileSync("web/examples/list.txt", "utf8").split("\n")) {
    if (!line.trim() || line.startsWith("#")) continue;
    const [file, title, expect = "exit 0"] = line.split("|").map((s) => s.trim());
    const want = Number(expect.replace(/^exit\s+/, ""));
    for (const m of ["run", "fmt"]) {
      let code = 0, out = "";
      const t0 = performance.now();
      try {
        out = execFileSync(process.execPath, ["--no-warnings", process.argv[1], wasmPath, ...libs, "--one", m, file], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
      } catch (e) {
        code = e.status ?? -1;
        out = e.stdout || "";
      }
      const ms = Math.round(performance.now() - t0);
      const formatted = m === "run" || out === readFileSync(file, "utf8");
      const ok = (m === "run" ? code === want : code === 0) && formatted;
      if (!ok) failed++;
      console.log(`${ok ? "ok  " : "FAIL"} ${m} ${file} (${title}): exit ${code}${formatted ? "" : ", not formatted"} [${ms} ms]`);
    }
  }
  if (failed) {
    console.error(`${failed} failed`);
    process.exit(1);
  }
}
