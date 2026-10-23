//! Orchestrates the compilation stages for the command-line tool and tests.

use crate::diag::{self, DiagFormat, Diagnostics};
use crate::source::{FileId, SourceMap};
use crate::syntax::ast::{File, NodeIdGen};
use crate::syntax::token::{LexOutput, Token, TokenKind};
use crate::syntax::{ast_dump, lexer, parser};
use std::collections::HashMap;
use std::fmt::Write as _;
use std::path::{Path, PathBuf};

/// The canonical form of `path`; for a file that does not exist (yet), its directory's
/// canonical form joined with its name.
pub fn canonical_path(path: &Path) -> PathBuf {
    if path.exists() {
        return crate::source::canonical(path);
    }
    match (path.parent().filter(|d| !d.as_os_str().is_empty()), path.file_name()) {
        (Some(dir), Some(name)) if dir.exists() => crate::source::canonical(dir).join(name),
        _ => path.to_path_buf(),
    }
}

/// The `.gm` files of module directory `dir`: those on disk plus the unsaved buffers
/// (`overlays`, by canonical path) that stand in a file of that directory. `None` if the
/// directory cannot be read and holds no buffer.
pub fn module_files(dir: &Path, overlays: &HashMap<PathBuf, String>) -> Option<Vec<PathBuf>> {
    let is_gm = |p: &Path| p.extension().is_some_and(|e| e == "gm");
    let (mut paths, readable) = match std::fs::read_dir(dir) {
        Ok(rd) => (rd.flatten().map(|e| e.path()).filter(|p| p.is_file() && is_gm(p)).collect::<Vec<_>>(), true),
        Err(_) => (vec![], false),
    };
    if !overlays.is_empty() {
        let key = canonical_path(dir);
        for buffer in overlays.keys() {
            if buffer.parent() == Some(key.as_path())
                && is_gm(buffer)
                && !paths.iter().any(|p| canonical_path(p) == *buffer)
            {
                paths.push(dir.join(buffer.file_name().unwrap_or_default()));
            }
        }
    }
    (readable || !paths.is_empty()).then_some(paths)
}

/// State shared by all stages of one compilation.
pub struct Session {
    pub sources: SourceMap,
    pub diags: Diagnostics,
    pub ids: NodeIdGen,
    /// The build mode (§1.4): `target.mode`, and how checked operations behave.
    pub mode: crate::interp::Mode,
    /// `-g`: native builds carry debug information mapping them to the source.
    pub debug_info: bool,
    /// The compilation target (§3.6): layouts and the `target` module follow it.
    pub target: crate::layout::Target,
    /// Where the git dependencies of the program's packages were fetched to, if the
    /// package manager resolved any.
    pub packages: Option<std::sync::Arc<crate::pkg::Resolution>>,
    /// Unsaved editor buffers by canonical path: `load` reads these instead of the disk.
    pub overlays: HashMap<PathBuf, String>,
    /// Recover from unclosed brackets by layout when lexing (see
    /// [`lexer::lex_recovering`]); for editor analysis.
    pub recover_layout: bool,
}

impl Default for Session {
    fn default() -> Self {
        Self::new()
    }
}

impl Session {
    pub fn new() -> Session {
        Session {
            sources: SourceMap::new(),
            diags: Diagnostics::new(),
            ids: NodeIdGen::new(),
            mode: crate::interp::Mode::Debug,
            debug_info: false,
            target: crate::layout::Target::host(),
            packages: None,
            overlays: HashMap::new(),
            recover_layout: false,
        }
    }

    /// The `.gm` files of module directory `dir`, counting the unsaved buffers (see
    /// [`module_files`]).
    pub fn module_files(&self, dir: &Path) -> Option<Vec<PathBuf>> {
        module_files(dir, &self.overlays)
    }

    /// Loads a file, reporting a location-less error on failure.
    pub fn load(&mut self, path: &Path) -> Option<FileId> {
        if !self.overlays.is_empty() {
            let key = canonical_path(path);
            if let Some(text) = self.overlays.get(&key) {
                return Some(self.sources.add(path, text.clone()));
            }
        }
        match self.sources.load(path) {
            Ok(id) => Some(id),
            Err(msg) => {
                self.diags.error(crate::source::Span::DUMMY, msg);
                None
            }
        }
    }

    pub fn lex(&mut self, file: FileId) -> LexOutput {
        let src = self.sources.file(file).src.clone();
        if self.recover_layout {
            lexer::lex_recovering(file, &src, &mut self.diags)
        } else {
            lexer::lex(file, &src, &mut self.diags)
        }
    }

    pub fn parse(&mut self, file: FileId) -> File {
        let lexed = self.lex(file);
        parser::parse_file(file, &lexed, &mut self.ids, &mut self.diags)
    }

    /// Renders all diagnostics, sorted by position.
    pub fn render_diagnostics(&mut self, format: DiagFormat) -> String {
        self.diags.sort();
        let mut out = String::new();
        for d in self.diags.iter() {
            out.push_str(&diag::render(&self.sources, d, format));
        }
        out
    }
}

/// One line per token: `line:col kind [payload]`, used by `gamma lex`.
pub fn dump_tokens(sources: &SourceMap, tokens: &[Token]) -> String {
    let mut out = String::new();
    for t in tokens {
        let (line, col) = sources.file(t.span.file).line_col(t.span.lo);
        let _ = write!(out, "{}:{} ", line, col);
        let _ = match &t.kind {
            TokenKind::Ident(s) => writeln!(out, "ident {}", s),
            TokenKind::Underscore => writeln!(out, "underscore"),
            TokenKind::Keyword(k) => writeln!(out, "keyword {}", k.as_str()),
            TokenKind::Int(l) => {
                let kind = if l.is_char { "char" } else { "int" };
                match l.suffix {
                    Some(s) => writeln!(out, "{} {} {}", kind, l.value, s),
                    None => writeln!(out, "{} {}", kind, l.value),
                }
            }
            TokenKind::Float(l) => {
                let kind = if l.imag { "imag" } else { "float" };
                match l.suffix {
                    Some(s) => writeln!(out, "{} {} {:?}", kind, l.value, s),
                    None => writeln!(out, "{} {}", kind, l.value),
                }
            }
            TokenKind::Str(s) => writeln!(out, "str {:?} {}", s.kind, escape_bytes(&s.bytes)),
            TokenKind::Intrinsic(s) => writeln!(out, "intrinsic @{}", s),
            TokenKind::Attribute(s) => writeln!(out, "attribute #{}", s),
            TokenKind::Punct(p) => writeln!(out, "punct {}", p.as_str()),
            TokenKind::Newline => writeln!(out, "newline"),
            TokenKind::Eof => writeln!(out, "eof"),
        };
    }
    out
}

/// Renders bytes as a double-quoted string with escapes for non-printable bytes.
pub fn escape_bytes(bytes: &[u8]) -> String {
    let mut s = String::from("\"");
    for &b in bytes {
        match b {
            b'"' => s.push_str("\\\""),
            b'\\' => s.push_str("\\\\"),
            b'\n' => s.push_str("\\n"),
            b'\r' => s.push_str("\\r"),
            b'\t' => s.push_str("\\t"),
            0x20..=0x7e => s.push(b as char),
            _ => {
                let _ = write!(s, "\\x{:02x}", b);
            }
        }
    }
    s.push('"');
    s
}

/// Renders an AST in the stable dump format.
pub fn dump_ast(file: &File) -> String {
    ast_dump::dump_file(file)
}
