//! Conversions between byte offsets and LSP positions (a line and a character, counted
//! in UTF-16 code units by default, or in bytes when the client offers UTF-8).

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Encoding {
    Utf8,
    Utf16,
}

impl Encoding {
    pub fn name(self) -> &'static str {
        match self {
            Encoding::Utf8 => "utf-8",
            Encoding::Utf16 => "utf-16",
        }
    }

    fn width(self, c: char) -> usize {
        match self {
            Encoding::Utf8 => c.len_utf8(),
            Encoding::Utf16 => c.len_utf16(),
        }
    }
}

pub struct LineIndex<'a> {
    text: &'a str,
    /// Byte offset of the start of each line.
    starts: Vec<usize>,
}

impl<'a> LineIndex<'a> {
    pub fn new(text: &'a str) -> LineIndex<'a> {
        let mut starts = vec![0];
        starts.extend(text.bytes().enumerate().filter(|(_, b)| *b == b'\n').map(|(i, _)| i + 1));
        LineIndex { text, starts }
    }

    /// The end of line `line`'s text: before its terminator.
    fn line_end(&self, line: usize) -> usize {
        let end = self.starts.get(line + 1).copied().unwrap_or(self.text.len());
        let t = &self.text[self.starts[line]..end];
        self.starts[line] + t.trim_end_matches(['\n', '\r']).len()
    }

    /// The position of byte `offset` (rounded down to a character boundary).
    pub fn position(&self, offset: usize, enc: Encoding) -> (u32, u32) {
        let mut offset = offset.min(self.text.len());
        while !self.text.is_char_boundary(offset) {
            offset -= 1;
        }
        let line = self.starts.partition_point(|&s| s <= offset) - 1;
        let col: usize = self.text[self.starts[line]..offset].chars().map(|c| enc.width(c)).sum();
        (line as u32, col as u32)
    }

    /// The byte offset of a position. A character past the end of its line gives the
    /// end of the line, and a line past the end of the text gives the end of the text.
    pub fn offset(&self, line: u32, character: u32, enc: Encoding) -> usize {
        let line = line as usize;
        if line >= self.starts.len() {
            return self.text.len();
        }
        let (start, end) = (self.starts[line], self.line_end(line));
        let mut units = 0usize;
        for (i, c) in self.text[start..end].char_indices() {
            let w = enc.width(c);
            if units + w > character as usize {
                return start + i;
            }
            units += w;
        }
        end
    }

    /// The position just past the last character.
    #[cfg(test)]
    pub fn end(&self, enc: Encoding) -> (u32, u32) {
        self.position(self.text.len(), enc)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEXT: &str = "ab\nçd✓e\n😀x\r\nlast";

    #[test]
    fn ascii_positions() {
        let ix = LineIndex::new(TEXT);
        assert_eq!(ix.position(0, Encoding::Utf16), (0, 0));
        assert_eq!(ix.position(2, Encoding::Utf16), (0, 2));
        assert_eq!(ix.position(3, Encoding::Utf16), (1, 0));
    }

    #[test]
    fn utf16_counts_code_units() {
        let ix = LineIndex::new(TEXT);
        // Line 1 is `çd✓e`: ç is 2 bytes, ✓ is 3 bytes; all are one UTF-16 unit.
        let e = TEXT.find('e').unwrap();
        assert_eq!(ix.position(e, Encoding::Utf16), (1, 3));
        assert_eq!(ix.position(e, Encoding::Utf8), (1, 6));
        // Line 2 starts with an emoji: 4 bytes, 2 UTF-16 units.
        let x = TEXT.find('x').unwrap();
        assert_eq!(ix.position(x, Encoding::Utf16), (2, 2));
        assert_eq!(ix.position(x, Encoding::Utf8), (2, 4));
    }

    #[test]
    fn offsets_invert_positions() {
        let ix = LineIndex::new(TEXT);
        for enc in [Encoding::Utf8, Encoding::Utf16] {
            for (off, _) in TEXT.char_indices() {
                // The `\n` of a `\r\n` has the position of the end of its line.
                if TEXT[..off].ends_with('\r') {
                    continue;
                }
                let (l, c) = ix.position(off, enc);
                assert_eq!(ix.offset(l, c, enc), off, "offset {} in {:?}", off, enc);
            }
        }
    }

    #[test]
    fn clamps_out_of_range_positions() {
        let ix = LineIndex::new(TEXT);
        // Past the end of line 0.
        assert_eq!(ix.offset(0, 99, Encoding::Utf16), 2);
        // Past the end of line 2, before its `\r\n`.
        let x = TEXT.find('x').unwrap();
        assert_eq!(ix.offset(2, 99, Encoding::Utf16), x + 1);
        // Past the last line.
        assert_eq!(ix.offset(40, 0, Encoding::Utf16), TEXT.len());
    }

    #[test]
    fn middle_of_a_surrogate_pair_rounds_down() {
        let ix = LineIndex::new(TEXT);
        let start = TEXT.find('😀').unwrap();
        assert_eq!(ix.offset(2, 1, Encoding::Utf16), start);
    }

    #[test]
    fn offsets_inside_a_character_round_down() {
        let ix = LineIndex::new(TEXT);
        let c = TEXT.find('ç').unwrap();
        assert_eq!(ix.position(c + 1, Encoding::Utf16), (1, 0));
    }

    #[test]
    fn end_of_text() {
        let ix = LineIndex::new(TEXT);
        assert_eq!(ix.end(Encoding::Utf16), (3, 4));
        assert_eq!(LineIndex::new("").end(Encoding::Utf16), (0, 0));
        assert_eq!(LineIndex::new("a\n").end(Encoding::Utf16), (1, 0));
    }
}
