//! Compiler diagnostics: construction, collection and rendering.

use crate::source::{SourceMap, Span};
use std::fmt::Write as _;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
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
    /// While positive, new diagnostics are discarded (used for speculative parsing).
    suppressed: u32,
}

impl Diagnostics {
    pub fn new() -> Diagnostics {
        Diagnostics::default()
    }

    pub fn push(&mut self, d: Diagnostic) {
        if self.suppressed == 0 {
            self.list.push(d);
        }
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
                let text = f.line_text(line);
                let _ = writeln!(out, "{} |", pad);
                let _ = writeln!(out, "{:>w$} | {}", line, text.replace('\t', " "), w = gutter);
                let (end_line, end_col) = f.line_col(d.span.hi);
                let width = if end_line == line {
                    (end_col - col).max(1)
                } else {
                    (text.chars().count() as u32 + 1 - col).max(1)
                };
                let _ = writeln!(out, "{} | {}{}", pad, " ".repeat(col as usize - 1), "^".repeat(width as usize));
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
