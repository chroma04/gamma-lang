//! The system services behind the core library's default handlers (§12.9):
//! standard input, clocks and random bytes. Compiled programs get the same
//! services from the C runtime.

use super::machine::*;
use super::value::*;
use crate::tir::{Expr, IoErrorKind, SysOp, io_error_code};
use crate::types::Ty;
use std::io::{Read, Write};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

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
        _ => IoErrorKind::Other(e.raw_os_error().unwrap_or(0)),
    };
    io_error_code(kind)
}

/// The start of the monotonic clock: the first time it is read.
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
                let n = loop {
                    match std::io::stdin().lock().read(&mut buf) {
                        Ok(n) => break n,
                        Err(err) if err.kind() == std::io::ErrorKind::Interrupted => {}
                        Err(err) => return Ok(int(self, e.ty, error_code(&err) as i128)),
                    }
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
                std::thread::sleep(std::time::Duration::from_nanos(ns));
                Ok(Value::unit())
            }
            SysOp::RandFill => {
                let (ptr, len) = self.slice_parts(&vals[0]);
                let mut buf = vec![0u8; len as usize];
                if let Err(err) = std::fs::File::open("/dev/urandom").and_then(|mut f| f.read_exact(&mut buf)) {
                    return self.trap(e.span, format!("cannot read random bytes: {}", err));
                }
                self.store(e.span, ptr, &buf, 1)?;
                Ok(Value::unit())
            }
        }
    }
}
