//! Helpers for building TIR programs by hand in tests.

use super::*;
use crate::layout::Target;
use crate::source::Span;
use crate::symbol::Symbol;
use num_bigint::BigInt;
use std::rc::Rc;

pub fn target64() -> Target {
    Target { ptr_bytes: 8, little_endian: true, int128_align: 16, os: "Linux", arch: "X86_64", filc: false }
}

pub fn cfg() -> RunConfig {
    RunConfig { target: target64(), mode: Mode::Checked, step_budget: None }
}

pub fn ex(ty: Ty, kind: ExprKind) -> Expr {
    Expr { ty, kind, span: Span::DUMMY }
}

pub fn int(v: i128, ty: Ty) -> Expr {
    ex(ty, ExprKind::Int(BigInt::from(v)))
}

pub fn bigint(v: BigInt, ty: Ty) -> Expr {
    ex(ty, ExprKind::Int(v))
}

pub fn flt(v: f64, ty: Ty) -> Expr {
    ex(ty, ExprKind::Float(v))
}

pub fn boolean(b: bool) -> Expr {
    ex(Ty::bool(), ExprKind::Bool(b))
}

pub fn unit() -> Expr {
    ex(Ty::unit(), ExprKind::Aggregate(vec![]))
}

pub fn string(s: &str) -> Expr {
    ex(Ty::string(), ExprKind::Str(Rc::from(s.as_bytes())))
}

pub fn local(l: LocalId, ty: Ty) -> Expr {
    ex(ty, ExprKind::Local(l))
}

pub fn upvar(levels: u32, l: LocalId, ty: Ty) -> Expr {
    ex(ty, ExprKind::Upvar { levels, local: l })
}

pub fn bin(op: BinOp, l: Expr, r: Expr) -> Expr {
    let ty = match op {
        BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => Ty::bool(),
        BinOp::Add(Flavor::Opt) | BinOp::Sub(Flavor::Opt) | BinOp::Mul(Flavor::Opt) | BinOp::Shl(Flavor::Opt) => {
            Ty::optional(l.ty)
        }
        _ => l.ty,
    };
    ex(ty, ExprKind::Binary { op, lhs: Box::new(l), rhs: Box::new(r) })
}

pub fn add(l: Expr, r: Expr) -> Expr {
    bin(BinOp::Add(Flavor::Checked), l, r)
}

pub fn sub(l: Expr, r: Expr) -> Expr {
    bin(BinOp::Sub(Flavor::Checked), l, r)
}

pub fn mul(l: Expr, r: Expr) -> Expr {
    bin(BinOp::Mul(Flavor::Checked), l, r)
}

pub fn lt(l: Expr, r: Expr) -> Expr {
    bin(BinOp::Lt, l, r)
}

pub fn un(op: UnOp, x: Expr) -> Expr {
    ex(x.ty, ExprKind::Unary { op, operand: Box::new(x) })
}

pub fn eq(l: Expr, r: Expr) -> Expr {
    ex(Ty::bool(), ExprKind::Eq { lhs: Box::new(l), rhs: Box::new(r), negated: false })
}

pub fn logic(is_and: bool, l: Expr, r: Expr) -> Expr {
    ex(Ty::bool(), ExprKind::Logic { is_and, lhs: Box::new(l), rhs: Box::new(r) })
}

pub fn call(f: FnId, args: Vec<Expr>, ty: Ty) -> Expr {
    ex(ty, ExprKind::Call { callee: Callee::Fn(f), args })
}

pub fn call_indirect(callee: Expr, args: Vec<Expr>, ty: Ty) -> Expr {
    ex(ty, ExprKind::Call { callee: Callee::Indirect(Box::new(callee)), args })
}

pub fn perform(effect: &EffectRef, op: u32, args: Vec<Expr>, ty: Ty) -> Expr {
    ex(ty, ExprKind::Call { callee: Callee::Perform { effect: effect.clone(), op }, args })
}

pub fn blk(stmts: Vec<Stmt>, tail: Option<Expr>, ty: Ty) -> Expr {
    ex(ty, ExprKind::Block(Box::new(Block { stmts, tail, span: Span::DUMMY })))
}

pub fn block_unit(stmts: Vec<Stmt>) -> Expr {
    blk(stmts, None, Ty::unit())
}

pub fn let_(l: LocalId, e: Expr) -> Stmt {
    Stmt::Let { local: l, init: Some(e) }
}

pub fn assign(place: Expr, value: Expr) -> Stmt {
    Stmt::Assign { place, value }
}

pub fn stmt(e: Expr) -> Stmt {
    Stmt::Expr(e)
}

pub fn defer(e: Expr) -> Stmt {
    Stmt::Defer { is_err: false, body: e }
}

pub fn errdefer(e: Expr) -> Stmt {
    Stmt::Defer { is_err: true, body: e }
}

pub fn if_(c: Expr, t: Expr, e: Option<Expr>, ty: Ty) -> Expr {
    ex(ty, ExprKind::If { cond: vec![CondPart::Bool(c)], then: Box::new(t), else_: e.map(Box::new) })
}

pub fn ret(levels: u32, v: Expr) -> Expr {
    ex(Ty::never(), ExprKind::Return { levels, value: Box::new(v) })
}

pub fn brk(target: crate::tir::Target, v: Option<Expr>) -> Expr {
    ex(Ty::never(), ExprKind::Break { target, value: v.map(Box::new) })
}

pub fn cont(target: crate::tir::Target) -> Expr {
    ex(Ty::never(), ExprKind::Continue { target })
}

pub fn lbl(levels: u32, label: u32) -> crate::tir::Target {
    crate::tir::Target::Label { levels, label: LabelId(label) }
}

pub fn coerce(kind: Coercion, e: Expr, ty: Ty) -> Expr {
    ex(ty, ExprKind::Coerce { kind, expr: Box::new(e) })
}

pub fn some(e: Expr) -> Expr {
    let t = Ty::optional(e.ty);
    coerce(Coercion::WrapOptional, e, t)
}

pub fn null(ty: Ty) -> Expr {
    ex(ty, ExprKind::Null)
}

pub fn ok(e: Expr, res_ty: Ty) -> Expr {
    coerce(Coercion::WrapOk, e, res_ty)
}

pub fn err(e: Expr, res_ty: Ty) -> Expr {
    ex(res_ty, ExprKind::MakeErr(Box::new(e)))
}

pub fn addr_of(e: Expr) -> Expr {
    let t = Ty::ptr(false, e.ty);
    ex(t, ExprKind::AddrOf(Box::new(e)))
}

pub fn deref(p: Expr) -> Expr {
    let t = match p.ty.kind() {
        TyKind::Ptr { pointee, .. } => *pointee,
        _ => panic!("deref of non-pointer"),
    };
    ex(t, ExprKind::Deref(Box::new(p)))
}

pub fn field(base: Expr, index: u32, ty: Ty) -> Expr {
    ex(ty, ExprKind::Field { base: Box::new(base), index })
}

pub fn tuple(items: Vec<Expr>) -> Expr {
    let ty = Ty::tuple(items.iter().map(|i| i.ty).collect());
    ex(ty, ExprKind::Aggregate(items))
}

pub fn array(items: Vec<Expr>) -> Expr {
    let ty = Ty::array(items.len() as u64, items[0].ty);
    ex(ty, ExprKind::Array(items))
}

pub fn index(base: Expr, i: Expr) -> Expr {
    let elem = match base.ty.kind() {
        TyKind::Array { elem, .. } | TyKind::Slice { elem, .. } | TyKind::ManyPtr { elem, .. } => *elem,
        _ => panic!("index of non-array"),
    };
    ex(elem, ExprKind::Index { base: Box::new(base), index: Box::new(i), checked: true })
}

pub fn slice(base: Expr, lo: Option<Expr>, hi: Option<Expr>, inclusive: bool) -> Expr {
    let elem = match base.ty.kind() {
        TyKind::Array { elem, .. } | TyKind::Slice { elem, .. } | TyKind::ManyPtr { elem, .. } => *elem,
        _ => panic!("slice of non-array"),
    };
    ex(
        Ty::slice(false, elem),
        ExprKind::Slice { base: Box::new(base), lo: lo.map(Box::new), hi: hi.map(Box::new), inclusive },
    )
}

pub fn builtin(op: Builtin, args: Vec<Expr>, ty: Ty) -> Expr {
    ex(ty, ExprKind::Builtin { op, args })
}

pub fn usz(v: i128) -> Expr {
    int(v, Ty::usize())
}

pub fn i32e(v: i128) -> Expr {
    int(v, Ty::i32())
}

pub fn u8e(v: i128) -> Expr {
    int(v, Ty::u8())
}

pub fn wild(ty: Ty) -> Pat {
    Pat { ty, kind: PatKind::Wild, span: Span::DUMMY }
}

pub fn pat(ty: Ty, kind: PatKind) -> Pat {
    Pat { ty, kind, span: Span::DUMMY }
}

pub fn bind(l: LocalId, ty: Ty) -> Pat {
    pat(ty, PatKind::Bind(l))
}

pub fn arm(p: Pat, guard: Option<Expr>, body: Expr) -> Arm {
    Arm { pat: p, guard, body }
}

pub fn match_(scrut: Expr, arms: Vec<Arm>, ty: Ty) -> Expr {
    ex(ty, ExprKind::Match { scrutinee: Box::new(scrut), arms })
}

pub fn loop_(label: u32, cond: Vec<CondPart>, body: Vec<Stmt>, step: Option<Expr>) -> Expr {
    ex(
        Ty::unit(),
        ExprKind::Loop {
            label: LabelId(label),
            cond,
            body: Box::new(Block { stmts: body, tail: None, span: Span::DUMMY }),
            step: step.map(Box::new),
        },
    )
}

pub fn effect_ref(name: &str) -> EffectRef {
    EffectRef { effect: EffectId::fresh(Symbol::intern(name)), args: Box::new([]) }
}

pub fn with_(label: u32, handlers: Vec<HandlerValue>, body: Expr, ty: Ty) -> Expr {
    ex(ty, ExprKind::With { label: LabelId(label), handlers, on: None, body: Box::new(body) })
}

pub fn write_to(sink: WriteSink, pieces: &[&str], args: Vec<Expr>) -> Expr {
    let pieces = pieces.iter().map(|p| Rc::from(p.as_bytes())).collect();
    ex(Ty::unit(), ExprKind::Write { sink, pieces, specs: vec![FmtSpec::default(); args.len()], args })
}

/// Builds one function.
pub struct FnB {
    name: Symbol,
    pub locals: Vec<LocalDecl>,
    pub params: Vec<LocalId>,
    pub ret: Ty,
    pub kind: FnKind,
    pub parent: Option<FnId>,
    pub effects: EffectSet,
}

impl FnB {
    pub fn new(name: &str, ret: Ty) -> FnB {
        FnB {
            name: Symbol::intern(name),
            locals: vec![],
            params: vec![],
            ret,
            kind: FnKind::Named,
            parent: None,
            effects: EffectSet::empty(),
        }
    }

    pub fn kind(mut self, k: FnKind) -> FnB {
        self.kind = k;
        self
    }

    pub fn local(&mut self, name: &str, ty: Ty) -> LocalId {
        self.locals.push(LocalDecl { name: Symbol::intern(name), ty, mutable: true, span: Span::DUMMY });
        LocalId(self.locals.len() as u32 - 1)
    }

    pub fn param(&mut self, name: &str, ty: Ty) -> LocalId {
        let l = self.local(name, ty);
        self.params.push(l);
        l
    }

    pub fn build(self, body: Expr) -> FnDef {
        FnDef {
            name: self.name,
            span: Span::DUMMY,
            params: self.params,
            locals: self.locals,
            ret: self.ret,
            effects: self.effects,
            parent: self.parent,
            kind: self.kind,
            callconv: Default::default(),
            hints: Default::default(),
            body: Some(body),
        }
    }
}

/// Program under construction.
#[derive(Default)]
pub struct Tb {
    pub prog: Program,
}

impl Tb {
    pub fn new() -> Tb {
        Tb::default()
    }

    /// The id the next added function will get.
    pub fn next_fn(&self) -> FnId {
        FnId(self.prog.fns.len() as u32)
    }

    pub fn add(&mut self, f: FnDef) -> FnId {
        self.prog.fns.push(f);
        FnId(self.prog.fns.len() as u32 - 1)
    }

    pub fn add_handler(&mut self, effect: &EffectRef, clauses: Vec<FnId>) -> HandlerId {
        self.prog.handlers.push(HandlerLit { effect: effect.clone(), clauses });
        HandlerId(self.prog.handlers.len() as u32 - 1)
    }

    pub fn add_handler_decl(&mut self, effect: &EffectRef, for_ty: Ty, clauses: Vec<FnId>) -> HandlerDeclId {
        self.prog.handler_decls.push(HandlerDecl { effect: effect.clone(), for_ty, clauses });
        HandlerDeclId(self.prog.handler_decls.len() as u32 - 1)
    }

    pub fn add_const(&mut self, name: &str, ty: Ty, init: Expr) -> ConstId {
        self.prog.consts.push(ConstDef { name: Symbol::intern(name), ty, span: Span::DUMMY, init, value: None });
        ConstId(self.prog.consts.len() as u32 - 1)
    }

    pub fn add_struct(&mut self, name: &str, fields: &[(&str, Ty)]) -> Ty {
        let id = AdtId::fresh(Symbol::intern(name));
        let fields = fields
            .iter()
            .map(|(n, t)| FieldDef { name: Symbol::intern(n), ty: *t, is_pub: true, align: None })
            .collect();
        self.prog.adts.insert(
            id,
            AdtDef {
                name: Symbol::intern(name),
                kind: AdtKind::Struct { fields, packed: false, align: None, bits: None },
            },
        );
        Ty::intern(TyKind::Adt(id, Box::new([])))
    }

    pub fn add_bits_struct(&mut self, name: &str, under: Ty, fields: &[(&str, Ty)]) -> Ty {
        let id = AdtId::fresh(Symbol::intern(name));
        let fields = fields
            .iter()
            .map(|(n, t)| FieldDef { name: Symbol::intern(n), ty: *t, is_pub: true, align: None })
            .collect();
        self.prog.adts.insert(
            id,
            AdtDef {
                name: Symbol::intern(name),
                kind: AdtKind::Struct { fields, packed: false, align: None, bits: Some(under) },
            },
        );
        Ty::intern(TyKind::Adt(id, Box::new([])))
    }

    /// A union; variants are `(name, payload type)`, tagged 0, 1, 2, ….
    pub fn add_union(&mut self, name: &str, variants: &[(&str, Option<Ty>)]) -> Ty {
        let id = AdtId::fresh(Symbol::intern(name));
        let vs = variants
            .iter()
            .enumerate()
            .map(|(i, (n, p))| VariantDef { name: Symbol::intern(n), payload: *p, tag: BigInt::from(i) })
            .collect();
        self.prog
            .adts
            .insert(id, AdtDef { name: Symbol::intern(name), kind: AdtKind::Union { tag: Ty::u8(), variants: vs } });
        Ty::intern(TyKind::Adt(id, Box::new([])))
    }

    pub fn eval(&self, e: &Expr) -> Result<ConstValue, Trap> {
        eval_const(&self.prog, &cfg(), e)
    }

    pub fn eval_mode(&self, e: &Expr, mode: Mode) -> Result<ConstValue, Trap> {
        let mut c = cfg();
        c.mode = mode;
        eval_const(&self.prog, &c, e)
    }

    /// Runs `main`, returning `(outcome, stdout, stderr)`.
    pub fn run(&mut self, main: FnId, args: &[&str]) -> (Outcome, String, String) {
        self.prog.main = Some(main);
        let args: Vec<String> = args.iter().map(|s| s.to_string()).collect();
        let (mut out, mut errb) = (Vec::new(), Vec::new());
        let o = run_main(&self.prog, &cfg(), &args, &mut out, &mut errb);
        (o, String::from_utf8_lossy(&out).into_owned(), String::from_utf8_lossy(&errb).into_owned())
    }
}

pub fn int_of(v: &ConstValue) -> i128 {
    let (signed, bits) = crate::layout::int_bits(v.ty, &target64()).expect("integer");
    num::decode_int(&v.bytes, signed, bits, true).to_i128().expect("fits i128")
}

pub fn big_of(v: &ConstValue) -> BigInt {
    let (signed, bits) = crate::layout::int_bits(v.ty, &target64()).expect("integer");
    num::decode_int(&v.bytes, signed, bits, true).to_big()
}

pub fn f64_of(v: &ConstValue) -> f64 {
    match v.ty.kind() {
        TyKind::Float(f) => num::decode_float(&v.bytes, *f, true),
        _ => panic!("not a float"),
    }
}

pub fn bool_of(v: &ConstValue) -> bool {
    v.bytes[0] != 0
}

pub fn trap_msg(r: Result<ConstValue, Trap>) -> String {
    match r {
        Err(t) => t.message,
        Ok(v) => panic!("expected a trap, got {:?}", v),
    }
}

/// Evaluates an expression and returns its integer value.
pub fn eval_int(e: &Expr) -> i128 {
    int_of(&Tb::new().eval(e).unwrap_or_else(|t| panic!("trap: {}", t.message)))
}

pub fn eval_trap(e: &Expr) -> String {
    trap_msg(Tb::new().eval(e))
}

/// Adds a function built from `build` and evaluates a call to it.
pub fn run_in(tb: &mut Tb, ret: Ty, build: impl FnOnce(&mut FnB) -> Expr) -> Result<ConstValue, Trap> {
    let mut f = FnB::new("t", ret);
    let body = build(&mut f);
    let id = tb.add(f.build(body));
    tb.eval(&call(id, vec![], ret))
}

pub fn run_body(ret: Ty, build: impl FnOnce(&mut FnB) -> Expr) -> Result<ConstValue, Trap> {
    run_in(&mut Tb::new(), ret, build)
}

/// A second-class function type with the given effects.
pub fn fn_ty(params: Vec<Ty>, ret: Ty, effects: Vec<EffectRef>) -> Ty {
    Ty::intern(TyKind::Fn(Box::new(FnTy {
        params: params.into_boxed_slice(),
        ret,
        effects: EffectSet::new(effects),
        variadic: false,
        callconv: Default::default(),
    })))
}

pub fn fnptr_ty(params: Vec<Ty>, ret: Ty) -> Ty {
    Ty::intern(TyKind::FnPtr(Box::new(FnTy {
        params: params.into_boxed_slice(),
        ret,
        effects: EffectSet::empty(),
        variadic: false,
        callconv: Default::default(),
    })))
}
