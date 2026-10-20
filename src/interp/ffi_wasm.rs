//! Calls into C on WebAssembly hosts: there is no C library to load, so programs with
//! foreign declarations are rejected up front and a foreign call traps. This keeps the
//! interface of `ffi.rs` (built everywhere else).

use super::RunConfig;
use super::machine::Machine;
use super::value::*;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;

/// Does nothing: the interpreter never owns the process on this host.
pub fn set_owns_process(_on: bool) {}

/// Does nothing: there are no libraries to search.
pub fn add_library_dirs(_dirs: Vec<std::path::PathBuf>) {}

/// Fails: shared libraries cannot be loaded on this host.
pub fn load_library(path: &std::path::Path) -> Result<(), String> {
    Err(format!("cannot load {}: no foreign code on this host", path.display()))
}

#[derive(Default)]
pub struct Ffi {}

/// Whether the interpreter can run `prog`'s foreign code: only if it has none.
pub fn check_program(prog: &Program, _cfg: &RunConfig) -> Result<(), String> {
    if prog.fns.iter().any(|f| matches!(f.kind, FnKind::Foreign { .. }))
        || prog.globals.iter().any(|g| g.link_name.is_some())
    {
        return Err("foreign code is not available on this host".into());
    }
    Ok(())
}

impl Machine<'_> {
    pub fn call_foreign(&mut self, span: Span, fid: FnId, _args: Vec<Value>, _extra: &[Ty]) -> R {
        let name = self.prog.fns[fid.index()].name;
        self.trap(span, format!("cannot call the foreign function `{}` on this host", name))
    }

    pub fn foreign_global_addr(&mut self, span: Span, name: &str) -> R<u64> {
        self.trap(span, format!("cannot find the foreign variable `{}`", name))
    }

    pub(super) fn exit_process(&mut self, _code: u8) {}
}
