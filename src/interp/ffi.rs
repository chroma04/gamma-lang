//! Calls from the interpreter into C (spec §19.2).
//!
//! Foreign functions are found with `dlsym` in the process itself (which has the C
//! library) and in the libraries the program names, and called through libffi.
//!
//! Interpreter pointers are addresses in the interpreter's own address space, not host
//! addresses, so memory crosses the boundary by copying. A pointer argument makes a
//! *mirror* of the interpreter allocation it points into: a host buffer holding the
//! allocation's bytes, with the pointers inside it (found from the static type of the
//! pointee) translated the same way. The mirror's address is what C sees. After the call
//! the bytes C changed are copied back, and pointers C stored or returned that point into
//! mirrors are translated back. A mirror lives as long as its allocation does, so the
//! address C saw stays valid between calls (as with `strtok`); it is refreshed from the
//! interpreter's memory at each call that passes a pointer to it.
//!
//! Addresses outside the interpreter's address space (returned by C: `getenv`, `fopen`,
//! `malloc`, and the mirrors themselves inside callbacks) are host memory, which the
//! interpreter accesses directly once foreign code is in use (see [`super::memory`]).
//!
//! A function pointer passed to C becomes a libffi closure that calls back into the
//! interpreter. A trap (or `Env.exit`) in a callback cannot unwind through C: it is kept,
//! further callbacks return at once with zero, and the foreign call reports it when C
//! returns.

use super::bytes::Bytes;
use super::machine::Machine;
use super::value::*;
use super::{Mode, RunConfig};
use crate::layout::Target;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use libffi::low::ffi_cif;
use libffi::middle::{Arg, Cif, Closure, CodePtr, Ret, Type as CType};
use std::alloc::Layout;
use std::cell::Cell;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::ffi::{CString, c_void};
use std::mem::ManuallyDrop;
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, Ordering};

/// Whether the interpreter owns the process: after `main` returns it then ends the process
/// with `exit`, so that functions C registered to run at exit (`atexit`) can still call
/// into the interpreter.
static OWNS_PROCESS: AtomicBool = AtomicBool::new(false);

/// Lets the interpreter end the process itself when the program has handed callbacks to C
/// (used by `gamma run`; library users and tests leave it off).
pub fn set_owns_process(on: bool) {
    OWNS_PROCESS.store(on, Ordering::Relaxed);
}

thread_local! {
    /// The machine running foreign code on this thread, for callbacks.
    static CURRENT: Cell<*mut c_void> = const { Cell::new(std::ptr::null_mut()) };
}

/// A host buffer aligned for any C type.
struct HostBuf {
    ptr: *mut u8,
    size: usize,
}

const BUF_ALIGN: usize = 64;

impl HostBuf {
    fn new(size: usize) -> HostBuf {
        let layout = Layout::from_size_align(size.max(1), BUF_ALIGN).expect("mirror layout");
        // SAFETY: the layout has a non-zero size.
        let ptr = unsafe { std::alloc::alloc_zeroed(layout) };
        if ptr.is_null() {
            std::alloc::handle_alloc_error(layout);
        }
        HostBuf { ptr, size }
    }

    fn bytes(&self) -> &[u8] {
        // SAFETY: `ptr` is valid for `size` bytes while the buffer lives.
        unsafe { std::slice::from_raw_parts(self.ptr, self.size) }
    }
}

impl Drop for HostBuf {
    fn drop(&mut self) {
        let layout = Layout::from_size_align(self.size.max(1), BUF_ALIGN).expect("mirror layout");
        // SAFETY: allocated in `new` with this layout.
        unsafe { std::alloc::dealloc(self.ptr, layout) }
    }
}

#[derive(Clone, Copy, Debug)]
enum SlotKind {
    /// A data pointer; the pointee type is known unless it is `rawptr`.
    Data(Option<Ty>),
    /// A function pointer.
    Func,
}

/// A pointer inside a value, at a byte offset.
#[derive(Clone, Copy, Debug)]
struct Slot {
    off: usize,
    kind: SlotKind,
}

struct Mirror {
    size: usize,
    buf: HostBuf,
    /// The bytes as they were when C was handed the mirror, to find what C changed.
    snap: Vec<u8>,
    /// Handed to an unfinished foreign call: not to be refreshed again.
    active: bool,
    /// Copied from the interpreter's memory since the last snapshot.
    fresh: bool,
    /// The type of the elements the allocation is taken to hold, and whether its pointers
    /// were translated since the last refresh.
    elem: Option<Ty>,
    scanned: bool,
}

/// Libraries and directories that packages add (`c-sources`, `lib-dirs` in gamma.toml),
/// set by the driver before the program runs.
#[derive(Default)]
struct Extra {
    /// Directories searched for `foreign "lib"` libraries before the `-L` directories of `LDFLAGS`.
    dirs: Vec<String>,
    /// Handles of the shared libraries made from the packages' C sources.
    handles: Vec<usize>,
}

static EXTRA: std::sync::Mutex<Extra> = std::sync::Mutex::new(Extra { dirs: Vec::new(), handles: Vec::new() });

fn extra() -> std::sync::MutexGuard<'static, Extra> {
    EXTRA.lock().unwrap_or_else(|e| e.into_inner())
}

/// Adds directories that foreign libraries are searched in.
pub fn add_library_dirs(dirs: Vec<std::path::PathBuf>) {
    extra().dirs.extend(dirs.iter().map(|d| d.display().to_string()));
}

/// Loads the shared library at `path`; its symbols are found by foreign declarations of
/// any library. It stays loaded for the life of the process.
pub fn load_library(path: &std::path::Path) -> Result<(), String> {
    let c = CString::new(path.display().to_string()).map_err(|_| "bad library path".to_string())?;
    // SAFETY: a valid C string; the handle is kept for the life of the process.
    let h = unsafe { dl::open_global(c.as_ptr()) };
    if h.is_null() {
        return Err(format!("cannot load {}: {}", path.display(), dl::last_error()));
    }
    extra().handles.push(h as usize);
    Ok(())
}

/// What a callback needs to call the Gamma function.
struct CbData {
    func: FnId,
    params: Vec<Ty>,
    ret: Ty,
}

struct CbClosure {
    code: u64,
    // Kept alive (and leaked at exit) so C may call the code at any time.
    _closure: ManuallyDrop<Closure<'static>>,
    _data: Box<CbData>,
}

struct Prepared {
    sym: usize,
    cif: Cif,
    params: Vec<Ty>,
    ret: Ty,
}

#[derive(Default)]
pub struct Ffi {
    libs: HashMap<String, Vec<usize>>,
    symbols: HashMap<(String, String), Option<usize>>,
    prepared: HashMap<FnId, Rc<Prepared>>,
    mirrors: BTreeMap<u64, Mirror>,
    /// Host address of each mirror's buffer to its interpreter address and size.
    by_host: BTreeMap<usize, (u64, usize)>,
    purge_at: usize,
    closures: HashMap<FnId, CbClosure>,
    /// Closure code addresses to the functions they call.
    code_to_fn: HashMap<usize, FnId>,
    slots: HashMap<Ty, Rc<Vec<Slot>>>,
    /// The mirrors each unfinished foreign call (or callback) activated.
    touched: Vec<Vec<u64>>,
    /// A trap or exit raised in a callback, reported when C returns.
    pending: Option<Box<Flow>>,
}

impl Ffi {
    /// Whether callbacks were handed to C.
    pub fn has_callbacks(&self) -> bool {
        !self.closures.is_empty()
    }

    fn open_lib(&mut self, lib: &str) -> Vec<usize> {
        if let Some(h) = self.libs.get(lib) {
            return h.clone();
        }
        let mut names = Vec::new();
        if cfg!(windows) && (lib == "c" || lib == "m") {
            // The C library is a DLL of its own; the one MinGW programs and `gamma` use first.
            names.extend(["ucrtbase.dll".to_string(), "msvcrt.dll".to_string()]);
        } else if lib != "c" {
            let (pre, ext) = if cfg!(target_os = "macos") {
                ("lib", "dylib")
            } else if cfg!(windows) {
                ("", "dll")
            } else {
                ("lib", "so")
            };
            let mut dirs: Vec<String> = extra().dirs.clone();
            if let Ok(f) = std::env::var("LDFLAGS") {
                let mut it = f.split_whitespace();
                while let Some(w) = it.next() {
                    if let Some(d) = w.strip_prefix("-L") {
                        if d.is_empty() {
                            dirs.extend(it.next().map(String::from));
                        } else {
                            dirs.push(d.to_string());
                        }
                    }
                }
            }
            let file = format!("{}{}.{}", pre, lib, ext);
            names.extend(dirs.iter().map(|d| format!("{}/{}", d, file)));
            names.push(file);
            if cfg!(windows) {
                names.push(format!("lib{}.dll", lib));
            }
            if lib == "m" && cfg!(target_os = "linux") {
                names.push("libm.so.6".into());
            }
        }
        let mut handles = Vec::new();
        for n in names {
            let Ok(c) = CString::new(n) else { continue };
            // SAFETY: a valid C string; the handle is kept for the life of the process.
            let h = unsafe { dl::open(c.as_ptr()) };
            if !h.is_null() {
                handles.push(h as usize);
                // On Windows both C libraries are searched (see `flush_c_streams`).
                if !(cfg!(windows) && (lib == "c" || lib == "m")) {
                    break;
                }
            }
        }
        self.libs.insert(lib.to_string(), handles.clone());
        handles
    }

    /// The address of `name` in `lib` or, failing that, anywhere in the process.
    pub fn symbol(&mut self, lib: &str, name: &str) -> Option<usize> {
        let key = (lib.to_string(), name.to_string());
        if let Some(r) = self.symbols.get(&key) {
            return *r;
        }
        let r = self.lookup(lib, name);
        self.symbols.insert(key, r);
        r
    }

    /// The address of the function `getter` anywhere in the process.
    fn symbol_of(&mut self, getter: &str, c: &CString) -> *mut c_void {
        let _ = getter;
        // SAFETY: `c` is a C string.
        let p = unsafe { dl::sym(dl::DEFAULT, c.as_ptr()) };
        if !p.is_null() {
            return p;
        }
        for h in self.open_lib("c") {
            // SAFETY: `h` is a handle from `dl::open`.
            let p = unsafe { dl::sym(h as *mut c_void, c.as_ptr()) };
            if !p.is_null() {
                return p;
            }
        }
        std::ptr::null_mut()
    }

    fn lookup(&mut self, lib: &str, name: &str) -> Option<usize> {
        // Before any foreign function runs, its C library's streams are set up.
        #[cfg(windows)]
        windows_c_libs();
        let c = CString::new(name).ok()?;
        // On Windows, handlers go to the C library whose `exit` ends the process.
        if cfg!(windows) && name == "atexit" {
            return Some(libc::atexit as *const () as usize);
        }
        let find = |h: *mut c_void| {
            // SAFETY: `h` is a handle from `dlopen` or `RTLD_DEFAULT`, and `c` a C string.
            let p = unsafe { dl::sym(h, c.as_ptr()) };
            (!p.is_null()).then_some(p as usize)
        };
        for h in self.open_lib(lib) {
            if let Some(p) = find(h as *mut c_void) {
                return Some(p);
            }
        }
        let package_libs = extra().handles.clone();
        for h in package_libs {
            if let Some(p) = find(h as *mut c_void) {
                return Some(p);
            }
        }
        if let Some(p) = find(dl::DEFAULT) {
            return Some(p);
        }
        #[cfg(windows)]
        for m in ["kernel32.dll", "ucrtbase.dll", "msvcrt.dll"] {
            let h = unsafe { dl::open(CString::new(m).ok()?.as_ptr()) };
            if !h.is_null()
                && let Some(p) = find(h)
            {
                return Some(p);
            }
        }
        // Some systems define `atexit` only in a static part of the C library.
        if name == "atexit" {
            return Some(libc::atexit as *const () as usize);
        }
        // `errno` is a macro for a thread-local on most systems; its address comes from a function.
        if name == "errno" {
            let getter = if cfg!(target_os = "macos") {
                "__error"
            } else if cfg!(windows) {
                "_errno"
            } else {
                "__errno_location"
            };
            let c = CString::new(getter).ok()?;
            // SAFETY: as above.
            let g = self.symbol_of(getter, &c);
            if g.is_null() {
                return None;
            }
            // SAFETY: the getter takes no arguments and returns the thread's `errno` address.
            let f: extern "C" fn() -> *mut c_void = unsafe { std::mem::transmute(g) };
            return Some(f() as usize);
        }
        None
    }
}

/// Flushes the C library's output streams. On Windows a program may use two C libraries
/// (`ucrtbase.dll` and `msvcrt.dll`, each with streams of its own, and UCRT lacks `printf`
/// and the like), so every one that is loaded is flushed.
fn flush_c_streams() {
    // SAFETY: flushing all C streams takes no arguments but null.
    unsafe { libc::fflush(std::ptr::null_mut()) };
    #[cfg(windows)]
    for &f in windows_c_libs() {
        // SAFETY: `fflush(NULL)` of a C library.
        let f: extern "C" fn(*mut c_void) -> i32 = unsafe { std::mem::transmute(f) };
        f(std::ptr::null_mut());
    }
}

/// The `fflush` of each C library of the process. The first call also puts their standard
/// streams in binary mode, as compiled programs have them: no `\r` before each newline.
#[cfg(windows)]
fn windows_c_libs() -> &'static [usize] {
    use std::sync::OnceLock;
    static LIBS: OnceLock<Vec<usize>> = OnceLock::new();
    LIBS.get_or_init(|| {
        let sym = |m: *mut c_void, name: &str| {
            let n = CString::new(name).ok()?;
            // SAFETY: a module handle and a C string.
            let p = unsafe { dl::sym(m, n.as_ptr()) };
            (!p.is_null()).then_some(p)
        };
        ["ucrtbase.dll", "msvcrt.dll"]
            .iter()
            .filter_map(|m| {
                let m = CString::new(*m).ok()?;
                // SAFETY: a C string; the module stays loaded for the life of the process.
                let h = unsafe { dl::open(m.as_ptr()) };
                if h.is_null() {
                    return None;
                }
                if let Some(setmode) = sym(h, "_setmode") {
                    // SAFETY: `_setmode(int fd, int mode)` of that C library.
                    let setmode: extern "C" fn(i32, i32) -> i32 = unsafe { std::mem::transmute(setmode) };
                    const O_BINARY: i32 = 0x8000;
                    for fd in 0..3 {
                        setmode(fd, O_BINARY);
                    }
                }
                sym(h, "fflush").map(|p| p as usize)
            })
            .collect()
    })
}

/// Dynamic loading: `dlopen`/`dlsym` where they exist, `LoadLibrary`/`GetProcAddress` on Windows.
#[cfg(unix)]
mod dl {
    use std::ffi::{c_char, c_void};
    pub const DEFAULT: *mut c_void = libc::RTLD_DEFAULT;
    pub unsafe fn open(name: *const c_char) -> *mut c_void {
        unsafe { libc::dlopen(name, libc::RTLD_LAZY | libc::RTLD_LOCAL) }
    }
    pub unsafe fn sym(h: *mut c_void, name: *const c_char) -> *mut c_void {
        unsafe { libc::dlsym(h, name) }
    }
    /// Opens a library whose symbols later loaded libraries can use too.
    pub unsafe fn open_global(name: *const c_char) -> *mut c_void {
        unsafe { libc::dlopen(name, libc::RTLD_LAZY | libc::RTLD_GLOBAL) }
    }
    /// The reason the last `open` failed.
    pub fn last_error() -> String {
        // SAFETY: `dlerror` returns a C string or null.
        let e = unsafe { libc::dlerror() };
        if e.is_null() { String::new() } else { unsafe { std::ffi::CStr::from_ptr(e) }.to_string_lossy().into_owned() }
    }
}

#[cfg(windows)]
mod dl {
    use std::ffi::{c_char, c_void};
    unsafe extern "system" {
        fn LoadLibraryA(name: *const c_char) -> *mut c_void;
        fn GetProcAddress(h: *mut c_void, name: *const c_char) -> *mut c_void;
        fn GetLastError() -> u32;
    }
    /// Windows has no "search everything" handle: the lookup then tries the modules by name.
    pub const DEFAULT: *mut c_void = std::ptr::null_mut();
    pub unsafe fn open(name: *const c_char) -> *mut c_void {
        unsafe { LoadLibraryA(name) }
    }
    pub unsafe fn sym(h: *mut c_void, name: *const c_char) -> *mut c_void {
        if h.is_null() {
            return std::ptr::null_mut();
        }
        unsafe { GetProcAddress(h, name) }
    }
    /// Windows libraries are always visible to the libraries loaded after them.
    pub unsafe fn open_global(name: *const c_char) -> *mut c_void {
        unsafe { LoadLibraryA(name) }
    }
    /// The reason the last `open` failed.
    pub fn last_error() -> String {
        // SAFETY: no preconditions.
        format!("Windows error {}", unsafe { GetLastError() })
    }
}

fn word(b: &[u8], off: usize) -> u64 {
    u64::from_le_bytes(b[off..off + 8].try_into().unwrap())
}

fn set_word(b: &mut [u8], off: usize, w: u64) {
    b[off..off + 8].copy_from_slice(&w.to_le_bytes());
}

fn is_ptr_like(ty: Ty) -> bool {
    match ty.kind() {
        TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => true,
        TyKind::Optional(t) => is_ptr_like(*t),
        _ => false,
    }
}

/// Whether the interpreter can run `prog`'s foreign code itself on this host, and the
/// reason if not. A program that needs the native back end is run by it instead.
pub fn check_program(prog: &Program, cfg: &RunConfig) -> Result<(), String> {
    if prog.fns.iter().any(|f| f.body.as_ref().is_some_and(contains_asm)) {
        return Err("the program uses inline assembly".into());
    }
    if !prog.fns.iter().any(|f| matches!(f.kind, FnKind::Foreign { .. }))
        && prog.globals.iter().all(|g| g.link_name.is_none())
    {
        return Ok(());
    }
    let host = Target::host();
    if cfg.target != host || host.ptr_bytes != 8 || !host.little_endian {
        return Err("the program targets another platform than the interpreter runs on".into());
    }
    let lookup = |id: AdtId| prog.adts.get(&id);
    let mut m = Machine::new(prog, &RunConfig { mode: Mode::Checked, ..cfg.clone() }, &lookup, 0);
    // Safe builds need the foreign code built for them as well (§18.7).
    if cfg.mode == Mode::Safe {
        return Err("Safe builds are native".into());
    }
    for f in prog.fns.iter() {
        let FnKind::Foreign { lib, link_name, .. } = f.kind else { continue };
        if f.callconv != CallConv::C && m.export_of(link_name.as_str()).is_none() {
            return Err(format!("`{}` uses a calling convention other than C", f.name));
        }
        let tys: Vec<Ty> = f.params.iter().map(|p| f.locals[p.index()].ty).collect();
        if !tys.iter().all(|&t| m.ffi_type_ok(t)) || !m.ffi_ret_ok(f.ret) {
            return Err(format!("the signature of `{}` is not supported by the interpreter", f.name));
        }
        if m.export_of(link_name.as_str()).is_none() && m.ffi.symbol(lib.as_str(), link_name.as_str()).is_none() {
            return Err(format!("cannot find the foreign symbol `{}`", link_name.as_str()));
        }
    }
    for g in prog.globals.iter() {
        if g.init.is_none()
            && let Some(name) = g.link_name
            && m.global_symbol(name.as_str()).is_none()
        {
            return Err(format!("cannot find the foreign variable `{}`", name.as_str()));
        }
    }
    Ok(())
}

fn contains_asm(e: &Expr) -> bool {
    let mut found = matches!(e.kind, ExprKind::Builtin { op: Builtin::Asm { .. }, .. });
    e.for_each_child(&mut |c| found = found || contains_asm(c));
    found
}

impl<'a> Machine<'a> {
    // --- types ---------------------------------------------------------------------------------

    /// The libffi type of the C-compatible type `ty`, if libffi can describe it.
    fn ctype(&self, ty: Ty) -> Option<CType> {
        use TyKind::*;
        Some(match ty.kind() {
            Int(IntTy { signed, bits }) => match (signed, bits) {
                (false, 8) => CType::u8(),
                (true, 8) => CType::i8(),
                (false, 16) => CType::u16(),
                (true, 16) => CType::i16(),
                (false, 32) => CType::u32(),
                (true, 32) => CType::i32(),
                (false, 64) => CType::u64(),
                (true, 64) => CType::i64(),
                _ => return None,
            },
            Usize => CType::u64(),
            Isize => CType::i64(),
            Float(FloatTy::F32) => CType::f32(),
            Float(FloatTy::F64) => CType::f64(),
            Bool => CType::u8(),
            Ptr { .. } | ManyPtr { .. } | Rawptr | FnPtr(_) => CType::pointer(),
            Optional(t) if is_ptr_like(*t) => CType::pointer(),
            Adt(..) => match &self.adt(ty)?.kind {
                AdtKind::Struct { bits: Some(u), .. } => return self.ctype(*u),
                AdtKind::Struct { fields, packed: false, align: None, bits: None } => {
                    let mut elems = Vec::new();
                    for f in fields {
                        if f.align.is_some() {
                            return None;
                        }
                        match f.ty.kind() {
                            Array { len, elem, .. } => {
                                let t = self.ctype(*elem)?;
                                for _ in 0..*len {
                                    elems.push(t.clone());
                                }
                            }
                            _ => elems.push(self.ctype(f.ty)?),
                        }
                    }
                    if elems.is_empty() {
                        return None;
                    }
                    CType::structure(elems)
                }
                AdtKind::CUnion { fields } => return self.union_ctype(ty, fields),
                _ => return None,
            },
            _ => return None,
        })
    }

    /// libffi has no unions, so a union passed by value is described by a struct that the C
    /// ABI treats the same way: for the eightbyte registers it fits in, a float type if every
    /// member bytes there are floats and an integer otherwise; for a larger one (passed in
    /// memory) integers of its alignment.
    fn union_ctype(&self, ty: Ty, fields: &[FieldDef]) -> Option<CType> {
        let (size, align) = (self.size_of(ty), self.ti(ty).align as usize);
        let mut leaves = Vec::new();
        for f in fields {
            self.leaves(f.ty, 0, &mut leaves)?;
        }
        if size == 0 {
            return None;
        }
        let int = |bytes: usize| match bytes {
            8 => CType::u64(),
            4 => CType::u32(),
            2 => CType::u16(),
            _ => CType::u8(),
        };
        let mut elems = Vec::new();
        if size > 16 {
            let unit = align.clamp(1, 8);
            for _ in 0..size / unit {
                elems.push(int(unit));
            }
            return Some(CType::structure(elems));
        }
        for start in (0..size).step_by(8) {
            let end = (start + 8).min(size);
            let here: Vec<&(usize, usize, bool)> =
                leaves.iter().filter(|(o, s, _)| *o < end && o + s > start).collect();
            let all_float = !here.is_empty() && here.iter().all(|l| l.2);
            if all_float && end - start == 8 {
                if here.iter().all(|l| l.1 == 4) {
                    elems.push(CType::f32());
                    elems.push(CType::f32());
                } else {
                    elems.push(CType::f64());
                }
            } else if all_float && end - start == 4 {
                elems.push(CType::f32());
            } else if end - start == 8 || end - start == 4 {
                elems.push(int(end - start));
            } else {
                for _ in start..end {
                    elems.push(CType::u8());
                }
            }
        }
        Some(CType::structure(elems))
    }

    /// The scalar leaves `(offset, size, is_float)` of a C-compatible type.
    fn leaves(&self, ty: Ty, base: usize, out: &mut Vec<(usize, usize, bool)>) -> Option<()> {
        use TyKind::*;
        match ty.kind() {
            Float(_) => out.push((base, self.size_of(ty), true)),
            Array { len, elem, .. } => {
                let es = self.size_of(*elem);
                for i in 0..*len as usize {
                    self.leaves(*elem, base + i * es, out)?;
                }
            }
            Adt(..) => match &self.adt(ty)?.kind {
                AdtKind::Struct { fields, bits: None, .. } => {
                    let offs = self.ti(ty).offsets.clone();
                    for (f, o) in fields.iter().zip(offs) {
                        self.leaves(f.ty, base + o as usize, out)?;
                    }
                }
                AdtKind::CUnion { fields } => {
                    for f in fields {
                        self.leaves(f.ty, base, out)?;
                    }
                }
                AdtKind::Struct { .. } => out.push((base, self.size_of(ty), false)),
                _ => return None,
            },
            _ => out.push((base, self.size_of(ty), false)),
        }
        Some(())
    }

    fn ffi_ret_ok(&self, ty: Ty) -> bool {
        matches!(ty.kind(), TyKind::Never) || self.size_of(ty) == 0 || self.ffi_type_ok(ty)
    }

    /// Whether a value of `ty` can cross the boundary: libffi describes it, and everything
    /// reachable through its pointers can be translated.
    fn ffi_type_ok(&self, ty: Ty) -> bool {
        self.ctype(ty).is_some() && self.reach_ok(ty, &mut HashSet::new())
    }

    fn reach_ok(&self, ty: Ty, seen: &mut HashSet<Ty>) -> bool {
        use TyKind::*;
        if !seen.insert(ty) {
            return true;
        }
        match ty.kind() {
            Ptr { pointee: t, .. }
            | ManyPtr { elem: t, .. }
            | Slice { elem: t, .. }
            | Optional(t)
            | Array { elem: t, .. } => self.reach_ok(*t, seen),
            FnPtr(sig) => {
                sig.callconv == CallConv::C
                    && sig.params.iter().all(|&p| self.ffi_type_ok(p))
                    && self.ffi_ret_ok(sig.ret)
            }
            Tuple { elems, .. } => elems.iter().all(|&t| self.reach_ok(t, seen)),
            Adt(..) => match self.adt(ty).map(|d| &d.kind) {
                Some(AdtKind::Struct { fields, .. }) => fields.iter().all(|f| self.reach_ok(f.ty, seen)),
                // A pointer inside a union cannot be told from an integer.
                Some(AdtKind::CUnion { fields }) => {
                    fields.iter().all(|f| self.slots_of(f.ty).is_empty() && self.reach_ok(f.ty, seen))
                }
                _ => true,
            },
            _ => true,
        }
    }

    // --- pointer slots ---------------------------------------------------------------------------

    fn slots_of(&self, ty: Ty) -> Rc<Vec<Slot>> {
        // The cache lives in `ffi`, which needs `&mut`; recomputing is cheap enough for the
        // shared-reference callers, and the hot path uses `slots_cached`.
        let mut out = Vec::new();
        self.walk_slots(ty, 0, &mut out);
        Rc::new(out)
    }

    fn slots_cached(&mut self, ty: Ty) -> Rc<Vec<Slot>> {
        if let Some(s) = self.ffi.slots.get(&ty) {
            return s.clone();
        }
        let s = self.slots_of(ty);
        self.ffi.slots.insert(ty, s.clone());
        s
    }

    fn walk_slots(&self, ty: Ty, base: usize, out: &mut Vec<Slot>) {
        use TyKind::*;
        match ty.kind() {
            Ptr { pointee, .. } => out.push(Slot { off: base, kind: SlotKind::Data(Some(*pointee)) }),
            ManyPtr { elem, .. } | Slice { elem, .. } => {
                out.push(Slot { off: base, kind: SlotKind::Data(Some(*elem)) })
            }
            Rawptr => out.push(Slot { off: base, kind: SlotKind::Data(None) }),
            FnPtr(_) => out.push(Slot { off: base, kind: SlotKind::Func }),
            Optional(t) if is_ptr_like(*t) => self.walk_slots(*t, base, out),
            Array { len, elem, .. } => {
                let es = self.size_of(*elem);
                if es > 0 {
                    for i in 0..*len as usize {
                        self.walk_slots(*elem, base + i * es, out);
                    }
                }
            }
            Tuple { elems, .. } => {
                let offs = self.ti(ty).offsets.clone();
                for (t, o) in elems.iter().zip(offs) {
                    self.walk_slots(*t, base + o as usize, out);
                }
            }
            Adt(..) => {
                if let Some(AdtKind::Struct { fields, bits: None, .. }) = self.adt(ty).map(|d| &d.kind) {
                    let offs = self.ti(ty).offsets.clone();
                    for (f, o) in fields.iter().zip(offs) {
                        self.walk_slots(f.ty, base + o as usize, out);
                    }
                }
            }
            _ => {}
        }
    }

    // --- mirrors -----------------------------------------------------------------------------------

    /// The host address C sees for the interpreter pointer `v` of kind `kind`.
    fn host_ptr(&mut self, v: u64, kind: SlotKind) -> u64 {
        if v == 0 {
            return 0;
        }
        match kind {
            SlotKind::Func => match self.fn_from_addr(v) {
                Some(f) => self.callback_code(f).unwrap_or(v),
                None => v,
            },
            SlotKind::Data(pointee) => self.mirror_ptr(v, pointee),
        }
    }

    fn mirror_ptr(&mut self, v: u64, pointee: Option<Ty>) -> u64 {
        let Some((base, size, _, live)) = self.mem.describe(v) else { return v };
        if !live {
            return v;
        }
        let size = size as usize;
        if !self.ffi.mirrors.contains_key(&base) {
            let buf = HostBuf::new(size);
            self.ffi.by_host.insert(buf.ptr as usize, (base, size));
            let m = Mirror { size, buf, snap: Vec::new(), active: false, fresh: false, elem: None, scanned: false };
            self.ffi.mirrors.insert(base, m);
        }
        let m = self.ffi.mirrors.get_mut(&base).unwrap();
        let host = m.buf.ptr;
        if !m.active {
            m.active = true;
            m.fresh = true;
            m.scanned = false;
            if let Some(t) = self.ffi.touched.last_mut() {
                t.push(base);
            }
            if let Ok(data) = self.mem.read(base, size as u64, 1) {
                // SAFETY: the buffer holds `size` bytes and does not overlap interpreter memory.
                unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), host, size) };
            }
        }
        let m = self.ffi.mirrors.get_mut(&base).unwrap();
        if m.elem.is_none() {
            m.elem = pointee;
        }
        if !m.scanned && m.elem.is_some() {
            m.scanned = true;
            self.scan_mirror(base);
        }
        host as u64 + (v - base)
    }

    /// Translates the pointers held in the mirror of the allocation at `base`.
    fn scan_mirror(&mut self, base: u64) {
        let m = &self.ffi.mirrors[&base];
        let (elem, size, host) = (m.elem.unwrap(), m.size, m.buf.ptr);
        let es = self.size_of(elem);
        let slots = self.slots_cached(elem);
        if es == 0 || slots.is_empty() {
            return;
        }
        let mut at = 0;
        while at + es <= size {
            for s in slots.iter() {
                let off = at + s.off;
                if off + 8 > size {
                    continue;
                }
                // SAFETY: `off + 8 <= size`, inside the mirror's buffer; accessed unaligned.
                let w = unsafe { (host.add(off) as *const u64).read_unaligned() };
                let h = self.host_ptr(w, s.kind);
                if h != w {
                    // SAFETY: as above.
                    unsafe { (host.add(off) as *mut u64).write_unaligned(h) };
                }
            }
            at += es;
        }
    }

    /// The interpreter pointer a host pointer `h` returned by C stands for: inside a mirror,
    /// the matching interpreter address; anywhere else, `h` itself.
    fn virt_ptr(&self, h: u64, kind: SlotKind) -> u64 {
        if h == 0 {
            return 0;
        }
        match kind {
            SlotKind::Func => match self.ffi.code_to_fn.get(&(h as usize)) {
                Some(&f) => self.fn_addr(f),
                None => h,
            },
            SlotKind::Data(_) => {
                if let Some((&hb, &(vb, size))) = self.ffi.by_host.range(..=h as usize).next_back()
                    && h as usize - hb <= size
                {
                    return vb + (h - hb as u64);
                }
                h
            }
        }
    }

    /// Translates the pointers inside a value of type `ty` for C.
    fn export_value(&mut self, ty: Ty, b: &mut [u8]) {
        let slots = self.slots_cached(ty);
        for s in slots.iter() {
            if s.off + 8 <= b.len() {
                let h = self.host_ptr(word(b, s.off), s.kind);
                set_word(b, s.off, h);
            }
        }
    }

    /// The reverse of [`Machine::export_value`], for a value C produced.
    fn import_value(&mut self, ty: Ty, b: &mut [u8]) {
        let slots = self.slots_cached(ty);
        for s in slots.iter() {
            if s.off + 8 <= b.len() {
                let v = self.virt_ptr(word(b, s.off), s.kind);
                set_word(b, s.off, v);
            }
        }
    }

    /// Takes the snapshots that tell later what C changed.
    fn snapshot_mirrors(&mut self) {
        let Some(t) = self.ffi.touched.last() else { return };
        for base in t.clone() {
            let m = self.ffi.mirrors.get_mut(&base).unwrap();
            if m.fresh {
                m.fresh = false;
                m.snap = m.buf.bytes().to_vec();
            }
        }
    }

    /// Copies what C changed in the mirrors the finished call activated back to the
    /// interpreter's memory.
    fn finish_mirrors(&mut self, bases: Vec<u64>) {
        for base in bases {
            let (host, snap, size, elem) = {
                let m = self.ffi.mirrors.get_mut(&base).unwrap();
                m.active = false;
                (m.buf.bytes().to_vec(), std::mem::take(&mut m.snap), m.size, m.elem)
            };
            if snap.len() != size || host == snap || !self.mem.is_live(base) {
                self.ffi.mirrors.get_mut(&base).unwrap().snap = snap;
                continue;
            }
            let Ok(cur) = self.mem.read(base, size as u64, 1).map(|s| s.to_vec()) else { continue };
            let mut cur = cur;
            for i in 0..size {
                if host[i] != snap[i] {
                    cur[i] = host[i];
                }
            }
            if let Some(t) = elem {
                let es = self.size_of(t);
                let slots = self.slots_cached(t);
                if es > 0 {
                    let mut at = 0;
                    while at + es <= size {
                        for s in slots.iter() {
                            let off = at + s.off;
                            if off + 8 <= size && word(&host, off) != word(&snap, off) {
                                let v = self.virt_ptr(word(&host, off), s.kind);
                                set_word(&mut cur, off, v);
                            }
                        }
                        at += es;
                    }
                }
            }
            let _ = self.mem.write(base, &cur, 1);
            self.ffi.mirrors.get_mut(&base).unwrap().snap = snap;
        }
    }

    /// Drops the mirrors of freed allocations.
    fn purge_mirrors(&mut self) {
        if self.ffi.mirrors.len() < self.ffi.purge_at.max(256) {
            return;
        }
        let dead: Vec<u64> =
            self.ffi.mirrors.iter().filter(|(b, m)| !m.active && !self.mem.is_live(**b)).map(|(b, _)| *b).collect();
        for b in dead {
            if let Some(m) = self.ffi.mirrors.remove(&b) {
                self.ffi.by_host.remove(&(m.buf.ptr as usize));
            }
        }
        self.ffi.purge_at = self.ffi.mirrors.len() * 2;
    }

    // --- symbols ---------------------------------------------------------------------------------

    pub(super) fn global_symbol(&mut self, name: &str) -> Option<usize> {
        let libs: Vec<String> = self
            .prog
            .fns
            .iter()
            .filter_map(|f| match f.kind {
                FnKind::Foreign { lib, .. } => Some(lib.as_str().to_string()),
                _ => None,
            })
            .collect::<HashSet<_>>()
            .into_iter()
            .collect();
        for l in libs {
            if let Some(p) = self.ffi.symbol(&l, name) {
                return Some(p);
            }
        }
        self.ffi.symbol("c", name)
    }

    /// The Gamma function exported under `sym` (§19.4), which a foreign declaration of the
    /// same name refers to.
    fn export_of(&self, sym: &str) -> Option<FnId> {
        self.prog.exports.iter().find(|(s, _)| s.as_str() == sym).map(|(_, f)| *f)
    }

    // --- calls ---------------------------------------------------------------------------------

    fn prepare(&mut self, span: Span, fid: FnId, extra: &[Ty]) -> R<Rc<Prepared>> {
        if extra.is_empty()
            && let Some(p) = self.ffi.prepared.get(&fid)
        {
            return Ok(p.clone());
        }
        let prog = self.prog;
        let f = &prog.fns[fid.index()];
        let FnKind::Foreign { lib, link_name, variadic } = f.kind else { unreachable!() };
        let Some(sym) = self.ffi.symbol(lib.as_str(), link_name.as_str()) else {
            return self.trap(span, format!("cannot find the foreign function `{}`", link_name.as_str()));
        };
        let mut params: Vec<Ty> = f.params.iter().map(|p| f.locals[p.index()].ty).collect();
        let fixed = params.len();
        params.extend_from_slice(extra);
        let mut ctypes = Vec::new();
        for &t in &params {
            match self.ctype(t) {
                Some(c) if self.reach_ok(t, &mut HashSet::new()) => ctypes.push(c),
                _ => {
                    return self.trap(
                        span,
                        format!("an argument of the foreign function `{}` cannot be passed by the interpreter", f.name),
                    );
                }
            }
        }
        let ret = if matches!(f.ret.kind(), TyKind::Never) || self.size_of(f.ret) == 0 {
            CType::void()
        } else {
            match self.ctype(f.ret) {
                Some(c) => c,
                None => {
                    return self
                        .trap(span, format!("the result of `{}` cannot be received by the interpreter", f.name));
                }
            }
        };
        let cif = if variadic { Cif::new_variadic(ctypes, fixed, ret) } else { Cif::new(ctypes, ret) };
        let p = Rc::new(Prepared { sym, cif, params, ret: f.ret });
        if extra.is_empty() {
            self.ffi.prepared.insert(fid, p.clone());
        }
        Ok(p)
    }

    fn flush_out(&mut self) {
        if let Some(w) = self.out.as_mut() {
            let _ = w.flush();
        }
    }

    /// Calls the foreign function `fid`. `extra` are the types of the arguments in the
    /// variadic position (§19.2).
    pub fn call_foreign(&mut self, span: Span, fid: FnId, args: Vec<Value>, extra: &[Ty]) -> R {
        let prog = self.prog;
        let f = &prog.fns[fid.index()];
        let FnKind::Foreign { link_name, .. } = f.kind else { unreachable!() };
        if self.const_mode {
            return self.trap(span, format!("foreign function `{}` cannot be called at compile time", f.name));
        }
        // A foreign declaration of an exported Gamma function reaches the function itself.
        if let Some(target) = self.export_of(link_name.as_str()) {
            return self.call_function(span, target, args, None, Ctx::empty(), None, false);
        }
        let p = self.prepare(span, fid, extra)?;
        if args.len() != p.params.len() {
            return self.trap(span, format!("internal error: `{}` called with {} arguments", f.name, args.len()));
        }
        self.mem.host_access = true;
        let outermost = self.ffi.touched.is_empty();
        if outermost {
            self.purge_mirrors();
        }
        self.ffi.touched.push(Vec::new());

        // Argument buffers, aligned for any type.
        let mut bufs: Vec<Vec<u128>> = Vec::with_capacity(args.len());
        for (v, &ty) in args.iter().zip(&p.params) {
            let Value::Bytes(b) = v else {
                let t = self.ffi.touched.pop().unwrap();
                self.finish_mirrors(t);
                return self.trap(span, "internal error: a second-class value was passed to C");
            };
            let mut buf = vec![0u128; b.len().div_ceil(16).max(1)];
            // SAFETY: the buffer is at least `b.len()` bytes.
            let bytes = unsafe { std::slice::from_raw_parts_mut(buf.as_mut_ptr() as *mut u8, b.len()) };
            bytes.copy_from_slice(b);
            self.export_value(ty, bytes);
            bufs.push(buf);
        }
        self.snapshot_mirrors();

        let ret_size = if matches!(p.ret.kind(), TyKind::Never) { 0 } else { self.size_of(p.ret) };
        let mut ret = vec![0u128; ret_size.div_ceil(16).max(1)];
        let pure = f.effects.is_empty();
        if !pure {
            self.flush_out();
        }
        {
            let cargs: Vec<Arg> = bufs.iter().map(|b| Arg::new(unsafe { &*(b.as_ptr() as *const u8) })).collect();
            let prev = CURRENT.with(|c| c.replace(self as *mut Machine<'a> as *mut c_void));
            // SAFETY: the symbol, argument types and buffers match the cif built from the
            // declaration; pointer arguments are host addresses of live mirrors.
            unsafe {
                let rbuf = std::slice::from_raw_parts_mut(ret.as_mut_ptr() as *mut u8, ret_size.max(1));
                p.cif.call_return_into(CodePtr(p.sym as *mut c_void), &cargs, Ret::new(rbuf));
            }
            CURRENT.with(|c| c.set(prev));
        }
        if !pure {
            flush_c_streams();
        }

        let mut rbytes = vec![0u8; ret_size];
        rbytes.copy_from_slice(unsafe { std::slice::from_raw_parts(ret.as_ptr() as *const u8, ret_size) });
        self.import_value(p.ret, &mut rbytes);
        let touched = self.ffi.touched.pop().unwrap();
        self.finish_mirrors(touched);
        if let Some(mut flow) = self.ffi.pending.take() {
            // The callback was called from C, which has no source location: the call of the
            // foreign function stands for it.
            if let Flow::Trap(t) = &mut *flow
                && let Some(last) = t.trace.last_mut()
                && last.1 == Span::DUMMY
            {
                last.1 = span;
            }
            return Err(flow);
        }
        if matches!(p.ret.kind(), TyKind::Never) {
            return self.trap(span, format!("the foreign function `{}` returned although it does not return", f.name));
        }
        Ok(Value::Bytes(Bytes::from(rbytes)))
    }

    /// The address of a foreign variable.
    pub fn foreign_global_addr(&mut self, span: Span, name: &str) -> R<u64> {
        self.mem.host_access = true;
        match self.global_symbol(name) {
            Some(a) => Ok(a as u64),
            None => self.trap(span, format!("cannot find the foreign variable `{}`", name)),
        }
    }

    // --- callbacks -------------------------------------------------------------------------------

    /// The host code address C calls to reach the function `fid`.
    fn callback_code(&mut self, fid: FnId) -> Option<u64> {
        if let Some(c) = self.ffi.closures.get(&fid) {
            return Some(c.code);
        }
        let prog = self.prog;
        let def = &prog.fns[fid.index()];
        if let FnKind::Foreign { lib, link_name, .. } = def.kind {
            if let Some(g) = self.export_of(link_name.as_str()) {
                return self.callback_code(g);
            }
            return self.ffi.symbol(lib.as_str(), link_name.as_str()).map(|a| a as u64);
        }
        let params: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
        let mut ct = Vec::new();
        for &t in &params {
            ct.push(self.ctype(t)?);
        }
        let ret = if matches!(def.ret.kind(), TyKind::Never) || self.size_of(def.ret) == 0 {
            CType::void()
        } else {
            self.ctype(def.ret)?
        };
        let cif = Cif::new(ct, ret);
        let data = Box::new(CbData { func: fid, params, ret: def.ret });
        // SAFETY: the data is boxed (stable address) and kept next to the closure, which is
        // never freed.
        let dref: &'static CbData = unsafe { &*(&*data as *const CbData) };
        let closure = Closure::new(cif, trampoline, dref);
        let code = *closure.code_ptr() as usize;
        self.ffi.code_to_fn.insert(code, fid);
        self.ffi
            .closures
            .insert(fid, CbClosure { code: code as u64, _closure: ManuallyDrop::new(closure), _data: data });
        Some(code as u64)
    }

    /// Runs the Gamma function of a callback for C.
    fn run_callback(&mut self, d: &CbData, args: *const *const c_void, result: *mut u8) {
        if self.ffi.pending.is_some() {
            return;
        }
        let own_frame = self.ffi.touched.is_empty();
        if own_frame {
            self.ffi.touched.push(Vec::new());
        }
        let mut vals = Vec::with_capacity(d.params.len());
        for (i, &ty) in d.params.iter().enumerate() {
            let size = self.size_of(ty);
            // SAFETY: libffi passes a pointer to each argument, `size` bytes of the declared type.
            let mut b = unsafe { std::slice::from_raw_parts(*args.add(i) as *const u8, size) }.to_vec();
            // Data pointers stay host addresses, which the interpreter can use directly;
            // function pointers of ours turn back into interpreter ones.
            let slots = self.slots_cached(ty);
            for s in slots.iter() {
                if let SlotKind::Func = s.kind
                    && s.off + 8 <= b.len()
                {
                    let v = self.virt_ptr(word(&b, s.off), s.kind);
                    set_word(&mut b, s.off, v);
                }
            }
            vals.push(Value::Bytes(Bytes::from(b)));
        }
        let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            self.call_function(Span::DUMMY, d.func, vals, None, Ctx::empty(), None, false)
        }));
        match r {
            Ok(Ok(Value::Bytes(b))) => {
                let mut b = b.to_vec();
                self.export_value(d.ret, &mut b);
                self.snapshot_mirrors();
                // libffi expects integers narrower than a register widened to it.
                let small = match (self.ti(d.ret).int, d.ret.kind()) {
                    (Some((signed, bits)), _) if bits < 64 => Some((signed, bits)),
                    (_, TyKind::Bool) => Some((false, 8)),
                    _ => None,
                };
                if let Some((signed, bits)) = small {
                    let mut w = [0u8; 8];
                    w[..b.len()].copy_from_slice(&b);
                    let mut v = u64::from_le_bytes(w);
                    if signed {
                        v = (((v << (64 - bits)) as i64) >> (64 - bits)) as u64;
                    }
                    b = v.to_le_bytes().to_vec();
                }
                // SAFETY: libffi provides a result buffer of at least the type's size, and of a
                // register's size for integers.
                unsafe { std::ptr::copy_nonoverlapping(b.as_ptr(), result, b.len()) };
            }
            Ok(Ok(_)) => {}
            Ok(Err(flow)) => self.ffi.pending = Some(flow),
            Err(p) => self.ffi.pending = Some(Box::new(Flow::Trap(super::panic_trap(p)))),
        }
        if own_frame {
            let t = self.ffi.touched.pop().unwrap();
            self.finish_mirrors(t);
        }
    }

    /// Ends the process through C's `exit`, keeping the interpreter alive for the callbacks
    /// registered to run then (`atexit`). Does nothing unless callbacks were handed to C and
    /// [`set_owns_process`] allows it.
    pub(super) fn exit_process(&mut self, code: u8) {
        if !OWNS_PROCESS.load(Ordering::Relaxed) || !self.ffi.has_callbacks() {
            return;
        }
        self.flush_out();
        CURRENT.with(|c| c.set(self as *mut Machine<'a> as *mut c_void));
        // SAFETY: `exit` runs the registered handlers and ends the process.
        unsafe { libc::exit(code as i32) }
    }
}

unsafe extern "C" fn trampoline(_cif: &ffi_cif, result: &mut u64, args: *const *const c_void, data: &CbData) {
    let result = result as *mut u64 as *mut u8;
    // SAFETY: libffi gives a result buffer of at least a register.
    unsafe { std::ptr::write_bytes(result, 0, 8) };
    let m = CURRENT.with(|c| c.get());
    if m.is_null() {
        return;
    }
    // SAFETY: set by the foreign call (or process exit) that is running C on this thread, which
    // does not touch the machine meanwhile.
    let m = unsafe { &mut *(m as *mut Machine<'static>) };
    m.run_callback(data, args, result);
}
