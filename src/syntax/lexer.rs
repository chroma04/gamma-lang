//! The lexer (spec §2). Placeholder; see `lex`.

use crate::diag::Diagnostics;
use crate::source::{FileId, Span};
use crate::syntax::token::{LexOutput, Token, TokenKind};

/// Converts source text into tokens, applying the newline rules of §2.8.
pub fn lex(file: FileId, src: &str, _diags: &mut Diagnostics) -> LexOutput {
    let end = src.len() as u32;
    LexOutput { tokens: vec![Token { kind: TokenKind::Eof, span: Span::new(file, end, end) }], docs: vec![] }
}
