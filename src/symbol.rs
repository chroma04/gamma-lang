//! Interned identifier strings.
//!
//! A [`Symbol`] is a small copyable handle to a string stored for the life of
//! the process. Interning makes identifier comparison and hashing cheap.

use std::collections::HashMap;
use std::fmt;
use std::sync::atomic::{AtomicPtr, Ordering};
use std::sync::{Mutex, OnceLock};

#[derive(Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Symbol(u32);

struct Interner {
    map: HashMap<&'static str, Symbol>,
    /// The number of strings interned; the strings themselves are in [`STRINGS`].
    len: usize,
}

fn interner() -> &'static Mutex<Interner> {
    static INTERNER: OnceLock<Mutex<Interner>> = OnceLock::new();
    INTERNER.get_or_init(|| Mutex::new(Interner { map: HashMap::new(), len: 0 }))
}

/// The interned strings by symbol, readable without the interner's lock: `Symbol::as_str`
/// is called from every phase and from the language server's thread. Chunk `c` holds
/// `FIRST_CHUNK << c` slots; chunks are allocated (under the lock) and never freed.
static STRINGS: [AtomicPtr<AtomicPtr<&'static str>>; 32] = [const { AtomicPtr::new(std::ptr::null_mut()) }; 32];
const FIRST_CHUNK: usize = 1024;

/// The chunk and the offset in it of slot `idx` of [`STRINGS`].
#[inline(always)]
fn slot(idx: usize) -> (usize, usize) {
    let v = idx / FIRST_CHUNK + 1;
    let chunk = (usize::BITS - 1 - v.leading_zeros()) as usize;
    (chunk, idx - FIRST_CHUNK * ((1 << chunk) - 1))
}

impl Symbol {
    pub fn intern(s: &str) -> Symbol {
        let mut i = interner().lock().unwrap();
        if let Some(&sym) = i.map.get(s) {
            return sym;
        }
        let leaked: &'static str = Box::leak(s.to_owned().into_boxed_str());
        let sym = Symbol(i.len as u32);
        let (chunk, off) = slot(i.len);
        let mut base = STRINGS[chunk].load(Ordering::Acquire);
        if base.is_null() {
            let slots: Box<[AtomicPtr<&'static str>]> =
                (0..FIRST_CHUNK << chunk).map(|_| AtomicPtr::new(std::ptr::null_mut())).collect();
            base = Box::leak(slots).as_mut_ptr();
            STRINGS[chunk].store(base, Ordering::Release);
        }
        let cell: &'static mut &'static str = Box::leak(Box::new(leaked));
        // SAFETY: `off` is within the chunk, which lives forever.
        unsafe { (*base.add(off)).store(cell, Ordering::Release) };
        i.len += 1;
        i.map.insert(leaked, sym);
        sym
    }

    pub fn as_str(self) -> &'static str {
        let (chunk, off) = slot(self.0 as usize);
        let base = STRINGS[chunk].load(Ordering::Acquire);
        // SAFETY: a `Symbol` exists only after `Symbol::intern` published its string (and
        // the chunk holding it) with release stores; nothing is ever freed or moved.
        unsafe { *(*base.add(off)).load(Ordering::Acquire) }
    }
}

impl fmt::Debug for Symbol {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.as_str())
    }
}

impl fmt::Display for Symbol {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

#[cfg(test)]
mod tests {
    use super::Symbol;

    #[test]
    fn interning_is_idempotent() {
        let a = Symbol::intern("hello");
        let b = Symbol::intern("hello");
        let c = Symbol::intern("world");
        assert_eq!(a, b);
        assert_ne!(a, c);
        assert_eq!(a.as_str(), "hello");
    }

    #[test]
    fn many_symbols_across_chunks_and_threads() {
        let names: Vec<String> = (0..5000).map(|i| format!("sym_{i}")).collect();
        let syms: Vec<Symbol> = names.iter().map(|n| Symbol::intern(n)).collect();
        std::thread::scope(|s| {
            for _ in 0..4 {
                s.spawn(|| {
                    for (n, sym) in names.iter().zip(&syms) {
                        assert_eq!(sym.as_str(), n);
                        assert_eq!(Symbol::intern(n), *sym);
                    }
                });
            }
        });
    }
}
