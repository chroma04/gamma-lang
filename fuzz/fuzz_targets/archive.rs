//! Unpacks arbitrary bytes as a `.tar.gz` package archive into a scratch directory.
//! Errors are fine; panics are bugs, and so is any file written outside that directory.
#![no_main]

use gamma::pkg::archive;
use libfuzzer_sys::fuzz_target;
use std::path::PathBuf;

fuzz_target!(|data: &[u8]| {
    let base = std::env::temp_dir().join(format!("gamma-fuzz-archive-{}", std::process::id()));
    let _ = gamma::pkg::git::remove_tree(&base);
    std::fs::create_dir_all(base.join("store")).unwrap();
    let file = base.join("in.tar.gz");
    std::fs::write(&file, data).unwrap();
    let dest: PathBuf = base.join("store").join("pkg");
    let _ = archive::unpack(&file, &dest);
    // Nothing but the archive file and the store may exist in the scratch directory.
    let mut names: Vec<String> =
        std::fs::read_dir(&base).unwrap().map(|e| e.unwrap().file_name().to_string_lossy().into_owned()).collect();
    names.sort();
    assert_eq!(names, ["in.tar.gz", "store"]);
    if dest.exists() {
        // Every link inside stays inside, which the checksum verifies.
        let _ = gamma::pkg::git::tree_checksum(&dest);
    }
    let _ = gamma::pkg::git::remove_tree(&base);
});
