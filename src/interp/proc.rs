//! Child processes (the `Proc` effect's default handler, §12.9): the services behind the
//! `@proc_*` operations of [`SysOp`]. Compiled programs get the same services from the C
//! runtime.

use super::R;
use super::machine::*;
use super::sys::error_code;
use crate::source::Span;
use crate::tir::{PROC_ENV, SysOp};
use crate::types::Ty;
use std::io::{Read, Write};
use std::process::{Child, ChildStderr, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::{Arc, Mutex};

/// A started child process. Its parts are separate so that one thread can kill the child
/// while another waits for it or reads its output.
pub struct ProcEntry {
    #[cfg(unix)]
    pid: u32,
    #[cfg(windows)]
    handle: usize,
    child: Mutex<Option<Child>>,
    stdin: Mutex<Option<Arc<ChildStdin>>>,
    outs: Mutex<Outs>,
}

#[derive(Default)]
struct Outs {
    stdout: Option<ChildStdout>,
    stderr: Option<ChildStderr>,
}

/// The error code of a failure that carries only an `errno`.
fn errno_code(errno: i32) -> i64 {
    error_code(&std::io::Error::from_raw_os_error(errno))
}

fn invalid() -> i64 {
    error_code(&std::io::Error::from(std::io::ErrorKind::InvalidInput))
}

fn stdio_of(mode: u32) -> Stdio {
    match mode {
        1 => Stdio::null(),
        2 => Stdio::piped(),
        _ => Stdio::inherit(),
    }
}

#[cfg(unix)]
fn os_str(bytes: &[u8]) -> std::ffi::OsString {
    use std::os::unix::ffi::OsStringExt;
    std::ffi::OsString::from_vec(bytes.to_vec())
}

#[cfg(not(unix))]
fn os_str(bytes: &[u8]) -> std::ffi::OsString {
    String::from_utf8_lossy(bytes).into_owned().into()
}

/// The file that `program` names, as `execvp` finds it: a name with a `/` is used as it
/// is; any other is searched for in the directories of `PATH` (an empty entry is the
/// current directory) and must be an executable file.
#[cfg(unix)]
fn resolve_program(program: &[u8]) -> std::io::Result<std::ffi::OsString> {
    use std::os::unix::ffi::{OsStrExt, OsStringExt};
    use std::os::unix::fs::PermissionsExt;
    if program.contains(&b'/') {
        return Ok(os_str(program));
    }
    if program.is_empty() {
        return Err(std::io::ErrorKind::NotFound.into());
    }
    let path = std::env::var_os("PATH").unwrap_or_else(|| "/usr/bin:/bin".into());
    let mut denied = false;
    for dir in path.as_bytes().split(|&b| b == b':') {
        let mut file = if dir.is_empty() { b".".to_vec() } else { dir.to_vec() };
        file.push(b'/');
        file.extend_from_slice(program);
        let file = std::ffi::OsString::from_vec(file);
        if let Ok(meta) = std::fs::metadata(&file)
            && meta.is_file()
        {
            if meta.permissions().mode() & 0o111 != 0 {
                return Ok(file);
            }
            denied = true;
        }
    }
    Err(if denied { std::io::ErrorKind::PermissionDenied } else { std::io::ErrorKind::NotFound }.into())
}

#[cfg(not(unix))]
fn resolve_program(program: &[u8]) -> std::io::Result<std::ffi::OsString> {
    Ok(os_str(program))
}

/// Starts a child. `argv` is not empty.
fn spawn(argv: &[Vec<u8>], env: &[Vec<u8>], dir: &[u8], flags: u32) -> Result<ProcEntry, i64> {
    if argv.iter().chain(env).any(|s| s.contains(&0)) || dir.contains(&0) || env.iter().any(|e| !e.contains(&b'=')) {
        return Err(invalid());
    }
    let program = resolve_program(&argv[0]).map_err(|e| error_code(&e))?;
    let mut cmd = Command::new(program);
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        cmd.arg0(os_str(&argv[0]));
    }
    cmd.args(argv[1..].iter().map(|a| os_str(a)));
    if !dir.is_empty() {
        cmd.current_dir(os_str(dir));
    }
    if flags & PROC_ENV != 0 {
        cmd.env_clear();
        for entry in env {
            // The name ends at the first `=` after its first character (Windows has names
            // such as `=C:`).
            let eq = entry.iter().skip(1).position(|&b| b == b'=').map_or(entry.len(), |i| i + 1);
            let (name, value) = entry.split_at(eq);
            cmd.env(os_str(name), os_str(value.get(1..).unwrap_or(&[])));
        }
    }
    cmd.stdin(stdio_of(flags & 3)).stdout(stdio_of((flags >> 2) & 3)).stderr(stdio_of((flags >> 4) & 3));
    let mut child = cmd.spawn().map_err(|e| error_code(&e))?;
    let stdin = child.stdin.take().map(Arc::new);
    #[cfg(target_vendor = "apple")]
    if let Some(pipe) = &stdin {
        use std::os::fd::AsRawFd;
        // `F_SETNOSIGPIPE` from Apple's <sys/fcntl.h>, which the `libc` crate does not export.
        const F_SETNOSIGPIPE: libc::c_int = 73;
        // SAFETY: a valid descriptor; writes to it then fail with `EPIPE` instead of raising `SIGPIPE`.
        unsafe { libc::fcntl(pipe.as_raw_fd(), F_SETNOSIGPIPE, 1) };
    }
    let outs = Outs { stdout: child.stdout.take(), stderr: child.stderr.take() };
    #[cfg(windows)]
    let handle = {
        use std::os::windows::io::AsRawHandle;
        child.as_raw_handle() as usize
    };
    Ok(ProcEntry {
        #[cfg(unix)]
        pid: child.id(),
        #[cfg(windows)]
        handle,
        child: Mutex::new(Some(child)),
        stdin: Mutex::new(stdin),
        outs: Mutex::new(outs),
    })
}

/// Writes to the child's stdin. The command-line program has the default action for
/// `SIGPIPE`, so a write to a pipe that nobody reads would end the interpreter: the signal
/// is blocked for the thread meanwhile and, if the write raised it, taken off again.
#[cfg(all(unix, not(target_vendor = "apple")))]
fn write_pipe(pipe: &ChildStdin, data: &[u8]) -> std::io::Result<usize> {
    use std::os::fd::AsRawFd;
    let data = &data[..data.len().min(1 << 30)];
    // SAFETY: plain system calls on a valid descriptor and on local signal sets.
    unsafe {
        let mut block: libc::sigset_t = std::mem::zeroed();
        let mut old: libc::sigset_t = std::mem::zeroed();
        let mut pending: libc::sigset_t = std::mem::zeroed();
        libc::sigemptyset(&mut block);
        libc::sigaddset(&mut block, libc::SIGPIPE);
        libc::pthread_sigmask(libc::SIG_BLOCK, &block, &mut old);
        libc::sigpending(&mut pending);
        let was_pending = libc::sigismember(&pending, libc::SIGPIPE) == 1;
        let r = libc::write(pipe.as_raw_fd(), data.as_ptr().cast(), data.len());
        let err = std::io::Error::last_os_error();
        if r < 0 && err.raw_os_error() == Some(libc::EPIPE) && !was_pending {
            let zero = libc::timespec { tv_sec: 0, tv_nsec: 0 };
            while libc::sigtimedwait(&block, std::ptr::null_mut(), &zero) < 0
                && std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted
            {}
        }
        libc::pthread_sigmask(libc::SIG_SETMASK, &old, std::ptr::null_mut());
        if r < 0 { Err(err) } else { Ok(r as usize) }
    }
}

#[cfg(any(not(unix), target_vendor = "apple"))]
fn write_pipe(pipe: &ChildStdin, data: &[u8]) -> std::io::Result<usize> {
    (&*pipe).write(data)
}

fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

impl ProcEntry {
    fn write_in(&self, data: &[u8]) -> i64 {
        let Some(pipe) = lock(&self.stdin).clone() else { return invalid() };
        loop {
            match write_pipe(&pipe, data) {
                Ok(n) => return n as i64,
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => {}
                Err(e) => return error_code(&e),
            }
        }
    }

    fn close_in(&self) {
        lock(&self.stdin).take();
    }

    /// Reads from whichever piped output stream has data, closing those that ended:
    /// the length plus 2^32 for stderr, or 0 when no stream is left.
    fn read_out(&self, buf: &mut [u8]) -> i64 {
        let cap = buf.len().min(u32::MAX as usize);
        let buf = &mut buf[..cap];
        let mut outs = lock(&self.outs);
        loop {
            if outs.stdout.is_none() && outs.stderr.is_none() {
                return 0;
            }
            if buf.is_empty() {
                return invalid();
            }
            let ready = match wait_readable(outs.stdout.as_ref(), outs.stderr.as_ref()) {
                Ok(r) => r,
                Err(e) => return error_code(&e),
            };
            // When both are ready, stdout goes first.
            let (stream, r) = if ready.0 && outs.stdout.is_some() {
                (0, outs.stdout.as_mut().map(|s| s.read(buf)))
            } else {
                (1, outs.stderr.as_mut().map(|s| s.read(buf)))
            };
            match r {
                Some(Ok(0)) => {
                    if stream == 0 {
                        outs.stdout = None;
                    } else {
                        outs.stderr = None;
                    }
                }
                Some(Ok(n)) => return n as i64 + (stream << 32),
                Some(Err(e)) if e.kind() == std::io::ErrorKind::Interrupted => {}
                // A pipe whose writer has gone reads as the end on Windows.
                Some(Err(e)) if e.kind() == std::io::ErrorKind::BrokenPipe => {
                    if stream == 0 {
                        outs.stdout = None;
                    } else {
                        outs.stderr = None;
                    }
                }
                Some(Err(e)) => return error_code(&e),
                None => {}
            }
        }
    }

    fn wait(&self) -> i64 {
        self.close_in();
        *lock(&self.outs) = Outs::default();
        let Some(mut child) = lock(&self.child).take() else { return invalid() };
        match child.wait() {
            Ok(status) => status_code(&status),
            Err(e) => error_code(&e),
        }
    }

    fn kill(&self) -> i64 {
        #[cfg(unix)]
        {
            // SAFETY: plain system call; the process has not been waited for.
            if unsafe { libc::kill(self.pid as libc::pid_t, libc::SIGKILL) } != 0 {
                return error_code(&std::io::Error::last_os_error());
            }
            0
        }
        #[cfg(windows)]
        {
            #[link(name = "kernel32")]
            unsafe extern "system" {
                fn TerminateProcess(h: *mut std::ffi::c_void, code: u32) -> i32;
            }
            // SAFETY: the handle stays open until the child is waited for.
            if unsafe { TerminateProcess(self.handle as *mut std::ffi::c_void, 1) } == 0 {
                return error_code(&std::io::Error::last_os_error());
            }
            0
        }
        #[cfg(not(any(unix, windows)))]
        {
            invalid()
        }
    }
}

/// The exit code, or the signal number plus 2^32.
fn status_code(status: &std::process::ExitStatus) -> i64 {
    #[cfg(unix)]
    {
        use std::os::unix::process::ExitStatusExt;
        if let Some(sig) = status.signal() {
            return sig as i64 + (1 << 32);
        }
    }
    status.code().map_or(0, |c| c as u32 as i64)
}

/// Waits until a stream has data or has ended; the flags say which of the two (stdout,
/// stderr) is ready.
#[cfg(unix)]
fn wait_readable(out: Option<&ChildStdout>, err: Option<&ChildStderr>) -> std::io::Result<(bool, bool)> {
    use std::os::fd::AsRawFd;
    let mut fds = [
        libc::pollfd { fd: out.map_or(-1, |s| s.as_raw_fd()), events: libc::POLLIN, revents: 0 },
        libc::pollfd { fd: err.map_or(-1, |s| s.as_raw_fd()), events: libc::POLLIN, revents: 0 },
    ];
    loop {
        // SAFETY: `fds` is a valid array of two entries; negative descriptors are ignored.
        let n = unsafe { libc::poll(fds.as_mut_ptr(), 2, -1) };
        if n >= 0 {
            return Ok((fds[0].revents != 0, fds[1].revents != 0));
        }
        let e = std::io::Error::last_os_error();
        if e.kind() != std::io::ErrorKind::Interrupted {
            return Err(e);
        }
    }
}

#[cfg(windows)]
fn wait_readable(out: Option<&ChildStdout>, err: Option<&ChildStderr>) -> std::io::Result<(bool, bool)> {
    use std::os::windows::io::AsRawHandle;
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn PeekNamedPipe(
            h: *mut std::ffi::c_void,
            buf: *mut u8,
            size: u32,
            read: *mut u32,
            avail: *mut u32,
            left: *mut u32,
        ) -> i32;
    }
    // Data is ready, or the pipe is broken (its writer has gone), which reads as the end.
    let ready = |h: Option<*mut std::ffi::c_void>| -> bool {
        let Some(h) = h else { return false };
        let mut avail = 0u32;
        // SAFETY: `h` is an open pipe handle; the buffer is not used (size 0).
        let ok = unsafe {
            PeekNamedPipe(h, std::ptr::null_mut(), 0, std::ptr::null_mut(), &mut avail, std::ptr::null_mut())
        };
        ok == 0 || avail > 0
    };
    loop {
        let (a, b) = (ready(out.map(|s| s.as_raw_handle())), ready(err.map(|s| s.as_raw_handle())));
        if a || b {
            return Ok((a, b));
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}

/// No child processes run on other hosts (spawning fails), so there is nothing to wait for.
#[cfg(not(any(unix, windows)))]
fn wait_readable(_out: Option<&ChildStdout>, _err: Option<&ChildStderr>) -> std::io::Result<(bool, bool)> {
    Err(std::io::ErrorKind::Unsupported.into())
}

impl<'a> Machine<'a> {
    /// The strings of a `[]const string` value.
    fn string_list(&self, span: Span, v: &[u8]) -> R<Vec<Vec<u8>>> {
        let p = self.ptr_size();
        let items = self.slice_bytes(span, v, 2 * p)?;
        let mut out = vec![];
        for item in items.chunks(2 * p) {
            out.push(self.read_str(span, item)?);
        }
        Ok(out)
    }

    fn proc_entry(&self, h: i64) -> Option<Arc<ProcEntry>> {
        self.sys.procs.get(&h).cloned()
    }

    /// A child-process service: its result, or an error code.
    pub(super) fn proc_op(&mut self, span: Span, op: SysOp, tys: &[Ty], vals: &[super::bytes::Bytes]) -> R<i64> {
        let bad = || errno_code(9); // EBADF
        Ok(match op {
            SysOp::ProcSpawn => {
                let argv = self.string_list(span, &vals[0])?;
                let env = self.string_list(span, &vals[1])?;
                let dir = self.read_str(span, &vals[2])?;
                let flags = self.int_of(tys[3], &vals[3]).to_u64_sat() as u32;
                if argv.is_empty() {
                    return Ok(invalid());
                }
                // What we have printed comes before anything the child prints.
                let _ = self.out.as_mut().map(|w| w.flush());
                let _ = self.errw.as_mut().map(|w| w.flush());
                match spawn(&argv, &env, &dir, flags) {
                    Ok(entry) => {
                        let h = self.sys.handle();
                        self.sys.procs.insert(h, Arc::new(entry));
                        h
                    }
                    Err(code) => code,
                }
            }
            SysOp::ProcWriteIn => {
                let h = self.int_arg(tys[0], &vals[0]);
                let data = self.read_str(span, &vals[1])?;
                match self.proc_entry(h) {
                    Some(p) => self.without_gil(|| p.write_in(&data)),
                    None => bad(),
                }
            }
            SysOp::ProcCloseIn => {
                let h = self.int_arg(tys[0], &vals[0]);
                match self.proc_entry(h) {
                    Some(p) => {
                        p.close_in();
                        0
                    }
                    None => bad(),
                }
            }
            SysOp::ProcReadOut => {
                let h = self.int_arg(tys[0], &vals[0]);
                let (ptr, len) = self.slice_parts(&vals[1]);
                let mut buf = self.dest_buf(span, ptr, len)?;
                let Some(p) = self.proc_entry(h) else { return Ok(bad()) };
                let n = self.without_gil(|| p.read_out(&mut buf));
                if n > 0 {
                    let got = (n & 0xffff_ffff) as usize;
                    self.store(span, ptr, &buf[..got], 1)?;
                }
                n
            }
            SysOp::ProcWait => {
                let h = self.int_arg(tys[0], &vals[0]);
                let Some(p) = self.proc_entry(h) else { return Ok(bad()) };
                let n = self.without_gil(|| p.wait());
                self.sys.procs.remove(&h);
                n
            }
            SysOp::ProcKill => {
                let h = self.int_arg(tys[0], &vals[0]);
                match self.proc_entry(h) {
                    Some(p) => p.kill(),
                    None => bad(),
                }
            }
            _ => unreachable!(),
        })
    }
}
