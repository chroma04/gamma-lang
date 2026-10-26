//! `gamma test` and `gamma bench`: running the `test` and `bench` declarations of a
//! program (§3.8, §3.9), in the interpreter or natively, in order or in parallel, with
//! timing and coverage when asked for.

use crate::{
    COMPILER_STACK, Options, USAGE, c_options, cgen_error, native_dir, native_exe_name, native_link, native_stderr,
    new_session, prepare_packages, runner_command, trap_report,
};
use gamma::driver::Session;
use gamma::tir::{Program, TestCase};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::{Duration, Instant};

/// What is being run.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Test,
    Bench,
}

/// The first words of the line a benchmark prints for `gamma bench`.
const BENCH_MARKER: &str = "gamma-bench: ";

/// The result of running one test or benchmark.
struct Finished {
    /// `None` if it passed; else what it wrote and why it failed.
    failure: Option<String>,
    /// What it wrote to its standard output and error.
    output: String,
    elapsed: Duration,
}

impl Finished {
    fn failed(detail: String) -> Finished {
        Finished { failure: Some(detail.clone()), output: detail, elapsed: Duration::ZERO }
    }
}

/// Counts and prints the results, in the format shared by both back ends.
struct Report {
    kind: Kind,
    passed: usize,
    failed: usize,
    /// The number of cases `--filter` left out, if it was given.
    filtered_out: Option<usize>,
    show_output: bool,
    time: bool,
    started: Instant,
}

impl Report {
    /// Prints the line for `t`; below it, a failure's detail (its output and the reason)
    /// and, with `--show-output`, the output of a case that passed, indented.
    fn record(&mut self, t: &TestCase, f: Finished) {
        let word = if self.kind == Kind::Test { "test" } else { "bench" };
        let name = match &t.module {
            Some(m) => format!("\"{}\" in {}", t.name, m),
            None => format!("\"{}\"", t.name),
        };
        let took = if self.time && self.kind == Kind::Test {
            format!(" ({})", fmt_duration(f.elapsed))
        } else {
            String::new()
        };
        let mut shown = String::new();
        match f.failure {
            None => {
                self.passed += 1;
                let mut output = f.output.as_str();
                let result = match self.kind {
                    Kind::Test => "ok".to_string(),
                    Kind::Bench => match summarize(output) {
                        Some((line, rest)) => {
                            output = rest;
                            line
                        }
                        None => "ok".to_string(),
                    },
                };
                println!("{} {} ... {}{}", word, name, result, took);
                if self.show_output {
                    shown = output.to_string();
                }
            }
            Some(detail) => {
                self.failed += 1;
                println!("{} {} ... FAILED{}", word, name, took);
                shown = detail;
            }
        }
        for line in shown.lines() {
            println!("    {}", line);
        }
    }

    fn finish(&self) -> ExitCode {
        let mut line = match (self.kind, self.filtered_out) {
            (Kind::Test, Some(n)) => format!("{} passed, {} failed, {} filtered out", self.passed, self.failed, n),
            (Kind::Test, None) => format!("{} passed, {} failed", self.passed, self.failed),
            (Kind::Bench, Some(n)) => format!("{} run, {} failed, {} filtered out", self.passed, self.failed, n),
            (Kind::Bench, None) => format!("{} run, {} failed", self.passed, self.failed),
        };
        if self.time {
            line.push_str(&format!(" (finished in {})", fmt_duration(self.started.elapsed())));
        }
        println!("{}", line);
        if self.failed == 0 { ExitCode::SUCCESS } else { ExitCode::FAILURE }
    }
}

/// A duration for a report: `850µs`, `12.3ms`, `1.52s`.
fn fmt_duration(d: Duration) -> String {
    let us = d.as_secs_f64() * 1e6;
    if us < 1000.0 {
        format!("{}µs", us.round() as u64)
    } else if us < 1e6 {
        format!("{:.1}ms", us / 1e3)
    } else {
        format!("{:.2}s", us / 1e6)
    }
}

/// A time per run in nanoseconds, with a unit that keeps the number short.
fn fmt_per_run(ns: f64) -> String {
    if ns < 1e3 {
        format!("{:.2} ns/run", ns)
    } else if ns < 1e6 {
        format!("{:.3} µs/run", ns / 1e3)
    } else if ns < 1e9 {
        format!("{:.3} ms/run", ns / 1e6)
    } else {
        format!("{:.3} s/run", ns / 1e9)
    }
}

/// The summary of what a benchmark measured, and the rest of its output: the last line that
/// starts with [`BENCH_MARKER`] holds the runs per sample and the nanoseconds each sample took.
fn summarize(output: &str) -> Option<(String, &str)> {
    let at = output.rfind(BENCH_MARKER)?;
    if at != 0 && output.as_bytes()[at - 1] != b'\n' {
        return None;
    }
    let line = output[at..].lines().next()?;
    let mut nums = line[BENCH_MARKER.len()..].split_whitespace().map(|w| w.parse::<f64>());
    let iters = nums.next()?.ok()?;
    let samples: Vec<f64> = nums.collect::<Result<_, _>>().ok()?;
    if samples.is_empty() || iters <= 0.0 {
        return None;
    }
    let per_run: Vec<f64> = samples.iter().map(|s| s / iters).collect();
    let n = per_run.len() as f64;
    let mean = per_run.iter().sum::<f64>() / n;
    let var = per_run.iter().map(|x| (x - mean) * (x - mean)).sum::<f64>() / n;
    let rel = if mean > 0.0 { var.sqrt() / mean * 100.0 } else { 0.0 };
    let summary = format!("{} (± {:.1}%, {} samples of {} runs)", fmt_per_run(mean), rel, per_run.len(), iters as u64);
    let rest = &output[at + line.len()..];
    // What the benchmark itself wrote precedes the line; the line ends the output.
    let before = &output[..at];
    Some((summary, if before.is_empty() { rest.trim_start_matches('\n') } else { before }))
}

/// Whether `t` is selected by `--filter`: its name or module path contains the filter text.
fn selected(t: &TestCase, filter: Option<&str>) -> bool {
    filter.is_none_or(|f| t.name.contains(f) || t.module.as_deref().is_some_and(|m| m.contains(f)))
}

/// Runs `n` jobs `0..n` and hands each result to `each`, in the order of the jobs, as soon as
/// it and all before it are done. With `jobs > 1`, that many threads take jobs in turn; each
/// builds its own state with `init` (the first job's thread may use `pre` in the sequential
/// case), because the state of the interpreter is not shared between threads. A thread whose
/// `init` fails serves no jobs, and `lost` gives the result of a job that nobody ran.
fn pool<S, T: Send>(
    n: usize,
    jobs: usize,
    pre: Option<&S>,
    init: &(dyn Fn() -> Option<S> + Sync),
    work: &(dyn Fn(&S, usize) -> T + Sync),
    lost: &dyn Fn(usize) -> T,
    mut each: impl FnMut(usize, T),
) {
    if jobs <= 1 || n <= 1 {
        let built;
        let state = match pre {
            Some(s) => Some(s),
            None => {
                built = init();
                built.as_ref()
            }
        };
        for i in 0..n {
            match state {
                Some(s) => each(i, work(s, i)),
                None => each(i, lost(i)),
            }
        }
        return;
    }
    let next = AtomicUsize::new(0);
    let (tx, rx) = std::sync::mpsc::channel::<(usize, T)>();
    std::thread::scope(|scope| {
        for _ in 0..jobs.min(n) {
            let tx = tx.clone();
            let next = &next;
            std::thread::Builder::new()
                .stack_size(COMPILER_STACK)
                .spawn_scoped(scope, move || {
                    let Some(state) = init() else { return };
                    loop {
                        let i = next.fetch_add(1, Ordering::Relaxed);
                        if i >= n {
                            return;
                        }
                        if tx.send((i, work(&state, i))).is_err() {
                            return;
                        }
                    }
                })
                .expect("cannot start a test thread");
        }
        drop(tx);
        let mut waiting: BTreeMap<usize, T> = BTreeMap::new();
        let mut upto = 0;
        while upto < n {
            match rx.recv() {
                Ok((i, t)) => {
                    waiting.insert(i, t);
                }
                // Every thread is gone: what is left was never run.
                Err(_) => {
                    for i in upto..n {
                        let t = waiting.remove(&i).unwrap_or_else(|| lost(i));
                        each(i, t);
                    }
                    return;
                }
            }
            while let Some(t) = waiting.remove(&upto) {
                each(upto, t);
                upto += 1;
            }
        }
    });
}

/// The state of an interpreter worker: its own analysis of the program.
struct Interp {
    sess: Session,
    prog: Program,
    cfg: gamma::interp::RunConfig,
    /// `--timeout`: how long one test may run.
    timeout: Option<Duration>,
}

impl Interp {
    /// Analyzes the program in `path` as the main thread did (the same mode, target and
    /// packages), and instruments it for coverage if `scope` is given.
    fn build(opts: &Options, proto: &Proto, path: &Path, scope: Option<&Scope>) -> Option<Interp> {
        let mut sess = Session::new();
        sess.mode = proto.mode;
        sess.debug_info = proto.debug_info;
        sess.target = proto.target.clone();
        sess.packages = proto.packages.clone();
        let mut prog = gamma::sema::analyze_package_tests(&mut sess, path, opts.all)?;
        if let Some(scope) = scope {
            let map = gamma::coverage::instrument(&mut prog, &sess.sources, &|p| scope.contains(p));
            gamma::interp::coverage_init(map.points.len());
        }
        let cfg = gamma::interp::RunConfig {
            target: prog.target.clone(),
            mode: opts.mode,
            step_budget: None,
            deadline: None,
        };
        Some(Interp { sess, prog, cfg, timeout: opts.timeout })
    }

    fn cases(&self, kind: Kind) -> &[TestCase] {
        if kind == Kind::Test { &self.prog.tests } else { &self.prog.benches }
    }

    fn run(&self, t: &TestCase) -> Finished {
        let (mut out, mut err) = (Vec::new(), Vec::new());
        let started = Instant::now();
        let cfg = gamma::interp::RunConfig { deadline: self.timeout.map(|t| started + t), ..self.cfg.clone() };
        let outcome = gamma::interp::run_entry(&self.prog, &cfg, t.entry, &[], &mut out, &mut err);
        let elapsed = started.elapsed();
        let reason = match outcome {
            gamma::interp::Outcome::Exit(0) => None,
            gamma::interp::Outcome::Exit(_) => Some(String::new()),
            gamma::interp::Outcome::Trap(trap) if trap.message == gamma::interp::DEADLINE_TRAP => {
                Some(timed_out(self.timeout))
            }
            gamma::interp::Outcome::Trap(trap) => Some(trap_report(&self.sess, &self.prog, &trap)),
        };
        let mut output = String::from_utf8_lossy(&out).into_owned();
        output.push_str(&String::from_utf8_lossy(&err));
        let failure = reason.map(|reason| format!("{}{}", output, reason));
        Finished { failure, output, elapsed }
    }
}

/// The failure detail of a test that ran out of time.
fn timed_out(limit: Option<Duration>) -> String {
    format!("timed out after {} s\n", limit.map_or(0.0, |d| d.as_secs_f64()))
}

/// The files that coverage counts: those under the package root, but not the standard library.
struct Scope {
    root: PathBuf,
    std: PathBuf,
}

impl Scope {
    fn new(input: &Path) -> Scope {
        let root = package_root(input);
        let std = gamma::sema::std_root();
        Scope { root, std: gamma::source::canonicalize(&std).unwrap_or(std) }
    }

    fn contains(&self, p: &Path) -> bool {
        let p = gamma::source::canonicalize(p).unwrap_or_else(|_| p.to_path_buf());
        p.starts_with(&self.root) && !p.starts_with(&self.std)
    }
}

/// The root directory of the package that holds `input`: the nearest directory with a
/// `gamma.toml`, or else the directory of the input itself.
fn package_root(input: &Path) -> PathBuf {
    let canon = gamma::source::canonicalize(input).unwrap_or_else(|_| input.to_path_buf());
    let dir = if canon.is_dir() { canon } else { canon.parent().map(Path::to_path_buf).unwrap_or_default() };
    let mut cur = Some(dir.as_path());
    while let Some(d) = cur {
        if d.join("gamma.toml").is_file() {
            return d.to_path_buf();
        }
        cur = d.parent();
    }
    dir
}

/// What a worker thread needs to analyze the program as the main thread did.
struct Proto {
    mode: gamma::interp::Mode,
    debug_info: bool,
    target: gamma::layout::Target,
    packages: Option<std::sync::Arc<gamma::pkg::Resolution>>,
}

impl Proto {
    fn of(sess: &Session) -> Proto {
        Proto {
            mode: sess.mode,
            debug_info: sess.debug_info,
            target: sess.target.clone(),
            packages: sess.packages.clone(),
        }
    }
}

/// `gamma test` and `gamma bench`: runs each test (or benchmark) of the root module, and of
/// the modules of its package that it imports (all of them with `--all`), with captured
/// output (§3.8). A failing test's output and failure reason are printed below its line.
pub fn run(opts: &Options, kind: Kind) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let (mut sess, toolchain) = new_session(opts);
    if crate::refuse_freestanding(&sess, if kind == Kind::Bench { "bench" } else { "test" }) {
        return ExitCode::FAILURE;
    }
    let Some(extras) = prepare_packages(opts, &mut sess, path) else { return ExitCode::FAILURE };
    // `--package=name`: the dependency's root module stands in for the input, and its own
    // dependencies resolve through this package's graph.
    let dep;
    let path = match &opts.package {
        None => path,
        Some(name) => {
            let Some(root) = gamma::pkg::find_root(path) else {
                eprintln!("gamma: `--package` needs the input to be in a package (a directory with a `gamma.toml`)");
                return ExitCode::from(2);
            };
            match gamma::pkg::dependency_dir(&root, sess.packages.as_deref(), name) {
                Ok(d) => dep = d,
                Err(e) => {
                    eprintln!("gamma: {}", e);
                    return ExitCode::from(2);
                }
            }
            &dep
        }
    };
    let prog = gamma::sema::analyze_package_tests(&mut sess, path, opts.all);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(mut prog) = prog else { return ExitCode::FAILURE };
    let cfg =
        gamma::interp::RunConfig { target: prog.target.clone(), mode: opts.mode, step_budget: None, deadline: None };
    // Tests of modules that call C or run assembly are compiled natively, so that C's output
    // is captured with the test's.
    let native = opts.c_backend || prog.has_foreign() || gamma::interp::foreign_supported(&prog, &cfg).is_err();
    let proto = Proto::of(&sess);
    let mut cover = None;
    if opts.coverage.is_some() {
        if kind == Kind::Bench {
            eprintln!("gamma: `--coverage` is an option of `gamma test`");
            return ExitCode::from(2);
        }
        if native && opts.cranelift {
            eprintln!("gamma: `--coverage` needs the interpreter or the C back end, not `--backend=cranelift`");
            return ExitCode::from(2);
        }
        let scope = Scope::new(path);
        let map = gamma::coverage::instrument(&mut prog, &sess.sources, &|p| scope.contains(p));
        gamma::interp::coverage_init(map.points.len());
        cover = Some((scope, map));
    }
    let me = Interp { sess, prog, cfg, timeout: opts.timeout };
    let cases = me.cases(kind);
    let chosen: Vec<usize> = (0..cases.len()).filter(|&i| selected(&cases[i], opts.filter.as_deref())).collect();
    let mut report = Report {
        kind,
        passed: 0,
        failed: 0,
        filtered_out: opts.filter.as_ref().map(|_| cases.len() - chosen.len()),
        show_output: opts.show_output,
        time: opts.time,
        started: Instant::now(),
    };
    // Benchmarks time themselves, so they take turns.
    let jobs = if kind == Kind::Bench { 1 } else { opts.jobs.max(1) };
    // What the native tests counted, added up over the tests (the interpreter keeps its own).
    let native_counts = std::sync::Mutex::new(Vec::new());
    let code = if native {
        let counts = cover.as_ref().map(|_| &native_counts);
        run_native(&me, kind, &chosen, &toolchain, &extras, jobs, opts.cranelift, opts.timeout, counts, &mut report)
    } else {
        let scope = cover.as_ref().map(|(s, _)| s);
        pool(
            chosen.len(),
            jobs,
            Some(&me),
            &|| Interp::build(opts, &proto, path, scope),
            &|s, j| s.run(&s.cases(kind)[chosen[j]]),
            &|_| Finished::failed("internal error: the test was not run\n".to_string()),
            |j, f| report.record(&cases[chosen[j]], f),
        );
        report.finish()
    };
    let counts = if native { native_counts.into_inner().unwrap_or_default() } else { gamma::interp::coverage_counts() };
    if let Some((scope, map)) = &cover
        && !coverage_report(&me.sess, scope, map, &counts, opts.coverage.as_ref().and_then(|f| f.as_deref()))
    {
        return ExitCode::FAILURE;
    }
    code
}

/// Compiles the chosen tests into one executable, which runs the test whose position among
/// them it is given, and runs it once per test.
#[allow(clippy::too_many_arguments)]
fn run_native(
    me: &Interp,
    kind: Kind,
    chosen: &[usize],
    toolchain: &Result<gamma::toolchain::Toolchain, String>,
    extras: &gamma::cgen::Extras,
    jobs: usize,
    cranelift: bool,
    timeout: Option<Duration>,
    counts: Option<&std::sync::Mutex<Vec<u32>>>,
    report: &mut Report,
) -> ExitCode {
    let cases = me.cases(kind);
    let entries: Vec<_> = chosen.iter().map(|&i| cases[i].entry).collect();
    let Some(dir) = native_dir(if kind == Kind::Test { "test" } else { "bench" }) else { return ExitCode::FAILURE };
    let exe = dir.join(native_exe_name("tests", toolchain));
    let opts = c_options(&me.sess);
    let built = (|| {
        let link = native_link(&me.sess, toolchain, extras)?;
        let toolchain = toolchain.as_ref().map_err(|e| format!("gamma: {}", e))?;
        if cranelift {
            let out = gamma::clif::generate(&me.prog, &me.sess.sources, &entries, true, &opts)
                .map_err(|u| cgen_error(&me.sess, u))?;
            gamma::clif::compile(&out, toolchain, &link, &exe)
        } else {
            let out = gamma::cgen::generate_tests(&me.prog, &me.sess.sources, &entries, &opts)
                .map_err(|u| cgen_error(&me.sess, u))?;
            gamma::cgen::compile(&out, toolchain, &link, &exe).map(|_| ())
        }
    })();
    let code = match built {
        Ok(()) => {
            pool(
                chosen.len(),
                jobs,
                Some(&()),
                &|| Some(()),
                &|_, j| {
                    let Some(counts) = counts else { return run_exe(&exe, j, timeout, None) };
                    let file = dir.join(format!("coverage-{j}"));
                    let done = run_exe(&exe, j, timeout, Some(&file));
                    add_counts(counts, &std::fs::read_to_string(&file).unwrap_or_default());
                    done
                },
                &|_| Finished::failed("internal error: the test was not run\n".to_string()),
                |j, f| report.record(&cases[chosen[j]], f),
            );
            report.finish()
        }
        Err(msg) => {
            eprintln!("{}", msg);
            ExitCode::FAILURE
        }
    };
    let _ = std::fs::remove_dir_all(&dir);
    code
}

/// Runs the native test executable `exe` on its `j`th test. A test that is still running
/// after `timeout` is killed, with everything it started, and fails.
/// With `cover`, the test writes its coverage counters to that file.
fn run_exe(exe: &Path, j: usize, timeout: Option<Duration>, cover: Option<&Path>) -> Finished {
    use std::io::Read;
    use std::process::Stdio;
    let mut cmd = runner_command(exe, None);
    cmd.arg(j.to_string()).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    if let Some(file) = cover {
        cmd.arg(file);
    }
    #[cfg(unix)]
    std::os::unix::process::CommandExt::process_group(&mut cmd, 0);
    let started = Instant::now();
    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(e) => return Finished::failed(format!("cannot run {}: {}\n", exe.display(), e)),
    };
    let drain = |mut r: Box<dyn Read + Send>| {
        std::thread::spawn(move || {
            let mut v = vec![];
            let _ = r.read_to_end(&mut v);
            v
        })
    };
    let out = drain(Box::new(child.stdout.take().expect("piped")));
    let err = drain(Box::new(child.stderr.take().expect("piped")));
    let mut expired = false;
    let status = loop {
        let status = match timeout {
            None => child.wait().map(Some),
            Some(_) => child.try_wait(),
        };
        match status {
            Ok(Some(s)) => break Ok(s),
            Ok(None) => {}
            Err(e) => break Err(e),
        }
        if timeout.is_some_and(|t| started.elapsed() >= t) {
            // The whole process group: the test may have started processes that keep the pipes open.
            // SAFETY: `kill` takes no pointers.
            #[cfg(unix)]
            unsafe {
                libc::kill(-(child.id() as i32), libc::SIGKILL);
            }
            let _ = child.kill();
            expired = true;
            break child.wait();
        }
        std::thread::sleep(Duration::from_millis(2));
    };
    let elapsed = started.elapsed();
    let status = match status {
        Ok(s) => s,
        Err(e) => return Finished::failed(format!("cannot run {}: {}\n", exe.display(), e)),
    };
    let mut output = String::from_utf8_lossy(&out.join().unwrap_or_default()).into_owned();
    output.push_str(&native_stderr(&String::from_utf8_lossy(&err.join().unwrap_or_default())));
    let failure = if expired {
        Some(format!("{}{}", output, timed_out(timeout)))
    } else {
        (status.code() != Some(0)).then(|| output.clone())
    };
    Finished { failure, output, elapsed }
}

/// Adds the counters a native test wrote (one decimal number per line) to `counts`.
fn add_counts(counts: &std::sync::Mutex<Vec<u32>>, text: &str) {
    let mut counts = counts.lock().unwrap_or_else(|e| e.into_inner());
    for (i, n) in text.lines().map(|l| l.trim().parse::<u32>().unwrap_or(0)).enumerate() {
        if counts.len() <= i {
            counts.resize(i + 1, 0);
        }
        counts[i] = counts[i].wrapping_add(n);
    }
}

/// Prints the lines of each file that the tests ran, and writes them as an LCOV tracefile
/// to `lcov` if that is given; false if the tracefile cannot be written.
fn coverage_report(
    sess: &Session,
    scope: &Scope,
    map: &gamma::coverage::CoverMap,
    counts: &[u32],
    lcov: Option<&Path>,
) -> bool {
    let mut files: BTreeMap<PathBuf, BTreeMap<u32, u32>> = BTreeMap::new();
    for (i, (file, line)) in map.points.iter().enumerate() {
        let path = &sess.sources.file(*file).path;
        let path = gamma::source::canonicalize(path).unwrap_or_else(|_| path.clone());
        let hits = counts.get(i).copied().unwrap_or(0);
        let slot = files.entry(path).or_default().entry(*line).or_insert(0);
        *slot = (*slot).max(hits);
    }
    println!();
    if files.is_empty() {
        println!("coverage: no code of the package ran");
    } else {
        let shown: Vec<(String, usize, usize)> = files
            .iter()
            .map(|(p, lines)| {
                let rel = p.strip_prefix(&scope.root).unwrap_or(p);
                let name = rel.components().map(|c| c.as_os_str().to_string_lossy()).collect::<Vec<_>>().join("/");
                (name, lines.values().filter(|&&c| c > 0).count(), lines.len())
            })
            .collect();
        let width = shown.iter().map(|(n, ..)| n.chars().count()).max().unwrap_or(0).max("total".len());
        println!("coverage (lines run):");
        let percent = |hit: usize, all: usize| if all == 0 { 100.0 } else { hit as f64 * 100.0 / all as f64 };
        for (name, hit, all) in &shown {
            println!("  {:<width$} {:>5}/{:<5} {:>6.1}%", name, hit, all, percent(*hit, *all));
        }
        let (hit, all) = shown.iter().fold((0, 0), |(h, a), (_, fh, fa)| (h + fh, a + fa));
        println!("  {:<width$} {:>5}/{:<5} {:>6.1}%", "total", hit, all, percent(hit, all));
    }
    if let Some(out) = lcov {
        let mut text = String::new();
        for (path, lines) in &files {
            text.push_str(&format!("TN:\nSF:{}\n", path.display()));
            for (line, hits) in lines {
                text.push_str(&format!("DA:{},{}\n", line, hits));
            }
            let hit = lines.values().filter(|&&c| c > 0).count();
            text.push_str(&format!("LF:{}\nLH:{}\nend_of_record\n", lines.len(), hit));
        }
        if let Err(e) = std::fs::write(out, text) {
            eprintln!("gamma: cannot write {}: {}", out.display(), e);
            return false;
        }
    }
    true
}
