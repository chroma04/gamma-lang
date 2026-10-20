//! Parses arbitrary text as a package manifest (`gamma.toml`) and as a lockfile
//! (`gamma.lock`): both come from other people's repositories. Errors are fine; panics are
//! bugs, and so is a lock entry accepted with a value that could name a path outside the
//! package store.
#![no_main]

use gamma::pkg::lock::Lock;
use gamma::pkg::manifest::Manifest;
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let Ok(text) = std::str::from_utf8(data) else { return };
    let _ = Manifest::parse(text);
    if let Ok(lock) = Lock::parse(text) {
        for p in &lock.packages {
            assert!(!p.rev.contains(['/', '\\', '.']), "{:?}", p.rev);
            if let Some(a) = &p.archive {
                assert!(!a.contains(['/', '\\']) && !a.contains(".."), "{:?}", a);
            }
        }
        // What is accepted renders and parses again.
        let again = Lock::parse(&lock.render()).expect("a rendered lock parses");
        assert_eq!(again.packages.len(), lock.packages.len());
    }
});
