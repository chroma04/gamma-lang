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
        if self == Span::DUMMY {
            return other;
        }
        if other == Span::DUMMY {
            return self;
        }
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
    /// True if `src` is ASCII, so that columns are byte distances.
    ascii: bool,
}

impl SourceFile {
    fn new(path: PathBuf, src: String) -> SourceFile {
        let mut line_starts = vec![0];
        for (i, b) in src.bytes().enumerate() {
            if b == b'\n' {
                line_starts.push(i as u32 + 1);
            }
        }
        let ascii = src.is_ascii();
        SourceFile { path, src, line_starts, ascii }
    }

    /// 1-based line and column (column counted in Unicode scalar values) of `offset`.
    pub fn line_col(&self, offset: u32) -> (u32, u32) {
        let line = match self.line_starts.binary_search(&offset) {
            Ok(i) => i,
            Err(i) => i - 1,
        };
        let start = self.line_starts[line] as usize;
        let end = (offset as usize).min(self.src.len());
        let col =
            if self.ascii { end - start } else { self.src.get(start..end).map_or(end - start, |s| s.chars().count()) };
        (line as u32 + 1, col as u32 + 1)
    }

    /// The text of the 1-based line `line`, without its terminator.
    pub fn line_text(&self, line: u32) -> &str {
        let i = (line.max(1) - 1) as usize;
        let start = self.line_starts[i] as usize;
        let end = self.line_starts.get(i + 1).map_or(self.src.len(), |&e| e as usize);
        self.src[start..end].trim_end_matches(['\n', '\r'])
    }

    /// True if the file is entirely ASCII.
    pub fn is_ascii(&self) -> bool {
        self.ascii
    }

    pub fn line_count(&self) -> u32 {
        self.line_starts.len() as u32
    }
}

/// The canonical form of `path` (`path` itself if it does not exist).
pub fn canonical(path: &Path) -> PathBuf {
    canonicalize(path).unwrap_or_else(|_| path.to_path_buf())
}

/// [`Path::canonicalize`], except that on Windows the result has no `\\?\` prefix: no
/// other spelling of a path has it, it would show in diagnostics, and C compilers and
/// other tools cannot open such paths. Use this instead of `canonicalize`.
pub fn canonicalize(path: &Path) -> std::io::Result<PathBuf> {
    let c = path.canonicalize()?;
    Ok(match c.to_str().and_then(|s| s.strip_prefix(r"\\?\")) {
        Some(plain) if !plain.starts_with("UNC\\") => PathBuf::from(plain),
        _ => c,
    })
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
    /// Panics if the text is 4 GiB or longer, as byte offsets are 32 bits; use
    /// [`SourceMap::try_add`] for text of unknown size.
    pub fn add(&mut self, path: impl Into<PathBuf>, src: String) -> FileId {
        match self.try_add(path, src) {
            Ok(id) => id,
            Err(e) => panic!("{e}"),
        }
    }

    /// Like [`SourceMap::add`], but reports a source that is too long as an error.
    pub fn try_add(&mut self, path: impl Into<PathBuf>, src: String) -> Result<FileId, String> {
        let path = path.into();
        if u32::try_from(src.len()).is_err() {
            return Err(format!("{}: source file is too large (the limit is 4 GiB)", path.display()));
        }
        let id = FileId(self.files.len() as u32);
        // File names show as they do everywhere else: with `/` (Windows accepts it too).
        #[cfg(windows)]
        let path = PathBuf::from(path.to_string_lossy().replace('\\', "/"));
        self.files.push(SourceFile::new(path, src));
        Ok(id)
    }

    /// Reads a file from disk. Invalid UTF-8 is reported as an error message.
    pub fn load(&mut self, path: &Path) -> Result<FileId, String> {
        let bytes = std::fs::read(path).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
        let src = String::from_utf8(bytes).map_err(|e| {
            format!("{}: source file is not valid UTF-8 (at byte {})", path.display(), e.utf8_error().valid_up_to())
        })?;
        self.try_add(path, src)
    }

    pub fn file(&self, id: FileId) -> &SourceFile {
        &self.files[id.0 as usize]
    }

    pub fn text(&self, span: Span) -> &str {
        &self.file(span.file).src[span.lo as usize..span.hi as usize]
    }

    /// The number of files.
    pub fn len(&self) -> usize {
        self.files.len()
    }

    pub fn is_empty(&self) -> bool {
        self.files.is_empty()
    }

    pub fn files(&self) -> impl Iterator<Item = (FileId, &SourceFile)> {
        self.files.iter().enumerate().map(|(i, f)| (FileId(i as u32), f))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn joining_with_a_dummy_span_keeps_the_real_one() {
        let real = Span::new(FileId(0), 3, 7);
        assert_eq!(Span::DUMMY.to(real), real);
        assert_eq!(real.to(Span::DUMMY), real);
    }

    #[test]
    fn columns_count_characters() {
        let mut sm = SourceMap::new();
        let a = sm.add("a.gm", "ab\ncd".to_string());
        let u = sm.add("u.gm", "é1\n€x".to_string());
        assert_eq!(sm.file(a).line_col(4), (2, 2));
        assert_eq!(sm.file(u).line_col(2), (1, 2));
        assert_eq!(sm.file(u).line_col(7), (2, 2));
        assert_eq!(sm.file(a).line_text(0), "ab");
    }
}
