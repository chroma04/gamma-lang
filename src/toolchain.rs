//! The C compiler the C back end invokes, and the target it compiles for.
//!
//! The compiler comes from the environment, as for `make`:
//!
//! - `CC` is the compiler command, which may carry arguments
//!   (`zig cc -target aarch64-linux-gnu`, `clang --target=riscv64-linux-gnu`);
//!   the default is `cc` (on Windows the first of `cc`, `gcc` and `clang` on `PATH`; it
//!   must be a MinGW-w64 compiler, not MSVC's `cl`).
//! - `CFLAGS` are extra compiler options, given after the back end's own.
//! - `LDFLAGS` are extra linker options, given after the source file.
//!
//! Each is split into words at whitespace; single or double quotes keep
//! whitespace inside one word, and a backslash takes the next character
//! literally.
//!
//! `--target=<triple>` (see [`Toolchain::for_target`]) picks a cross compiler for a
//! target triple, and `GAMMA_RUNNER` ([`runner_from_env`]) names the emulator that
//! runs the executables such a compiler builds.
//!
//! When any of them is set, the target is asked of the compiler itself
//! ([`Toolchain::target`]), so a cross compiler yields layouts for the machine
//! it compiles for rather than the one `gamma` runs on.

use crate::layout::Target;
use std::process::{Command, Stdio};

/// A C compiler command and the extra options it is given.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Toolchain {
    /// The compiler program followed by its own arguments (from `CC`).
    pub cc: Vec<String>,
    /// Extra compiler options (`CFLAGS`).
    pub cflags: Vec<String>,
    /// Extra linker options (`LDFLAGS`).
    pub ldflags: Vec<String>,
}

/// The compiler used when `CC` is unset: `cc`; on Windows, where MinGW-w64 installations
/// differ in which of these they provide, the first of `cc`, `gcc` and `clang` on `PATH`.
fn default_cc() -> String {
    static CC: std::sync::OnceLock<String> = std::sync::OnceLock::new();
    CC.get_or_init(|| {
        if cfg!(windows) {
            for prog in ["cc", "gcc", "clang"] {
                if program_on_path(prog) {
                    return prog.to_string();
                }
            }
        }
        "cc".to_string()
    })
    .clone()
}

/// Whether `prog` (with `.exe` appended, on Windows) is a file in a `PATH` directory.
fn program_on_path(prog: &str) -> bool {
    std::env::var_os("PATH").is_some_and(|p| {
        std::env::split_paths(&p).any(|d| d.join(prog).is_file() || d.join(format!("{prog}.exe")).is_file())
    })
}

impl Default for Toolchain {
    fn default() -> Self {
        Toolchain { cc: vec![default_cc()], cflags: vec![], ldflags: vec![] }
    }
}

impl Toolchain {
    /// The toolchain `CC`, `CFLAGS` and `LDFLAGS` describe.
    pub fn from_env() -> Result<Toolchain, String> {
        let var = |name: &str| std::env::var(name).ok().filter(|v| !v.trim().is_empty());
        Toolchain::from_vars(var("CC").as_deref(), var("CFLAGS").as_deref(), var("LDFLAGS").as_deref())
    }

    /// The toolchain given the values of `CC`, `CFLAGS` and `LDFLAGS` (`None` when
    /// unset).
    pub fn from_vars(cc: Option<&str>, cflags: Option<&str>, ldflags: Option<&str>) -> Result<Toolchain, String> {
        let mut t = Toolchain::default();
        if let Some(cc) = cc {
            t.cc = split_words(cc).map_err(|e| format!("cannot read CC: {}", e))?;
            if t.cc.is_empty() {
                t.cc = vec![default_cc()];
            }
        }
        if let Some(f) = cflags {
            t.cflags = split_words(f).map_err(|e| format!("cannot read CFLAGS: {}", e))?;
        }
        if let Some(f) = ldflags {
            t.ldflags = split_words(f).map_err(|e| format!("cannot read LDFLAGS: {}", e))?;
        }
        Ok(t)
    }

    /// The toolchain for building for the target triple `triple`
    /// (`--target=s390x-linux-gnu`), from the environment.
    pub fn for_target(triple: &str) -> Result<Toolchain, String> {
        let var = |name: &str| std::env::var(name).ok().filter(|v| !v.trim().is_empty());
        Toolchain::for_triple(
            triple,
            var("CC").as_deref(),
            var("CFLAGS").as_deref(),
            var("LDFLAGS").as_deref(),
            &program_on_path,
        )
    }

    /// The toolchain for `triple` given the values of `CC`, `CFLAGS` and `LDFLAGS`
    /// and a test for whether a program is on `PATH`:
    ///
    /// - With `CC` unset, the compiler is `<triple>-gcc` if that is on `PATH`, and
    ///   otherwise `clang --target=<triple>` if `clang` is.
    /// - With `CC` set, it is used as written, except that `--target=<triple>` is
    ///   added when it is clang (`-target <triple>` for `zig cc`) and names no target
    ///   itself. A gcc must already be the cross compiler for the triple.
    ///
    /// Whether the resulting compiler really produces code for `triple` is checked
    /// by [`Toolchain::target_for`].
    pub fn for_triple(
        triple: &str,
        cc: Option<&str>,
        cflags: Option<&str>,
        ldflags: Option<&str>,
        on_path: &dyn Fn(&str) -> bool,
    ) -> Result<Toolchain, String> {
        if triple.is_empty() || triple.contains(char::is_whitespace) {
            return Err(format!("`{}` is not a target triple (such as `aarch64-linux-gnu`)", triple));
        }
        match cc {
            Some(cc) => {
                let mut t = Toolchain::from_vars(Some(cc), cflags, ldflags)?;
                let prog =
                    std::path::Path::new(&t.cc[0]).file_name().map_or(String::new(), |n| n.to_string_lossy().into());
                let names_target = t.cc.iter().any(|w| w == "-target" || w.starts_with("--target"));
                if !names_target {
                    if prog.contains("clang") {
                        t.cc.push(format!("--target={}", triple));
                    } else if prog == "zig" && t.cc.get(1).is_some_and(|w| w == "cc" || w == "c++") {
                        t.cc.extend(["-target".to_string(), triple.to_string()]);
                    }
                }
                Ok(t)
            }
            None => {
                let mut t = Toolchain::from_vars(None, cflags, ldflags)?;
                let gcc = format!("{}-gcc", triple);
                if on_path(&gcc) {
                    t.cc = vec![gcc];
                } else if on_path("clang") {
                    t.cc = vec!["clang".into(), format!("--target={}", triple)];
                } else {
                    return Err(format!(
                        "no C compiler for the target `{triple}`: `{triple}-gcc` and `clang` are not on PATH; install a cross compiler or set CC"
                    ));
                }
                Ok(t)
            }
        }
    }

    /// The toolchain of a Safe build (§18.7), which compiles with Fil-C: as
    /// [`Toolchain::from_env`], except that without `CC` the compiler is Fil-C's
    /// `filcc`, found on `PATH` or in `/opt/fil/bin`. Whether the compiler really is
    /// Fil-C is up to the caller to check, with [`Toolchain::target`].
    pub fn for_safe_build() -> Result<Toolchain, String> {
        let mut t = Toolchain::from_env()?;
        if std::env::var("CC").is_ok_and(|v| !v.trim().is_empty()) {
            return Ok(t);
        }
        let on_path = std::env::var_os("PATH")
            .and_then(|p| std::env::split_paths(&p).map(|d| d.join("filcc")).find(|f| f.is_file()));
        let installed = std::path::Path::new("/opt/fil/bin/filcc");
        let filcc = on_path.or_else(|| installed.is_file().then(|| installed.to_path_buf())).ok_or(
            "Safe builds compile with the Fil-C compiler (https://fil-c.org), but `filcc` is neither on PATH nor in /opt/fil/bin; install it or set CC",
        )?;
        t.cc = vec![filcc.display().to_string()];
        Ok(t)
    }

    /// Whether this is the plain system compiler (`cc` with no extra options), which
    /// compiles for the host.
    pub fn is_default(&self) -> bool {
        *self == Toolchain::default()
    }

    /// The compiler command as written, for messages.
    pub fn display(&self) -> String {
        self.cc.join(" ")
    }

    /// A command running the compiler with its own arguments, before `CFLAGS`.
    pub fn base_command(&self) -> Command {
        let mut cmd = Command::new(&self.cc[0]);
        cmd.args(&self.cc[1..]);
        cmd
    }

    /// A command running the compiler with its own arguments, `opts` (the caller's
    /// options) and then `CFLAGS`, so the user's flags take precedence.
    pub fn command<S: AsRef<std::ffi::OsStr>>(&self, opts: &[S]) -> Command {
        let mut cmd = self.base_command();
        cmd.args(opts).args(&self.cflags);
        cmd
    }

    /// The target the compiler produces code for. The host when this is the default
    /// toolchain; otherwise the compiler is asked for its predefined macros.
    pub fn target(&self) -> Result<Target, String> {
        if self.is_default() {
            return Ok(Target::host());
        }
        let macros = self.macros()?;
        target_from_macros(&macros).map_err(|e| format!("the C compiler `{}` {}", self.display(), e))
    }

    /// As [`Toolchain::target`], and checks that the compiler produces code for the
    /// target triple `triple` (its architecture, operating system and byte order).
    pub fn target_for(&self, triple: &str) -> Result<Target, String> {
        let macros = self.macros()?;
        let target = target_from_macros(&macros).map_err(|e| format!("the C compiler `{}` {}", self.display(), e))?;
        check_triple(triple, &macros).map_err(|e| format!("the C compiler `{}` {}", self.display(), e))?;
        Ok(target)
    }

    /// The compiler's predefined macros (`cc -dM -E`).
    fn macros(&self) -> Result<String, String> {
        let out = self
            .command::<&str>(&[])
            .args(["-dM", "-E", "-x", "c", "-"])
            .stdin(Stdio::null())
            .output()
            .map_err(|e| format!("cannot run the C compiler `{}`: {}", self.display(), e))?;
        if !out.status.success() {
            return Err(format!(
                "cannot ask the C compiler `{}` for its target:\n{}",
                self.display(),
                String::from_utf8_lossy(&out.stderr)
            ));
        }
        Ok(String::from_utf8_lossy(&out.stdout).into_owned())
    }
}

/// The command prefix from `GAMMA_RUNNER` that runs executables built for another
/// machine (`qemu-s390x -L /usr/s390x-linux-gnu`), empty when it is unset.
pub fn runner_from_env() -> Result<Vec<String>, String> {
    match std::env::var("GAMMA_RUNNER") {
        Ok(v) => split_words(&v).map_err(|e| format!("cannot read GAMMA_RUNNER: {}", e)),
        Err(_) => Ok(vec![]),
    }
}

/// Checks that the compiler whose predefined macros are `macros` produces code for
/// the target triple `triple`. The error completes the sentence "the C compiler … ".
pub fn check_triple(triple: &str, macros: &str) -> Result<(), String> {
    let mut parts = triple.split('-');
    let arch = parts.next().unwrap_or("");
    let rest: Vec<&str> = parts.collect();
    // The architecture's own macros, and whether it is big-endian.
    let (arch_macro, big): (&[&str], bool) = match arch {
        "x86_64" | "amd64" | "x86_64h" => (&["__x86_64__"], false),
        "aarch64" | "arm64" | "arm64e" => (&["__aarch64__"], false),
        "aarch64_be" | "arm64_be" => (&["__aarch64__"], true),
        "riscv64" => (&["__riscv"], false),
        "s390x" => (&["__s390x__"], true),
        "powerpc64" | "ppc64" => (&["__powerpc64__", "__PPC64__"], true),
        "powerpc64le" | "ppc64le" => (&["__powerpc64__", "__PPC64__"], false),
        "mips64" => (&["__mips64", "__mips64__"], true),
        "mips64el" => (&["__mips64", "__mips64__"], false),
        "sparc64" => (&["__sparc__"], true),
        "wasm32" => (&["__wasm32__"], false),
        "loongarch64" => (&["__loongarch64"], false),
        _ => return Err(format!("cannot be checked against `{}`: unknown architecture `{}`", triple, arch)),
    };
    let defined =
        |m: &str| macros.lines().any(|l| l.strip_prefix("#define ").and_then(|l| l.split(' ').next()) == Some(m));
    if !arch_macro.iter().any(|m| defined(m)) {
        return Err(format!("does not target the architecture of `{}` (it does not define {})", triple, arch_macro[0]));
    }
    let target = target_from_macros(macros)?;
    if big == target.little_endian {
        return Err(format!(
            "targets a {}-endian machine, but `{}` is {}-endian",
            if target.little_endian { "little" } else { "big" },
            triple,
            if big { "big" } else { "little" }
        ));
    }
    let os_macro = if rest.iter().any(|c| c.starts_with("linux")) {
        Some(("linux", "__linux__"))
    } else if rest.iter().any(|c| c.starts_with("darwin") || *c == "apple") {
        Some(("darwin", "__APPLE__"))
    } else if rest.iter().any(|c| c.starts_with("freebsd")) {
        Some(("freebsd", "__FreeBSD__"))
    } else if rest.iter().any(|c| c.starts_with("windows") || *c == "mingw32") {
        Some(("windows", "_WIN32"))
    } else {
        None
    };
    if let Some((os, m)) = os_macro
        && !defined(m)
    {
        return Err(format!("does not target {}, which `{}` names", os, triple));
    }
    Ok(())
}

/// The target described by a C compiler's predefined macros (the output of
/// `cc -dM -E`). The error completes the sentence "the C compiler … ".
pub fn target_from_macros(macros: &str) -> Result<Target, String> {
    let mut defs = std::collections::HashMap::new();
    for line in macros.lines() {
        let mut parts = line.strip_prefix("#define ").unwrap_or("").splitn(2, ' ');
        if let Some(name) = parts.next().filter(|n| !n.is_empty()) {
            defs.insert(name, parts.next().unwrap_or("").trim());
        }
    }
    let has = |m: &str| defs.contains_key(m);
    let num = |m: &str| defs.get(m).and_then(|v| v.parse::<u64>().ok());
    let ptr_bytes = num("__SIZEOF_POINTER__").ok_or("does not define __SIZEOF_POINTER__")?;
    if ptr_bytes != 4 && ptr_bytes != 8 {
        return Err(format!("targets {}-byte pointers; only 4 and 8 are supported", ptr_bytes));
    }
    if !has("__SIZEOF_INT128__") {
        return Err("targets a machine without `__int128`, which the C back end needs (a 64-bit target)".into());
    }
    // `__BYTE_ORDER__` is defined as one of the `__ORDER_…__` macros or as its value.
    let value = |m: &str| defs.get(m).map(|v| defs.get(v).copied().unwrap_or(v));
    let little_endian = match (value("__BYTE_ORDER__"), value("__ORDER_BIG_ENDIAN__")) {
        (Some(order), Some(big)) => order != big,
        _ => true,
    };
    let arch = if has("__x86_64__") {
        "X86_64"
    } else if has("__aarch64__") {
        "Aarch64"
    } else if has("__riscv") && num("__riscv_xlen") == Some(64) {
        "Riscv64"
    } else if has("__s390x__") {
        "S390x"
    } else if has("__wasm32__") {
        "Wasm32"
    } else {
        "Other"
    };
    let os = if has("__linux__") {
        "Linux"
    } else if has("__APPLE__") {
        "MacOS"
    } else if has("_WIN32") {
        "Windows"
    } else if has("__FreeBSD__") {
        "FreeBSD"
    } else {
        "Freestanding"
    };
    // `__int128` is 16-byte aligned everywhere it exists except on s390x.
    let int128_align = if has("__s390x__") { 8 } else { 16 };
    Ok(Target {
        ptr_bytes,
        little_endian,
        int128_align,
        os,
        arch,
        filc: has("__FILC__"),
        f16: has("__FLT16_MANT_DIG__"),
    })
}

/// Splits `s` into words as a shell would, without expansions: at unquoted
/// whitespace, with `'…'` and `"…"` quoting and `\` escaping one character.
pub fn split_words(s: &str) -> Result<Vec<String>, String> {
    let mut words = vec![];
    let mut word = String::new();
    let mut in_word = false;
    let mut chars = s.chars();
    while let Some(c) = chars.next() {
        match c {
            c if c.is_whitespace() => {
                if in_word {
                    words.push(std::mem::take(&mut word));
                    in_word = false;
                }
            }
            '\'' | '"' => {
                in_word = true;
                loop {
                    match chars.next() {
                        None => return Err(format!("unterminated {} quote", c)),
                        Some(q) if q == c => break,
                        Some('\\') if c == '"' => match chars.next() {
                            Some(e) => word.push(e),
                            None => return Err("`\\` at the end".into()),
                        },
                        Some(x) => word.push(x),
                    }
                }
            }
            '\\' => {
                in_word = true;
                match chars.next() {
                    Some(e) => word.push(e),
                    None => return Err("`\\` at the end".into()),
                }
            }
            c => {
                in_word = true;
                word.push(c);
            }
        }
    }
    if in_word {
        words.push(word);
    }
    Ok(words)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splits_words() {
        let w = |s: &str| split_words(s).unwrap();
        assert_eq!(w("zig cc -target aarch64-linux-gnu"), ["zig", "cc", "-target", "aarch64-linux-gnu"]);
        assert_eq!(w("  cc  "), ["cc"]);
        assert_eq!(w(""), Vec::<String>::new());
        assert_eq!(w("'/opt/my tools/cc' -DX=\"a b\""), ["/opt/my tools/cc", "-DX=a b"]);
        assert_eq!(w(r"a\ b ''"), ["a b", ""]);
        assert_eq!(w(r#""say \"hi\"""#), [r#"say "hi""#]);
        assert!(split_words("'open").is_err());
        assert!(split_words("x\\").is_err());
    }

    #[test]
    fn reads_variables() {
        let t =
            Toolchain::from_vars(Some("clang --target=aarch64-linux-gnu"), Some("-O1 -g"), Some("-static")).unwrap();
        assert_eq!(t.cc, ["clang", "--target=aarch64-linux-gnu"]);
        assert_eq!(t.cflags, ["-O1", "-g"]);
        assert_eq!(t.ldflags, ["-static"]);
        assert!(!t.is_default());
        assert!(Toolchain::from_vars(None, None, None).unwrap().is_default());
        assert!(Toolchain::from_vars(Some(&default_cc()), None, None).unwrap().is_default());
    }

    fn defs(lines: &[&str]) -> String {
        lines.iter().map(|l| format!("#define {}\n", l)).collect()
    }

    #[test]
    fn targets_from_macros() {
        let base = ["__SIZEOF_INT128__ 16", "__ORDER_LITTLE_ENDIAN__ 1234", "__ORDER_BIG_ENDIAN__ 4321"];
        let t = |extra: &[&str]| target_from_macros(&defs(&[&base[..], extra].concat()));
        assert!(!t(&["__SIZEOF_POINTER__ 8", "__BYTE_ORDER__ 4321"]).unwrap().little_endian);
        let a = t(&["__SIZEOF_POINTER__ 8", "__BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__", "__aarch64__ 1", "__APPLE__ 1"])
            .unwrap();
        assert_eq!(
            a,
            Target {
                ptr_bytes: 8,
                little_endian: true,
                int128_align: 16,
                os: "MacOS",
                arch: "Aarch64",
                filc: false,
                f16: false
            }
        );
        let s =
            t(&["__SIZEOF_POINTER__ 8", "__BYTE_ORDER__ __ORDER_BIG_ENDIAN__", "__s390x__ 1", "__linux__ 1"]).unwrap();
        assert_eq!(
            s,
            Target {
                ptr_bytes: 8,
                little_endian: false,
                int128_align: 8,
                os: "Linux",
                arch: "S390x",
                filc: false,
                f16: false
            }
        );
        let r = t(&["__SIZEOF_POINTER__ 8", "__riscv 1", "__riscv_xlen 64", "__linux__ 1"]).unwrap();
        assert_eq!((r.arch, r.os), ("Riscv64", "Linux"));
        let w = t(&["__SIZEOF_POINTER__ 4", "__wasm32__ 1", "__wasi__ 1"]).unwrap();
        assert_eq!((w.ptr_bytes, w.arch, w.os), (4, "Wasm32", "Freestanding"));
        assert!(target_from_macros(&defs(&["__SIZEOF_POINTER__ 4", "__i386__ 1"])).unwrap_err().contains("__int128"));
        assert!(target_from_macros(&defs(&["__x86_64__ 1"])).is_err());
    }

    #[test]
    fn selects_a_compiler_for_a_triple() {
        let t = "s390x-linux-gnu";
        let sel = |cc: Option<&str>, path: &[&str]| {
            Toolchain::for_triple(t, cc, Some("-O1"), Some("-static"), &|p| path.contains(&p))
        };
        // CC unset: the cross gcc, then clang.
        assert_eq!(sel(None, &["s390x-linux-gnu-gcc", "clang"]).unwrap().cc, ["s390x-linux-gnu-gcc"]);
        let c = sel(None, &["clang"]).unwrap();
        assert_eq!(c.cc, ["clang", "--target=s390x-linux-gnu"]);
        assert_eq!((c.cflags, c.ldflags), (vec!["-O1".to_string()], vec!["-static".to_string()]));
        assert!(sel(None, &["gcc"]).unwrap_err().contains("s390x-linux-gnu-gcc"));
        // CC set: --target is added to clang only, and only if it names none.
        assert_eq!(sel(Some("clang-18"), &[]).unwrap().cc, ["clang-18", "--target=s390x-linux-gnu"]);
        assert_eq!(
            sel(Some("/usr/bin/clang -fPIC"), &[]).unwrap().cc,
            ["/usr/bin/clang", "-fPIC", "--target=s390x-linux-gnu"]
        );
        assert_eq!(
            sel(Some("clang --target=aarch64-linux-gnu"), &[]).unwrap().cc,
            ["clang", "--target=aarch64-linux-gnu"]
        );
        assert_eq!(sel(Some("zig cc"), &[]).unwrap().cc, ["zig", "cc", "-target", "s390x-linux-gnu"]);
        assert_eq!(sel(Some("s390x-linux-gnu-gcc"), &[]).unwrap().cc, ["s390x-linux-gnu-gcc"]);
        assert_eq!(sel(Some("gcc"), &[]).unwrap().cc, ["gcc"]);
        assert!(Toolchain::for_triple("", None, None, None, &|_| true).is_err());
    }

    #[test]
    fn checks_the_compiler_against_a_triple() {
        let m = |extra: &[&str]| {
            defs(&[&["__SIZEOF_INT128__ 16", "__SIZEOF_POINTER__ 8", "__ORDER_BIG_ENDIAN__ 4321"][..], extra].concat())
        };
        let s390x = m(&["__BYTE_ORDER__ 4321", "__s390x__ 1", "__linux__ 1"]);
        assert!(check_triple("s390x-linux-gnu", &s390x).is_ok());
        assert!(check_triple("s390x-unknown-linux-gnu", &s390x).is_ok());
        assert!(check_triple("aarch64-linux-gnu", &s390x).unwrap_err().contains("__aarch64__"));
        assert!(check_triple("s390x-apple-darwin", &s390x).unwrap_err().contains("darwin"));
        assert!(check_triple("s390x-linux-gnu", &m(&["__BYTE_ORDER__ 1234", "__s390x__ 1", "__linux__ 1"])).is_err());
        let a64 = m(&["__BYTE_ORDER__ 1234", "__aarch64__ 1", "__linux__ 1"]);
        assert!(check_triple("aarch64-linux-gnu", &a64).is_ok());
        assert!(check_triple("aarch64_be-linux-gnu", &a64).unwrap_err().contains("little-endian"));
        assert!(check_triple("vax-linux", &a64).unwrap_err().contains("unknown architecture"));
    }

    /// The host compiler, asked through a non-default toolchain, reports the host.
    #[test]
    fn probes_host_compiler() {
        let mut t = Toolchain::default();
        if !t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()) {
            return;
        }
        t.cflags.push("-DGAMMA_PROBE".into());
        assert_eq!(t.target().unwrap(), Target::host());
    }
}
