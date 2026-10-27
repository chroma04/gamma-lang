//! Helpers shared by the integration test suites (`mod common;` in each).

#![allow(dead_code)]

use std::path::{Path, PathBuf};
use std::process::Command;

/// The root of the repository.
pub fn root() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

/// The compiler the suites test: the `gamma` executable that `GAMMA_TEST_COMPILER` names,
/// built from `compiler/` (`tools/test.sh` builds it and sets the variable).
pub fn compiler() -> &'static Path {
    static COMPILER: std::sync::OnceLock<PathBuf> = std::sync::OnceLock::new();
    COMPILER.get_or_init(|| {
        let exe = std::env::var_os("GAMMA_TEST_COMPILER").filter(|v| !v.is_empty()).map(PathBuf::from);
        let exe = exe.unwrap_or_else(|| {
            panic!(
                "GAMMA_TEST_COMPILER is not set: run the tests with tools/test.sh, which builds \
                 gamma from compiler/ and sets it, or set it to a gamma executable"
            )
        });
        assert!(exe.is_file(), "GAMMA_TEST_COMPILER names {}, which is not a file", exe.display());
        // Relative to the directory the tests started in, not to the ones they run in.
        std::path::absolute(&exe).unwrap()
    })
}

/// A command running the compiler under test with the repository's standard library and
/// runtime, caching compiled runtime objects under the target directory rather than in the
/// user's cache.
pub fn gamma() -> Command {
    compiler_command(compiler())
}

/// Like `gamma`, for the compiler `exe` (one that a test built).
pub fn compiler_command(exe: &Path) -> Command {
    let mut cmd = Command::new(exe);
    cmd.env("GAMMA_STD", root().join("lib/std"))
        .env("GAMMA_RUNTIME", root().join("runtime"))
        .env("GAMMA_CACHE_DIR", Path::new(env!("CARGO_TARGET_TMPDIR")).join("gamma-cache"));
    cmd
}

/// Whether the C back end can be tested: a C compiler runs and `GAMMA_TEST_C` is not 0.
/// With `GAMMA_TEST_C=1` a missing compiler fails the tests instead of skipping the C
/// lanes, so a CI job that is meant to test C cannot pass by testing only the interpreter.
pub fn c_backend_available() -> bool {
    static AVAILABLE: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *AVAILABLE.get_or_init(|| {
        let setting = std::env::var("GAMMA_TEST_C").unwrap_or_default();
        if setting == "0" {
            return false;
        }
        let found = gamma::toolchain::Toolchain::from_env()
            .is_ok_and(|t| t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()));
        assert!(found || setting != "1", "GAMMA_TEST_C=1, but no C compiler runs (set CC?)");
        found
    })
}

/// Runs `cmd` (whose standard input the caller has set) to completion and collects its
/// output, like `Command::output`, but kills it and everything it started after
/// `GAMMA_TEST_TIMEOUT` seconds (default 600), so that a program that never ends fails its
/// test instead of hanging the suite.
pub fn output(cmd: &mut std::process::Command) -> std::io::Result<std::process::Output> {
    use std::io::Read;
    use std::process::Stdio;
    use std::time::{Duration, Instant};
    let limit = std::env::var("GAMMA_TEST_TIMEOUT").ok().and_then(|v| v.parse().ok()).unwrap_or(600);
    #[cfg(unix)]
    std::os::unix::process::CommandExt::process_group(cmd, 0);
    let mut child = cmd.stdout(Stdio::piped()).stderr(Stdio::piped()).spawn()?;
    let drain = |mut r: Box<dyn Read + Send>| {
        std::thread::spawn(move || {
            let mut v = vec![];
            let _ = r.read_to_end(&mut v);
            v
        })
    };
    let out = drain(Box::new(child.stdout.take().unwrap()));
    let err = drain(Box::new(child.stderr.take().unwrap()));
    let deadline = Instant::now() + Duration::from_secs(limit);
    let status = loop {
        if let Some(s) = child.try_wait()? {
            break s;
        }
        if Instant::now() >= deadline {
            // The whole process group: `gamma run --backend=c` runs the program as a child,
            // which holds the pipes open.
            // SAFETY: `kill` takes no pointers.
            #[cfg(unix)]
            unsafe {
                libc::kill(-(child.id() as i32), libc::SIGKILL);
            }
            let _ = child.kill();
            let _ = child.wait();
            return Err(std::io::Error::new(
                std::io::ErrorKind::TimedOut,
                format!("timed out after {} s (GAMMA_TEST_TIMEOUT)", limit),
            ));
        }
        std::thread::sleep(Duration::from_millis(5));
    };
    Ok(std::process::Output { status, stdout: out.join().unwrap_or_default(), stderr: err.join().unwrap_or_default() })
}
