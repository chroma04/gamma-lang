//! The layout of an installed release: `<prefix>/bin/gamma` finds the standard library in
//! `<prefix>/lib/gamma/std`, without `GAMMA_STD` and without the source tree.

use std::path::PathBuf;
use std::process::Command;

#[test]
fn installed_compiler_finds_its_standard_library() {
    let prefix = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("install-layout");
    let _ = std::fs::remove_dir_all(&prefix);
    let bin = prefix.join("bin");
    let std_dir = prefix.join("lib").join("gamma").join("std");
    std::fs::create_dir_all(&bin).unwrap();
    // A module that exists only in the installed library, so that the test shows which
    // directory the compiler used.
    std::fs::create_dir_all(std_dir.join("only_installed")).unwrap();
    std::fs::write(std_dir.join("only_installed").join("m.gm"), "pub answer :: fn() -> i32 { return 42 }\n").unwrap();
    let exe = bin.join(if cfg!(windows) { "gamma.exe" } else { "gamma" });
    std::fs::copy(env!("CARGO_BIN_EXE_gamma"), &exe).unwrap();

    let src = prefix.join("main.gm");
    std::fs::write(
        &src,
        "m :: import \"std/only_installed\"\n\nmain :: fn() needs Out {\n    print(\"{}\\n\", m.answer())\n}\n",
    )
    .unwrap();
    let out = Command::new(&exe).env_remove("GAMMA_STD").arg("run").arg(&src).output().unwrap();
    assert!(out.status.success(), "gamma run failed:\n{}", String::from_utf8_lossy(&out.stderr));
    assert_eq!(String::from_utf8_lossy(&out.stdout), "42\n");

    // `GAMMA_STD` still wins over the installed library.
    let out = Command::new(&exe)
        .env("GAMMA_STD", concat!(env!("CARGO_MANIFEST_DIR"), "/lib/std"))
        .arg("check")
        .arg(&src)
        .output()
        .unwrap();
    assert!(!out.status.success(), "the module should not exist in $GAMMA_STD");
}
