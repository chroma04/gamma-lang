//! The parser (spec Appendix A). Placeholder; see `parse_file`.

use crate::diag::Diagnostics;
use crate::source::{FileId, Span};
use crate::syntax::ast::{File, NodeIdGen};
use crate::syntax::token::LexOutput;

/// Parses one lexed file into an AST, reporting syntax errors to `diags`.
pub fn parse_file(file: FileId, lexed: &LexOutput, _ids: &mut NodeIdGen, _diags: &mut Diagnostics) -> File {
    let end = lexed.tokens.last().map_or(0, |t| t.span.hi);
    File { items: vec![], span: Span::new(file, 0, end) }
}
