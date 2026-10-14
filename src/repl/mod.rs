//! `gamma repl`: an interactive read-eval-print loop on the interpreter.
//!
//! There is no incremental compilation, so every input is checked and run as a
//! complete program that is synthesized from the session so far:
//!
//! - Top-level declarations (functions, types, effects, imports, constants) are
//!   kept as source text and form the module. A declaration with an existing
//!   name replaces the old one.
//! - Statements run inside a synthesized `main`. Variables cannot outlive a run
//!   of the interpreter, so the statements that define them are replayed, with
//!   their output hidden, before each new input. Only statements that can be
//!   repeated without visible effect are kept: those that need no ambient
//!   effect other than `Alloc`, and variable definitions. See
//!   `docs/guide/01-getting-started.md` for the limits that follow.
//! - A variable defined again gets a fresh internal name, because locals cannot
//!   be shadowed; later inputs are rewritten to use it.

mod input;
mod report;

use crate::diag::DiagFormat;
use crate::driver::Session;
use crate::interp::{self, Mode, Outcome, RunConfig};
use crate::source::FileId;
use crate::tir::Program;
use input::{ItemInfo, StmtInfo};
use std::collections::HashMap;
use std::io::{BufRead, IsTerminal, Write};
use std::path::PathBuf;
use std::process::ExitCode;

pub struct Options {
    /// No banner and no prompts (always the case when stdin is not a terminal).
    pub quiet: bool,
    pub mode: Mode,
    pub format: DiagFormat,
    /// Files whose declarations are loaded before the first prompt.
    pub preload: Vec<PathBuf>,
}

const HELP: &str = "\
Enter an expression to print its value, a statement or a definition to add it to the session.
An input continues on the next line while brackets are open or it is incomplete.

  :help             this text
  :quit             leave (so does end of input)
  :type <expr>      the type of an expression, which is not run unless it must be
  :list             the definitions and variables of the session
  :load <file>      add the declarations of a file
  :reset            forget everything
  :cancel           (on a continuation line) discard the input so far
";

/// A statement kept for replay.
struct Rec {
    /// As the user wrote it.
    shown: String,
    /// As replayed: rewritten for variables that were defined again.
    text: String,
    /// Replaying it repeats effects other than allocation: it defines a variable
    /// from such an expression.
    impure: bool,
}

struct Chunk {
    names: Vec<String>,
    text: String,
}

struct Repl {
    opts: Options,
    chunks: Vec<Chunk>,
    recs: Vec<Rec>,
    /// User variable name -> the name it has in the synthesized `main`.
    vars: HashMap<String, String>,
    fresh: u32,
    root: PathBuf,
}

/// A synthesized program, checked.
struct Compiled {
    sess: Session,
    file: FileId,
    lo: u32,
    input: String,
    prog: Option<Program>,
}

impl Compiled {
    fn diagnostics(&self, format: DiagFormat) -> String {
        report::render_region(&self.sess, self.file, self.lo, &self.input, format)
    }
}

/// How the last input is placed in the synthesized `main`.
struct Tail<'a> {
    prefix: &'a str,
    input: &'a str,
    suffix: &'a str,
}

/// Declared in every synthesized program: names the type an expression gets when
/// its type is only fixed by use (`1 + 2`).
const HELPERS: &str = "__repl_type :: fn(T: type, v: T) -> string => @type_name(T)\n\n";

const ALL_EFFECTS: &str = "Alloc, Out, Err, In, Clock, Rand, Fs, Net, Env, Sys";

impl Repl {
    fn new(opts: Options) -> Repl {
        let dir = std::env::current_dir().unwrap_or_default();
        Repl { opts, chunks: vec![], recs: vec![], vars: HashMap::new(), fresh: 0, root: dir.join("gamma_repl.gm") }
    }

    fn reset(&mut self) {
        self.chunks.clear();
        self.recs.clear();
        self.vars.clear();
    }

    /// Checks the session plus `new_decls` (replacing the chunks that declare
    /// `replaced`), the statements `replay`, and `tail`.
    fn compile(
        &self,
        new_decls: Option<&str>,
        replaced: &[String],
        replay: &[&Rec],
        tail: Option<Tail>,
        effects: &str,
        sentinel: bool,
    ) -> Compiled {
        let mut src = String::from(HELPERS);
        for c in &self.chunks {
            if c.names.iter().any(|n| replaced.contains(n)) {
                continue;
            }
            src.push_str(&c.text);
            src.push_str("\n\n");
        }
        let mut lo = 0;
        let mut input = String::new();
        if let Some(d) = new_decls {
            lo = src.len() as u32;
            input = d.to_string();
            src.push_str(d);
            src.push_str("\n\n");
        }
        src.push_str(&format!("main :: fn() -> AllocError!() \\ {{{}}} {{\n", effects));
        for r in replay {
            src.push_str("    ");
            src.push_str(&r.text);
            src.push('\n');
        }
        if sentinel && !replay.is_empty() {
            src.push_str(&format!("    print(\"{}\")\n    eprint(\"{}\")\n", report::SENTINEL, report::SENTINEL));
        }
        if let Some(t) = tail {
            src.push_str(t.prefix);
            lo = src.len() as u32;
            input = t.input.to_string();
            src.push_str(t.input);
            src.push_str(t.suffix);
        }
        src.push_str("}\n");
        let mut sess = Session::new();
        sess.mode = self.opts.mode;
        sess.overlays.insert(self.root.clone(), src);
        let prog = crate::sema::analyze(&mut sess, &self.root);
        let file = sess.sources.files().find(|(_, f)| f.path == self.root).map_or(FileId(0), |(id, _)| id);
        Compiled { sess, file, lo, input, prog }
    }

    /// Runs a checked program; true unless it trapped. Output of the replayed
    /// part is hidden when `replayed`.
    fn run(&self, c: &Compiled, replayed: bool) -> bool {
        let prog = c.prog.as_ref().expect("a checked program");
        let cfg = RunConfig { target: prog.target.clone(), mode: self.opts.mode, step_budget: None };
        if let Err(why) = interp::foreign_supported(prog, &cfg) {
            eprintln!("error: cannot run this in the REPL: {}", why);
            return false;
        }
        let (mut out, mut err) = (std::io::stdout(), std::io::stderr());
        let outcome = {
            let mut fo = report::Filter::new(&mut out, replayed);
            let mut fe = report::Filter::new(&mut err, replayed);
            let o = interp::run_main(prog, &cfg, &["repl".to_string()], &mut fo, &mut fe);
            let _ = fo.flush();
            let _ = fe.flush();
            o
        };
        match outcome {
            // A non-zero status is `Env.exit` or an error result (reported by the interpreter).
            Outcome::Exit(code) => code == 0,
            Outcome::Trap(t) => {
                eprint!("{}", report::trap_report(&t, &c.sess, c.file, c.lo, &c.input));
                false
            }
        }
    }

    fn submit(&mut self, text: &str) {
        let c = input::classify(text);
        match (&c.items, &c.stmts) {
            (None, None) => eprint!("{}", input::syntax_errors(text, self.opts.format)),
            (Some(items), stmts) => {
                // `n :: 12` is a constant if it can be one, else a local variable.
                let local = stmts.as_ref().filter(|_| items.iter().all(|i| i.plain_const));
                match self.try_decls(text, items) {
                    Ok(()) => {}
                    Err(msg) => match local {
                        Some(s) => {
                            self.statements(text, s);
                        }
                        None => eprint!("{}", msg),
                    },
                }
            }
            (None, Some(s)) => {
                self.statements(text, s);
            }
        }
    }

    /// Adds declarations. On failure the rendered diagnostics are returned.
    fn try_decls(&mut self, text: &str, items: &[ItemInfo]) -> Result<(), String> {
        if items.iter().any(|i| i.names.iter().any(|n| n == "main")) {
            return Err("error: `main` is reserved by the REPL\n".to_string());
        }
        let names: Vec<String> = items.iter().flat_map(|i| i.names.iter().cloned()).collect();
        let replay: Vec<&Rec> = self.recs.iter().collect();
        let c = self.compile(Some(text), &names, &replay, None, ALL_EFFECTS, false);
        if c.prog.is_none() {
            return Err(c.diagnostics(self.opts.format));
        }
        let warnings = c.diagnostics(self.opts.format);
        eprint!("{}", warnings);
        self.chunks.retain(|c| !c.names.iter().any(|n| names.contains(n)));
        for i in items {
            self.chunks.push(Chunk { names: i.names.clone(), text: i.text.clone() });
        }
        Ok(())
    }

    /// Runs statements. Returns whether the input was accepted.
    fn statements(&mut self, text: &str, info: &StmtInfo) -> bool {
        // Variables defined again get fresh names; uses refer to the current ones.
        let mut defs = vec![];
        let mut next_vars = self.vars.clone();
        for (name, lo, hi) in &info.bindings {
            if name == "_" {
                continue;
            }
            let internal = if self.vars.contains_key(name) {
                self.fresh += 1;
                format!("{}__{}", name, self.fresh)
            } else {
                name.clone()
            };
            defs.push((*lo, *hi, internal.clone()));
            next_vars.insert(name.clone(), internal);
        }
        let uses: HashMap<String, String> =
            self.vars.iter().filter(|(k, v)| k != v).map(|(k, v)| (k.clone(), v.clone())).collect();
        let body = if defs.iter().any(|d| d.2 != text[d.0 as usize..d.1 as usize]) || !uses.is_empty() {
            input::rename(text, &defs, &uses)
        } else {
            text.to_string()
        };
        let replay: Vec<&Rec> = self.recs.iter().collect();
        let raw = Tail { prefix: "    ", input: &body, suffix: "\n" };
        let c_raw = self.compile(None, &[], &replay, Some(raw), ALL_EFFECTS, true);
        let (chosen, value) = if c_raw.prog.is_some() {
            (c_raw, false)
        } else if info.single_expr {
            let tail = Tail { prefix: "    __repl_v := ", input: &body, suffix: "\n    print(\"{}\\n\", __repl_v)\n" };
            let c_val = self.compile(None, &[], &replay, Some(tail), ALL_EFFECTS, true);
            if c_val.prog.is_some() {
                (c_val, true)
            } else {
                let unused = c_raw.sess.diags.iter().next().is_some_and(|d| d.message.contains("is unused"));
                let shown = if unused { &c_val } else { &c_raw };
                eprint!("{}", shown.diagnostics(self.opts.format));
                return false;
            }
        } else {
            eprint!("{}", c_raw.diagnostics(self.opts.format));
            return false;
        };
        eprint!("{}", chosen.diagnostics(self.opts.format));
        // What to replay later: the statement itself, or for a printed
        // expression only when it may change variables.
        let kept = if !value {
            Some(body.clone())
        } else if input::may_mutate(&body) {
            Some(format!("_ = {}", body))
        } else {
            None
        };
        let mut impure = false;
        let mut kept_text = None;
        if let Some(k) = kept {
            let pure: Vec<&Rec> = self.recs.iter().filter(|r| !r.impure).collect();
            let tail = Tail { prefix: "    ", input: &k, suffix: "\n" };
            let probe = self.compile(None, &[], &pure, Some(tail), "Alloc", false);
            impure = probe.prog.is_none();
            if !impure || !info.bindings.is_empty() {
                kept_text = Some(k);
            }
        }
        let ok = self.run(&chosen, !self.recs.is_empty());
        if !ok {
            return false;
        }
        if let Some(k) = kept_text {
            if impure {
                let names: Vec<&str> = info.bindings.iter().map(|b| b.0.as_str()).collect();
                eprintln!(
                    "note: {} will be evaluated again before each later input, because its definition has effects",
                    names.join(", ")
                );
            }
            self.recs.push(Rec { shown: text.to_string(), text: k, impure });
        }
        self.vars = next_vars;
        true
    }

    fn type_of(&mut self, expr: &str) {
        let replay: Vec<&Rec> = self.recs.iter().collect();
        let uses: HashMap<String, String> =
            self.vars.iter().filter(|(k, v)| k != v).map(|(k, v)| (k.clone(), v.clone())).collect();
        let expr = if uses.is_empty() { expr.to_string() } else { input::rename(expr, &[], &uses) };
        let direct = Tail { prefix: "    print(\"{}\\n\", @type_name(@type_of(", input: &expr, suffix: ")))\n" };
        let c = self.compile(None, &[], &replay, Some(direct), ALL_EFFECTS, true);
        let c = if c.prog.is_some() {
            c
        } else {
            let bound = Tail { prefix: "    print(\"{}\\n\", __repl_type(", input: &expr, suffix: "))\n" };
            let c2 = self.compile(None, &[], &replay, Some(bound), ALL_EFFECTS, true);
            if c2.prog.is_none() {
                eprint!("{}", c.diagnostics(self.opts.format));
                return;
            }
            c2
        };
        self.run(&c, !self.recs.is_empty());
    }

    fn load(&mut self, path: &str) {
        let path = path.trim();
        let text = match std::fs::read_to_string(path) {
            Ok(t) => t,
            Err(e) => {
                eprintln!("error: cannot read {}: {}", path, e);
                return;
            }
        };
        let c = input::classify(&text);
        let Some(items) = c.items else {
            eprint!("{}", input::syntax_errors(&text, self.opts.format));
            return;
        };
        let (main, rest): (Vec<&ItemInfo>, Vec<&ItemInfo>) =
            items.iter().partition(|i| i.names.iter().any(|n| n == "main"));
        let (text, items): (String, Vec<&ItemInfo>) = if main.is_empty() {
            (text, rest)
        } else {
            eprintln!("note: `main` in {} is not loaded", path);
            (rest.iter().map(|i| i.text.as_str()).collect::<Vec<_>>().join("\n\n"), rest)
        };
        let owned: Vec<ItemInfo> = items
            .iter()
            .map(|i| ItemInfo { names: i.names.clone(), text: i.text.clone(), plain_const: i.plain_const })
            .collect();
        if let Err(msg) = self.try_decls(&text, &owned) {
            eprint!("{}", msg);
        }
    }

    fn list(&self) {
        for c in &self.chunks {
            println!("{}", c.text);
        }
        for r in &self.recs {
            println!("{}", r.shown);
        }
    }

    /// Runs a `:` command; false to leave.
    fn command(&mut self, line: &str) -> bool {
        let (cmd, arg) = match line.split_once(char::is_whitespace) {
            Some((c, a)) => (c, a.trim()),
            None => (line, ""),
        };
        match cmd {
            ":quit" | ":q" | ":exit" => return false,
            ":help" | ":h" | ":?" => print!("{}", HELP),
            ":reset" => self.reset(),
            ":list" => self.list(),
            ":type" | ":t" => {
                if arg.is_empty() {
                    eprintln!("error: `:type` needs an expression");
                } else {
                    self.type_of(arg);
                }
            }
            ":load" => {
                if arg.is_empty() {
                    eprintln!("error: `:load` needs a file");
                } else {
                    self.load(arg);
                }
            }
            ":cancel" => {}
            _ => eprintln!("error: unknown command `{}` (`:help` lists the commands)", cmd),
        }
        true
    }
}

pub fn run(mut opts: Options) -> ExitCode {
    let interactive = std::io::stdin().is_terminal() && std::io::stdout().is_terminal();
    opts.quiet = opts.quiet || !interactive;
    let preload = std::mem::take(&mut opts.preload);
    let quiet = opts.quiet;
    let mut repl = Repl::new(opts);
    for p in preload {
        repl.load(&p.to_string_lossy());
    }
    if !quiet {
        println!("gamma {} repl; :help for commands, :quit to leave", env!("CARGO_PKG_VERSION"));
    }
    let prompt = |s: &str| {
        if !quiet {
            print!("{}", s);
            let _ = std::io::stdout().flush();
        }
    };
    let stdin = std::io::stdin();
    let mut pending = String::new();
    loop {
        prompt(if pending.is_empty() { ">> " } else { ".. " });
        let mut line = String::new();
        match stdin.lock().read_line(&mut line) {
            Ok(0) | Err(_) => {
                if !pending.is_empty() {
                    repl.submit(&pending);
                }
                break;
            }
            Ok(_) => {}
        }
        let line = line.trim_end_matches(['\n', '\r']);
        if pending.is_empty() {
            if line.trim_start().starts_with(':') {
                if !repl.command(line.trim()) {
                    break;
                }
                continue;
            }
            if input::is_blank(line) {
                continue;
            }
        } else if line.trim() == ":cancel" {
            pending.clear();
            continue;
        }
        if !pending.is_empty() {
            pending.push('\n');
        }
        pending.push_str(line);
        let c = input::classify(&pending);
        // A blank line ends an input that only lacks its end, so that its error shows.
        if c.incomplete && !(line.trim().is_empty() && !input::open_brackets(&pending)) {
            continue;
        }
        let text = std::mem::take(&mut pending);
        repl.submit(&text);
    }
    ExitCode::SUCCESS
}
