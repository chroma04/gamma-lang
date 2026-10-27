//! The conformance `run` tests built by the compiler under test (`GAMMA_TEST_COMPILER`) in
//! a lane other than the host's own: `GAMMA_TEST_LANE` names it, `safe` (Safe builds with
//! Fil-C), `s390x` (big-endian) or `aarch64` (cross-compiled for Linux and run under
//! qemu-user), or `wasi` (WebAssembly, run under Wasmtime). Each program must give the expected output and exit status. CI
//! runs this where the lane's tools are installed; without `GAMMA_TEST_LANE` it does
//! nothing.

mod common;

use common::root;
use std::path::{Path, PathBuf};

/// The `.gm` files under `dir`, sorted, as paths relative to the repository root.
fn gm_files(dir: &Path, out: &mut Vec<String>) {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(root().join(dir)).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    for p in entries {
        let rel = p.strip_prefix(root()).unwrap().to_path_buf();
        if p.is_dir() {
            gm_files(&rel, out);
        } else if p.extension().is_some_and(|e| e == "gm") {
            out.push(rel.to_str().unwrap().replace('\\', "/"));
        }
    }
}

/// The first line where `actual` differs from `expected`, as (the file it belongs to, its
/// line number in the output, the expected line, the actual line).
fn first_difference(expected: &str, actual: &str) -> Option<(String, usize, String, String)> {
    let (mut e, mut a) = (expected.lines(), actual.lines());
    let mut file = String::new();
    for n in 1.. {
        match (e.next(), a.next()) {
            (None, None) => return None,
            (x, y) if x == y => {
                if let Some(name) = x.and_then(|l| l.strip_prefix("==> ")).and_then(|l| l.strip_suffix(" <==")) {
                    file = name.to_string();
                }
            }
            (x, y) => {
                let show = |l: Option<&str>| l.unwrap_or("(end of output)").to_string();
                return Some((file, n, show(x), show(y)));
            }
        }
    }
    None
}

/// A conformance `run` test that `gamma run` compiles: its root, build mode, standard
/// input, expected output and exit status (see `tests/conformance.rs` for the directives).
#[cfg(unix)]
struct RunTest {
    name: String,
    root: PathBuf,
    mode: String,
    stdin: Vec<u8>,
    stdout: Vec<u8>,
    exit: i32,
}

/// A way of building the run tests other than for the host in their own mode, chosen with
/// `GAMMA_TEST_LANE` (see the top of this file).
#[cfg(unix)]
struct Lane {
    name: String,
    /// The options `gamma run` gets, before the mode.
    args: Vec<String>,
    /// The environment of `gamma run`.
    env: Vec<(String, String)>,
    /// The values of the `arch:`, `endian:`, `os:` and `ptr_bits:` directives it runs.
    host: [(&'static str, String); 4],
    /// The `needs:` requirements it lacks.
    lacks: Vec<&'static str>,
}

#[cfg(unix)]
fn lane() -> Option<Lane> {
    let name = std::env::var("GAMMA_TEST_LANE").ok().filter(|n| !n.is_empty())?;
    let s = |v: &str| v.to_string();
    let host = |arch: &str, endian: &str, os: &str, bits: &str| {
        [("arch:", s(arch)), ("endian:", s(endian)), ("os:", s(os)), ("ptr_bits:", s(bits))]
    };
    let lane = match name.as_str() {
        "safe" => Lane {
            name,
            args: vec![],
            env: vec![],
            host: host(std::env::consts::ARCH, "little", "unix", "64"),
            lacks: vec![],
        },
        "s390x" => Lane {
            name,
            args: vec![s("--target=s390x-linux-gnu")],
            env: vec![
                (s("CC"), s("s390x-linux-gnu-gcc")),
                (s("CFLAGS"), s("-static")),
                (s("GAMMA_RUNNER"), s("qemu-s390x -L /usr/s390x-linux-gnu")),
            ],
            host: host("s390x", "big", "unix", "64"),
            lacks: vec!["f16"],
        },
        "aarch64" => Lane {
            name,
            args: vec![s("--target=aarch64-linux-gnu")],
            env: vec![
                (s("CC"), s("aarch64-linux-gnu-gcc")),
                (s("CFLAGS"), s("-static")),
                (s("GAMMA_RUNNER"), s("qemu-aarch64 -L /usr/aarch64-linux-gnu")),
            ],
            host: host("aarch64", "little", "unix", "64"),
            lacks: vec![],
        },
        "wasi" => Lane {
            name,
            args: vec![s("--target=wasm32-wasi")],
            // A WebAssembly module sees only the current directory (the repository's root):
            // its temporary files go below it.
            env: vec![(s("TMPDIR"), {
                let tmp = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-wasi-tmp");
                std::fs::create_dir_all(&tmp).unwrap();
                tmp.strip_prefix(root()).unwrap_or(&tmp).display().to_string()
            })],
            host: host("wasm32", "little", "wasi", "32"),
            lacks: vec!["f16", "threads", "sockets"],
        },
        other => panic!("unknown GAMMA_TEST_LANE `{}` (safe, s390x, aarch64 or wasi)", other),
    };
    Some(lane)
}

/// The `run` tests that expect no compile errors and apply to `lane`.
#[cfg(unix)]
fn lane_tests(lane: &Lane) -> Vec<RunTest> {
    let dir = root().join("tests/conformance/run");
    let mut entries: Vec<PathBuf> = std::fs::read_dir(&dir).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    let mut tests = vec![];
    for p in entries {
        let name = p.file_name().unwrap().to_str().unwrap().to_string();
        let base = if let Some(b) = name.strip_suffix(".gm") {
            b.to_string()
        } else if let Some(b) = name.strip_suffix(".test") {
            b.to_string()
        } else {
            continue;
        };
        let mut text = String::new();
        if p.is_dir() {
            let mut files = vec![];
            gm_files(p.strip_prefix(root()).unwrap(), &mut files);
            for f in files {
                text.push_str(&std::fs::read_to_string(root().join(f)).unwrap());
            }
        } else {
            text = std::fs::read_to_string(&p).unwrap();
        }
        let directive = |key: &str| {
            text.lines()
                .find_map(|l| l.strip_prefix("//! ").and_then(|r| r.strip_prefix(key)).map(|v| v.trim().to_string()))
        };
        if text.contains("//~") || directive("stage:").is_some_and(|s| s != "run") {
            continue;
        }
        let host = &lane.host;
        if host.iter().any(|(k, v)| directive(k).is_some_and(|d| &d != v)) {
            continue;
        }
        let needs = directive("needs:").unwrap_or_default();
        if needs.split(',').any(|n| lane.lacks.contains(&n.trim())) {
            continue;
        }
        let mut mode = directive("mode:").unwrap_or_else(|| "debug".to_string());
        match lane.name.as_str() {
            // Every test of the default and Checked modes also runs in a Safe build.
            "safe" => {
                if mode == "fast" || directive("safe:").is_some_and(|v| v.starts_with("skip")) {
                    continue;
                }
                mode = "safe".to_string();
            }
            _ if mode == "safe" => continue,
            _ => {}
        }
        let root_dir = if p.is_dir() && p.join("main").is_dir() { p.join("main") } else { p.clone() };
        let read = |ext: &str| std::fs::read(dir.join(format!("{}.{}", base, ext))).unwrap_or_default();
        tests.push(RunTest {
            name: format!("tests/conformance/run/{}", name),
            root: root_dir,
            mode,
            stdin: read("stdin"),
            stdout: read("stdout"),
            exit: directive("exit:").map_or(0, |e| e.parse().unwrap()),
        });
    }
    tests
}

/// Runs `t` with `gamma run`, which compiles it to a temporary executable, built and run as
/// `lane` says; returns why it failed, if it did.
#[cfg(unix)]
fn build_and_run_in(t: &RunTest, dir: &Path, lane: &Lane) -> Option<String> {
    let stdin = dir.join(format!("{}.stdin", t.name.replace('/', "_")));
    std::fs::write(&stdin, &t.stdin).unwrap();
    let mut cmd = common::gamma();
    cmd.current_dir(root()).arg("run").args(&lane.args).envs(lane.env.iter().map(|(k, v)| (k, v)));
    cmd.arg(format!("--mode={}", t.mode)).arg(&t.root);
    cmd.stdin(std::fs::File::open(&stdin).unwrap());
    let ran = common::output(&mut cmd).unwrap();
    let _ = std::fs::remove_file(&stdin);
    if ran.status.code() != Some(t.exit) {
        return Some(format!(
            "exit status {:?}, expected {}:\n{}",
            ran.status.code(),
            t.exit,
            String::from_utf8_lossy(&ran.stderr)
        ));
    }
    if ran.stdout != t.stdout {
        let (e, a) = (String::from_utf8_lossy(&t.stdout), String::from_utf8_lossy(&ran.stdout));
        let (_, line, e, a) = first_difference(&e, &a).unwrap_or_default();
        return Some(format!("output differs at line {}:\nexpected: {}\n  actual: {}", line, e, a));
    }
    None
}

/// The run tests built for the lane `GAMMA_TEST_LANE` names (see `lane`).
#[test]
#[cfg(unix)]
fn programs_compiled_in_a_lane_behave_as_expected() {
    let Some(lane) = lane() else { return };
    let tests = lane_tests(&lane);
    assert!(tests.len() > 300, "only {} run tests found", tests.len());
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join(format!("selfhost-lane-{}", lane.name));
    std::fs::create_dir_all(&dir).unwrap();
    let next = std::sync::atomic::AtomicUsize::new(0);
    let failures = std::sync::Mutex::new(vec![]);
    let workers = std::thread::available_parallelism().map_or(4, |n| n.get());
    std::thread::scope(|s| {
        for _ in 0..workers {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(t) = tests.get(i) else { break };
                    if let Some(why) = build_and_run_in(t, &dir, &lane) {
                        failures.lock().unwrap().push(format!("{}: {}", t.name, why));
                    }
                }
            });
        }
    });
    let mut failures = failures.into_inner().unwrap();
    failures.sort();
    assert!(
        failures.is_empty(),
        "{}: {} of {} programs failed:\n{}",
        lane.name,
        failures.len(),
        tests.len(),
        failures.join("\n\n")
    );
}
