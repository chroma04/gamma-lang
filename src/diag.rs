//! Compiler diagnostics: construction, collection and rendering.

use crate::source::{SourceMap, Span};
use std::collections::HashSet;
use std::fmt::Write as _;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Severity {
    Note,
    Warning,
    Error,
}

impl Severity {
    pub fn as_str(self) -> &'static str {
        match self {
            Severity::Note => "note",
            Severity::Warning => "warning",
            Severity::Error => "error",
        }
    }
}

/// A secondary location attached to a diagnostic.
#[derive(Clone, Debug)]
pub struct Label {
    pub span: Span,
    pub message: String,
}

#[derive(Clone, Debug)]
pub struct Diagnostic {
    pub severity: Severity,
    pub message: String,
    /// The primary location. `Span::DUMMY` for diagnostics without a location.
    pub span: Span,
    pub labels: Vec<Label>,
    pub notes: Vec<String>,
}

impl Diagnostic {
    pub fn error(span: Span, message: impl Into<String>) -> Diagnostic {
        Diagnostic { severity: Severity::Error, message: message.into(), span, labels: vec![], notes: vec![] }
    }

    pub fn warning(span: Span, message: impl Into<String>) -> Diagnostic {
        Diagnostic { severity: Severity::Warning, message: message.into(), span, labels: vec![], notes: vec![] }
    }

    pub fn with_label(mut self, span: Span, message: impl Into<String>) -> Diagnostic {
        self.labels.push(Label { span, message: message.into() });
        self
    }

    pub fn with_note(mut self, note: impl Into<String>) -> Diagnostic {
        self.notes.push(note.into());
        self
    }
}

/// Collects diagnostics during compilation.
#[derive(Default)]
pub struct Diagnostics {
    list: Vec<Diagnostic>,
    /// Severity, location and message of every diagnostic in `list` that has a location.
    seen: HashSet<(Severity, Span, String)>,
    /// While positive, new diagnostics are discarded (used for speculative parsing).
    suppressed: u32,
}

impl Diagnostics {
    pub fn new() -> Diagnostics {
        Diagnostics::default()
    }

    /// Records `d`, unless it repeats a diagnostic with the same severity, location and
    /// message (generic code is checked once per instantiation, for example), or is an
    /// error about the error type `{error}`, which only restates an earlier error.
    pub fn push(&mut self, d: Diagnostic) {
        if self.suppressed != 0 {
            return;
        }
        if d.severity == Severity::Error && d.message.contains("{error}") {
            return;
        }
        if d.span != Span::DUMMY && !self.seen.insert((d.severity, d.span, d.message.clone())) {
            return;
        }
        self.list.push(d);
    }

    pub fn error(&mut self, span: Span, message: impl Into<String>) {
        self.push(Diagnostic::error(span, message));
    }

    pub fn warning(&mut self, span: Span, message: impl Into<String>) {
        self.push(Diagnostic::warning(span, message));
    }

    pub fn has_errors(&self) -> bool {
        self.list.iter().any(|d| d.severity == Severity::Error)
    }

    pub fn error_count(&self) -> usize {
        self.list.iter().filter(|d| d.severity == Severity::Error).count()
    }

    pub fn len(&self) -> usize {
        self.list.len()
    }

    pub fn is_empty(&self) -> bool {
        self.list.is_empty()
    }

    /// Adds `note` to the diagnostics recorded after the first `from` whose primary
    /// location lies within `span`.
    pub fn note_since(&mut self, from: usize, span: Span, note: &str) {
        for d in self.list.iter_mut().skip(from) {
            if d.span.file == span.file && d.span.lo >= span.lo && d.span.hi <= span.hi {
                d.notes.push(note.to_string());
            }
        }
    }

    /// Drops diagnostics recorded after the first `len` (used to roll back speculation).
    pub fn truncate(&mut self, len: usize) {
        for d in self.list.iter().skip(len) {
            self.seen.remove(&(d.severity, d.span, d.message.clone()));
        }
        self.list.truncate(len);
    }

    pub fn suppress(&mut self) {
        self.suppressed += 1;
    }

    pub fn unsuppress(&mut self) {
        self.suppressed -= 1;
    }

    pub fn iter(&self) -> impl Iterator<Item = &Diagnostic> {
        self.list.iter()
    }

    pub fn into_vec(self) -> Vec<Diagnostic> {
        self.list
    }

    /// Sorts diagnostics by file and position, keeping the order of equal positions.
    pub fn sort(&mut self) {
        self.list.sort_by_key(|d| (d.span.file, d.span.lo));
    }
}

/// How diagnostics are printed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DiagFormat {
    /// Human-oriented output with source excerpts.
    Human,
    /// One line per diagnostic: `path:line:col: severity: message`.
    Short,
}

/// Renders one diagnostic in the given format.
/// Source lines longer than this (in characters) are shown as a window of `WINDOW`
/// characters around the location of a diagnostic.
const LONG_LINE: u32 = 200;
const WINDOW: u32 = 120;

pub fn render(sm: &SourceMap, d: &Diagnostic, format: DiagFormat) -> String {
    let mut out = String::new();
    let loc = if d.span == Span::DUMMY {
        None
    } else {
        let f = sm.file(d.span.file);
        let (line, col) = f.line_col(d.span.lo);
        Some((f, line, col))
    };
    match format {
        DiagFormat::Short => {
            if let Some((f, line, col)) = loc {
                let _ = write!(out, "{}:{}:{}: ", f.path.display(), line, col);
            }
            let _ = writeln!(out, "{}: {}", d.severity.as_str(), d.message);
            // Secondary locations follow as notes, one per line.
            for l in d.labels.iter().filter(|l| l.span != Span::DUMMY) {
                let f = sm.file(l.span.file);
                let (line, col) = f.line_col(l.span.lo);
                let _ = writeln!(out, "{}:{}:{}: note: {}", f.path.display(), line, col, l.message);
            }
            // Plain notes are reported at the primary location.
            for n in &d.notes {
                if let Some((f, line, col)) = loc {
                    let _ = write!(out, "{}:{}:{}: ", f.path.display(), line, col);
                }
                let _ = writeln!(out, "note: {}", n);
            }
        }
        DiagFormat::Human => {
            let _ = writeln!(out, "{}: {}", d.severity.as_str(), d.message);
            if let Some((f, line, col)) = loc {
                let gutter = line.to_string().len().max(2);
                let pad = " ".repeat(gutter);
                let _ = writeln!(out, "{}--> {}:{}:{}", pad, f.path.display(), line, col);
                let text = f.line_text(line).replace('\t', " ");
                let len = text.chars().count() as u32;
                let (end_line, end_col) = f.line_col(d.span.hi);
                let mut width = if end_line == line { (end_col - col).max(1) } else { (len + 1 - col).max(1) };
                // A very long line is shown as a window around the location, so that many
                // errors on one line do not repeat all of it.
                let (shown, caret) = if len > LONG_LINE {
                    let start = (col - 1).saturating_sub(WINDOW / 3);
                    let end = (start + WINDOW).min(len);
                    let body: String = text.chars().skip(start as usize).take((end - start) as usize).collect();
                    let pre = if start > 0 { "…" } else { "" };
                    let post = if end < len { "…" } else { "" };
                    width = width.min(end + 1 - col);
                    (format!("{pre}{body}{post}"), col - 1 - start + pre.chars().count() as u32)
                } else {
                    (text, col - 1)
                };
                let _ = writeln!(out, "{} |", pad);
                let _ = writeln!(out, "{:>w$} | {}", line, shown, w = gutter);
                let _ = writeln!(out, "{} | {}{}", pad, " ".repeat(caret as usize), "^".repeat(width.max(1) as usize));
            }
            for l in &d.labels {
                if l.span == Span::DUMMY {
                    continue;
                }
                let f = sm.file(l.span.file);
                let (line, col) = f.line_col(l.span.lo);
                let _ = writeln!(out, "  note: {} ({}:{}:{})", l.message, f.path.display(), line, col);
            }
            for n in &d.notes {
                let _ = writeln!(out, "  = note: {}", n);
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::source::SourceMap;

    #[test]
    fn long_lines_are_shown_around_the_location() {
        let mut sm = SourceMap::new();
        let line = format!("x := {}y{}", "a".repeat(1000), "b".repeat(1000));
        let f = sm.add("t.gm", line.clone());
        let at = line.find('y').unwrap() as u32;
        let out = render(&sm, &Diagnostic::error(Span { file: f, lo: at, hi: at + 1 }, "here"), DiagFormat::Human);
        let lines: Vec<&str> = out.lines().collect();
        assert!(lines[3].chars().count() < 140, "{}", lines[3]);
        assert!(lines[3].contains("…a") && lines[3].contains("b…"), "{}", lines[3]);
        // The caret is under the `y`.
        let caret = lines[4].find('^').unwrap();
        assert_eq!(lines[3].chars().nth(lines[4][..caret].chars().count()), Some('y'), "{}", out);
    }
}
