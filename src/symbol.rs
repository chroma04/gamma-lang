//! Interned identifier strings.
//!
//! A [`Symbol`] is a small copyable handle to a string stored for the life of
//! the process. Interning makes identifier comparison and hashing cheap.

use std::collections::HashMap;
use std::fmt;
use std::sync::{Mutex, OnceLock};

#[derive(Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Symbol(u32);

struct Interner {
    map: HashMap<&'static str, Symbol>,
    strings: Vec<&'static str>,
}

fn interner() -> &'static Mutex<Interner> {
    static INTERNER: OnceLock<Mutex<Interner>> = OnceLock::new();
    INTERNER.get_or_init(|| Mutex::new(Interner { map: HashMap::new(), strings: Vec::new() }))
}

impl Symbol {
    pub fn intern(s: &str) -> Symbol {
        let mut i = interner().lock().unwrap();
        if let Some(&sym) = i.map.get(s) {
            return sym;
        }
        let leaked: &'static str = Box::leak(s.to_owned().into_boxed_str());
        let sym = Symbol(i.strings.len() as u32);
        i.strings.push(leaked);
        i.map.insert(leaked, sym);
        sym
    }

    pub fn as_str(self) -> &'static str {
        interner().lock().unwrap().strings[self.0 as usize]
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
}
