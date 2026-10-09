//! The interpreter's flat virtual address space.
//!
//! Every allocation receives a fresh, never reused address range from a bump
//! counter. Allocations are kept sorted by base address, so an address is
//! resolved with a binary search. A freed allocation leaves a tombstone so that
//! use after free is reported precisely; very old tombstones are eventually
//! dropped, after which stale pointers are reported as unmapped addresses.

use std::cell::Cell;

/// Gap left between consecutive allocations so one-past-the-end addresses and
/// small overruns never land inside another allocation.
const GAP: u64 = 16;
const MIN_ALIGN: u64 = 16;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AllocKind {
    Stack,
    Temp,
    Heap,
    /// Static bytes such as a string literal; the index is into [`Memory::statics`].
    Static(u32),
    Const(u32),
    Global(u32),
}

struct Alloc {
    base: u64,
    size: u64,
    bytes: Vec<u8>,
    live: bool,
    mutable: bool,
    kind: AllocKind,
}

#[derive(Debug, Clone, PartialEq)]
pub enum MemError {
    Null,
    Unmapped {
        addr: u64,
        len: u64,
    },
    Dead {
        addr: u64,
        kind: AllocKind,
    },
    OutOfBounds {
        addr: u64,
        len: u64,
        base: u64,
        size: u64,
    },
    Misaligned {
        addr: u64,
        align: u64,
    },
    ReadOnly {
        addr: u64,
        kind: AllocKind,
    },
    Exhausted,
    /// The host could not provide memory for an object of `size` bytes.
    TooLarge {
        size: u64,
        kind: AllocKind,
    },
}

impl MemError {
    pub fn message(&self) -> String {
        match self {
            MemError::Null => "invalid memory access: null pointer dereference".into(),
            MemError::Unmapped { addr, len } => {
                format!("invalid memory access: {} byte(s) at address {:#x} are not inside any allocation", len, addr)
            }
            MemError::Dead { addr, kind } => {
                let what = match kind {
                    AllocKind::Stack | AllocKind::Temp => "a local variable whose lifetime has ended",
                    AllocKind::Heap => "freed memory",
                    _ => "a dead allocation",
                };
                format!("invalid memory access: address {:#x} points into {}", addr, what)
            }
            MemError::OutOfBounds { addr, len, base, size } => format!(
                "invalid memory access: {} byte(s) at address {:#x} are out of bounds of the allocation at {:#x} ({} bytes)",
                len, addr, base, size
            ),
            MemError::Misaligned { addr, align } => {
                format!("invalid memory access: address {:#x} is not aligned to {} bytes", addr, align)
            }
            MemError::ReadOnly { addr, .. } => {
                format!("invalid memory access: write to read-only memory at address {:#x}", addr)
            }
            MemError::Exhausted => "address space exhausted".into(),
            MemError::TooLarge { size, kind: AllocKind::Stack | AllocKind::Temp } => {
                format!("stack overflow: a local of {} bytes does not fit", size)
            }
            MemError::TooLarge { size, .. } => format!("out of memory allocating {} bytes", size),
        }
    }
}

pub struct Memory {
    allocs: Vec<Alloc>,
    next: u64,
    limit: u64,
    live_count: usize,
    last: Cell<usize>,
    /// Contents of static allocations referenced by [`AllocKind::Static`].
    pub statics: Vec<std::rc::Rc<[u8]>>,
}

impl Memory {
    pub fn new(data_base: u64, ptr_bytes: u64) -> Memory {
        let limit = if ptr_bytes >= 8 { u64::MAX } else { 1u64 << (ptr_bytes * 8) };
        Memory { allocs: Vec::new(), next: data_base, limit, live_count: 0, last: Cell::new(0), statics: Vec::new() }
    }

    pub fn alloc(&mut self, size: u64, align: u64, kind: AllocKind, mutable: bool) -> Result<u64, MemError> {
        let a = align.max(MIN_ALIGN);
        let base = self.next.div_ceil(a) * a;
        let end = base.checked_add(size.max(1)).ok_or(MemError::Exhausted)?;
        let next = end.checked_add(GAP).ok_or(MemError::Exhausted)?;
        if next >= self.limit {
            return Err(MemError::Exhausted);
        }
        let mut bytes = Vec::new();
        if bytes.try_reserve_exact(size as usize).is_err() {
            return Err(MemError::TooLarge { size, kind });
        }
        bytes.resize(size as usize, 0);
        self.next = next;
        self.allocs.push(Alloc { base, size, bytes, live: true, mutable, kind });
        self.live_count += 1;
        Ok(base)
    }

    fn find(&self, addr: u64, len: u64) -> Result<usize, MemError> {
        if addr == 0 {
            return Err(MemError::Null);
        }
        let hint = self.last.get();
        if let Some(a) = self.allocs.get(hint)
            && addr >= a.base
            && addr - a.base < a.size.max(1)
        {
            return Ok(hint);
        }
        let idx = self.allocs.partition_point(|a| a.base <= addr);
        if idx == 0 {
            return Err(MemError::Unmapped { addr, len });
        }
        let i = idx - 1;
        let a = &self.allocs[i];
        // One-past-the-end of an allocation (for zero-length accesses) is also accepted.
        if addr - a.base > a.size {
            return Err(MemError::Unmapped { addr, len });
        }
        self.last.set(i);
        Ok(i)
    }

    fn check(&self, addr: u64, len: u64) -> Result<usize, MemError> {
        let i = self.find(addr, len)?;
        let a = &self.allocs[i];
        if !a.live {
            return Err(MemError::Dead { addr, kind: a.kind });
        }
        let off = addr - a.base;
        if off.checked_add(len).is_none_or(|e| e > a.size) {
            return Err(MemError::OutOfBounds { addr, len, base: a.base, size: a.size });
        }
        Ok(i)
    }

    pub fn read(&self, addr: u64, len: u64, align: u64) -> Result<&[u8], MemError> {
        if len == 0 && align <= 1 {
            return Ok(&[]);
        }
        if align > 1 && !addr.is_multiple_of(align) {
            return Err(MemError::Misaligned { addr, align });
        }
        let i = self.check(addr, len)?;
        let a = &self.allocs[i];
        let off = (addr - a.base) as usize;
        Ok(&a.bytes[off..off + len as usize])
    }

    pub fn write(&mut self, addr: u64, data: &[u8], align: u64) -> Result<(), MemError> {
        if data.is_empty() && align <= 1 {
            return Ok(());
        }
        if align > 1 && !addr.is_multiple_of(align) {
            return Err(MemError::Misaligned { addr, align });
        }
        let i = self.check(addr, data.len() as u64)?;
        let a = &mut self.allocs[i];
        if !a.mutable {
            return Err(MemError::ReadOnly { addr, kind: a.kind });
        }
        let off = (addr - a.base) as usize;
        a.bytes[off..off + data.len()].copy_from_slice(data);
        Ok(())
    }

    /// Writes without the read-only check (used to initialize constants and statics).
    pub fn init(&mut self, addr: u64, data: &[u8]) -> Result<(), MemError> {
        let i = self.check(addr, data.len() as u64)?;
        let a = &mut self.allocs[i];
        let off = (addr - a.base) as usize;
        a.bytes[off..off + data.len()].copy_from_slice(data);
        Ok(())
    }

    /// Marks the allocation at `base` dead. Returns false if there is none (or it is already dead).
    pub fn kill(&mut self, base: u64) -> bool {
        let idx = self.allocs.partition_point(|a| a.base < base);
        match self.allocs.get_mut(idx) {
            Some(a) if a.base == base && a.live => {
                a.live = false;
                a.bytes = Vec::new();
                self.live_count -= 1;
                if self.allocs.len() > (1 << 20) && self.allocs.len() > 4 * self.live_count {
                    self.compact();
                }
                true
            }
            _ => false,
        }
    }

    fn compact(&mut self) {
        let keep_dead_from = self.allocs.len().saturating_sub(1 << 16);
        let mut i = 0;
        self.allocs.retain(|a| {
            let keep = a.live || i >= keep_dead_from;
            i += 1;
            keep
        });
        self.last.set(0);
    }

    /// The allocation containing `addr` (a one-past-the-end address belongs to the allocation it ends).
    pub fn describe(&self, addr: u64) -> Option<(u64, u64, AllocKind, bool)> {
        let i = self.find(addr, 0).ok()?;
        let a = &self.allocs[i];
        Some((a.base, a.size, a.kind, a.live))
    }

    pub fn is_live(&self, base: u64) -> bool {
        let idx = self.allocs.partition_point(|a| a.base < base);
        matches!(self.allocs.get(idx), Some(a) if a.base == base && a.live)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alloc_read_write() {
        let mut m = Memory::new(0x10000, 8);
        let a = m.alloc(8, 8, AllocKind::Stack, true).unwrap();
        let b = m.alloc(4, 4, AllocKind::Stack, true).unwrap();
        assert!(b >= a + 8 + 16);
        m.write(a, &[1, 2, 3, 4], 1).unwrap();
        assert_eq!(m.read(a, 4, 1).unwrap(), &[1, 2, 3, 4]);
        assert!(matches!(m.read(a, 9, 1), Err(MemError::OutOfBounds { .. })));
        assert!(matches!(m.read(a + 8, 1, 1), Err(MemError::OutOfBounds { .. })));
        assert!(matches!(m.read(a + 9, 1, 1), Err(MemError::Unmapped { .. })));
        assert!(matches!(m.read(0, 1, 1), Err(MemError::Null)));
        assert!(matches!(m.read(a + 1, 2, 2), Err(MemError::Misaligned { .. })));
        assert!(m.kill(a));
        assert!(matches!(m.read(a, 1, 1), Err(MemError::Dead { .. })));
    }

    #[test]
    fn read_only() {
        let mut m = Memory::new(0x10000, 8);
        let a = m.alloc(4, 1, AllocKind::Static(0), false).unwrap();
        m.init(a, &[1, 2, 3, 4]).unwrap();
        assert_eq!(m.read(a, 4, 1).unwrap(), &[1, 2, 3, 4]);
        assert!(matches!(m.write(a, &[0], 1), Err(MemError::ReadOnly { .. })));
    }

    #[test]
    fn zero_sized() {
        let mut m = Memory::new(0x10000, 8);
        let a = m.alloc(0, 1, AllocKind::Static(0), false).unwrap();
        assert!(m.read(a, 0, 1).is_ok());
        assert!(m.read(a, 1, 1).is_err());
    }
}
