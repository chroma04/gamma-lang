# 044 Language changes from the s38 pain points (s39, 2026-10-24)

User: "Implement the proposed language changes … and continue with the self-hosted checker".
Measured each pain point before designing; two needed no new language feature.

1. **Composite hash keys**: already worked. `hash_map((u32, []const Ty), u32)` and
   `HashMap(TyKind, u32)` hash/compare structurally (slices by content). gammac's byte-string
   keys were a misunderstanding; types interner (by_kind) and fn_instances now use them.
2. **Optional parts in payloads**: TRIED "nested bare binding binds as-is" in the reference:
   conformance passed, but 11 gammac sites rely on `.V(null) => … / .V(x) => …` (present-only
   nested binding) — the idiom is good. Kept §9.3; instead: `.Break(b)` binds a whole tuple
   payload (§9.2) so the optional parts are `b.label`; spec §9.3 says so; the non-exhaustive
   match error labels the nested present-only binding + note (Rust exhaust.rs present_only,
   gammac exhaust.gm/matching.gm). Handbook rewritten (no struct payloads needed).
3. **pub(package)**: `visibility = 'pub' [ '(' 'package' ')' ]` (`package` contextual).
   ast::Visibility {Private, Package, Public} (ordered) replaces is_pub on Item/FieldDecl/
   FieldDef (both impls). Sema::visible/decl_visible/same_package; module_member and
   public_module_member take `from`. Signature rule generalized (needs ≥ own vis; fields
   min(field, struct)). Same package = equal package index (core: only itself; gammac core
   modules have package 0 → same_package excludes is_prelude). std modules stay one package
   each (making std one package would change `gamma test` std runs + modules dump).
   FOUND HOLE: private fns of a type's module were callable as methods from other modules
   (spec §7.10 forbade it); now an error. gammac used it for 9 sema helpers → pub(package).
   fmt: Keyword::Pub is tight before `(`. tree-sitter: `visibility` node (prec.right).
4. **needless-pub** lint (sema/lint.rs needless_pub + Sema::outside_uses, filled by
   note_use_from in module_member/public_module_member/method calls/untyped m.X/
   ty_expr aliases). Fires for pub(package) fns/consts unused outside module, and `pub`
   ones only in a *program package* (gamma.toml + root has main). Types/effects/imports
   skipped (pub signatures may need them). Dropped 73 pubs in gammac.
5. **dangling-address** lint (sema/lint_escape.rs): scope-depth walk; reports &local held
   in a value stored to an outer-scope place/deref/global, passed for a bare type-param
   param with a mutable pointer to longer storage (push/put), or returned in an aggregate.
   No false positives on the repo; one true positive (example3 global = &x of main).
   Not detected: pointers into a List across push (needs liveness).
- Lint paths fix: load_package_modules starts from the package root relative to cwd.
6. **present-only-binding** lint (sema/lint_present.rs): a bare name at an optional/result
   position below the top of a match arm, when no other non-catch-all arm names the absent
   case at that path and a catch-all arm with a NON-EMPTY body exists (`_ => {}` = intended
   skip, not reported). Found the real gammac bug: `.Return(v)`/`.Continue(label)` in
   check/expr.gm sent bare `return`/`continue` to "this kind of expression" (~100 roots).
   Evidence for the pain point: the bug existed since stage 4b started.
