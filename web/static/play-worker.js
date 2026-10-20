// Runs the Gamma interpreter (gamma-play.wasm, a WASI command) for the playground page.
// Each request gets a fresh instance: {mode: "run" | "fmt", source}. Output is streamed
// back as {kind: "out" | "err", text} messages, then {kind: "exit", code, ms}.
// The page stops a run by terminating this worker.
//
// The interpreter needs only a small part of WASI: arguments, the environment, standard
// streams, clocks, randomness and sleeping. There is no file system (the standard library
// is built into the module), so every other call fails with ENOSYS.

"use strict";

const ESUCCESS = 0, EBADF = 8, EINVAL = 28, ENOSYS = 52;

class Exit extends Error {
  constructor(code) { super("exit " + code); this.code = code; }
}

let modulePromise = null;

function loadModule(url) {
  if (!modulePromise) {
    modulePromise = (async () => {
      const res = await fetch(url);
      if (!res.ok) throw new Error("cannot load the interpreter (" + res.status + ")");
      if (WebAssembly.compileStreaming && (res.headers.get("content-type") || "").includes("wasm")) {
        return WebAssembly.compileStreaming(res);
      }
      return WebAssembly.compile(await res.arrayBuffer());
    })();
  }
  return modulePromise;
}

// Batches small writes so that a program printing a character at a time stays fast.
function makeStream(kind) {
  const decoder = new TextDecoder();
  let pending = "";
  let last = 0;
  return {
    write(bytes) {
      pending += decoder.decode(bytes, { stream: true });
      const now = performance.now();
      if (pending.length > 8192 || now - last > 50) this.flush(now);
    },
    flush(now) {
      pending += decoder.decode();
      if (pending) postMessage({ kind, text: pending });
      pending = "";
      last = now || performance.now();
    },
  };
}

function run(module, mode, source) {
  const args = ["gamma-play", mode];
  const env = ["GAMMA_STD=/std"];
  const encoder = new TextEncoder();
  let stdin = encoder.encode(source);
  const out = makeStream("out"), err = makeStream("err");
  let memory = null;

  const view = () => new DataView(memory.buffer);
  const bytes = () => new Uint8Array(memory.buffer);

  // Writes a list of NUL-terminated strings and the pointers to them.
  function putStrings(list, ptrs, buf) {
    const v = view(), b = bytes();
    for (const s of list) {
      const e = encoder.encode(s + "\0");
      v.setUint32(ptrs, buf, true);
      b.set(e, buf);
      ptrs += 4;
      buf += e.length;
    }
    return ESUCCESS;
  }
  function putSizes(list, countPtr, sizePtr) {
    const v = view();
    v.setUint32(countPtr, list.length, true);
    v.setUint32(sizePtr, list.reduce((n, s) => n + encoder.encode(s).length + 1, 0), true);
    return ESUCCESS;
  }

  function now(id) {
    // 0: realtime; everything else is treated as monotonic.
    const ms = id === 0 ? Date.now() : performance.timeOrigin + performance.now();
    return BigInt(Math.round(ms * 1e6));
  }

  const wasi = {
    args_get: (p, b) => putStrings(args, p, b),
    args_sizes_get: (c, s) => putSizes(args, c, s),
    environ_get: (p, b) => putStrings(env, p, b),
    environ_sizes_get: (c, s) => putSizes(env, c, s),

    fd_write(fd, iovs, n, written) {
      if (fd !== 1 && fd !== 2) return EBADF;
      const v = view();
      let total = 0;
      for (let i = 0; i < n; i++) {
        const ptr = v.getUint32(iovs + i * 8, true), len = v.getUint32(iovs + i * 8 + 4, true);
        (fd === 1 ? out : err).write(bytes().slice(ptr, ptr + len));
        total += len;
      }
      view().setUint32(written, total, true);
      return ESUCCESS;
    },
    fd_read(fd, iovs, n, read) {
      if (fd !== 0) return EBADF;
      const v = view();
      let total = 0;
      for (let i = 0; i < n && stdin.length; i++) {
        const ptr = v.getUint32(iovs + i * 8, true), len = v.getUint32(iovs + i * 8 + 4, true);
        const k = Math.min(len, stdin.length);
        bytes().set(stdin.subarray(0, k), ptr);
        stdin = stdin.subarray(k);
        total += k;
      }
      view().setUint32(read, total, true);
      return ESUCCESS;
    },
    fd_fdstat_get(fd, ptr) {
      if (fd > 2) return EBADF;
      const v = view();
      v.setUint8(ptr, 2); // character device
      v.setUint16(ptr + 2, 0, true);
      v.setBigUint64(ptr + 8, 0n, true);
      v.setBigUint64(ptr + 16, 0n, true);
      return ESUCCESS;
    },
    fd_prestat_get: () => EBADF, // no preopened directories
    fd_close: () => ESUCCESS,

    clock_res_get(id, ptr) {
      view().setBigUint64(ptr, 1000n, true);
      return ESUCCESS;
    },
    clock_time_get(id, precision, ptr) {
      view().setBigUint64(ptr, now(id), true);
      return ESUCCESS;
    },
    random_get(ptr, len) {
      for (let off = 0; off < len; off += 65536) {
        crypto.getRandomValues(new Uint8Array(memory.buffer, ptr + off, Math.min(65536, len - off)));
      }
      return ESUCCESS;
    },
    sched_yield: () => ESUCCESS,

    // Sleeping: only clock subscriptions, waited for by spinning (a worker has no
    // blocking sleep without shared memory). The whole time is waited out, however
    // long; the page's Stop button ends the worker.
    poll_oneoff(inPtr, outPtr, n, countPtr) {
      const v = view();
      let wait = 0n;
      for (let i = 0; i < n; i++) {
        const s = inPtr + i * 48;
        if (v.getUint8(s + 8) !== 0) return ENOSYS;
        const id = v.getUint32(s + 16, true);
        let t = v.getBigUint64(s + 24, true);
        if (v.getUint16(s + 40, true) & 1) t -= now(id); // absolute time
        if (t > wait) wait = t;
      }
      const until = performance.now() + Number(wait) / 1e6;
      while (performance.now() < until) { /* spin */ }
      for (let i = 0; i < n; i++) {
        const s = inPtr + i * 48, e = outPtr + i * 32;
        v.setBigUint64(e, v.getBigUint64(s, true), true);
        v.setUint16(e + 8, 0, true);
        v.setUint8(e + 10, 0);
      }
      v.setUint32(countPtr, n, true);
      return ESUCCESS;
    },

    proc_exit(code) { throw new Exit(code); },
  };

  const imports = { wasi_snapshot_preview1: {} };
  for (const imp of WebAssembly.Module.imports(module)) {
    if (imp.kind !== "function") continue;
    imports[imp.module] ??= {};
    imports[imp.module][imp.name] = wasi[imp.name] || (() => ENOSYS);
  }
  const instance = new WebAssembly.Instance(module, imports);
  memory = instance.exports.memory;

  let code = 0;
  try {
    instance.exports._start();
  } catch (e) {
    if (e instanceof Exit) {
      code = e.code;
    } else {
      out.flush();
      err.flush();
      if (e instanceof RangeError) {
        postMessage({ kind: "err", text: "trap: stack overflow (the browser's stack is smaller than a native one)\n" });
      } else {
        postMessage({ kind: "err", text: "internal error: " + e.message + "\n" });
      }
      code = 134;
    }
  }
  out.flush();
  err.flush();
  return code;
}

onmessage = async (ev) => {
  const { mode, source, wasm } = ev.data;
  try {
    const module = await loadModule(wasm);
    if (mode === "load") {
      postMessage({ kind: "ready" });
      return;
    }
    const t0 = performance.now();
    const code = run(module, mode, source);
    postMessage({ kind: "exit", code, ms: performance.now() - t0 });
  } catch (e) {
    postMessage({ kind: "fail", text: String(e.message || e) });
  }
};
