//! Building for WASI (`wasm32-wasi`): the link options and the Asyncify step.
//!
//! WebAssembly gives a program no way to switch stacks, so the runtime implements fiber
//! switches with Binaryen's Asyncify transformation (see "context switching" in
//! `runtime/gm_rt.c`). The runtime reaches it through imports from the module `asyncify`;
//! when the linked module still has such an import, the program can switch fibers, and
//! [`asyncify`] runs `wasm-opt --asyncify` over it, which replaces those imports by the
//! functions that do the work. A program without fibers needs no transformation, and is
//! left as the C compiler made it.
//!
//! The C compiler is not allowed to optimize the linked module itself (Clang runs
//! `wasm-opt` on it when it finds one and the link is optimized): Binaryen's inlining before
//! the transformation could move code into the runtime functions that start and stop
//! unwinding, which the transformation leaves alone. The objects are optimized as usual,
//! and the module is optimized after the transformation.

use std::path::Path;

/// The size of the module's own stack: as large as the stack the runtime gives the entry
/// function's thread on other systems, so deep recursion behaves alike everywhere.
pub const STACK_SIZE: u64 = 64 << 20;

/// The linker options of every WASI module: the stack comes first in memory, so that
/// running off its end is an out-of-bounds access rather than a write over the data.
pub fn link_flags() -> Vec<String> {
    vec!["-Wl,--stack-first".into(), format!("-Wl,-z,stack-size={STACK_SIZE}")]
}

/// The `wasm-opt` command: `WASM_OPT` (a path) if set, else `wasm-opt` from `PATH`.
fn wasm_opt() -> String {
    std::env::var("WASM_OPT").ok().filter(|v| !v.trim().is_empty()).unwrap_or_else(|| "wasm-opt".into())
}

/// The output of `wasm-opt --version` (empty if it cannot be run), which belongs in the
/// cache key of a module that goes through it.
pub fn wasm_opt_version() -> Vec<u8> {
    std::process::Command::new(wasm_opt()).arg("--version").output().map(|o| o.stdout).unwrap_or_default()
}

/// Applies the Asyncify transformation to the module at `path` in place when it needs it
/// (it imports from `asyncify`), then optimizes it at the build's level `opt` (`-O1`, ...).
/// `debug_info` keeps the debugging information.
pub fn asyncify(path: &Path, opt: &str, debug_info: bool) -> Result<(), String> {
    let bytes = std::fs::read(path).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
    let modules = import_modules(&bytes)
        .map_err(|e| format!("{} is not a WebAssembly module I can read: {e}", path.display()))?;
    if !modules.iter().any(|m| m == "asyncify") {
        return Ok(());
    }
    let tool = wasm_opt();
    let mut cmd = std::process::Command::new(&tool);
    cmd.arg(path).arg("--asyncify");
    // No import starts an unwind except those of the module `asyncify`, which the pass
    // knows about: naming a module that does not exist says so.
    cmd.arg("--pass-arg=asyncify-imports@gamma.none");
    // Optimizing after the transformation recovers most of what it costs.
    if opt != "-O0" {
        cmd.arg(opt);
    }
    if debug_info {
        cmd.arg("-g");
    }
    cmd.arg("-o").arg(path);
    let out = cmd.output().map_err(|e| {
        format!(
            "this program switches fibers, which on WASI needs Binaryen's `wasm-opt` \
             (https://github.com/WebAssembly/binaryen), but `{tool}` cannot be run: {e}; \
             install Binaryen or set WASM_OPT to its wasm-opt"
        )
    })?;
    if !out.status.success() {
        return Err(format!("`{tool} --asyncify` failed:\n{}", String::from_utf8_lossy(&out.stderr)));
    }
    Ok(())
}

/// The module names of the imports of WebAssembly module `wasm`.
pub fn import_modules(wasm: &[u8]) -> Result<Vec<String>, String> {
    let mut r = Reader { b: wasm, at: 0 };
    if r.bytes(4)? != b"\0asm" {
        return Err("no WebAssembly header".into());
    }
    r.bytes(4)?;
    let mut modules = vec![];
    while r.at < wasm.len() {
        let id = r.byte()?;
        let size = r.uleb()? as usize;
        let body = r.bytes(size)?;
        if id != 2 {
            continue;
        }
        let mut s = Reader { b: body, at: 0 };
        for _ in 0..s.uleb()? {
            modules.push(s.name()?);
            s.name()?;
            match s.byte()? {
                0 => drop(s.uleb()?), // a function: its type index
                1 => {
                    s.byte()?; // a table: its element type and limits
                    s.limits()?;
                }
                2 => s.limits()?,       // a memory
                3 => drop(s.bytes(2)?), // a global: its type and mutability
                4 => {
                    s.byte()?; // a tag: its attribute and type index
                    s.uleb()?;
                }
                k => return Err(format!("unknown import kind {k}")),
            }
        }
    }
    Ok(modules)
}

struct Reader<'a> {
    b: &'a [u8],
    at: usize,
}

impl<'a> Reader<'a> {
    fn bytes(&mut self, n: usize) -> Result<&'a [u8], String> {
        let end = self.at.checked_add(n).filter(|&e| e <= self.b.len()).ok_or("truncated")?;
        let s = &self.b[self.at..end];
        self.at = end;
        Ok(s)
    }

    fn byte(&mut self) -> Result<u8, String> {
        Ok(self.bytes(1)?[0])
    }

    fn uleb(&mut self) -> Result<u64, String> {
        let mut v = 0u64;
        for shift in (0..64).step_by(7) {
            let b = self.byte()?;
            v |= u64::from(b & 0x7f) << shift;
            if b & 0x80 == 0 {
                return Ok(v);
            }
        }
        Err("integer too long".into())
    }

    fn name(&mut self) -> Result<String, String> {
        let n = self.uleb()? as usize;
        Ok(String::from_utf8_lossy(self.bytes(n)?).into_owned())
    }

    fn limits(&mut self) -> Result<(), String> {
        let flags = self.byte()?;
        self.uleb()?;
        if flags & 1 != 0 {
            self.uleb()?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn leb(mut v: usize, out: &mut Vec<u8>) {
        loop {
            let b = (v & 0x7f) as u8;
            v >>= 7;
            out.push(if v == 0 { b } else { b | 0x80 });
            if v == 0 {
                break;
            }
        }
    }

    fn name(s: &str, out: &mut Vec<u8>) {
        leb(s.len(), out);
        out.extend(s.as_bytes());
    }

    #[test]
    fn reads_the_import_modules() {
        let mut imports = vec![4];
        name("wasi_snapshot_preview1", &mut imports);
        name("fd_write", &mut imports);
        imports.extend([0, 3]);
        name("env", &mut imports);
        name("memory", &mut imports);
        imports.extend([2, 1, 2, 0x80, 0x01]);
        name("env", &mut imports);
        name("g", &mut imports);
        imports.extend([3, 0x7f, 1]);
        name("asyncify", &mut imports);
        name("start_unwind", &mut imports);
        imports.extend([0, 1]);
        let mut m = b"\0asm\x01\0\0\0".to_vec();
        // A custom section first, which is skipped.
        m.extend([0, 3, 1, b'x', 9]);
        m.push(2);
        leb(imports.len(), &mut m);
        m.extend(&imports);
        assert_eq!(import_modules(&m).unwrap(), ["wasi_snapshot_preview1", "env", "env", "asyncify"]);
        assert!(import_modules(b"\x7fELF\x02\x01\x01\0").is_err());
        assert!(import_modules(&m[..m.len() - 3]).is_err());
    }
}
