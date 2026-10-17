//! Source-level debugging of compiled programs (`gamma build -g`).
//!
//! With debug information on, the generated C maps back to the Gamma source:
//!
//! - Each statement is preceded by a `#line` directive naming its `.gm` file and
//!   line, so the C compiler's debug information (and sanitizer reports) point at
//!   Gamma lines. Each generated function ends with a marker that [`resolve_lines`]
//!   turns into a `#line` back to the C file itself, so code the back end adds
//!   between functions keeps its own location.
//! - Each frame field also has the Gamma name of its local (an anonymous union of
//!   `lN` and the name), and struct mirrors name their fields after the Gamma
//!   fields, so a debugger shows `F.count` and `F.p.x`.
//!
//! The Gamma names only appear in declarations the generated code never refers to.
//! A name can still be a macro of a system header (`linux`, `stdout`, `st_mtime`…),
//! so [`guard`] suspends any such macro around those declarations.

use std::collections::BTreeSet;

/// The line that ends a function whose body carries `#line` directives.
pub const LINE_RESET: &str = "#line GM_C_SOURCE";

/// Prepares generated C with `#line` directives for the C file `c_file`. A directive
/// numbers the lines after it consecutively, but all the C lines of a statement belong
/// to the statement's one source line, so each of them gets the directive repeated.
/// Each [`LINE_RESET`] line becomes a directive giving the next line its true position
/// in the C file.
pub fn resolve_lines(code: &str, c_file: &str) -> String {
    let mut out = String::with_capacity(code.len() * 2);
    let mut n = 0; // lines written so far
    let mut active: Option<&str> = None;
    let push = |out: &mut String, n: &mut usize, line: &str| {
        out.push_str(line);
        out.push('\n');
        *n += 1;
    };
    for line in code.lines() {
        if line == LINE_RESET {
            active = None;
            // `#line N` numbers the line after the directive.
            let directive = format!("#line {} {}", n + 2, super::types::c_string(c_file.as_bytes()));
            push(&mut out, &mut n, &directive);
        } else if line.starts_with("#line ") {
            active = Some(line);
            push(&mut out, &mut n, line);
        } else {
            if let Some(d) = active
                && !out.ends_with(&format!("{d}\n"))
            {
                push(&mut out, &mut n, d);
            }
            push(&mut out, &mut n, line);
        }
    }
    out
}

/// The gdb pretty printers for debugger views (`super::views`).
const GDB_PRINTERS: &str = include_str!("gdb_printers.py");

/// The lldb data formatters for debugger views (`super::views`), a Python module.
const LLDB_FORMATTERS: &str = include_str!("lldb_formatters.py");

/// The name under which the lldb formatters are written next to an executable built with
/// debug information.
pub const LLDB_FORMATTERS_FILE: &str = "gamma_lldb.py";

/// Writes the lldb data formatters to `gamma_lldb.py` in the directory of `exe`, replacing
/// any earlier copy (every program gets the same file, so executables in one directory
/// share it). Load it in lldb with `command script import`.
pub fn write_lldb_formatters(exe: &std::path::Path) -> Result<(), String> {
    let dir = exe.parent().filter(|d| !d.as_os_str().is_empty()).unwrap_or(std::path::Path::new("."));
    let path = dir.join(LLDB_FORMATTERS_FILE);
    std::fs::write(&path, LLDB_FORMATTERS).map_err(|e| format!("cannot write {}: {}", path.display(), e))
}

/// A top-level `asm` statement that embeds the gdb pretty printers in the executable's
/// `.debug_gdb_scripts` section (an inline Python script), which gdb loads with the
/// executable where its auto-load safe path allows. ELF targets only.
pub fn gdb_script_section() -> String {
    let mut asm = String::from(".pushsection \".debug_gdb_scripts\", \"MS\",%progbits,1\n.byte 4\n");
    asm.push_str(".ascii \"gdb.inlined-script.gamma\\n\"\n");
    for line in GDB_PRINTERS.lines() {
        let escaped = line.replace('\\', "\\\\").replace('"', "\\\"");
        asm.push_str(&format!(".ascii \"{}\\n\"\n", escaped));
    }
    asm.push_str(".byte 0\n.popsection\n");
    format!("__asm__({});\n", super::types::c_string(asm.as_bytes()))
}

/// `name` if it can be the name of a struct member in the generated C: an ASCII C
/// identifier that is neither a keyword nor a name the generated code itself uses.
pub fn member_name(name: &str) -> Option<&str> {
    let mut chars = name.chars();
    let first = chars.next()?;
    if !(first.is_ascii_alphabetic() || first == '_') || !chars.all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return None;
    }
    // Reserved identifiers, and the back end's and runtime's own prefixes.
    let lower = name.to_ascii_lowercase();
    if name.starts_with("__")
        || (name.starts_with('_') && name[1..].starts_with(|c: char| c.is_ascii_uppercase()))
        || lower.starts_with("gm_")
    {
        return None;
    }
    // The frame's own members (`h`, `ret`, `lN`, `wN`) and the macros generated code
    // relies on inside the guarded declarations.
    let numbered =
        |p: &str| name.strip_prefix(p).is_some_and(|d| !d.is_empty() && d.bytes().all(|b| b.is_ascii_digit()));
    if numbered("l") || numbered("w") || numbered("f") || numbered("a") || numbered("b") {
        return None;
    }
    const OWN: &[&str] =
        &["h", "ret", "b", "F", "B", "NULL", "offsetof", "bool", "true", "false", "uint8_t", "_Bool", "size_t"];
    if OWN.contains(&name) || KEYWORDS.contains(&name) {
        return None;
    }
    Some(name)
}

const KEYWORDS: &[&str] = &[
    "auto",
    "break",
    "case",
    "char",
    "const",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "register",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "struct",
    "switch",
    "typedef",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
    "asm",
    "typeof",
    "alignas",
    "alignof",
    "noreturn",
    "static_assert",
    "thread_local",
    "complex",
    "imaginary",
];

/// Picks member names for `names` (in order): the Gamma name where [`member_name`]
/// accepts it and no other member has it, else none. `taken` holds names already in
/// use in the same struct and receives the chosen ones.
pub fn unique_members<'n>(
    names: impl IntoIterator<Item = &'n str>,
    taken: &mut BTreeSet<String>,
) -> Vec<Option<String>> {
    unique_with(names, taken, member_name)
}

/// [`unique_members`] for the members of a debugger view (`super::views`), where the
/// names of a frame's own members are free.
pub fn unique_view_members<'n>(
    names: impl IntoIterator<Item = &'n str>,
    taken: &mut BTreeSet<String>,
) -> Vec<Option<String>> {
    unique_with(names, taken, |n| if n == "h" || n == "ret" { Some(n) } else { member_name(n) })
}

fn unique_with<'n>(
    names: impl IntoIterator<Item = &'n str>,
    taken: &mut BTreeSet<String>,
    accept: fn(&str) -> Option<&str>,
) -> Vec<Option<String>> {
    let names: Vec<&str> = names.into_iter().collect();
    let mut count = std::collections::HashMap::new();
    for n in &names {
        *count.entry(*n).or_insert(0) += 1;
    }
    names
        .iter()
        .map(|n| {
            let m = accept(n)?;
            if count[n] > 1 || taken.contains(m) {
                return None;
            }
            taken.insert(m.to_string());
            Some(m.to_string())
        })
        .collect()
}

/// `decls`, with every macro named in `names` suspended around it.
pub fn guard(names: &BTreeSet<String>, decls: &str) -> String {
    if names.is_empty() || decls.is_empty() {
        return decls.to_string();
    }
    let mut out = String::new();
    for n in names {
        out.push_str(&format!("#pragma push_macro(\"{n}\")\n#undef {n}\n"));
    }
    out.push_str(decls);
    for n in names {
        out.push_str(&format!("#pragma pop_macro(\"{n}\")\n"));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn member_names() {
        assert_eq!(member_name("count"), Some("count"));
        assert_eq!(member_name("st_mtime"), Some("st_mtime"));
        for bad in ["", "1x", "héllo", "int", "l3", "w0", "ret", "h", "__x", "_Foo", "gm_x", "GM_LD", "<tmp>", "NULL"]
        {
            assert_eq!(member_name(bad), None, "{bad}");
        }
        assert_eq!(member_name("l"), Some("l"));
        assert_eq!(member_name("l_2"), Some("l_2"));
    }

    #[test]
    fn duplicates_get_no_name() {
        let mut taken = BTreeSet::new();
        let m = unique_members(["x", "y", "x", "int"], &mut taken);
        assert_eq!(m, vec![None, Some("y".to_string()), None, None]);
        assert!(taken.contains("y"));
    }

    #[test]
    fn every_mapped_line_keeps_its_source_line() {
        let code = format!("a\n#line 3 \"x.gm\"\nb\nc\n{LINE_RESET}\nd\n");
        let r = resolve_lines(&code, "out.c");
        assert_eq!(r, "a\n#line 3 \"x.gm\"\nb\n#line 3 \"x.gm\"\nc\n#line 7 \"out.c\"\nd\n");
    }
}
