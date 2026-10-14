//! Tests of the package manager: git dependencies, the lockfile, `gamma fetch`,
//! `update`, `add`, `remove`, `new` and `init`. The repositories are local, made in a
//! temporary directory, so the tests need `git` but not the network. Each test has a
//! cache directory of its own.

use std::path::{Path, PathBuf};
use std::process::Command;

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

fn git_available() -> bool {
    Command::new("git").arg("--version").output().is_ok_and(|o| o.status.success())
}

macro_rules! need_git {
    () => {
        if !git_available() {
            eprintln!("skipped: git is not installed");
            return;
        }
    };
}

/// Deletes a tree whose directories may be read-only (fetched packages are).
fn force_remove(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fn walk(p: &Path) {
            let Ok(meta) = std::fs::symlink_metadata(p) else { return };
            if meta.is_dir() {
                let _ = std::fs::set_permissions(p, std::fs::Permissions::from_mode(0o755));
                if let Ok(rd) = std::fs::read_dir(p) {
                    for e in rd.flatten() {
                        walk(&e.path());
                    }
                }
            }
        }
        walk(path);
    }
    let _ = std::fs::remove_dir_all(path);
}

/// A fresh directory for the test `name`.
struct World {
    root: PathBuf,
}

impl World {
    fn new(name: &str) -> World {
        World::in_dir(PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("pkg"), name)
    }

    /// A world outside any git work tree (the target directory may be inside one).
    fn outside(name: &str) -> World {
        World::in_dir(std::env::temp_dir().join("gamma-pkg-tests"), name)
    }

    fn in_dir(base: PathBuf, name: &str) -> World {
        let root = base.join(name);
        force_remove(&root);
        std::fs::create_dir_all(&root).unwrap();
        World { root: root.canonicalize().unwrap() }
    }

    fn cache(&self) -> PathBuf {
        self.root.join("cache")
    }

    fn path(&self, p: &str) -> PathBuf {
        self.root.join(p)
    }

    fn url(&self, repo: &str) -> String {
        format!("file://{}", self.path(repo).display())
    }

    fn write(&self, rel: &str, text: &str) {
        let p = self.path(rel);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, text).unwrap();
    }

    fn read(&self, rel: &str) -> String {
        std::fs::read_to_string(self.path(rel)).unwrap_or_default()
    }

    /// Runs git in `repo` (relative to the world) and returns its output.
    fn git(&self, repo: &str, args: &[&str]) -> String {
        let out = Command::new("git")
            .args(["-c", "init.defaultBranch=main", "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false"])
            .args(args)
            .current_dir(self.path(repo))
            .env("GIT_AUTHOR_NAME", "t")
            .env("GIT_AUTHOR_EMAIL", "t@example.com")
            .env("GIT_COMMITTER_NAME", "t")
            .env("GIT_COMMITTER_EMAIL", "t@example.com")
            .output()
            .unwrap();
        assert!(out.status.success(), "git {:?} failed: {}", args, String::from_utf8_lossy(&out.stderr));
        String::from_utf8_lossy(&out.stdout).trim().to_string()
    }

    /// Creates the repository `name` with `files`, one commit and, if given, a tag.
    fn repo(&self, name: &str, files: &[(&str, &str)], tag: Option<&str>) {
        std::fs::create_dir_all(self.path(name)).unwrap();
        self.git(name, &["init", "-q"]);
        for (f, t) in files {
            self.write(&format!("{}/{}", name, f), t);
        }
        self.commit(name, "initial", tag);
    }

    /// Commits all changes in `repo` and tags the commit.
    fn commit(&self, repo: &str, msg: &str, tag: Option<&str>) {
        self.git(repo, &["add", "-A"]);
        self.git(repo, &["commit", "-q", "-m", msg]);
        if let Some(t) = tag {
            self.git(repo, &["tag", t]);
        }
    }

    /// A library package with an exported function returning `n`.
    fn lib(&self, name: &str, n: i32, tag: Option<&str>) {
        self.repo(name, &[("gamma.toml", &lib_manifest(name)), ("lib.gm", &lib_source(n))], tag);
    }

    /// New version of the library: changes the number, commits, tags.
    fn bump(&self, name: &str, n: i32, tag: &str) {
        self.write(&format!("{}/lib.gm", name), &lib_source(n));
        self.commit(name, "bump", Some(tag));
    }

    fn gamma(&self, dir: &str, args: &[&str]) -> Out {
        let out = Command::new(GAMMA)
            .args(args)
            .current_dir(self.path(dir))
            .env("GAMMA_CACHE_DIR", self.cache())
            .env("GIT_AUTHOR_NAME", "t")
            .env("GIT_AUTHOR_EMAIL", "t@example.com")
            .env("GIT_COMMITTER_NAME", "t")
            .env("GIT_COMMITTER_EMAIL", "t@example.com")
            .output()
            .unwrap();
        Out {
            ok: out.status.success(),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        }
    }

    /// An application `app` whose manifest has `deps` and which prints `body`.
    fn app(&self, deps: &str, imports: &str, body: &str) {
        self.write(
            "app/gamma.toml",
            &format!("[package]\nname = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n{}", deps),
        );
        self.write("app/main.gm", &format!("{}\nmain :: fn() \\ {{Out}} {{\n{}\n}}\n", imports, body));
    }
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

fn lib_manifest(name: &str) -> String {
    format!("[package]\nname = \"{}\"\nversion = \"1.0.0\"\n", name)
}

fn lib_source(n: i32) -> String {
    format!("pub value :: fn() -> i32 => {}\n", n)
}

/// An application that imports `names` and prints the sum of their `value()`s.
fn sum_app(w: &World, deps: &str, names: &[&str]) {
    let imports: String = names.iter().map(|n| format!("{n} :: import \"{n}\"\n")).collect();
    let sum: Vec<String> = names.iter().map(|n| format!("{}.value()", n)).collect();
    w.app(deps, &imports, &format!("    print(\"{{}}\\n\", {})", sum.join(" + ")));
}

fn lock_field(lock: &str, name: &str, field: &str) -> Option<String> {
    let mut in_pkg = false;
    for line in lock.lines() {
        if line == "[[package]]" {
            in_pkg = false;
        }
        if line == format!("name = \"{}\"", name) {
            in_pkg = true;
        }
        if in_pkg && let Some(v) = line.strip_prefix(&format!("{} = ", field)) {
            return Some(v.trim_matches('"').to_string());
        }
    }
    None
}

#[test]
fn add_fetch_and_run_a_git_dependency() {
    need_git!();
    let w = World::new("add_run");
    w.lib("http", 7, Some("v1.0.0"));
    w.write("app/gamma.toml", "# my app\n[package]\nname = \"app\"\n");
    w.write("app/main.gm", "http :: import \"http\"\nmain :: fn() \\ {Out} {\n    print(\"{}\\n\", http.value())\n}\n");
    let out = w.gamma("app", &["add", "http", "--git", &w.url("http")]).success();
    assert!(out.stderr.contains("Fetching http"), "{}", out.stderr);
    assert!(out.stderr.contains("Locking http v1.0.0"), "{}", out.stderr);
    let manifest = w.read("app/gamma.toml");
    assert!(manifest.starts_with("# my app\n[package]"), "{}", manifest);
    assert!(
        manifest.contains(&format!("http = {{ git = \"{}\", version = \"^1.0.0\" }}", w.url("http"))),
        "{}",
        manifest
    );
    let lock = w.read("app/gamma.lock");
    assert!(lock.starts_with("# This file is generated by `gamma`. Do not edit it by hand.\nversion = 1\n"));
    assert_eq!(lock_field(&lock, "http", "reference").as_deref(), Some("tag=v1.0.0"));
    assert_eq!(lock_field(&lock, "http", "version").as_deref(), Some("1.0.0"));
    assert_eq!(lock_field(&lock, "http", "rev").unwrap(), w.git("http", &["rev-parse", "HEAD"]));
    assert!(lock_field(&lock, "http", "checksum").unwrap().starts_with("sha256:"));
    // Everything that compiles uses the dependency; nothing is fetched again.
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "7\n");
    assert!(!out.stderr.contains("Fetching"), "{}", out.stderr);
    assert_eq!(w.gamma("app", &["check", "."]).success().stderr, "");
    w.gamma("app", &["build", ".", "-o", "app.out"]).success();
    let built = Command::new(w.path("app/app.out")).output().unwrap();
    assert_eq!(String::from_utf8_lossy(&built.stdout), "7\n");
    // The lockfile is stable.
    w.gamma("app", &["fetch"]).success();
    assert_eq!(w.read("app/gamma.lock"), lock);
    // Diagnostics name the package's module after the dependency, not its directory.
    w.write("app/main.gm", "http :: import \"http\"\nmain :: fn() \\ {Out} {\n    print(\"{}\\n\", http.nope())\n}\n");
    let out = w.gamma("app", &["check", "."]).failure();
    assert!(out.stderr.contains("the module `http` has no declaration `nope`"), "{}", out.stderr);
}

#[test]
fn packages_are_found_from_a_subdirectory_and_by_file() {
    need_git!();
    let w = World::new("subdir_root");
    w.lib("http", 3, Some("v1.0.0"));
    sum_app(&w, &format!("http = {{ git = \"{}\", tag = \"v1.0.0\" }}\n", w.url("http")), &["http"]);
    w.gamma("app", &["fetch"]).success();
    assert_eq!(w.gamma(".", &["fetch", "app"]).success().stderr, "");
    std::fs::create_dir_all(w.path("app/sub")).unwrap();
    assert!(w.gamma("app/sub", &["fetch"]).ok);
    assert!(!w.gamma("cache", &["fetch"]).ok, "no package here");
    assert!(w.gamma("cache", &["fetch"]).stderr.contains("no gamma.toml found"));
}

#[test]
fn version_requirements_and_update() {
    need_git!();
    let w = World::new("versions");
    w.lib("util", 1, Some("v1.0.0"));
    w.bump("util", 2, "v1.2.0");
    w.bump("util", 3, "v2.0.0");
    w.git("util", &["tag", "nightly"]);
    sum_app(&w, &format!("util = {{ git = \"{}\", version = \"1\" }}\n", w.url("util")), &["util"]);
    w.gamma("app", &["fetch"]).success();
    let lock = w.read("app/gamma.lock");
    assert_eq!(lock_field(&lock, "util", "version").as_deref(), Some("1.2.0"));
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "2\n");

    // A new compatible release does not change the lock...
    w.bump("util", 4, "v1.3.0");
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "2\n");
    assert_eq!(w.read("app/gamma.lock"), lock);
    assert!(!out.stderr.contains("Fetching"));
    w.gamma("app", &["fetch"]).success();
    assert_eq!(w.read("app/gamma.lock"), lock);

    // ...until `update`.
    let out = w.gamma("app", &["update"]).success();
    assert!(out.stderr.contains("Updating util v1.2.0 -> v1.3.0"), "{}", out.stderr);
    assert_eq!(lock_field(&w.read("app/gamma.lock"), "util", "version").as_deref(), Some("1.3.0"));
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "4\n");

    // Updating by name, and a name that is not a dependency.
    w.bump("util", 5, "v1.4.0");
    let out = w.gamma("app", &["update", "util"]).success();
    assert!(out.stderr.contains("v1.3.0 -> v1.4.0"), "{}", out.stderr);
    let out = w.gamma("app", &["update", "nope"]).failure();
    assert!(out.stderr.contains("`nope` is not a git dependency"), "{}", out.stderr);

    // Changing the requirement re-resolves just that package.
    let manifest = w.read("app/gamma.toml").replace("version = \"1\"", "version = \"2\"");
    w.write("app/gamma.toml", &manifest);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "3\n");
    assert_eq!(lock_field(&w.read("app/gamma.lock"), "util", "version").as_deref(), Some("2.0.0"));
    // No tag matches.
    let manifest = manifest.replace("version = \"2\"", "version = \"3\"");
    w.write("app/gamma.toml", &manifest);
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("no version tag"), "{}", out.stderr);
}

#[test]
fn tags_branches_revs_and_the_default_branch() {
    need_git!();
    let w = World::new("refs");
    w.lib("lib", 1, Some("v1.0.0"));
    let first = w.git("lib", &["rev-parse", "HEAD"]);
    w.bump("lib", 2, "v1.1.0");
    w.git("lib", &["checkout", "-q", "-b", "dev"]);
    w.write("lib/lib.gm", &lib_source(10));
    w.commit("lib", "dev work", None);
    w.git("lib", &["checkout", "-q", "main"]);
    let url = w.url("lib");
    for (dep, expect) in [
        ("tag = \"v1.0.0\"".to_string(), "1"),
        ("branch = \"dev\"".to_string(), "10"),
        (format!("rev = \"{}\"", &first[..10]), "1"),
        (String::new(), "2"),
        ("version = \"~1.0\"".to_string(), "1"),
    ] {
        let comma = if dep.is_empty() { "" } else { ", " };
        sum_app(&w, &format!("lib = {{ git = \"{}\"{}{} }}\n", url, comma, dep), &["lib"]);
        let _ = std::fs::remove_file(w.path("app/gamma.lock"));
        let out = w.gamma("app", &["run", "."]).success();
        assert_eq!(out.stdout.trim(), expect, "{}", dep);
        let lock = w.read("app/gamma.lock");
        let reference = lock_field(&lock, "lib", "reference").unwrap();
        match dep.split(' ').next().unwrap() {
            "tag" => assert_eq!(reference, "tag=v1.0.0"),
            "branch" => assert_eq!(reference, "branch=dev"),
            "rev" => assert!(reference.starts_with("rev="), "{}", reference),
            "" => assert_eq!(reference, "default"),
            _ => assert_eq!(reference, "tag=v1.0.0"),
        }
    }
    // A branch stays where the lock put it until `update`.
    sum_app(&w, &format!("lib = {{ git = \"{}\", branch = \"dev\" }}\n", url), &["lib"]);
    let _ = std::fs::remove_file(w.path("app/gamma.lock"));
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "10\n");
    w.git("lib", &["checkout", "-q", "dev"]);
    w.write("lib/lib.gm", &lib_source(11));
    w.commit("lib", "more dev work", None);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "10\n");
    w.gamma("app", &["update"]).success();
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "11\n");
}

#[test]
fn the_lockfile_is_checked_with_locked() {
    need_git!();
    let w = World::new("locked");
    w.lib("a", 1, Some("v1.0.0"));
    w.lib("b", 2, Some("v1.0.0"));
    sum_app(&w, &format!("a = {{ git = \"{}\", version = \"1\" }}\n", w.url("a")), &["a"]);
    let out = w.gamma("app", &["fetch", "--locked"]).failure();
    assert!(out.stderr.contains("gamma.lock is missing"), "{}", out.stderr);
    assert!(!out.stderr.contains("Fetching"), "{}", out.stderr);
    assert!(!w.path("app/gamma.lock").exists());
    w.gamma("app", &["fetch"]).success();
    w.gamma("app", &["fetch", "--locked"]).success();
    w.gamma("app", &["run", "--locked", "."]).success();
    // A manifest the lock does not cover.
    let manifest = format!("{}b = {{ git = \"{}\" }}\n", w.read("app/gamma.toml"), w.url("b"));
    w.write("app/gamma.toml", &manifest);
    let before = w.read("app/gamma.lock");
    let out = w.gamma("app", &["check", "--locked", "."]).failure();
    assert!(out.stderr.contains("--locked"), "{}", out.stderr);
    assert_eq!(w.read("app/gamma.lock"), before);
    // A lock with a package the manifest no longer wants would change.
    let out = w.gamma("app", &["check", "."]).success();
    assert!(out.stderr.contains("Locking b"), "{}", out.stderr);
    w.write("app/gamma.toml", &manifest.replace(&format!("b = {{ git = \"{}\" }}\n", w.url("b")), ""));
    let out = w.gamma("app", &["check", "--locked", "."]).failure();
    assert!(out.stderr.contains("needs to be updated"), "{}", out.stderr);
}

#[test]
fn offline_uses_only_what_is_locked_and_fetched() {
    need_git!();
    let w = World::new("offline");
    w.lib("a", 5, Some("v1.0.0"));
    sum_app(&w, &format!("a = {{ git = \"{}\", version = \"1\" }}\n", w.url("a")), &["a"]);
    let out = w.gamma("app", &["run", "--offline", "."]).failure();
    assert!(out.stderr.contains("--offline"), "{}", out.stderr);
    assert!(!w.cache().join("git").exists(), "nothing was fetched");
    w.gamma("app", &["fetch"]).success();
    let out = w.gamma("app", &["run", "--offline", "."]).success();
    assert_eq!(out.stdout, "5\n");
    // With the repository gone, a fetched package still builds offline...
    std::fs::rename(w.path("a"), w.path("a-moved")).unwrap();
    assert_eq!(w.gamma("app", &["test", "--offline", "."]).success().stdout.trim(), "0 passed, 0 failed");
    // ...and an empty cache needs the network.
    gamma::pkg::git::remove_tree(&w.cache()).unwrap();
    let out = w.gamma("app", &["run", "--offline", "."]).failure();
    assert!(out.stderr.contains("not in the cache"), "{}", out.stderr);
    // Online, the missing repository is reported by git.
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("cannot fetch"), "{}", out.stderr);
}

#[test]
fn transitive_dependencies_are_locked_in_the_root() {
    need_git!();
    let w = World::new("transitive");
    w.lib("util", 40, Some("v0.3.0"));
    w.repo(
        "http",
        &[
            (
                "gamma.toml",
                &format!(
                    "[package]\nname = \"http\"\n\n[dependencies]\nutil = {{ git = \"{}\", version = \"0.3\" }}\ncommon = {{ path = \"common\" }}\n",
                    w.url("util")
                ),
            ),
            ("lib.gm", "util :: import \"util\"\ncommon :: import \"common\"\npub value :: fn() -> i32 => util.value() + common.one() + 1\n"),
            ("common/gamma.toml", "[package]\nname = \"common\"\n"),
            ("common/x.gm", "pub one :: fn() -> i32 => 1\n"),
        ],
        Some("v1.0.0"),
    );
    sum_app(&w, &format!("http = {{ git = \"{}\", version = \"1\" }}\n", w.url("http")), &["http"]);
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "42\n");
    assert!(out.stderr.contains("Fetching util"), "{}", out.stderr);
    let lock = w.read("app/gamma.lock");
    assert_eq!(lock_field(&lock, "util", "version").as_deref(), Some("0.3.0"));
    assert_eq!(lock_field(&lock, "http", "dependencies").as_deref(), Some("[\"util\"]"));
    // The transitive package comes from the lock, offline too.
    assert_eq!(w.gamma("app", &["run", "--offline", "--locked", "."]).success().stdout, "42\n");
}

#[test]
fn two_requirements_on_one_repository_share_a_revision() {
    need_git!();
    let w = World::new("shared");
    w.lib("util", 1, Some("v1.0.0"));
    w.bump("util", 2, "v1.4.0");
    w.bump("util", 3, "v2.0.0");
    for (name, req) in [("a", "^1.0"), ("b", ">=1.2, <2")] {
        w.repo(
            name,
            &[
                (
                    "gamma.toml",
                    &format!("[package]\nname = \"{name}\"\n[dependencies]\nutil = {{ git = \"{}\", version = \"{req}\" }}\n", w.url("util")),
                ),
                ("lib.gm", "util :: import \"util\"\npub value :: fn() -> i32 => util.value()\n"),
            ],
            Some("v1.0.0"),
        );
    }
    let deps = format!("a = {{ git = \"{}\" }}\nb = {{ git = \"{}\" }}\n", w.url("a"), w.url("b"));
    sum_app(&w, &deps, &["a", "b"]);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "4\n");
    let lock = w.read("app/gamma.lock");
    assert_eq!(lock.matches("[[package]]").count(), 3);
    assert_eq!(lock_field(&lock, "util", "version").as_deref(), Some("1.4.0"));

    // Requirements that cannot meet fail, naming both requirers.
    w.git("b", &["checkout", "-q", "-b", "next"]);
    w.write(
        "b/gamma.toml",
        &format!(
            "[package]\nname = \"b\"\n[dependencies]\nutil = {{ git = \"{}\", version = \"2\" }}\n",
            w.url("util")
        ),
    );
    w.commit("b", "need util 2", Some("v2.0.0"));
    w.git("b", &["checkout", "-q", "main"]);
    let _ = std::fs::remove_file(w.path("app/gamma.lock"));
    sum_app(
        &w,
        &format!("a = {{ git = \"{}\" }}\nb = {{ git = \"{}\", tag = \"v2.0.0\" }}\n", w.url("a"), w.url("b")),
        &["a", "b"],
    );
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("no version tag"), "{}", out.stderr);
    assert!(
        out.stderr.contains("`a` requires version `^1.0`") && out.stderr.contains("`b` requires version `^2`"),
        "{}",
        out.stderr
    );
    assert!(!w.path("app/gamma.lock").exists());
}

#[test]
fn different_refs_of_one_repository_conflict() {
    need_git!();
    let w = World::new("conflict");
    w.lib("util", 1, Some("v1.0.0"));
    w.bump("util", 2, "v1.1.0");
    std::os::unix::fs::symlink(w.path("util"), w.path("util.git")).unwrap();
    w.repo(
        "a",
        &[
            (
                "gamma.toml",
                &format!(
                    "[package]\nname = \"a\"\n[dependencies]\nutil = {{ git = \"{}.git\", tag = \"v1.0.0\" }}\n",
                    w.url("util")
                ),
            ),
            ("lib.gm", "util :: import \"util\"\npub value :: fn() -> i32 => util.value()\n"),
        ],
        Some("v1.0.0"),
    );
    // `.git` and a trailing slash name the same repository.
    let deps =
        format!("a = {{ git = \"{}\" }}\nutil = {{ git = \"{}/\", tag = \"v1.1.0\" }}\n", w.url("a"), w.url("util"));
    sum_app(&w, &deps, &["a", "util"]);
    let out = w.gamma("app", &["check", "."]).failure();
    assert!(out.stderr.contains("conflicting requirements"), "{}", out.stderr);
    assert!(
        out.stderr.contains("`a` requires tag `v1.0.0`") && out.stderr.contains("`app` requires tag `v1.1.0`"),
        "{}",
        out.stderr
    );
}

#[test]
fn dependency_cycles_are_errors() {
    need_git!();
    let w = World::new("cycle");
    for (name, other) in [("a", "b"), ("b", "a")] {
        w.repo(
            name,
            &[
                (
                    "gamma.toml",
                    &format!(
                        "[package]\nname = \"{name}\"\n[dependencies]\n{other} = {{ git = \"{}\" }}\n",
                        w.url(other)
                    ),
                ),
                ("lib.gm", "pub value :: fn() -> i32 => 1\n"),
            ],
            None,
        );
    }
    sum_app(&w, &format!("a = {{ git = \"{}\" }}\n", w.url("a")), &["a"]);
    let out = w.gamma("app", &["check", "."]).failure();
    assert!(out.stderr.contains("dependency cycle: a -> b -> a"), "{}", out.stderr);
}

#[test]
fn a_git_package_cannot_reach_outside_its_checkout() {
    need_git!();
    let w = World::new("escape");
    w.repo(
        "evil",
        &[
            ("gamma.toml", "[package]\nname = \"evil\"\n[dependencies]\nsecret = { path = \"../../../..\" }\n"),
            ("lib.gm", "pub value :: fn() -> i32 => 1\n"),
        ],
        None,
    );
    sum_app(&w, &format!("evil = {{ git = \"{}\" }}\n", w.url("evil")), &["evil"]);
    let out = w.gamma("app", &["check", "."]).failure();
    assert!(out.stderr.contains("leaves the repository"), "{}", out.stderr);
}

#[test]
fn a_package_in_a_subdirectory() {
    need_git!();
    let w = World::new("subdirs");
    w.repo(
        "mono",
        &[
            ("README", "a monorepo\n"),
            ("one/gamma.toml", "[package]\nname = \"one\"\n[dependencies]\ntwo = { path = \"../two\" }\n"),
            ("one/lib.gm", "two :: import \"two\"\npub value :: fn() -> i32 => two.value() + 1\n"),
            ("two/gamma.toml", "[package]\nname = \"two\"\n"),
            ("two/lib.gm", "pub value :: fn() -> i32 => 20\n"),
        ],
        Some("v0.1.0"),
    );
    sum_app(&w, &format!("one = {{ git = \"{}\", subdir = \"one\", tag = \"v0.1.0\" }}\n", w.url("mono")), &["one"]);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "21\n");
    let lock = w.read("app/gamma.lock");
    assert_eq!(lock_field(&lock, "one", "subdir").as_deref(), Some("one"));
    // A subdirectory without a package is reported.
    sum_app(&w, &format!("one = {{ git = \"{}\", subdir = \"zero\" }}\n", w.url("mono")), &["one"]);
    let out = w.gamma("app", &["check", "."]).failure();
    assert!(out.stderr.contains("no directory `zero`"), "{}", out.stderr);
}

#[test]
fn a_changed_checkout_fails_its_checksum() {
    need_git!();
    let w = World::new("checksum");
    w.lib("a", 1, Some("v1.0.0"));
    sum_app(&w, &format!("a = {{ git = \"{}\", version = \"1\" }}\n", w.url("a")), &["a"]);
    w.gamma("app", &["fetch"]).success();
    let lock = w.read("app/gamma.lock");
    let sum = lock_field(&lock, "a", "checksum").unwrap();

    // A moved tag does not change what the lock names: the same revision comes out.
    w.write("a/lib.gm", &lib_source(99));
    w.git("a", &["add", "-A"]);
    w.git("a", &["commit", "-q", "-m", "oops"]);
    w.git("a", &["tag", "-f", "v1.0.0"]);
    gamma::pkg::git::remove_tree(&w.cache().join("git/checkouts")).unwrap();
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "1\n");
    assert_eq!(w.read("app/gamma.lock"), lock);

    // A lock whose checksum differs from the content is refused.
    gamma::pkg::git::remove_tree(&w.cache().join("git/checkouts")).unwrap();
    w.write("app/gamma.lock", &lock.replace(&sum, &format!("sha256:{}", "0".repeat(64))));
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("checksum mismatch for package a (expected sha256:000"), "{}", out.stderr);
    assert!(out.stderr.contains(&format!("found {}", sum)), "{}", out.stderr);
    assert_eq!(
        std::fs::read_dir(w.cache().join("git/checkouts"))
            .unwrap()
            .flatten()
            .flat_map(|d| std::fs::read_dir(d.path()).unwrap().flatten())
            .count(),
        0,
        "the bad checkout is removed"
    );
    // Restoring the lock fixes it.
    w.write("app/gamma.lock", &lock);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "1\n");

    // The tag is the only way to a revision: with a fresh cache and the locked
    // revision gone from the repository, the error says so.
    w.git("a", &["checkout", "-q", "--orphan", "fresh"]);
    w.git("a", &["commit", "-q", "-m", "fresh start", "--allow-empty"]);
    w.git("a", &["branch", "-q", "-D", "main"]);
    w.git("a", &["tag", "-f", "v1.0.0"]);
    w.git("a", &["reflog", "expire", "--expire=now", "--all"]);
    w.git("a", &["gc", "-q", "--prune=now"]);
    gamma::pkg::git::remove_tree(&w.cache()).unwrap();
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("no longer available"), "{}", out.stderr);
}

#[test]
fn remove_drops_the_dependency_and_its_lock_entry() {
    need_git!();
    let w = World::new("remove");
    w.lib("a", 1, Some("v1.0.0"));
    w.lib("b", 2, Some("v1.0.0"));
    w.write(
        "app/gamma.toml",
        "[package]\nname = \"app\"\n\n[dependencies]\n# the a library\na = { git = \"PLACEHOLDER\" }\n",
    );
    w.write("app/gamma.toml", &w.read("app/gamma.toml").replace("PLACEHOLDER", &w.url("a")));
    w.gamma("app", &["add", "b", "--git", &w.url("b"), "--tag", "v1.0.0"]).success();
    let manifest = w.read("app/gamma.toml");
    assert!(manifest.contains("# the a library"));
    assert!(manifest.contains(&format!("b = {{ git = \"{}\", tag = \"v1.0.0\" }}", w.url("b"))), "{}", manifest);
    assert_eq!(w.read("app/gamma.lock").matches("[[package]]").count(), 2);
    // Adding an existing name is refused and changes nothing.
    let out = w.gamma("app", &["add", "b", "--git", &w.url("b")]).failure();
    assert!(out.stderr.contains("already exists"), "{}", out.stderr);
    assert_eq!(w.read("app/gamma.toml"), manifest);
    // A failing add leaves the manifest as it was.
    let out = w.gamma("app", &["add", "c", "--git", &w.url("a"), "--tag", "nope"]).failure();
    assert!(out.stderr.contains("no tag `nope`"), "{}", out.stderr);
    assert_eq!(w.read("app/gamma.toml"), manifest);

    w.gamma("app", &["remove", "b"]).success();
    assert!(w.read("app/gamma.toml").contains("# the a library"));
    assert!(!w.read("app/gamma.toml").contains("b ="));
    assert_eq!(w.read("app/gamma.lock").matches("[[package]]").count(), 1);
    assert!(w.gamma("app", &["remove", "b"]).failure().stderr.contains("no dependency `b`"));
    w.gamma("app", &["remove", "a"]).success();
    assert!(!w.path("app/gamma.lock").exists(), "no git dependencies, no lockfile");
}

#[test]
fn add_chooses_a_version_or_the_default_branch_and_accepts_paths() {
    need_git!();
    let w = World::new("add_kinds");
    w.lib("tagged", 1, Some("v0.2.0"));
    w.bump("tagged", 2, "v0.10.1");
    w.bump("tagged", 3, "v1.0.0-rc.1");
    w.lib("untagged", 3, None);
    w.lib("local", 4, None);
    w.write("app/gamma.toml", "[package]\nname = \"app\"\n");
    w.write("app/main.gm", "main :: fn() \\ {Out} {}\n");
    w.gamma("app", &["add", "tagged", "--git", &w.url("tagged")]).success();
    w.gamma("app", &["add", "untagged", "--git", &w.url("untagged")]).success();
    w.gamma("app", &["add", "local", "--path", "../local"]).success();
    w.gamma("app", &["add", "ranged", "--git", &w.url("tagged"), "--version", "0.10"]).success();
    let manifest = w.read("app/gamma.toml");
    assert!(
        manifest.contains(&format!("tagged = {{ git = \"{}\", version = \"^0.10.1\" }}", w.url("tagged"))),
        "{}",
        manifest
    );
    assert!(manifest.contains(&format!("untagged = {{ git = \"{}\" }}", w.url("untagged"))), "{}", manifest);
    assert!(manifest.contains("local = { path = \"../local\" }"), "{}", manifest);
    let lock = w.read("app/gamma.lock");
    assert_eq!(lock_field(&lock, "untagged", "reference").as_deref(), Some("default"));
    assert_eq!(lock_field(&lock, "tagged", "version").as_deref(), Some("0.10.1"));
    assert_eq!(lock.matches("[[package]]").count(), 2, "one repository is one entry: {}", lock);
    // Option errors.
    assert!(w.gamma("app", &["add", "x"]).failure().stderr.contains("needs `--path"));
    assert!(w.gamma("app", &["add", "x", "--git", "u", "--tag", "t", "--branch", "b"]).stderr.contains("only one"));
    assert!(w.gamma("app", &["add", "bad-name", "--path", "."]).stderr.contains("invalid dependency name"));
    assert!(w.gamma("app", &["add", "x", "--bogus"]).stderr.contains("unknown option"));
}

#[test]
fn manifest_errors_name_the_line() {
    let w = World::new("manifest_errors");
    w.write("app/main.gm", "main :: fn() \\ {Out} {}\n");
    for (manifest, line, msg) in [
        (
            "[package]\nname = \"app\"\n[dependencies]\nx = { path = \"a\", colour = \"red\" }\n",
            4,
            "unknown key `colour`",
        ),
        ("[dependencies]\nx = { path = \"a\", git = \"b\" }\n", 2, "both `path` and `git`"),
        ("[dependencies]\n\nx = \"1.0\"\n", 3, "must be a table"),
        ("[dependencies]\nx-y = { path = \"a\" }\n", 2, "invalid dependency name"),
        ("[package]\nname = \"9app\"\n", 2, "invalid package name"),
        ("[dependencies]\nx = { git = \"u\", tag = \"t\", rev = \"ab\" }\n", 2, "use only one"),
    ] {
        w.write("app/gamma.toml", manifest);
        let out = w.gamma("app", &["check", "."]).failure();
        let expect = format!("gamma.toml:{}: ", line);
        assert!(out.stderr.contains(&expect) && out.stderr.contains(msg), "{}\n{}", manifest, out.stderr);
    }
}

#[test]
fn path_dependencies_work_as_before() {
    let w = World::new("paths");
    w.write("json/gamma.toml", "[package]\nname = \"json\"\n");
    w.write("json/lib.gm", &lib_source(9));
    sum_app(&w, "json = { path = \"../json\", version = \"1\" }\n", &["json"]);
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "9\n");
    assert_eq!(out.stderr, "");
    w.gamma("app", &["fetch"]).success();
    assert!(!w.path("app/gamma.lock").exists());
    w.gamma("app", &["run", "--offline", "--locked", "."]).success();
}

#[test]
fn a_git_dependency_needs_the_package_manager_when_used_as_a_library() {
    let w = World::new("library_use");
    w.write(
        "app/gamma.toml",
        "[package]\nname = \"app\"\n[dependencies]\nhttp = { git = \"https://example.invalid/http\" }\n",
    );
    w.write("app/main.gm", "main :: fn() \\ {Out} {}\n");
    let mut sess = gamma::driver::Session::new();
    let ok = gamma::sema::analyze(&mut sess, &w.path("app")).is_some();
    let text = sess.render_diagnostics(gamma::diag::DiagFormat::Short);
    assert!(!ok);
    assert!(text.contains("dependency `http` is a git dependency; run `gamma fetch`"), "{}", text);
    assert!(text.contains("gamma.toml:4:"), "{}", text);
}

fn run_ok(w: &World, dir: &str, args: &[&str]) -> Out {
    w.gamma(dir, args).success()
}

#[test]
fn new_creates_a_working_package() {
    let w = World::outside("new");
    let out = run_ok(&w, ".", &["new", "my-app"]);
    assert!(out.stdout.contains("Created package `my_app` in my-app"), "{}", out.stdout);
    assert!(out.stdout.contains("gamma run my-app") && out.stdout.contains("gamma test my-app"), "{}", out.stdout);
    let manifest = w.read("my-app/gamma.toml");
    assert_eq!(manifest, "[package]\nname = \"my_app\"\nversion = \"0.1.0\"\n\n[dependencies]\n");
    assert!(w.read("my-app/.gitignore").lines().any(|l| l == "/my_app"));
    assert!(w.read("my-app/.gitignore").lines().any(|l| l == "/main"));
    if git_available() {
        assert!(w.path("my-app/.git").exists());
    }
    assert_eq!(run_ok(&w, ".", &["run", "my-app"]).stdout, "hello from my_app\n");
    let t = run_ok(&w, ".", &["test", "my-app"]);
    assert!(t.stdout.contains("1 passed, 0 failed"), "{}", t.stdout);
    run_ok(&w, ".", &["check", "my-app"]);
    run_ok(&w, ".", &["fmt", "--check", "my-app"]);
    run_ok(&w, "my-app", &["run", "."]);
    // The executable `build .` writes is ignored.
    run_ok(&w, "my-app", &["build", "."]);
    assert!(w.path("my-app/main").exists());
    if git_available() {
        let status = w.git("my-app", &["status", "--porcelain", "--ignored"]);
        assert!(status.contains("!! main"), "{}", status);
    }
    // A name can be given, and an existing non-empty directory is refused.
    let out = run_ok(&w, ".", &["new", "other", "--name=thing", "--vcs=none"]);
    assert!(out.stdout.contains("package `thing`"));
    assert!(!w.path("other/.git").exists());
    let out = w.gamma(".", &["new", "my-app"]).failure();
    assert!(out.stderr.contains("not empty"), "{}", out.stderr);
    assert!(w.gamma(".", &["new", "x", "--name=1x"]).failure().stderr.contains("invalid package name"));
    assert!(w.gamma(".", &["new"]).failure().stderr.contains("needs a directory"));
    // An empty existing directory is fine.
    std::fs::create_dir(w.path("empty")).unwrap();
    run_ok(&w, ".", &["new", "empty"]);
    assert_eq!(run_ok(&w, ".", &["run", "empty"]).stdout, "hello from empty\n");
}

#[test]
fn init_never_overwrites() {
    let w = World::outside("init");
    w.write("proj/main.gm", "main :: fn() \\ {Out} {\n    print(\"mine\\n\")\n}\n");
    w.write("proj/.gitignore", "custom\n");
    let out = run_ok(&w, "proj", &["init", "--vcs=none"]);
    assert!(out.stdout.contains("Skipped main.gm (already exists)"), "{}", out.stdout);
    assert!(out.stdout.contains("Skipped .gitignore (already exists)"), "{}", out.stdout);
    assert!(out.stdout.contains("Created package `proj`"), "{}", out.stdout);
    assert_eq!(w.read("proj/.gitignore"), "custom\n");
    assert!(w.read("proj/main.gm").contains("mine"));
    assert!(w.read("proj/gamma.toml").contains("name = \"proj\""));
    assert_eq!(run_ok(&w, ".", &["run", "proj"]).stdout, "mine\n");
    // Running it again creates nothing.
    let manifest = w.read("proj/gamma.toml");
    w.write("proj/gamma.toml", &format!("{}# edited\n", manifest));
    let out = run_ok(&w, "proj", &["init"]);
    assert!(out.stdout.contains("Nothing to create"), "{}", out.stdout);
    assert!(w.read("proj/gamma.toml").ends_with("# edited\n"));
    // `init` with a directory argument, and a missing directory.
    std::fs::create_dir(w.path("fresh")).unwrap();
    run_ok(&w, ".", &["init", "fresh", "--name", "named"]);
    assert!(w.read("fresh/gamma.toml").contains("name = \"named\""));
    assert!(w.gamma(".", &["init", "missing"]).failure().stderr.contains("not a directory"));
}

#[test]
fn init_leaves_an_enclosing_repository_alone() {
    need_git!();
    let w = World::new("init_in_repo");
    std::fs::create_dir_all(w.path("outer")).unwrap();
    w.git("outer", &["init", "-q"]);
    std::fs::create_dir_all(w.path("outer/inner")).unwrap();
    run_ok(&w, "outer/inner", &["init"]);
    assert!(!w.path("outer/inner/.git").exists());
    assert!(w.path("outer/inner/gamma.toml").exists());
}

#[test]
fn usage_of_the_package_commands() {
    let w = World::new("usage");
    let out = w.gamma(".", &["help", "packages"]).success();
    assert!(out.stdout.contains("gamma add <name>") && out.stdout.contains("--offline"), "{}", out.stdout);
    let out = w.gamma(".", &["update", "--bogus"]).failure();
    assert!(out.stderr.contains("unknown option `--bogus`"), "{}", out.stderr);
    assert!(Path::new(GAMMA).exists());
}

/// Writes one framed JSON-RPC message.
fn lsp_send(w: &mut impl std::io::Write, body: &str) {
    write!(w, "Content-Length: {}\r\n\r\n{}", body.len(), body).unwrap();
    w.flush().unwrap();
}

/// Reads framed messages until one contains `needle`; returns it.
fn lsp_until(r: &mut impl std::io::BufRead, needle: &str) -> String {
    loop {
        let mut len = 0;
        loop {
            let mut line = String::new();
            assert!(r.read_line(&mut line).unwrap() > 0, "the server exited");
            let line = line.trim_end();
            if line.is_empty() {
                break;
            }
            if let Some(n) = line.strip_prefix("Content-Length: ") {
                len = n.parse().unwrap();
            }
        }
        let mut body = vec![0; len];
        r.read_exact(&mut body).unwrap();
        let body = String::from_utf8(body).unwrap();
        if body.contains(needle) {
            return body;
        }
    }
}

#[test]
fn the_language_server_sees_fetched_git_dependencies() {
    need_git!();
    let w = World::new("lsp");
    w.lib("http", 7, Some("v1.0.0"));
    sum_app(&w, &format!("http = {{ git = \"{}\" }}\n", w.url("http")), &["http"]);
    w.gamma("app", &["fetch"]).success();
    let lock = w.read("app/gamma.lock");
    let main = w.path("app/main.gm").canonicalize().unwrap();
    let mut child = Command::new(GAMMA)
        .arg("lsp")
        .env("GAMMA_CACHE_DIR", w.cache())
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()
        .unwrap();
    let mut stdin = child.stdin.take().unwrap();
    let mut stdout = std::io::BufReader::new(child.stdout.take().unwrap());
    lsp_send(&mut stdin, r#"{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}"#);
    lsp_until(&mut stdout, r#""id":1"#);
    lsp_send(&mut stdin, r#"{"jsonrpc":"2.0","method":"initialized","params":{}}"#);
    let text = std::fs::read_to_string(&main).unwrap();
    let open = format!(
        r#"{{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"file://{}","languageId":"gamma","version":1,"text":{:?}}}}}}}"#,
        main.display(),
        text
    );
    lsp_send(&mut stdin, &open);
    let diags = lsp_until(&mut stdout, "publishDiagnostics");
    assert!(diags.contains(r#""diagnostics":[]"#), "{}", diags);
    lsp_send(&mut stdin, r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#);
    lsp_until(&mut stdout, r#""id":2"#);
    lsp_send(&mut stdin, r#"{"jsonrpc":"2.0","method":"exit"}"#);
    assert!(child.wait().unwrap().success());
    // The server leaves the lockfile alone.
    assert_eq!(w.read("app/gamma.lock"), lock);
}

// ---- the cache, read-only checkouts, symbolic links and archive dependencies ----

fn curl_available() -> bool {
    Command::new("curl").arg("--version").output().is_ok_and(|o| o.status.success())
}

macro_rules! need_curl {
    () => {
        if !curl_available() {
            eprintln!("skipped: curl is not installed");
            return;
        }
    };
}

/// The checkouts in the cache's `git` store.
fn checkouts(w: &World) -> Vec<PathBuf> {
    let mut v = vec![];
    if let Ok(repos) = std::fs::read_dir(w.cache().join("git/checkouts")) {
        for r in repos.flatten() {
            for c in std::fs::read_dir(r.path()).unwrap().flatten() {
                if c.path().is_dir() {
                    v.push(c.path());
                }
            }
        }
    }
    v.sort();
    v
}

/// The unpacked archives in the cache's `url` store.
fn archive_dirs(w: &World) -> Vec<PathBuf> {
    let mut v = vec![];
    if let Ok(repos) = std::fs::read_dir(w.cache().join("git/url")) {
        for r in repos.flatten() {
            if !r.path().is_dir() {
                continue;
            }
            for c in std::fs::read_dir(r.path()).unwrap().flatten() {
                if c.path().is_dir() {
                    v.push(c.path());
                }
            }
        }
    }
    v.sort();
    v
}

fn age_stamp(dir: &Path, days: u64) {
    let mut name = dir.file_name().unwrap().to_os_string();
    name.push(".used");
    let stamp = dir.with_file_name(name);
    let t = std::time::SystemTime::now() - std::time::Duration::from_secs(days * 86400);
    std::fs::OpenOptions::new().create(true).append(true).open(stamp).unwrap().set_modified(t).unwrap();
}

#[cfg(unix)]
fn is_root() -> bool {
    // SAFETY: `geteuid` has no preconditions.
    unsafe { libc::geteuid() == 0 }
}

#[cfg(unix)]
fn mode(p: &Path) -> u32 {
    use std::os::unix::fs::PermissionsExt;
    std::fs::metadata(p).unwrap().permissions().mode() & 0o777
}

fn git_dep_app(w: &World) {
    sum_app(w, &format!("http = {{ git = \"{}\", tag = \"v1.0.0\" }}\n", w.url("http")), &["http"]);
}

#[cfg(unix)]
#[test]
fn checkouts_are_read_only_and_removable() {
    need_git!();
    let w = World::new("readonly");
    w.lib("http", 7, Some("v1.0.0"));
    git_dep_app(&w);
    w.gamma("app", &["fetch"]).success();
    let cos = checkouts(&w);
    assert_eq!(cos.len(), 1);
    for p in [cos[0].clone(), cos[0].join("lib.gm"), cos[0].join("gamma.toml")] {
        assert_eq!(mode(&p) & 0o222, 0, "{} is writable", p.display());
    }
    // Editing a dependency in place is refused (a superuser can write anywhere).
    if !is_root() {
        assert!(std::fs::write(cos[0].join("lib.gm"), "changed").is_err());
        assert!(std::fs::File::create(cos[0].join("new.gm")).is_err());
        assert!(std::fs::remove_file(cos[0].join("lib.gm")).is_err());
    }
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "7\n");
    // Deleting restores permissions first; the next build fetches and verifies again.
    let out = w.gamma("app", &["cache", "clean", "--all"]).success();
    assert!(out.stdout.contains("Removed 1 package checkout, 1 repository mirror"), "{}", out.stdout);
    assert!(checkouts(&w).is_empty());
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "7\n");
    assert!(out.stderr.contains("Fetching http"), "{}", out.stderr);
    assert_eq!(checkouts(&w).len(), 1);
    // A tampered checkout (changed by a superuser, say) is found when it is rebuilt from the lock.
    let lock = w.read("app/gamma.lock");
    w.gamma("app", &["cache", "clean", "--all"]).success();
    let manifest = lib_manifest("http");
    w.git("http", &["checkout", "-q", "-b", "other"]);
    w.write("http/lib.gm", &lib_source(99));
    w.write("http/gamma.toml", &manifest);
    w.commit("http", "other", None);
    // The lock still names the original revision, and the mirror has it.
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "7\n");
    assert_eq!(w.read("app/gamma.lock"), lock);
}

#[test]
fn cache_dir_and_clean() {
    need_git!();
    let w = World::new("cache_clean");
    w.lib("http", 7, Some("v1.0.0"));
    git_dep_app(&w);
    w.gamma("app", &["fetch"]).success();
    let out = w.gamma("app", &["cache", "dir"]).success();
    assert_eq!(out.stdout.trim(), w.cache().display().to_string());
    // Native builds, as `run --backend=c` and the runtime cache leave them.
    w.write("cache/run/prog-0123456789abcdef", "binary");
    w.write("cache/gm_rt-0123456789abcdef.o", "object");
    w.write("cache/gm_rt-0123456789abcdef.c", "source");
    w.write("cache/native/obj-0123.o", "object");
    w.write("cache/unrelated.txt", "not ours");
    let age = |p: &str, days: u64| {
        let t = std::time::SystemTime::now() - std::time::Duration::from_secs(days * 86400);
        std::fs::File::open(w.path(p)).unwrap().set_modified(t).unwrap();
    };
    age("cache/run/prog-0123456789abcdef", 40);
    age("cache/gm_rt-0123456789abcdef.o", 40);

    // Nothing is old enough for the default of 30 days: the checkout was just used.
    let out = w.gamma("app", &["cache", "clean"]).success();
    assert!(out.stdout.contains("Removed 0 ") || out.stdout.contains("2 cached builds"), "{}", out.stdout);
    assert!(!w.path("cache/run/prog-0123456789abcdef").exists());
    assert!(!w.path("cache/gm_rt-0123456789abcdef.o").exists() && !w.path("cache/gm_rt-0123456789abcdef.c").exists());
    assert!(w.path("cache/native/obj-0123.o").exists());
    assert_eq!(checkouts(&w).len(), 1);
    assert!(w.path("cache/unrelated.txt").exists());
    let out = w.gamma("app", &["cache", "clean"]).success();
    assert!(out.stdout.starts_with("Nothing to remove"), "{}", out.stdout);

    // Unused for 40 days: removed, with the mirror nothing needs any more.
    age_stamp(&checkouts(&w)[0], 40);
    let out = w.gamma("app", &["cache", "clean", "--dry-run"]).success();
    assert!(out.stdout.contains("Would remove 1 package checkout, 1 repository mirror"), "{}", out.stdout);
    assert_eq!(checkouts(&w).len(), 1);
    // A build that uses it marks it used again.
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "7\n");
    assert!(w.gamma("app", &["cache", "clean"]).success().stdout.starts_with("Nothing to remove"));
    age_stamp(&checkouts(&w)[0], 40);
    let out = w.gamma("app", &["cache", "clean"]).success();
    assert!(out.stdout.contains("Removed 1 package checkout, 1 repository mirror, 0 cached builds"), "{}", out.stdout);
    assert!(out.stdout.contains("freed"), "{}", out.stdout);
    assert!(checkouts(&w).is_empty());
    assert!(!w.path("cache/git/db").read_dir().is_ok_and(|mut d| d.next().is_some()));
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "7\n");
    assert!(out.stderr.contains("Fetching http"), "{}", out.stderr);

    // The age is a number of days; `--all` takes no age.
    let out = w.gamma("app", &["cache", "clean", "--older-than=0"]).success();
    assert!(out.stdout.contains("1 package checkout"), "{}", out.stdout);
    assert!(!w.gamma("app", &["cache", "clean", "--older-than=soon"]).ok);
    assert!(!w.gamma("app", &["cache", "clean", "--all", "--older-than=3"]).ok);
    assert!(!w.gamma("app", &["cache", "tidy"]).ok);
    assert!(!w.gamma("app", &["cache"]).ok);
    w.gamma("app", &["run", "."]).success();
    let out = w.gamma("app", &["cache", "clean", "--all"]).success();
    assert!(out.stdout.contains("Removed 1 package checkout"), "{}", out.stdout);
    assert!(w.path("cache/unrelated.txt").exists());
    assert!(!w.path("cache/native").exists());
}

#[cfg(unix)]
#[test]
fn symbolic_links_are_checksummed_and_must_stay_inside() {
    need_git!();
    let w = World::new("symlinks");
    let files = [("gamma.toml", lib_manifest("lnk")), ("lib.gm", lib_source(3))];
    let files: Vec<(&str, &str)> = files.iter().map(|(a, b)| (*a, b.as_str())).collect();
    w.repo("lnk", &files, None);
    std::os::unix::fs::symlink("lib.gm", w.path("lnk/alias.txt")).unwrap();
    std::fs::create_dir_all(w.path("lnk/sub/deep")).unwrap();
    std::os::unix::fs::symlink("../../lib.gm", w.path("lnk/sub/deep/up.txt")).unwrap();
    w.commit("lnk", "links", Some("v1.0.0"));
    // The same tree with the links replaced by copies.
    w.repo("cpy", &files, None);
    w.write("cpy/alias.txt", &lib_source(3));
    w.write("cpy/sub/deep/up.txt", &lib_source(3));
    w.commit("cpy", "copies", Some("v1.0.0"));
    let mut sums = vec![];
    for name in ["lnk", "cpy"] {
        sum_app(&w, &format!("{n} = {{ git = \"{}\", tag = \"v1.0.0\" }}\n", w.url(name), n = name), &[name]);
        w.gamma("app", &["fetch"]).success();
        assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "3\n");
        sums.push(lock_field(&w.read("app/gamma.lock"), "lnk", "checksum").unwrap());
    }
    assert_ne!(sums[0], sums[1]);

    // Links that leave the package are refused, and no checkout is left behind.
    for (i, target) in ["../../../etc/passwd", "/etc/passwd"].iter().enumerate() {
        let name = format!("esc{}", i);
        w.repo(&name, &files, None);
        std::os::unix::fs::symlink(target, w.path(&format!("{}/leak.txt", name))).unwrap();
        w.commit(&name, "escape", Some("v1.0.0"));
        let _ = std::fs::remove_file(w.path("app/gamma.lock"));
        sum_app(&w, &format!("lnk = {{ git = \"{}\", tag = \"v1.0.0\" }}\n", w.url(&name)), &["lnk"]);
        let before = checkouts(&w).len();
        let out = w.gamma("app", &["fetch"]).failure();
        assert!(out.stderr.contains("leak.txt") && out.stderr.contains("points outside the package"), "{}", out.stderr);
        assert_eq!(checkouts(&w).len(), before);
    }
}

/// Writes a gzip-compressed tar file with `files` and symbolic `links` (path, target).
fn make_tgz(path: &Path, files: &[(&str, &str)], links: &[(&str, &str)]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let mut b = tar::Builder::new(Vec::new());
    for (p, data) in files {
        let mut h = tar::Header::new_gnu();
        h.set_size(data.len() as u64);
        h.set_mode(0o644);
        h.set_mtime(0);
        h.set_cksum();
        b.append_data(&mut h, p, data.as_bytes()).unwrap();
    }
    for (p, target) in links {
        let mut h = tar::Header::new_gnu();
        h.set_entry_type(tar::EntryType::Symlink);
        h.set_size(0);
        h.set_mode(0o777);
        h.set_mtime(0);
        h.set_cksum();
        b.append_link(&mut h, p, target).unwrap();
    }
    let data = b.into_inner().unwrap();
    let mut gz = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::fast());
    std::io::Write::write_all(&mut gz, &data).unwrap();
    std::fs::write(path, gz.finish().unwrap()).unwrap();
}

fn sha256_of(path: &Path) -> String {
    use sha2::Digest;
    sha2::Sha256::digest(std::fs::read(path).unwrap()).iter().map(|b| format!("{:02x}", b)).collect()
}

fn util_archive(w: &World, file: &str, n: i32) {
    make_tgz(
        &w.path(file),
        &[("util-1.0.0/gamma.toml", &lib_manifest("util")), ("util-1.0.0/lib.gm", &lib_source(n))],
        &[],
    );
}

#[test]
fn archive_dependencies_are_downloaded_locked_and_verified() {
    need_curl!();
    let w = World::new("archives");
    util_archive(&w, "dist/util-1.0.0.tar.gz", 5);
    let url = w.url("dist/util-1.0.0.tar.gz");
    let hash = sha256_of(&w.path("dist/util-1.0.0.tar.gz"));
    sum_app(&w, &format!("util = {{ url = \"{}\" }}\n", url), &["util"]);

    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "5\n");
    assert!(out.stderr.contains("Fetching util") && out.stderr.contains("Locking util archive"), "{}", out.stderr);
    let lock = w.read("app/gamma.lock");
    assert_eq!(lock_field(&lock, "util", "source").unwrap(), format!("url+{}", url));
    assert_eq!(lock_field(&lock, "util", "archive").unwrap(), format!("sha256:{}", hash));
    assert!(lock_field(&lock, "util", "checksum").unwrap().starts_with("sha256:"));
    assert!(lock_field(&lock, "util", "rev").is_none());
    // Nothing is downloaded again, and the lock can be enforced.
    let out = w.gamma("app", &["run", "."]).success();
    assert!(!out.stderr.contains("Fetching"), "{}", out.stderr);
    w.gamma("app", &["run", "--offline", "--locked", "."]).success();
    w.gamma("app", &["build", ".", "-o", "app.out"]).success();
    assert_eq!(w.read("app/gamma.lock"), lock);
    // The unpacked archive is in the store, read-only, without the top-level directory.
    let dirs = archive_dirs(&w);
    assert_eq!(dirs.len(), 1);
    assert!(dirs[0].join("lib.gm").is_file() && dirs[0].join("gamma.toml").is_file());
    #[cfg(unix)]
    assert_eq!(mode(&dirs[0]) & 0o222, 0);

    // `update` downloads again only if the URL changed: a different file at the same URL is ignored.
    util_archive(&w, "dist/util-1.0.0.tar.gz", 6);
    let out = w.gamma("app", &["update"]).success();
    assert!(!out.stderr.contains("Fetching"), "{}", out.stderr);
    assert_eq!(w.read("app/gamma.lock"), lock);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "5\n");
    // With the cache gone, the locked hash catches the changed file...
    force_remove(&w.cache());
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("SHA-256") && out.stderr.contains("gamma.lock says"), "{}", out.stderr);
    assert!(w.gamma("app", &["run", "--offline", "."]).failure().stderr.contains("--offline"));
    // ...and the original file is accepted again.
    util_archive(&w, "dist/util-1.0.0.tar.gz", 5);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "5\n");
    assert_eq!(w.read("app/gamma.lock"), lock);

    // A new URL is a new package version.
    util_archive(&w, "dist/util-1.1.0.tar.gz", 6);
    let url2 = w.url("dist/util-1.1.0.tar.gz");
    let hash2 = sha256_of(&w.path("dist/util-1.1.0.tar.gz"));
    sum_app(&w, &format!("util = {{ url = \"{}\" }}\n", url2), &["util"]);
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "6\n");
    assert!(out.stderr.contains("Fetching util"), "{}", out.stderr);
    assert_eq!(lock_field(&w.read("app/gamma.lock"), "util", "archive").unwrap(), format!("sha256:{}", hash2));

    // A `sha256` in the manifest must match the file.
    let bad = "0".repeat(64);
    sum_app(&w, &format!("util = {{ url = \"{}\", sha256 = \"{}\" }}\n", url2, bad), &["util"]);
    let out = w.gamma("app", &["run", "."]).failure();
    assert!(out.stderr.contains("SHA-256") && out.stderr.contains("gamma.toml says"), "{}", out.stderr);
    sum_app(&w, &format!("util = {{ url = \"{}\", sha256 = \"{}\" }}\n", url2, hash2.to_uppercase()), &["util"]);
    let out = w.gamma("app", &["run", "."]).success();
    assert_eq!(out.stdout, "6\n");
    // A hash in the manifest also stops a first download of another file.
    let _ = std::fs::remove_file(w.path("app/gamma.lock"));
    force_remove(&w.cache());
    sum_app(&w, &format!("util = {{ url = \"{}\", sha256 = \"{}\" }}\n", url, hash2), &["util"]);
    assert!(w.gamma("app", &["run", "."]).failure().stderr.contains("gamma.toml says"));
}

#[test]
fn archive_subdirectories_add_and_bad_archives() {
    need_curl!();
    let w = World::new("archives2");
    make_tgz(
        &w.path("dist/mono.tgz"),
        &[
            ("mono-2/README", "a monorepo"),
            ("mono-2/pkgs/inner/gamma.toml", &lib_manifest("inner")),
            ("mono-2/pkgs/inner/lib.gm", &lib_source(8)),
        ],
        &[],
    );
    let url = w.url("dist/mono.tgz");
    let hash = sha256_of(&w.path("dist/mono.tgz"));
    sum_app(&w, &format!("inner = {{ url = \"{}\", subdir = \"pkgs/inner\" }}\n", url), &["inner"]);
    assert_eq!(w.gamma("app", &["run", "."]).success().stdout, "8\n");
    let lock = w.read("app/gamma.lock");
    assert_eq!(lock_field(&lock, "inner", "subdir").as_deref(), Some("pkgs/inner"));
    // A subdirectory that is not there.
    sum_app(&w, &format!("inner = {{ url = \"{}\", subdir = \"pkgs/nope\" }}\n", url), &["inner"]);
    assert!(w.gamma("app", &["run", "."]).failure().stderr.contains("has no directory `pkgs/nope`"));

    // `gamma add --url`.
    w.write("app2/gamma.toml", "# my app\n[package]\nname = \"app2\"\n");
    w.write(
        "app2/main.gm",
        "inner :: import \"inner\"\nmain :: fn() \\ {Out} {\n    print(\"{}\\n\", inner.value())\n}\n",
    );
    w.gamma("app2", &["add", "inner", "--url", &url, "--sha256", &hash, "--subdir", "pkgs/inner"]).success();
    let manifest = w.read("app2/gamma.toml");
    assert!(manifest.starts_with("# my app\n"), "{}", manifest);
    assert!(
        manifest.contains(&format!("inner = {{ url = \"{}\", sha256 = \"{}\", subdir = \"pkgs/inner\" }}", url, hash)),
        "{}",
        manifest
    );
    assert_eq!(w.gamma("app2", &["run", "."]).success().stdout, "8\n");
    assert!(w.read("app2/gamma.lock").contains("archive = "));
    // A failing add leaves the manifest as it was.
    let before = w.read("app2/gamma.toml");
    let out = w.gamma("app2", &["add", "other", "--url", &w.url("dist/missing.tgz")]).failure();
    assert!(out.stderr.contains("cannot download"), "{}", out.stderr);
    assert_eq!(w.read("app2/gamma.toml"), before);
    assert!(!w.gamma("app2", &["add", "x", "--url", &url, "--git", &url]).ok);
    assert!(!w.gamma("app2", &["add", "x", "--path", "../y", "--sha256", &hash]).ok);
    w.gamma("app2", &["remove", "inner"]).success();
    assert!(!w.path("app2/gamma.lock").exists());

    // Archives that are not acceptable.
    w.write("dist/plain.tgz", "this is not an archive");
    sum_app(&w, &format!("bad = {{ url = \"{}\" }}\n", w.url("dist/plain.tgz")), &["bad"]);
    assert!(w.gamma("app", &["fetch"]).failure().stderr.contains("gzip"));
    make_tgz(
        &w.path("dist/dots.tgz"),
        &[("pkg/gamma.toml", &lib_manifest("bad"))],
        &[("pkg/leak", "../../../etc/passwd")],
    );
    sum_app(&w, &format!("bad = {{ url = \"{}\" }}\n", w.url("dist/dots.tgz")), &["bad"]);
    let out = w.gamma("app", &["fetch"]).failure();
    assert!(out.stderr.contains("points outside the package"), "{}", out.stderr);
    assert!(archive_dirs(&w).iter().all(|d| !d.join("leak").exists()));
    // Manifest errors.
    for (dep, msg) in [
        ("x = { url = \"u\", sha256 = \"abc\" }", "64 hexadecimal digits"),
        ("x = { url = \"u\", git = \"g\" }", "both `git` and `url`"),
        ("x = { path = \"p\", sha256 = \"abc\" }", "needs `url`"),
        ("x = { url = \"u\", tag = \"t\" }", "needs `git`"),
        ("x = { url = \"\" }", "is empty"),
    ] {
        w.write("app/gamma.toml", &format!("[package]\nname = \"app\"\n[dependencies]\n{}\n", dep));
        let out = w.gamma("app", &["check", "."]).failure();
        assert!(out.stderr.contains("gamma.toml:4: ") && out.stderr.contains(msg), "{}\n{}", dep, out.stderr);
    }
}
