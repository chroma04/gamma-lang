//! A small-buffer-optimized byte string used for first-class values.

use std::ops::{Deref, DerefMut};

const INLINE: usize = 24;

/// Bytes of a value in target layout. Values up to 24 bytes are stored inline.
#[derive(Clone)]
pub enum Bytes {
    Inline(u8, [u8; INLINE]),
    Heap(Vec<u8>),
}

impl Bytes {
    pub fn empty() -> Bytes {
        Bytes::Inline(0, [0; INLINE])
    }

    pub fn zeroed(n: usize) -> Bytes {
        if n <= INLINE { Bytes::Inline(n as u8, [0; INLINE]) } else { Bytes::Heap(vec![0; n]) }
    }

    pub fn filled(n: usize, b: u8) -> Bytes {
        let mut r = Bytes::zeroed(n);
        r.fill(b);
        r
    }

    #[inline]
    pub fn from_slice(s: &[u8]) -> Bytes {
        let n = s.len();
        if n <= INLINE {
            let mut a = [0; INLINE];
            match n {
                8 => a[..8].copy_from_slice(&s[..8]),
                4 => a[..4].copy_from_slice(&s[..4]),
                1 => a[0] = s[0],
                2 => a[..2].copy_from_slice(&s[..2]),
                16 => a[..16].copy_from_slice(&s[..16]),
                _ => a[..n].copy_from_slice(s),
            }
            Bytes::Inline(n as u8, a)
        } else {
            Bytes::Heap(s.to_vec())
        }
    }

    /// The first `n` (at most 8) bytes of `w` in little-endian order.
    #[inline(always)]
    pub fn from_word(w: u64, n: usize) -> Bytes {
        debug_assert!(n <= 8);
        let mut a = [0; INLINE];
        a[..8].copy_from_slice(&w.to_le_bytes());
        Bytes::Inline(n as u8, a)
    }

    pub fn into_vec(self) -> Vec<u8> {
        match self {
            Bytes::Inline(n, a) => a[..n as usize].to_vec(),
            Bytes::Heap(v) => v,
        }
    }

    /// Resizes (zero-extending or truncating) to `n` bytes.
    pub fn resized(&self, n: usize) -> Bytes {
        let mut r = Bytes::zeroed(n);
        let m = n.min(self.len());
        r[..m].copy_from_slice(&self[..m]);
        r
    }
}

impl From<Vec<u8>> for Bytes {
    fn from(v: Vec<u8>) -> Bytes {
        if v.len() <= INLINE { Bytes::from_slice(&v) } else { Bytes::Heap(v) }
    }
}

impl Deref for Bytes {
    type Target = [u8];
    #[inline]
    fn deref(&self) -> &[u8] {
        match self {
            Bytes::Inline(n, a) => &a[..*n as usize],
            Bytes::Heap(v) => v,
        }
    }
}

impl DerefMut for Bytes {
    fn deref_mut(&mut self) -> &mut [u8] {
        match self {
            Bytes::Inline(n, a) => &mut a[..*n as usize],
            Bytes::Heap(v) => v,
        }
    }
}

impl std::fmt::Debug for Bytes {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", &**self)
    }
}

impl PartialEq for Bytes {
    fn eq(&self, other: &Bytes) -> bool {
        **self == **other
    }
}
