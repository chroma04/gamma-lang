//! Splitting generated C into several translation units, compiled in parallel.
//!
//! A Debug build links the separately compiled runtime (see [`super::compile`]) and favors
//! turnaround over whole-program optimization, and the C compiler's time is almost all
//! spent on the function bodies. Such a build may split the program: every unit includes a shared header (types, frames, declarations of every
//! function and every piece of static data), unit 0 also defines the data and holds the
//! entry code, and the function bodies are dealt out among the units.
//!
//! The generated code is written with internal linkage (`static`), as one unit needs. A
//! split gives the functions and data external linkage instead: their names all start
//! with `gm_`, as do the runtime's, so they cannot clash with the C library or the
//! program's own C code. Small `static` helpers of the shared header stay `static`, one
//! copy per unit.

use std::path::{Path, PathBuf};

/// The generated code by role, as [`super::generate`] produces it in one unit.
#[derive(Default)]
pub struct Parts {
    /// Text every unit needs as it is: support code, types, frames.
    pub head: String,
    /// Function declarations, one per line.
    pub protos: String,
    /// Static data, one definition (or forward declaration) per line, and the `static
    /// inline` accessors and `extern` declarations that go with it.
    pub data: String,
    /// Text only unit 0 holds, ahead of the data (the `gm_checks` flag, the debugger's
    /// script section).
    pub once: String,
    /// Function definitions.
    pub funcs: String,
    /// The entry code, which unit 0 holds after its share of the functions.
    pub tail: String,
}

/// The source of each unit of `parts` split `n` ways, or `None` if the code has a shape
/// the split does not handle (it is then compiled as one unit).
pub fn split(parts: &Parts, n: usize) -> Option<Vec<String>> {
    let mut decls = String::new();
    let mut defs = String::new();
    for line in parts.protos.lines() {
        decls.push_str(&external(line).unwrap_or_else(|| line.to_string()));
        decls.push('\n');
    }
    for line in parts.data.lines() {
        if line.is_empty() || line.starts_with("extern ") || line.starts_with("static inline ") {
            decls.push_str(line);
            decls.push('\n');
            continue;
        }
        let def = line.strip_prefix("static ")?;
        let head = def.split_once(" = ").map_or(def.strip_suffix(';')?, |(h, _)| h);
        decls.push_str(&format!("extern {head};\n"));
        defs.push_str(def);
        defs.push('\n');
    }
    let mut items = vec![];
    for item in items_of(&parts.funcs)? {
        let (decl, def) = function(item)?;
        decls.push_str(&decl);
        decls.push('\n');
        items.push(def);
    }
    let shared = format!("{}{}", parts.head, decls);
    let total: usize = items.iter().map(String::len).sum();
    let share = total.div_ceil(n.max(1)).max(1);
    let mut units = vec![format!("{}{}{}", shared, parts.once, defs)];
    let mut size = 0;
    for item in items {
        if size >= share && units.len() < n {
            units.push(shared.clone());
            size = 0;
        }
        size += item.len();
        units.last_mut().unwrap().push_str(&item);
    }
    units[0].push_str(&parts.tail);
    Some(units)
}

/// `line` as an external declaration, if it is a `static` one (not `static inline`).
fn external(line: &str) -> Option<String> {
    if line.starts_with("static inline ") {
        return None;
    }
    if let Some(rest) = line.strip_prefix("static ") {
        return Some(rest.to_string());
    }
    line.strip_prefix("GM_NOINLINE static ").map(|rest| format!("GM_NOINLINE {rest}"))
}

/// The declaration and the external definition of function definition `item`.
fn function(item: &str) -> Option<(String, String)> {
    // `#line` directives may come first.
    let start = item.lines().take_while(|l| l.starts_with('#')).map(|l| l.len() + 1).sum::<usize>();
    let (pre, text) = item.split_at(start);
    let first = text.lines().next()?;
    let def = match external(first) {
        Some(ext) => format!("{pre}{ext}{}", &text[first.len()..]),
        None if first.starts_with("static ") || first.starts_with("GM_NOINLINE static ") => return None,
        None => item.to_string(),
    };
    // The header ends at the body's opening brace, outside the parentheses.
    let body = &def[pre.len()..];
    let mut depth = 0i32;
    let open = body.char_indices().find_map(|(i, c)| {
        match c {
            '(' => depth += 1,
            ')' => depth -= 1,
            '{' if depth == 0 => return Some(i),
            _ => {}
        }
        None
    })?;
    let header = body[..open].trim_end();
    if header.contains('\n') || header.contains(['"', '\'']) || header.contains("static") {
        return None;
    }
    Some((format!("{header};"), def))
}

/// The top-level items of C text that holds only function definitions, each with the
/// preprocessor lines and blank lines before it; `None` if it holds anything else.
fn items_of(code: &str) -> Option<Vec<&str>> {
    let b = code.as_bytes();
    let mut items = vec![];
    let (mut start, mut depth, mut i) = (0, 0i32, 0);
    let mut line_start = true;
    while i < b.len() {
        let c = b[i];
        match c {
            b'#' if line_start => {
                while i < b.len() && b[i] != b'\n' {
                    i += 1;
                }
                continue;
            }
            b'"' | b'\'' => {
                i += 1;
                while i < b.len() && b[i] != c {
                    i += if b[i] == b'\\' { 2 } else { 1 };
                }
            }
            b'/' if b.get(i + 1) == Some(&b'*') => {
                i += 2;
                while i + 1 < b.len() && !(b[i] == b'*' && b[i + 1] == b'/') {
                    i += 1;
                }
                i += 1;
            }
            b'/' if b.get(i + 1) == Some(&b'/') => {
                while i + 1 < b.len() && b[i + 1] != b'\n' {
                    i += 1;
                }
            }
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth < 0 {
                    return None;
                }
                if depth == 0 && b.get(i + 1).is_none_or(|&n| n == b'\n') {
                    i = (i + 2).min(b.len());
                    items.push(&code[start..i]);
                    start = i;
                    line_start = true;
                    continue;
                }
            }
            b';' if depth == 0 => return None,
            _ => {}
        }
        line_start = c == b'\n';
        i += 1;
    }
    // Only blank lines and directives may follow the last item.
    code[start..].lines().all(|l| l.trim().is_empty() || l.starts_with('#')).then_some(items)
}

/// The C file of unit `i` of a build of `exe` (unit 0's is `exe` with the extension `.c`).
pub fn unit_path(exe: &Path, i: usize) -> PathBuf {
    if i == 0 { exe.with_extension("c") } else { exe.with_extension(format!("{i}.c")) }
}

/// The number of units `GAMMA_C_UNITS` asks for. A program that cannot be split then
/// fails to build, so tests notice.
pub fn forced() -> Option<usize> {
    std::env::var("GAMMA_C_UNITS").ok().and_then(|v| v.parse().ok())
}

/// The most units a build is split into.
pub const MAX_UNITS: usize = 8;

/// How much function text (in bytes) makes a unit worth its own compiler process.
const UNIT_BYTES: usize = 96 * 1024;

/// The number of units to compile `parts` as: `GAMMA_C_UNITS` if set (tests use it to
/// split small programs), else one per [`UNIT_BYTES`] of function text, at most one per
/// processor and [`MAX_UNITS`].
pub fn count(parts: &Parts) -> usize {
    if let Some(n) = forced() {
        return n.clamp(1, MAX_UNITS);
    }
    let cpus = std::thread::available_parallelism().map_or(1, |n| n.get());
    (parts.funcs.len() / UNIT_BYTES).clamp(1, cpus.min(MAX_UNITS))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parts() -> Parts {
        Parts {
            head: "typedef int gm_t0;\n".into(),
            protos: "static int gm_f0(int a);\nGM_NOINLINE static void gm_pf0(void);\nstatic void gm_f1(void);\n".into(),
            data: "static const uint8_t gm_s0[3] = \"a;\";\nstatic uint8_t gm_c0[8] __attribute__((aligned(8)));\nstatic inline int gm_ga0(void) { return 0; }\n".into(),
            once: "int gm_checks = 1;\n".into(),
            funcs: "#line 3 \"a.gm\"\nstatic int gm_f0(int a) {\n    if (a) { return \"}\"[0]; }\n    return 0;\n}\nGM_NOINLINE static void gm_pf0(void) { }\nstatic __attribute__((aligned(4))) void gm_f1(void) {\n    /* } */\n}\n".into(),
            tail: "int main(void) { return gm_f0(1); }\n".into(),
        }
    }

    #[test]
    fn functions_and_data_get_external_linkage() {
        let units = split(&parts(), 2).unwrap();
        assert_eq!(units.len(), 2);
        for u in &units {
            assert!(u.contains("int gm_f0(int a);\nGM_NOINLINE void gm_pf0(void);\n"), "{u}");
            assert!(
                u.contains("extern const uint8_t gm_s0[3];\nextern uint8_t gm_c0[8] __attribute__((aligned(8)));\n")
            );
            assert!(u.contains("static inline int gm_ga0(void)"));
            assert!(u.contains("__attribute__((aligned(4))) void gm_f1(void);"));
        }
        let u0 = &units[0];
        assert!(u0.contains("int gm_checks = 1;\nconst uint8_t gm_s0[3] = \"a;\";\nuint8_t gm_c0[8]"), "{u0}");
        assert!(u0.contains("#line 3 \"a.gm\"\nint gm_f0(int a) {\n"));
        assert!(u0.ends_with("int main(void) { return gm_f0(1); }\n"));
        assert!(!units[1].contains("gm_checks") && !units[1].contains("main"));
        assert!(units[1].contains("\n__attribute__((aligned(4))) void gm_f1(void) {\n"));
    }

    #[test]
    fn other_shapes_are_not_split() {
        let mut p = parts();
        p.funcs.push_str("static int gm_x;\n");
        assert!(split(&p, 2).is_none());
        let mut p = parts();
        p.funcs.push_str("static inline int gm_y(void) { return 1; }\n");
        assert!(split(&p, 2).is_none());
        let mut p = parts();
        p.data.push_str("int gm_z;\n");
        assert!(split(&p, 2).is_none());
    }
}
