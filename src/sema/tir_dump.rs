//! `gamma tir`: the typed intermediate representation of a checked program, as text. It
//! is the stable form of checking function bodies that the self-hosted compiler is
//! compared against.
//!
//! Every function whose body is written outside the core library is printed, in source
//! order, except the instances of generic functions (their templates are printed). A
//! function is named by its name and the place it is written (`f@main.gm:3:1`), an
//! instance by its template and type arguments, so that nothing depends on the order in
//! which the checker created them. Each expression is one line, `kind operands : type`,
//! followed by its sub-expressions indented by two spaces.

use super::Sema;
use crate::tir::{
    Arm, Block, Callee, CondPart, Expr, ExprKind, FnId, FnKind, HandlerValue, Pat, PatKind, Stmt, Target, WriteSink,
};
use crate::types::Ty;
use std::fmt::Write as _;

impl Sema<'_> {
    /// The text of the program's functions (see the module documentation).
    pub(super) fn tir_dump(&self) -> String {
        let mut fns: Vec<FnId> = (0..self.prog.fns.len() as u32)
            .map(FnId)
            .filter(|&f| {
                let def = &self.prog.fns[f.index()];
                !self.instance_of.contains_key(&f)
                    && def.name.as_str() != "<entry>"
                    && def.span != crate::source::Span::DUMMY
                    && !self.sess.sources.file(def.span.file).path.starts_with("<core>")
            })
            .collect();
        fns.sort_by_key(|&f| {
            let s = self.prog.fns[f.index()].span;
            (s.file, s.lo, s.hi, f)
        });
        let mut d = Dump { sema: self, out: String::new(), fn_id: FnId(0) };
        for f in fns {
            d.function(f);
        }
        d.out
    }

    /// How the dump names function `f`.
    pub(super) fn fn_label(&self, f: FnId) -> String {
        if let Some((g, args)) = self.instance_of.get(&f) {
            let args: Vec<String> = args.iter().map(|t| t.to_string()).collect();
            return format!("{}[{}]", self.fn_label(*g), args.join(", "));
        }
        let def = &self.prog.fns[f.index()];
        if def.span == crate::source::Span::DUMMY {
            return def.name.to_string();
        }
        let file = self.sess.sources.file(def.span.file);
        let (line, col) = file.line_col(def.span.lo);
        format!("{}@{}:{}:{}", def.name, file.path.to_string_lossy().replace('\\', "/"), line, col)
    }
}

struct Dump<'a, 's> {
    sema: &'a Sema<'s>,
    out: String,
    /// The function being printed, whose locals the expressions name.
    fn_id: FnId,
}

impl Dump<'_, '_> {
    fn line(&mut self, depth: usize, text: &str) {
        for _ in 0..depth {
            self.out.push_str("  ");
        }
        self.out.push_str(text);
        self.out.push('\n');
    }

    fn function(&mut self, f: FnId) {
        self.fn_id = f;
        let def = &self.sema.prog.fns[f.index()];
        let kind = match def.kind {
            FnKind::Named => "named".to_string(),
            FnKind::Literal => "literal".to_string(),
            FnKind::Clause => "clause".to_string(),
            FnKind::Foreign { .. } => "foreign".to_string(),
        };
        let mut head = format!("fn {} {}", self.sema.fn_label(f), kind);
        if let Some(p) = def.parent {
            let _ = write!(head, " in {}", self.sema.fn_label(p));
        }
        self.line(0, &head);
        for (i, l) in def.locals.iter().enumerate() {
            let param = def.params.iter().any(|p| p.index() == i);
            let text = format!(
                "{} {} {}: {}{}",
                if param { "param" } else { "local" },
                i,
                l.name,
                l.ty,
                if l.mutable { " mut" } else { "" }
            );
            self.line(1, &text);
        }
        let ret = format!("ret {}", def.ret);
        self.line(1, &ret);
        if !def.effects.is_empty() {
            let effects: Vec<String> = def.effects.effects.iter().map(|e| e.to_string()).collect();
            let text = format!("needs {}", effects.join(" + "));
            self.line(1, &text);
        }
        match &def.body {
            Some(b) => self.expr(1, b),
            None => self.line(1, "no body"),
        }
    }

    fn local(&self, l: crate::tir::LocalId) -> String {
        let def = &self.sema.prog.fns[self.fn_id.index()];
        match def.locals.get(l.index()) {
            Some(d) => format!("{} {}", l.0, d.name),
            None => format!("{} ?", l.0),
        }
    }

    fn target(t: &Target) -> String {
        match t {
            Target::Label { levels, label } => format!("{} {}", levels, label.0),
            Target::InstallingWith => "with".to_string(),
        }
    }

    fn sink(sink: &WriteSink) -> String {
        match sink {
            WriteSink::Fn(_) => "fn".to_string(),
            WriteSink::Perform { effect, op } => format!("perform {} {}", effect, op),
        }
    }

    fn expr(&mut self, depth: usize, e: &Expr) {
        let ty = e.ty;
        let mut kids: Vec<&Expr> = vec![];
        let head = match &e.kind {
            ExprKind::Int(v) => format!("int {}", v),
            ExprKind::Float(x) => format!("float {:#018x}", x.to_bits()),
            ExprKind::SizeOf { of, align } => format!("{} {}", if *align { "align-of" } else { "size-of" }, of),
            ExprKind::IntLimit { of, max } => format!("{} {}", if *max { "max-value" } else { "min-value" }, of),
            ExprKind::TypeName { of } => format!("type-name {}", of),
            ExprKind::TypeIs { a, b, negated } => {
                format!("type-is{} {} {}", if *negated { "-not" } else { "" }, a, b)
            }
            ExprKind::UntypedFloat(r) => format!("untyped-float {}", r),
            ExprKind::UntypedComplex(c) => format!("untyped-complex {} {}", c.0, c.1),
            ExprKind::Bool(b) => format!("bool {}", b),
            ExprKind::Str(s) => format!("str {}", crate::driver::escape_bytes(s)),
            ExprKind::CStr(s) => format!("cstr {}", crate::driver::escape_bytes(s)),
            ExprKind::Null => "null".to_string(),
            ExprKind::ConstBytes(b) => {
                let hex: String = b.iter().map(|x| format!("{:02x}", x)).collect();
                format!("const-bytes {}", hex)
            }
            ExprKind::ValueParam(p) => format!("value-param {}", p.name()),
            ExprKind::Uninit => "uninit".to_string(),
            ExprKind::Const(c) => {
                let def = &self.sema.prog.consts[c.index()];
                let file = self.sema.sess.sources.file(def.span.file);
                let (line, col) = file.line_col(def.span.lo);
                format!("const {}@{}:{}:{}", def.name, file.path.to_string_lossy().replace('\\', "/"), line, col)
            }
            ExprKind::Local(l) => format!("local {}", self.local(*l)),
            ExprKind::Upvar { levels, local } => format!("upvar {} {}", levels, local.0),
            ExprKind::Global(g) => format!("global {}", self.sema.prog.globals[g.index()].name),
            ExprKind::Deref(x) => {
                kids.push(x);
                "deref".to_string()
            }
            ExprKind::Field { base, index } => {
                kids.push(base);
                format!("field {}", index)
            }
            ExprKind::VariantPayload { base, variant } => {
                kids.push(base);
                format!("variant-payload {}", variant)
            }
            ExprKind::Index { base, index, checked } => {
                kids.push(base);
                kids.push(index);
                format!("index{}", if *checked { "" } else { " unchecked" })
            }
            ExprKind::Slice { base, lo, hi, inclusive } => {
                kids.push(base);
                kids.extend(lo.as_deref());
                kids.extend(hi.as_deref());
                format!(
                    "slice{}{}{}",
                    if lo.is_some() { " lo" } else { "" },
                    if hi.is_some() { " hi" } else { "" },
                    if *inclusive { " inclusive" } else { "" }
                )
            }
            ExprKind::SliceLen(x) => {
                kids.push(x);
                "slice-len".to_string()
            }
            ExprKind::SlicePtr(x) => {
                kids.push(x);
                "slice-ptr".to_string()
            }
            ExprKind::AddrOf(x) => {
                kids.push(x);
                "addr-of".to_string()
            }
            ExprKind::AddrOfTemp(x) => {
                kids.push(x);
                "addr-of-temp".to_string()
            }
            ExprKind::FnRef(f) => format!("fn-ref {}", self.sema.fn_label(*f)),
            ExprKind::Closure(f) => format!("closure {}", self.sema.fn_label(*f)),
            ExprKind::HandlerRef(h) => {
                let clauses: Vec<String> =
                    self.sema.prog.handlers[h.index()].clauses.iter().map(|c| self.sema.fn_label(*c)).collect();
                format!("handler-ref {} [{}]", self.sema.prog.handlers[h.index()].effect, clauses.join(", "))
            }
            ExprKind::Unary { op, operand } => {
                kids.push(operand);
                format!("unary {:?}", op)
            }
            ExprKind::Binary { op, lhs, rhs } => {
                kids.push(lhs);
                kids.push(rhs);
                format!("binary {:?}", op)
            }
            ExprKind::Logic { is_and, lhs, rhs } => {
                kids.push(lhs);
                kids.push(rhs);
                (if *is_and { "and" } else { "or" }).to_string()
            }
            ExprKind::Coerce { kind, expr } => {
                kids.push(expr);
                format!("coerce {:?}", kind)
            }
            ExprKind::Builtin { op, args } => {
                kids.extend(args.iter());
                format!("builtin {:?}", op)
            }
            ExprKind::Call { callee, args } => {
                let head = match callee {
                    Callee::Fn(f) => format!("call fn {}", self.sema.fn_label(*f)),
                    Callee::Indirect(c) => {
                        kids.push(c);
                        "call indirect".to_string()
                    }
                    Callee::Perform { effect, op } => format!("call perform {} {}", effect, op),
                };
                kids.extend(args.iter());
                head
            }
            ExprKind::Aggregate(xs) => {
                kids.extend(xs.iter());
                "aggregate".to_string()
            }
            ExprKind::Array(xs) => {
                kids.extend(xs.iter());
                "array".to_string()
            }
            ExprKind::ArrayFill(x) => {
                kids.push(x);
                "array-fill".to_string()
            }
            ExprKind::Variant { variant, payload } => {
                kids.extend(payload.as_deref());
                format!("variant {}", variant)
            }
            ExprKind::MakeErr(x) => {
                kids.push(x);
                "make-err".to_string()
            }
            ExprKind::Unwrap { expr, is_result } => {
                kids.push(expr);
                (if *is_result { "unwrap-result" } else { "unwrap-optional" }).to_string()
            }
            ExprKind::Eq { lhs, rhs, negated } => {
                kids.push(lhs);
                kids.push(rhs);
                (if *negated { "is-not" } else { "is" }).to_string()
            }
            ExprKind::In { elem, container, negated } => {
                kids.push(elem);
                kids.push(container);
                (if *negated { "not-in" } else { "in" }).to_string()
            }
            ExprKind::Block(b) => {
                self.line(depth, &format!("block : {}", ty));
                self.block(depth + 1, b);
                return;
            }
            ExprKind::If { cond, then, else_ } => {
                self.line(depth, &format!("if : {}", ty));
                self.conds(depth + 1, cond);
                self.line(depth + 1, "then");
                self.expr(depth + 2, then);
                if let Some(x) = else_ {
                    self.line(depth + 1, "else");
                    self.expr(depth + 2, x);
                }
                return;
            }
            ExprKind::Match { scrutinee, arms } => {
                self.line(depth, &format!("match : {}", ty));
                self.expr(depth + 1, scrutinee);
                for a in arms {
                    self.arm(depth + 1, a);
                }
                return;
            }
            ExprKind::Else { lhs, capture, rhs } => {
                kids.push(lhs);
                kids.push(rhs);
                match capture {
                    Some(l) => format!("else capture {}", self.local(*l)),
                    None => "else".to_string(),
                }
            }
            ExprKind::Try { expr, lift, levels } => {
                kids.push(expr);
                match lift {
                    Some(crate::tir::Lift::Variant(v)) => format!("try {} lift {}", levels, v),
                    None => format!("try {}", levels),
                }
            }
            ExprKind::Loop { label, cond, body, step } => {
                self.line(depth, &format!("loop {} : {}", label.0, ty));
                self.conds(depth + 1, cond);
                self.line(depth + 1, "body");
                self.block(depth + 2, body);
                if let Some(s) = step {
                    self.line(depth + 1, "step");
                    self.expr(depth + 2, s);
                }
                return;
            }
            ExprKind::Break { target, value } => {
                kids.extend(value.as_deref());
                format!("break {}", Self::target(target))
            }
            ExprKind::Continue { target } => format!("continue {}", Self::target(target)),
            ExprKind::Return { levels, value } => {
                kids.push(value);
                format!("return {}", levels)
            }
            ExprKind::With { label, handlers, on, body } => {
                self.line(depth, &format!("with {} : {}", label.0, ty));
                for h in handlers {
                    match h {
                        HandlerValue::Literal(x) => {
                            self.line(depth + 1, "handler");
                            self.expr(depth + 2, x);
                        }
                        HandlerValue::Decl { decls, value } => {
                            let names: Vec<String> = decls
                                .iter()
                                .map(|d| {
                                    let h = &self.sema.prog.handler_decls[d.index()];
                                    format!("{} for {}", h.effect, h.for_ty)
                                })
                                .collect();
                            self.line(depth + 1, &format!("handlers {}", names.join(", ")));
                            self.expr(depth + 2, value);
                        }
                    }
                }
                if let Some(x) = on {
                    self.line(depth + 1, "on");
                    self.expr(depth + 2, x);
                }
                self.line(depth + 1, "body");
                self.expr(depth + 2, body);
                return;
            }
            ExprKind::Resume { cont, value } => {
                kids.push(cont);
                kids.push(value);
                "resume".to_string()
            }
            ExprKind::Cancel(x) => {
                kids.push(x);
                "cancel".to_string()
            }
            ExprKind::Trap { message } => {
                kids.push(message);
                "trap".to_string()
            }
            ExprKind::Write { sink, pieces, specs, args } => {
                let mut head = format!("write {}", Self::sink(sink));
                for p in pieces.iter() {
                    let _ = write!(head, " {}", crate::driver::escape_bytes(p));
                }
                for s in specs {
                    if s.is_default() {
                        head.push_str(" {}");
                    } else {
                        let _ = write!(head, " {:?}", s);
                    }
                }
                if let WriteSink::Fn(x) = sink {
                    kids.push(x);
                }
                kids.extend(args.iter());
                head
            }
            ExprKind::WriteDeferred { sink, template, args, tmp } => {
                let head = format!("write-deferred {} {}", Self::sink(sink), self.local(*tmp));
                if let WriteSink::Fn(x) = sink {
                    kids.push(x);
                }
                kids.push(template);
                kids.extend(args.as_deref());
                head
            }
            ExprKind::SoaDeferred { op, args } => {
                kids.extend(args.iter());
                format!("soa {:?}", op)
            }
        };
        self.line(depth, &format!("{} : {}", head, ty));
        for k in kids {
            self.expr(depth + 1, k);
        }
    }

    fn block(&mut self, depth: usize, b: &Block) {
        for s in &b.stmts {
            match s {
                Stmt::Let { local, init } => {
                    self.line(depth, &format!("let {}", self.local(*local)));
                    if let Some(x) = init {
                        self.expr(depth + 1, x);
                    }
                }
                Stmt::LetTuple { locals, init } => {
                    let names: Vec<String> =
                        locals.iter().map(|l| l.map_or_else(|| "_".to_string(), |l| self.local(l))).collect();
                    self.line(depth, &format!("let-tuple {}", names.join(", ")));
                    self.expr(depth + 1, init);
                }
                Stmt::Assign { place, value } => {
                    self.line(depth, "assign");
                    self.expr(depth + 1, place);
                    self.expr(depth + 1, value);
                }
                Stmt::AssignTuple { places, value } => {
                    self.line(depth, "assign-tuple");
                    for p in places {
                        match p {
                            Some(x) => self.expr(depth + 1, x),
                            None => self.line(depth + 1, "_"),
                        }
                    }
                    self.expr(depth + 1, value);
                }
                Stmt::Expr(x) => {
                    self.line(depth, "expr");
                    self.expr(depth + 1, x);
                }
                Stmt::Defer { is_err, body } => {
                    self.line(depth, if *is_err { "errdefer" } else { "defer" });
                    self.expr(depth + 1, body);
                }
            }
        }
        if let Some(t) = &b.tail {
            self.line(depth, "tail");
            self.expr(depth + 1, t);
        }
    }

    fn conds(&mut self, depth: usize, cond: &[CondPart]) {
        for c in cond {
            match c {
                CondPart::Bool(x) => {
                    self.line(depth, "cond");
                    self.expr(depth + 1, x);
                }
                CondPart::Match { pat, value } => {
                    self.line(depth, "cond-match");
                    self.pat(depth + 1, pat);
                    self.expr(depth + 1, value);
                }
            }
        }
    }

    fn arm(&mut self, depth: usize, a: &Arm) {
        self.line(depth, "arm");
        self.pat(depth + 1, &a.pat);
        if let Some(g) = &a.guard {
            self.line(depth + 1, "guard");
            self.expr(depth + 2, g);
        }
        self.expr(depth + 1, &a.body);
    }

    fn pat(&mut self, depth: usize, p: &Pat) {
        let ty: Ty = p.ty;
        let head = match &p.kind {
            PatKind::Wild => "pat-wild".to_string(),
            PatKind::Bind(l) => format!("pat-bind {}", self.local(*l)),
            PatKind::Const(x) => {
                self.line(depth, &format!("pat-const : {}", ty));
                self.expr(depth + 1, x);
                return;
            }
            PatKind::Range { lo, hi, inclusive } => {
                format!("pat-range {} {}{}", lo, hi, if *inclusive { " inclusive" } else { "" })
            }
            PatKind::Null => "pat-null".to_string(),
            PatKind::Some(q) | PatKind::Ok(q) | PatKind::Err(q) => {
                let name = match &p.kind {
                    PatKind::Some(_) => "pat-some",
                    PatKind::Ok(_) => "pat-ok",
                    _ => "pat-err",
                };
                self.line(depth, &format!("{} : {}", name, ty));
                self.pat(depth + 1, q);
                return;
            }
            PatKind::Variant { variant, payload } => {
                self.line(depth, &format!("pat-variant {} : {}", variant, ty));
                if let Some(q) = payload {
                    self.pat(depth + 1, q);
                }
                return;
            }
            PatKind::Fields(fs) => {
                self.line(depth, &format!("pat-fields : {}", ty));
                for (i, q) in fs {
                    self.line(depth + 1, &format!("field {}", i));
                    self.pat(depth + 2, q);
                }
                return;
            }
            PatKind::Or(ps) => {
                self.line(depth, &format!("pat-or : {}", ty));
                for q in ps {
                    self.pat(depth + 1, q);
                }
                return;
            }
        };
        self.line(depth, &format!("{} : {}", head, ty));
    }
}
