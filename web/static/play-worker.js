// Runs the Gamma interpreter (gamma-play.wasm, a WASI command) for the playground page.
// Each request gets a fresh instance: {mode: "run" | "fmt", source}. Output is streamed
// back as {kind: "out" | "err", text} messages, then {kind: "exit", code, ms}.
// The page stops a run by terminating this worker.
//
// The interpreter needs only a small part of WASI: arguments, the environment, standard
// streams, clocks, randomness, sleeping and, for the compiler it contains, a read-only
// file system. That file system is the libraries (`libs`, a JSON object from path to
// text, such as {"/std/io/io.gm": "..."}), served from memory under one preopened
// directory "/". A module that imports none of the file calls (the Rust interpreter,
// which embeds its library) runs without one. Every other call fails with ENOSYS.

"use strict";

const ESUCCESS = 0, EACCES = 2, EBADF = 8, EINVAL = 28, EISDIR = 31, ENOENT = 44, ENOSYS = 52, ENOTDIR = 54, EROFS = 69;
const FILETYPE_DIRECTORY = 3, FILETYPE_REGULAR_FILE = 4;

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

let libsPromise = null;

// The libraries as a tree of directories and files, loaded once.
function loadLibs(url) {
  if (!libsPromise) {
    libsPromise = (async () => {
      const res = await fetch(url);
      if (!res.ok) throw new Error("cannot load the libraries (" + res.status + ")");
      const encoder = new TextEncoder();
      const root = { dir: new Map() };
      for (const [path, text] of Object.entries(await res.json())) {
        const names = path.split("/").filter((n) => n);
        let node = root;
        for (const name of names.slice(0, -1)) {
          if (!node.dir.has(name)) node.dir.set(name, { dir: new Map() });
          node = node.dir.get(name);
        }
        node.dir.set(names[names.length - 1], { data: encoder.encode(text) });
      }
      return root;
    })();
  }
  return libsPromise;
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

function run(module, mode, source, libs) {
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


  // The file system: descriptor 3 is the preopened root; opened files and directories
  // follow. Nothing can be created, changed or removed.
  const fds = new Map([[3, { node: libs || { dir: new Map() }, pos: 0 }]]);
  let nextFd = 4;

  // The node at `path` below the directory of descriptor `fd`, or an error number.
  function lookup(fd, ptr, len) {
    const base = fds.get(fd);
    if (!base || !base.node.dir) return EBADF;
    const text = new TextDecoder().decode(bytes().subarray(ptr, ptr + len));
    const stack = [base.node];
    for (const name of text.split("/")) {
      if (name === "" || name === ".") continue;
      if (name === "..") { if (stack.length > 1) stack.pop(); continue; }
      const top = stack[stack.length - 1];
      if (!top.dir) return ENOTDIR;
      const next = top.dir.get(name);
      if (!next) return ENOENT;
      stack.push(next);
    }
    return stack[stack.length - 1];
  }

  function putFilestat(ptr, node) {
    const v = view();
    for (let i = 0; i < 64; i += 8) v.setBigUint64(ptr + i, 0n, true);
    v.setUint8(ptr + 16, node.dir ? FILETYPE_DIRECTORY : FILETYPE_REGULAR_FILE);
    v.setBigUint64(ptr + 24, 1n, true);
    v.setBigUint64(ptr + 32, BigInt(node.dir ? 0 : node.data.length), true);
  }

  // Copies from `data`, starting at `pos`, into the buffers of an iovec list; returns the
  // number of bytes copied.
  function readInto(data, pos, iovs, n) {
    const v = view();
    let total = 0;
    for (let i = 0; i < n && pos + total < data.length; i++) {
      const ptr = v.getUint32(iovs + i * 8, true), len = v.getUint32(iovs + i * 8 + 4, true);
      const k = Math.min(len, data.length - pos - total);
      bytes().set(data.subarray(pos + total, pos + total + k), ptr);
      total += k;
    }
    return total;
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
      if (fd === 0) {
        const total = readInto(stdin, 0, iovs, n);
        stdin = stdin.subarray(total);
        view().setUint32(read, total, true);
        return ESUCCESS;
      }
      const f = fds.get(fd);
      if (!f) return EBADF;
      if (f.node.dir) return EISDIR;
      const total = readInto(f.node.data, f.pos, iovs, n);
      f.pos += total;
      view().setUint32(read, total, true);
      return ESUCCESS;
    },
    fd_pread(fd, iovs, n, offset, read) {
      const f = fds.get(fd);
      if (!f) return EBADF;
      if (f.node.dir) return EISDIR;
      view().setUint32(read, readInto(f.node.data, Number(offset), iovs, n), true);
      return ESUCCESS;
    },
    fd_seek(fd, offset, whence, newOffset) {
      const f = fds.get(fd);
      if (!f || fd < 3) return EBADF;
      const size = f.node.dir ? 0 : f.node.data.length;
      const pos = Number(offset) + (whence === 0 ? 0 : whence === 1 ? f.pos : size);
      if (pos < 0) return EINVAL;
      f.pos = pos;
      view().setBigUint64(newOffset, BigInt(pos), true);
      return ESUCCESS;
    },
    fd_tell(fd, ptr) {
      const f = fds.get(fd);
      if (!f || fd < 3) return EBADF;
      view().setBigUint64(ptr, BigInt(f.pos), true);
      return ESUCCESS;
    },
    fd_fdstat_get(fd, ptr) {
      const f = fds.get(fd);
      if (fd > 2 && !f) return EBADF;
      const v = view();
      v.setUint8(ptr, fd <= 2 ? 2 : f.node.dir ? FILETYPE_DIRECTORY : FILETYPE_REGULAR_FILE); // 2: character device
      v.setUint16(ptr + 2, 0, true);
      v.setBigUint64(ptr + 8, fd <= 2 ? 0n : 0x1fffffffn, true);
      v.setBigUint64(ptr + 16, fd <= 2 ? 0n : 0x1fffffffn, true);
      return ESUCCESS;
    },
    fd_filestat_get(fd, ptr) {
      const f = fds.get(fd);
      if (!f) return EBADF;
      putFilestat(ptr, f.node);
      return ESUCCESS;
    },
    path_filestat_get(fd, flags, path, len, ptr) {
      const node = lookup(fd, path, len);
      if (typeof node === "number") return node;
      putFilestat(ptr, node);
      return ESUCCESS;
    },
    path_open(fd, dirflags, path, len, oflags, rightsBase, rightsInheriting, fdflags, opened) {
      const node = lookup(fd, path, len);
      if (typeof node === "number") return oflags & 1 && node === ENOENT ? EROFS : node; // O_CREAT
      if (oflags & 1 && oflags & 4) return EROFS; // O_CREAT | O_EXCL
      if (oflags & 8) return EROFS; // O_TRUNC
      if (oflags & 2 && !node.dir) return ENOTDIR; // O_DIRECTORY
      if (rightsBase & 0x40n && !node.dir) return EACCES; // a request to write (fd_write)
      fds.set(nextFd, { node, pos: 0 });
      view().setUint32(opened, nextFd++, true);
      return ESUCCESS;
    },
    path_readlink: () => EINVAL,
    fd_readdir(fd, buf, bufLen, cookie, used) {
      const f = fds.get(fd);
      if (!f) return EBADF;
      if (!f.node.dir) return ENOTDIR;
      const encoder = new TextEncoder();
      const entries = [...f.node.dir.entries()];
      let off = 0;
      for (let i = Number(cookie); i < entries.length && off < bufLen; i++) {
        const [name, node] = entries[i];
        const nameBytes = encoder.encode(name);
        const entry = new Uint8Array(24 + nameBytes.length);
        const head = new DataView(entry.buffer);
        head.setBigUint64(0, BigInt(i + 1), true); // d_next
        head.setBigUint64(8, BigInt(i + 1), true); // d_ino
        head.setUint32(16, nameBytes.length, true);
        head.setUint8(20, node.dir ? FILETYPE_DIRECTORY : FILETYPE_REGULAR_FILE);
        entry.set(nameBytes, 24);
        const k = Math.min(entry.length, bufLen - off);
        bytes().set(entry.subarray(0, k), buf + off);
        off += k;
      }
      view().setUint32(used, off, true);
      return ESUCCESS;
    },
    fd_prestat_get(fd, ptr) {
      if (fd !== 3 || !libs) return EBADF;
      view().setUint8(ptr, 0); // a directory
      view().setUint32(ptr + 4, 1, true); // whose name, "/", has one byte
      return ESUCCESS;
    },
    fd_prestat_dir_name(fd, ptr, len) {
      if (fd !== 3 || !libs) return EBADF;
      bytes()[ptr] = 0x2f;
      return ESUCCESS;
    },
    fd_close(fd) {
      if (fd > 3) fds.delete(fd);
      return ESUCCESS;
    },

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
      if (e instanceof RangeError || (e && e.name === "RangeError")) {
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
  const { mode, source, wasm, libs } = ev.data;
  try {
    const module = await loadModule(wasm);
    // A module that embeds its libraries needs none: a page without the file still runs it.
    const tree = libs ? await loadLibs(libs).catch(() => { libsPromise = null; return null; }) : null;
    if (mode === "load") {
      postMessage({ kind: "ready" });
      return;
    }
    const t0 = performance.now();
    const code = run(module, mode, source, tree);
    postMessage({ kind: "exit", code, ms: performance.now() - t0 });
  } catch (e) {
    postMessage({ kind: "fail", text: String(e.message || e) });
  }
};
