//! Checks the code examples in the documents listed in `CHECKED_DOCS`.
//!
//! In those documents every fenced code block whose info string starts with
//! `gamma` is checked according to the words that follow it:
//!
//! - ```` ```gamma ```` is a complete program. It must be formatted as `gamma fmt`
//!   formats it and must run with exit status 0. If the next fenced block is an
//!   ```` ```output ```` block, that is the program's expected standard output.
//! - ```` ```gamma test ```` is a complete module whose `test` declarations must all
//!   pass under `gamma test`. It must be formatted, too.
//! - ```` ```gamma error ```` is a program that must be rejected by `gamma check`.
//!   Each line that ends in a `// error: ...` comment must get at least one error,
//!   and every error must be on such a line, so the block fails for the reason
//!   the text gives.
//! - ```` ```gamma fragment ```` is part of a program and is not checked.
//!
//! Blocks in other languages (`c`, `rust`, `sh`, ...) are not checked. Runs go
//! through the interpreter and, where a C compiler is available and
//! `GAMMA_TEST_C` is not 0, through the C back end too; both must agree.
//! `GAMMA_TEST_FILTER=substring` runs only the blocks whose name
//! (`<document>:<line>`) contains it.

mod common;

use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

/// The documents whose examples are checked, relative to the repository root.
const CHECKED_DOCS: &[&str] = &["docs/guide/for-c-programmers.md", "docs/guide/for-rust-programmers.md"];

#[derive(Clone, Copy, PartialEq, Debug)]
enum Kind {
    Run,
    Test,
    Error,
}

struct Block {
    /// `<document>:<line of the opening fence>`.
    name: String,
    kind: Kind,
    code: String,
    expected: Option<String>,
}

fn root() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

/// The fenced blocks of `text` as (line of the opening fence, info string, contents).
fn fences(text: &str) -> Result<Vec<(usize, String, String)>, String> {
    let mut out = vec![];
    let mut open: Option<(usize, String, String, String)> = None;
    for (i, line) in text.lines().enumerate() {
        let trimmed = line.trim_start();
        match &mut open {
            None => {
                if let Some(info) = trimmed.strip_prefix("```") {
                    let indent = line[..line.len() - trimmed.len()].to_string();
                    open = Some((i + 1, info.trim().to_string(), String::new(), indent));
                }
            }
            Some((_, _, body, indent)) => {
                if trimmed.starts_with("```") && trimmed.trim() == "```" {
                    let (start, info, body, _) = open.take().unwrap();
                    out.push((start, info, body));
                } else {
                    body.push_str(line.strip_prefix(indent.as_str()).unwrap_or(line));
                    body.push('\n');
                }
            }
        }
    }
    match open {
        Some((start, ..)) => Err(format!("line {}: unclosed code block", start)),
        None => Ok(out),
    }
}

fn blocks(doc: &str) -> Vec<Block> {
    let text = fs::read_to_string(root().join(doc)).unwrap_or_else(|e| panic!("{}: {}", doc, e));
    let fences = fences(&text).unwrap_or_else(|e| panic!("{}: {}", doc, e));
    let mut out: Vec<Block> = vec![];
    for (line, info, body) in fences {
        let mut words = info.split_whitespace();
        let lang = words.next().unwrap_or("");
        let name = format!("{}:{}", doc, line);
        if lang == "output" {
            match out.last_mut() {
                Some(b) if b.kind == Kind::Run && b.expected.is_none() => b.expected = Some(body),
                _ => panic!("{}: an `output` block must follow a `gamma` program block", name),
            }
            continue;
        }
        if lang != "gamma" {
            continue;
        }
        let kind = match words.next() {
            None => Kind::Run,
            Some("test") => Kind::Test,
            Some("error") => Kind::Error,
            Some("fragment") => continue,
            Some(w) => panic!("{}: unknown block kind `{}` (expected test, error or fragment)", name, w),
        };
        out.push(Block { name, kind, code: body, expected: None });
    }
    out
}

/// Writes the block to its own directory and returns the path of the source file.
fn write_block(block: &Block) -> PathBuf {
    let dir_name: String = block.name.chars().map(|c| if c.is_ascii_alphanumeric() { c } else { '_' }).collect();
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("doc-examples").join(dir_name);
    fs::create_dir_all(&dir).unwrap();
    let file = dir.join("example.gm");
    fs::write(&file, &block.code).unwrap();
    file
}

fn gamma(args: &[&str], file: &Path) -> (i32, String, String) {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.env("GAMMA_CACHE_DIR", Path::new(env!("CARGO_TARGET_TMPDIR")).join("gamma-cache"))
        .current_dir(file.parent().unwrap())
        .args(args)
        .arg(file.file_name().unwrap())
        .stdin(Stdio::null());
    let out = common::output(&mut cmd).expect("cannot run gamma");
    (
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    )
}

/// Checks that the errors `gamma check` reports are exactly on the `// error:` lines.
fn check_error(block: &Block, file: &Path) -> Result<(), String> {
    let (code, stdout, stderr) = gamma(&["check", "--diag-format=short"], file);
    if code == 0 {
        return Err("the program was accepted, but the block is marked `error`".into());
    }
    let marked: Vec<usize> =
        block.code.lines().enumerate().filter(|(_, l)| l.contains("// error:")).map(|(i, _)| i + 1).collect();
    if marked.is_empty() {
        return Err("an `error` block needs at least one line with a `// error:` comment".into());
    }
    let mut reported = vec![];
    let prefix = format!("{}:", file.file_name().unwrap().to_string_lossy());
    for line in stderr.lines().chain(stdout.lines()) {
        let Some(rest) = line.strip_prefix(&prefix) else { continue };
        let mut parts = rest.splitn(3, ':');
        let (Some(l), Some(_col), Some(msg)) = (parts.next(), parts.next(), parts.next()) else { continue };
        if !msg.trim_start().starts_with("error") {
            continue;
        }
        let l: usize = l.parse().map_err(|_| format!("cannot read diagnostic `{}`", line))?;
        reported.push((l, line.to_string()));
    }
    let mut problems = vec![];
    for (l, line) in &reported {
        if !marked.contains(l) {
            problems.push(format!("error on a line not marked `// error:`: {}", line));
        }
    }
    for m in &marked {
        if !reported.iter().any(|(l, _)| l == m) {
            problems.push(format!("line {} is marked `// error:` but gets no error", m));
        }
    }
    if problems.is_empty() { Ok(()) } else { Err(format!("{}\ndiagnostics:\n{}", problems.join("\n"), stderr)) }
}

fn check(block: &Block, with_c: bool) -> Result<(), String> {
    let file = write_block(block);
    if block.kind == Kind::Error {
        return check_error(block, &file);
    }
    match gamma::fmt::format_source(&block.code) {
        Ok(f) if f == block.code => {}
        Ok(f) => return Err(format!("not formatted as `gamma fmt` formats it; formatted:\n{}", f)),
        Err(_) => return Err("`gamma fmt` cannot parse the block".into()),
    }
    let command = if block.kind == Kind::Test { "test" } else { "run" };
    let lanes: &[bool] = if with_c { &[false, true] } else { &[false] };
    let mut problems = vec![];
    for &c in lanes {
        let lane = if c { "[C back end]" } else { "[interpreter]" };
        let args: &[&str] = if c { &[command, "--backend=c"] } else { &[command] };
        let (code, stdout, stderr) = gamma(args, &file);
        if code != 0 {
            problems.push(format!("{} exit status {}\nstdout:\n{}stderr:\n{}", lane, code, stdout, stderr));
            continue;
        }
        if let Some(expected) = &block.expected
            && *expected != stdout
        {
            problems.push(format!("{} output differs.\nexpected:\n{}actual:\n{}", lane, expected, stdout));
        }
    }
    if problems.is_empty() { Ok(()) } else { Err(problems.join("\n")) }
}

#[test]
fn doc_examples() {
    let filter = std::env::var("GAMMA_TEST_FILTER").ok();
    let with_c = common::c_backend_available();
    let blocks: Vec<Block> = CHECKED_DOCS
        .iter()
        .flat_map(|d| blocks(d))
        .filter(|b| filter.as_ref().is_none_or(|f| b.name.contains(f.as_str())))
        .collect();
    let next = std::sync::atomic::AtomicUsize::new(0);
    let failures = std::sync::Mutex::new(Vec::new());
    let workers = std::thread::available_parallelism().map_or(1, |n| n.get());
    std::thread::scope(|s| {
        for _ in 0..workers {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(block) = blocks.get(i) else { break };
                    if let Err(msg) = check(block, with_c) {
                        failures.lock().unwrap().push(format!("FAIL {} ({:?})\n{}", block.name, block.kind, msg));
                    }
                }
            });
        }
    });
    let failures = failures.into_inner().unwrap();
    for f in &failures {
        eprintln!("{}\n", f);
    }
    eprintln!("doc examples: {} blocks, {} failed", blocks.len(), failures.len());
    assert!(failures.is_empty(), "{} doc example(s) failed", failures.len());
}

#[test]
fn fence_parsing() {
    let text = "intro\n```gamma\nmain :: fn() {}\n```\n\n```output\nhi\n```\n  ```c\n  int x;\n  ```\n";
    let f = fences(text).unwrap();
    assert_eq!(f.len(), 3);
    assert_eq!(f[0], (2, "gamma".to_string(), "main :: fn() {}\n".to_string()));
    assert_eq!(f[1].1, "output");
    assert_eq!(f[2].2, "int x;\n");
    assert!(fences("```gamma\nx\n").is_err());
}
