//! Runtime values, frames, handler contexts and control-flow signals.

use super::bytes::Bytes;
use crate::source::Span;
use crate::tir::{FnId, HandlerDeclId, HandlerId, LabelId};
use crate::types::{EffectRef, Ty};
use std::cell::RefCell;
use std::rc::Rc;

/// A compile-time or run-time trap.
#[derive(Clone, Debug, PartialEq)]
pub struct Trap {
    pub message: String,
    pub span: Span,
    /// In Debug builds, the calls the trap left, innermost first: each called
    /// function and the location of its call.
    pub trace: Vec<(FnId, Span)>,
}

impl Trap {
    pub fn new(message: impl Into<String>, span: Span) -> Trap {
        Trap { message: message.into(), span, trace: Vec::new() }
    }
}

/// A value of the language: first-class data in target layout, or a second-class value.
#[derive(Clone, Debug)]
pub enum Value {
    Bytes(Bytes),
    /// A second-class function value: code, the frame it captures, and the
    /// handler context at the point it was written.
    Closure(Closure),
    /// A handler literal value.
    Handler(Rc<HandlerVal>),
}

#[derive(Clone, Debug)]
pub struct Closure {
    pub func: FnId,
    pub env: Option<Rc<Frame>>,
    pub ctx: Ctx,
}

#[derive(Debug)]
pub struct HandlerVal {
    pub id: HandlerId,
    pub env: Option<Rc<Frame>>,
    pub ctx: Ctx,
}

impl Value {
    pub fn unit() -> Value {
        Value::Bytes(Bytes::empty())
    }

    pub fn from_slice(s: &[u8]) -> Value {
        Value::Bytes(Bytes::from_slice(s))
    }
}

/// Where the storage of a local variable lives.
#[derive(Clone, Debug)]
pub enum Slot {
    Empty,
    /// A first-class local: its own allocation at this address.
    Mem(u64),
    /// A first-class local of at most [`super::machine::REG_MAX`] bytes whose address
    /// nothing has needed yet: its bytes, the first one least significant. It moves to
    /// an allocation of its own (`Mem`) when its address is first needed.
    Word(u64),
    /// A second-class local (function or handler value).
    Val(Box<Value>),
}

/// One activation of a function body (or the root of a constant evaluation).
#[derive(Debug)]
pub struct Frame {
    pub id: u64,
    /// The function's return type (for result-error detection and `try`).
    pub ret_ty: Ty,
    pub func: Option<FnId>,
    pub parent_env: Option<Rc<Frame>>,
    pub locals: RefCell<Vec<Slot>>,
    /// Allocations (locals, temporaries) that die when the frame exits.
    pub allocs: RefCell<Vec<u64>>,
    /// For a handler-literal clause: the frame and label of the `with` that installed it.
    pub installing: Option<(u64, LabelId)>,
    /// True for clauses of handler literals, which lexically belong to `parent_env`'s function.
    pub is_clause_lit: bool,
}

/// An entry of the persistent handler context.
#[derive(Debug)]
pub struct CtxNode {
    pub parent: Ctx,
    pub effect: EffectRef,
    pub installed: Installed,
    /// The `with` that installed it: frame id and label (0 for the runtime's defaults).
    pub with_frame: u64,
    pub with_label: LabelId,
}

#[derive(Clone, Debug, Default)]
pub struct Ctx(pub Option<Rc<CtxNode>>);

impl Ctx {
    pub fn empty() -> Ctx {
        Ctx(None)
    }

    pub fn push(&self, effect: EffectRef, installed: Installed, with_frame: u64, with_label: LabelId) -> Ctx {
        Ctx(Some(Rc::new(CtxNode { parent: self.clone(), effect, installed, with_frame, with_label })))
    }

    /// The innermost entry handling `effect`.
    pub fn find(&self, effect: &EffectRef) -> Option<&Rc<CtxNode>> {
        let mut cur = self.0.as_ref();
        while let Some(n) = cur {
            if &n.effect == effect {
                return Some(n);
            }
            cur = n.parent.0.as_ref();
        }
        None
    }
}

#[derive(Clone, Debug)]
pub enum Installed {
    /// A handler literal: its clauses run in the frame and context where it was written.
    Lit(Rc<HandlerVal>),
    /// A handler declaration with the value it was installed for and the context outside the `with`.
    Decl {
        decl: HandlerDeclId,
        value: Bytes,
        outer: Ctx,
    },
    Native(NativeKind),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NativeKind {
    Out,
    Err,
    Env,
}

/// Non-local control flow, propagated as the error side of evaluation.
#[derive(Debug)]
pub enum Flow {
    Break {
        frame: u64,
        label: LabelId,
        value: Value,
    },
    Continue {
        frame: u64,
        label: LabelId,
    },
    Return {
        frame: u64,
        value: Value,
        is_err: bool,
    },
    /// `Env.exit`: unwinds every frame to the runtime.
    Exit(u8),
    Trap(Trap),
    /// `resume k(v)`: ends the `ctl` clause; its `with`'s driver then switches to `fiber`.
    Resume {
        fiber: u64,
        value: Value,
    },
    /// Unwinds a discarded fiber to its base (§14.5).
    Cancel,
}

pub type R<T = Value> = Result<T, Box<Flow>>;

pub fn trap_flow(span: Span, message: impl Into<String>) -> Box<Flow> {
    Box::new(Flow::Trap(Trap::new(message, span)))
}
