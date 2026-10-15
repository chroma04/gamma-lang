//! The package commands of the `gamma` tool: `fetch`, `update`, `add`, `remove`, `new`,
//! `init` and `cache`. They take their own arguments, so `main` hands them over before it
//! parses the options of the compiler commands.

use super::cache;
use super::edit::{self, NewDep};
use super::manifest::{Manifest, name_problem};
use super::{Options, Refresh, find_root, resolve, scaffold, sync};
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

pub const USAGE: &str = "\
package commands:
  new     create a package in a new directory: gamma new <dir> [--name=<name>] [--vcs=none]
  init    create a package in an existing directory: gamma init [dir] [--name=<name>] [--vcs=none]
  fetch   resolve and download the git and archive dependencies, and write gamma.lock:
          gamma fetch [dir] [--offline] [--locked]
  update  move git dependencies to the newest revisions their requirements allow:
          gamma update [name...] [--dir <dir>]
  add     add a dependency to gamma.toml and fetch it:
          gamma add <name> --path <dir> [--dir <dir>]
          gamma add <name> --git <url> [--tag <tag> | --branch <branch> | --rev <rev> | --version <req>]
                    [--subdir <path>] [--dir <dir>]
          gamma add <name> --url <url.tar.gz> [--sha256 <hex>] [--subdir <path>] [--dir <dir>]
  remove  remove a dependency: gamma remove <name> [--dir <dir>]
  cache   the cache of fetched packages and native builds:
          gamma cache dir                     print the cache directory
          gamma cache clean [--older-than=<days>] [--all] [--dry-run]
                    remove package checkouts and builds unused for <days> (default 30),
                    and mirrors no checkout needs; --all removes everything

build settings (`[build]`, `[profile.<name>]`) are described in docs/guide/15-packages.md;
`--profile=<name>` selects a profile in check, run, build, test and bench.
";

/// Whether `command` is one of the package commands.
pub fn is_package_command(command: &str) -> bool {
    matches!(command, "fetch" | "update" | "add" | "remove" | "new" | "init" | "cache")
}

struct Parsed {
    pos: Vec<String>,
    values: HashMap<String, String>,
    flags: HashSet<String>,
}

fn parse(args: &[String], value_opts: &[&str], flag_opts: &[&str]) -> Result<Parsed, String> {
    let mut p = Parsed { pos: vec![], values: HashMap::new(), flags: HashSet::new() };
    let mut it = args.iter();
    while let Some(a) = it.next() {
        if let Some(opt) = a.strip_prefix("--") {
            let (name, inline) = match opt.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (opt, None),
            };
            if flag_opts.contains(&name) && inline.is_none() {
                p.flags.insert(name.to_string());
            } else if value_opts.contains(&name) {
                let v = match inline {
                    Some(v) => v,
                    None => it.next().cloned().ok_or_else(|| format!("`--{}` needs a value", name))?,
                };
                if p.values.insert(name.to_string(), v).is_some() {
                    return Err(format!("`--{}` given twice", name));
                }
            } else {
                return Err(format!("unknown option `{}`", a));
            }
        } else if a.starts_with('-') && a.len() > 1 {
            return Err(format!("unknown option `{}`", a));
        } else {
            p.pos.push(a.clone());
        }
    }
    Ok(p)
}

fn root_of(dir: &Path) -> Result<PathBuf, String> {
    if !dir.exists() {
        return Err(format!("{} does not exist", dir.display()));
    }
    find_root(dir).ok_or_else(|| {
        format!("no gamma.toml found in {} or any parent directory (`gamma init` creates one)", dir.display())
    })
}

/// Runs the package command `command` with `args`.
pub fn run(command: &str, args: &[String]) -> ExitCode {
    let result = match command {
        "fetch" => fetch(args),
        "update" => update(args),
        "add" => add(args),
        "remove" => remove(args),
        "new" => new_package(args, true),
        "init" => new_package(args, false),
        "cache" => cache_command(args),
        _ => Err(Failure::Usage(format!("unknown command `{}`", command))),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(Failure::Usage(e)) => {
            eprintln!("gamma: {}\n\n{}", e, USAGE);
            ExitCode::from(2)
        }
        Err(Failure::Error(e)) => {
            eprintln!("gamma: {}", e);
            ExitCode::FAILURE
        }
    }
}

enum Failure {
    Usage(String),
    Error(String),
}

impl From<String> for Failure {
    fn from(e: String) -> Failure {
        Failure::Error(e)
    }
}

fn usage<T>(parsed: Result<T, String>) -> Result<T, Failure> {
    parsed.map_err(Failure::Usage)
}

fn fetch(args: &[String]) -> Result<(), Failure> {
    let p = usage(parse(args, &[], &["offline", "locked"]))?;
    if p.pos.len() > 1 {
        return Err(Failure::Usage("more than one directory given".into()));
    }
    let dir = PathBuf::from(p.pos.first().map_or(".", |s| s.as_str()));
    let root = root_of(&dir)?;
    let opts = Options { offline: p.flags.contains("offline"), locked: p.flags.contains("locked") };
    sync(&root, &opts, &Refresh::None, true)?;
    Ok(())
}

fn update(args: &[String]) -> Result<(), Failure> {
    let p = usage(parse(args, &["dir"], &[]))?;
    let root = root_of(Path::new(p.values.get("dir").map_or(".", |s| s.as_str())))?;
    let refresh = if p.pos.is_empty() { Refresh::All } else { Refresh::Names(p.pos.clone()) };
    sync(&root, &Options::default(), &refresh, true)?;
    Ok(())
}

/// `target` written relative to the directory `from` (both canonical).
fn relative_path(from: &Path, target: &Path) -> String {
    let (f, t): (Vec<_>, Vec<_>) = (from.components().collect(), target.components().collect());
    let common = f.iter().zip(&t).take_while(|(a, b)| a == b).count();
    let mut parts: Vec<String> = vec!["..".into(); f.len() - common];
    parts.extend(t[common..].iter().map(|c| c.as_os_str().to_string_lossy().into_owned()));
    if parts.is_empty() { ".".into() } else { parts.join("/") }
}

fn add(args: &[String]) -> Result<(), Failure> {
    let p =
        usage(parse(args, &["path", "git", "url", "sha256", "tag", "branch", "rev", "version", "subdir", "dir"], &[]))?;
    let [name] = p.pos.as_slice() else {
        return Err(Failure::Usage("`add` needs exactly one dependency name".into()));
    };
    if let Some(problem) = name_problem(name, "dependency") {
        return Err(Failure::Error(problem));
    }
    let root = root_of(Path::new(p.values.get("dir").map_or(".", |s| s.as_str())))?;
    let refs: Vec<&str> =
        ["tag", "branch", "rev", "version"].into_iter().filter(|k| p.values.contains_key(*k)).collect();
    let sources = ["path", "git", "url"].into_iter().filter(|k| p.values.contains_key(*k)).count();
    if sources > 1 {
        return Err(Failure::Usage("use only one of `--path`, `--git` and `--url`".into()));
    }
    if p.values.contains_key("sha256") && !p.values.contains_key("url") {
        return Err(Failure::Usage("`--sha256` needs `--url`".into()));
    }
    let dep = match (p.values.get("path"), p.values.get("git")) {
        _ if p.values.contains_key("url") => {
            if !refs.is_empty() {
                return Err(Failure::Usage("`--tag`, `--branch`, `--rev` and `--version` need `--git`".into()));
            }
            NewDep::Url {
                url: p.values["url"].clone(),
                sha256: p.values.get("sha256").cloned(),
                subdir: p.values.get("subdir").cloned(),
            }
        }
        (Some(_), Some(_)) => unreachable!(),
        (None, None) => {
            return Err(Failure::Usage("`add` needs `--path <dir>`, `--git <url>` or `--url <url>`".into()));
        }
        (Some(path), None) => {
            if !refs.is_empty() || p.values.contains_key("subdir") {
                return Err(Failure::Usage(
                    "`--tag`, `--branch`, `--rev`, `--version` and `--subdir` need `--git`".into(),
                ));
            }
            let abs = crate::source::canonicalize(Path::new(path))
                .map_err(|e| Failure::Error(format!("cannot use the path {}: {}", path, e)))?;
            NewDep::Path(relative_path(&root, &abs))
        }
        (None, Some(url)) => {
            if refs.len() > 1 {
                return Err(Failure::Usage("use only one of `--tag`, `--branch`, `--rev`, `--version`".into()));
            }
            let reference = match refs.first() {
                Some(&k) => {
                    let v = p.values[k].clone();
                    if k == "version" {
                        semver::VersionReq::parse(v.trim())
                            .map_err(|e| Failure::Error(format!("invalid version requirement `{}`: {}", v, e)))?;
                    }
                    Some((static_key(k), v))
                }
                None => newest_version(url)?.map(|v| ("version", format!("^{}", v))),
            };
            NewDep::Git { url: url.clone(), reference, subdir: p.values.get("subdir").cloned() }
        }
    };
    let manifest_path = root.join("gamma.toml");
    let old_text = std::fs::read_to_string(&manifest_path)
        .map_err(|e| Failure::Error(format!("cannot read {}: {}", manifest_path.display(), e)))?;
    Manifest::parse(&old_text)
        .map_err(|e| Failure::Error(format!("{}:{}: {}", manifest_path.display(), e.line, e.msg)))?;
    let new_text = edit::add_dependency(&old_text, name, &dep)?;
    // The manifest is checked as it would be written, and put back if resolving fails.
    Manifest::parse(&new_text)
        .map_err(|e| Failure::Error(format!("{}:{}: {}", manifest_path.display(), e.line, e.msg)))?;
    write(&manifest_path, &new_text)?;
    if let Err(e) = sync(&root, &Options::default(), &Refresh::None, true) {
        write(&manifest_path, &old_text)?;
        return Err(Failure::Error(e));
    }
    eprintln!("      Adding {} to dependencies", name);
    Ok(())
}

fn static_key(k: &str) -> &'static str {
    match k {
        "tag" => "tag",
        "branch" => "branch",
        "rev" => "rev",
        _ => "version",
    }
}

/// The newest release tag of the repository, if it has version tags.
fn newest_version(url: &str) -> Result<Option<semver::Version>, String> {
    Ok(super::git::remote_tags(url)?.iter().filter_map(|t| resolve::tag_version(t)).filter(|v| v.pre.is_empty()).max())
}

fn write(path: &Path, text: &str) -> Result<(), Failure> {
    super::write_atomic(path, text).map_err(Failure::Error)
}

fn remove(args: &[String]) -> Result<(), Failure> {
    let p = usage(parse(args, &["dir"], &[]))?;
    let [name] = p.pos.as_slice() else {
        return Err(Failure::Usage("`remove` needs exactly one dependency name".into()));
    };
    let root = root_of(Path::new(p.values.get("dir").map_or(".", |s| s.as_str())))?;
    let manifest_path = root.join("gamma.toml");
    let old_text = std::fs::read_to_string(&manifest_path)
        .map_err(|e| Failure::Error(format!("cannot read {}: {}", manifest_path.display(), e)))?;
    let new_text = edit::remove_dependency(&old_text, name)?;
    write(&manifest_path, &new_text)?;
    if let Err(e) = sync(&root, &Options::default(), &Refresh::None, true) {
        write(&manifest_path, &old_text)?;
        return Err(Failure::Error(e));
    }
    eprintln!("    Removing {} from dependencies", name);
    Ok(())
}

/// `gamma cache dir` and `gamma cache clean`.
fn cache_command(args: &[String]) -> Result<(), Failure> {
    let Some((sub, rest)) = args.split_first() else {
        return Err(Failure::Usage("`cache` needs a subcommand: `dir` or `clean`".into()));
    };
    let dir = crate::cgen::cache_dir();
    match sub.as_str() {
        "dir" => {
            usage(parse(rest, &[], &[]))?;
            let dir = dir.ok_or_else(|| Failure::Error("the cache is disabled (GAMMA_CACHE_DIR is empty)".into()))?;
            println!("{}", dir.display());
            Ok(())
        }
        "clean" => {
            let p = usage(parse(rest, &["older-than"], &["all", "dry-run"]))?;
            if !p.pos.is_empty() {
                return Err(Failure::Usage("`cache clean` takes no arguments".into()));
            }
            let days = match p.values.get("older-than") {
                None => 30,
                Some(v) => v
                    .parse::<u64>()
                    .map_err(|_| Failure::Usage(format!("`--older-than` needs a number of days, not `{}`", v)))?,
            };
            if p.flags.contains("all") && p.values.contains_key("older-than") {
                return Err(Failure::Usage("`--all` and `--older-than` cannot be combined".into()));
            }
            let dir = dir.ok_or_else(|| Failure::Error("the cache is disabled (GAMMA_CACHE_DIR is empty)".into()))?;
            let policy = cache::Policy {
                older_than: std::time::Duration::from_secs(days.saturating_mul(86400)),
                all: p.flags.contains("all"),
                dry_run: p.flags.contains("dry-run"),
            };
            let report = cache::clean(&dir, &policy)?;
            println!("{} in {}", report.summary(policy.dry_run), dir.display());
            Ok(())
        }
        other => Err(Failure::Usage(format!("unknown `cache` subcommand `{}`: use `dir` or `clean`", other))),
    }
}

fn new_package(args: &[String], create: bool) -> Result<(), Failure> {
    let p = usage(parse(args, &["name", "vcs"], &[]))?;
    let dir = match (p.pos.as_slice(), create) {
        ([d], _) => PathBuf::from(d),
        ([], false) => PathBuf::from("."),
        ([], true) => return Err(Failure::Usage("`new` needs a directory".into())),
        _ => return Err(Failure::Usage("more than one directory given".into())),
    };
    let git = match p.values.get("vcs").map(String::as_str) {
        None | Some("git") => true,
        Some("none") => false,
        Some(other) => return Err(Failure::Usage(format!("unknown --vcs `{}`: use `git` or `none`", other))),
    };
    if create {
        match std::fs::read_dir(&dir) {
            Ok(mut rd) => {
                if rd.next().is_some() {
                    return Err(Failure::Error(format!("the directory {} exists and is not empty", dir.display())));
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                std::fs::create_dir_all(&dir)
                    .map_err(|e| Failure::Error(format!("cannot create {}: {}", dir.display(), e)))?;
            }
            Err(e) => return Err(Failure::Error(format!("cannot use {}: {}", dir.display(), e))),
        }
    } else if !dir.is_dir() {
        return Err(Failure::Error(format!("{} is not a directory (`gamma new` creates one)", dir.display())));
    }
    let mut derived_from = None;
    let name = match p.values.get("name") {
        Some(n) => match name_problem(n, "package") {
            None => n.clone(),
            Some(problem) => return Err(Failure::Error(problem)),
        },
        None => {
            let abs = crate::source::canonicalize(&dir)
                .map_err(|e| Failure::Error(format!("cannot use {}: {}", dir.display(), e)))?;
            let dir_name = abs.file_name().map_or(String::new(), |s| s.to_string_lossy().into_owned());
            let name = scaffold::sanitize_name(&dir_name);
            if let Some(problem) = name_problem(&name, "package") {
                return Err(Failure::Error(format!(
                    "the directory name `{dir_name}` gives the package name `{name}`: {problem}; choose one with `--name`"
                )));
            }
            if name != dir_name {
                derived_from = Some(dir_name);
            }
            name
        }
    };
    let report = scaffold::scaffold(&dir, &name, git)?;
    for f in &report.skipped {
        println!("     Skipped {} (already exists)", f);
    }
    if report.created.is_empty() {
        println!("     Nothing to create in {}", dir.display());
        return Ok(());
    }
    if report.skipped.iter().any(|f| f == "gamma.toml") {
        println!(
            "     Created {} in {} (the existing gamma.toml is unchanged)",
            report.created.join(", "),
            dir.display()
        );
    } else {
        println!("     Created package `{}` in {}", name, dir.display());
    }
    if let Some(dir_name) = derived_from {
        println!("        Note: `{dir_name}` is not a valid package name; use `--name` to choose another");
    }
    if report.git_initialized {
        println!("     Initialized a git repository");
    }
    println!("\nNext steps:\n    gamma run {d}\n    gamma test {d}", d = dir.display());
    Ok(())
}
