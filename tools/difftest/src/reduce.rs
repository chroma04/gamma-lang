//! Shrinks a program that the back ends disagree on: delta debugging over its lines.

use crate::{Config, disagreement, run_all};
use std::path::Path;

/// Whether `lines` is still a program on which the same configurations disagree with the
/// interpreter as in the original.
fn interesting(cfg: &Config, lines: &[String], signature: &str, scratch: &Path) -> bool {
    let file = scratch.join("main.gm");
    if std::fs::write(&file, lines.join("\n") + "\n").is_err() {
        return false;
    }
    let results = run_all(cfg, &file);
    let base = &results[0].1;
    if base.code == -1 || base.code == 1 && base.stdout.is_empty() && base.stderr.contains("error") {
        return false;
    }
    disagreement(&results).as_deref() == Some(signature)
}

pub fn reduce_file(cfg: &Config, file: &Path) {
    let text = std::fs::read_to_string(file).unwrap_or_else(|e| {
        eprintln!("difftest: cannot read {}: {e}", file.display());
        std::process::exit(2)
    });
    let scratch = cfg.work.join("reduce");
    let _ = std::fs::create_dir_all(&scratch);
    let mut lines: Vec<String> = text.lines().map(str::to_string).collect();
    let first = run_all(cfg, file);
    let Some(signature) = disagreement(&first) else {
        eprintln!("difftest: the back ends agree on {}", file.display());
        std::process::exit(2)
    };
    eprintln!("reducing: {} lines, interp disagrees with {signature}", lines.len());
    let mut chunk = lines.len().div_ceil(2);
    loop {
        let mut changed = false;
        let mut at = 0;
        while at < lines.len() {
            let end = (at + chunk).min(lines.len());
            let mut candidate = lines.clone();
            candidate.drain(at..end);
            if !candidate.is_empty() && interesting(cfg, &candidate, &signature, &scratch) {
                lines = candidate;
                changed = true;
                eprintln!("  {} lines", lines.len());
            } else {
                at = end;
            }
        }
        if chunk == 1 {
            if !changed {
                break;
            }
        } else {
            chunk = chunk.div_ceil(2);
        }
    }
    let out = file.with_extension("min.gm");
    std::fs::write(&out, lines.join("\n") + "\n").expect("cannot write the reduced program");
    println!("{}", out.display());
}
