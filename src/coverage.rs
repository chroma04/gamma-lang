//! Statement coverage for `gamma test --coverage`.
//!
//! [`instrument`] rewrites a checked program: in front of every statement of the functions
//! of the package under test (and in front of the value of each block) it inserts a
//! [`Builtin::Cover`] that counts one execution of that source line. The interpreter keeps
//! the counters ([`crate::interp::coverage_counts`]); the report is the lines whose counter
//! is not zero among the lines that have one. Only the interpreter runs instrumented code.
//!
//! The functions counted are those the program contains: a generic function is counted
//! through its instances, and one that nothing instantiates is not counted at all. The
//! bodies of tests and benchmarks are left out.

use crate::source::{FileId, SourceMap, Span};
use crate::tir::{Block, Builtin, Expr, ExprKind, FnId, Program, Stmt};
use crate::types::Ty;
use std::collections::HashMap;
use std::path::Path;

/// The source lines that carry a counter: the line of counter `i` is `points[i]`.
#[derive(Debug, Default)]
pub struct CoverMap {
    pub points: Vec<(FileId, u32)>,
}

/// Inserts counters into the functions of `prog` whose source is a file for which `in_scope`
/// holds.
pub fn instrument(prog: &mut Program, sources: &SourceMap, in_scope: &dyn Fn(&Path) -> bool) -> CoverMap {
    let counted: Vec<bool> = (0..prog.fns.len()).map(|i| counts(prog, FnId(i as u32))).collect();
    let mut cx = Cx { ids: HashMap::new(), map: CoverMap::default(), sources, in_scope };
    for (i, f) in prog.fns.iter_mut().enumerate() {
        if !counted[i] {
            continue;
        }
        let Some(body) = f.body.as_mut() else { continue };
        if !matches!(body.kind, ExprKind::Block(_)) {
            // An expression body: `fn(x) => x + 1`.
            let span = body.span;
            let ty = body.ty;
            let cover = cx.point(span).map(|id| cover_stmt(id, span));
            if let Some(cover) = cover {
                let old = std::mem::replace(body, cover_expr(0, span));
                let block = Block { stmts: vec![cover], tail: Some(old), span };
                *body = Expr { ty, kind: ExprKind::Block(Box::new(block)), span };
            }
        }
        cx.visit(body);
    }
    cx.map
}

/// Whether the function `f` is part of the code under test: not generated, not a test.
fn counts(prog: &Program, f: FnId) -> bool {
    let mut cur = Some(f);
    while let Some(id) = cur {
        let def = &prog.fns[id.index()];
        let name = def.name.as_str();
        if name.starts_with('<') || name.starts_with("test \"") || name.starts_with("bench \"") {
            return false;
        }
        cur = def.parent;
    }
    prog.fns[f.index()].body.is_some()
}

struct Cx<'a> {
    ids: HashMap<(FileId, u32), u32>,
    map: CoverMap,
    sources: &'a SourceMap,
    in_scope: &'a dyn Fn(&Path) -> bool,
}

impl Cx<'_> {
    /// The counter for the line `span` starts on, if that line is in a file to be covered.
    fn point(&mut self, span: Span) -> Option<u32> {
        if span == Span::DUMMY {
            return None;
        }
        let file = self.sources.file(span.file);
        if !(self.in_scope)(&file.path) {
            return None;
        }
        let line = file.line_col(span.lo).0;
        let key = (span.file, line);
        if let Some(&id) = self.ids.get(&key) {
            return Some(id);
        }
        let id = self.map.points.len() as u32;
        self.map.points.push(key);
        self.ids.insert(key, id);
        Some(id)
    }

    fn visit(&mut self, e: &mut Expr) {
        match &mut e.kind {
            ExprKind::Block(b) => self.block(b),
            ExprKind::Loop { body, .. } => self.block(body),
            _ => {}
        }
        e.for_each_child_mut(&mut |c| self.visit(c));
    }

    /// Puts a counter before each statement of `b`, and before its value.
    fn block(&mut self, b: &mut Block) {
        let old = std::mem::take(&mut b.stmts);
        let mut stmts = Vec::with_capacity(old.len() * 2);
        for s in old {
            if let Some(span) = stmt_span(&s)
                && let Some(id) = self.point(span)
            {
                stmts.push(cover_stmt(id, span));
            }
            stmts.push(s);
        }
        if let Some(t) = &b.tail
            && let Some(id) = self.point(t.span)
        {
            stmts.push(cover_stmt(id, t.span));
        }
        b.stmts = stmts;
    }
}

/// Where a statement starts, if it has source text of its own.
fn stmt_span(s: &Stmt) -> Option<Span> {
    match s {
        Stmt::Let { init, .. } => init.as_ref().map(|e| e.span),
        Stmt::LetTuple { init, .. } => Some(init.span),
        Stmt::Assign { place, .. } => Some(place.span),
        Stmt::AssignTuple { value, .. } => Some(value.span),
        Stmt::Expr(e) => Some(e.span),
        Stmt::Defer { .. } => None,
    }
}

fn cover_expr(id: u32, span: Span) -> Expr {
    Expr { ty: Ty::unit(), kind: ExprKind::Builtin { op: Builtin::Cover(id), args: vec![] }, span }
}

fn cover_stmt(id: u32, span: Span) -> Stmt {
    Stmt::Expr(cover_expr(id, span))
}
