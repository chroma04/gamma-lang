# The Gamma website

The site published at <https://chroma04.github.io/gamma-lang/>: a landing page, the guide,
the specification, the standard library reference, the other documents in `docs/`, and a
playground that runs Gamma programs in the browser. It is built by
`.github/workflows/pages.yml` on every pull request and published from `main`.

| Path | What it is |
| --- | --- |
| `layout.html` | The page frame shared by every page (`{{title}}`, `{{root}}`, `{{body}}`, …) |
| `pages/*.html` | Hand-written pages (the landing page, the playground); a front-matter comment on the first line sets the title, section, layout and description |
| `static/` | Copied to `assets/`: the stylesheet, the playground's scripts, the icon |
| `examples/` | The playground's own example programs; `examples/list.txt` is its menu |
| `sitegen/` | The generator: Markdown → HTML, highlighting with Gamma's own lexer, sidebars, link rewriting and a link check |
| `playground/` | The interpreter as a WASI command for the browser (`gamma-play.wasm`) |
| `check.mjs` | Runs every menu example in the WebAssembly interpreter with Node |

## Building it locally

```sh
# the interpreter for the playground (the crate's .cargo/config.toml picks the target)
rustup target add wasm32-wasip1
(cd web/playground && cargo build --release)
node web/check.mjs web/playground/target/wasm32-wasip1/release/gamma-playground.wasm

# the pages
cargo run --release --manifest-path web/sitegen/Cargo.toml -- --out _site
cp web/playground/target/wasm32-wasip1/release/gamma-playground.wasm _site/assets/gamma-play.wasm
python3 -m http.server -d _site 8000     # then open http://localhost:8000
```

Both crates are separate Cargo workspaces, so `cargo test` at the root does not build them.
The generator fails if any internal link or `#anchor` is broken: when a document links to
a heading, keep the link in step with the heading.

## How the playground works

`playground/` builds the ordinary interpreter for `wasm32-wasip1`. It reads the program
from standard input, checks it and runs it (or formats it, with the argument `fmt`),
exactly as `gamma run` would; the standard library is compiled into the module and served
to the compiler as in-memory files, so no file system is needed. The compiler builds
for WebAssembly with a few differences, all behind `cfg(target_family = "wasm")`:

- the Cranelift back end and libffi are left out, and a program with foreign
  declarations is rejected (`src/interp/ffi_wasm.rs`);
- the interpreter runs on the calling thread instead of a thread with a 1 GiB stack (the
  module is linked with a 32 MiB stack, `playground/.cargo/config.toml`);
- threads, fibers (`with … on`) and child processes fail at run time, as they would on a
  host that cannot create them.

In the browser, `static/play-worker.js` runs the module in a Web Worker with a small WASI
implementation: arguments, environment, standard streams, clocks, randomness and
sleeping. Output is streamed to the page as it is written; *Stop* terminates the worker.
`static/play.js` is the editor (a textarea over a highlighted copy), the examples menu,
and sharing: a shared link carries the program in the address, `#code=<base64url>`.
Code blocks in the guide that declare `main` get a "Run in the playground" link of that
form.

## Publishing

In the repository's settings, *Pages* → *Build and deployment* → *Source* must be
*GitHub Actions*. Pushes to `main` then publish the site.
