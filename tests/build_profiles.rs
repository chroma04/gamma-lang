//! Tests of the build settings of gamma.toml: `[build]`, `[profile.<name>]`, C sources
//! compiled into the program, libraries and their directories. They need a C compiler
//! (the interpreter loads the C sources as a shared library) and, for the git
//! dependency, `git`. Each test has a directory and a cache directory of its own.

mod common;

use std::path::{Path, PathBuf};
use std::process::Command;

fn have(tool: &str) -> bool {
    Command::new(tool).arg("--version").output().is_ok_and(|o| o.status.success())
}

macro_rules! need_cc {
    () => {
        if !have("cc") {
            eprintln!("skipped: no C compiler");
            return;
        }
    };
}

struct World {
    root: PathBuf,
}

struct Out {
    ok: bool,
    stdout: String,
    stderr: String,
}

impl Out {
    #[track_caller]
    fn success(self) -> Out {
        assert!(self.ok, "command failed\nstdout: {}\nstderr: {}", self.stdout, self.stderr);
        self
    }

    #[track_caller]
    fn failure(self) -> Out {
        assert!(!self.ok, "command succeeded\nstdout: {}\nstderr: {}", self.stdout, self.stderr);
        self
    }
}

impl World {
    fn new(name: &str) -> World {
        let root = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("build_profiles").join(name);
        let _ = gamma::pkg::git::remove_tree(&root);
        std::fs::create_dir_all(&root).unwrap();
        World { root: gamma::source::canonicalize(&root).unwrap() }
    }

    fn path(&self, p: &str) -> PathBuf {
        self.root.join(p)
    }

    fn cache(&self) -> PathBuf {
        self.root.join("cache")
    }

    fn write(&self, rel: &str, text: &str) {
        let p = self.path(rel);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, text).unwrap();
    }

    fn gamma_env(&self, dir: &str, args: &[&str], env: &[(&str, &str)]) -> Out {
        let mut cmd = common::gamma();
        cmd.args(args).current_dir(self.path(dir)).env("GAMMA_CACHE_DIR", self.cache());
        cmd.env_remove("CFLAGS").env_remove("LDFLAGS").env_remove("CC");
        for (k, v) in env {
            cmd.env(k, v);
        }
        let out = cmd.output().unwrap();
        Out {
            ok: out.status.success(),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        }
    }

    fn gamma(&self, dir: &str, args: &[&str]) -> Out {
        self.gamma_env(dir, args, &[])
    }

    /// The names of the files in the cache's `native` directory with the extension `ext`.
    fn native(&self, ext: &str) -> Vec<String> {
        let mut v: Vec<String> = std::fs::read_dir(self.cache().join("native"))
            .map(|rd| {
                rd.flatten()
                    .map(|e| e.file_name().to_string_lossy().into_owned())
                    .filter(|n| n.ends_with(ext))
                    .collect()
            })
            .unwrap_or_default();
        v.sort();
        v
    }
}

const SHIM_H: &str = "int shim_add(int a, int b);\n";

fn shim_c(extra: i32) -> String {
    format!(
        "#include \"shim.h\"\n#ifndef BONUS\n#define BONUS 0\n#endif\nint shim_add(int a, int b) {{ return a + b + BONUS + {}; }}\n",
        extra
    )
}

const SHIM_MAIN: &str = "\
c :: import \"std/c\"

shim :: foreign \"c\" {
    shim_add :: fn(a: c.c_int, b: c.c_int) -> c.c_int needs not
}

main :: fn() needs Out + Sys {
    print(\"{}\\n\", shim.shim_add(2, 3))
}

test \"the C source is linked\" {
    expect(shim.shim_add(2, 3) >= 5)
}
";

const MODE_MAIN: &str = "\
mode_name :: fn() -> string {
    when target.mode is .Debug { return \"debug\" }
        else when target.mode is .Checked { return \"checked\" }
        else when target.mode is .Safe { return \"safe\" }
        else { return \"fast\" }
}

main :: fn() needs Out {
    print(\"{}\\n\", mode_name())
}
";

/// A package `app` with the C source `c/shim.c` and its header.
fn shim_package(w: &World, build: &str) {
    w.write("app/gamma.toml", &format!("[package]\nname = \"app\"\n\n{}", build));
    w.write("app/c/include/shim.h", SHIM_H);
    w.write("app/c/shim.c", &shim_c(0));
    w.write("app/main.gm", SHIM_MAIN);
}

const SHIM_BUILD: &str = "[build]\nc-sources = [\"c/shim.c\"]\ninclude-dirs = [\"c/include\"]\n";

#[test]
fn c_sources_work_with_every_command() {
    need_cc!();
    let w = World::new("c_sources");
    shim_package(&w, SHIM_BUILD);
    // The interpreter loads the C source as a shared library.
    assert_eq!(w.gamma("app", &["run", "main.gm"]).success().stdout, "5\n");
    let libs = w.native(".so").len() + w.native(".dylib").len();
    assert_eq!(libs, 1);
    assert_eq!(w.native(".o").len(), 1);
    // The C back end links the object, and so does `build`.
    assert_eq!(w.gamma("app", &["run", "--backend=c", "main.gm"]).success().stdout, "5\n");
    w.gamma("app", &["build", "main.gm", "-o", "app.out"]).success();
    let built = Command::new(w.path("app/app.out")).output().unwrap();
    assert_eq!(String::from_utf8_lossy(&built.stdout), "5\n");
    // Tests of modules that call C run natively with the C sources.
    let out = w.gamma("app", &["test", "main.gm"]).success();
    assert!(out.stdout.contains("1 passed") || out.stdout.contains("ok"), "{}", out.stdout);
    // One object served all of them.
    assert_eq!(w.native(".o").len(), 1);
    // `check` does not need the C compiler's work.
    w.gamma("app", &["check", "main.gm"]).success();
}

#[test]
fn c_objects_are_cached_and_follow_changes() {
    need_cc!();
    let w = World::new("c_cache");
    shim_package(&w, SHIM_BUILD);
    w.gamma("app", &["run", "--backend=c", "main.gm"]).success();
    let first = w.native(".o");
    assert_eq!(first.len(), 1);
    // Nothing changed: the object is reused, not rebuilt.
    assert_eq!(w.gamma("app", &["run", "--backend=c", "main.gm"]).success().stdout, "5\n");
    assert_eq!(w.native(".o"), first);
    // A changed source gives a new object, and the cached executable follows.
    w.write("app/c/shim.c", &shim_c(10));
    assert_eq!(w.gamma("app", &["run", "--backend=c", "main.gm"]).success().stdout, "15\n");
    assert_eq!(w.gamma("app", &["run", "main.gm"]).success().stdout, "15\n");
    assert_eq!(w.native(".o").len(), 2);
    // So does a changed header.
    w.write("app/c/include/shim.h", "#define BONUS 1000\nint shim_add(int a, int b);\n");
    assert_eq!(w.gamma("app", &["run", "--backend=c", "main.gm"]).success().stdout, "1015\n");
    assert_eq!(w.gamma("app", &["run", "main.gm"]).success().stdout, "1015\n");
    assert_eq!(w.native(".o").len(), 3);
    // And a changed flag.
    w.write("app/c/include/shim.h", SHIM_H);
    let out = w.gamma_env("app", &["run", "--backend=c", "main.gm"], &[("CFLAGS", "-DBONUS=7")]).success();
    assert_eq!(out.stdout, "22\n");
}

#[test]
fn c_compile_errors_are_reported() {
    need_cc!();
    let w = World::new("c_errors");
    shim_package(&w, SHIM_BUILD);
    w.write("app/c/shim.c", "#include \"shim.h\"\nint shim_add(int a, int b) { return a +; }\n");
    for args in [&["run", "main.gm"][..], &["run", "--backend=c", "main.gm"], &["build", "main.gm"]] {
        let out = w.gamma("app", args).failure();
        assert!(out.stderr.contains("shim.c"), "{:?}: {}", args, out.stderr);
    }
    w.write("app/c/shim.c", "#include \"missing.h\"\n");
    assert!(w.gamma("app", &["run", "main.gm"]).failure().stderr.contains("missing.h"));
    // A listed file that does not exist.
    shim_package(&w, "[build]\nc-sources = [\"c/nope.c\"]\n");
    let out = w.gamma("app", &["run", "main.gm"]).failure();
    assert!(out.stderr.contains("c/nope.c") && out.stderr.contains("not a file"), "{}", out.stderr);
}

#[test]
fn profiles_and_precedence() {
    need_cc!();
    let w = World::new("profiles");
    let manifest = "\
[build]
mode = \"checked\"
c-sources = [\"c/shim.c\"]
include-dirs = [\"c/include\"]

[profile.release]
mode = \"fast\"
cflags = [\"-DBONUS=100\"]

[profile.dev]
mode = \"debug\"
";
    shim_package(&w, manifest);
    w.write("app/mode.gm", MODE_MAIN);
    // [build] over the built-in defaults.
    assert_eq!(w.gamma("app", &["run", "mode.gm"]).success().stdout, "checked\n");
    // The profile over [build].
    assert_eq!(w.gamma("app", &["run", "--profile=release", "mode.gm"]).success().stdout, "fast\n");
    assert_eq!(w.gamma("app", &["run", "--profile=dev", "mode.gm"]).success().stdout, "debug\n");
    // The command line over the profile.
    assert_eq!(
        w.gamma("app", &["run", "--profile=release", "--mode=checked", "mode.gm"]).success().stdout,
        "checked\n"
    );
    assert_eq!(w.gamma("app", &["run", "--profile=release", "--mode=debug", "mode.gm"]).success().stdout, "debug\n");
    assert_eq!(w.gamma("app", &["run", "--mode=fast", "mode.gm"]).success().stdout, "fast\n");
    // The same for build, test and check.
    w.gamma("app", &["build", "--profile=release", "mode.gm", "-o", "m.out"]).success();
    assert_eq!(String::from_utf8_lossy(&Command::new(w.path("app/m.out")).output().unwrap().stdout), "fast\n");
    w.gamma("app", &["check", "--profile=release", "mode.gm"]).success();
    // Flags of the profile reach the C compiler, in the interpreter and natively.
    assert_eq!(w.gamma("app", &["run", "main.gm"]).success().stdout, "5\n");
    assert_eq!(w.gamma("app", &["run", "--profile=release", "main.gm"]).success().stdout, "105\n");
    assert_eq!(w.gamma("app", &["run", "--profile=release", "--backend=c", "main.gm"]).success().stdout, "105\n");
    assert!(w.gamma("app", &["test", "--profile=release", "main.gm"]).ok);
    // The environment's CFLAGS come after those of the manifest and win.
    let out = w.gamma_env("app", &["run", "--profile=release", "main.gm"], &[("CFLAGS", "-DBONUS=7")]).success();
    assert_eq!(out.stdout, "12\n");
    // Unknown profiles name the known ones.
    let out = w.gamma("app", &["run", "--profile=nope", "mode.gm"]).failure();
    assert!(
        out.stderr.contains("unknown profile `nope`") && out.stderr.contains("release") && out.stderr.contains("dev")
    );
    // Without a package there is nothing to select from.
    w.write("loose/main.gm", MODE_MAIN);
    let out = w.gamma("loose", &["run", "--profile=release", "main.gm"]).failure();
    assert!(out.stderr.contains("needs a package"), "{}", out.stderr);
    assert_eq!(w.gamma("loose", &["run", "main.gm"]).success().stdout, "debug\n");
    // A profile on a package without profiles.
    w.write("plain/gamma.toml", "[package]\nname = \"plain\"\n");
    w.write("plain/main.gm", MODE_MAIN);
    let out = w.gamma("plain", &["run", "--profile=x", "main.gm"]).failure();
    assert!(out.stderr.contains("defines no"), "{}", out.stderr);
}

#[test]
fn debug_info_comes_from_the_manifest() {
    let w = World::new("debug_info");
    w.write("app/gamma.toml", "[package]\nname = \"app\"\n[build]\ndebug-info = true\nmode = \"debug\"\n");
    w.write("app/main.gm", MODE_MAIN);
    w.gamma("app", &["run", "main.gm"]).success();
    if have("cc") {
        // With debug information the executable keeps the generated C file's line map.
        w.gamma("app", &["build", "main.gm", "-o", "out"]).success();
        assert!(w.path(if cfg!(windows) { "app/out.exe" } else { "app/out" }).is_file());
    }
}

#[test]
fn manifest_errors_have_lines() {
    let w = World::new("errors");
    w.write("app/main.gm", MODE_MAIN);
    let cases = [
        ("[package]\nname = \"a\"\n[build]\nmode = \"checked\"\nspeed = 3\n", 5, "unknown key `speed`"),
        ("[package]\nname = \"a\"\n[profile.x]\ncflags = \"-O2\"\n", 4, "must be an array of strings"),
        ("[package]\nname = \"a\"\n[profile.x]\nmode = \"turbo\"\n", 4, "invalid mode `turbo`"),
        ("[package]\nname = \"a\"\n[build]\ndebug-info = \"yes\"\n", 4, "true or false"),
        ("[package]\nname = \"a\"\n[build]\nlibs = [\"-lz\"]\n", 4, "invalid library name"),
        ("[package]\nname = \"a\"\n[build]\nc-sources = [\"\"]\n", 4, "empty path"),
        ("[package]\nname = \"a\"\n[profile]\nx = 1\n", 4, "must be a table"),
        ("build = 3\n[package]\nname = \"a\"\n", 1, "`build` must be a table"),
        ("[package]\nname = \"a\"\n[profile.\"a b\"]\n", 3, "invalid profile name"),
    ];
    for (text, line, msg) in cases {
        w.write("app/gamma.toml", text);
        let out = w.gamma("app", &["run", "main.gm"]).failure();
        assert!(
            out.stderr.contains(&format!("gamma.toml:{}: ", line)) && out.stderr.contains(msg),
            "{}\n{}",
            text,
            out.stderr
        );
        assert!(w.gamma("app", &["check", "main.gm"]).stderr.contains(msg));
    }
}

/// A dependency package that wraps C: its own C source and include directory.
fn c_dependency(w: &World, dir: &str) {
    w.write(&format!("{}/gamma.toml", dir), "[package]\nname = \"wrap\"\nversion = \"1.0.0\"\n\n[build]\nc-sources = [\"native/wrap.c\"]\ninclude-dirs = [\"native\"]\nlibs = [\"m\"]\n");
    w.write(&format!("{}/native/config.h", dir), "#define WRAP_BASE 40\n");
    w.write(
        &format!("{}/native/wrap.c", dir),
        "#include \"config.h\"\n#include <math.h>\nint wrap_value(void) { return WRAP_BASE + (int)sqrt(4.0); }\n",
    );
    w.write(
        &format!("{}/lib.gm", dir),
        "c :: import \"std/c\"\nlib :: foreign \"c\" {\n    wrap_value :: fn() -> c.c_int needs not\n}\npub value :: fn() -> i32 needs Sys => lib.wrap_value()\n",
    );
}

const WRAP_MAIN: &str = "\
wrap :: import \"wrap\"
c :: import \"std/c\"

own :: foreign \"c\" {
    own_value :: fn() -> c.c_int needs not
}

main :: fn() needs Out + Sys {
    print(\"{} {}\\n\", wrap.value(), own.own_value())
}
";

/// The root package has a `config.h` of its own, with another value: each package's C
/// files are compiled with that package's include directories.
fn wrap_app(w: &World, dep: &str) {
    w.write(
        "app/gamma.toml",
        &format!(
            "[package]\nname = \"app\"\n\n[dependencies]\nwrap = {}\n\n[build]\nc-sources = [\"own.c\"]\ninclude-dirs = [\"inc\"]\n",
            dep
        ),
    );
    w.write("app/inc/config.h", "#define OWN_BASE 1000\n");
    w.write("app/own.c", "#include \"config.h\"\nint own_value(void) { return OWN_BASE; }\n");
    w.write("app/main.gm", WRAP_MAIN);
}

#[test]
fn path_dependencies_can_wrap_c() {
    need_cc!();
    let w = World::new("dep_path");
    c_dependency(&w, "wrap");
    wrap_app(&w, "{ path = \"../wrap\" }");
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "42 1000\n");
    assert_eq!(w.gamma("app", &["run", "--backend=c", "."]).success().stdout, "42 1000\n");
    w.gamma("app", &["build", ".", "-o", "app.out"]).success();
    assert_eq!(String::from_utf8_lossy(&Command::new(w.path("app/app.out")).output().unwrap().stdout), "42 1000\n");
    assert_eq!(w.native(".o").len(), 2);
    // A dependency's paths stay inside it.
    w.write("wrap/gamma.toml", "[package]\nname = \"wrap\"\n[build]\nc-sources = [\"../app/own.c\"]\n");
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("not inside the package"), "{}", out.stderr);
    w.write("wrap/gamma.toml", "[package]\nname = \"wrap\"\n[build]\nlib-dirs = [\"/usr/lib\"]\n");
    assert!(w.gamma("app", &["run", "."]).failure().stderr.contains("not inside the package"));
}

#[test]
fn git_dependencies_can_wrap_c() {
    need_cc!();
    if !have("git") {
        eprintln!("skipped: no git");
        return;
    }
    let w = World::new("dep_git");
    c_dependency(&w, "wrap");
    let git = |args: &[&str]| {
        let out = Command::new("git")
            .args(["-c", "init.defaultBranch=main", "-c", "commit.gpgsign=false"])
            .args(args)
            .current_dir(w.path("wrap"))
            .env("GIT_AUTHOR_NAME", "t")
            .env("GIT_AUTHOR_EMAIL", "t@example.com")
            .env("GIT_COMMITTER_NAME", "t")
            .env("GIT_COMMITTER_EMAIL", "t@example.com")
            .output()
            .unwrap();
        assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    };
    git(&["init", "-q"]);
    git(&["add", "-A"]);
    git(&["commit", "-q", "-m", "wrap"]);
    git(&["tag", "v1.0.0"]);
    wrap_app(&w, &format!("{{ git = \"{}\", version = \"1\" }}", file_url(&w.path("wrap"))));
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "42 1000\n", "{}", out.stderr);
    assert_eq!(w.gamma("app", &["run", "--backend=c", "."]).success().stdout, "42 1000\n");
    w.gamma("app", &["test", "."]).success();
    // One object per package.
    assert_eq!(w.native(".o").len(), 2);
}

#[cfg(unix)]
#[test]
fn libraries_and_library_directories() {
    need_cc!();
    let w = World::new("libs");
    w.write(
        "app/gamma.toml",
        "[package]\nname = \"app\"\n[build]\nlibs = [\"vendored\"]\nlib-dirs = [\"vendor/lib\"]\n",
    );
    w.write("vendor/src/v.c", "int vendored_answer(void) { return 42; }\n");
    std::fs::create_dir_all(w.path("app/vendor/lib")).unwrap();
    let so = w.path("app/vendor/lib/libvendored.so");
    let ext = if cfg!(target_os = "macos") { "-dynamiclib" } else { "-shared" };
    let status = Command::new("cc").args([ext, "-fPIC", "-o"]).arg(&so).arg(w.path("vendor/src/v.c")).status().unwrap();
    assert!(status.success());
    w.write(
        "app/main.gm",
        "c :: import \"std/c\"\nv :: foreign \"vendored\" {\n    vendored_answer :: fn() -> c.c_int needs not\n}\nmain :: fn() needs Out + Sys {\n    print(\"{}\\n\", v.vendored_answer())\n}\n",
    );
    // The interpreter opens the library from `lib-dirs`; the executable finds it by an
    // rpath of the same directory.
    assert_eq!(w.gamma("app", &["run", "main.gm"]).success().stdout, "42\n");
    assert_eq!(w.gamma("app", &["run", "--backend=c", "main.gm"]).success().stdout, "42\n");
    w.gamma("app", &["build", "main.gm", "-o", "app.out"]).success();
    let built = Command::new(w.path("app/app.out")).output().unwrap();
    assert_eq!(String::from_utf8_lossy(&built.stdout), "42\n");
}

#[test]
fn ldflags_of_the_manifest_reach_the_linker() {
    need_cc!();
    let w = World::new("ldflags");
    shim_package(&w, &format!("{}ldflags = [\"-lnonexistent_library_for_test\"]\n", SHIM_BUILD));
    let out = w.gamma("app", &["build", "main.gm"]).failure();
    assert!(out.stderr.contains("nonexistent_library_for_test"), "{}", out.stderr);
}

/// The `file:` URL of the local directory `p`, as git takes it (`file:///D:/x` on Windows).
fn file_url(p: &Path) -> String {
    let s = p.display().to_string().replace('\\', "/");
    if s.starts_with('/') { format!("file://{s}") } else { format!("file:///{s}") }
}
