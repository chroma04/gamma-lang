//! The counters of `gamma test --coverage`: one per source line that [`crate::coverage`]
//! instrumented, shared by every machine of the process.

use std::sync::OnceLock;
use std::sync::atomic::{AtomicU32, Ordering};

static COUNTERS: OnceLock<Box<[AtomicU32]>> = OnceLock::new();

/// Allocates `n` counters, all zero. The counters live as long as the process; a second
/// call changes nothing.
pub fn coverage_init(n: usize) {
    let _ = COUNTERS.set((0..n).map(|_| AtomicU32::new(0)).collect());
}

/// How many times each counter was hit.
pub fn coverage_counts() -> Vec<u32> {
    COUNTERS.get().map_or_else(Vec::new, |c| c.iter().map(|a| a.load(Ordering::Relaxed)).collect())
}

/// Counts one execution of the line behind counter `id`.
pub(super) fn cover_hit(id: u32) {
    if let Some(c) = COUNTERS.get().and_then(|c| c.get(id as usize)) {
        c.fetch_add(1, Ordering::Relaxed);
    }
}
