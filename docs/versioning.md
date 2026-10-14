# Versioning and stability

Gamma has three things that change over time: the language (`gamma-spec.md`), the
standard library (`lib/core` and `lib/std`), and the `gamma` tool (its commands, options
and file formats). They share one version number, the version of the release, written
`MAJOR.MINOR.PATCH` as in [Semantic Versioning](https://semver.org). The version is the
`version` in `Cargo.toml`; a release is the git tag `vMAJOR.MINOR.PATCH`, and every change
it brings is listed in [`CHANGELOG.md`](../CHANGELOG.md).

## Before 1.0

Releases `0.x.y` are previews. The language is complete and implemented, but it has had
few users, and experience will change it.

- A **minor** release (`0.2.0`) may change the language, the library and the tool in
  incompatible ways. Each such change is listed under **Breaking** in the changelog, with
  what to do about it.
- A **patch** release (`0.1.1`) only fixes bugs: a program that compiles and behaves as
  the specification says keeps doing so. A fix can still reject a program that was
  wrongly accepted.

Package version requirements follow the same rule as Cargo's: `0.1` means `>=0.1.0,
<0.2.0`, so a package that requires one 0.x line does not move to the next by itself.

## From 1.0 on

- **Language.** A program accepted by a `1.x` compiler is accepted by every later `1.y`
  compiler and means the same. A minor release may add features and fix the
  specification where it contradicts itself or the reference implementation in a way
  no program can rely on. A new keyword is introduced only in a major release. Behavior
  the specification calls undefined or implementation-defined is not covered.
- **Standard library.** A released public name changes or disappears only after it has
  been marked `#deprecated` (§20.2) for at least one minor release (§22.3). New names
  may be added in any minor release; since a module's names can hide the universe's, a
  new name can, rarely, require a rename in a program that defined the same name.
- **Tool.** Commands, options, `gamma.toml` keys and the `gamma.lock` format keep
  working throughout a major version. Options that only change output for humans
  (diagnostic wording, test-report layout) may change in any release; the
  machine-readable forms (`--diag-format=short`, exit statuses) do not.
- **Generated C and the runtime** are internal and may change in any release. Programs
  must not depend on the names or layout of generated C beyond what §19 defines.

## Diagnostics are not covered

The wording of error messages, the set of warnings and the order of diagnostics may change
in any release. A new warning never stops a build.

## Recording changes

Each pull request that changes behavior adds a line to the `Unreleased` section of
`CHANGELOG.md`, under one of **Breaking**, **Added**, **Changed** or **Fixed**. A
change to the specification names the section it touches. When a release is cut, the
`Unreleased` section becomes the section of that release.

## Cutting a release

1. Move the `Unreleased` entries of `CHANGELOG.md` under a heading for the new version
   and date, and set `version` in `Cargo.toml` to match (then `cargo build` to update
   `Cargo.lock`).
2. Run the Release workflow by hand on that commit (Actions → Release → Run workflow).
   It builds and smoke-tests the archives for every platform without publishing them.
3. Merge, then tag the merge commit `vX.Y.Z` and push the tag. The workflow checks that
   the tag matches `Cargo.toml`, builds the archives again and publishes the release with
   `install.sh` and `SHA256SUMS` attached.
4. Check the installer on a clean Linux and macOS machine:
   `curl -fsSL https://github.com/chroma04/gamma-lang/releases/latest/download/install.sh | sh`,
   then `gamma run` and `gamma run --backend=c` on a small program.
