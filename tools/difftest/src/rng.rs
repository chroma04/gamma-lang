//! A small deterministic random number generator (SplitMix64), so that a seed always
//! gives the same program on every platform.

pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Rng {
        let mut r = Rng(seed ^ 0x9E37_79B9_7F4A_7C15);
        r.next();
        r
    }

    pub fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// A number in `0..n` (`n > 0`).
    pub fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }

    /// True with probability `percent`/100.
    pub fn chance(&mut self, percent: usize) -> bool {
        self.below(100) < percent
    }

    pub fn pick<'a, T>(&mut self, items: &'a [T]) -> &'a T {
        &items[self.below(items.len())]
    }
}
