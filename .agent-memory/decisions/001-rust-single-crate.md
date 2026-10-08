# 001 Implementation language: Rust, single crate
- Rust (toolchain 1.97 available; good enums/matching for compilers; cargo test harness).
- One crate `gamma` (lib + bin). Modules: source, diag, symbol, syntax/{token,lexer,parser,ast,ast_dump},
  driver; later sema/, tir/, interp/, cgen/. Split into crates only if build times hurt.
- Deps kept minimal: num-bigint 0.4 (+ num-rational 0.4 w/ num-bigint feature, num-traits,
  num-integer) for unbounded literal values / untyped constants / wide ints. num-bigint must stay
  0.4 to match num-rational 0.4.
- rustfmt: max_width 120, use_small_heuristics=Max. dev profile opt-level=1.
