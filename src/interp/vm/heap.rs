//! `new` and `destroy` while the default heap allocator is current.
//!
//! The core library's `new` performs `Alloc.current`, calls the allocator's `alloc`
//! through its vtable, records the allocation's owner and site (Debug builds) and stores
//! the initial value; `destroy` performs `Alloc.current` again, checks the owner
//! and frees through the vtable. With the default allocator every one of those steps is
//! a fixed piece of the machine, so the VM does them in one instruction instead of a
//! dozen calls and builtins. The fast path is taken only when it cannot be told apart
//! from the library code: the handler for `Alloc` is the declaration for `Allocator`
//! installed with the value `heap_allocator()` returns, nothing that the library code
//! would do can trap, and no limit that the calls would be charged against is near.
//! Otherwise the instruction makes the ordinary call.

use super::*;
use crate::interp::memory::AllocKind;
use crate::interp::{Mode, calls::MAX_FRAME};

/// What the VM knows about a call of `new` or `destroy`.
#[derive(Clone, Copy, Debug)]
pub struct HeapInfo {
    pub which: HeapFn,
    /// The size and alignment of the allocated type.
    pub size: u64,
    pub align: u64,
}

/// Whether a function is known to be `new` or `destroy` of a type the fast path handles.
#[derive(Clone, Copy, Default)]
pub enum HeapSlot {
    #[default]
    Unknown,
    No,
    Yes(HeapInfo),
}

/// The identity of the default heap allocator, found the first time it is needed.
#[derive(Default)]
pub struct HeapCx {
    state: HeapState,
}

#[derive(Default)]
enum HeapState {
    #[default]
    Unready,
    Off,
    Ready {
        alloc: EffectRef,
        /// The type `Allocator`.
        ty: Ty,
        /// The bytes of `heap_allocator()`.
        value: [u8; 16],
    },
}

/// Words of the registers at `r` as bytes.
fn word(regs: &[u64], r: Reg) -> u64 {
    regs[r as usize]
}

impl<'a> Machine<'a> {
    /// Finds out what the default heap allocator looks like; false if this program cannot use
    /// the fast path.
    #[cold]
    #[inline(never)]
    fn heap_init(&mut self) -> bool {
        let prog = self.prog;
        let state = (|| {
            if self.target.ptr_bytes != 8 || !self.little() {
                return HeapState::Off;
            }
            let (Some(eff), Some(h)) = (prog.ambient.alloc, prog.heap_allocator) else { return HeapState::Off };
            let ty = prog.fns[h.index()].ret;
            match self.call_function(Span::DUMMY, h, vec![], None, Ctx::empty(), None, false) {
                Ok(Value::Bytes(b)) if b.len() == 16 => {
                    let mut value = [0u8; 16];
                    value.copy_from_slice(&b);
                    HeapState::Ready { alloc: EffectRef { effect: eff, args: Box::new([]) }, ty, value }
                }
                _ => HeapState::Off,
            }
        })();
        let on = matches!(state, HeapState::Ready { .. });
        self.heap_cx.state = state;
        on
    }

    /// Runs the call of `new` or `destroy` (`f`, with operands `list` and result `out`)
    /// directly if that is exactly what it would do; false if the caller must make the call.
    pub(super) fn vm_heap_call(&mut self, regs: &mut [u64], cx: &Ctx, f: FnId, list: &[Arg], out: Arg) -> bool {
        // The checks a call makes, and the stack the calls it replaces would be charged.
        let marker = 0u8;
        if self.budget.is_some()
            || self.fiber_stack != u64::MAX
            || self.threads.gil.is_some()
            || self.depth + 16 >= MAX_DEPTH
            || (&marker as *const u8 as usize) < self.stack_floor.saturating_add(64 * 1024)
        {
            return false;
        }
        let info = match self.heap_info[f.index()] {
            HeapSlot::Yes(i) => i,
            HeapSlot::No => return false,
            HeapSlot::Unknown => match heap_info(self, f) {
                Some(i) => i,
                None => return false,
            },
        };
        if matches!(self.heap_cx.state, HeapState::Unready) && !self.heap_init() {
            return false;
        }
        let HeapState::Ready { alloc, ty, value } = &self.heap_cx.state else { return false };
        // The innermost handler for `Alloc` must be the default heap allocator.
        let Some(node) = cx.find(alloc) else { return false };
        let Installed::Decl { decl, value: v, .. } = &node.installed else { return false };
        if v[..] != value[..] || self.prog.handler_decls[decl.index()].for_ty != *ty {
            return false;
        }
        let owner = u64::from_le_bytes(value[..8].try_into().unwrap());
        let vt = u64::from_le_bytes(value[8..].try_into().unwrap());
        let debug = self.mode == Mode::Debug;
        match info.which {
            HeapFn::New => {
                let [Arg::Data { reg: init, size: isz }, Arg::Data { reg: site, size: ssz }] = *list else {
                    return false;
                };
                let Arg::Data { reg: dst, size: 16 } = out else { return false };
                if isz as u64 != info.size || ssz < 24 {
                    return false;
                }
                // The words that spell out a `SourceLocation`: file (pointer, length), line, column.
                let (fp, fl, lc) = (word(regs, site), word(regs, site + 1), word(regs, site + 2));
                let n = info.size as usize;
                let addr = match self.mem.alloc(info.size, info.align.max(1), AllocKind::Heap, true) {
                    Ok(a) => a,
                    Err(_) => return false,
                };
                let stored = if n <= 8 {
                    n == 0 || self.mem.try_write_word(addr, regs[init as usize], n, info.align)
                } else {
                    let mut buf = [0u8; 64];
                    let owned;
                    let bytes: &[u8] = if n <= buf.len() {
                        for i in 0..n.div_ceil(8) {
                            buf[i * 8..i * 8 + 8].copy_from_slice(&regs[init as usize + i].to_le_bytes());
                        }
                        &buf[..n]
                    } else {
                        let mut v = Vec::with_capacity(n.div_ceil(8) * 8);
                        for i in 0..n.div_ceil(8) {
                            v.extend_from_slice(&regs[init as usize + i].to_le_bytes());
                        }
                        v.truncate(n);
                        owned = v;
                        &owned[..]
                    };
                    self.mem.write(addr, bytes, info.align).is_ok()
                };
                if !stored {
                    // Cannot happen for a fresh block; let the library code report whatever it is.
                    self.mem.kill(addr);
                    return false;
                }
                if debug {
                    let mut file = [0u8; 16];
                    file[..8].copy_from_slice(&fp.to_le_bytes());
                    file[8..].copy_from_slice(&fl.to_le_bytes());
                    let site = (Bytes::from_slice(&file), lc & 0xffff_ffff, lc >> 32);
                    self.alloc_tags.insert(addr, (owner, vt, site));
                }
                regs[dst as usize] = 0;
                regs[dst as usize + 1] = addr;
                true
            }
            HeapFn::Destroy => {
                let [Arg::Data { reg: p, size: 8 }, Arg::Data { .. }] = *list else { return false };
                let ptr = regs[p as usize];
                if debug
                    && let Some((o, t, _)) = self.alloc_tags.get(&ptr)
                    && (*o, *t) != (owner, vt)
                {
                    return false;
                }
                match self.mem.describe(ptr) {
                    Some((base, size, AllocKind::Heap, true)) if base == ptr && size == info.size => {}
                    _ => return false,
                }
                if debug {
                    self.alloc_tags.remove(&ptr);
                }
                self.mem.kill(ptr);
                true
            }
        }
    }
}

/// What a call of `f` needs to take the fast path, if `f` is `new` or `destroy` of the core library.
pub fn heap_info(m: &mut Machine, f: FnId) -> Option<HeapInfo> {
    let info = heap_info_of(m, f);
    m.heap_info[f.index()] = match info {
        Some(i) => HeapSlot::Yes(i),
        None => HeapSlot::No,
    };
    info
}

fn heap_info_of(m: &Machine, f: FnId) -> Option<HeapInfo> {
    let which = *m.prog.heap_fns.get(&f)?;
    let def = &m.prog.fns[f.index()];
    let p0 = *def.params.first()?;
    let ty = def.locals[p0.index()].ty;
    let target = match which {
        HeapFn::New => ty,
        HeapFn::Destroy => match ty.kind() {
            TyKind::Ptr { pointee, .. } => *pointee,
            _ => return None,
        },
    };
    // Values too big for registers are left to the library code.
    let ti = m.ti(target);
    let (size, align) = (ti.size as u64, ti.align);
    if size > MAX_FRAME / 4 {
        return None;
    }
    Some(HeapInfo { which, size, align })
}
