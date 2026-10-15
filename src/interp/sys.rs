//! The system services behind the core library's default handlers (§12.9):
//! standard input, clocks and random bytes. Compiled programs get the same
//! services from the C runtime.

use super::machine::*;
use super::value::*;
use crate::tir::{self, Expr, IoErrorKind, SysOp, io_error_code};
use crate::types::Ty;
use std::collections::HashMap;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

/// Open files and directories, by handle.
#[derive(Default)]
pub struct SysState {
    files: HashMap<i64, std::fs::File>,
    dirs: HashMap<i64, std::fs::ReadDir>,
    // Shared, so a blocking call can use one while other threads run (§19.6).
    streams: HashMap<i64, std::sync::Arc<std::net::TcpStream>>,
    listeners: HashMap<i64, std::sync::Arc<std::net::TcpListener>>,
    last: i64,
}

impl SysState {
    fn handle(&mut self) -> i64 {
        self.last += 1;
        self.last + 2
    }
}

fn path_of(bytes: Vec<u8>) -> PathBuf {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStringExt;
        PathBuf::from(std::ffi::OsString::from_vec(bytes))
    }
    #[cfg(not(unix))]
    {
        PathBuf::from(String::from_utf8_lossy(&bytes).into_owned())
    }
}

fn name_bytes(name: std::ffi::OsString) -> Vec<u8> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStringExt;
        name.into_vec()
    }
    #[cfg(not(unix))]
    {
        name.to_string_lossy().into_owned().into_bytes()
    }
}

/// A result of a file-system call as an `i64`: the value, or an error code.
fn code(r: std::io::Result<i64>) -> i64 {
    r.unwrap_or_else(|e| error_code(&e))
}

fn bad_handle() -> std::io::Error {
    std::io::Error::from_raw_os_error(9) // EBADF
}

/// The error code of a failed system call (see [`io_error_code`]).
pub fn error_code(e: &std::io::Error) -> i64 {
    use std::io::ErrorKind as K;
    let kind = match e.kind() {
        K::NotFound => IoErrorKind::NotFound,
        K::PermissionDenied => IoErrorKind::PermissionDenied,
        K::AlreadyExists => IoErrorKind::AlreadyExists,
        K::WouldBlock => IoErrorKind::WouldBlock,
        K::Interrupted => IoErrorKind::Interrupted,
        K::BrokenPipe => IoErrorKind::BrokenPipe,
        K::ConnectionReset => IoErrorKind::ConnectionReset,
        K::InvalidInput => IoErrorKind::InvalidInput,
        K::UnexpectedEof => IoErrorKind::UnexpectedEof,
        K::StorageFull => IoErrorKind::NoSpace,
        K::ConnectionRefused => IoErrorKind::ConnectionRefused,
        K::TimedOut => IoErrorKind::TimedOut,
        _ => IoErrorKind::Other(e.raw_os_error().unwrap_or(0)),
    };
    io_error_code(kind)
}

/// The kind of a directory entry as `FsStat` reports it.
fn kind_code(t: std::fs::FileType) -> i64 {
    if t.is_file() {
        0
    } else if t.is_dir() {
        1
    } else if t.is_symlink() {
        2
    } else {
        3
    }
}

/// The error code of a failed network call: a timed-out blocking call reports
/// `WouldBlock`, which callers see as `TimedOut` (sockets are never non-blocking).
fn net_error(e: &std::io::Error) -> i64 {
    if e.kind() == std::io::ErrorKind::WouldBlock { io_error_code(IoErrorKind::TimedOut) } else { error_code(e) }
}

/// The start of the monotonic clock: the first time it is read.
/// Fills `buf` with random bytes from the system.
#[cfg(not(windows))]
fn random_bytes(buf: &mut [u8]) -> std::io::Result<()> {
    std::fs::File::open("/dev/urandom").and_then(|mut f| f.read_exact(buf))
}

/// Fills `buf` with random bytes from the system.
#[cfg(windows)]
fn random_bytes(buf: &mut [u8]) -> std::io::Result<()> {
    #[link(name = "bcrypt")]
    unsafe extern "system" {
        fn BCryptGenRandom(alg: *mut std::ffi::c_void, buf: *mut u8, len: u32, flags: u32) -> i32;
    }
    const BCRYPT_USE_SYSTEM_PREFERRED_RNG: u32 = 2;
    for chunk in buf.chunks_mut(u32::MAX as usize) {
        // SAFETY: `chunk` is writable for its length.
        let status = unsafe {
            BCryptGenRandom(
                std::ptr::null_mut(),
                chunk.as_mut_ptr(),
                chunk.len() as u32,
                BCRYPT_USE_SYSTEM_PREFERRED_RNG,
            )
        };
        if status != 0 {
            return Err(std::io::Error::other(format!("BCryptGenRandom failed with status {status:#x}")));
        }
    }
    Ok(())
}

fn epoch() -> Instant {
    static START: std::sync::OnceLock<Instant> = std::sync::OnceLock::new();
    *START.get_or_init(Instant::now)
}

impl<'a> Machine<'a> {
    pub fn eval_sys(&mut self, a: Act, e: &Expr, op: SysOp, args: &[Expr]) -> R {
        let mut vals = vec![];
        for x in args {
            vals.push(self.eval_bytes(a, x)?);
        }
        let int = |m: &Self, t: Ty, n: i128| {
            Value::Bytes(m.int_bytes(t, &super::num::Int::from_big(num_bigint::BigInt::from(n))))
        };
        match op {
            SysOp::Read => {
                let (ptr, len) = self.slice_parts(&vals[0]);
                let mut buf = vec![0u8; len as usize];
                let _ = self.out.as_mut().map(|w| w.flush());
                let r = self.without_gil(|| {
                    loop {
                        match std::io::stdin().lock().read(&mut buf) {
                            Err(err) if err.kind() == std::io::ErrorKind::Interrupted => {}
                            r => break r,
                        }
                    }
                });
                let n = match r {
                    Ok(n) => n,
                    Err(err) => return Ok(int(self, e.ty, error_code(&err) as i128)),
                };
                self.store(e.span, ptr, &buf[..n], 1)?;
                Ok(int(self, e.ty, n as i128))
            }
            SysOp::ClockNow => Ok(int(self, e.ty, epoch().elapsed().as_nanos() as i128)),
            SysOp::ClockWall => {
                let t = match SystemTime::now().duration_since(UNIX_EPOCH) {
                    Ok(d) => d.as_nanos() as i128,
                    Err(before) => -(before.duration().as_nanos() as i128),
                };
                Ok(int(self, e.ty, t))
            }
            SysOp::Sleep => {
                let ns = self.int_of(args[0].ty, &vals[0]).to_u64_sat();
                self.without_gil(|| std::thread::sleep(std::time::Duration::from_nanos(ns)));
                Ok(Value::unit())
            }
            SysOp::FsOpen
            | SysOp::FsClose
            | SysOp::FsRead
            | SysOp::FsWrite
            | SysOp::FsSeek
            | SysOp::FsStat
            | SysOp::FsRemove
            | SysOp::FsRename
            | SysOp::FsMakeDir
            | SysOp::FsRemoveDir
            | SysOp::FsOpenDir
            | SysOp::FsNextEntry
            | SysOp::FsCloseDir
            | SysOp::FsLstat
            | SysOp::FsTruncate
            | SysOp::FsSync
            | SysOp::FsCwd => {
                let n = self.fs_op(e, op, args, &vals)?;
                Ok(int(self, e.ty, n as i128))
            }
            SysOp::NetConnect
            | SysOp::NetListen
            | SysOp::NetLocalPort
            | SysOp::NetAccept
            | SysOp::NetSend
            | SysOp::NetRecv
            | SysOp::NetClose
            | SysOp::NetShutdown
            | SysOp::NetSetTimeouts
            | SysOp::NetPeer => {
                let n = self.net_op(e, op, args, &vals)?;
                Ok(int(self, e.ty, n as i128))
            }
            SysOp::ThreadSpawn => {
                let f = self.ptr_of(&vals[0]);
                let h = self.spawn_thread(e.span, f, Value::Bytes(vals[1].clone()))?;
                Ok(int(self, e.ty, h as i128))
            }
            SysOp::ThreadJoin => {
                let h = self.int_arg(&args[0], &vals[0]);
                let r = self.join_thread(h)?;
                Ok(int(self, e.ty, r as i128))
            }
            SysOp::ThreadDetach => {
                let h = self.int_arg(&args[0], &vals[0]);
                let r = self.detach_thread(h);
                Ok(int(self, e.ty, r as i128))
            }
            SysOp::ThreadYield => {
                self.yield_thread()?;
                Ok(Value::unit())
            }
            SysOp::FutexWait => {
                let addr = self.ptr_of(&vals[0]);
                let expected = self.int_arg(&args[1], &vals[1]) as u32;
                let timeout = self.int_arg(&args[2], &vals[2]);
                let n = self.futex_wait(e.span, addr, expected, timeout)?;
                Ok(int(self, e.ty, n as i128))
            }
            SysOp::FutexWake => {
                // Every waiter wakes (a wake-up may be spurious) and re-checks its word.
                let (lock, cv) = &*futex();
                *lock.lock().unwrap_or_else(|e| e.into_inner()) += 1;
                cv.notify_all();
                Ok(int(self, e.ty, 0))
            }
            SysOp::CpuCount => {
                let n = std::thread::available_parallelism().map_or(1, |n| n.get());
                Ok(int(self, e.ty, n as i128))
            }
            SysOp::SysWrite => {
                let fd = self.int_arg(&args[0], &vals[0]);
                let data = self.read_str(e.span, &vals[1])?;
                let w = if fd == 2 { self.errw.as_mut() } else { self.out.as_mut() };
                if let Some(w) = w {
                    let _ = w.write_all(&data);
                    if fd == 2 {
                        let _ = w.flush();
                    }
                }
                Ok(Value::unit())
            }
            SysOp::RandFill => {
                let (ptr, len) = self.slice_parts(&vals[0]);
                let mut buf = vec![0u8; len as usize];
                if let Err(err) = random_bytes(&mut buf) {
                    return self.trap(e.span, format!("cannot read random bytes: {}", err));
                }
                self.store(e.span, ptr, &buf, 1)?;
                Ok(Value::unit())
            }
        }
    }
}

type Futex = (std::sync::Mutex<u64>, std::sync::Condvar);

/// The wake-up counter and condition variable shared by all futex waiters.
fn futex() -> std::sync::Arc<Futex> {
    static F: std::sync::OnceLock<std::sync::Arc<Futex>> = std::sync::OnceLock::new();
    F.get_or_init(Default::default).clone()
}

impl<'a> Machine<'a> {
    /// `@futex_wait`: sleeps while the word at `addr` is `expected`. The interpreter lock
    /// serializes the check with every wake-up, so none is missed.
    fn futex_wait(&mut self, span: crate::source::Span, addr: u64, expected: u32, timeout: i64) -> R<i64> {
        let word = self.load(span, addr, 4, 4)?;
        if self.int_of(Ty::u32(), &word).to_big() != expected.into() {
            return Ok(0);
        }
        let fx = futex();
        let seen = *fx.0.lock().unwrap_or_else(|e| e.into_inner());
        let deadline = (timeout >= 0).then(|| Instant::now() + std::time::Duration::from_nanos(timeout as u64));
        self.without_gil(|| {
            let mut g = fx.0.lock().unwrap_or_else(|e| e.into_inner());
            while *g == seen {
                match deadline {
                    None => g = fx.1.wait(g).unwrap_or_else(|e| e.into_inner()),
                    Some(d) => {
                        let now = Instant::now();
                        if now >= d {
                            break;
                        }
                        g = fx.1.wait_timeout(g, d - now).unwrap_or_else(|e| e.into_inner()).0;
                    }
                }
            }
        });
        Ok(0)
    }

    fn path_arg(&self, e: &Expr, v: &[u8]) -> R<PathBuf> {
        Ok(path_of(self.read_str(e.span, v)?))
    }

    fn int_arg(&self, x: &Expr, v: &[u8]) -> i64 {
        self.int_of(x.ty, v).to_big().try_into().unwrap_or(-1)
    }

    /// A file-system service: its result, or an error code.
    fn fs_op(&mut self, e: &Expr, op: SysOp, args: &[Expr], vals: &[super::bytes::Bytes]) -> R<i64> {
        Ok(match op {
            SysOp::FsOpen => {
                let path = self.path_arg(e, &vals[0])?;
                let flags = self.int_arg(&args[1], &vals[1]) as u32;
                let has = |f: u32| flags & f != 0;
                // Like POSIX, an access mode with neither write nor append is read-only; there
                // `create`, `exclusive` and `truncate` are passed to open(2) as they are.
                let write = has(tir::FS_WRITE) || has(tir::FS_APPEND);
                let mut o = std::fs::OpenOptions::new();
                o.read(has(tir::FS_READ) || !write).write(has(tir::FS_WRITE)).append(has(tir::FS_APPEND));
                if write {
                    o.create(has(tir::FS_CREATE) && !has(tir::FS_EXCLUSIVE))
                        .create_new(has(tir::FS_EXCLUSIVE))
                        .truncate(has(tir::FS_TRUNCATE));
                } else {
                    #[cfg(unix)]
                    {
                        use std::os::unix::fs::OpenOptionsExt;
                        let mut fl = 0;
                        if has(tir::FS_CREATE) || has(tir::FS_EXCLUSIVE) {
                            fl |= libc::O_CREAT;
                        }
                        if has(tir::FS_EXCLUSIVE) {
                            fl |= libc::O_EXCL;
                        }
                        if has(tir::FS_TRUNCATE) {
                            fl |= libc::O_TRUNC;
                        }
                        o.custom_flags(fl);
                    }
                }
                let r = o.open(path);
                let sys = &mut self.sys;
                code(r.map(|f| {
                    let h = sys.handle();
                    sys.files.insert(h, f);
                    h
                }))
            }
            SysOp::FsClose => {
                let h = self.int_arg(&args[0], &vals[0]);
                let sys = &mut self.sys;
                code(sys.files.remove(&h).map(|_| 0).ok_or_else(bad_handle))
            }
            SysOp::FsRead => {
                let h = self.int_arg(&args[0], &vals[0]);
                let (ptr, len) = self.slice_parts(&vals[1]);
                let mut buf = vec![0u8; len as usize];
                let r = match self.sys.files.get_mut(&h) {
                    Some(f) => f.read(&mut buf),
                    None => Err(bad_handle()),
                };
                match r {
                    Ok(n) => {
                        self.store(e.span, ptr, &buf[..n], 1)?;
                        n as i64
                    }
                    Err(err) => error_code(&err),
                }
            }
            SysOp::FsWrite => {
                let h = self.int_arg(&args[0], &vals[0]);
                let data = self.read_str(e.span, &vals[1])?;
                code(match self.sys.files.get_mut(&h) {
                    Some(f) => f.write(&data).map(|n| n as i64),
                    None => Err(bad_handle()),
                })
            }
            SysOp::FsSeek => {
                let h = self.int_arg(&args[0], &vals[0]);
                let off = self.int_arg(&args[1], &vals[1]);
                let whence = self.int_arg(&args[2], &vals[2]);
                let from = match whence {
                    0 if off >= 0 => Ok(SeekFrom::Start(off as u64)),
                    1 => Ok(SeekFrom::Current(off)),
                    2 => Ok(SeekFrom::End(off)),
                    _ => Err(std::io::Error::from(std::io::ErrorKind::InvalidInput)),
                };
                code(match (self.sys.files.get_mut(&h), from) {
                    (Some(f), Ok(from)) => f.seek(from).map(|p| p as i64),
                    (None, _) => Err(bad_handle()),
                    (_, Err(err)) => Err(err),
                })
            }
            SysOp::FsStat | SysOp::FsLstat => {
                let path = self.path_arg(e, &vals[0])?;
                let r = if op == SysOp::FsStat { std::fs::metadata(path) } else { std::fs::symlink_metadata(path) };
                match r {
                    Ok(m) => {
                        let mtime = m
                            .modified()
                            .ok()
                            .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
                            .map_or(0, |d| d.as_nanos() as i64);
                        let (ptr, len) = self.slice_parts(&vals[1]);
                        let mut out = vec![];
                        for v in [m.len() as i64, kind_code(m.file_type()), mtime].into_iter().take(len as usize) {
                            out.extend_from_slice(
                                &self.int_bytes(Ty::int(true, 64), &super::num::Int::from_big(v.into())),
                            );
                        }
                        self.store(e.span, ptr, &out, 8)?;
                        0
                    }
                    Err(err) => error_code(&err),
                }
            }
            SysOp::FsRemove => code(std::fs::remove_file(self.path_arg(e, &vals[0])?).map(|_| 0)),
            SysOp::FsRename => {
                let (a, b) = (self.path_arg(e, &vals[0])?, self.path_arg(e, &vals[1])?);
                code(std::fs::rename(a, b).map(|_| 0))
            }
            SysOp::FsMakeDir => code(std::fs::create_dir(self.path_arg(e, &vals[0])?).map(|_| 0)),
            SysOp::FsRemoveDir => code(std::fs::remove_dir(self.path_arg(e, &vals[0])?).map(|_| 0)),
            SysOp::FsOpenDir => {
                let r = std::fs::read_dir(self.path_arg(e, &vals[0])?);
                let sys = &mut self.sys;
                code(r.map(|d| {
                    let h = sys.handle();
                    sys.dirs.insert(h, d);
                    h
                }))
            }
            SysOp::FsNextEntry => {
                let h = self.int_arg(&args[0], &vals[0]);
                let next = match self.sys.dirs.get_mut(&h) {
                    Some(d) => d.next().transpose(),
                    None => Err(bad_handle()),
                };
                match next {
                    Ok(None) => 0,
                    Ok(Some(entry)) => {
                        let name = name_bytes(entry.file_name());
                        let (ptr, len) = self.slice_parts(&vals[1]);
                        if name.len() as u64 > len {
                            io_error_code(IoErrorKind::InvalidInput)
                        } else {
                            match entry.file_type() {
                                Ok(t) => {
                                    self.store(e.span, ptr, &name, 1)?;
                                    name.len() as i64 + (kind_code(t) << 32)
                                }
                                Err(err) => error_code(&err),
                            }
                        }
                    }
                    Err(err) => error_code(&err),
                }
            }
            SysOp::FsCloseDir => {
                let h = self.int_arg(&args[0], &vals[0]);
                code(self.sys.dirs.remove(&h).map(|_| 0).ok_or_else(bad_handle))
            }
            SysOp::FsTruncate => {
                let h = self.int_arg(&args[0], &vals[0]);
                let size = self.int_of(args[1].ty, &vals[1]).to_u64_sat();
                code(match self.sys.files.get(&h) {
                    Some(f) => f.set_len(size).map(|_| 0),
                    None => Err(bad_handle()),
                })
            }
            SysOp::FsSync => {
                let h = self.int_arg(&args[0], &vals[0]);
                code(match self.sys.files.get(&h) {
                    Some(f) => f.sync_all().map(|_| 0),
                    None => Err(bad_handle()),
                })
            }
            SysOp::FsCwd => match std::env::current_dir() {
                Ok(dir) => {
                    let name = name_bytes(dir.into_os_string());
                    let (ptr, len) = self.slice_parts(&vals[0]);
                    if name.len() as u64 > len {
                        io_error_code(IoErrorKind::InvalidInput)
                    } else {
                        self.store(e.span, ptr, &name, 1)?;
                        name.len() as i64
                    }
                }
                Err(err) => error_code(&err),
            },
            _ => unreachable!(),
        })
    }
}

impl<'a> Machine<'a> {
    /// A network service: its result, or an error code.
    fn net_op(&mut self, e: &Expr, op: SysOp, args: &[Expr], vals: &[super::bytes::Bytes]) -> R<i64> {
        use std::net::{TcpListener, TcpStream, ToSocketAddrs};
        let addr = |m: &Self| -> R<std::io::Result<Vec<std::net::SocketAddr>>> {
            let host = String::from_utf8_lossy(&m.read_str(e.span, &vals[0])?).into_owned();
            let port = m.int_arg(&args[1], &vals[1]) as u16;
            Ok((host.as_str(), port).to_socket_addrs().map(|a| a.collect()))
        };
        Ok(match op {
            SysOp::NetConnect => {
                let a = addr(self)?;
                let r = self.without_gil(|| a.and_then(|a| TcpStream::connect(&a[..])));
                let sys = &mut self.sys;
                code(r.map(|s| {
                    let h = sys.handle();
                    sys.streams.insert(h, s.into());
                    h
                }))
            }
            SysOp::NetListen => {
                let r = addr(self)?.and_then(|a| TcpListener::bind(&a[..]));
                let sys = &mut self.sys;
                code(r.map(|l| {
                    let h = sys.handle();
                    sys.listeners.insert(h, l.into());
                    h
                }))
            }
            SysOp::NetLocalPort => {
                let h = self.int_arg(&args[0], &vals[0]);
                let sys = &self.sys;
                let a = match (sys.listeners.get(&h), sys.streams.get(&h)) {
                    (Some(l), _) => l.local_addr(),
                    (_, Some(s)) => s.local_addr(),
                    _ => Err(bad_handle()),
                };
                code(a.map(|a| a.port() as i64))
            }
            SysOp::NetAccept => {
                let h = self.int_arg(&args[0], &vals[0]);
                let r = match self.sys.listeners.get(&h).cloned() {
                    Some(l) => self.without_gil(|| l.accept().map(|(s, _)| s)),
                    None => Err(bad_handle()),
                };
                let sys = &mut self.sys;
                code(r.map(|s| {
                    let h = sys.handle();
                    sys.streams.insert(h, s.into());
                    h
                }))
            }
            SysOp::NetSend => {
                let h = self.int_arg(&args[0], &vals[0]);
                let data = self.read_str(e.span, &vals[1])?;
                match self.sys.streams.get(&h).cloned() {
                    Some(s) => self.without_gil(|| (&*s).write(&data)).map_or_else(|e| net_error(&e), |n| n as i64),
                    None => error_code(&bad_handle()),
                }
            }
            SysOp::NetRecv => {
                let h = self.int_arg(&args[0], &vals[0]);
                let (ptr, len) = self.slice_parts(&vals[1]);
                let mut buf = vec![0u8; len as usize];
                let r = match self.sys.streams.get(&h).cloned() {
                    Some(s) => self.without_gil(|| (&*s).read(&mut buf)),
                    None => Err(bad_handle()),
                };
                match r {
                    Ok(n) => {
                        self.store(e.span, ptr, &buf[..n], 1)?;
                        n as i64
                    }
                    Err(err) => net_error(&err),
                }
            }
            SysOp::NetClose => {
                let h = self.int_arg(&args[0], &vals[0]);
                let sys = &mut self.sys;
                let found = sys.streams.remove(&h).is_some() || sys.listeners.remove(&h).is_some();
                code(if found { Ok(0) } else { Err(bad_handle()) })
            }
            SysOp::NetShutdown => {
                let h = self.int_arg(&args[0], &vals[0]);
                let how = match self.int_arg(&args[1], &vals[1]) {
                    0 => std::net::Shutdown::Read,
                    1 => std::net::Shutdown::Write,
                    _ => std::net::Shutdown::Both,
                };
                code(match self.sys.streams.get(&h) {
                    Some(s) => s.shutdown(how).map(|_| 0),
                    None => Err(bad_handle()),
                })
            }
            SysOp::NetSetTimeouts => {
                let h = self.int_arg(&args[0], &vals[0]);
                let timeout = |ns: u64| (ns > 0).then(|| std::time::Duration::from_nanos(ns));
                let recv = timeout(self.int_of(args[1].ty, &vals[1]).to_u64_sat());
                let send = timeout(self.int_of(args[2].ty, &vals[2]).to_u64_sat());
                code(match self.sys.streams.get(&h) {
                    Some(s) => s.set_read_timeout(recv).and_then(|_| s.set_write_timeout(send)).map(|_| 0),
                    None => Err(bad_handle()),
                })
            }
            SysOp::NetPeer => {
                let h = self.int_arg(&args[0], &vals[0]);
                match self.sys.streams.get(&h).map(|s| s.peer_addr()) {
                    Some(Ok(a)) => {
                        let name = a.to_string().into_bytes();
                        let (ptr, len) = self.slice_parts(&vals[1]);
                        if name.len() as u64 > len {
                            io_error_code(IoErrorKind::InvalidInput)
                        } else {
                            self.store(e.span, ptr, &name, 1)?;
                            name.len() as i64
                        }
                    }
                    Some(Err(err)) => error_code(&err),
                    None => error_code(&bad_handle()),
                }
            }
            _ => unreachable!(),
        })
    }
}
