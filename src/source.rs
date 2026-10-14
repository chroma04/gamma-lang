//! Source files, byte spans and line/column lookup.

use std::path::{Path, PathBuf};

/// Identifies a file registered in a [`SourceMap`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct FileId(pub u32);

/// A half-open byte range `lo..hi` within one source file.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Span {
    pub file: FileId,
    pub lo: u32,
    pub hi: u32,
}

impl Span {
    pub fn new(file: FileId, lo: u32, hi: u32) -> Span {
        debug_assert!(lo <= hi);
        Span { file, lo, hi }
    }

    /// A zero-width span used for synthesized nodes that have no source text.
    pub const DUMMY: Span = Span { file: FileId(u32::MAX), lo: 0, hi: 0 };

    /// The smallest span covering both `self` and `other` (same file).
    pub fn to(self, other: Span) -> Span {
        debug_assert_eq!(self.file, other.file);
        Span { file: self.file, lo: self.lo.min(other.lo), hi: self.hi.max(other.hi) }
    }

    /// The empty span at the start of `self`.
    pub fn shrink_to_lo(self) -> Span {
        Span { hi: self.lo, ..self }
    }

    /// The empty span at the end of `self`.
    pub fn shrink_to_hi(self) -> Span {
        Span { lo: self.hi, ..self }
    }

    pub fn len(self) -> u32 {
        self.hi - self.lo
    }

    pub fn is_empty(self) -> bool {
        self.lo == self.hi
    }
}

/// One loaded source file.
pub struct SourceFile {
    pub path: PathBuf,
    pub src: String,
    /// Byte offset of the start of each line.
    line_starts: Vec<u32>,
}

impl SourceFile {
    fn new(path: PathBuf, src: String) -> SourceFile {
        let mut line_starts = vec![0];
        for (i, b) in src.bytes().enumerate() {
            if b == b'\n' {
                line_starts.push(i as u32 + 1);
            }
        }
        SourceFile { path, src, line_starts }
    }

    /// 1-based line and column (column counted in Unicode scalar values) of `offset`.
    pub fn line_col(&self, offset: u32) -> (u32, u32) {
        let line = match self.line_starts.binary_search(&offset) {
            Ok(i) => i,
            Err(i) => i - 1,
        };
        let start = self.line_starts[line] as usize;
        let end = (offset as usize).min(self.src.len());
        let col = self.src.get(start..end).map_or(end - start, |s| s.chars().count());
        (line as u32 + 1, col as u32 + 1)
    }

    /// The text of the 1-based line `line`, without its terminator.
    pub fn line_text(&self, line: u32) -> &str {
        let i = (line - 1) as usize;
        let start = self.line_starts[i] as usize;
        let end = self.line_starts.get(i + 1).map_or(self.src.len(), |&e| e as usize);
        self.src[start..end].trim_end_matches(['\n', '\r'])
    }

    pub fn line_count(&self) -> u32 {
        self.line_starts.len() as u32
    }
}

/// The canonical form of `path` (`path` itself if it does not exist). On Windows the
/// canonical path of the system has a `\\?\` prefix that no other spelling of the path has
/// and that would show in diagnostics, so it is left off.
pub fn canonical(path: &Path) -> PathBuf {
    let c = path.canonicalize().unwrap_or_else(|_| path.to_path_buf());
    match c.to_str().and_then(|s| s.strip_prefix(r"\\?\")) {
        Some(plain) if !plain.starts_with("UNC\\") => PathBuf::from(plain),
        _ => c,
    }
}

/// All source files known to one compilation.
#[derive(Default)]
pub struct SourceMap {
    files: Vec<SourceFile>,
}

impl SourceMap {
    pub fn new() -> SourceMap {
        SourceMap::default()
    }

    /// Registers in-memory source text under `path` (which need not exist on disk).
    pub fn add(&mut self, path: impl Into<PathBuf>, src: String) -> FileId {
        let id = FileId(self.files.len() as u32);
        let path = path.into();
        // File names show as they do everywhere else: with `/` (Windows accepts it too).
        #[cfg(windows)]
        let path = PathBuf::from(path.to_string_lossy().replace('\\', "/"));
        self.files.push(SourceFile::new(path, src));
        id
    }

    /// Reads a file from disk. Invalid UTF-8 is reported as an error message.
    pub fn load(&mut self, path: &Path) -> Result<FileId, String> {
        let bytes = std::fs::read(path).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
        let src = String::from_utf8(bytes).map_err(|e| {
            format!("{}: source file is not valid UTF-8 (at byte {})", path.display(), e.utf8_error().valid_up_to())
        })?;
        Ok(self.add(path, src))
    }

    pub fn file(&self, id: FileId) -> &SourceFile {
        &self.files[id.0 as usize]
    }

    pub fn text(&self, span: Span) -> &str {
        &self.file(span.file).src[span.lo as usize..span.hi as usize]
    }

    pub fn files(&self) -> impl Iterator<Item = (FileId, &SourceFile)> {
        self.files.iter().enumerate().map(|(i, f)| (FileId(i as u32), f))
    }
}
