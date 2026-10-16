//! Lexes and parses arbitrary text. Diagnostics are fine; panics and hangs are bugs.
#![no_main]

use gamma::diag::Diagnostics;
use gamma::source::FileId;
use gamma::syntax::ast::NodeIdGen;
use gamma::syntax::{lexer, parser};
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let Ok(src) = std::str::from_utf8(data) else { return };
    let mut diags = Diagnostics::new();
    let lexed = lexer::lex(FileId(0), src, &mut diags);
    let mut ids = NodeIdGen::new();
    parser::parse_file(FileId(0), &lexed, &mut ids, &mut diags);

    let mut diags = Diagnostics::new();
    let lexed = lexer::lex_recovering(FileId(0), src, &mut diags);
    let mut ids = NodeIdGen::new();
    parser::parse_file(FileId(0), &lexed, &mut ids, &mut diags);
});
