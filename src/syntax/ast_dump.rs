//! A stable, span-free textual rendering of the AST, used by `gamma parse --dump`
//! and by golden parser tests. Placeholder.

use crate::syntax::ast::File;

pub fn dump_file(file: &File) -> String {
    format!("(file {})\n", file.items.len())
}
