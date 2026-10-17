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

/// A replacement of the source text at `span` (empty for an insertion) by `text`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Edit {
    pub span: Span,
    pub text: String,
}

/// A change to the source that resolves a diagnostic, for tools to apply: `gamma fix`
/// applies the fix of a diagnostic that has exactly one, and the language server offers
/// each as a quick fix.
#[derive(Clone, Debug)]
pub struct Fix {
    /// What the fix does, such as "replace `&&` with `and`".
    pub message: String,
    /// Edits of non-overlapping spans.
    pub edits: Vec<Edit>,
}

#[derive(Clone, Debug)]
pub struct Diagnostic {
    pub severity: Severity,
    pub message: String,
    /// The primary location. `Span::DUMMY` for diagnostics without a location.
    pub span: Span,
    pub labels: Vec<Label>,
    pub notes: Vec<String>,
    /// Alternative ways to resolve the diagnostic mechanically. The message says what to
    /// write in words; human-oriented output does not show fixes.
    pub fixes: Vec<Fix>,
}

impl Diagnostic {
    pub fn error(span: Span, message: impl Into<String>) -> Diagnostic {
        Diagnostic::new(Severity::Error, span, message.into())
    }

    pub fn warning(span: Span, message: impl Into<String>) -> Diagnostic {
        Diagnostic::new(Severity::Warning, span, message.into())
    }

    fn new(severity: Severity, span: Span, message: String) -> Diagnostic {
        Diagnostic { severity, message, span, labels: vec![], notes: vec![], fixes: vec![] }
    }

    /// Adds a fix that replaces the text at `span` with `text`.
    pub fn with_fix(self, message: impl Into<String>, span: Span, text: impl Into<String>) -> Diagnostic {
        self.with_fix_edits(message, vec![Edit { span, text: text.into() }])
    }

    /// Adds a fix made of several edits.
    pub fn with_fix_edits(mut self, message: impl Into<String>, edits: Vec<Edit>) -> Diagnostic {
        self.fixes.push(Fix { message: message.into(), edits });
        self
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
    /// How many errors were ever passed to [`Diagnostics::push`], including those that were
    /// discarded or repeat an earlier diagnostic.
    raised: usize,
}

impl Diagnostics {
    pub fn new() -> Diagnostics {
        Diagnostics::default()
    }

    /// Records `d`, unless it repeats a diagnostic with the same severity, location and
    /// message (generic code is checked once per instantiation, for example), or is an
    /// error about the error type `{error}`, which only restates an earlier error.
    pub fn push(&mut self, d: Diagnostic) {
        if d.severity == Severity::Error {
            self.raised += 1;
        }
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

    /// The number of errors raised so far, counting those that were discarded or repeat
    /// an earlier diagnostic. A stage that sees it change knows that what it just checked
    /// contains an error, whether or not that error was recorded again.
    pub fn raised_errors(&self) -> usize {
        self.raised
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
    /// One JSON object per line, with locations, notes and fixes (see [`to_json`]).
    Json,
}

/// The location of `span` as JSON fields: the file, byte offsets and 1-based lines and
/// columns (columns count characters). Nothing for a span without a location.
fn json_location(sm: &SourceMap, span: Span, obj: &mut serde_json::Map<String, serde_json::Value>) {
    if span == Span::DUMMY {
        return;
    }
    let f = sm.file(span.file);
    let (line, col) = f.line_col(span.lo);
    let (end_line, end_col) = f.line_col(span.hi);
    obj.insert("file".into(), f.path.display().to_string().into());
    obj.insert("offset".into(), span.lo.into());
    obj.insert("end_offset".into(), span.hi.into());
    obj.insert("line".into(), line.into());
    obj.insert("column".into(), col.into());
    obj.insert("end_line".into(), end_line.into());
    obj.insert("end_column".into(), end_col.into());
}

/// `d` as a JSON object:
/// `{"severity", "message", <location>, "labels": [{"message", <location>}], "notes": [..],
/// "fixes": [{"message", "edits": [{<location>, "text"}]}]}`, where a location is the
/// fields `file`, `offset`, `end_offset` (bytes), `line`, `column`, `end_line` and
/// `end_column` (1-based; columns count characters), absent for a diagnostic without one.
pub fn to_json(sm: &SourceMap, d: &Diagnostic) -> serde_json::Value {
    use serde_json::{Map, Value};
    let mut obj = Map::new();
    obj.insert("severity".into(), d.severity.as_str().into());
    obj.insert("message".into(), d.message.clone().into());
    json_location(sm, d.span, &mut obj);
    let labels = d
        .labels
        .iter()
        .map(|l| {
            let mut o = Map::new();
            o.insert("message".into(), l.message.clone().into());
            json_location(sm, l.span, &mut o);
            Value::Object(o)
        })
        .collect();
    obj.insert("labels".into(), Value::Array(labels));
    obj.insert("notes".into(), d.notes.iter().map(|n| Value::from(n.clone())).collect());
    let fixes = d
        .fixes
        .iter()
        .map(|fx| {
            let edits = fx
                .edits
                .iter()
                .map(|e| {
                    let mut o = Map::new();
                    json_location(sm, e.span, &mut o);
                    o.insert("text".into(), e.text.clone().into());
                    Value::Object(o)
                })
                .collect();
            let mut o = Map::new();
            o.insert("message".into(), fx.message.clone().into());
            o.insert("edits".into(), Value::Array(edits));
            Value::Object(o)
        })
        .collect();
    obj.insert("fixes".into(), Value::Array(fixes));
    Value::Object(obj)
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
        DiagFormat::Json => {
            let _ = writeln!(out, "{}", to_json(sm, d));
        }
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
