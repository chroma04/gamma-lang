//! Runs the whole front end (lexing, parsing, name resolution, type checking and lowering)
//! on arbitrary text as a single-file program, with the real standard library. Diagnostics
//! are fine; panics and hangs are bugs.
#![no_main]

use gamma::driver::Session;
use libfuzzer_sys::fuzz_target;
use std::path::PathBuf;

/// The compiler's stack size, as in the command-line tool: its passes recurse over the tree.
const STACK: usize = 1 << 30;

fuzz_target!(|data: &[u8]| {
    let Ok(src) = std::str::from_utf8(data) else { return };
    // The standard library is `lib/std` of the source tree the compiler was built from.
    let path = PathBuf::from("/gamma-fuzz-input/main.gm");
    let result = std::thread::scope(|s| {
        std::thread::Builder::new()
            .stack_size(STACK)
            .spawn_scoped(s, || {
                let mut sess = Session::new();
                // The program lives in memory only: no file is read or written for it.
                sess.overlays.insert(gamma::driver::canonical_path(&path), src.to_string());
                gamma::sema::analyze(&mut sess, &path);
                sess.render_diagnostics(gamma::diag::DiagFormat::Human);
            })
            .expect("cannot start the checker thread")
            .join()
    });
    if let Err(panic) = result {
        std::panic::resume_unwind(panic);
    }
});
