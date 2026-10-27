// The playground page: a highlighted editor, the examples menu, and the buttons that run
// or format the program in play-worker.js. The program is kept in the address
// (#code=<base64url>) when shared, and in local storage between visits.

"use strict";

(() => {
  const $ = (id) => document.getElementById(id);
  const editor = $("source"), shadow = $("shadow").firstChild, output = $("output");
  const runBtn = $("run"), fmtBtn = $("fmt"), shareBtn = $("share"), status = $("status"), menu = $("examples");
  const WASM = new URL("assets/gamma-play.wasm", location.href).href;
  const LIBS = new URL("assets/gamma-play-libs.json", location.href).href;
  const KEY = "gamma-playground-source";
  const examples = window.GAMMA_EXAMPLES || [];

  // ------------------------------------------------------------------ highlighting

  const KEYWORDS = new Set(("and break cancel continue ctl defer effect else errdefer esc false fn for foreign " +
    "handler if import in is match needs not null or pub resume return struct true try union using when while with " +
    "const on where test").split(" "));
  const TOKEN = new RegExp([
    String.raw`(\/\/[^\n]*)`,                                   // 1 line comment
    String.raw`(\/\*)`,                                         // 2 block comment (nests)
    String.raw`(c?"(?:[^"\\\n]|\\.)*"?)`,                       // 3 string
    String.raw`('(?:[^'\\\n]|\\[^\n])*'?)`,                     // 4 character
    String.raw`(\x60+)`,                                        // 5 raw string
    String.raw`(0x[0-9a-fA-F_]+(?:\.[0-9a-fA-F_]+)?(?:p[+-]?\d+)?\w*|0[ob][0-7_]+\w*|\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][+-]?\d+)?\w*)`, // 6 number
    String.raw`([@#][A-Za-z_]\w*)`,                             // 7 intrinsic, attribute
    String.raw`([A-Za-z_]\w*)`,                                 // 8 identifier
  ].join("|"), "g");

  const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  const span = (cls, s) => `<span class="${cls}">${esc(s)}</span>`;

  function highlight(src) {
    let html = "", at = 0, m;
    TOKEN.lastIndex = 0;
    while ((m = TOKEN.exec(src))) {
      html += esc(src.slice(at, m.index));
      let text = m[0];
      if (m[1]) html += span("c", text);
      else if (m[2]) {
        let depth = 0, i = m.index;
        while (i < src.length) {
          if (src.startsWith("/*", i)) { depth++; i += 2; }
          else if (src.startsWith("*/", i)) { depth--; i += 2; if (!depth) break; }
          else i++;
        }
        text = src.slice(m.index, i);
        html += span("c", text);
      } else if (m[3] || m[4]) html += span("s", text);
      else if (m[5]) {
        const close = src.indexOf(text, m.index + text.length);
        const end = close < 0 ? src.length : close + text.length;
        text = src.slice(m.index, end);
        html += span("s", text);
      } else if (m[6]) html += span("n", text);
      else if (m[7]) html += span("a", text);
      else if (KEYWORDS.has(text)) html += span("k", text);
      else if (/^[A-Z]/.test(text)) html += span("t", text);
      else if (src[m.index + text.length] === "(" || /^\s*::\s*fn\b/.test(src.slice(m.index + text.length, m.index + text.length + 12))) html += span("f", text);
      else html += esc(text);
      at = m.index + text.length;
      TOKEN.lastIndex = at;
    }
    return html + esc(src.slice(at)) + "\n";
  }

  function refresh() {
    shadow.innerHTML = highlight(editor.value);
    syncScroll();
  }
  function syncScroll() {
    shadow.parentNode.scrollTop = editor.scrollTop;
    shadow.parentNode.scrollLeft = editor.scrollLeft;
  }

  // ------------------------------------------------------------------ editing

  // Replaces the selection so that the browser's undo history keeps working.
  function insert(text) {
    if (!document.execCommand("insertText", false, text)) editor.setRangeText(text, editor.selectionStart, editor.selectionEnd, "end");
  }

  editor.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === "Enter") { e.preventDefault(); start("run"); return; }
    if ((e.ctrlKey || e.metaKey) && e.shiftKey && (e.key === "f" || e.key === "F")) { e.preventDefault(); start("fmt"); return; }
    const v = editor.value, s = editor.selectionStart, end = editor.selectionEnd;
    const lineStart = v.lastIndexOf("\n", s - 1) + 1;
    if (e.key === "Tab" && !e.ctrlKey && !e.metaKey && !e.altKey) {
      e.preventDefault();
      if (s === end && !e.shiftKey) { insert("    "); return; }
      // Indent or outdent the selected lines.
      const block = v.slice(lineStart, end);
      const changed = e.shiftKey ? block.replace(/^ {1,4}/gm, "") : block.replace(/^/gm, "    ");
      editor.setSelectionRange(lineStart, end);
      insert(changed);
      editor.setSelectionRange(lineStart, lineStart + changed.length);
    } else if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      const indent = v.slice(lineStart, s).match(/^ */)[0];
      const opens = /[{(\[]\s*$/.test(v.slice(lineStart, s));
      insert("\n" + indent + (opens ? "    " : ""));
    } else if (e.key === "}" && s === end && /^ +$/.test(v.slice(lineStart, s))) {
      e.preventDefault();
      editor.setSelectionRange(Math.max(lineStart, s - 4), s);
      insert("}");
    }
  });
  editor.addEventListener("input", () => { refresh(); save(); });
  editor.addEventListener("scroll", syncScroll);

  let saveTimer = 0;
  function save() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => { try { localStorage.setItem(KEY, editor.value); } catch (e) { /* private mode */ } }, 300);
  }

  function setSource(text) {
    editor.value = text;
    editor.setSelectionRange(0, 0);
    editor.scrollTop = 0;
    refresh();
    save();
  }

  // ------------------------------------------------------------------ sharing

  function encode(text) {
    let bin = "";
    for (const b of new TextEncoder().encode(text)) bin += String.fromCharCode(b);
    return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  }
  function decode(s) {
    const bin = atob(s.replace(/-/g, "+").replace(/_/g, "/"));
    return new TextDecoder().decode(Uint8Array.from(bin, (c) => c.charCodeAt(0)));
  }
  function fromHash() {
    const m = location.hash.match(/^#code=([A-Za-z0-9_-]*)/);
    if (!m) return null;
    try { return decode(m[1]); } catch (e) { return null; }
  }

  shareBtn.addEventListener("click", async () => {
    const url = location.href.split("#")[0] + "#code=" + encode(editor.value);
    history.replaceState(null, "", url);
    try {
      await navigator.clipboard.writeText(url);
      flash("Link copied");
    } catch (e) {
      flash("Link in the address bar");
    }
  });

  let flashTimer = 0;
  function flash(text) {
    status.textContent = text;
    clearTimeout(flashTimer);
    flashTimer = setTimeout(() => { if (status.textContent === text) status.textContent = ""; }, 2500);
  }

  // ------------------------------------------------------------------ examples

  for (const [i, ex] of examples.entries()) {
    const o = document.createElement("option");
    o.value = i;
    o.textContent = ex.title;
    menu.appendChild(o);
  }
  menu.addEventListener("change", () => {
    const ex = examples[menu.value];
    if (!ex) return;
    setSource(ex.source);
    history.replaceState(null, "", location.pathname);
    output.innerHTML = "";
    menu.blur();
    start("run");
  });

  // ------------------------------------------------------------------ running

  let worker = null, busy = null, ready = false, stopTimer = 0;

  function spawn() {
    worker = new Worker("assets/play-worker.js");
    worker.onmessage = onMessage;
    worker.onerror = (e) => { finish(); show("err", "The interpreter could not start: " + (e.message || "unknown error") + "\n"); };
    worker.postMessage({ mode: "load", wasm: WASM, libs: LIBS });
  }

  function show(kind, text) {
    const last = output.lastElementChild;
    if (last && last.tagName === "PRE" && last.dataset.kind === kind) {
      last.appendChild(document.createTextNode(text));
    } else {
      const pre = document.createElement("pre");
      pre.dataset.kind = kind;
      if (kind === "err") pre.className = "stderr";
      pre.textContent = text;
      output.appendChild(pre);
    }
    output.scrollTop = output.scrollHeight;
  }
  function meta(text) {
    const p = document.createElement("p");
    p.className = "meta";
    p.textContent = text;
    output.appendChild(p);
    output.scrollTop = output.scrollHeight;
  }

  function onMessage(ev) {
    const m = ev.data;
    if (m.kind === "ready") {
      ready = true;
      runBtn.disabled = fmtBtn.disabled = false;
      if (status.textContent === "Loading the interpreter…") status.textContent = "";
      return;
    }
    if (m.kind === "fail") { finish(); show("err", m.text + "\n"); return; }
    if (!busy) return;
    if (busy.mode === "fmt") {
      if (m.kind === "out") busy.text += m.text;
      else if (m.kind === "err") busy.err += m.text;
      else if (m.kind === "exit") {
        const { text, err } = busy;
        finish();
        if (m.code === 0) {
          if (text !== editor.value) {
            const pos = editor.selectionStart;
            editor.select();
            insert(text);
            editor.setSelectionRange(Math.min(pos, text.length), Math.min(pos, text.length));
            refresh();
            save();
          }
          flash("Formatted");
        } else {
          output.innerHTML = "";
          show("err", err || "cannot format the program\n");
        }
      }
      return;
    }
    if (m.kind === "out" || m.kind === "err") show(m.kind, m.text);
    else if (m.kind === "exit") {
      finish();
      const time = m.ms < 1000 ? Math.round(m.ms) + " ms" : (m.ms / 1000).toFixed(2) + " s";
      meta((m.code === 0 ? "Exited normally" : "Exit status " + m.code) + " · " + time);
    }
  }

  function start(mode) {
    if (busy) { if (mode === "run") stop(); return; }
    if (!ready) { flash("Still loading the interpreter…"); return; }
    busy = { mode, text: "", err: "" };
    if (mode === "run") {
      output.innerHTML = "";
      runBtn.firstChild.textContent = "Stop";
      clearTimeout(stopTimer);
      stopTimer = setTimeout(() => { if (busy) status.textContent = "Running…"; }, 400);
    }
    fmtBtn.disabled = true;
    worker.postMessage({ mode, source: editor.value, wasm: WASM, libs: LIBS });
  }

  function finish() {
    busy = null;
    clearTimeout(stopTimer);
    runBtn.firstChild.textContent = "Run";
    fmtBtn.disabled = false;
    if (status.textContent === "Running…") status.textContent = "";
  }

  // A run cannot be interrupted from inside, so stopping restarts the worker.
  function stop() {
    worker.terminate();
    finish();
    meta("Stopped");
    ready = false;
    runBtn.disabled = fmtBtn.disabled = true;
    spawn();
  }

  runBtn.addEventListener("click", () => start("run"));
  fmtBtn.addEventListener("click", () => start("fmt"));

  // ------------------------------------------------------------------ start

  let initial = fromHash();
  if (initial === null) { try { initial = localStorage.getItem(KEY); } catch (e) { initial = null; } }
  if (initial === null && examples.length) initial = examples[0].source;
  setSource(initial || "");
  window.addEventListener("hashchange", () => { const t = fromHash(); if (t !== null) setSource(t); });

  runBtn.disabled = fmtBtn.disabled = true;
  status.textContent = "Loading the interpreter…";
  spawn();
})();
