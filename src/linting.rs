//! `gamma lint`: the command line of [`gamma::lint`], with `--fix`.

use crate::{Options, new_session, own_files, prepare_packages};
use gamma::diag::DiagFormat;
use gamma::lint::{Config, Level, RULES};
use std::path::Path;
use std::process::ExitCode;

/// The options of `gamma lint`, printed by `gamma help lint`.
pub const USAGE: &str = "\
gamma lint [options] <file or directory>

Checks the program and the other modules of its package, with their tests, and reports
the findings of the lint rules in the package's own files as warnings, each naming its
rule: `warning[unused-variable]: …`. Exits with 1 if the program does not compile or a
rule at the `deny` level found something, else 0.

options:
  --fix               apply the fixes of the warnings that have one (where a
                      program has several, again until none is left), then report
                      what is left; a fix after which the program no longer
                      compiles is undone
  --rules             list the rules and their default levels
  --allow=<rules>     do not report these rules (comma-separated; `all` is every rule)
  --warn=<rules>      report these rules as warnings
  --deny=<rules>      report these rules as errors
  -D, --deny-warnings report every rule that would warn as an error
  --diag-format=human|short|json, --offline, --locked, --target=<triple>
                      as for `gamma check`

The levels of the rules come from their defaults, then the `[lint]` table of
gamma.toml (`unused-parameter = \"allow\"`), then the options above in order.
`#allow(\"rule\", …)` before a top-level declaration turns the rules off inside it.
";

/// A warning, by file, position and rule.
type Key = (std::path::PathBuf, u32, &'static str);

#[derive(Default, Clone)]
pub struct LintOptions {
    fix: bool,
    rules: bool,
    deny_warnings: bool,
    levels: Vec<(String, Level)>,
}

impl LintOptions {
    /// Reads the lint option `arg`; `false` if it is not one.
    pub fn parse_arg(&mut self, arg: &str) -> Result<bool, String> {
        match arg {
            "--fix" => self.fix = true,
            "--rules" => self.rules = true,
            "-D" | "--deny-warnings" => self.deny_warnings = true,
            _ => {
                let Some((opt, list)) = arg.split_once('=') else { return Ok(false) };
                let level = match opt {
                    "--allow" => Level::Allow,
                    "--warn" => Level::Warn,
                    "--deny" => Level::Deny,
                    _ => return Ok(false),
                };
                for name in list.split(',').filter(|n| !n.is_empty()) {
                    if name != "all" && gamma::lint::rule(name).is_none() {
                        return Err(format!("unknown lint rule `{}` (`gamma lint --rules` lists them)", name));
                    }
                    self.levels.push((name.to_string(), level));
                }
            }
        }
        Ok(true)
    }
}

fn level_name(l: Level) -> &'static str {
    match l {
        Level::Allow => "allow",
        Level::Warn => "warn",
        Level::Deny => "deny",
    }
}

/// The levels for linting `path`: the defaults, gamma.toml's `[lint]`, the command line.
fn config(opts: &Options, path: &Path) -> Result<Config, String> {
    let mut config = Config::default();
    if let Some(root) = gamma::pkg::find_root(path) {
        let manifest = gamma::pkg::resolve::read_manifest(&root)?;
        for (name, level) in &manifest.lint {
            config.set(name, *level)?;
        }
    }
    for (name, level) in &opts.lint.levels {
        config.set(name, *level)?;
    }
    if opts.lint.deny_warnings {
        config.deny_warnings();
    }
    Ok(config)
}

pub fn run(opts: &Options) -> ExitCode {
    if opts.lint.rules {
        let width = RULES.iter().map(|r| r.name.len()).max().unwrap_or(0);
        for r in RULES {
            println!("{:width$}  {:5}  {}", r.name, level_name(r.default), r.summary);
        }
        return ExitCode::SUCCESS;
    }
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", crate::USAGE);
        return ExitCode::from(2);
    };
    let fail = |e: String| {
        eprintln!("gamma: {}", e);
        ExitCode::FAILURE
    };
    let scope = match own_files(path) {
        Ok(s) => s,
        Err(e) => return fail(e),
    };
    let config = match config(opts, path) {
        Ok(c) => c,
        Err(e) => return fail(e),
    };
    let std_root = gamma::source::canonical(&gamma::sema::std_root());
    let linting_std = gamma::source::canonical(path).starts_with(&std_root);
    // A file is the package's own if it is in the package's directory, but not in the
    // standard library (unless that is what is linted) or in another package inside it
    // (a path dependency, say).
    let mut owners = std::collections::HashMap::new();
    let mut in_scope = |p: &Path| {
        let p = gamma::source::canonical(p);
        if !p.starts_with(&scope) || (p.starts_with(&std_root) && !linting_std) {
            return false;
        }
        let dir = p.parent().map(Path::to_path_buf).unwrap_or_default();
        let owner = owners.entry(dir.clone()).or_insert_with(|| gamma::pkg::find_root(&dir));
        owner.as_ref().is_none_or(|o| gamma::source::canonical(o) == scope)
    };
    let in_scope = std::cell::RefCell::new(&mut in_scope);
    let in_scope = |p: &Path| (in_scope.borrow_mut())(p);
    let (mut fixes, mut files) = (0, std::collections::BTreeSet::new());
    // The text of the files the last round of fixes changed, to undo it if it breaks
    // the program, and the warnings it fixed.
    let mut undo: Vec<(std::path::PathBuf, String)> = vec![];
    let (mut last, mut last_n): (Vec<Key>, usize) = (vec![], 0);
    // After a round of fixes breaks the program, fixes are applied one at a time, and
    // those that break it are left out.
    let (mut careful, mut blocked, mut refused) = (false, std::collections::HashSet::new(), 0);
    for round in 0.. {
        let (mut sess, _) = new_session(opts);
        if prepare_packages(opts, &mut sess, path).is_none() {
            return ExitCode::FAILURE;
        }
        let outcome = gamma::lint::run(&mut sess, path, &in_scope, &config);
        if outcome.is_none() && !undo.is_empty() {
            for (p, text) in undo.drain(..) {
                if let Err(e) = std::fs::write(&p, text) {
                    return fail(format!("cannot write `{}`: {}", p.display(), e));
                }
            }
            fixes -= last_n;
            if careful {
                refused += 1;
                blocked.extend(last.drain(..));
            }
            careful = true;
            continue;
        }
        let key = |d: &gamma::diag::Diagnostic| -> Key {
            (sess.sources.file(d.span.file).path.clone(), d.span.lo, d.code.unwrap_or_default())
        };
        let rounds = if careful { 1000 } else { 10 };
        let (plan, n) = match &outcome {
            Some(_) if opts.lint.fix && round < rounds => {
                let editable = |f: gamma::source::FileId| in_scope(&sess.sources.file(f).path);
                let mut todo: Vec<_> = sess
                    .diags
                    .iter()
                    .filter(|d| d.code.is_some() && d.fixes.len() == 1 && !blocked.contains(&key(d)))
                    .collect();
                if careful {
                    todo.truncate(1);
                }
                last = todo.iter().map(|d| key(d)).collect();
                gamma::fix::plan(todo, editable)
            }
            _ => Default::default(),
        };
        if n == 0 {
            eprint!("{}", sess.render_diagnostics(opts.format));
            let s = |k: usize| if k == 1 { "" } else { "s" };
            if opts.format != DiagFormat::Json {
                if refused > 0 {
                    eprintln!(
                        "gamma: {} lint fix{} left the program failing to compile, so {} not applied",
                        refused,
                        if refused == 1 { "" } else { "es" },
                        if refused == 1 { "it was" } else { "they were" },
                    );
                }
                if fixes > 0 {
                    let es = if fixes == 1 { "" } else { "es" };
                    eprintln!("gamma: applied {} fix{} in {} file{}", fixes, es, files.len(), s(files.len()));
                }
                if let Some(o) = &outcome
                    && o.warnings + o.errors > 0
                {
                    let mut parts = vec![];
                    if o.errors > 0 {
                        parts.push(format!("{} error{}", o.errors, s(o.errors)));
                    }
                    if o.warnings > 0 {
                        parts.push(format!("{} warning{}", o.warnings, s(o.warnings)));
                    }
                    eprintln!("gamma lint: {}", parts.join(" and "));
                }
            }
            return match outcome {
                Some(o) if o.errors == 0 => ExitCode::SUCCESS,
                _ => ExitCode::FAILURE,
            };
        }
        undo.clear();
        last_n = n;
        for (f, edits) in &plan {
            let file = sess.sources.file(*f);
            let text = gamma::fix::apply(&file.src, edits);
            if let Err(e) = std::fs::write(&file.path, text) {
                return fail(format!("cannot write `{}`: {}", file.path.display(), e));
            }
            undo.push((file.path.clone(), file.src.clone()));
            files.insert(file.path.clone());
        }
        fixes += n;
    }
    unreachable!()
}
