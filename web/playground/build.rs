//! Embeds the standard library (`lib/std/**/*.gm`) so that programs can import it with no
//! file system.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};

fn collect(dir: &Path, out: &mut Vec<PathBuf>) {
    let mut entries: Vec<_> = std::fs::read_dir(dir).unwrap().flatten().map(|e| e.path()).collect();
    entries.sort();
    for p in entries {
        if p.is_dir() {
            collect(&p, out);
        } else if p.extension().is_some_and(|e| e == "gm") {
            out.push(p);
        }
    }
}

fn main() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../lib/std");
    let root = root.canonicalize().unwrap();
    println!("cargo::rerun-if-changed={}", root.display());
    let mut files = Vec::new();
    collect(&root, &mut files);
    let mut src = String::from("pub static STD: &[(&str, &str)] = &[\n");
    for f in &files {
        let rel = f.strip_prefix(&root).unwrap().to_str().unwrap().replace('\\', "/");
        writeln!(src, "    ({:?}, include_str!({:?})),", rel, f.display().to_string()).unwrap();
    }
    src.push_str("];\n");
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("std_files.rs");
    std::fs::write(out, src).unwrap();
}
