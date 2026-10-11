//! The C compiler the C back end invokes, and the target it compiles for.
//!
//! The compiler comes from the environment, as for `make`:
//!
//! - `CC` is the compiler command, which may carry arguments
//!   (`zig cc -target aarch64-linux-gnu`, `clang --target=riscv64-linux-gnu`);
//!   the default is `cc`.
//! - `CFLAGS` are extra compiler options, given after the back end's own.
//! - `LDFLAGS` are extra linker options, given after the source file.
//!
//! Each is split into words at whitespace; single or double quotes keep
//! whitespace inside one word, and a backslash takes the next character
//! literally.
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

impl Default for Toolchain {
    fn default() -> Self {
        Toolchain { cc: vec!["cc".into()], cflags: vec![], ldflags: vec![] }
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
                t.cc = vec!["cc".into()];
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
        target_from_macros(&String::from_utf8_lossy(&out.stdout))
            .map_err(|e| format!("the C compiler `{}` {}", self.display(), e))
    }
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
    Ok(Target { ptr_bytes, little_endian, int128_align, os, arch })
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
        assert!(Toolchain::from_vars(Some("cc"), None, None).unwrap().is_default());
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
        assert_eq!(a, Target { ptr_bytes: 8, little_endian: true, int128_align: 16, os: "MacOS", arch: "Aarch64" });
        let s =
            t(&["__SIZEOF_POINTER__ 8", "__BYTE_ORDER__ __ORDER_BIG_ENDIAN__", "__s390x__ 1", "__linux__ 1"]).unwrap();
        assert_eq!(s, Target { ptr_bytes: 8, little_endian: false, int128_align: 8, os: "Linux", arch: "Other" });
        let r = t(&["__SIZEOF_POINTER__ 8", "__riscv 1", "__riscv_xlen 64", "__linux__ 1"]).unwrap();
        assert_eq!((r.arch, r.os), ("Riscv64", "Linux"));
        let w = t(&["__SIZEOF_POINTER__ 4", "__wasm32__ 1", "__wasi__ 1"]).unwrap();
        assert_eq!((w.ptr_bytes, w.arch, w.os), (4, "Wasm32", "Freestanding"));
        assert!(target_from_macros(&defs(&["__SIZEOF_POINTER__ 4", "__i386__ 1"])).unwrap_err().contains("__int128"));
        assert!(target_from_macros(&defs(&["__x86_64__ 1"])).is_err());
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
