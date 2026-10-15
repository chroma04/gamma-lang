# 15. Packages

[Contents](README.md) · [Previous: Interoperating with C](14-interop.md) · [Next: The standard library](16-standard-library.md)

A **package** is a directory tree with a `gamma.toml` manifest at its root. This chapter covers the manifest, the three kinds of dependency (a directory next to yours, a git repository, or a source archive at a URL), the lockfile, build settings and profiles (including C sources that ship with a package), the cache, and the commands that manage them. The language itself only fixes that a manifest names a package and its dependencies (§3.1); the format below belongs to this implementation.

## Starting a package

`gamma new` makes a directory with a working package in it:

```text
$ gamma new hello
     Created package `hello` in hello

Next steps:
    gamma run hello
    gamma test hello
```

You get `gamma.toml`, a `main.gm` that prints a greeting and has a small function with a `test`, and a `.gitignore` for the executable that `gamma build .` writes. If `git` is installed and the directory is not already inside a git work tree, `gamma new` also runs `git init`; pass `--vcs=none` to skip that.

`gamma init [dir]` does the same in a directory that already exists (the current one by default). It never overwrites a file: anything already there is reported as skipped (and if `gamma.toml` is among them, the message says so instead of claiming a new package).

The package name is the directory name with every character that is not a letter, digit or `_` replaced by `_` (`my-app` becomes `my_app`; `gamma new` and `gamma init` print a note when the name differs from the directory's). Give another with `--name=<name>`. A package name must be an identifier: letters, digits and `_`, not starting with a digit, because it is the first segment of an import path. It also cannot be a keyword, `_`, a builtin type name (`i32`, `bool`, `string`, ...), or `std` or `core`; if the directory name gives such a name, pass `--name`.

Every package command finds its package by walking up from the directory you name (or the current one) to the nearest `gamma.toml`.

## The manifest

```toml
[package]
name = "app"
version = "0.1.0"

[dependencies]
json = { path = "../json" }
http = { git = "https://github.com/someone/http-gm", version = "1.2" }
util = { git = "https://example.com/util.git", tag = "v0.3.0" }
blob = { url = "https://example.com/blob-1.2.0.tar.gz", sha256 = "9f86…" }
```

`[package]` holds the `name` and a semantic `version`. Both are optional. `[dependencies]` maps a name to a source. The name is what you write in `import "json"`, so it must be a valid name as well (the same rules as for a package name); it does not have to match the dependency's own package name. A `gamma.toml` that does not follow these rules is reported with its line, as in `gamma.toml:6: dependency `x` has an unknown key `colour``. Keys that are not listed here are errors inside a dependency, and ignored elsewhere, except in the `[build]` and `[profile.*]` tables (see [Build settings](#build-settings)), where an unknown key is an error too.

### Path dependencies

`{ path = "dir" }` names a directory relative to the manifest. Nothing is fetched or locked. A `version` next to a `path` is allowed and ignored.

### Git dependencies

`{ git = "<url>" }` names a repository. It may carry one of these (at most one):

| Key | Meaning |
| --- | --- |
| `version = "1.2"` | The highest tag that matches the requirement |
| `tag = "v0.3.0"` | That tag |
| `branch = "main"` | The head of that branch, as of locking |
| `rev = "3f2a9c1"` | That commit, by a full id or a prefix |
| *(none)* | The head of the repository's default branch, as of locking |

Version requirements use the same syntax as Cargo: a bare `1.2` means `^1.2`, so it matches `1.2.0` up to, but not including, `2.0.0`. `~1.2`, `>=1.2, <1.5` and `=1.2.3` work as you would expect. A version tag is named `vX.Y.Z` or `X.Y.Z`; other tags are ignored for version requirements. A prerelease tag such as `v2.0.0-rc.1` only matches a requirement that itself mentions that prerelease. A `rev` or a tag must be reachable from a branch or tag of the repository.

If the package is not at the top of the repository, add `subdir = "path/in/repo"`. Several packages in one repository, each with its own `subdir`, are locked separately.

A git package is a package like any other: it needs its own `gamma.toml`, and its dependencies are resolved too. Its git dependencies end up in *your* lockfile. Its path dependencies are relative to its checkout, and a path that leaves the repository is an error.

### Archive dependencies

`{ url = "<url>" }` names a gzip-compressed tar archive (`.tar.gz` or `.tgz`), such as the source archive a code host offers for a release. It may carry:

| Key | Meaning |
| --- | --- |
| `sha256 = "9f86…"` | The expected SHA-256 of the archive file (64 hexadecimal digits, as `sha256sum` prints it). If present, a download that hashes differently is refused |
| `subdir = "path"` | The directory of the package inside the archive, if it is not the top |

Gamma downloads the file with the `curl` command-line tool (so proxy settings and the system's certificate authorities apply, and `file://` URLs work), unpacks it into the cache, and locks the URL together with the hash of the file and a checksum of the unpacked tree. If the archive holds a single top-level directory (`blob-1.2.0/`), that directory is the package root. An archive whose paths would leave its directory, or that contains hard links or special files, is rejected, and so is a symbolic link that points outside the package (see below).

Nothing is downloaded again while the lock covers the URL and the cache has the files. `gamma update` re-downloads only if you changed the URL; a file that changes at the same URL is not noticed, which is why the lock stores its hash: a fresh cache that downloads different bytes fails with `the archive of dependency `blob` (…) has the SHA-256 …, but gamma.lock says …`. Put a new version at a new URL. Two packages that name one URL share it, and a `sha256` in one manifest must agree with the file.

An archive package is a package like any other: it needs its own `gamma.toml`, its path dependencies must stay inside it, and its git and archive dependencies end up in your lockfile.

### One revision per repository

A repository is identified by its URL, with a trailing `/` and `.git` ignored, so `https://h/x/y.git` and `https://h/x/y` are the same repository. A program uses exactly one revision of it. When two packages require the same repository:

- Two version requirements are satisfied by the highest tag that matches both.
- A tag or branch must agree with every other requirement, or the build stops with an error naming both requirers, for example ``conflicting requirements for https://h/util: `a` requires tag `v1.0.0`, `app` requires tag `v1.1.0` ``.
- A dependency with no `version`, `tag`, `branch` or `rev` accepts whatever the others choose.

Gamma does not link two versions of one package side by side. Two packages that need incompatible versions cannot be used together until one of them is updated.

Dependency cycles that involve a git package are errors.

## The lockfile

The first time Gamma resolves a git or archive dependency it writes `gamma.lock` next to the root package's `gamma.toml`:

```toml
# This file is generated by `gamma`. Do not edit it by hand.
version = 1

[[package]]
name = "http"
source = "git+https://github.com/someone/http-gm"
version = "1.2.3"
reference = "tag=v1.2.3"
rev = "3f2a9c1d0b7e4a65c8d2e91f0a47b3c5d6e7f801"
checksum = "sha256:9c4b…"
dependencies = ["util"]

[[package]]
name = "blob"
source = "url+https://example.com/blob-1.2.0.tar.gz"
archive = "sha256:9f86…"
checksum = "sha256:41d2…"
dependencies = []
```

Each git entry records the exact commit (`rev`), what the manifest asked for (`reference`), the version when it came from a version tag, the other locked packages it uses, and a `checksum` of the files in the checkout. An archive entry records the `archive` hash of the downloaded file instead of the commit. Entries are sorted, so the file only changes when something real does.

**Commit `gamma.lock`** for programs, so that everyone who builds the program gets the same code. While the lock satisfies the manifest, a new tag or a moved branch in a repository changes nothing: the build keeps using the locked commit. When you edit a requirement, only the dependencies whose entry no longer satisfies it are resolved again; the rest stay put. `gamma update` is the way to move on.

The checksum is SHA-256 over the checkout, in order of path (with `/` separators, `.git` left out). A regular file contributes its path, a zero byte, its length as eight little-endian bytes, and its contents. A symbolic link contributes its path, a zero byte, eight `0xFF` bytes (a length no file has, so a link can never look like a file), the length of its target as eight little-endian bytes, and the target. A tree without symbolic links therefore has the checksum it always had. A link must be relative and stay inside the package (`a/b -> ../c` is fine if `a/..` is still inside); one that points elsewhere makes the package unusable, because its contents would depend on the machine: ``the package contains the symbolic link `leak.txt` -> `/etc/passwd`, which points outside the package``. When Gamma has to create a checkout it compares the result with the lock, and refuses to use it on a mismatch: `checksum mismatch for package http (expected sha256:…, found sha256:…)`. The commit already fixes the content; the checksum also catches a lockfile that was edited and a cache that was tampered with. A checkout that is already in the cache is hashed again, against the lock, each time the dependency is used (the cost is that of reading the package once). The recorded `version` follows from the tag, so it is checked against the tag name: a plain run rewrites an edited `version`, and `--locked` fails with `gamma.lock needs to be updated`. An empty `gamma.lock` is treated as a missing one.

If a locked commit has disappeared from the repository (history was rewritten), Gamma says so and suggests `gamma update`.

## Commands

| Command | What it does |
| --- | --- |
| `gamma fetch [dir]` | Resolves what the lock does not cover, downloads everything, writes `gamma.lock` |
| `gamma update [name...] [--dir <dir>]` | Moves the named dependencies (default: all) to the newest revisions their requirements allow, and prints `Updating http v1.2.3 -> v1.3.0` |
| `gamma add <name> --path <dir>` | Adds a path dependency to `gamma.toml` |
| `gamma add <name> --git <url> [--tag T \| --branch B \| --rev R \| --version REQ] [--subdir P]` | Adds a git dependency, then locks and fetches it |
| `gamma add <name> --url <url> [--sha256 H] [--subdir P]` | Adds an archive dependency, then downloads and locks it |
| `gamma cache dir` | Prints the cache directory |
| `gamma cache clean [--older-than=DAYS] [--all] [--dry-run]` | Frees cache space (see [The cache](#the-cache)) |
| `gamma remove <name>` | Removes a dependency and its lock entry |
| `gamma new <dir>`, `gamma init [dir]` | Create a package (see above) |

`add` and `remove` edit `gamma.toml` in place and keep your comments and layout. `add` refuses a name that already exists, and puts the file back as it was if the dependency cannot be resolved. Given `--git` with no reference, `add` writes `version = "^X.Y.Z"` for the newest release tag, or leaves out the reference if the repository has no version tags.

`gamma check`, `run`, `build` and `test` do the work of `fetch` first, but only when something is missing, so a normal build never touches the network. Progress goes to standard error, and only when something happens:

```text
    Fetching http (https://github.com/someone/http-gm)
     Locking http v1.2.3
```

Two options apply to `fetch`, `check`, `run`, `build` and `test` (`--profile=<name>`, described next, applies to the last four):

- `--locked` fails if `gamma.lock` is missing or would change. Use it in CI to be sure the committed lock is complete.
- `--offline` never uses the network. Everything must already be locked and in the cache; otherwise the command fails and says what is missing.

## Build settings

Everything a build needs beyond the sources can be written in `gamma.toml`, so that building takes no long command line and a dependency that wraps a C library just works. All keys are optional:

```toml
[build]                         # defaults for run, build, test and check
mode = "checked"                # debug | checked | safe | fast
debug-info = true               # as -g
cflags = ["-O2"]                # extra C compiler options
ldflags = ["-Wl,-rpath,/opt/x/lib"]
c-sources = ["c/shim.c"]        # C files, relative to the package, compiled and linked into the program
include-dirs = ["c/include"]    # -I for those C files
libs = ["z"]                    # extra -l<name>
lib-dirs = ["vendor/lib"]       # -L<dir>, relative to the package

[profile.release]               # selected with --profile=release; same keys
mode = "fast"
```

**Profiles.** `[profile.<name>]` has the same keys as `[build]`. `--profile=<name>` (on `check`, `run`, `build` and `test`) applies the profile over `[build]`: a key the profile writes *replaces* the one in `[build]` (lists are replaced, not extended), and a key it leaves out keeps the `[build]` value. There are no built-in profiles and profiles do not inherit from each other; `release` and `dev` are only names. An unknown profile is an error that names the known ones.

**Precedence**, from strongest to weakest: the command line (`--mode=…`, `-g`), the selected profile, `[build]`, the built-in defaults (Debug mode, no debug information). `mode` and `debug-info` affect `check` too, since programs can branch on `target.mode` at compile time.

**Flags.** `cflags` go to every C compiler invocation and `ldflags` to the link, before the environment's `CFLAGS` and `LDFLAGS`, so the user's environment can override a manifest. A Safe build or a cross build uses the same flags with its own compiler.

**Which packages count.** `mode`, `debug-info`, `cflags` and `ldflags` are read from the root package only (the package being built). `c-sources`, `include-dirs`, `libs` and `lib-dirs` are collected from the root package *and every dependency* (path, git and archive), each resolved relative to its own package, in the order the dependency graph is walked. Each package's C files are compiled with that package's `include-dirs` only, so two packages can both have a `config.h`. A dependency's profiles are ignored; it contributes its `[build]` table. In a dependency, these paths must be relative and stay inside the package; the root package may use absolute paths or `..`. A missing C file is an error that names the manifest.

**C sources in practice.** Declare the functions in a `foreign "c"` block (spec §19.2) and call them:

```gamma
c :: import "std/c"
shim :: foreign "c" {
    shim_add :: fn(a: c.c_int, b: c.c_int) -> c.c_int \ {}
}
```

- A native build (`build`, `run --backend=c`, `test`) compiles each C file with `-O2` (`-O1` in Debug mode, `-O0 -g` with debug information), `-fPIC`, `-pthread`, the package's `-I` directories and then the `cflags`, and links the objects with the program. `libs` become `-l` options and `lib-dirs` become `-L` options, with a run-time search path (`-Wl,-rpath`) for the same directories so the executable finds shared libraries there.
- The interpreter (`gamma run` without `--backend=c`) links the same objects into a shared library, loads it, and finds the symbols in it whatever library the `foreign` block names; `foreign "lib"` blocks for libraries in `lib-dirs` are looked up there first, then in the `-L` directories of `ldflags` and `LDFLAGS`. Safe builds and cross builds (`--target`) cannot load code into the interpreter, so `run` compiles those natively.
- Compiled objects and the interpreter's shared libraries are cached (in `<cache>/native`). The name of an object is a hash of the *preprocessed* C file (so a changed header counts), the compiler and its version, and all flags; a C file is compiled once per change, not once per build. The cached executables of `run --backend=c` also depend on the objects and on the size and date of the library files found in `lib-dirs`.
- Without a cache directory the objects are compiled into a temporary directory for each build.

## The cache

Fetched packages live in the same cache directory as compiled runtime objects: `$GAMMA_CACHE_DIR`, else `$XDG_CACHE_HOME/gamma`, else `~/Library/Caches/gamma` on macOS and `~/.cache/gamma` elsewhere (`gamma cache dir` prints it). Under it:

| Path | Contents |
| --- | --- |
| `git/db/` | one bare mirror per repository |
| `git/checkouts/<repo>/<rev>/` | the files of each revision in use |
| `git/url/<name>-<hash>/<hash>/` | each unpacked archive (`<name>` is the archive's file name) |
| `run/`, `native/`, `gm_rt-*` | cached executables, compiled C sources, the compiled runtime |

All of it can be deleted at any time; the next build fetches or compiles again from the lock. **Checkouts are read-only**: once a checkout is created and verified, Gamma removes the write permission from its files and directories, so editing a dependency in place fails with a permission error instead of silently changing every program that uses the checkout. To change a dependency, edit its repository (or use a `path` dependency while you work on it). Gamma restores permissions before it deletes a checkout.

Every build that uses a checkout touches a `<rev>.used` file next to it. `gamma cache clean` uses it:

```text
$ gamma cache clean
Removed 2 package checkouts, 1 repository mirror, 3 cached builds (14.2 MiB freed) in /home/me/.cache/gamma
```

- `gamma cache clean` removes checkouts and archives unused for 30 days, the mirrors that no checkout is left for, the cached executables, compiled C sources and runtime objects unused for 30 days (they carry their last use in their modification time), and half-written temporary files older than a day. `--older-than=<days>` changes the age; `--older-than=0` removes everything not in use right now. `--all` and `--older-than` cannot be combined.
- `gamma cache clean --all` removes everything Gamma keeps there (and nothing else in the directory), whatever its age.
- `--dry-run` reports what would be removed without removing it.


If the cache is turned off (`GAMMA_CACHE_DIR` set to an empty string), packages go to `.gamma/deps` inside the root package instead. `gamma new` puts `/.gamma/` in `.gitignore`.

Gamma runs the `git` command-line tool for all of this, with prompts turned off, so private repositories work with whatever credentials `git` already has (an SSH agent, a credential helper). If `git` fails, its message is part of the error.

## Common mistakes

> - **Forgetting to commit `gamma.lock`.** Without it, a fresh checkout resolves again and can pick newer revisions.
> - **A manifest edit that does not match the lock in CI.** Run `gamma fetch` locally and commit the result; `--locked` will then pass.
> - **Using a dependency without a `gamma.toml`.** A git package needs its own manifest, even an empty one.
> - **Writing a package name with a dash.** Use `_`; the name is part of an import path.

## Summary

- A package is a directory tree with a `gamma.toml`; `gamma new` and `gamma init` create one.
- A dependency is a `path`, a `git` repository with at most one of `version`, `tag`, `branch`, `rev`, or a `url` to a `.tar.gz` (optionally with its `sha256`).
- `gamma.lock` records the commit (or archive hash) and checksum of every git and archive package; commit it.
- `[build]` and `[profile.<name>]` hold `mode`, `debug-info`, `cflags`, `ldflags`, `c-sources`, `include-dirs`, `libs` and `lib-dirs`; the command line beats the profile, which beats `[build]`. C sources of the root package and all dependencies are compiled once per change and used by `run`, `build` and `test`.
- Checkouts in the cache are read-only; `gamma cache clean` frees the ones that are no longer used.
- `fetch`, `update`, `add` and `remove` manage dependencies; `--locked` and `--offline` make builds strict.
- One repository means one revision in a program.
