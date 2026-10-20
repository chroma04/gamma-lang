//! `docs/architecture.md` names every source file, so that its map of the code does not
//! drift as files are added.

use std::path::Path;

fn sources(dir: &Path, out: &mut Vec<String>) {
    for e in std::fs::read_dir(dir).unwrap() {
        let p = e.unwrap().path();
        if p.is_dir() {
            sources(&p, out);
        } else if p.extension().is_some_and(|x| x == "rs") {
            out.push(p.to_string_lossy().replace('\\', "/"));
        }
    }
}

#[test]
fn every_source_file_is_mentioned() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let doc = std::fs::read_to_string(root.join("docs/architecture.md")).unwrap();
    let mut files = vec![];
    sources(&root.join("src"), &mut files);
    let mut missing = vec![];
    for f in &files {
        let rel = f.split("/src/").last().unwrap();
        let name = Path::new(rel).file_name().unwrap().to_string_lossy().into_owned();
        // Module roots and unit-test files go with their directory.
        if name == "mod.rs" || name == "lib.rs" || name.starts_with("tests") {
            continue;
        }
        let stem = name.trim_end_matches(".rs");
        let named = doc.contains(rel)
            || doc.contains(&format!("`{name}`"))
            || doc.contains(&format!("/{name}"))
            || doc.contains(&format!("`{stem}`"))
            || doc.contains(&format!("{name} "))
            || doc.contains(&format!("{name},"))
            || doc.contains(&format!("{name})"));
        if !named {
            missing.push(rel.to_string());
        }
    }
    assert!(missing.is_empty(), "docs/architecture.md does not mention: {}", missing.join(", "));
}
