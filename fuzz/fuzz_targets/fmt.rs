//! Formats text the parser accepts and checks that formatting never fails and is idempotent.
#![no_main]

use gamma::diag::Diagnostics;
use gamma::fmt::format_source;
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
    if diags.has_errors() {
        return;
    }
    let out = format_source(src).unwrap_or_else(|e| panic!("cannot format valid source: {e}"));
    let again = format_source(&out).unwrap_or_else(|e| panic!("cannot format formatted source: {e}"));
    assert_eq!(out, again, "formatting is not idempotent");
});
