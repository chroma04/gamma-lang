//! `gamma lint`: warnings about code that compiles but is probably not what was meant,
//! or that has a simpler form.
//!
//! The rules come in two kinds. Those that need names resolved and types known (unused
//! locals and declarations, variables that never change, comparisons with a known
//! result) read the checked program through [`crate::sema::analyze_for_lint`]; the rest
//! walk the syntax tree ([`syntax`]). Each warning names its rule, and the rules' levels
//! come from [`RULES`], the `[lint]` table of `gamma.toml`, the command line, and
//! `#allow("rule")` attributes on declarations, in increasing order of precedence.
//! Warnings with a fix are applied by `gamma lint --fix`.

mod syntax;

use crate::diag::{Diagnostic, Severity};
use crate::driver::Session;
use crate::sema::Finding;
use crate::source::Span;
use crate::symbol::Symbol;
use std::collections::{HashMap, HashSet};
use std::path::Path;

/// How a rule's findings are reported.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Level {
    /// Not reported.
    Allow,
    /// Reported as a warning.
    Warn,
    /// Reported as an error: `gamma lint` fails.
    Deny,
}

impl Level {
    pub fn parse(s: &str) -> Option<Level> {
        match s {
            "allow" => Some(Level::Allow),
            "warn" => Some(Level::Warn),
            "deny" => Some(Level::Deny),
            _ => None,
        }
    }
}

pub struct Rule {
    pub name: &'static str,
    /// The level when nothing configures the rule.
    pub default: Level,
    /// What the rule reports, in a line.
    pub summary: &'static str,
}

const fn rule_def(name: &'static str, summary: &'static str) -> Rule {
    Rule { name, default: Level::Warn, summary }
}

/// Every rule, grouped by kind.
pub const RULES: &[Rule] = &[
    rule_def("unused-variable", "a local variable or pattern binding whose value is never read"),
    rule_def("unused-parameter", "a parameter of a function that its body never uses"),
    rule_def("unused-import", "an import that nothing in its module refers to"),
    rule_def("unused-declaration", "a private top-level function, type, constant or global that nothing refers to"),
    rule_def("unused-label", "a loop or `with` label that no `break` or `continue` names"),
    Rule {
        name: "prefer-immutable",
        default: Level::Allow,
        summary: "a variable declared with `:=` that is never assigned, so `::` would do",
    },
    rule_def("bool-comparison", "a `bool` compared with `true` or `false`"),
    rule_def("self-comparison", "a value compared with itself"),
    rule_def("self-assignment", "a place assigned to itself"),
    rule_def("constant-condition", "an `if` or `while` whose condition is the literal `true` or `false`"),
    rule_def("useless-defer", "a `defer` or `errdefer` that is the last statement of its block"),
    rule_def("needless-bool", "`if c { true } else { false }` and the like, which are `c` or `not c`"),
    rule_def("double-negation", "`not not x`, which is `x`"),
    rule_def("collapsible-else-if", "`else { if … }`, which is `else if …`"),
    rule_def("empty-branch", "an `if` with an empty body and no `else`, or an empty `else`"),
    rule_def("identical-branches", "an `if` whose branches are all the same"),
    rule_def("unnecessary-parens", "parentheses around a whole condition, initializer, assigned or returned value"),
    rule_def(
        "naming",
        "names not written in the usual case: types and variants in UpperCamelCase, the rest in snake_case",
    ),
];

/// The rule named `name`.
pub fn rule(name: &str) -> Option<&'static Rule> {
    RULES.iter().find(|r| r.name == name)
}

/// The level of each rule.
#[derive(Clone, Debug)]
pub struct Config {
    levels: HashMap<&'static str, Level>,
}

impl Default for Config {
    fn default() -> Config {
        Config { levels: RULES.iter().map(|r| (r.name, r.default)).collect() }
    }
}

impl Config {
    /// Sets the level of rule `name`, or of every rule for `all`.
    pub fn set(&mut self, name: &str, level: Level) -> Result<(), String> {
        if name == "all" {
            for l in self.levels.values_mut() {
                *l = level;
            }
            return Ok(());
        }
        let r = rule(name).ok_or_else(|| format!("unknown lint rule `{}` (`gamma lint --rules` lists them)", name))?;
        self.levels.insert(r.name, level);
        Ok(())
    }

    /// Makes every rule that warns an error instead.
    pub fn deny_warnings(&mut self) {
        for l in self.levels.values_mut() {
            if *l == Level::Warn {
                *l = Level::Deny;
            }
        }
    }

    pub fn level(&self, name: &str) -> Level {
        self.levels.get(name).copied().unwrap_or(Level::Allow)
    }
}

/// A warning of rule `code`.
pub(crate) fn lint(span: Span, code: &'static str, message: impl Into<String>) -> Diagnostic {
    Diagnostic::warning(span, message).with_code(code)
}

/// How a local's name is declared, which decides how an unused one can be fixed.
#[derive(Clone, Copy, Debug)]
pub enum Site {
    /// `x := e`, `x :: e`, `x: T = e` or `x: T : e`. `op` is the binding operator.
    /// `keep_mutable`: making it immutable could change what the initializer means (a
    /// literal becomes an untyped constant, an intrinsic may be evaluated at compile time),
    /// so there is no fix; `fn_lit`: the initializer is a function literal, which would
    /// become a nested function.
    Single { var: bool, typed: bool, op: Option<Span>, keep_mutable: bool, fn_lit: bool, uninit: bool },
    /// A name of a destructuring binding: written `_` instead, it discards.
    Replace(Span, &'static str),
    /// A pattern binding, and the edit that discards it, if one can.
    Pattern { fix: Option<(Span, &'static str)> },
}

/// What linting found.
pub struct Outcome {
    /// Warnings and errors reported by the rules (also added to the session's diagnostics).
    pub warnings: usize,
    pub errors: usize,
}

/// Lints the package of the program rooted at `root`: checks it, then adds the findings of
/// the rules in files for which `in_scope` holds to `sess.diags`, at the levels of
/// `config`. Returns `None` if the program does not compile (the errors are in `sess.diags`).
pub fn run(sess: &mut Session, root: &Path, in_scope: &dyn Fn(&Path) -> bool, config: &Config) -> Option<Outcome> {
    let findings = crate::sema::analyze_for_lint(sess, root, in_scope)?;
    let mut diags = vec![];
    let mut sites = HashMap::new();
    let mut allows = vec![];
    let mut when_names: HashSet<Symbol> = HashSet::new();
    let files: Vec<_> = sess
        .sources
        .files()
        .filter(|(_, f)| !f.path.to_string_lossy().starts_with('<') && in_scope(&f.path))
        .map(|(id, f)| (id, f.src.clone()))
        .collect();
    for (id, src) in files {
        let mut scratch = crate::diag::Diagnostics::new();
        let lexed = crate::syntax::lexer::lex(id, &src, &mut scratch);
        let mut ids = crate::syntax::ast::NodeIdGen::new();
        let file = crate::syntax::parser::parse_file(id, &lexed, &mut ids, &mut scratch);
        if scratch.has_errors() {
            continue;
        }
        let mut walk = syntax::Walk::new(&sess.sources);
        walk.file(&file);
        let facts = walk.facts;
        syntax::names_in(&lexed.tokens, &facts.whens, &mut when_names);
        diags.extend(facts.diags);
        sites.extend(facts.sites);
        allows.extend(facts.allows);
    }
    for f in findings {
        if let Some(d) = finding_diag(sess, &f, &sites, &when_names) {
            diags.push(d);
        }
    }
    let mut out = Outcome { warnings: 0, errors: 0 };
    for mut d in diags {
        let code = d.code.unwrap_or_default();
        let allowed = allows.iter().any(|(item, rules): &(Span, Vec<String>)| {
            item.file == d.span.file
                && item.lo <= d.span.lo
                && d.span.hi <= item.hi
                && rules.iter().any(|r| r == code || r == "all")
        });
        match config.level(code) {
            _ if allowed => continue,
            Level::Allow => continue,
            Level::Warn => out.warnings += 1,
            Level::Deny => {
                d.severity = Severity::Error;
                out.errors += 1;
            }
        }
        sess.diags.push(d);
    }
    Some(out)
}

/// The diagnostic for a finding of the checker, or `None` if it is not to be reported:
/// a name also written in a `when` branch may be used in the branch not taken, which
/// is not checked.
fn finding_diag(
    sess: &Session,
    f: &Finding,
    sites: &HashMap<Span, Site>,
    when_names: &HashSet<Symbol>,
) -> Option<Diagnostic> {
    let text = |span: Span| sess.sources.text(span);
    Some(match *f {
        Finding::UnusedLocal { name, span, param: true, .. } => {
            if when_names.contains(&name) {
                return None;
            }
            lint(span, "unused-parameter", format!("the parameter `{}` is never used", name))
                .with_note(format!("if the function must take it, name it `_{}`", name))
        }
        Finding::UnusedLocal { name, span, param: false, assigned } => {
            if when_names.contains(&name) {
                return None;
            }
            let msg = if assigned {
                format!("the variable `{}` is assigned but never read", name)
            } else {
                format!("the variable `{}` is never used", name)
            };
            let d = lint(span, "unused-variable", msg);
            match sites.get(&span) {
                Some(Site::Single { typed: false, op: Some(op), .. }) if !assigned => {
                    d.with_fix("discard the value with `_ =`", Span::new(span.file, span.lo, op.hi), "_ =")
                }
                Some(Site::Replace(at, with)) if !assigned => d.with_fix(format!("write `{}`", with), *at, *with),
                Some(Site::Pattern { fix: Some((at, with)) }) => {
                    let what = if with.is_empty() { "remove the capture" } else { "discard the value" };
                    d.with_fix(what, *at, *with)
                }
                _ => d.with_note(format!("if this is deliberate, name it `_{}`", name)),
            }
        }
        Finding::NeverAssigned { name, span } => {
            if when_names.contains(&name) {
                return None;
            }
            let Some(&Site::Single { var: true, typed, op, keep_mutable, fn_lit: false, uninit: false }) =
                sites.get(&span)
            else {
                return None;
            };
            let d = lint(
                span,
                "prefer-immutable",
                format!(
                    "the variable `{}` is never assigned: declare it with `{}`",
                    name,
                    if typed { ":" } else { "::" }
                ),
            );
            match op {
                Some(op) if !keep_mutable => {
                    d.with_fix("make the binding immutable", op, if typed { ":" } else { "::" })
                }
                _ => d,
            }
        }
        Finding::UnusedDecl { name, span, item, what } => {
            if when_names.contains(&name) {
                return None;
            }
            if what == "import" {
                let d = lint(span, "unused-import", format!("the import `{}` is never used", name));
                match whole_lines(sess, item) {
                    Some(lines) => d.with_fix("remove the import", lines, ""),
                    None => d,
                }
            } else {
                lint(span, "unused-declaration", format!("the {} `{}` is never used", what, name))
                    .with_note("it is private to its module: mark it `pub` if other modules are to use it")
            }
        }
        Finding::BoolComparison { span, operand, same } => {
            let whole = text(span);
            if !(whole.contains("true") || whole.contains("false")) {
                return None;
            }
            let o = text(operand);
            let replacement = if same {
                o.to_string()
            } else if o.contains(" and ") || o.contains(" or ") || o.contains(" else ") {
                format!("not ({})", o)
            } else {
                format!("not {}", o)
            };
            lint(span, "bool-comparison", "comparing a `bool` with `true` or `false` is redundant").with_fix(
                format!("write `{}`", replacement),
                span,
                replacement.clone(),
            )
        }
        Finding::SelfComparison { span, always } => {
            lint(span, "self-comparison", format!("this compares a value with itself, so it is always `{}`", always))
        }
        Finding::SelfAssignment { span } => lint(span, "self-assignment", "this assigns a place to itself"),
    })
}

/// `span` widened to the whole lines it is on, with the line break after them, if
/// nothing else is on those lines.
fn whole_lines(sess: &Session, span: Span) -> Option<Span> {
    let src = &sess.sources.file(span.file).src;
    let start = src[..span.lo as usize].rfind('\n').map_or(0, |i| i + 1);
    let end = src[span.hi as usize..].find('\n').map_or(src.len(), |i| span.hi as usize + i + 1);
    let before = &src[start..span.lo as usize];
    let after = &src[span.hi as usize..end];
    if !before.trim().is_empty() || !(after.trim().is_empty() || after.trim_start().starts_with("//")) {
        return None;
    }
    // Removing the only line between a blank line (or the start of the file) and
    // another blank line would leave two blank lines; take one with it.
    let mut end = end;
    if (start == 0 || src[..start].ends_with("\n\n")) && src[end..].starts_with('\n') {
        end += 1;
    }
    Some(Span::new(span.file, start as u32, end as u32))
}
