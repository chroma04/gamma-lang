//! `gamma new` and `gamma init`: the files of a fresh package.

use super::manifest::is_identifier;
use std::path::Path;
use std::process::{Command, Stdio};

/// A package name for a directory name: other characters become `_`, and a leading digit
/// gets a `_` in front.
pub fn sanitize_name(dir_name: &str) -> String {
    let mut s: String = dir_name.chars().map(|c| if c.is_ascii_alphanumeric() { c } else { '_' }).collect();
    if s.is_empty() || s.chars().all(|c| c == '_') {
        return "app".into();
    }
    if s.starts_with(|c: char| c.is_ascii_digit()) {
        s.insert(0, '_');
    }
    s
}

pub fn manifest(name: &str) -> String {
    format!("[package]\nname = \"{}\"\nversion = \"0.1.0\"\n\n[dependencies]\n", name)
}

pub fn main_source(name: &str) -> String {
    format!(
        "main :: fn() needs Out {{\n    print(\"hello from {}\\n\")\n}}\n\n\
         double :: fn(n: i32) -> i32 => n * 2\n\n\
         test \"double\" {{\n    expect_eq(double(21), 42)\n}}\n",
        name
    )
}

/// Ignores what `gamma build` writes: the executable is named after the directory given
/// (`main` for `.`), with `.exe` on Windows.
pub fn gitignore(name: &str) -> String {
    format!("/main\n/main.exe\n/{name}\n/{name}.exe\n/.gamma/\n")
}

/// What a scaffold did.
#[derive(Debug, Default)]
pub struct Report {
    pub created: Vec<String>,
    pub skipped: Vec<String>,
    pub git_initialized: bool,
}

/// Writes the package files into `dir` (which exists), leaving existing files alone.
pub fn scaffold(dir: &Path, name: &str, git: bool) -> Result<Report, String> {
    assert!(is_identifier(name));
    let mut report = Report::default();
    let files = [("gamma.toml", manifest(name)), ("main.gm", main_source(name)), (".gitignore", gitignore(name))];
    for (file, text) in files {
        let path = dir.join(file);
        match std::fs::OpenOptions::new().write(true).create_new(true).open(&path) {
            Ok(mut f) => {
                use std::io::Write;
                f.write_all(text.as_bytes()).map_err(|e| format!("cannot write {}: {}", path.display(), e))?;
                report.created.push(file.to_string());
            }
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => report.skipped.push(file.to_string()),
            Err(e) => return Err(format!("cannot write {}: {}", path.display(), e)),
        }
    }
    if git && git_available() && !inside_work_tree(dir) {
        report.git_initialized = Command::new("git")
            .arg("init")
            .arg("-q")
            .arg(dir)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_ok_and(|s| s.success());
    }
    Ok(report)
}

fn git_available() -> bool {
    Command::new("git").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok_and(|s| s.success())
}

fn inside_work_tree(dir: &Path) -> bool {
    Command::new("git")
        .arg("-C")
        .arg(dir)
        .args(["rev-parse", "--is-inside-work-tree"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .is_ok_and(|s| s.success())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_become_identifiers() {
        assert_eq!(sanitize_name("my-app"), "my_app");
        assert_eq!(sanitize_name("2fast"), "_2fast");
        assert_eq!(sanitize_name("app"), "app");
        assert_eq!(sanitize_name("a.b c"), "a_b_c");
        assert_eq!(sanitize_name("---"), "app");
        assert_eq!(sanitize_name(""), "app");
        for n in ["my-app", "2fast", "x y", "ünï"] {
            assert!(is_identifier(&sanitize_name(n)), "{}", n);
        }
    }

    #[test]
    fn templates() {
        assert!(manifest("app").contains("name = \"app\""));
        assert!(main_source("app").contains("hello from app\\n"));
        assert_eq!(gitignore("app"), "/main\n/main.exe\n/app\n/app.exe\n/.gamma/\n");
    }
}
