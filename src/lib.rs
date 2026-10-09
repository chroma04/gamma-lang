//! The Gamma compiler: lexer, parser, semantic analysis, interpreter and back end.
//!
//! See `docs/architecture.md` for an overview of the pipeline.

pub mod cgen;
pub mod diag;
pub mod driver;
pub mod float;
pub mod interp;
pub mod layout;
pub mod sema;
pub mod source;
pub mod symbol;
pub mod syntax;
pub mod tir;
pub mod types;
