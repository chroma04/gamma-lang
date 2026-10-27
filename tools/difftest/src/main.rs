//! Differential tester for the Gamma compiler.
//!
//! Generates random, well-typed, terminating programs from seeds and runs each with the
//! interpreter (`gamma run --backend=interp`) and with the C back end compiled by gcc and by clang
//! (`gamma run --backend=c` with `CC` set). The three runs must agree on standard output,
//! standard error and exit status. See `docs/contributing.md`.

mod program;
mod reduce;
mod rng;

use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

const USAGE: &str = "\
usage: difftest [options]

Runs random programs through the interpreter and the C back end (gcc, clang).

  --start N          first seed (default 0)
  --count N          number of seeds to run (default 100)
  --jobs N           programs run in parallel (default: the number of CPUs)
  --gamma PATH       the gamma executable (default: $GAMMA, else target/bootstrap/gamma
                     of the repository)
  --cc LIST          C compilers to compare, comma separated (default gcc,clang);
                     compilers that are not installed are skipped
  --mode MODE        build mode passed to gamma: debug (default), checked
  --work DIR         where programs are written (default: the system temporary directory);
                     programs of seeds that disagree are kept there
  --timeout SECS     per run (default 60)
  --print SEED       print the program of SEED and exit
  --save SEED FILE   write the program of SEED to FILE and exit
  --reduce FILE      shrink a program that the back ends disagree on; writes FILE.min.gm
  --stop-first       stop at the first disagreement";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub stdout: String,
    pub stderr: String,
    /// The exit status, or -1 for a timeout, -2 if the process could not be run.
    pub code: i32,
}

#[derive(Clone)]
pub struct Config {
    pub gamma: PathBuf,
    pub ccs: Vec<String>,
    pub mode: String,
    pub work: PathBuf,
    pub timeout: Duration,
}

pub fn run_with_timeout(mut cmd: Command, timeout: Duration) -> (String, String, i32) {
    cmd.stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    // A group of its own, so that a timeout also kills the program `gamma run` started,
    // which would otherwise keep the pipes open.
    std::os::unix::process::CommandExt::process_group(&mut cmd, 0);
    let Ok(mut child) = cmd.spawn() else { return (String::new(), "cannot run the process".into(), -2) };
    let mut out = child.stdout.take().unwrap();
    let mut err = child.stderr.take().unwrap();
    let t_out = std::thread::spawn(move || {
        let mut b = vec![];
        let _ = out.read_to_end(&mut b);
        b
    });
    let t_err = std::thread::spawn(move || {
        let mut b = vec![];
        let _ = err.read_to_end(&mut b);
        b
    });
    let start = Instant::now();
    let code = loop {
        match child.try_wait() {
            Ok(Some(status)) => {
                use std::os::unix::process::ExitStatusExt;
                break status.code().unwrap_or_else(|| 128 + status.signal().unwrap_or(0));
            }
            Ok(None) if start.elapsed() > timeout => {
                // A negative pid names the process group (a plain `kill` command needs no
                // unsafe code here).
                let _ = Command::new("kill").args(["-KILL", "--", &format!("-{}", child.id())]).status();
                let _ = child.kill();
                let _ = child.wait();
                break -1;
            }
            Ok(None) => std::thread::sleep(Duration::from_millis(5)),
            Err(_) => break -2,
        }
    };
    let stdout = String::from_utf8_lossy(&t_out.join().unwrap_or_default()).into_owned();
    let stderr = String::from_utf8_lossy(&t_err.join().unwrap_or_default()).into_owned();
    (stdout, stderr, code)
}

/// Runs `file` with the interpreter (`cc` is `None`) or the C back end compiled by `cc`.
pub fn run_one(cfg: &Config, file: &Path, cc: Option<&str>) -> Outcome {
    let mut cmd = Command::new(&cfg.gamma);
    cmd.arg("run").arg(format!("--mode={}", cfg.mode));
    match cc {
        Some(cc) => cmd.arg("--backend=c").env("CC", cc),
        None => cmd.arg("--backend=interp"),
    };
    cmd.arg("--diag-format=short").arg(file);
    cmd.env("GAMMA_CACHE_DIR", cfg.work.join("cache"));
    let (stdout, stderr, code) = run_with_timeout(cmd, cfg.timeout);
    // Diagnostics and trap reports name the file; the directory differs per seed.
    let path = file.display().to_string();
    let stderr = stderr.replace(&path, "FILE");
    Outcome { stdout, stderr, code }
}

/// The outcomes of every configuration: the interpreter first, then each C compiler.
pub fn run_all(cfg: &Config, file: &Path) -> Vec<(String, Outcome)> {
    let mut v = vec![("interp".to_string(), run_one(cfg, file, None))];
    for cc in &cfg.ccs {
        v.push((cc.clone(), run_one(cfg, file, Some(cc))));
    }
    v
}

/// Which configurations disagree with the interpreter, as a signature.
pub fn disagreement(results: &[(String, Outcome)]) -> Option<String> {
    let base = &results[0].1;
    let diff: Vec<&str> = results[1..].iter().filter(|(_, o)| o != base).map(|(n, _)| n.as_str()).collect();
    if diff.is_empty() { None } else { Some(diff.join("+")) }
}

fn describe(results: &[(String, Outcome)]) -> String {
    let mut s = String::new();
    for (name, o) in results {
        s.push_str(&format!("--- {name}: exit {}\n", o.code));
        s.push_str("stdout:\n");
        s.push_str(&o.stdout);
        s.push_str("stderr:\n");
        s.push_str(&o.stderr);
    }
    s
}

fn installed(cc: &str) -> bool {
    Command::new(cc).arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok_and(|s| s.success())
}

fn default_gamma() -> PathBuf {
    if let Some(p) = std::env::var_os("GAMMA") {
        return PathBuf::from(p);
    }
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target/bootstrap/gamma")
}

struct Stats {
    done: AtomicU64,
    trapped: AtomicU64,
    invalid: AtomicU64,
    mismatches: AtomicU64,
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut start = 0u64;
    let mut count = 100u64;
    let mut jobs = std::thread::available_parallelism().map_or(2, |n| n.get());
    let mut gamma = default_gamma();
    let mut ccs = vec!["gcc".to_string(), "clang".to_string()];
    let mut mode = "debug".to_string();
    let mut work = std::env::temp_dir().join("difftest");
    let mut timeout = 60u64;
    let mut keep_going = true;
    let mut step;
    let mut i = 0;
    let need = |i: usize, what: &str| -> String {
        args.get(i + 1).cloned().unwrap_or_else(|| {
            eprintln!("difftest: {what} needs a value\n\n{USAGE}");
            std::process::exit(2)
        })
    };
    let num = |s: String| -> u64 {
        s.parse().unwrap_or_else(|_| {
            eprintln!("difftest: not a number: {s}");
            std::process::exit(2)
        })
    };
    while i < args.len() {
        step = 2;
        match args[i].as_str() {
            "--start" => start = num(need(i, "--start")),
            "--count" => count = num(need(i, "--count")),
            "--jobs" => jobs = num(need(i, "--jobs")).max(1) as usize,
            "--gamma" => gamma = PathBuf::from(need(i, "--gamma")),
            "--cc" => ccs = need(i, "--cc").split(',').map(str::to_string).collect(),
            "--mode" => mode = need(i, "--mode"),
            "--work" => work = PathBuf::from(need(i, "--work")),
            "--timeout" => timeout = num(need(i, "--timeout")),
            "--stop-first" => {
                keep_going = false;
                step = 1;
            }
            "--print" => {
                print!("{}", program::Gen::program(num(need(i, "--print"))));
                return;
            }
            "--save" => {
                let seed = num(need(i, "--save"));
                let file = args.get(i + 2).unwrap_or_else(|| {
                    eprintln!("difftest: --save needs a seed and a file\n\n{USAGE}");
                    std::process::exit(2)
                });
                std::fs::write(file, program::Gen::program(seed)).expect("cannot write the file");
                return;
            }
            "--reduce" => {
                let file = PathBuf::from(need(i, "--reduce"));
                ccs.retain(|c| installed(c));
                let cfg = Config { gamma, ccs, mode, work, timeout: Duration::from_secs(timeout.min(20)) };
                reduce::reduce_file(&cfg, &file);
                return;
            }
            "-h" | "--help" => {
                println!("{USAGE}");
                return;
            }
            other => {
                eprintln!("difftest: unknown option `{other}`\n\n{USAGE}");
                std::process::exit(2);
            }
        }
        i += step;
    }
    if !gamma.exists() {
        eprintln!("difftest: {} not found; build it with `cargo build --release` or pass --gamma", gamma.display());
        std::process::exit(2);
    }
    ccs.retain(|c| {
        let ok = installed(c);
        if !ok {
            eprintln!("difftest: C compiler `{c}` is not installed, skipping it");
        }
        ok
    });
    let _ = std::fs::create_dir_all(work.join("cache"));
    let cfg = Config { gamma, ccs, mode, work, timeout: Duration::from_secs(timeout) };
    let next = Arc::new(AtomicU64::new(start));
    let end = start + count;
    let stats = Arc::new(Stats {
        done: AtomicU64::new(0),
        trapped: AtomicU64::new(0),
        invalid: AtomicU64::new(0),
        mismatches: AtomicU64::new(0),
    });
    let stop = Arc::new(Mutex::new(false));
    let handles: Vec<_> = (0..jobs)
        .map(|_| {
            let (cfg, next, stats, stop) = (cfg.clone(), next.clone(), stats.clone(), stop.clone());
            std::thread::spawn(move || {
                loop {
                    let seed = next.fetch_add(1, Ordering::SeqCst);
                    if seed >= end || *stop.lock().unwrap() {
                        break;
                    }
                    check_seed(&cfg, seed, &stats, keep_going, &stop);
                    let done = stats.done.fetch_add(1, Ordering::SeqCst) + 1;
                    if done % 50 == 0 {
                        eprintln!(
                            "difftest: {done} programs, {} disagreements",
                            stats.mismatches.load(Ordering::SeqCst)
                        );
                    }
                }
            })
        })
        .collect();
    for h in handles {
        let _ = h.join();
    }
    println!(
        "difftest: {} programs ({} trapped, {} rejected by the compiler), {} disagreements; configurations: interp{}",
        stats.done.load(Ordering::SeqCst),
        stats.trapped.load(Ordering::SeqCst),
        stats.invalid.load(Ordering::SeqCst),
        stats.mismatches.load(Ordering::SeqCst),
        cfg.ccs.iter().map(|c| format!(", {c}")).collect::<String>()
    );
    if stats.mismatches.load(Ordering::SeqCst) > 0 || stats.invalid.load(Ordering::SeqCst) > 0 {
        std::process::exit(1);
    }
}

fn check_seed(cfg: &Config, seed: u64, stats: &Stats, keep_going: bool, stop: &Mutex<bool>) {
    let dir = cfg.work.join(format!("seed-{seed}"));
    let _ = std::fs::create_dir_all(&dir);
    let file = dir.join("main.gm");
    std::fs::write(&file, program::Gen::program(seed)).expect("cannot write the program");
    let results = run_all(cfg, &file);
    let base = &results[0].1;
    // A program the compiler rejects is a bug of the generator (or of the compiler).
    let rejected = base.code == 1 && base.stdout.is_empty() && base.stderr.contains("error");
    if rejected {
        stats.invalid.fetch_add(1, Ordering::SeqCst);
        println!("seed {seed}: rejected by the compiler ({})\n{}", file.display(), base.stderr);
        return;
    }
    if base.code == 134 {
        stats.trapped.fetch_add(1, Ordering::SeqCst);
    }
    match disagreement(&results) {
        None => {
            let _ = std::fs::remove_dir_all(&dir);
        }
        Some(who) => {
            stats.mismatches.fetch_add(1, Ordering::SeqCst);
            println!("seed {seed}: interp disagrees with {who} ({})\n{}", file.display(), describe(&results));
            if !keep_going {
                *stop.lock().unwrap() = true;
            }
        }
    }
}
