//! The `gamma` command-line tool.

use gamma::diag::DiagFormat;
use gamma::driver::{self, Session};
use gamma::toolchain::Toolchain;
use std::path::PathBuf;
use std::process::ExitCode;

mod linting;
mod testing;

const USAGE: &str = "\
usage: gamma <command> [options] <file>

commands:
  lex     print the tokens of a source file
  parse   check the syntax of a source file (--dump prints the syntax tree)
  check   type-check a program (a .gm file or a module directory)
  fix     check a program and apply the fixes its diagnostics suggest where there is only
          one, to the files of its package; prints the diagnostics left
  lint    check a package (a .gm file or a directory, with all the modules of its package)
          and warn about code that is probably a mistake or has a simpler form
          (run `gamma help lint` for the options and `gamma lint --rules` for the rules)
  run     run a program; arguments after the file go to the program
  build   compile a program to a native executable (via the system C compiler); for a
          freestanding target (--target with a compiler that targets no operating system)
          an object file of its `#export`ed functions, to link with the code that starts them
  test    run the `test` declarations of a module and of the modules of its package
          that it imports (natively if it needs C)
  bench   run the `bench` declarations the same way and report the time per run
          (compiled via C in Fast mode unless --backend=interp or --mode is given)
  fmt     format source files in place: gamma fmt [--check] <file or directory>... (`-` filters stdin to stdout)
  doc     generate API documentation: gamma doc <module> [--format=markdown|html] [-o <path>] [--all] [--missing]
          (run `gamma help doc` for the options)
  new     create a package in a new directory: gamma new <dir>
  init    create a package in an existing directory: gamma init [dir]
  fetch   download the git and archive dependencies of a package and write gamma.lock
  update  move git dependencies to newer revisions
  add     add a dependency to gamma.toml:
          gamma add <name> (--path <dir> | --git <url> [--tag|--branch|--rev|--version <x>] | --url <url.tar.gz>)
  remove  remove a dependency from gamma.toml
  cache   gamma cache dir | gamma cache clean [--older-than=<days>] [--all]
          (run `gamma help packages` for the options of the package commands)
  repl    interactive read-eval-print loop: gamma repl [--quiet] [file] (loads the file's declarations)
  lsp     run the language server on standard input and output
  help    print this message; `gamma help packages`, `gamma help doc` and `gamma help lint`
          add the options of the package commands, of `doc` and of `lint` (as does `--help`
          after a command)
  version print the compiler version

options:
  --diag-format=human|short|json
                              how to print diagnostics (default: human); json prints one
                              object per line, with locations and fixes
  --dump                      (parse) print the syntax tree
  --backend=interp|c|cranelift
                              (run, test, bench, build) use the interpreter (default for run and
                              test), compile via C (the default for build and bench), or
                              generate native code directly with Cranelift (64-bit Linux and
                              macOS; see docs/guide/native-backend.md)
  --filter=<text>             (test, bench) run only the tests whose name or module path
                              contains <text>
  --all                       (test, bench) also run those of the modules of the package
                              that nothing imports (every directory with `.gm` files)
  --package=<name>            (test, bench) run those of the dependency <name> of the input's
                              package (or of one of its dependencies) instead, with the
                              dependency versions this package locks
  --show-output               (test, bench) also print the output of the tests that pass
  --jobs=<n>, -j <n>          (test) run <n> tests at a time (default 1; 0: one per processor;
                              the output is still in order)
  --timeout=<seconds>         (test, bench) fail a test that runs longer than <seconds> (no
                              limit by default; the interpreter cannot stop a test that is
                              blocked in a system call, but native tests are always killed)
  --time                      (test, bench) print how long each test took and the total
  --coverage[=<file>]         (test) count the lines of the package that the tests run, with
                              the interpreter; print a table, and with <file> write LCOV
  --target=<triple>           (check, run --backend=c, test, bench, build) compile for the machine
                              <triple> names (`s390x-linux-gnu`) with a cross C compiler:
                              with CC unset <triple>-gcc, else `clang --target=<triple>`;
                              with CC set, CC (given --target if it is clang). A compiler that
                              targets no operating system (`x86_64-unknown-none-elf`) makes a
                              freestanding program (spec 3.7.1): check and build only, and
                              build writes an object file of its `#export`ed functions;
                              `wasm32-wasi` builds a WebAssembly module (<name>.wasm)
  --quiet                     (repl) no banner and prompts, even on a terminal
  --offline                   (check, run, build, test, bench) never use the network: git
                              dependencies must be locked and already fetched
  --locked                    (check, run, build, test, bench) fail if gamma.lock is missing or
                              would change
  -o <file>                   (build) the executable to write (default: the input's name,
                              with .exe for Windows and .wasm for WASI);
                              for a freestanding target the object file (default: <name>.o)
  --profile=<name>            (check, run, build, test, bench) use the `[profile.<name>]` table of
                              gamma.toml over its `[build]` table (mode, debug-info, cflags,
                              ldflags, c-sources, include-dirs, libs, lib-dirs); options on
                              the command line win over both
  -g                          (build, and run, test and bench via C) add debug information that maps
                              the executable to the Gamma source, for debuggers; a Debug build
                              is then compiled without optimization
  --mode=debug|checked|safe|fast
                              (run, build, test, bench, repl) the build mode (default: debug;
                              bench: fast): Debug, Checked and Safe trap on failed checks,
                              Fast omits them; Debug also checks every free; Safe compiles
                              with Fil-C, which traps on every invalid memory access

environment:
  CC        the C compiler for native builds, with any arguments (default: cc, and
            for Safe builds Fil-C's filcc from PATH or /opt/fil/bin); a cross
            compiler (`zig cc -target aarch64-linux-musl`) makes it the target
  CFLAGS    extra C compiler options (after those of gamma.toml, so they win)
  LDFLAGS   extra linker options (likewise)
  GAMMA_RUNNER
            a command that runs the executables of `run --backend=c` and `test`
            (`qemu-s390x -L /usr/s390x-linux-gnu` for a cross build), with any arguments;
            WebAssembly modules (--target=wasm32-wasi) run by default with
            `wasmtime run --dir=. -S inherit-env -W max-wasm-stack=268435456`
  WASI_SDK_PATH
            a wasi-sdk whose clang and sysroot --target=wasm32-wasi uses when CC is unset
  WASM_OPT  Binaryen's wasm-opt, which WASI modules that switch fibers need (default:
            wasm-opt from PATH)
";

struct Options {
    command: String,
    file: Option<PathBuf>,
    /// Arguments for the program (`gamma run file.gm args…`).
    program_args: Vec<std::ffi::OsString>,
    format: DiagFormat,
    dump: bool,
    /// `--backend=c` or `--backend=cranelift`: run natively.
    c_backend: bool,
    /// `--backend=cranelift`: generate native code with Cranelift rather than through C.
    cranelift: bool,
    /// `-o`: the output of `build`.
    output: Option<PathBuf>,
    /// `--mode=…`: the build mode.
    mode: gamma::interp::Mode,
    /// `-g`: native code carries debug information.
    debug_info: bool,
    /// `--filter=…`: run only the tests that match.
    filter: Option<String>,
    /// `--all`: also the tests of modules that nothing imports.
    all: bool,
    /// `--package=name`: the tests of that dependency instead of the package's own.
    package: Option<String>,
    /// `--show-output`: print the output of passing tests too.
    show_output: bool,
    /// `--jobs=n`: how many tests run at once.
    jobs: usize,
    /// `--timeout=seconds`: the longest a test or benchmark may run.
    timeout: Option<std::time::Duration>,
    /// `--time`: report how long tests take.
    time: bool,
    /// `--coverage[=file]`: count the lines the tests run; the LCOV file to write, if any.
    coverage: Option<Option<PathBuf>>,
    /// `--target=…`: the target triple to compile for.
    target: Option<String>,
    /// `--offline`: do not use the network for dependencies.
    offline: bool,
    /// `--locked`: gamma.lock must be up to date.
    locked: bool,
    /// `--quiet`: the REPL prints no banner or prompts.
    quiet: bool,
    /// `--profile=…`: the build profile of gamma.toml.
    profile: Option<String>,
    /// Whether `--mode` / `-g` were given (they win over gamma.toml).
    mode_given: bool,
    debug_info_given: bool,
    /// `cflags` / `ldflags` of gamma.toml, before those of the environment.
    manifest_cflags: Vec<String>,
    manifest_ldflags: Vec<String>,
    /// `gamma lint`'s options.
    lint: linting::LintOptions,
}

/// The number of tests to run at once: `--jobs=n`, where 0 means one per processor.
fn parse_jobs(text: &str) -> Result<usize, String> {
    match text.parse::<usize>() {
        Ok(0) => Ok(std::thread::available_parallelism().map_or(1, |n| n.get())),
        Ok(n) => Ok(n),
        Err(_) => Err(format!("`--jobs` needs a number, not `{}`", text)),
    }
}

/// The time limit of a test: `--timeout=seconds`, a positive number.
fn parse_timeout(text: &str) -> Result<std::time::Duration, String> {
    match text.parse::<f64>() {
        Ok(s) if s > 0.0 && s < 1e9 => Ok(std::time::Duration::from_secs_f64(s)),
        _ => Err(format!("`--timeout` needs a positive number of seconds, not `{}`", text)),
    }
}

fn invalid_value(option: &str, value: &str, expected: &str) -> String {
    format!("invalid value `{}` for {} (expected {})", value, option, expected)
}

/// An argument of gamma itself, which must be text.
fn text_arg(arg: std::ffi::OsString) -> Result<String, String> {
    arg.into_string().map_err(|a| format!("the argument `{}` is not valid UTF-8", a.to_string_lossy()))
}

/// The bytes of an argument for the program: exact on Unix, where arguments are bytes.
fn arg_bytes(arg: &std::ffi::OsStr) -> Vec<u8> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        arg.as_bytes().to_vec()
    }
    #[cfg(not(unix))]
    arg.to_string_lossy().into_owned().into_bytes()
}

fn parse_args() -> Result<Options, String> {
    let mut args = std::env::args_os().skip(1);
    let command = text_arg(args.next().ok_or_else(|| "missing command".to_string())?)?;
    let bench = command == "bench";
    let mut opts = Options {
        command,
        file: None,
        program_args: vec![],
        format: DiagFormat::Human,
        dump: false,
        // Benchmarks are about speed: compiled, without checks, unless asked otherwise.
        c_backend: bench,
        cranelift: false,
        output: None,
        mode: if bench { gamma::interp::Mode::Fast } else { gamma::interp::Mode::Debug },
        debug_info: false,
        filter: None,
        all: false,
        package: None,
        show_output: false,
        jobs: 1,
        timeout: None,
        time: false,
        coverage: None,
        target: None,
        offline: false,
        locked: false,
        quiet: false,
        profile: None,
        mode_given: false,
        debug_info_given: false,
        manifest_cflags: vec![],
        manifest_ldflags: vec![],
        lint: Default::default(),
    };
    let mut args = args.peekable();
    while let Some(arg) = args.next() {
        // What follows the file of `gamma run` is the program's, whatever its bytes are.
        if opts.file.is_some() && opts.command == "run" {
            opts.program_args.push(arg);
            continue;
        }
        let arg = text_arg(arg)?;
        match arg.as_str() {
            "--dump" => opts.dump = true,
            "--offline" => opts.offline = true,
            "--locked" => opts.locked = true,
            "--quiet" => opts.quiet = true,
            "--all" => opts.all = true,
            "--show-output" => opts.show_output = true,
            "--time" => opts.time = true,
            "--coverage" => opts.coverage = Some(None),
            a if a.starts_with("--coverage=") => {
                opts.coverage = Some(Some(PathBuf::from(&a["--coverage=".len()..])));
            }
            "-j" | "--jobs" => {
                let n = text_arg(args.next().ok_or_else(|| format!("`{}` needs a number", arg))?)?;
                opts.jobs = parse_jobs(&n)?;
            }
            a if a.starts_with("--timeout=") => opts.timeout = Some(parse_timeout(&a["--timeout=".len()..])?),
            a if a.starts_with("--jobs=") => opts.jobs = parse_jobs(&a["--jobs=".len()..])?,
            "--backend=c" => (opts.c_backend, opts.cranelift) = (true, false),
            "--backend=cranelift" => (opts.c_backend, opts.cranelift) = (true, true),
            "--backend=interp" => (opts.c_backend, opts.cranelift) = (false, false),
            a if a.starts_with("--backend=") => {
                return Err(invalid_value("--backend", &a["--backend=".len()..], "c, cranelift or interp"));
            }
            "-o" => opts.output = Some(PathBuf::from(args.next().ok_or_else(|| "`-o` needs a file name".to_string())?)),
            "-g" => {
                opts.debug_info = true;
                opts.debug_info_given = true;
            }
            a if a.starts_with("--mode=") => {
                opts.mode = match &a["--mode=".len()..] {
                    "debug" => gamma::interp::Mode::Debug,
                    "checked" => gamma::interp::Mode::Checked,
                    "safe" => gamma::interp::Mode::Safe,
                    "fast" => gamma::interp::Mode::Fast,
                    v => return Err(invalid_value("--mode", v, "debug, checked, safe or fast")),
                };
                opts.mode_given = true;
            }
            a if a.starts_with("--profile=") => {
                let name = &a["--profile=".len()..];
                if name.is_empty() {
                    return Err("`--profile=` needs a profile name".to_string());
                }
                opts.profile = Some(name.to_string());
            }
            "--diag-format=human" => opts.format = DiagFormat::Human,
            "--diag-format=short" => opts.format = DiagFormat::Short,
            "--diag-format=json" => opts.format = DiagFormat::Json,
            a if a.starts_with("--diag-format=") => {
                return Err(invalid_value("--diag-format", &a["--diag-format=".len()..], "human, short or json"));
            }
            a if a.starts_with("--target=") => {
                let triple = &a["--target=".len()..];
                if triple.is_empty() {
                    return Err("`--target=` needs a target triple".to_string());
                }
                opts.target = Some(triple.to_string());
            }
            a if a.starts_with("--filter=") => opts.filter = Some(a["--filter=".len()..].to_string()),
            a if a.starts_with("--package=") => opts.package = Some(a["--package=".len()..].to_string()),
            a if opts.command == "lint" && opts.lint.parse_arg(a)? => {}
            a if a.starts_with('-') => return Err(format!("unknown option `{}`", a)),
            a => {
                if opts.file.is_some() {
                    return Err("more than one input file given".to_string());
                }
                opts.file = Some(PathBuf::from(a));
            }
        }
    }
    Ok(opts)
}

/// Lets a closed output pipe end the process quietly, as for other command-line tools
/// (`gamma test … | head`), instead of a panic on the failed write. Rust ignores
/// `SIGPIPE` by default.
#[cfg(unix)]
fn default_sigpipe() {
    unsafe extern "C" {
        fn signal(sig: i32, handler: usize) -> usize;
    }
    const SIGPIPE: i32 = 13; // the same on Linux, macOS and the BSDs
    const SIG_DFL: usize = 0;
    // SAFETY: restoring the default disposition of a signal has no preconditions.
    unsafe {
        signal(SIGPIPE, SIG_DFL);
    }
}

/// The compiler's stack. Its passes recurse over the syntax tree, so a long operator
/// chain or deeply nested code needs more than the default; the memory is only reserved.
const COMPILER_STACK: usize = 1 << 30;

fn main() -> ExitCode {
    std::thread::Builder::new()
        .name("gamma".into())
        .stack_size(COMPILER_STACK)
        .spawn(compiler_main)
        .expect("cannot start the compiler thread")
        .join()
        .unwrap_or_else(|_| {
            eprintln!("{}", gamma::ice::take_report());
            ExitCode::from(101)
        })
}

/// The command whose help `gamma <command> … --help` asks for. Options come before the
/// file, and the arguments after `gamma run <file>` belong to the program.
fn help_requested(args: &[String]) -> Option<String> {
    let cmd = args.get(1)?;
    for a in &args[2..] {
        match a.as_str() {
            "--help" | "-h" => return Some(cmd.clone()),
            _ if cmd == "run" && !a.starts_with('-') => return None,
            _ => {}
        }
    }
    None
}

/// Prints the usage, followed by the details of `topic` (a command) where it has more.
fn print_help(topic: &str) {
    print!("{}", USAGE);
    if topic == "packages" || gamma::pkg::cli::is_package_command(topic) {
        print!("\n{}", gamma::pkg::cli::USAGE);
    }
    if topic == "doc" {
        print!("\n{}", gamma::doc::USAGE);
    }
    if topic == "lint" {
        print!("\n{}", linting::USAGE);
    }
}

fn compiler_main() -> ExitCode {
    #[cfg(unix)]
    default_sigpipe();
    // Arguments are read as text (lossily here), except those that `gamma run` passes on to
    // the program; `parse_args` rejects any other that is not valid UTF-8.
    let argv: Vec<String> = std::env::args_os().map(|a| a.to_string_lossy().into_owned()).collect();
    if argv.get(1).map(String::as_str) != Some("run")
        && let Some(a) = std::env::args_os().find(|a| a.to_str().is_none())
    {
        eprintln!("gamma: the argument `{}` is not valid UTF-8", a.to_string_lossy());
        return ExitCode::from(2);
    }
    // The language server catches panics per request and keeps the default hook's log.
    if argv.get(1).map(String::as_str) != Some("lsp") {
        gamma::ice::install();
        if std::env::var_os("GAMMA_DEBUG_PANIC").is_some() {
            panic!("requested by GAMMA_DEBUG_PANIC");
        }
    }
    if let Some(cmd) = help_requested(&argv) {
        print_help(&cmd);
        return ExitCode::SUCCESS;
    }
    if argv.get(1).map(String::as_str) == Some("fmt") {
        return fmt_command(argv[2..].to_vec());
    }
    if argv.get(1).map(String::as_str) == Some("doc") {
        return gamma::doc::run(&argv[2..]);
    }
    if let Some(cmd) = argv.get(1).filter(|c| gamma::pkg::cli::is_package_command(c)) {
        return gamma::pkg::cli::run(cmd, &argv[2..]);
    }
    if argv.get(1).map(String::as_str) == Some("lsp") {
        if let Some(a) = argv.get(2) {
            eprintln!("gamma: unknown option `{}`\n\n{}", a, USAGE);
            return ExitCode::from(2);
        }
        return match gamma::lsp::run_stdio() {
            Ok(code) => ExitCode::from(code),
            Err(e) => {
                eprintln!("gamma lsp: {}", e);
                ExitCode::FAILURE
            }
        };
    }
    let mut opts = match parse_args() {
        Ok(o) => o,
        Err(e) => {
            eprintln!("gamma: {}\n\n{}", e, USAGE);
            return ExitCode::from(2);
        }
    };
    if let Err(e) = apply_build_settings(&mut opts) {
        eprintln!("gamma: {}", e);
        return ExitCode::FAILURE;
    }
    match opts.command.as_str() {
        "help" | "--help" | "-h" => {
            print_help(&opts.file.as_deref().map(|f| f.to_string_lossy().into_owned()).unwrap_or_default());
            ExitCode::SUCCESS
        }
        "version" | "--version" => {
            println!("gamma {}", env!("CARGO_PKG_VERSION"));
            ExitCode::SUCCESS
        }
        "lex" | "parse" => run_front_end(&opts),
        "check" => check(&opts),
        "fix" => fix(&opts),
        "lint" => linting::run(&opts),
        "run" => run(&opts),
        "test" => testing::run(&opts, testing::Kind::Test),
        "bench" => testing::run(&opts, testing::Kind::Bench),
        "build" => build(&opts),
        "repl" => gamma::repl::run(gamma::repl::Options {
            quiet: opts.quiet,
            mode: opts.mode,
            format: opts.format,
            preload: opts.file.clone().into_iter().collect(),
        }),
        other => {
            eprintln!("gamma: unknown command `{}`\n\n{}", other, USAGE);
            ExitCode::from(2)
        }
    }
}

/// Applies the build settings of the package that holds the input (gamma.toml's `[build]`
/// and the selected profile) under the command line: an option given there wins.
fn apply_build_settings(opts: &mut Options) -> Result<(), String> {
    if !matches!(opts.command.as_str(), "check" | "fix" | "lint" | "run" | "build" | "test" | "bench") {
        return Ok(());
    }
    let Some(path) = opts.file.clone() else { return Ok(()) };
    let Some(settings) = gamma::pkg::build::root_settings(&path, opts.profile.as_deref())? else { return Ok(()) };
    if let (false, Some(m)) = (opts.mode_given, settings.mode) {
        opts.mode = m;
    }
    if let (false, Some(d)) = (opts.debug_info_given, settings.debug_info) {
        opts.debug_info = d;
    }
    opts.manifest_cflags = settings.cflags.unwrap_or_default();
    opts.manifest_ldflags = settings.ldflags.unwrap_or_default();
    Ok(())
}

/// `toolchain` with the flags of gamma.toml placed before those of the environment.
fn with_manifest_flags(mut toolchain: Toolchain, opts: &Options) -> Toolchain {
    toolchain.cflags.splice(0..0, opts.manifest_cflags.iter().cloned());
    toolchain.ldflags.splice(0..0, opts.manifest_ldflags.iter().cloned());
    toolchain
}

fn run_front_end(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
    sess.debug_info = opts.debug_info;
    let mut out = String::new();
    if let Some(file) = sess.load(path) {
        if opts.command == "lex" {
            let lexed = sess.lex(file);
            out = driver::dump_tokens(&sess.sources, &lexed.tokens);
        } else {
            let ast = sess.parse(file);
            if opts.dump {
                out = driver::dump_ast(&ast);
            }
        }
    }
    print!("{}", out);
    eprint!("{}", sess.render_diagnostics(opts.format));
    if sess.diags.has_errors() { ExitCode::FAILURE } else { ExitCode::SUCCESS }
}

/// A session for `opts`, with the C toolchain from the environment (`CC`, `CFLAGS`,
/// `LDFLAGS`; for a Safe build, Fil-C). The session's target is the one the toolchain compiles for; if the
/// toolchain cannot be used it stays the host, and the error is reported when a
/// native build needs the toolchain.
fn new_session(opts: &Options) -> (Session, Result<Toolchain, String>) {
    let mut sess = Session::new();
    sess.mode = opts.mode;
    sess.debug_info = opts.debug_info;
    let toolchain = if let Some(triple) = &opts.target {
        if opts.mode == gamma::interp::Mode::Safe {
            Err("`--target` cannot be combined with `--mode=safe`, which builds for the host with Fil-C".to_string())
        } else {
            Toolchain::for_target(triple).map(|t| with_manifest_flags(t, opts)).and_then(|t| {
                sess.target = t.target_for(triple)?;
                Ok(t)
            })
        }
    } else if opts.mode == gamma::interp::Mode::Safe {
        Toolchain::for_safe_build().map(|t| with_manifest_flags(t, opts)).and_then(|t| {
            let target = t.target()?;
            if !target.filc {
                return Err(format!(
                    "Safe builds compile with the Fil-C compiler (https://fil-c.org), but the C compiler `{}` is not Fil-C",
                    t.display()
                ));
            }
            sess.target = target;
            Ok(t)
        })
    } else {
        Toolchain::from_env().map(|t| with_manifest_flags(t, opts)).and_then(|t| {
            sess.target = t.target()?;
            Ok(t)
        })
    };
    // An explicit target must be reachable: there is nothing else to fall back to.
    if let (Some(_), Err(e)) = (&opts.target, &toolchain) {
        eprintln!("gamma: {}", e);
        std::process::exit(1);
    }
    (sess, toolchain)
}

/// Reports that `command` cannot run a program for a freestanding target (§3.7.1), which has
/// no operating system to run it on: it is built to an object file and linked with the code
/// that starts it. Returns whether the session's target is freestanding.
fn refuse_freestanding(sess: &Session, command: &str) -> bool {
    if sess.target.os != "Freestanding" {
        return false;
    }
    eprintln!(
        "gamma: `gamma {command}` cannot run a program for a freestanding target (the C compiler targets no operating system); \
         build it with `gamma build`, which writes an object file to link into the firmware or kernel. \
         Tests and benchmarks run on the host: leave out --target (and unset CC) for them"
    );
    true
}

/// Fetches the git dependencies of the package that contains `input` (see `gamma::pkg`)
/// and tells the session where they are. Returns the C sources and libraries that the
/// packages add to a native build, or reports the error and returns `None`.
fn prepare_packages(opts: &Options, sess: &mut Session, input: &std::path::Path) -> Option<gamma::cgen::Extras> {
    let pkg_opts = gamma::pkg::Options { offline: opts.offline, locked: opts.locked };
    let extras = gamma::pkg::prepare(input, &pkg_opts).and_then(|res| {
        sess.packages = res.map(std::sync::Arc::new);
        gamma::pkg::build::collect(input, sess.packages.as_deref(), opts.profile.as_deref())
    });
    match extras {
        Ok(e) => Some(e),
        Err(e) => {
            eprintln!("gamma: {}", e);
            None
        }
    }
}

/// What a native build links besides the generated program: the packages' C sources,
/// compiled (or found in the cache), and their libraries.
fn native_link(
    sess: &Session,
    toolchain: &Result<Toolchain, String>,
    extras: &gamma::cgen::Extras,
) -> Result<gamma::cgen::Link, String> {
    if extras.is_empty() {
        return Ok(gamma::cgen::Link::default());
    }
    let toolchain = toolchain.as_ref().map_err(|e| format!("gamma: {}", e))?;
    gamma::cgen::link_inputs(extras, &c_options(sess), toolchain)
}

fn check(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    // Only the target matters to checking.
    let (mut sess, _) = new_session(opts);
    if prepare_packages(opts, &mut sess, path).is_none() {
        return ExitCode::FAILURE;
    }
    let ok = gamma::sema::analyze(&mut sess, path).is_some();
    eprint!("{}", sess.render_diagnostics(opts.format));
    if ok { ExitCode::SUCCESS } else { ExitCode::FAILURE }
}

/// The directory of the files that `gamma fix` may change and `gamma lint` reports on:
/// the input's package, or outside a package, the input's directory; never the standard
/// library or other packages.
fn own_files(path: &std::path::Path) -> Result<PathBuf, String> {
    match gamma::pkg::find_root(path) {
        Some(root) => Ok(root),
        None => match gamma::source::canonicalize(path) {
            Ok(p) if p.is_dir() => Ok(p),
            Ok(p) => Ok(p.parent().map(|d| d.to_path_buf()).unwrap_or(p)),
            Err(e) => Err(format!("cannot read `{}`: {}", path.display(), e)),
        },
    }
}

/// `gamma fix`: checks the program and applies the fixes of its diagnostics, again until
/// none is left to apply (a fix can let checking go further), then reports what is left.
fn fix(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let scope = match own_files(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("gamma: {}", e);
            return ExitCode::FAILURE;
        }
    };
    let std_root = gamma::source::canonical(&gamma::sema::std_root());
    let (mut fixes, mut files) = (0, std::collections::BTreeSet::new());
    // Each round applies fixes that do not overlap; ten rounds are plenty for real code.
    for round in 0.. {
        let (mut sess, _) = new_session(opts);
        if prepare_packages(opts, &mut sess, path).is_none() {
            return ExitCode::FAILURE;
        }
        let ok = gamma::sema::analyze(&mut sess, path).is_some();
        let editable = |f: gamma::source::FileId| {
            let p = gamma::source::canonical(&sess.sources.file(f).path);
            p.starts_with(&scope) && !p.starts_with(&std_root)
        };
        let (plan, n) = if round < 10 { gamma::fix::plan(sess.diags.iter(), editable) } else { Default::default() };
        if n == 0 {
            eprint!("{}", sess.render_diagnostics(opts.format));
            if fixes > 0 && opts.format != DiagFormat::Json {
                let s = |k: usize| if k == 1 { "" } else { "s" };
                eprintln!(
                    "gamma: applied {} fix{} in {} file{}",
                    fixes,
                    if fixes == 1 { "" } else { "es" },
                    files.len(),
                    s(files.len())
                );
            }
            return if ok { ExitCode::SUCCESS } else { ExitCode::FAILURE };
        }
        for (f, edits) in &plan {
            let file = sess.sources.file(*f);
            let text = gamma::fix::apply(&file.src, edits);
            if let Err(e) = std::fs::write(&file.path, text) {
                eprintln!("gamma: cannot write `{}`: {}", file.path.display(), e);
                return ExitCode::FAILURE;
            }
            files.insert(file.path.clone());
        }
        fixes += n;
    }
    unreachable!()
}

fn run(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let (mut sess, toolchain) = new_session(opts);
    if refuse_freestanding(&sess, "run") {
        return ExitCode::FAILURE;
    }
    let Some(extras) = prepare_packages(opts, &mut sess, path) else { return ExitCode::FAILURE };
    let prog = gamma::sema::analyze(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    let cfg =
        gamma::interp::RunConfig { target: prog.target.clone(), mode: opts.mode, step_budget: None, deadline: None };
    // The interpreter calls C itself where it can (§19.2); it cannot run assembly, or foreign
    // code it cannot find or describe to libffi, so such programs run natively. The C sources
    // of the packages are loaded as a shared library, unless the program is not for the host
    // (Safe builds, cross targets), which runs natively with them linked in.
    let native =
        opts.c_backend || (!extras.is_empty() && (opts.mode == gamma::interp::Mode::Safe || opts.target.is_some()));
    if !native
        && !extras.is_empty()
        && let Ok(t) = &toolchain
        && let Err(e) = gamma::cgen::load_for_interpreter(&extras, &c_options(&sess), t)
    {
        eprintln!("gamma: {}", e);
        return ExitCode::FAILURE;
    }
    if native || gamma::interp::foreign_supported(&prog, &cfg).is_err() {
        return run_native(&sess, &prog, &toolchain, &extras, path, opts);
    }
    gamma::interp::set_owns_process(true);
    let mut args = vec![path.display().to_string().into_bytes()];
    args.extend(opts.program_args.iter().map(|a| arg_bytes(a)));
    let mut stdout = std::io::stdout();
    let mut stderr = std::io::stderr();
    match gamma::interp::run_main(&prog, &cfg, &args, &mut stdout, &mut stderr) {
        gamma::interp::Outcome::Exit(code) => ExitCode::from(code),
        gamma::interp::Outcome::Trap(t) => {
            use std::io::Write;
            let _ = stdout.flush();
            eprint!("trap: {}", trap_report(&sess, &prog, &t));
            // The status of a process terminated by `abort` (SIGABRT).
            ExitCode::from(134)
        }
    }
}

/// The source location of `span` as `file:line:col`, if it has one.
fn location(sess: &Session, span: gamma::source::Span) -> Option<String> {
    if span == gamma::source::Span::DUMMY {
        return None;
    }
    let f = sess.sources.file(span.file);
    let (line, col) = f.line_col(span.lo);
    Some(format!("{}:{}:{}", f.path.display(), line, col))
}

/// A trap's message and where it happened: its location and, in Debug builds, the
/// calls leading to it (the same report the C runtime prints).
fn trap_report(sess: &Session, prog: &gamma::tir::Program, trap: &gamma::interp::Trap) -> String {
    let mut s = format!("{}\n", trap.message);
    let name = |i: usize| trap.trace.get(i).and_then(|(f, _)| prog.backtrace_name(*f, &sess.sources));
    if let Some(at) = location(sess, trap.span) {
        s.push_str(&format!("  at {}", at));
        if let Some(n) = name(0) {
            s.push_str(&format!(" in {}", n));
        }
        s.push('\n');
    }
    // Each call site lies in the function that the next (outer) entry called.
    let callers: Vec<String> = (0..trap.trace.len())
        .filter_map(|i| Some(format!("  called from {} in {}\n", location(sess, trap.trace[i].1)?, name(i + 1)?)))
        .collect();
    // Past BACKTRACE_MAX callers, only the first and last BACKTRACE_MAX / 2, with a line
    // that does not count the calls in between (a stack overflow reaches different depths
    // on different back ends).
    const BACKTRACE_MAX: usize = 20;
    let n = callers.len();
    for (i, line) in callers.iter().enumerate() {
        if n <= BACKTRACE_MAX || i < BACKTRACE_MAX / 2 || i >= n - BACKTRACE_MAX / 2 {
            s.push_str(line);
        } else if i == BACKTRACE_MAX / 2 {
            s.push_str("  ... more calls\n");
        }
    }
    s
}

/// The file name of an executable called `stem`: it has the extension `.exe` when the
/// toolchain builds for Windows (which cannot run a program without it), and `.wasm` when
/// it builds a WebAssembly module for WASI (which says how to run it).
fn native_exe_name(stem: &str, toolchain: &Result<Toolchain, String>) -> String {
    match toolchain.as_ref().ok().and_then(|t| t.target().ok()).map(|t| t.os) {
        Some("Windows") => format!("{stem}.exe"),
        Some("Wasi") => format!("{stem}.wasm"),
        _ => stem.to_string(),
    }
}

/// The command that runs the executable `exe`: behind the `GAMMA_RUNNER` command if
/// that is set (a malformed value ends the process), else behind a WebAssembly runtime for
/// a `.wasm` module (see [`gamma::toolchain::runner_for`]), and with `arg0` as the
/// program's name if given and no runner is in the way.
#[cfg_attr(not(unix), allow(unused_variables, unused_mut))]
fn runner_command(exe: &std::path::Path, arg0: Option<&std::path::Path>) -> std::process::Command {
    let runner = gamma::toolchain::runner_for(exe).unwrap_or_else(|e| {
        eprintln!("gamma: {}", e);
        std::process::exit(2)
    });
    match runner.split_first() {
        Some((prog, args)) => {
            let mut cmd = std::process::Command::new(prog);
            cmd.args(args).arg(exe);
            cmd
        }
        None => {
            let mut cmd = std::process::Command::new(exe);
            #[cfg(unix)]
            if let Some(name) = arg0 {
                std::os::unix::process::CommandExt::arg0(&mut cmd, name);
            }
            cmd
        }
    }
}

/// The standard error of a failed native test as the interpreter reports it: a
/// trap's message and location are in it, without the `trap: ` prefix.
fn native_stderr(err: &str) -> String {
    let err = gamma::toolchain::strip_wasm_trap_report(err);
    const PREFIX: &str = "trap: ";
    match err.rfind(PREFIX) {
        Some(i) if i == 0 || err.as_bytes()[i - 1] == b'\n' => {
            format!("{}{}", &err[..i], &err[i + PREFIX.len()..])
        }
        _ => err.to_string(),
    }
}

/// The C back end options for the session's mode.
fn c_options(sess: &Session) -> gamma::cgen::Options {
    gamma::cgen::Options {
        checks: sess.mode != gamma::interp::Mode::Fast,
        debug: sess.mode == gamma::interp::Mode::Debug,
        debug_info: sess.debug_info,
    }
}

/// The message for a construct the native back end cannot translate.
fn cgen_error(sess: &Session, u: gamma::cgen::Unsupported) -> String {
    match location(sess, u.span) {
        Some(at) => format!("{}: error: {}", at, u.message),
        None => format!("error: {}", u.message),
    }
}

/// Translates the program to C (or with `cranelift` to machine code) and compiles it to
/// `exe`.
fn compile_native(
    sess: &Session,
    prog: &gamma::tir::Program,
    toolchain: &Result<Toolchain, String>,
    link: &gamma::cgen::Link,
    exe: &std::path::Path,
    cranelift: bool,
) -> Result<(), String> {
    let toolchain = toolchain.as_ref().map_err(|e| format!("gamma: {}", e))?;
    let Some(entry) = prog.main else { return Err("the program has no `main` function".into()) };
    if cranelift {
        let out = gamma::clif::generate(prog, &sess.sources, &[entry], false, &c_options(sess))
            .map_err(|u| cgen_error(sess, u))?;
        return gamma::clif::compile(&out, toolchain, link, exe);
    }
    let out = gamma::cgen::generate(prog, &sess.sources, entry, &c_options(sess)).map_err(|u| cgen_error(sess, u))?;
    gamma::cgen::compile(&out, toolchain, link, exe).map(|_| ())
}

/// Creates a temporary directory for a native build, reporting a failure.
fn native_dir(what: &str) -> Option<PathBuf> {
    // A directory no other process uses: created here, never reused (a process id alone
    // can repeat across sandboxes sharing one temporary directory).
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    for attempt in 0..100u32 {
        let dir = std::env::temp_dir().join(format!("gamma-{}-{}-{:x}-{}", what, std::process::id(), nanos, attempt));
        match gamma::cgen::private_dir_builder().create(&dir) {
            Ok(()) => return Some(dir),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(e) => {
                eprintln!("gamma: cannot create {}: {}", dir.display(), e);
                return None;
            }
        }
    }
    eprintln!("gamma: cannot create a temporary directory in {}", std::env::temp_dir().display());
    None
}

fn build(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let (mut sess, toolchain) = new_session(opts);
    if let Err(e) = &toolchain {
        eprintln!("gamma: {}", e);
        return ExitCode::FAILURE;
    }
    let Some(extras) = prepare_packages(opts, &mut sess, path) else { return ExitCode::FAILURE };
    let freestanding = sess.target.os == "Freestanding";
    if freestanding && !extras.is_empty() {
        eprintln!(
            "gamma: a program for a freestanding target builds to an object file only, so the C sources and libraries \
             of its packages are not linked in; link them with the object yourself"
        );
        return ExitCode::FAILURE;
    }
    let prog = gamma::sema::analyze(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    if freestanding {
        return build_object(&sess, &prog, &toolchain, path, opts);
    }
    // An output name without an extension gets `.exe` on Windows, as the C compiler would
    // add it there anyway.
    let exe = match &opts.output {
        Some(o) if o.extension().is_none() && o.file_name().is_some() => {
            o.with_file_name(native_exe_name(&o.file_name().unwrap().to_string_lossy(), &toolchain))
        }
        Some(o) => o.clone(),
        None => {
            let stem = path.file_stem().map_or("main".into(), |s| s.to_string_lossy().into_owned());
            PathBuf::from(native_exe_name(&stem, &toolchain))
        }
    };
    let built = native_link(&sess, &toolchain, &extras)
        .and_then(|link| compile_native(&sess, &prog, &toolchain, &link, &exe, opts.cranelift));
    // A build with debug information comes with the lldb data formatters.
    let built =
        built.and_then(|()| if sess.debug_info { gamma::cgen::debug::write_lldb_formatters(&exe) } else { Ok(()) });
    match built {
        Ok(()) => ExitCode::SUCCESS,
        Err(msg) => {
            eprintln!("{}", msg);
            ExitCode::FAILURE
        }
    }
}

/// `gamma build` for a freestanding target (§3.7.1): the object file `<name>.o` (or `-o`)
/// with the program's exported functions and the runtime support they use.
fn build_object(
    sess: &Session,
    prog: &gamma::tir::Program,
    toolchain: &Result<Toolchain, String>,
    path: &std::path::Path,
    opts: &Options,
) -> ExitCode {
    let obj = opts.output.clone().unwrap_or_else(|| {
        PathBuf::from(format!("{}.o", path.file_stem().map_or("main".into(), |s| s.to_string_lossy().into_owned())))
    });
    let built = toolchain.as_ref().map_err(|e| format!("gamma: {e}")).and_then(|t| {
        if prog.exports.is_empty() {
            return Err(
                "gamma: the program exports no function (`#export(\"symbol\")`, §19.4), so its object file would have no entry point"
                    .to_string(),
            );
        }
        let out = gamma::cgen::generate_object(prog, &sess.sources, &c_options(sess)).map_err(|u| cgen_error(sess, u))?;
        gamma::cgen::compile(&out, t, &gamma::cgen::Link::default(), &obj).map(|_| ())
    });
    match built {
        Ok(()) => ExitCode::SUCCESS,
        Err(msg) => {
            eprintln!("{}", msg);
            ExitCode::FAILURE
        }
    }
}

/// The exit status a shell reports for a finished child: its exit code, or 128 plus the
/// number of the signal that killed it. A trap gives 134 (`SIGABRT`), including one
/// raised by a trap instruction (`SIGTRAP`, `SIGILL`), as Fil-C does on a memory error.
fn exit_status_code(status: std::process::ExitStatus) -> u8 {
    #[cfg(unix)]
    if let Some(sig) = std::os::unix::process::ExitStatusExt::signal(&status) {
        const SIGILL: i32 = 4;
        const SIGTRAP: i32 = 5;
        return if sig == SIGILL || sig == SIGTRAP { 134 } else { (128 + sig) as u8 };
    }
    // On Windows a crash is an NT status such as 0xC0000409 (a fast fail) or 0xC0000005.
    match status.code() {
        Some(c) if (c as u32) >= 0xC000_0000 => 134,
        Some(c) => c as u8,
        None => 134,
    }
}

/// `gamma run --backend=c`: compiles to a temporary executable and runs it.
fn run_native(
    sess: &Session,
    prog: &gamma::tir::Program,
    toolchain: &Result<Toolchain, String>,
    extras: &gamma::cgen::Extras,
    path: &std::path::Path,
    opts: &Options,
) -> ExitCode {
    let link = match native_link(sess, toolchain, extras) {
        Ok(l) => l,
        Err(msg) => {
            eprintln!("{}", msg);
            return ExitCode::FAILURE;
        }
    };
    // The executable comes from the cache when the same program was built before.
    let cached = match if opts.cranelift { Ok(None) } else { cached_native(sess, prog, toolchain, &link) } {
        Ok(c) => c,
        Err(msg) => {
            eprintln!("{}", msg);
            return ExitCode::FAILURE;
        }
    };
    let dir = match cached {
        Some(_) => None,
        None => match native_dir("run") {
            Some(d) => Some(d),
            None => return ExitCode::FAILURE,
        },
    };
    let (exe, result) = match (&cached, &dir) {
        (Some(exe), _) => (exe.clone(), Ok(())),
        (None, Some(dir)) => {
            let exe = dir.join(native_exe_name("prog", toolchain));
            let r = compile_native(sess, prog, toolchain, &link, &exe, opts.cranelift);
            (exe, r)
        }
        (None, None) => unreachable!(),
    };
    let code = match result {
        Ok(()) => {
            // The program sees the source path as its name, as with the interpreter
            // (unless a runner starts it, which names it by its path).
            let mut cmd = runner_command(&exe, Some(path));
            let status = cmd.args(&opts.program_args).status();
            match status {
                Ok(s) => ExitCode::from(exit_status_code(s)),
                Err(e) => {
                    eprintln!("gamma: cannot run {}: {}", exe.display(), e);
                    ExitCode::FAILURE
                }
            }
        }
        Err(msg) => {
            eprintln!("{}", msg);
            ExitCode::FAILURE
        }
    };
    if let Some(dir) = dir {
        let _ = std::fs::remove_dir_all(&dir);
    }
    code
}

/// The program's executable from the build cache (built into it if missing), or `None`
/// if there is no cache.
fn cached_native(
    sess: &Session,
    prog: &gamma::tir::Program,
    toolchain: &Result<Toolchain, String>,
    link: &gamma::cgen::Link,
) -> Result<Option<PathBuf>, String> {
    let toolchain = toolchain.as_ref().map_err(|e| format!("gamma: {}", e))?;
    let Some(entry) = prog.main else { return Err("the program has no `main` function".into()) };
    let out = gamma::cgen::generate(prog, &sess.sources, entry, &c_options(sess)).map_err(|u| cgen_error(sess, u))?;
    gamma::cgen::cached_program(&out, toolchain, link)
}

/// `gamma fmt [--check] <file or directory>... (`-` filters stdin to stdout)`
fn fmt_command(args: Vec<String>) -> ExitCode {
    let mut check = false;
    let mut paths = Vec::new();
    for a in args {
        match a.as_str() {
            "--check" => check = true,
            s if s.starts_with('-') && s != "-" => {
                eprintln!("gamma: unknown option `{}`\n\n{}", s, USAGE);
                return ExitCode::from(2);
            }
            _ => paths.push(PathBuf::from(a)),
        }
    }
    if paths.is_empty() {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    }
    if paths == [PathBuf::from("-")] {
        // Filter mode: standard input to standard output.
        let mut src = String::new();
        if let Err(e) = std::io::Read::read_to_string(&mut std::io::stdin(), &mut src) {
            eprintln!("gamma: <stdin>: {}", e);
            return ExitCode::FAILURE;
        }
        return match gamma::fmt::format_source(&src) {
            Ok(out) => {
                print!("{}", out);
                ExitCode::SUCCESS
            }
            Err(e) => {
                eprintln!("<stdin>:{}", e);
                ExitCode::FAILURE
            }
        };
    }
    let mut files = Vec::new();
    for p in &paths {
        if let Err(e) = collect_gm_files(p, &mut files) {
            eprintln!("gamma: {}: {}", p.display(), e);
            return ExitCode::FAILURE;
        }
    }
    let mut failed = false;
    for f in &files {
        let src = match std::fs::read_to_string(f) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("gamma: {}: {}", f.display(), e);
                failed = true;
                continue;
            }
        };
        match gamma::fmt::format_source(&src) {
            Ok(out) if out != src => {
                if check {
                    println!("{}", f.display());
                    failed = true;
                } else if let Err(e) = std::fs::write(f, out) {
                    eprintln!("gamma: {}: {}", f.display(), e);
                    failed = true;
                }
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("{}:{}", f.display(), e);
                failed = true;
            }
        }
    }
    if failed { ExitCode::FAILURE } else { ExitCode::SUCCESS }
}

/// Adds `path` (a file) or every `.gm` file below it (a directory) to `out`, in name order.
/// A file named explicitly is always included; the walk of a directory skips hidden
/// directories (`.git`, …) and the `target` directory of a package.
fn collect_gm_files(path: &std::path::Path, out: &mut Vec<PathBuf>) -> std::io::Result<()> {
    if path.is_dir() { collect_dir(path, out) } else { std::fs::metadata(path).map(|_| out.push(path.to_path_buf())) }
}

fn collect_dir(dir: &std::path::Path, out: &mut Vec<PathBuf>) -> std::io::Result<()> {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(dir)?.map(|e| e.map(|e| e.path())).collect::<Result<_, _>>()?;
    entries.sort();
    let package_root = dir.join("gamma.toml").is_file();
    for e in entries {
        let name = e.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        if e.is_dir() {
            if name.starts_with('.') || (package_root && name == "target") {
                continue;
            }
            collect_dir(&e, out)?;
        } else if e.extension().is_some_and(|x| x == "gm") {
            out.push(e);
        }
    }
    Ok(())
}
