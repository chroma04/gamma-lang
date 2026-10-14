//! Output of the REPL: diagnostics relative to the input, trap reports, and the
//! filter that hides the output of replayed statements.

use crate::diag::{self, DiagFormat, Diagnostic, Severity};
use crate::driver::Session;
use crate::interp::Trap;
use crate::source::{FileId, SourceMap, Span};
use std::io::Write;

/// Renders the diagnostics of `sess`. Those inside the input (the part of file
/// `file` that starts at byte `lo` and is `input` long) are shown with positions
/// relative to the input; errors elsewhere in the file are shown against the
/// whole session text; other warnings are left out.
pub fn render_region(sess: &Session, file: FileId, lo: u32, input: &str, format: DiagFormat) -> String {
    let hi = lo + input.len() as u32;
    let mut view = SourceMap::new();
    view.add("<repl>", input.to_string());
    view.add("<session>", sess.sources.file(file).src.clone());
    let mut diags: Vec<&Diagnostic> = sess.diags.iter().collect();
    diags.sort_by_key(|d| (d.span.file, d.span.lo));
    let mut out = String::new();
    for d in diags {
        if d.span == Span::DUMMY || d.span.file != file {
            if d.severity == Severity::Error {
                out.push_str(&diag::render(&sess.sources, d, format));
            }
            continue;
        }
        let inside = (d.span.lo >= lo || d.span.hi > lo) && d.span.lo <= hi;
        if !inside && d.severity != Severity::Error {
            continue;
        }
        let mut nd = d.clone();
        let map = |s: Span| -> Option<Span> {
            if s == Span::DUMMY || s.file != file {
                None
            } else if inside {
                let (a, b) = (s.lo.max(lo), s.hi.min(hi + 1).max(s.lo.max(lo)));
                Some(Span::new(FileId(0), a - lo, (b - lo).min(input.len() as u32)))
            } else {
                Some(Span::new(FileId(1), s.lo, s.hi))
            }
        };
        nd.span = map(d.span).unwrap_or(Span::DUMMY);
        nd.labels = d
            .labels
            .iter()
            .filter_map(|l| map(l.span).map(|s| diag::Label { span: s, message: l.message.clone() }))
            .collect();
        out.push_str(&diag::render(&view, &nd, format));
    }
    out
}

/// The report for a trap that happened at `span` while running the session text
/// in `file`, with the input starting at byte `lo`.
pub fn trap_report(trap: &Trap, sess: &Session, file: FileId, lo: u32, input: &str) -> String {
    let mut s = format!("trap: {}\n", trap.message);
    let span = trap.span;
    if span != Span::DUMMY {
        let f = sess.sources.file(span.file);
        let (line, col) = f.line_col(span.lo);
        if span.file == file && span.lo >= lo && span.lo <= lo + input.len() as u32 {
            let rel = f.line_col(lo).0;
            let start_col = f.line_col(lo).1;
            let col = if line == rel { col + 1 - start_col } else { col };
            s.push_str(&format!("  at <repl>:{}:{}\n", line - rel + 1, col));
        } else if span.file != file {
            s.push_str(&format!("  at {}:{}:{}\n", f.path.display(), line, col));
        }
    }
    s
}

/// Printed by the synthesized program after it has replayed earlier statements;
/// everything before it is hidden.
pub const SENTINEL: &str = "@@gamma-repl-replay-done@@";

/// A writer that drops everything up to the replay marker.
pub struct Filter<'a> {
    out: &'a mut dyn Write,
    hiding: bool,
    pending: Vec<u8>,
}

impl<'a> Filter<'a> {
    pub fn new(out: &'a mut dyn Write, hiding: bool) -> Filter<'a> {
        Filter { out, hiding, pending: vec![] }
    }
}

impl Write for Filter<'_> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        if !self.hiding {
            return self.out.write(buf);
        }
        self.pending.extend_from_slice(buf);
        let m = SENTINEL.as_bytes();
        if let Some(pos) = self.pending.windows(m.len()).position(|w| w == m) {
            let rest = self.pending.split_off(pos + m.len());
            self.pending.clear();
            self.hiding = false;
            self.out.write_all(&rest)?;
        } else if self.pending.len() > m.len() {
            let keep = self.pending.len() - m.len();
            self.pending.drain(..keep);
        }
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        if !self.hiding {
            self.out.flush()?;
        }
        Ok(())
    }
}
