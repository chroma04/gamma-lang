# Self-hosting roadmap (decisions/043; public doc docs/self-hosting.md)
State (s38b): stage 1 lexer + stage 2 parser done; parity (dump + JSON diags) on all ~980
.gm files, plain and --recover. gammac modules: source, diag, bignum, syntax (tokens,
lexer, token dump), ast (tree + dump), parse (parser), main.gm (lex|parse CLI).
Port took ~1 session by hand (no subagents): 1:1 file mapping to src/syntax/parser/*.rs
made it mechanical; matched on first full-corpus run.

## Next: stage 3 modules and packages
- What `gamma check` does before sema: driver module loading (module_files: every .gm in a
  dir; overlays), import resolution (§3.2: ./ ../ relative, std, deps from gamma.toml),
  pkg manifest (TOML subset!) + lock, cycle detection. Needs a TOML reader in Gamma
  (check std for one first; none as of s38 → write compiler/toml or std/toml?).
- Parity target: there is no dump for module graphs. Options: add `gamma check
  --dump-modules` (hidden/debug flag) to the reference, or compare only diagnostics of
  `gamma check` restricted to load/import errors. Decide at start (record in decisions/043).
## Later
- stage 4 checker (biggest; TIR), stage 5 C gen (reuse runtime/gm_rt.c), stage 6 bootstrap.
- Perf: gammac lex ~3 MB/s, parse whole repo 2.1 s native (reference 4 s incl. process
  spawns); profile later.
