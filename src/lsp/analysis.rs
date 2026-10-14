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
    crate::driver::canonical_path(p)
}

/// A `file://` URI for `path`.
pub fn path_to_uri(path: &Path) -> String {
    let mut out = String::from("file://");
    // A Windows path `C:\a\b` is the URI path `/C:/a/b`.
    let text = path.to_string_lossy();
    let text = if cfg!(windows) {
        let t = text.replace('\\', "/");
        if t.as_bytes().get(1) == Some(&b':') { format!("/{t}") } else { t }
    } else {
        text.into_owned()
    };
    for &b in text.as_bytes() {
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
    let mut path = String::from_utf8(out).ok()?;
    // The URI path `/C:/a/b` is the Windows path `C:/a/b`.
    if cfg!(windows) && path.as_bytes().get(2) == Some(&b':') && path.starts_with('/') {
        path.remove(0);
    }
    Some(PathBuf::from(path))
}

/// The result of checking one module directory.
pub struct Analysis {
    pub sess: Session,
    pub index: IdeIndex,
    /// The files of the analysis by canonical path.
    pub files: HashMap<PathBuf, FileId>,
    /// Where diagnostics without a location go.
    pub primary: PathBuf,
    /// Files made for the embedded core library, by source file.
    core_files: std::cell::RefCell<HashMap<FileId, Option<PathBuf>>>,
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

/// A session for checking `dir` with the open documents. The git dependencies of its
/// package are used as locked and fetched: the server never touches the network or
/// rewrites gamma.lock (`gamma fetch` does). If they are not available, checking reports
/// the dependency as needing `gamma fetch`.
fn new_session(dir: &Path, overlays: &HashMap<PathBuf, String>) -> Session {
    let mut sess = Session::new();
    sess.overlays = overlays.clone();
    let opts = crate::pkg::Options { offline: true, locked: true };
    sess.packages = crate::pkg::prepare(dir, &opts).ok().flatten().map(std::sync::Arc::new);
    sess
}

impl Analysis {
    /// Checks the module in `dir`, reading the open documents from `overlays`. A panic in
    /// the compiler becomes one diagnostic on `primary`.
    ///
    /// With `recycle`, the types and ids that the run allocates are freed when it ends
    /// (an analysis holds none), so that a server that runs for days does not grow.
    pub fn run(dir: &Path, overlays: &HashMap<PathBuf, String>, primary: &Path, recycle: bool) -> Analysis {
        let mark = recycle.then(crate::types::mark);
        let a = Analysis::run_with(dir, overlays, primary, &crate::sema::analyze_for_ide);
        if let Some(m) = mark {
            // SAFETY: the analysis holds no types or ids; nothing else allocates types.
            unsafe { crate::types::release_since(m) };
        }
        a
    }

    /// [`Analysis::run`] with the checker's entry point given, so that tests can make it fail.
    fn run_with(
        dir: &Path,
        overlays: &HashMap<PathBuf, String>,
        primary: &Path,
        analyze: &dyn Fn(&mut Session, &Path) -> IdeIndex,
    ) -> Analysis {
        let result = catch_unwind(AssertUnwindSafe(|| {
            let mut sess = new_session(dir, overlays);
            let index = analyze(&mut sess, dir);
            (sess, index)
        }));
        let (sess, index) = match result {
            Ok(r) => r,
            Err(payload) => {
                // Checking a module with syntax errors can fail where the compiler would
                // have stopped at the first error: report what the compiler itself reports.
                let strict = catch_unwind(AssertUnwindSafe(|| {
                    let mut sess = new_session(dir, overlays);
                    crate::sema::analyze_tests(&mut sess, dir);
                    sess
                }));
                match strict {
                    Ok(sess) if sess.diags.has_errors() => (sess, IdeIndex::default()),
                    _ => {
                        let mut sess = Session::new();
                        if let Some(text) = overlays.get(primary) {
                            sess.sources.add(primary, text.clone());
                        }
                        sess.diags.error(Span::DUMMY, format!("internal compiler error: {}", panic_message(&*payload)));
                        (sess, IdeIndex::default())
                    }
                }
            }
        };
        let files = sess.sources.files().map(|(id, f)| (canon(&f.path), id)).collect();
        Analysis { sess, index, files, primary: primary.to_path_buf(), core_files: Default::default() }
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

    /// Where an editor can open the file: its path, or for the embedded core library a
    /// file with its text (see [`super::corelib`]).
    pub fn open_path(&self, file: FileId) -> Option<PathBuf> {
        if let Some(p) = self.real_path(file) {
            return Some(p);
        }
        let f = self.sess.sources.file(file);
        let name = f.path.to_string_lossy();
        if !super::corelib::is_core(&name) {
            return None;
        }
        self.core_files.borrow_mut().entry(file).or_insert_with(|| super::corelib::path_for(&name, &f.src)).clone()
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
    fn a_missing_module_is_an_ordinary_error() {
        let primary = PathBuf::from("/nonexistent/dir/main.gm");
        let a = Analysis::run(Path::new("/nonexistent/dir"), &HashMap::new(), &primary, false);
        let d = a.diagnostics(Encoding::Utf16, &|p| path_to_uri(p));
        assert_eq!(d[&primary].len(), 1);
        assert!(d[&primary][0]["message"].as_str().unwrap().contains("cannot read"));
    }

    /// A module directory with one file `main.gm` holding `text`; the directory is
    /// removed when the returned guard is dropped.
    struct Module {
        dir: PathBuf,
        file: PathBuf,
    }

    impl Drop for Module {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.dir);
        }
    }

    fn module_with(name: &str, text: &str) -> Module {
        let dir = canon(&std::env::temp_dir()).join(format!("gamma-lsp-{}-{}", name, std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("main.gm");
        std::fs::write(&file, text).unwrap();
        Module { dir, file }
    }

    #[test]
    fn a_panic_in_the_checker_becomes_a_diagnostic() {
        let m = module_with("ice", "main :: fn() {}\n");
        let boom = |_: &mut Session, _: &Path| -> IdeIndex { panic!("boom") };
        let a = Analysis::run_with(&m.dir, &HashMap::new(), &m.file, &boom);
        let d = a.diagnostics(Encoding::Utf16, &|p| path_to_uri(p));
        assert_eq!(d[&m.file].len(), 1);
        assert_eq!(d[&m.file][0]["message"], "internal compiler error: boom");
        assert_eq!(d[&m.file][0]["range"]["start"], serde_json::json!({"line": 0, "character": 0}));
    }

    #[test]
    fn a_panic_with_syntax_errors_reports_the_syntax_errors() {
        let m = module_with("ice2", "main :: fn( {\n");
        let boom = |_: &mut Session, _: &Path| -> IdeIndex { panic!("boom") };
        let a = Analysis::run_with(&m.dir, &HashMap::new(), &m.file, &boom);
        let d = a.diagnostics(Encoding::Utf16, &|p| path_to_uri(p));
        assert!(!d[&m.file].is_empty());
        assert!(d[&m.file].iter().all(|x| !x["message"].as_str().unwrap().contains("internal")), "{:?}", d);
    }
}
