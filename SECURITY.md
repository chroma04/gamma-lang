# Security policy

## Reporting a vulnerability

Please report security problems privately through GitHub's
[private vulnerability reporting](https://github.com/chroma04/gamma-lang/security/advisories/new)
(the repository's **Security** tab, then **Report a vulnerability**). Do not open a
public issue for them.

Include what you can of: the affected version or commit, a program or input that
shows the problem, the command you ran, and what an attacker gains. You should get
an answer within a week. Once a fix is released, the advisory is published with
credit to you unless you prefer otherwise.

## Supported versions

Gamma is before 1.0. Fixes go into the latest release and the `main` branch only.

## Scope

In scope:

- Memory-safety failures that the language promises to catch: a Safe-mode build
  (`--mode=safe`) that lets a program read or write memory it does not own, or a
  Checked or Debug build that skips a check the specification requires (§18.2).
- The compiler, the interpreter or a tool running code or writing files it should
  not when given an untrusted source file, package manifest or archive, for example
  `gamma fetch` writing outside the package cache.
- The install scripts (`tools/install.sh`, `tools/install.ps1`) and the release archives.

Not in scope:

- Programs built with `--mode=fast`, whose failed checks are undefined behavior by
  design (§18.1), operations the specification lists as unchecked (many-pointer
  indexing, `downcast_unchecked`), and foreign (C) code.
- Running a Gamma program: a program can do anything its effects allow, as any
  native program can. Compile-time evaluation runs only code whose effects
  cannot touch the outside world (§17), so a way around that is in scope.
- Denial of service through a source file that is slow to compile.
