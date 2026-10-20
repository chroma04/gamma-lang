//! API documentation (`gamma doc`): the public declarations of a module, with their
//! doc comments, rendered as Markdown or HTML.
//!
//! Documentation is built from the syntax tree alone, so a module needs to parse but not
//! to type-check. Signatures are copied from the source text, between the spans the
//! parser recorded; doc comments (`///`) come from the parser, and the module's own
//! documentation is the `//!` lines at the top of its files. The output depends only on
//! the sources, never on the machine or the time, so it can be checked in and compared.
//!
//! * [`load_module`] reads one module (a directory of `.gm` files, or a single file);
//! * `markdown` and `html` render a [`Module`] and the index of a set of modules;
//! * [`run`] is the command-line driver.

mod collect;
mod html;
mod markdown;
pub(crate) mod md_html;

pub use collect::{Decl, Kind, Module, load_module};

use std::path::{Path, PathBuf};
use std::process::ExitCode;

pub const USAGE: &str = "\
usage: gamma doc <module directory | file | package root> [options]

Writes the documentation of the public declarations of a module (the directory of
.gm files that `import` names, or one file), from their `///` comments, and the `//!`
comments at the top of its files.

options:
  --format=markdown|html   the output format (default: markdown)
  -o <path>                write to <path>: a directory gets one file per module and an
                           index (index.md or index.html); a single module may also go
                           to a file. Without -o one module is printed to stdout
  --all                    document every module below the directory (needs -o)
  --missing                warn about public declarations and modules without
                           documentation
";

/// The output format.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Format {
    Markdown,
    Html,
}

impl Format {
    fn extension(self) -> &'static str {
        match self {
            Format::Markdown => "md",
            Format::Html => "html",
        }
    }
}

/// Renders one module. `index` is the relative link to the index page, if there is one.
pub fn render_module(m: &Module, format: Format, index: Option<&str>) -> String {
    match format {
        Format::Markdown => markdown::module(m, index),
        Format::Html => html::module(m, index),
    }
}

/// Renders the index page of `modules`.
pub fn render_index(title: &str, modules: &[Module], format: Format) -> String {
    match format {
        Format::Markdown => markdown::index(title, modules),
        Format::Html => html::index(title, modules),
    }
}

struct Args {
    path: PathBuf,
    format: Format,
    out: Option<PathBuf>,
    all: bool,
    missing: bool,
}

fn parse_format(v: &str) -> Result<Format, String> {
    match v {
        "markdown" | "md" => Ok(Format::Markdown),
        "html" => Ok(Format::Html),
        _ => Err(format!("unknown format `{}` (markdown or html)", v)),
    }
}

fn parse_args(args: &[String]) -> Result<Args, String> {
    let mut path = None;
    let mut format = Format::Markdown;
    let mut out = None;
    let mut all = false;
    let mut missing = false;
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--all" => all = true,
            "--missing" => missing = true,
            "-o" => out = Some(PathBuf::from(it.next().ok_or("`-o` needs a path")?)),
            "--format" => format = parse_format(it.next().ok_or("`--format` needs a value")?)?,
            s if s.starts_with("--format=") => format = parse_format(&s["--format=".len()..])?,
            s if s.starts_with('-') => return Err(format!("unknown option `{}`", s)),
            _ => {
                if path.replace(PathBuf::from(a)).is_some() {
                    return Err("more than one input given".into());
                }
            }
        }
    }
    let path = path.ok_or("no module given")?;
    Ok(Args { path, format, out, all, missing })
}

/// Runs `gamma doc` with the arguments after the command name.
pub fn run(args: &[String]) -> ExitCode {
    let args = match parse_args(args) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("gamma: {}\n\n{}", e, USAGE);
            return ExitCode::from(2);
        }
    };
    match run_with(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{}", e.trim_end());
            ExitCode::FAILURE
        }
    }
}

fn run_with(args: &Args) -> Result<(), String> {
    let modules = load_modules(&args.path, args.all)?;
    if args.missing {
        for m in &modules {
            for w in m.missing() {
                eprintln!("{}", w);
            }
        }
    }
    let ext = args.format.extension();
    match (&args.out, args.all) {
        (None, true) => Err("gamma: `--all` needs `-o <directory>`".into()),
        (None, false) => {
            print!("{}", render_module(&modules[0], args.format, None));
            Ok(())
        }
        (Some(out), false) if out.extension().is_some_and(|e| e == ext) && !out.is_dir() => {
            write_file(out, &render_module(&modules[0], args.format, None))
        }
        (Some(dir), _) => {
            let title = format!("{} modules", root_name(&args.path)?);
            for (rel, text) in output_files(&title, &modules, args.format) {
                write_file(&dir.join(rel), &text)?;
            }
            Ok(())
        }
    }
}

/// The files of a documentation tree, as paths below its directory with their contents:
/// one page per module and the index.
pub fn output_files(title: &str, modules: &[Module], format: Format) -> Vec<(String, String)> {
    let ext = format.extension();
    let mut files = Vec::new();
    for m in modules {
        let up = "../".repeat(m.path.matches('/').count());
        let index = format!("{}index.{}", up, ext);
        files.push((format!("{}.{}", m.path, ext), render_module(m, format, Some(&index))));
    }
    files.push((format!("index.{}", ext), render_index(title, modules, format)));
    files
}

fn write_file(path: &Path, text: &str) -> Result<(), String> {
    if let Some(parent) = path.parent().filter(|p| !p.as_os_str().is_empty()) {
        std::fs::create_dir_all(parent).map_err(|e| format!("gamma: {}: {}", parent.display(), e))?;
    }
    std::fs::write(path, text).map_err(|e| format!("gamma: {}: {}", path.display(), e))
}

/// The name of the module that `path` (a file or directory) denotes: its import path
/// (`std/io`) inside the standard library (a `lib/std` directory), else the name of the
/// directory or file with `.` and `..` resolved.
fn root_name(path: &Path) -> Result<String, String> {
    let canon = crate::source::canonicalize(path).map_err(|e| format!("gamma: {}: {}", path.display(), e))?;
    let dir = if canon.is_dir() { Some(canon.as_path()) } else { canon.parent() };
    let parts: Vec<String> =
        dir.iter().flat_map(|d| d.components()).map(|c| c.as_os_str().to_string_lossy().into_owned()).collect();
    if let Some(i) = parts.windows(2).rposition(|w| w[0] == "lib" && w[1] == "std") {
        // Also for a file: it is named after its module's directory.
        return Ok(parts[i + 1..].join("/"));
    }
    let name = if canon.is_dir() { canon.file_name() } else { canon.file_stem() };
    Ok(name.map_or("module".to_string(), |n| n.to_string_lossy().into_owned()))
}

/// The module(s) to document: a file or a directory, and with `all` every module below it.
pub fn load_modules(path: &Path, all: bool) -> Result<Vec<Module>, String> {
    if !path.exists() {
        return Err(format!("gamma: {}: no such file or directory", path.display()));
    }
    let root = root_name(path)?;
    if path.is_file() {
        return Ok(vec![load_module(&[path.to_path_buf()], &root, &root)?]);
    }
    let mut dirs = Vec::new();
    if all {
        module_dirs(path, &mut dirs)?;
    } else if !gm_files(path)?.is_empty() {
        dirs.push(path.to_path_buf());
    }
    if dirs.is_empty() {
        let hint = if all { "" } else { " (`--all` documents the modules below a directory)" };
        return Err(format!("gamma: {} has no `.gm` files{}", path.display(), hint));
    }
    let mut modules = Vec::new();
    for d in dirs {
        let rel: Vec<String> = d
            .strip_prefix(path)
            .unwrap_or(Path::new(""))
            .components()
            .map(|c| c.as_os_str().to_string_lossy().into_owned())
            .collect();
        let (name, out_path) = if rel.is_empty() || !all {
            (root.clone(), root.clone())
        } else {
            (format!("{}/{}", root, rel.join("/")), rel.join("/"))
        };
        modules.push(load_module(&gm_files(&d)?, &name, &out_path)?);
    }
    modules.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(modules)
}

/// The `.gm` files directly in `dir`, in name order.
fn gm_files(dir: &Path) -> Result<Vec<PathBuf>, String> {
    let rd = std::fs::read_dir(dir).map_err(|e| format!("gamma: {}: {}", dir.display(), e))?;
    let mut files: Vec<PathBuf> =
        rd.flatten().map(|e| e.path()).filter(|p| p.is_file() && p.extension().is_some_and(|e| e == "gm")).collect();
    files.sort();
    Ok(files)
}

/// `dir` and the directories below it that hold `.gm` files, in name order.
fn module_dirs(dir: &Path, out: &mut Vec<PathBuf>) -> Result<(), String> {
    if !gm_files(dir)?.is_empty() {
        out.push(dir.to_path_buf());
    }
    let rd = std::fs::read_dir(dir).map_err(|e| format!("gamma: {}: {}", dir.display(), e))?;
    let mut subs: Vec<PathBuf> = rd.flatten().map(|e| e.path()).filter(|p| p.is_dir()).collect();
    subs.sort();
    for s in subs {
        let hidden = s.file_name().is_some_and(|n| n.to_string_lossy().starts_with('.') || n == "target");
        if !hidden {
            module_dirs(&s, out)?;
        }
    }
    Ok(())
}
