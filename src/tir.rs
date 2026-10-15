//! The typed intermediate representation (TIR).
//!
//! Semantic analysis lowers the syntax tree to TIR. TIR is consumed by the
//! interpreter (compile-time evaluation and `gamma run`) and by the C back end.
//!
//! Invariants established by semantic analysis:
//!
//! - Every expression has its final type: no inference variables remain.
//!   (Generic functions are lowered once with [`TyKind::Param`] types and
//!   instantiated by substitution before execution.)
//! - Names are resolved to locals, functions, constants or globals.
//! - Implicit coercions (§6.4) are explicit [`ExprKind::Coerce`] nodes;
//!   auto-dereference of field access and method receivers is an explicit
//!   [`ExprKind::Deref`] / [`ExprKind::AddrOf`].
//! - Operators are resolved to typed operations ([`BinOp`], [`UnOp`]).
//! - Struct construction lists every field in declaration order (defaults filled in).
//! - `for` over ranges, arrays and slices is lowered to [`ExprKind::Loop`];
//!   `for` over an iterator call is lowered to a `with` installing a handler.
//! - Method calls are ordinary calls; untyped constants are inlined as constants.
//!
//! [`TyKind::Param`]: crate::types::TyKind::Param

use crate::source::Span;
use crate::symbol::Symbol;
use crate::types::{AdtId, EffectId, EffectRef, EffectSet, Ty};
use num_bigint::BigInt;
use std::collections::HashMap;
use std::rc::Rc;

macro_rules! id_type {
    ($($(#[$m:meta])* $name:ident;)*) => {$(
        $(#[$m])*
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
        pub struct $name(pub u32);
        impl $name {
            pub fn index(self) -> usize { self.0 as usize }
        }
    )*};
}

id_type! {
    /// A function body: a named function, a function literal, or a handler clause.
    FnId;
    /// A local variable or parameter, unique within its [`FnDef`].
    LocalId;
    /// A loop or `with` that `break`/`continue` can target, unique within its [`FnDef`].
    LabelId;
    /// A top-level constant whose value is computed at compile time.
    ConstId;
    /// A `#global` variable.
    GlobalId;
    /// A handler literal, unique within the program.
    HandlerId;
    /// A handler declaration `handler E for T`.
    HandlerDeclId;
}

/// The system services used by the core library's default handlers (§12.9). They are
/// intrinsics available only to core modules. Operations that can fail return an `i64`:
/// a count when non-negative, otherwise an error code (see [`io_error_code`]).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SysOp {
    /// `@sys_read(buf: []u8) -> i64`: reads standard input.
    Read,
    /// `@clock_now() -> u64`: monotonic nanoseconds.
    ClockNow,
    /// `@clock_wall() -> i64`: Unix time in nanoseconds.
    ClockWall,
    /// `@clock_sleep(ns: u64)`.
    Sleep,
    /// `@rand_fill(buf: []u8)`: bytes from the operating system's generator.
    RandFill,
    /// `@fs_open(path: string, flags: u32) -> i64`: a file handle. The flags are
    /// [`FS_READ`], [`FS_WRITE`], … combined.
    FsOpen,
    /// `@fs_close(f: i64) -> i64`.
    FsClose,
    /// `@fs_read(f: i64, buf: []u8) -> i64`: 0 at the end of the file.
    FsRead,
    /// `@fs_write(f: i64, bytes: []const u8) -> i64`.
    FsWrite,
    /// `@fs_seek(f: i64, offset: i64, whence: u32) -> i64`: the new position. `whence` is
    /// 0 (from the start), 1 (from the current position) or 2 (from the end).
    FsSeek,
    /// `@fs_stat(path: string, out: []i64) -> i64`: stores the size, the kind (0 file, 1
    /// directory, 2 symbolic link, 3 other) and the modification time (Unix nanoseconds)
    /// in `out[0..3]`.
    FsStat,
    /// `@fs_remove(path: string) -> i64`: removes a file.
    FsRemove,
    /// `@fs_rename(from, to: string) -> i64`.
    FsRename,
    /// `@fs_make_dir(path: string) -> i64`.
    FsMakeDir,
    /// `@fs_remove_dir(path: string) -> i64`: removes an empty directory.
    FsRemoveDir,
    /// `@fs_open_dir(path: string) -> i64`: a directory handle.
    FsOpenDir,
    /// `@fs_next_entry(d: i64, buf: []u8) -> i64`: stores the next entry's name (never `.`
    /// or `..`) in `buf` and returns its length plus its kind (as in `FsStat`) times
    /// 2^32; 0 after the last. A name longer than `buf` is an `InvalidInput` error.
    FsNextEntry,
    /// `@fs_close_dir(d: i64) -> i64`.
    FsCloseDir,
    /// `@fs_lstat(path: string, out: []i64) -> i64`: like `FsStat`, but a final symbolic
    /// link is described, not followed.
    FsLstat,
    /// `@fs_truncate(f: i64, size: u64) -> i64`: sets the file's length.
    FsTruncate,
    /// `@fs_sync(f: i64) -> i64`: flushes the file to the storage device.
    FsSync,
    /// `@fs_cwd(buf: []u8) -> i64`: stores the working directory in `buf` and returns its
    /// length; `InvalidInput` if it does not fit.
    FsCwd,
    /// `@net_connect(host: string, port: u16) -> i64`: a TCP connection's handle.
    NetConnect,
    /// `@net_listen(host: string, port: u16) -> i64`: a listening socket's handle (port 0:
    /// any free port).
    NetListen,
    /// `@net_local_port(h: i64) -> i64`: the port a socket is bound to.
    NetLocalPort,
    /// `@net_accept(listener: i64) -> i64`: the next connection's handle.
    NetAccept,
    /// `@net_send(h: i64, bytes: []const u8) -> i64`.
    NetSend,
    /// `@net_recv(h: i64, buf: []u8) -> i64`: 0 when the peer has closed the connection.
    NetRecv,
    /// `@net_close(h: i64) -> i64`: closes a connection or a listening socket.
    NetClose,
    /// `@net_shutdown(h: i64, how: u32) -> i64`: 0 stops reading, 1 stops writing, 2 both.
    NetShutdown,
    /// `@net_set_timeouts(h: i64, recv_ns: u64, send_ns: u64) -> i64`: 0 means no timeout.
    NetSetTimeouts,
    /// `@net_peer(h: i64, buf: []u8) -> i64`: stores the peer's `host:port` in `buf` and
    /// returns its length; `InvalidInput` if it does not fit.
    NetPeer,
    /// `@thread_spawn(f: ^fn(arg: rawptr) \ {Sys}, arg: rawptr) -> i64`: a new thread
    /// running `f(arg)` with no handlers installed (§19.6); its handle.
    ThreadSpawn,
    /// `@thread_join(h: i64) -> i64`: waits for the thread to finish.
    ThreadJoin,
    /// `@thread_detach(h: i64) -> i64`: the thread is never joined; its resources are
    /// released when it ends.
    ThreadDetach,
    /// `@thread_yield()`: lets other threads run.
    ThreadYield,
    /// `@futex_wait(addr: ^u32, expected: u32, timeout_ns: i64) -> i64`: blocks while
    /// `addr^` is `expected`, until woken or the timeout passes (negative: no timeout).
    /// Returns 0 or an error code; wakes up spuriously now and then.
    FutexWait,
    /// `@futex_wake(addr: ^u32, n: u32) -> i64`: wakes up to `n` threads waiting on `addr`
    /// (`u32::MAX`: all).
    FutexWake,
    /// `@cpu_count() -> u32`: the number of processors available to the program (at least 1).
    CpuCount,
    /// `@sys_write(fd: i32, bytes: []const u8)`: standard output (1) or error (2).
    SysWrite,
}

/// Flags of [`SysOp::FsOpen`].
pub const FS_READ: u32 = 1;
pub const FS_WRITE: u32 = 2;
pub const FS_APPEND: u32 = 4;
pub const FS_CREATE: u32 = 8;
pub const FS_TRUNCATE: u32 = 16;
pub const FS_EXCLUSIVE: u32 = 32;

impl SysOp {
    /// The intrinsic's name, its parameter types and its result type.
    pub fn signature(self) -> (&'static str, Vec<Ty>, Ty) {
        let i64 = Ty::int(true, 64);
        let buf = Ty::intern(crate::types::TyKind::Slice { elem: Ty::u8(), is_const: false });
        match self {
            SysOp::Read => ("sys_read", vec![buf], i64),
            SysOp::ClockNow => ("clock_now", vec![], Ty::int(false, 64)),
            SysOp::ClockWall => ("clock_wall", vec![], i64),
            SysOp::Sleep => ("clock_sleep", vec![Ty::int(false, 64)], Ty::unit()),
            SysOp::RandFill => ("rand_fill", vec![buf], Ty::unit()),
            SysOp::FsOpen => ("fs_open", vec![Ty::string(), Ty::int(false, 32)], i64),
            SysOp::FsClose => ("fs_close", vec![i64], i64),
            SysOp::FsRead => ("fs_read", vec![i64, buf], i64),
            SysOp::FsWrite => ("fs_write", vec![i64, Ty::string()], i64),
            SysOp::FsSeek => ("fs_seek", vec![i64, i64, Ty::int(false, 32)], i64),
            SysOp::FsStat => (
                "fs_stat",
                vec![Ty::string(), Ty::intern(crate::types::TyKind::Slice { elem: i64, is_const: false })],
                i64,
            ),
            SysOp::FsRemove => ("fs_remove", vec![Ty::string()], i64),
            SysOp::FsRename => ("fs_rename", vec![Ty::string(), Ty::string()], i64),
            SysOp::FsMakeDir => ("fs_make_dir", vec![Ty::string()], i64),
            SysOp::FsRemoveDir => ("fs_remove_dir", vec![Ty::string()], i64),
            SysOp::FsOpenDir => ("fs_open_dir", vec![Ty::string()], i64),
            SysOp::FsNextEntry => ("fs_next_entry", vec![i64, buf], i64),
            SysOp::FsCloseDir => ("fs_close_dir", vec![i64], i64),
            SysOp::FsLstat => (
                "fs_lstat",
                vec![Ty::string(), Ty::intern(crate::types::TyKind::Slice { elem: i64, is_const: false })],
                i64,
            ),
            SysOp::FsTruncate => ("fs_truncate", vec![i64, Ty::int(false, 64)], i64),
            SysOp::FsSync => ("fs_sync", vec![i64], i64),
            SysOp::FsCwd => ("fs_cwd", vec![buf], i64),
            SysOp::NetConnect => ("net_connect", vec![Ty::string(), Ty::int(false, 16)], i64),
            SysOp::NetListen => ("net_listen", vec![Ty::string(), Ty::int(false, 16)], i64),
            SysOp::NetLocalPort => ("net_local_port", vec![i64], i64),
            SysOp::NetAccept => ("net_accept", vec![i64], i64),
            SysOp::NetSend => ("net_send", vec![i64, Ty::string()], i64),
            SysOp::NetRecv => ("net_recv", vec![i64, buf], i64),
            SysOp::NetClose => ("net_close", vec![i64], i64),
            SysOp::NetShutdown => ("net_shutdown", vec![i64, Ty::int(false, 32)], i64),
            SysOp::NetSetTimeouts => ("net_set_timeouts", vec![i64, Ty::int(false, 64), Ty::int(false, 64)], i64),
            SysOp::NetPeer => ("net_peer", vec![i64, buf], i64),
            // The function's type is checked separately: any `^fn(arg: rawptr) \ {Sys}`.
            SysOp::ThreadSpawn => ("thread_spawn", vec![Ty::intern(crate::types::TyKind::Rawptr); 2], i64),
            SysOp::ThreadJoin => ("thread_join", vec![i64], i64),
            SysOp::ThreadDetach => ("thread_detach", vec![i64], i64),
            SysOp::ThreadYield => ("thread_yield", vec![], Ty::unit()),
            SysOp::FutexWait => ("futex_wait", vec![Ty::ptr(false, Ty::u32()), Ty::u32(), i64], i64),
            SysOp::FutexWake => ("futex_wake", vec![Ty::ptr(false, Ty::u32()), Ty::u32()], i64),
            SysOp::CpuCount => ("cpu_count", vec![], Ty::u32()),
            SysOp::SysWrite => ("sys_write", vec![Ty::int(true, 32), Ty::string()], Ty::unit()),
        }
    }

    pub const ALL: [SysOp; 40] = [
        SysOp::Read,
        SysOp::ClockNow,
        SysOp::ClockWall,
        SysOp::Sleep,
        SysOp::RandFill,
        SysOp::FsOpen,
        SysOp::FsClose,
        SysOp::FsRead,
        SysOp::FsWrite,
        SysOp::FsSeek,
        SysOp::FsStat,
        SysOp::FsRemove,
        SysOp::FsRename,
        SysOp::FsMakeDir,
        SysOp::FsRemoveDir,
        SysOp::FsOpenDir,
        SysOp::FsNextEntry,
        SysOp::FsCloseDir,
        SysOp::FsLstat,
        SysOp::FsTruncate,
        SysOp::FsSync,
        SysOp::FsCwd,
        SysOp::NetConnect,
        SysOp::NetListen,
        SysOp::NetLocalPort,
        SysOp::NetAccept,
        SysOp::NetSend,
        SysOp::NetRecv,
        SysOp::NetClose,
        SysOp::NetShutdown,
        SysOp::NetSetTimeouts,
        SysOp::NetPeer,
        SysOp::ThreadSpawn,
        SysOp::ThreadJoin,
        SysOp::ThreadDetach,
        SysOp::ThreadYield,
        SysOp::FutexWait,
        SysOp::FutexWake,
        SysOp::CpuCount,
        SysOp::SysWrite,
    ];
}

/// The error codes of [`SysOp`] results: `-1 - i` for the `i`-th variant of
/// `IoError` other than `Other`, and `-1000 - errno` for `Other(errno)`.
pub fn io_error_code(kind: IoErrorKind) -> i64 {
    match kind {
        IoErrorKind::Other(code) => -1000 - code as i64,
        k => -1 - k.index() as i64,
    }
}

/// The variants of the core library's `IoError` (§21.7), in declaration order.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum IoErrorKind {
    NotFound,
    PermissionDenied,
    AlreadyExists,
    WouldBlock,
    Interrupted,
    BrokenPipe,
    ConnectionReset,
    InvalidInput,
    UnexpectedEof,
    NoSpace,
    ConnectionRefused,
    TimedOut,
    Other(i32),
}

impl IoErrorKind {
    fn index(self) -> usize {
        match self {
            IoErrorKind::NotFound => 0,
            IoErrorKind::PermissionDenied => 1,
            IoErrorKind::AlreadyExists => 2,
            IoErrorKind::WouldBlock => 3,
            IoErrorKind::Interrupted => 4,
            IoErrorKind::BrokenPipe => 5,
            IoErrorKind::ConnectionReset => 6,
            IoErrorKind::InvalidInput => 7,
            IoErrorKind::UnexpectedEof => 8,
            IoErrorKind::NoSpace => 9,
            IoErrorKind::ConnectionRefused => 10,
            IoErrorKind::TimedOut => 11,
            IoErrorKind::Other(_) => 12,
        }
    }
}

/// `@min_stack`: the smallest fiber stack, in bytes (§14.1).
pub const MIN_STACK: u64 = 16384;

/// The bytes at the end of every fiber stack that Gamma frames may not use: they are
/// kept for the run-time support code called from the deepest frame.
pub const STACK_RESERVE: u64 = 8192;

/// What `@stack_size` adds for the fiber's own start-up frames and for a `ctl`
/// perform, which saves the fiber's state on its stack before switching away.
pub const STACK_BASE: u64 = 1024;

/// The stack bytes charged for one call of `def`, the cost model shared by
/// `@stack_size` (§14.8) and the interpreter's fiber stacks. It bounds a compiled
/// frame generously: a fixed overhead for the return address, saved registers and
/// spills; the locals; every composite temporary bigger than a register (call
/// results, constructed values, and the copies made by reading a large value, but
/// not by using it as a place); and the records of the handlers its `with`s install.
pub fn frame_cost(def: &FnDef, size_of: &mut dyn FnMut(Ty) -> u64) -> u64 {
    let mut c = 256u64;
    for l in &def.locals {
        c += size_of(l.ty).next_multiple_of(8);
    }
    if let Some(b) = &def.body {
        // `place` is set for an expression used as a place (the base of an index,
        // field or slice, an operand of `&`, the target of an assignment): reading
        // it makes no copy.
        fn temps(e: &Expr, place: bool, size_of: &mut dyn FnMut(Ty) -> u64, c: &mut u64) {
            let mut add = |t: Ty, c: &mut u64| {
                let n = size_of(t);
                if n > 8 {
                    *c += n.next_multiple_of(8);
                }
            };
            match &e.kind {
                ExprKind::With { handlers, .. } => *c += 128 * handlers.len() as u64,
                ExprKind::Call { .. }
                | ExprKind::Aggregate(_)
                | ExprKind::Array(_)
                | ExprKind::ArrayFill(_)
                | ExprKind::Variant { .. }
                | ExprKind::Builtin { .. } => add(e.ty, c),
                // Reading a large value copies it.
                ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Global(_) if !place => add(e.ty, c),
                ExprKind::Deref(x) | ExprKind::Field { base: x, .. } | ExprKind::Index { base: x, .. } => {
                    if !place {
                        add(e.ty, c);
                    }
                    temps(x, true, size_of, c);
                    if let ExprKind::Index { index, .. } = &e.kind {
                        temps(index, false, size_of, c);
                    }
                    return;
                }
                ExprKind::AddrOf(x) | ExprKind::SliceLen(x) | ExprKind::SlicePtr(x) => {
                    temps(x, true, size_of, c);
                    return;
                }
                ExprKind::Slice { base, .. } => {
                    temps(base, true, size_of, c);
                    e.for_each_child(&mut |x| {
                        if !std::ptr::eq(x, &**base) {
                            temps(x, false, size_of, c)
                        }
                    });
                    return;
                }
                ExprKind::Block(b) => {
                    for st in &b.stmts {
                        match st {
                            Stmt::Assign { place, value } => {
                                temps(place, true, size_of, c);
                                temps(value, false, size_of, c);
                            }
                            Stmt::AssignTuple { places, value } => {
                                for p in places.iter().flatten() {
                                    temps(p, true, size_of, c);
                                }
                                temps(value, false, size_of, c);
                            }
                            Stmt::Let { init: Some(x), .. }
                            | Stmt::LetTuple { init: x, .. }
                            | Stmt::Expr(x)
                            | Stmt::Defer { body: x, .. } => temps(x, false, size_of, c),
                            Stmt::Let { init: None, .. } => {}
                        }
                    }
                    if let Some(t) = &b.tail {
                        temps(t, false, size_of, c);
                    }
                    return;
                }
                _ => {}
            }
            e.for_each_child(&mut |x| temps(x, false, size_of, c));
        }
        temps(b, false, size_of, &mut c);
    }
    c
}

impl Program {
    /// Whether the program declares foreign functions (§19.2).
    pub fn has_foreign(&self) -> bool {
        self.fns.iter().any(|f| matches!(f.kind, FnKind::Foreign { .. }))
    }
}

/// A whole program, ready to run.
#[derive(Debug, Default)]
pub struct Program {
    pub fns: Vec<FnDef>,
    pub consts: Vec<ConstDef>,
    pub globals: Vec<GlobalDef>,
    pub handlers: Vec<HandlerLit>,
    pub handler_decls: Vec<HandlerDecl>,
    /// Definitions of the (instantiated) nominal types used by the program.
    pub adts: HashMap<AdtId, AdtDef>,
    /// The entry point, for executables.
    pub main: Option<FnId>,
    /// The tests of the root module and of the modules of its package that it
    /// imports, when compiled for testing (§3.8).
    pub tests: Vec<TestCase>,
    /// The `bench` declarations, selected like the tests (§3.9).
    pub benches: Vec<TestCase>,
    /// Functions exported to C under a symbol (§19.4).
    pub exports: Vec<(Symbol, FnId)>,
    /// The predeclared effects whose default handlers the runtime provides (§12.9).
    pub ambient: Ambient,
    /// The `ctl` operations (§14), by effect and operation index: performing one
    /// suspends the running fiber.
    pub ctl_ops: std::collections::HashSet<(EffectId, u32)>,
    /// The instances of the core library's `new` and `destroy`, which the interpreter
    /// may run directly while the default heap allocator is current.
    pub heap_fns: HashMap<FnId, HeapFn>,
    /// The core library's `heap_allocator`, once something instantiates `new` or `destroy`.
    pub heap_allocator: Option<FnId>,
    /// The target the program was checked for: layouts and `target` constants
    /// depend on it, so back ends use the same one.
    pub target: crate::layout::Target,
}

/// Which of the allocating functions of the core library an instance is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HeapFn {
    New,
    Destroy,
}

/// Ids of the predeclared ambient effects, when the program uses them.
#[derive(Debug, Default, Clone)]
pub struct Ambient {
    /// `Out { write_out: fn(bytes: []const u8) }`
    pub out: Option<EffectId>,
    /// `Err { write_err: fn(bytes: []const u8) }`
    pub err: Option<EffectId>,
    /// `Env { args, var, exit }` (operation indices 0, 1, 2)
    pub env: Option<EffectId>,
    /// `Alloc { current: fn() -> Allocator }`
    pub alloc: Option<EffectId>,
    /// `Sys`, which has no operations (§12.10).
    pub sys: Option<EffectId>,
    /// `In { read }`
    pub input: Option<EffectId>,
    /// `Clock { now, wall, sleep }`
    pub clock: Option<EffectId>,
    /// `Rand { fill }`
    pub rand: Option<EffectId>,
    /// `Fs { open, … }`
    pub fs: Option<EffectId>,
    /// `Net { connect, … }`
    pub net: Option<EffectId>,
    /// `Test { note, fail }` (§3.8)
    pub test: Option<EffectId>,
}

impl Program {
    pub fn func(&self, id: FnId) -> &FnDef {
        &self.fns[id.index()]
    }

    /// How trap backtraces name function `f`, or `None` for functions of the core
    /// library and the `<entry>` functions that call `main` and tests, whose frames
    /// backtraces leave out.
    pub fn backtrace_name(&self, f: FnId, sources: &crate::source::SourceMap) -> Option<String> {
        let def = self.func(f);
        if def.span != Span::DUMMY && sources.file(def.span.file).path.starts_with("<core>") {
            return None;
        }
        if def.name.as_str() == "<entry>" {
            return None;
        }
        let name = def.name.as_str();
        let parent = def.parent.and_then(|p| self.backtrace_name(p, sources));
        Some(match (def.kind, parent) {
            (FnKind::Clause, Some(p)) if name.starts_with('<') => format!("{} in {}", name, p),
            (FnKind::Clause, Some(p)) => format!("`{}` clause in {}", name, p),
            (FnKind::Clause, None) => format!("`{}` clause", name),
            (FnKind::Literal, Some(p)) if name.starts_with('<') => format!("{} in {}", name, p),
            _ => name.to_string(),
        })
    }
    pub fn adt(&self, id: AdtId) -> &AdtDef {
        &self.adts[&id]
    }
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct FnDef {
    pub name: Symbol,
    pub span: Span,
    /// Parameters, in order. Each is also an entry in `locals`.
    pub params: Vec<LocalId>,
    pub locals: Vec<LocalDecl>,
    pub ret: Ty,
    pub effects: EffectSet,
    /// For function literals and handler clauses that capture locals: the
    /// function whose frame they capture (by reference). `None` for top-level
    /// and nested named functions, which capture nothing (§11.8).
    pub parent: Option<FnId>,
    /// What kind of body this is (affects how `return` and `break` behave).
    pub kind: FnKind,
    /// The calling convention (§19.2); other than C only for plain functions.
    pub callconv: crate::types::CallConv,
    /// `#fast_math`, `#inline`, `#noinline` and `#cold` (§11.10).
    pub hints: FnHints,
    /// The body; its type is `ret` (or `never`). `None` for foreign functions.
    pub body: Option<Expr>,
}

/// Attributes of a function that do not change its meaning (§11.10), except that
/// `#fast_math` float results may differ.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FnHints {
    /// Float contraction and reassociation are permitted (§5.3).
    pub fast_math: bool,
    /// `Some(true)` for `#inline`, `Some(false)` for `#noinline`.
    pub inline: Option<bool>,
    pub cold: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FnKind {
    /// A top-level or nested named function.
    Named,
    /// A function literal (second-class if it captures, §11.6).
    Literal,
    /// A clause of a handler literal or handler declaration. Its parameters are
    /// the operation's arguments (preceded by `self` for declarations, followed
    /// by the continuation for `ctl` operations).
    Clause,
    /// A function declared in a `foreign` block.
    Foreign { lib: Symbol, link_name: Symbol, variadic: bool },
}

#[derive(Clone, Debug)]
pub struct LocalDecl {
    pub name: Symbol,
    pub ty: Ty,
    pub mutable: bool,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Data definitions
// ---------------------------------------------------------------------------

/// A struct or union definition, after generic instantiation.
#[derive(Clone, Debug)]
pub struct AdtDef {
    pub name: Symbol,
    pub kind: AdtKind,
}

#[derive(Clone, Debug)]
pub enum AdtKind {
    Struct {
        fields: Vec<FieldDef>,
        packed: bool,
        align: Option<u64>,
        bits: Option<Ty>,
    },
    Union {
        tag: Ty,
        variants: Vec<VariantDef>,
    },
    /// `#c_union`: every field at offset 0, no tag.
    CUnion {
        fields: Vec<FieldDef>,
    },
}

#[derive(Clone, Debug)]
pub struct FieldDef {
    pub name: Symbol,
    pub ty: Ty,
    pub is_pub: bool,
    /// `#align(n)` on the field.
    pub align: Option<u64>,
}

#[derive(Clone, Debug)]
pub struct VariantDef {
    pub name: Symbol,
    pub payload: Option<Ty>,
    /// The tag value (fits the union's tag type).
    pub tag: BigInt,
}

/// A `test` declaration (§3.8): its name and the entry function that runs it.
#[derive(Clone, Debug)]
pub struct TestCase {
    pub name: String,
    /// The module of a test outside the root module: its directory relative to the
    /// package root (§3.8).
    pub module: Option<String>,
    /// A parameterless function returning `u8`: 0 if the test passed.
    pub entry: FnId,
    pub span: Span,
}

#[derive(Debug)]
pub struct ConstDef {
    pub name: Symbol,
    pub ty: Ty,
    pub span: Span,
    /// The initializer, evaluated once at compile time.
    pub init: Expr,
    /// The value, once evaluated.
    pub value: Option<ConstValue>,
}

/// A compile-time value in target layout. Pointers inside it point into static
/// data and are recorded as relocations, since static data has no fixed address
/// until the program is loaded or linked.
#[derive(Clone, Debug, PartialEq)]
pub struct ConstValue {
    pub ty: Ty,
    pub bytes: Vec<u8>,
    pub relocs: Vec<Reloc>,
}

/// A pointer stored at `offset` within a [`ConstValue`]: the address of
/// `target` plus `addend` bytes.
#[derive(Clone, Debug, PartialEq)]
pub struct Reloc {
    pub offset: u64,
    pub target: StaticData,
    pub addend: u64,
}

#[derive(Clone, Debug, PartialEq)]
pub enum StaticData {
    /// Read-only bytes such as a string literal's contents (a C string's
    /// stored bytes include its NUL terminator).
    Bytes(Rc<[u8]>),
    /// The storage of another constant.
    Const(ConstId),
    /// The code address of a function (for function pointers).
    Fn(FnId),
}

#[derive(Debug)]
pub struct GlobalDef {
    pub name: Symbol,
    pub ty: Ty,
    pub span: Span,
    /// `None` for foreign globals.
    pub init: Option<Expr>,
    /// The initializer's compile-time value (`None` for foreign globals).
    pub value: Option<ConstValue>,
    pub thread_local: bool,
    /// For a foreign global: its symbol.
    pub link_name: Option<Symbol>,
    /// The alignment `#align(n)` raises the global's to.
    pub align: Option<u64>,
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

/// A handler literal (§12.5): one clause per operation of the effect, each a
/// [`FnDef`] of kind `Clause` whose `parent` is the function containing the literal.
#[derive(Debug)]
pub struct HandlerLit {
    pub effect: EffectRef,
    /// Indexed by operation number.
    pub clauses: Vec<FnId>,
}

/// A handler declaration `handler E for T` (§12.6). Each clause takes `self: T` first.
#[derive(Debug)]
pub struct HandlerDecl {
    pub effect: EffectRef,
    pub for_ty: Ty,
    pub clauses: Vec<FnId>,
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Expr {
    pub ty: Ty,
    pub kind: ExprKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum ExprKind {
    // --- constants ---
    /// An integer constant of integer type `ty` (fits the type).
    Int(BigInt),
    /// A float constant of float type `ty`, already rounded to that type; stored as f64
    /// (exact for f16 and f32 values).
    Float(f64),
    /// `@size_of(T)` (or `@align_of(T)` when `align`) of a type that mentions type
    /// parameters; folded to a constant when the generic body is instantiated.
    /// Never present in executable code.
    SizeOf {
        of: Ty,
        align: bool,
    },
    /// `@min_value(T)` (or `@max_value(T)` when `max`) of a type parameter with `Int(T)`;
    /// folded to an `Int` when the generic body is instantiated. Never present in
    /// executable code.
    IntLimit {
        of: Ty,
        max: bool,
    },
    /// `@type_name(T)` of a type that mentions type parameters; folded to a `Str` when
    /// the generic body is instantiated. Never present in executable code.
    TypeName {
        of: Ty,
    },
    /// `a is b` (or `is not`, when `negated`) on types that mention type parameters;
    /// folded to a `Bool` when the generic body is instantiated. Never present in
    /// executable code.
    TypeIs {
        a: Ty,
        b: Ty,
        negated: bool,
    },
    /// An untyped float literal's exact value. Exists only while semantic analysis
    /// is inferring literal types; never present in a finished [`Program`].
    UntypedFloat(Rc<num_rational::BigRational>),
    Bool(bool),
    /// A string literal: a `string` (slice) pointing at static bytes.
    Str(Rc<[u8]>),
    /// A C string literal: a `cstring` pointing at static, NUL-terminated bytes
    /// (the stored bytes exclude the terminator).
    CStr(Rc<[u8]>),
    /// The absent value of the optional type `ty`.
    Null,
    /// A plain-data value of type `ty`, given by its bytes in target layout
    /// (a compile-time argument substituted into an instantiation).
    ConstBytes(Rc<[u8]>),
    /// A compile-time value parameter inside a generic body (§16.5). Never
    /// present in a finished [`Program`]: instantiation replaces it.
    ValueParam(crate::types::ParamId),
    /// `---`: unspecified contents (plain-data `ty` only).
    Uninit,
    /// A reference to a top-level constant's value.
    Const(ConstId),

    // --- places ---
    /// A local of the current function.
    Local(LocalId),
    /// A local of an enclosing function, `levels` frames up the capture chain (≥ 1).
    Upvar {
        levels: u32,
        local: LocalId,
    },
    /// A `#global` (performs `Sys`).
    Global(GlobalId),
    /// `p^`: the place a pointer points to.
    Deref(Box<Expr>),
    /// Field `index` of a struct or tuple (place if `base` is a place).
    Field {
        base: Box<Expr>,
        index: u32,
    },
    /// The payload of a union value known to be variant `variant` (used after a
    /// successful pattern test only; not checked).
    VariantPayload {
        base: Box<Expr>,
        variant: u32,
    },
    /// `a[i]` on an array (place or value), slice or many-pointer. `checked` is true
    /// for arrays and slices (bounds checked, §18.2).
    Index {
        base: Box<Expr>,
        index: Box<Expr>,
        checked: bool,
    },
    /// `a[lo..hi]` giving a slice. Missing bounds are 0 and `len`.
    /// `base` is a place of array type, a slice, or a many-pointer.
    Slice {
        base: Box<Expr>,
        lo: Option<Box<Expr>>,
        hi: Option<Box<Expr>>,
        inclusive: bool,
    },
    /// `s.len` / `s.ptr` of a slice value.
    SliceLen(Box<Expr>),
    SlicePtr(Box<Expr>),

    // --- values ---
    /// `&place` (the place is never a temporary, §7.2).
    AddrOf(Box<Expr>),
    /// Address of a temporary holding the value of `expr`, alive until the end of the
    /// enclosing statement (used for `^const T` method receivers, §7.10).
    AddrOfTemp(Box<Expr>),
    /// A named function or non-capturing literal used as a value (function pointer or
    /// second-class function value).
    FnRef(FnId),
    /// A capturing function literal: a second-class value pairing `FnId` with the
    /// current frame (and the current handler context, §12.7).
    Closure(FnId),
    /// A handler literal value, capturing the current frame and handler context.
    HandlerRef(HandlerId),
    Unary {
        op: UnOp,
        operand: Box<Expr>,
    },
    Binary {
        op: BinOp,
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    /// Short-circuit `and` / `or`.
    Logic {
        is_and: bool,
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    Coerce {
        kind: Coercion,
        expr: Box<Expr>,
    },
    /// An explicit conversion or builtin method / intrinsic.
    Builtin {
        op: Builtin,
        args: Vec<Expr>,
    },
    Call {
        callee: Callee,
        args: Vec<Expr>,
    },
    /// Construct a struct (fields in declaration order) or tuple (elements in order).
    Aggregate(Vec<Expr>),
    /// `[N]T(e1, …)`.
    Array(Vec<Expr>),
    /// `[N]T(fill: x)`.
    ArrayFill(Box<Expr>),
    /// Union variant construction.
    Variant {
        variant: u32,
        payload: Option<Box<Expr>>,
    },
    /// `!e`: a result in the error state.
    MakeErr(Box<Expr>),
    /// `x?` / `x!`: checked unwrap (traps when absent / error, §10.3).
    Unwrap {
        expr: Box<Expr>,
        is_result: bool,
    },
    /// `x is y` (structural equality, §7.6); `negated` for `is not`.
    Eq {
        lhs: Box<Expr>,
        rhs: Box<Expr>,
        negated: bool,
    },
    /// `x in c` for ranges, arrays/slices and inline tuples (§7.7).
    In {
        elem: Box<Expr>,
        container: Box<Expr>,
        negated: bool,
    },

    // --- control flow ---
    Block(Box<Block>),
    /// `if` with parts joined by `and`; binding parts are pattern matches (§8.2).
    If {
        cond: Vec<CondPart>,
        then: Box<Expr>,
        else_: Option<Box<Expr>>,
    },
    /// `match` (§8.6); arms are tried in order.
    Match {
        scrutinee: Box<Expr>,
        arms: Vec<Arm>,
    },
    /// `x else d`; with `capture`, the error value is bound to that local in `rhs`.
    Else {
        lhs: Box<Expr>,
        capture: Option<LocalId>,
        rhs: Box<Expr>,
    },
    /// `try x`: unwraps or returns the absent/error value (lifted via `lift`) from the
    /// function `levels` frames up (0 = the current one).
    Try {
        expr: Box<Expr>,
        lift: Option<Lift>,
        levels: u32,
    },
    /// A loop: `while`, or a lowered `for`. Each iteration checks `cond` (all parts
    /// must succeed; binding parts bind for the body), then runs `body`; after the body
    /// completes normally or by `continue`, `step` (if any) runs, then the next iteration
    /// begins. `break` leaves the loop without running `step`. The loop's value is `()`.
    Loop {
        label: LabelId,
        cond: Vec<CondPart>,
        body: Box<Block>,
        step: Option<Box<Expr>>,
    },
    Break {
        target: Target,
        value: Option<Box<Expr>>,
    },
    Continue {
        target: Target,
    },
    /// `return` from the function `levels` frames up (0 = current).
    Return {
        levels: u32,
        value: Box<Expr>,
    },
    /// `with h1, h2 { body }` (§12.7); `label` is the target of `break` from clauses.
    With {
        label: LabelId,
        handlers: Vec<HandlerValue>,
        on: Option<Box<Expr>>,
        body: Box<Expr>,
    },
    /// `resume k(v)`.
    Resume {
        cont: Box<Expr>,
        value: Box<Expr>,
    },
    /// `cancel k`.
    Cancel(Box<Expr>),
    /// `trap(msg)` or a failed `assert`/`@unreachable()`: stops the program.
    Trap {
        message: Box<Expr>,
    },
    /// `@write(sink, template, args)`: `pieces` are the literal parts of the template,
    /// interleaved with `args` (`pieces.len() == args.len() + 1`). Each argument is
    /// formatted by its type and its format spec in `specs` (§21.6.1) and the output
    /// passed to the sink in pieces.
    Write {
        sink: WriteSink,
        pieces: Vec<Rc<[u8]>>,
        specs: Vec<FmtSpec>,
        args: Vec<Expr>,
    },
    /// A `Write` whose template is a compile-time `string` parameter of a generic
    /// function: each instance splits the template and its arguments (§16.1). `tmp`
    /// holds the arguments when they are a tuple of several values.
    WriteDeferred {
        sink: WriteSink,
        template: Box<Expr>,
        args: Option<Box<Expr>>,
        tmp: LocalId,
    },
}

/// Where `@write` sends its output.
#[derive(Clone, Debug)]
pub enum WriteSink {
    /// Call a second-class function value `fn(bytes: []const u8)`.
    Fn(Box<Expr>),
    /// Perform an operation taking `[]const u8` (e.g. `Out.write_out` for `print`).
    Perform { effect: EffectRef, op: u32 },
}

/// The target of a `break` or `continue`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Target {
    /// A loop or `with` in the function `levels` frames up the capture chain
    /// (0 = the current function body).
    Label { levels: u32, label: LabelId },
    /// The `with` that installed the handler whose clause is running (§12.7.1).
    InstallingWith,
}

#[derive(Clone, Debug)]
pub struct Block {
    pub stmts: Vec<Stmt>,
    /// The value of the block; `None` means `()` (or `never` if the last statement diverges).
    pub tail: Option<Expr>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum Stmt {
    /// Initialize a local (`init` is `None` only for `---` of plain data, which
    /// sema represents as `Some(Uninit)`; kept optional for synthesized locals).
    Let {
        local: LocalId,
        init: Option<Expr>,
    },
    /// Bind several locals from a tuple value (destructuring, §4.5). `None` entries discard.
    LetTuple {
        locals: Vec<Option<LocalId>>,
        init: Expr,
    },
    /// `place = value` (compound assignments are lowered to this with a single
    /// evaluation of the place's sub-expressions via temporaries).
    Assign {
        place: Expr,
        value: Expr,
    },
    /// `a, b = (x, y)`: all values computed before any store.
    AssignTuple {
        places: Vec<Option<Expr>>,
        value: Expr,
    },
    Expr(Expr),
    Defer {
        is_err: bool,
        body: Expr,
    },
}

#[derive(Clone, Debug)]
pub enum CondPart {
    Bool(Expr),
    /// Matches `value` against `pat`, binding its locals for later parts and `then`.
    Match {
        pat: Pat,
        value: Expr,
    },
}

#[derive(Clone, Debug)]
pub struct Arm {
    pub pat: Pat,
    pub guard: Option<Expr>,
    pub body: Expr,
}

#[derive(Clone, Debug)]
pub enum Callee {
    /// A direct call to a known function.
    Fn(FnId),
    /// A call through a function pointer or second-class function value.
    Indirect(Box<Expr>),
    /// Performing operation `op` (index into the effect's operations) of `effect`.
    Perform { effect: EffectRef, op: u32 },
}

/// How to convert an error value when `try` propagates it (§10.6).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Lift {
    /// Wrap the error in variant `variant` of the function's error union.
    Variant(u32),
}

/// A handler installed by `with`.
#[derive(Clone, Debug)]
pub enum HandlerValue {
    /// A handler literal value (evaluates `expr`, of handler type).
    Literal(Expr),
    /// A value of type `T` installing every handler declared `for T` (§12.6).
    Decl { decls: Vec<HandlerDeclId>, value: Expr },
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Pat {
    pub ty: Ty,
    pub kind: PatKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum PatKind {
    Wild,
    /// Binds the matched value to a local (a copy).
    Bind(LocalId),
    /// Matches a value `is`-equal to the constant (integers, bools, strings).
    Const(Box<Expr>),
    /// Integer range.
    Range {
        lo: BigInt,
        hi: BigInt,
        inclusive: bool,
    },
    /// The absent optional.
    Null,
    /// A present optional whose value matches.
    Some(Box<Pat>),
    /// A successful result whose value matches.
    Ok(Box<Pat>),
    /// A failed result whose error matches.
    Err(Box<Pat>),
    /// A union value of the given variant; payload pattern if any.
    Variant {
        variant: u32,
        payload: Option<Box<Pat>>,
    },
    /// Struct fields or tuple elements by index; unlisted ones are ignored.
    Fields(Vec<(u32, Pat)>),
    /// Alternatives `p1, p2` of a match arm, tried in order. All alternatives
    /// bind the same locals.
    Or(Vec<Pat>),
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

pub use crate::syntax::ast::Flavor;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UnOp {
    /// Integer negation (checked) or float negation, chosen by the operand type.
    Neg,
    /// Bitwise complement (integers and `#bits` structs).
    BitNot,
    /// Logical not.
    Not,
}

/// Binary operators on numbers. The operand type (`lhs.ty`) selects integer or float
/// semantics; arrays apply the operator elementwise (§7.4.6).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BinOp {
    Add(Flavor),
    Sub(Flavor),
    Mul(Flavor),
    Shl(Flavor),
    Div,
    Rem,
    Shr,
    BitAnd,
    BitOr,
    BitXor,
    Lt,
    Le,
    Gt,
    Ge,
}

/// The operation of an atomic read-modify-write (§19.6).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RmwOp {
    Add,
    Sub,
    And,
    Or,
    Xor,
    Max,
    Min,
}

/// Implicit coercions (§6.4), made explicit.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Coercion {
    /// Integer widening (including to `usize`/`isize`).
    IntWiden,
    /// Float widening.
    FloatWiden,
    /// `T` → `?T`.
    WrapOptional,
    /// `T` → `E!T`.
    WrapOk,
    /// `E!T` → `F!T` lifting the error into variant `variant` of `F` (§10.6).
    LiftErr { variant: u32 },
    /// `never` → any type (the operand never produces a value).
    FromNever,
    /// Pointer coercions of §5.5 that do not change the representation
    /// (const-adding, kind change, to `rawptr`, embedding `^Outer` → `^S`).
    PtrToPtr,
    /// `^[N]T` → `[]T` (or the const forms).
    ArrayPtrToSlice,
    /// Tuple name erasure / naming (§5.9), or the identity conversion `T(x)` of a `T`
    /// (§6.5): same layout.
    TupleRename,
    /// A named function to a function pointer or second-class value.
    FnToPtr,
    /// A function pointer to a pointer type with a larger effect set (§12.3).
    FnPtrWiden,
    /// Applies the inner coercion inside an optional (`?^T` → `?^const T`).
    InOptional,
    /// A coercion from an unresolved literal type, decided once inference is done.
    /// Exists only during semantic analysis; never present in a finished [`Program`].
    Unresolved,
}

/// Explicit conversions, builtin methods (§21.3) and intrinsics (§20.3).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Builtin {
    /// `T(x)` lossless conversion (§6.5): integer→integer/float, float→float,
    /// bool→integer, enum→integer, `#bits` ↔ integer. Target is the expression type.
    Convert,
    /// `x.trunc(T)` (integer: keep low bits; float: toward zero, checked).
    Trunc,
    /// `x.round(T)`: float→integer (checked), integer→float, float→narrower float.
    Round,
    Floor,
    Ceil,
    /// `x.checked(T)` → `?T`.
    Checked,
    /// `x.sat(T)`.
    Sat,
    /// `x.bits(T)`: reinterpret bytes.
    Bits,
    /// `p.cast(P)` (checked alignment).
    Cast,
    /// `p.addr()`.
    Addr,
    /// `p + n` and `p - n` on a many-pointer (`n: usize` or `isize`), moving by
    /// whole elements; the address wraps.
    PtrAdd,
    PtrSub,
    /// `p - q` on many-pointers: the distance in elements, as `isize`.
    PtrDiff,
    /// `n.to_ptr(P)` → `?P`.
    ToPtr,
    /// `u.tag()`.
    Tag,
    /// `U.from_tag(x)` → `?U`.
    FromTag,
    Min,
    Max,
    Clamp,
    Abs,
    AbsDiff,
    Pow(Flavor),
    IsNan,
    IsInf,
    IsFinite,
    SignBit,
    Sqrt,
    Fma,
    Clz,
    Ctz,
    Popcount,
    Bswap,
    Bitreverse,
    /// `@rotl(x, n)` / `@rotr(x, n)`: rotate the bits of `x` by `n` (a `u32`, taken
    /// modulo the width).
    Rotl,
    Rotr,
    /// `@static_assert(cond, …)` in a generic body whose condition depends on its
    /// parameters: evaluated for each instance during monomorphization, then removed.
    /// The number indexes `Sema::deferred_asserts`.
    StaticAssert(u32),
    /// `a.get(i)` → `?T`.
    Get,
    /// `a.get_ptr(i)` → `?^T`.
    GetPtr,
    /// `a.first()` → `?T`.
    First,
    /// `a.last()` → `?T`.
    Last,
    /// `r.len()` of a range: the number of values it contains (0 if empty).
    RangeLen,

    /// `s.cstr_len()`.
    CstrLen,
    /// `assert(cond, msg, loc)`: checked (removed in Fast builds).
    Assert,
    /// `@hash(x, seed)`.
    Hash,
    /// `@unreachable()`: traps when reached (checked, §18).
    Unreachable,
    /// `@black_box(x)`: `x`, opaque to the optimizer (§21.9).
    BlackBox,
    /// Counts one execution of a statement, for `gamma test --coverage`: inserted by
    /// [`crate::coverage`], never by sema. The number indexes the counters.
    Cover(u32),
    /// `@atomic_load(p, order)` (§19.6). The memory orders are `MemoryOrder` values.
    AtomicLoad,
    /// `@atomic_store(p, v, order)`.
    AtomicStore,
    /// `@atomic_swap(p, v, order)` → the old value.
    AtomicSwap,
    /// `@atomic_cas(p, expected, new, success, failure)` → `?T`: `null` on success,
    /// the observed value on failure.
    AtomicCas,
    /// `@atomic_add(p, v, order)` and the other read-modify-write operations → the
    /// old value (wrapping arithmetic).
    AtomicRmw(RmwOp),
    /// `@fence(order)`.
    Fence,
    /// `@volatile_load(p)` (§19.5).
    VolatileLoad,
    /// `@volatile_store(p, v)`.
    VolatileStore,
    /// A system service behind a default handler of the core library (§12.9).
    Sys(SysOp),
    /// `@fiber(f, arg, stack)` in a `ctl` clause (§14.6) → `cont(())`: a new fiber of the
    /// clause's `with`, about to call the function pointer `f` with `arg`.
    Fiber,
    /// Allocates `size` bytes aligned to `align` from the system heap → `?[^]u8`.
    /// Used only by the core library's heap allocator.
    HeapAlloc,
    /// Frees a heap block `(p, size, align)` obtained from `HeapAlloc`.
    HeapFree,
    /// `@alloc_tag(p, owner, vt, file, line, column)`: in Debug builds, records that the
    /// block at `p` was allocated by the allocator `(owner, vt)` at that source location
    /// (§18.3). Used only by the core library's `new` and `make`.
    AllocTag,
    /// `@alloc_check(p, owner, vt, file, line, column)`: in Debug builds, traps if the block
    /// at `p` was allocated by another allocator than the one freeing it, reporting both
    /// sites, and forgets the block (§18.3).
    AllocCheck,
    /// `@asm(template, outputs, inputs, clobbers)` (§19.5). The arguments are the
    /// template (a `Str`), then a constraint (`Str`) and a pointer for each output,
    /// then a constraint and a value for each input, then each clobber (`Str`).
    Asm {
        outputs: u32,
        inputs: u32,
    },
}

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

impl Expr {
    /// Calls `f` on each direct sub-expression, including those inside
    /// statements, conditions, match arms and patterns.
    pub fn for_each_child<'a>(&'a self, f: &mut dyn FnMut(&'a Expr)) {
        match &self.kind {
            ExprKind::Deref(a)
            | ExprKind::AddrOf(a)
            | ExprKind::AddrOfTemp(a)
            | ExprKind::SliceLen(a)
            | ExprKind::SlicePtr(a)
            | ExprKind::MakeErr(a)
            | ExprKind::Cancel(a)
            | ExprKind::ArrayFill(a)
            | ExprKind::Unary { operand: a, .. }
            | ExprKind::Coerce { expr: a, .. }
            | ExprKind::Unwrap { expr: a, .. }
            | ExprKind::Try { expr: a, .. }
            | ExprKind::Field { base: a, .. }
            | ExprKind::VariantPayload { base: a, .. }
            | ExprKind::Return { value: a, .. }
            | ExprKind::Trap { message: a } => f(a),
            ExprKind::Index { base, index, .. } => {
                f(base);
                f(index);
            }
            ExprKind::Slice { base, lo, hi, .. } => {
                f(base);
                if let Some(x) = lo.as_deref() {
                    f(x);
                }
                if let Some(x) = hi.as_deref() {
                    f(x);
                }
            }
            ExprKind::Binary { lhs, rhs, .. }
            | ExprKind::Logic { lhs, rhs, .. }
            | ExprKind::Eq { lhs, rhs, .. }
            | ExprKind::Else { lhs, rhs, .. } => {
                f(lhs);
                f(rhs);
            }
            ExprKind::In { elem, container, .. } => {
                f(elem);
                f(container);
            }
            ExprKind::Builtin { args, .. } | ExprKind::Aggregate(args) | ExprKind::Array(args) => {
                args.iter().for_each(f)
            }
            ExprKind::Call { callee, args } => {
                if let Callee::Indirect(c) = callee {
                    f(c);
                }
                args.iter().for_each(f);
            }
            ExprKind::Variant { payload, .. } => {
                if let Some(x) = payload.as_deref() {
                    f(x);
                }
            }
            ExprKind::Block(b) => b.for_each_child(f),
            ExprKind::If { cond, then, else_ } => {
                cond.iter().for_each(|c| c.for_each_child(f));
                f(then);
                if let Some(x) = else_.as_deref() {
                    f(x);
                }
            }
            ExprKind::Match { scrutinee, arms } => {
                f(scrutinee);
                for a in arms.iter() {
                    a.pat.for_each_child(f);
                    if let Some(g) = a.guard.iter().next() {
                        f(g);
                    }
                    f(&a.body);
                }
            }
            ExprKind::Loop { cond, body, step, .. } => {
                cond.iter().for_each(|c| c.for_each_child(f));
                body.for_each_child(f);
                if let Some(x) = step.as_deref() {
                    f(x);
                }
            }
            ExprKind::Break { value, .. } => {
                if let Some(x) = value.as_deref() {
                    f(x);
                }
            }
            ExprKind::With { handlers, on, body, .. } => {
                for h in handlers.iter() {
                    match h {
                        HandlerValue::Literal(x) | HandlerValue::Decl { value: x, .. } => f(x),
                    }
                }
                if let Some(x) = on.as_deref() {
                    f(x);
                }
                f(body);
            }
            ExprKind::Resume { cont, value } => {
                f(cont);
                f(value);
            }
            ExprKind::Write { sink, args, .. } => {
                if let WriteSink::Fn(x) = sink {
                    f(x);
                }
                args.iter().for_each(f);
            }
            ExprKind::WriteDeferred { sink, template, args, .. } => {
                if let WriteSink::Fn(x) = sink {
                    f(x);
                }
                f(template);
                if let Some(a) = args {
                    f(a);
                }
            }
            _ => {}
        }
    }
}

impl Block {
    /// Calls `f` on each expression directly inside the block's statements and tail.
    pub fn for_each_child<'a>(&'a self, f: &mut dyn FnMut(&'a Expr)) {
        for s in self.stmts.iter() {
            match s {
                Stmt::Let { init, .. } => {
                    if let Some(x) = init.iter().next() {
                        f(x);
                    }
                }
                Stmt::LetTuple { init, .. } => f(init),
                Stmt::Assign { place, value } => {
                    f(place);
                    f(value);
                }
                Stmt::AssignTuple { places, value } => {
                    places.iter().flatten().for_each(&mut *f);
                    f(value);
                }
                Stmt::Expr(e) | Stmt::Defer { body: e, .. } => f(e),
            }
        }
        if let Some(t) = self.tail.iter().next() {
            f(t);
        }
    }
}

impl CondPart {
    pub fn for_each_child<'a>(&'a self, f: &mut dyn FnMut(&'a Expr)) {
        match self {
            CondPart::Bool(e) => f(e),
            CondPart::Match { pat, value } => {
                pat.for_each_child(f);
                f(value);
            }
        }
    }
}

impl Pat {
    /// Calls `f` on the constant expressions inside the pattern.
    pub fn for_each_child<'a>(&'a self, f: &mut dyn FnMut(&'a Expr)) {
        match &self.kind {
            PatKind::Const(e) => f(e),
            PatKind::Some(p) | PatKind::Ok(p) | PatKind::Err(p) => p.for_each_child(f),
            PatKind::Variant { payload: Some(p), .. } => p.for_each_child(f),
            PatKind::Fields(fs) => fs.iter().for_each(|(_, p)| p.for_each_child(f)),
            PatKind::Or(ps) => ps.iter().for_each(|p| p.for_each_child(f)),
            _ => {}
        }
    }
}

impl Expr {
    /// Calls `f` on each direct sub-expression, including those inside
    /// statements, conditions, match arms and patterns.
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        match &mut self.kind {
            ExprKind::Deref(a)
            | ExprKind::AddrOf(a)
            | ExprKind::AddrOfTemp(a)
            | ExprKind::SliceLen(a)
            | ExprKind::SlicePtr(a)
            | ExprKind::MakeErr(a)
            | ExprKind::Cancel(a)
            | ExprKind::ArrayFill(a)
            | ExprKind::Unary { operand: a, .. }
            | ExprKind::Coerce { expr: a, .. }
            | ExprKind::Unwrap { expr: a, .. }
            | ExprKind::Try { expr: a, .. }
            | ExprKind::Field { base: a, .. }
            | ExprKind::VariantPayload { base: a, .. }
            | ExprKind::Return { value: a, .. }
            | ExprKind::Trap { message: a } => f(a),
            ExprKind::Index { base, index, .. } => {
                f(base);
                f(index);
            }
            ExprKind::Slice { base, lo, hi, .. } => {
                f(base);
                if let Some(x) = lo.as_deref_mut() {
                    f(x);
                }
                if let Some(x) = hi.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Binary { lhs, rhs, .. }
            | ExprKind::Logic { lhs, rhs, .. }
            | ExprKind::Eq { lhs, rhs, .. }
            | ExprKind::Else { lhs, rhs, .. } => {
                f(lhs);
                f(rhs);
            }
            ExprKind::In { elem, container, .. } => {
                f(elem);
                f(container);
            }
            ExprKind::Builtin { args, .. } | ExprKind::Aggregate(args) | ExprKind::Array(args) => {
                args.iter_mut().for_each(f)
            }
            ExprKind::Call { callee, args } => {
                if let Callee::Indirect(c) = callee {
                    f(c);
                }
                args.iter_mut().for_each(f);
            }
            ExprKind::Variant { payload, .. } => {
                if let Some(x) = payload.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Block(b) => b.for_each_child_mut(f),
            ExprKind::If { cond, then, else_ } => {
                cond.iter_mut().for_each(|c| c.for_each_child_mut(f));
                f(then);
                if let Some(x) = else_.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Match { scrutinee, arms } => {
                f(scrutinee);
                for a in arms.iter_mut() {
                    a.pat.for_each_child_mut(f);
                    if let Some(g) = a.guard.iter_mut().next() {
                        f(g);
                    }
                    f(&mut a.body);
                }
            }
            ExprKind::Loop { cond, body, step, .. } => {
                cond.iter_mut().for_each(|c| c.for_each_child_mut(f));
                body.for_each_child_mut(f);
                if let Some(x) = step.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Break { value, .. } => {
                if let Some(x) = value.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::With { handlers, on, body, .. } => {
                for h in handlers.iter_mut() {
                    match h {
                        HandlerValue::Literal(x) | HandlerValue::Decl { value: x, .. } => f(x),
                    }
                }
                if let Some(x) = on.as_deref_mut() {
                    f(x);
                }
                f(body);
            }
            ExprKind::Resume { cont, value } => {
                f(cont);
                f(value);
            }
            ExprKind::Write { sink, args, .. } => {
                if let WriteSink::Fn(x) = sink {
                    f(x);
                }
                args.iter_mut().for_each(f);
            }
            ExprKind::WriteDeferred { sink, template, args, .. } => {
                if let WriteSink::Fn(x) = sink {
                    f(x);
                }
                f(template);
                if let Some(a) = args {
                    f(a);
                }
            }
            _ => {}
        }
    }
}

impl Block {
    /// Calls `f` on each expression directly inside the block's statements and tail.
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        for s in self.stmts.iter_mut() {
            match s {
                Stmt::Let { init, .. } => {
                    if let Some(x) = init.iter_mut().next() {
                        f(x);
                    }
                }
                Stmt::LetTuple { init, .. } => f(init),
                Stmt::Assign { place, value } => {
                    f(place);
                    f(value);
                }
                Stmt::AssignTuple { places, value } => {
                    places.iter_mut().flatten().for_each(&mut *f);
                    f(value);
                }
                Stmt::Expr(e) | Stmt::Defer { body: e, .. } => f(e),
            }
        }
        if let Some(t) = self.tail.iter_mut().next() {
            f(t);
        }
    }
}

impl CondPart {
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        match self {
            CondPart::Bool(e) => f(e),
            CondPart::Match { pat, value } => {
                pat.for_each_child_mut(f);
                f(value);
            }
        }
    }
}

impl Pat {
    /// Calls `f` on the constant expressions inside the pattern.
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        match &mut self.kind {
            PatKind::Const(e) => f(e),
            PatKind::Some(p) | PatKind::Ok(p) | PatKind::Err(p) => p.for_each_child_mut(f),
            PatKind::Variant { payload: Some(p), .. } => p.for_each_child_mut(f),
            PatKind::Fields(fs) => fs.iter_mut().for_each(|(_, p)| p.for_each_child_mut(f)),
            PatKind::Or(ps) => ps.iter_mut().for_each(|p| p.for_each_child_mut(f)),
            _ => {}
        }
    }
}

/// How a placeholder's text is aligned within its width (§21.6.1).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FmtAlign {
    /// Right for numbers, left for everything else.
    #[default]
    Auto,
    Left,
    Right,
    Center,
}

/// How a placeholder writes a number (§21.6.1).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FmtKind {
    /// The display rule of the argument's type.
    #[default]
    Display,
    /// Integers in base 16 (`x`, `X`), 8 (`o`) or 2 (`b`).
    Hex,
    HexUpper,
    Oct,
    Bin,
    /// Floats in scientific notation (`e`).
    Exp,
}

/// The format spec of a template placeholder, `{:[[fill]align][+][#][0][width][.precision][kind]}`
/// (§21.6.1). The default is the plain `{}`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FmtSpec {
    pub fill: u8,
    pub align: FmtAlign,
    /// `+`: write a sign for non-negative numbers too.
    pub plus: bool,
    /// `#`: write the base prefix (`0x`, `0o`, `0b`).
    pub alt: bool,
    /// `0`: pad a number with zeros after its sign and prefix.
    pub zero: bool,
    pub width: u32,
    pub precision: Option<u32>,
    pub kind: FmtKind,
}

impl Default for FmtSpec {
    fn default() -> Self {
        FmtSpec {
            fill: b' ',
            align: FmtAlign::Auto,
            plus: false,
            alt: false,
            zero: false,
            width: 0,
            precision: None,
            kind: FmtKind::Display,
        }
    }
}

impl FmtSpec {
    pub fn is_default(&self) -> bool {
        *self == FmtSpec::default()
    }
}
