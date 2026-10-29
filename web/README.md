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
| `gen/` | The generator, written in Gamma: Markdown → HTML, highlighting with Gamma's own lexer, sidebars, link rewriting and a link check |
| `play/` | The playground's interpreter built from `compiler/`, as a WASI command for the browser (`gamma-play.wasm`), and the script that builds it |
| `check.mjs` | Runs every menu example in the WebAssembly interpreter with Node |

## Building it locally

Everything is built by `gamma` (see `bootstrap/README.md` for building one with only a C
compiler). The playground's interpreter needs, besides `gamma`, clang with the WASI C
library and Binaryen's `wasm-opt` (see `play/build.sh`); the generator needs only `gamma`:

```sh
GAMMA=target/bootstrap/gamma CC="clang --target=wasm32-wasi" web/play/build.sh target/web/play
node web/check.mjs target/web/play/gamma-play.wasm target/web/play/gamma-play-libs.json
gamma build web/gen -o target/web/sitegen
target/web/sitegen --out _site          # --root DIR names the repository (default: .)
cp target/web/play/gamma-play.wasm target/web/play/gamma-play-libs.json _site/assets/
python3 -m http.server -d _site 8000     # then open http://localhost:8000
```

The generator fails if any internal link or `#anchor` is broken: when a document links to
a heading, keep the link in step with the heading. `gamma test web/gen` runs its unit
tests.

## How the playground works

`play/main.gm` is a small Gamma program that depends on the `gammac` package
(`compiler/`) and reuses its loader, checker, interpreter and formatter. Built with
`gamma build --target=wasm32-wasi`, it is a WASI command that reads the program from
standard input and checks and runs it in the interpreter (`run`, as
`gamma run --backend=interp` does), or formats it (`fmt`, as `gamma fmt -` does). The exit
status is the program's, 1 for a program with errors and 134 after a trap; diagnostics
and traps go to standard error in the human format. A program that calls foreign code or
uses assembly is refused, since the module has no C compiler to fall back on.

The compiler reads the standard and core libraries from files. A browser module has none,
so the page serves them to it: `play/tools/bundle.gm` (run by `build.sh`) writes the
`.gm` files of `lib/std` and `lib/core` into one JSON object, `gamma-play-libs.json`
(about 600 KB, 160 KB compressed), and the worker keeps them as an in-memory tree
that it presents to the module as a read-only preopened directory `/`, with `/std` and
`/core` in it. The program itself is not a file: `main.gm` hands it to the loader as an
in-memory buffer that stands in for `/play/main.gm`, so the module needs no writable
directory.

Every run starts by loading and checking the core library, which is the same work each
time, so the worker does it once. When the page loads, it runs the module in a third
mode, `init`, which does only that and keeps the result in the module's memory (a
`#global`), and saves the memory pages that `init` changed. Each run then gets a fresh
instance with those pages copied in, and calls the module's export `gamma_play_run`
instead of starting it (the C library's start-up code refuses to run twice in one
memory): the run finds the core library ready. A small program then takes about 10 ms
instead of 40. After that, the worker runs a small program with its output discarded, so
that the browser has compiled most of the module's functions before the first real run.
A module without the export (or whose `init` fails) is started for every run as before.

The interpreter runs fibers (`with … on`, generators, the scheduler of `std/fiber`) and
threads, which the compiler's own interpreter provides itself; the module switches its
fibers with Binaryen's Asyncify, which `gamma build` applies. What a program can notice: the
first run after the page loads is slower than the next ones (the module is about 8 MB, 3 MB
compressed, and the browser compiles its functions as they are first called), it sees no
command-line arguments or environment variables, a trap's location line names the
function (`at /play/main.gm:4:19 in main`), and a runaway recursion ends when the
browser's stack does (status 134 with a message from the worker) instead of at the
interpreter's own limit.

In the browser, `static/play-worker.js` runs the module in a Web Worker with a small WASI
implementation: arguments, environment, standard streams, clocks, randomness, sleeping and
the file system above. Output is streamed to the page as it is written; *Stop* terminates
the worker. `static/play.js` is the editor (a textarea over a highlighted copy), the
examples menu, and sharing: a shared link carries the program in the address,
`#code=<base64url>`. Code blocks in the guide that declare `main` get a "Run in the
playground" link of that form. `check.mjs` runs the same worker script under Node (with
stand-ins for the page's `fetch` and `postMessage`), so it exercises the file system and
the message protocol that the page uses.

## Publishing

In the repository's settings, *Pages* → *Build and deployment* → *Source* must be
*GitHub Actions*. Pushes to `main` then publish the site.

## The generator

`gen/` is the generator, written in Gamma. It takes `--out DIR` and `--root DIR` and writes
the site. It needs `gamma` (see `bootstrap/README.md` for building one with only a C
compiler):

```sh
gamma build web/gen -o _gen            # or: gamma run web/gen --out _site
./_gen --out _site                     # --root DIR names the repository (default: .)
gamma test web/gen                     # the unit tests
```

The modules are `markdown/` (a CommonMark parser with pipe tables and strikethrough, and
the HTML output), `highlight/`, `pages/`, `links/`, `check/` and `examples/`, with
`main.gm` as the command line.

- Highlighting uses the compiler's own lexer. `gen/gamma.toml` has a path dependency on
  `compiler/` (the package `gammac`), and `highlight/` imports `gammac/syntax`; nothing is
  copied, so a change to the lexer shows up in the site at once.
- The Markdown parser implements what the documents in this repository use, and more of
  CommonMark than that where it was cheap: block quotes, lists, indented and fenced code,
  HTML blocks, reference definitions, setext and ATX headings, emphasis, links, images,
  autolinks and raw inline HTML. Not supported: footnotes; named character references
  other than a common subset (all numeric ones work); and, outside ASCII, letters and
  lower-casing in heading ids beyond the Latin, Greek and Cyrillic alphabets.
- Tests run on fakes of the file system and the allocator.
