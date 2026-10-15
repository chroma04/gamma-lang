//! The Gamma compiler: lexer, parser, semantic analysis, interpreter, C back end, formatter,
//! package manager, language server, REPL, documentation generator and test runner.
//!
//! See `docs/architecture.md` for an overview of the pipeline.

pub mod cgen;
pub mod coverage;
pub mod diag;
pub mod doc;
pub mod driver;
pub mod float;
pub mod fmt;
pub mod ice;
pub mod ide;
pub mod interp;
pub mod layout;
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
