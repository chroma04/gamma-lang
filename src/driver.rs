//! Orchestrates the compilation stages for the command-line tool and tests.

use crate::diag::{self, DiagFormat, Diagnostics};
use crate::source::{FileId, SourceMap};
use crate::syntax::ast::{File, NodeIdGen};
use crate::syntax::token::{LexOutput, Token, TokenKind};
use crate::syntax::{ast_dump, lexer, parser};
use std::collections::HashMap;
use std::fmt::Write as _;
use std::path::{Path, PathBuf};

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
        }
    }

    /// Loads a file, reporting a location-less error on failure.
    pub fn load(&mut self, path: &Path) -> Option<FileId> {
        if !self.overlays.is_empty() {
            let key = crate::source::canonical(path);
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
        lexer::lex(file, &src, &mut self.diags)
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
            TokenKind::Float(l) => match l.suffix {
                Some(s) => writeln!(out, "float {} {:?}", l.value, s),
                None => writeln!(out, "float {}", l.value),
            },
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
