// Runs every program of the playground's examples menu (web/examples/list.txt) in the
// WebAssembly build of the interpreter, as the playground would, and checks that each
// exits with status 0 (or the status its line names: `| exit 134`), and that each is
// formatted as the playground's Format button would format it.
//
//   node web/check.mjs web/playground/target/wasm32-wasip1/release/gamma-playground.wasm
//
// Each program runs in a child process of its own (Node's WASI does not reliably survive
// many instances in one process).

import { WASI } from "node:wasi";
import { readFileSync, openSync } from "node:fs";
import { execFileSync } from "node:child_process";

const [wasmPath, one, mode, path] = process.argv.slice(2);
if (!wasmPath) {
  console.error("usage: node web/check.mjs <gamma-playground.wasm>");
  process.exit(2);
}

if (one === "--one") {
  // Child: run `mode` on the program at `path`; its output becomes ours.
  const module = await WebAssembly.compile(readFileSync(wasmPath));
  const wasi = new WASI({ version: "preview1", args: ["gamma-play", mode], env: {}, stdin: openSync(path, "r"), returnOnExit: true });
  const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
  process.exitCode = wasi.start(instance);
} else {
  let failed = 0;
  for (const line of readFileSync("web/examples/list.txt", "utf8").split("\n")) {
    if (!line.trim() || line.startsWith("#")) continue;
    const [file, title, expect = "exit 0"] = line.split("|").map((s) => s.trim());
    const want = Number(expect.replace(/^exit\s+/, ""));
    for (const m of ["run", "fmt"]) {
      let code = 0, out = "";
      try {
        out = execFileSync(process.execPath, ["--no-warnings", process.argv[1], wasmPath, "--one", m, file], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
      } catch (e) {
        code = e.status ?? -1;
        out = e.stdout || "";
      }
      const formatted = m === "run" || out === readFileSync(file, "utf8");
      const ok = (m === "run" ? code === want : code === 0) && formatted;
      if (!ok) failed++;
      console.log(`${ok ? "ok  " : "FAIL"} ${m} ${file} (${title}): exit ${code}${formatted ? "" : ", not formatted"}`);
    }
  }
  if (failed) {
    console.error(`${failed} failed`);
    process.exit(1);
  }
}
