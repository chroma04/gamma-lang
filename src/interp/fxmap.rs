//! A fast, non-cryptographic hasher for the machine's tables, which are keyed by
//! integers and small tuples that the program cannot choose adversarially.

use std::collections::HashMap;
use std::hash::{BuildHasherDefault, Hasher};

#[derive(Default, Clone, Copy)]
pub struct FxHasher {
    hash: u64,
}

const SEED: u64 = 0x51_7c_c1_b7_27_22_0a_95;

impl FxHasher {
    #[inline(always)]
    fn add(&mut self, w: u64) {
        self.hash = (self.hash.rotate_left(5) ^ w).wrapping_mul(SEED);
    }
}

impl Hasher for FxHasher {
    #[inline]
    fn write(&mut self, bytes: &[u8]) {
        let mut chunks = bytes.chunks_exact(8);
        for c in &mut chunks {
            self.add(u64::from_le_bytes(c.try_into().unwrap()));
        }
        let rest = chunks.remainder();
        if !rest.is_empty() {
            let mut t = [0u8; 8];
            t[..rest.len()].copy_from_slice(rest);
            self.add(u64::from_le_bytes(t));
        }
    }

    #[inline(always)]
    fn write_u8(&mut self, i: u8) {
        self.add(i as u64);
    }

    #[inline(always)]
    fn write_u32(&mut self, i: u32) {
        self.add(i as u64);
    }

    #[inline(always)]
    fn write_u64(&mut self, i: u64) {
        self.add(i);
    }

    #[inline(always)]
    fn write_usize(&mut self, i: usize) {
        self.add(i as u64);
    }

    #[inline(always)]
    fn finish(&self) -> u64 {
        self.hash
    }
}

pub type FxMap<K, V> = HashMap<K, V, BuildHasherDefault<FxHasher>>;
