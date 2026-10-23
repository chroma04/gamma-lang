//! The Gamma compiler: lexer, parser, semantic analysis, interpreter, C back end, formatter,
//! package manager, language server, REPL, documentation generator and test runner.
//!
//! See `docs/architecture.md` for an overview of the pipeline.

pub mod cgen;
#[cfg(not(target_family = "wasm"))]
pub mod clif;
pub mod coverage;
pub mod diag;
pub mod doc;
pub mod driver;
pub mod fix;
pub mod float;
pub mod fmt;
pub mod ice;
pub mod ide;
pub mod interp;
pub mod layout;
pub mod lint;
pub mod llvm;
pub mod lsp;
pub mod pkg;
pub mod repl;
pub mod sema;
pub mod source;
pub mod symbol;
pub mod syntax;
pub mod tir;
pub mod toolchain;
pub mod types;
