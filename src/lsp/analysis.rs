//! Running the compiler on a module for the server, and turning its diagnostics into
//! LSP diagnostics.

use super::position::{Encoding, LineIndex};
use crate::diag::{Diagnostic, Severity};
use crate::driver::Session;
use crate::ide::IdeIndex;
use crate::source::{FileId, Span};
use serde_json::{Value, json};
use std::collections::HashMap;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::{Path, PathBuf};

/// The path as the compiler's overlays and file lookups spell it.
pub fn canon(p: &Path) -> PathBuf {
    p.canonicalize().unwrap_or_else(|_| p.to_path_buf())
}

/// A `file://` URI for `path`.
pub fn path_to_uri(path: &Path) -> String {
    let mut out = String::from("file://");
    for &b in path.to_string_lossy().as_bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' | b'/' | b':' => out.push(b as char),
            _ => out.push_str(&format!("%{:02X}", b)),
        }
    }
    out
}

/// The path of a `file://` URI.
pub fn uri_to_path(uri: &str) -> Option<PathBuf> {
    let rest = uri.strip_prefix("file://")?;
    // `file://host/path` and `file:///path`: the path starts at the next slash.
    let rest = &rest[rest.find('/')?..];
    let bytes = rest.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        let hex = bytes.get(i + 1..i + 3).and_then(|h| std::str::from_utf8(h).ok());
        if bytes[i] == b'%'
            && let Some(v) = hex.and_then(|h| u8::from_str_radix(h, 16).ok())
        {
            out.push(v);
            i += 3;
        } else {
            out.push(bytes[i]);
            i += 1;
        }
    }
    String::from_utf8(out).ok().map(PathBuf::from)
}

/// The result of checking one module directory.
pub struct Analysis {
    pub sess: Session,
    pub index: IdeIndex,
    /// The files of the analysis by canonical path.
    pub files: HashMap<PathBuf, FileId>,
    /// Where diagnostics without a location go.
    pub primary: PathBuf,
}

fn panic_message(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(s) = payload.downcast_ref::<&str>() {
        (*s).to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "unknown panic".to_string()
    }
}

impl Analysis {
    /// Checks the module in `dir`, reading the open documents from `overlays`. A panic in
    /// the compiler becomes one diagnostic on `primary`.
    pub fn run(dir: &Path, overlays: &HashMap<PathBuf, String>, primary: &Path) -> Analysis {
        let result = catch_unwind(AssertUnwindSafe(|| {
            let mut sess = Session::new();
            sess.overlays = overlays.clone();
            let index = crate::sema::analyze_for_ide(&mut sess, dir);
            (sess, index)
        }));
        let (sess, index) = match result {
            Ok(r) => r,
            Err(payload) => {
                let mut sess = Session::new();
                if let Some(text) = overlays.get(primary) {
                    sess.sources.add(primary, text.clone());
                }
                sess.diags.error(Span::DUMMY, format!("internal compiler error: {}", panic_message(&*payload)));
                (sess, IdeIndex::default())
            }
        };
        let files = sess.sources.files().map(|(id, f)| (canon(&f.path), id)).collect();
        Analysis { sess, index, files, primary: primary.to_path_buf() }
    }

    pub fn file_id(&self, path: &Path) -> Option<FileId> {
        self.files.get(path).copied()
    }

    pub fn text(&self, file: FileId) -> &str {
        &self.sess.sources.file(file).src
    }

    /// The path of a file of the analysis, unless it is not a real file (the embedded
    /// core library).
    pub fn real_path(&self, file: FileId) -> Option<PathBuf> {
        let p = &self.sess.sources.file(file).path;
        if p.to_string_lossy().starts_with('<') { None } else { Some(canon(p)) }
    }

    /// The LSP range of `span`; an empty span is widened to one character so that
    /// editors can underline it.
    pub fn range(&self, span: Span, enc: Encoding) -> Value {
        let text = self.text(span.file);
        let ix = LineIndex::new(text);
        let (mut lo, mut hi) = (span.lo as usize, span.hi as usize);
        if lo == hi {
            if let Some(c) = text.get(hi..).and_then(|t| t.chars().next()).filter(|c| *c != '\n' && *c != '\r') {
                hi += c.len_utf8();
            } else if let Some(c) = text.get(..lo).and_then(|t| t.chars().next_back()).filter(|c| *c != '\n') {
                lo -= c.len_utf8();
            }
        }
        range_json(&ix, lo, hi, enc)
    }

    /// The diagnostics of the analysis as LSP diagnostics, by canonical file path.
    /// `uri_of` gives the URI to use for a file.
    pub fn diagnostics(&self, enc: Encoding, uri_of: &dyn Fn(&Path) -> String) -> HashMap<PathBuf, Vec<Value>> {
        let mut out: HashMap<PathBuf, Vec<Value>> = HashMap::new();
        for d in self.sess.diags.iter() {
            let (path, span) = if d.span == Span::DUMMY {
                (self.primary.clone(), None)
            } else {
                match self.real_path(d.span.file) {
                    Some(p) => (p, Some(d.span)),
                    None => continue,
                }
            };
            let range = match span {
                Some(s) => self.range(s, enc),
                None => json!({"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}}),
            };
            out.entry(path).or_default().push(self.diagnostic(d, range, enc, uri_of));
        }
        out
    }

    fn diagnostic(&self, d: &Diagnostic, range: Value, enc: Encoding, uri_of: &dyn Fn(&Path) -> String) -> Value {
        let mut message = d.message.clone();
        for n in &d.notes {
            message.push_str("\nnote: ");
            message.push_str(n);
        }
        let related: Vec<Value> = d
            .labels
            .iter()
            .filter(|l| l.span != Span::DUMMY)
            .filter_map(|l| {
                let path = self.real_path(l.span.file)?;
                Some(json!({
                    "location": {"uri": uri_of(&path), "range": self.range(l.span, enc)},
                    "message": l.message,
                }))
            })
            .collect();
        let severity = match d.severity {
            Severity::Error => 1,
            Severity::Warning => 2,
            Severity::Note => 3,
        };
        let mut v = json!({"range": range, "severity": severity, "source": "gamma", "message": message});
        if !related.is_empty() {
            v["relatedInformation"] = Value::Array(related);
        }
        v
    }
}

pub fn range_json(ix: &LineIndex, lo: usize, hi: usize, enc: Encoding) -> Value {
    let (sl, sc) = ix.position(lo, enc);
    let (el, ec) = ix.position(hi, enc);
    json!({"start": {"line": sl, "character": sc}, "end": {"line": el, "character": ec}})
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn uris_round_trip() {
        for p in ["/tmp/a b/ü.gm", "/x/y.gm", "/a%b/c#d"] {
            let uri = path_to_uri(Path::new(p));
            assert!(uri.starts_with("file:///"));
            assert!(!uri.contains(' '));
            assert_eq!(uri_to_path(&uri), Some(PathBuf::from(p)));
        }
        assert_eq!(uri_to_path("file:///a/b%20c.gm"), Some(PathBuf::from("/a/b c.gm")));
        assert_eq!(uri_to_path("untitled:Untitled-1"), None);
    }

    #[test]
    fn an_internal_error_becomes_a_diagnostic() {
        let primary = PathBuf::from("/nonexistent/dir/main.gm");
        let a = Analysis::run(Path::new("/nonexistent/dir"), &HashMap::new(), &primary);
        // The module does not exist: an ordinary error, reported on the primary file.
        let d = a.diagnostics(Encoding::Utf16, &|p| path_to_uri(p));
        assert_eq!(d[&primary].len(), 1);
        assert!(d[&primary][0]["message"].as_str().unwrap().contains("cannot read"));
    }
}
