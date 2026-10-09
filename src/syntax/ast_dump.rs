//! A stable, span-free textual rendering of the AST, used by `gamma parse --dump`
//! and by golden parser tests.
//!
//! The output is an S-expression tree. Every field of the AST that carries
//! meaning is shown; spans and node ids are not. Optional parts that are absent
//! are omitted, and each optional part is introduced by a label such as `(ty …)`
//! or `(ret …)`, so the form is unambiguous. A node is printed on one line when
//! it is short and has no block-like children; otherwise its children are put on
//! their own lines, indented by two spaces.
//!
//! ```text
//! (file
//!   (item pub
//!     (binding const main
//!       (fn (params) (ret (tuple))
//!         (block
//!           (expr (call (ident print) (arg (str "hello\n")))))))))
//! ```

use crate::driver::escape_bytes;
use crate::syntax::ast::*;
use crate::syntax::token::{FloatSuffix, StrKind, StrLit};

/// Maximum width of a node printed on one line, counting its indentation.
const WIDTH: usize = 92;

/// Renders a parsed file.
pub fn dump_file(file: &File) -> String {
    let sx = Dumper.file(file);
    let mut out = String::new();
    render(&sx, 0, &mut out);
    out.push('\n');
    out
}

/// Renders one expression on a single line (for unit tests and debugging).
pub fn dump_expr(e: &Expr) -> String {
    let sx = Dumper.expr(e);
    let mut out = String::new();
    flat(&sx, &mut out);
    out
}

// ---------------------------------------------------------------------------
// S-expression tree and layout
// ---------------------------------------------------------------------------

enum Sx {
    Atom(String),
    /// A list. `brk` forces the children onto separate lines.
    List {
        items: Vec<Sx>,
        brk: bool,
    },
}

fn atom(s: impl Into<String>) -> Sx {
    Sx::Atom(s.into())
}

/// A list that is printed on one line when short enough.
fn list(items: Vec<Sx>) -> Sx {
    Sx::List { items, brk: false }
}

/// A list whose children are always put on separate lines (unless it has none).
fn broken(items: Vec<Sx>) -> Sx {
    Sx::List { items, brk: true }
}

/// `(head rest…)`.
fn node(head: &str, rest: Vec<Sx>) -> Sx {
    let mut items = vec![atom(head)];
    items.extend(rest);
    list(items)
}

/// `(head rest…)`, always broken.
fn bnode(head: &str, rest: Vec<Sx>) -> Sx {
    let mut items = vec![atom(head)];
    items.extend(rest);
    broken(items)
}

/// `(label x)` for a present optional.
fn labeled(label: &str, x: Option<Sx>) -> Option<Sx> {
    x.map(|x| node(label, vec![x]))
}

fn push_opt(v: &mut Vec<Sx>, x: Option<Sx>) {
    if let Some(x) = x {
        v.push(x);
    }
}

/// Whether `sx` fits on one line in `budget` characters; subtracts its width.
fn fits(sx: &Sx, budget: &mut isize) -> bool {
    match sx {
        Sx::Atom(s) => {
            *budget -= s.chars().count() as isize;
            *budget >= 0
        }
        Sx::List { items, brk } => {
            if *brk && items.len() > 1 {
                return false;
            }
            *budget -= 2 + items.len().saturating_sub(1) as isize;
            if *budget < 0 {
                return false;
            }
            items.iter().all(|i| fits(i, budget))
        }
    }
}

fn flat(sx: &Sx, out: &mut String) {
    match sx {
        Sx::Atom(s) => out.push_str(s),
        Sx::List { items, .. } => {
            out.push('(');
            for (i, item) in items.iter().enumerate() {
                if i > 0 {
                    out.push(' ');
                }
                flat(item, out);
            }
            out.push(')');
        }
    }
}

fn render(sx: &Sx, indent: usize, out: &mut String) {
    match sx {
        Sx::Atom(s) => out.push_str(s),
        Sx::List { items, brk } => {
            let mut budget = WIDTH as isize - indent as isize;
            if fits(sx, &mut budget) {
                flat(sx, out);
                return;
            }
            // The head and any following children that fit stay on the first
            // line; once a child does not fit, the rest go on their own lines.
            out.push('(');
            let mut col = indent + 1;
            let mut inline = true;
            for (i, item) in items.iter().enumerate() {
                if inline {
                    let mut text = String::new();
                    let mut budget = WIDTH as isize - col as isize - 1;
                    if matches!(item, Sx::Atom(_)) || (!*brk && i > 0 && fits(item, &mut budget)) {
                        flat(item, &mut text);
                        if i > 0 {
                            out.push(' ');
                            col += 1;
                        }
                        col += text.chars().count();
                        out.push_str(&text);
                        continue;
                    }
                    inline = false;
                }
                out.push('\n');
                out.push_str(&" ".repeat(indent + 2));
                render(item, indent + 2, out);
            }
            out.push(')');
        }
    }
}

// ---------------------------------------------------------------------------
// AST to S-expression
// ---------------------------------------------------------------------------

struct Dumper;

fn name(id: &Ident) -> Sx {
    atom(id.name.as_str())
}

fn str_lit(s: &StrLit) -> Sx {
    let head = match s.kind {
        StrKind::Regular => "str",
        StrKind::Raw => "raw-str",
        StrKind::C => "c-str",
    };
    node(head, vec![atom(escape_bytes(&s.bytes))])
}

fn float_suffix(s: FloatSuffix) -> &'static str {
    match s {
        FloatSuffix::F16 => "f16",
        FloatSuffix::F32 => "f32",
        FloatSuffix::F64 => "f64",
    }
}

fn binop_name(op: BinOp) -> String {
    op.as_str().replace(' ', "-")
}

fn unop_name(op: UnOp) -> &'static str {
    match op {
        UnOp::Neg => "neg",
        UnOp::BitNot => "bit-not",
        UnOp::AddrOf => "addr-of",
        UnOp::MakeErr => "make-err",
        UnOp::Try => "try",
        UnOp::Not => "not",
    }
}

fn assign_op_name(op: AssignOp) -> String {
    match op {
        AssignOp::Assign => "=".to_string(),
        AssignOp::Compound(b) => format!("{}=", b.as_str()),
    }
}

impl Dumper {
    fn file(&self, f: &File) -> Sx {
        bnode("file", f.items.iter().map(|i| self.item(i)).collect())
    }

    fn attr(&self, a: &Attribute) -> Sx {
        let mut v = vec![name(&a.name)];
        if let Some(args) = &a.args {
            let mut xs = vec![atom("args")];
            xs.extend(args.iter().map(|e| self.expr(e)));
            v.push(list(xs));
        }
        node("attr", v)
    }

    fn attrs(&self, attrs: &[Attribute]) -> Vec<Sx> {
        attrs.iter().map(|a| self.attr(a)).collect()
    }

    // --- items ---

    fn item(&self, item: &Item) -> Sx {
        let mut v = Vec::new();
        if item.is_pub {
            v.push(atom("pub"));
        }
        if let Some(doc) = &item.doc {
            v.push(node("doc", vec![atom(escape_bytes(doc.as_bytes()))]));
        }
        v.extend(self.attrs(&item.attrs));
        v.push(match &item.kind {
            ItemKind::Binding(b) => self.binding(b),
            ItemKind::When(w) => self.when_items(w),
            ItemKind::Handler(h) => self.handler_decl(h),
            ItemKind::Test { name, body, .. } => node("test", vec![str_lit(name), self.block(body)]),
            ItemKind::Intrinsic(e) => node("intrinsic-decl", vec![self.expr(e)]),
        });
        bnode("item", v)
    }

    fn items(&self, label: &str, items: &[Item]) -> Sx {
        bnode(label, items.iter().map(|i| self.item(i)).collect())
    }

    fn when_items(&self, w: &WhenItems) -> Sx {
        let mut v = vec![self.expr(&w.cond), self.items("then", &w.then)];
        if let Some(e) = &w.else_ {
            v.push(self.items("else", e));
        }
        bnode("when-items", v)
    }

    fn handler_decl(&self, h: &HandlerDecl) -> Sx {
        let mut v = vec![self.expr(&h.effect), node("for", vec![self.expr(&h.for_ty)])];
        push_opt(&mut v, h.effects.as_ref().map(|e| self.effects(e)));
        v.extend(h.clauses.iter().map(|c| self.clause(c)));
        bnode("handler-decl", v)
    }

    fn clause(&self, c: &Clause) -> Sx {
        let mut params = vec![atom("params")];
        params.extend(c.params.iter().map(name));
        bnode("clause", vec![name(&c.name), list(params), self.expr(&c.body)])
    }

    fn effects(&self, e: &[Expr]) -> Sx {
        let mut v = vec![atom("effects")];
        v.extend(e.iter().map(|x| self.expr(x)));
        list(v)
    }

    // --- bindings and statements ---

    fn binding(&self, b: &Binding) -> Sx {
        let mut v = vec![atom(if b.kind == BindingKind::Const { "const" } else { "var" })];
        if b.names.len() == 1 {
            v.push(name(&b.names[0]));
        } else {
            let mut ns = vec![atom("names")];
            ns.extend(b.names.iter().map(name));
            v.push(list(ns));
        }
        push_opt(&mut v, labeled("ty", b.ty.as_ref().map(|t| self.expr(t))));
        v.push(self.expr(&b.init));
        node("binding", v)
    }

    fn block(&self, b: &Block) -> Sx {
        bnode("block", b.stmts.iter().map(|s| self.stmt(s)).collect())
    }

    fn stmt(&self, s: &Stmt) -> Sx {
        match &s.kind {
            StmtKind::Binding { attrs, binding } => {
                if attrs.is_empty() {
                    self.binding(binding)
                } else {
                    let mut v = self.attrs(attrs);
                    v.push(self.binding(binding));
                    node("binding-stmt", v)
                }
            }
            StmtKind::Assign { places, op, value } => {
                let mut ps = vec![atom("places")];
                ps.extend(places.iter().map(|p| self.expr(p)));
                node("assign", vec![atom(assign_op_name(*op)), list(ps), self.expr(value)])
            }
            StmtKind::Expr(e) => node("expr", vec![self.expr(e)]),
            StmtKind::Defer { is_err, body } => node(if *is_err { "errdefer" } else { "defer" }, vec![self.stmt(body)]),
            StmtKind::When(w) => self.when_stmt(w),
            StmtKind::While { label, cond, body } => {
                let mut v = Vec::new();
                push_opt(&mut v, labeled("label", label.as_ref().map(name)));
                v.push(self.cond(cond));
                v.push(self.block(body));
                node("while", v)
            }
            StmtKind::Discard(e) => node("discard", vec![self.expr(e)]),
            StmtKind::Block(b) => self.block(b),
        }
    }

    fn when_stmt(&self, w: &WhenStmt) -> Sx {
        let mut v = vec![self.expr(&w.cond), self.block(&w.then)];
        match &w.else_ {
            None => {}
            Some(WhenElse::Block(b)) => v.push(node("else", vec![self.block(b)])),
            Some(WhenElse::When(w)) => v.push(node("else", vec![self.when_stmt(w)])),
        }
        node("when-stmt", v)
    }

    fn cond(&self, c: &Cond) -> Sx {
        let mut v = vec![atom("cond")];
        for part in &c.parts {
            v.push(match part {
                CondPart::Bind { pat, value } => node("bind", vec![self.pattern(pat), self.expr(value)]),
                CondPart::Expr(e) => self.expr(e),
            });
        }
        list(v)
    }

    // --- expressions ---

    fn expr(&self, e: &Expr) -> Sx {
        match &e.kind {
            ExprKind::Int(l) => {
                let mut v = vec![atom(l.value.to_string())];
                if let Some(s) = l.suffix {
                    v.push(atom(s.to_string()));
                }
                node(if l.is_char { "char" } else { "int" }, v)
            }
            ExprKind::Float(l) => {
                let mut v = vec![atom(l.value.to_string())];
                if let Some(s) = l.suffix {
                    v.push(atom(float_suffix(s)));
                }
                node("float", v)
            }
            ExprKind::Str(s) => str_lit(s),
            ExprKind::Bool(b) => node("bool", vec![atom(b.to_string())]),
            ExprKind::Null => atom("null"),
            ExprKind::Uninit => atom("uninit"),
            ExprKind::Ident(id) => node("ident", vec![name(id)]),
            ExprKind::Underscore => atom("underscore"),
            ExprKind::DotIdent(id) => node("dot", vec![name(id)]),
            ExprKind::Paren(x) => node("paren", vec![self.expr(x)]),
            ExprKind::Tuple(elems) => node("tuple", elems.iter().map(|t| self.tuple_elem(t)).collect()),
            ExprKind::Unary { op, operand } => node("unary", vec![atom(unop_name(*op)), self.expr(operand)]),
            ExprKind::Binary { op, lhs, rhs } => {
                node("binary", vec![atom(binop_name(*op)), self.expr(lhs), self.expr(rhs)])
            }
            ExprKind::Else { lhs, capture, rhs } => {
                let mut v = vec![self.expr(lhs)];
                push_opt(&mut v, labeled("capture", capture.as_ref().map(name)));
                v.push(self.expr(rhs));
                node("else", v)
            }
            ExprKind::OptUnwrap(x) => node("opt-unwrap", vec![self.expr(x)]),
            ExprKind::ResUnwrap(x) => node("res-unwrap", vec![self.expr(x)]),
            ExprKind::Bang { lhs, rhs } => node("bang", vec![self.expr(lhs), self.expr(rhs)]),
            ExprKind::Deref(x) => node("deref", vec![self.expr(x)]),
            ExprKind::Call { callee, args, trailing_comma } => {
                let mut v = vec![self.expr(callee)];
                v.extend(args.iter().map(|a| self.arg(a)));
                if *trailing_comma {
                    v.push(atom("trailing-comma"));
                }
                node("call", v)
            }
            ExprKind::Index { base, index } => node("index", vec![self.expr(base), self.expr(index)]),
            ExprKind::Slice { base, lo, hi, inclusive } => {
                let mut v = vec![self.expr(base)];
                push_opt(&mut v, labeled("lo", lo.as_ref().map(|x| self.expr(x))));
                push_opt(&mut v, labeled("hi", hi.as_ref().map(|x| self.expr(x))));
                if *inclusive {
                    v.push(atom("inclusive"));
                }
                node("slice", v)
            }
            ExprKind::Field { base, name: n } => node("field", vec![self.expr(base), name(n)]),
            ExprKind::TupleField { base, index, .. } => {
                node("tuple-field", vec![self.expr(base), atom(index.to_string())])
            }
            ExprKind::OptionalType(x) => node("optional-type", vec![self.expr(x)]),
            ExprKind::PtrType { is_const, inner } => self.const_type("ptr-type", *is_const, inner),
            ExprKind::SliceType { is_const, inner } => self.const_type("slice-type", *is_const, inner),
            ExprKind::ManyPtrType { is_const, inner } => self.const_type("many-ptr-type", *is_const, inner),
            ExprKind::ArrayType { len, elem } => {
                node("array-type", vec![len.as_ref().map_or(atom("_"), |l| self.expr(l)), self.expr(elem)])
            }
            ExprKind::ErrorUnionType { err, ok } => node("error-union-type", vec![self.expr(err), self.expr(ok)]),
            ExprKind::FnType(sig) => self.fn_sig("fn-type", sig, vec![]),
            ExprKind::ContType(x) => node("cont-type", vec![self.expr(x)]),
            ExprKind::Struct(s) => self.struct_lit(s),
            ExprKind::Union(u) => self.union_lit(u),
            ExprKind::Effect(e) => self.effect_lit(e),
            ExprKind::Attributed { attrs, inner } => {
                let mut v = self.attrs(attrs);
                v.push(self.expr(inner));
                node("attributed", v)
            }
            ExprKind::Fn(f) => self.fn_lit(f),
            ExprKind::Handler(h) => {
                let mut v = vec![self.expr(&h.effect)];
                v.extend(h.clauses.iter().map(|c| self.clause(c)));
                bnode("handler", v)
            }
            ExprKind::Import(s) => node("import", vec![str_lit(s)]),
            ExprKind::Foreign(f) => {
                let mut v = vec![str_lit(&f.lib)];
                v.extend(f.members.iter().map(|m| self.foreign_member(m)));
                bnode("foreign", v)
            }
            ExprKind::If(i) => self.if_expr(i),
            ExprKind::Match(m) => {
                let mut v = vec![self.expr(&m.scrutinee)];
                v.extend(m.arms.iter().map(|a| self.arm(a)));
                bnode("match", v)
            }
            ExprKind::With(w) => {
                let mut v = Vec::new();
                push_opt(&mut v, labeled("label", w.label.as_ref().map(name)));
                let mut hs = vec![atom("handlers")];
                hs.extend(w.handlers.iter().map(|h| self.expr(h)));
                v.push(list(hs));
                push_opt(&mut v, labeled("on", w.on.as_ref().map(|x| self.expr(x))));
                v.push(self.block(&w.body));
                node("with", v)
            }
            ExprKind::For(f) => {
                let mut v = Vec::new();
                push_opt(&mut v, labeled("label", f.label.as_ref().map(name)));
                let mut ps = vec![atom("pats")];
                ps.extend(f.pats.iter().map(|p| self.pattern(p)));
                v.push(list(ps));
                v.push(self.expr(&f.iter));
                v.push(self.block(&f.body));
                node("for", v)
            }
            ExprKind::Block(b) => self.block(b),
            ExprKind::Return(x) => node("return", x.iter().map(|x| self.expr(x)).collect()),
            ExprKind::Break { label, value } => {
                let mut v = Vec::new();
                push_opt(&mut v, labeled("label", label.as_ref().map(name)));
                v.extend(value.iter().map(|x| self.expr(x)));
                node("break", v)
            }
            ExprKind::Continue { label } => {
                let mut v = Vec::new();
                push_opt(&mut v, labeled("label", label.as_ref().map(name)));
                node("continue", v)
            }
            ExprKind::Resume { cont, value } => {
                let mut v = vec![self.expr(cont)];
                v.extend(value.iter().map(|x| self.expr(x)));
                node("resume", v)
            }
            ExprKind::Intrinsic { name: n, args } => {
                let mut v = vec![name(n)];
                if let Some(args) = args {
                    let mut xs = vec![atom("args")];
                    xs.extend(args.iter().map(|a| self.arg(a)));
                    v.push(list(xs));
                }
                node("intrinsic", v)
            }
            ExprKind::Error => atom("error"),
        }
    }

    fn const_type(&self, head: &str, is_const: bool, inner: &Expr) -> Sx {
        let mut v = Vec::new();
        if is_const {
            v.push(atom("const"));
        }
        v.push(self.expr(inner));
        node(head, v)
    }

    fn tuple_elem(&self, t: &TupleElem) -> Sx {
        match &t.name {
            Some(n) => node("named", vec![name(n), self.expr(&t.value)]),
            None => self.expr(&t.value),
        }
    }

    fn arg(&self, a: &Arg) -> Sx {
        match &a.kind {
            ArgKind::Positional(e) => self.expr(e),
            ArgKind::Named(n, e) => node("named", vec![name(n), self.expr(e)]),
            ArgKind::Spread(e) => node("spread", vec![self.expr(e)]),
        }
    }

    fn if_expr(&self, i: &IfExpr) -> Sx {
        let mut v = Vec::new();
        for (cond, block) in &i.branches {
            v.push(bnode("branch", vec![self.cond(cond), self.block(block)]));
        }
        match &i.else_ {
            None => {}
            Some(IfElse::Block(b)) => v.push(node("else", vec![self.block(b)])),
            Some(IfElse::Capture { name: n, body }) => v.push(node("else-capture", vec![name(n), self.expr(body)])),
        }
        bnode("if", v)
    }

    fn arm(&self, a: &Arm) -> Sx {
        let mut pats = vec![atom("pats")];
        pats.extend(a.pats.iter().map(|p| self.pattern(p)));
        let mut v = vec![list(pats)];
        push_opt(&mut v, labeled("guard", a.guard.as_ref().map(|g| self.expr(g))));
        v.push(self.expr(&a.body));
        node("arm", v)
    }

    // --- functions and type literals ---

    fn param(&self, p: &Param) -> Sx {
        let mut ns = vec![atom("names")];
        ns.extend(p.names.iter().map(name));
        let mut v = vec![list(ns)];
        if p.comptime {
            v.push(atom("comptime"));
        }
        push_opt(&mut v, labeled("ty", p.ty.as_ref().map(|t| self.expr(t))));
        push_opt(&mut v, labeled("default", p.default.as_ref().map(|t| self.expr(t))));
        node("param", v)
    }

    fn params(&self, params: &[Param]) -> Sx {
        let mut v = vec![atom("params")];
        v.extend(params.iter().map(|p| self.param(p)));
        list(v)
    }

    fn fn_sig(&self, head: &str, sig: &FnSig, tail: Vec<Sx>) -> Sx {
        let mut v = vec![self.params(&sig.params)];
        if sig.variadic {
            v.push(atom("variadic"));
        }
        push_opt(&mut v, labeled("ret", sig.ret.as_ref().map(|t| self.expr(t))));
        push_opt(&mut v, sig.effects.as_ref().map(|e| self.effects(e)));
        v.extend(tail);
        node(head, v)
    }

    fn constraints(&self, cs: &[Constraint]) -> Option<Sx> {
        if cs.is_empty() {
            return None;
        }
        let mut v = vec![atom("where")];
        v.extend(cs.iter().map(|c| node("constraint", vec![name(&c.name), self.expr(&c.ty)])));
        Some(list(v))
    }

    fn fn_lit(&self, f: &FnLit) -> Sx {
        let mut tail = Vec::new();
        push_opt(&mut tail, self.constraints(&f.where_));
        tail.push(match &f.body {
            FnBody::Block(b) => self.block(b),
            FnBody::Expr(e) => node("=>", vec![self.expr(e)]),
        });
        self.fn_sig("fn", &f.sig, tail)
    }

    fn struct_lit(&self, s: &StructLit) -> Sx {
        let mut v = Vec::new();
        push_opt(&mut v, s.params.as_ref().map(|p| self.params(p)));
        push_opt(&mut v, self.constraints(&s.where_));
        v.extend(s.members.iter().map(|m| self.struct_member(m)));
        bnode("struct", v)
    }

    fn struct_member(&self, m: &StructMember) -> Sx {
        match m {
            StructMember::Field(f) => {
                let mut v = self.attrs(&f.attrs);
                if f.is_pub {
                    v.push(atom("pub"));
                }
                if f.is_using {
                    v.push(atom("using"));
                }
                let mut ns = vec![atom("names")];
                ns.extend(f.names.iter().map(name));
                v.push(list(ns));
                v.push(self.expr(&f.ty));
                push_opt(&mut v, labeled("default", f.default.as_ref().map(|d| self.expr(d))));
                node("field", v)
            }
            StructMember::When { cond, then, else_, .. } => {
                let mut v = vec![self.expr(cond), bnode("then", then.iter().map(|m| self.struct_member(m)).collect())];
                if let Some(e) = else_ {
                    v.push(bnode("else", e.iter().map(|m| self.struct_member(m)).collect()));
                }
                bnode("when-members", v)
            }
        }
    }

    fn union_lit(&self, u: &UnionLit) -> Sx {
        let mut v = Vec::new();
        push_opt(&mut v, labeled("tag", u.tag_type.as_ref().map(|t| self.expr(t))));
        push_opt(&mut v, u.params.as_ref().map(|p| self.params(p)));
        push_opt(&mut v, self.constraints(&u.where_));
        v.extend(u.members.iter().map(|m| self.union_member(m)));
        bnode("union", v)
    }

    fn union_member(&self, m: &UnionMember) -> Sx {
        match m {
            UnionMember::Variant(var) => {
                let mut v = vec![name(&var.name)];
                push_opt(&mut v, labeled("payload", var.payload.as_ref().map(|p| self.expr(p))));
                push_opt(&mut v, labeled("value", var.value.as_ref().map(|p| self.expr(p))));
                node("variant", v)
            }
            UnionMember::When { cond, then, else_, .. } => {
                let mut v = vec![self.expr(cond), bnode("then", then.iter().map(|m| self.union_member(m)).collect())];
                if let Some(e) = else_ {
                    v.push(bnode("else", e.iter().map(|m| self.union_member(m)).collect()));
                }
                bnode("when-members", v)
            }
        }
    }

    fn effect_lit(&self, e: &EffectLit) -> Sx {
        let mut v = Vec::new();
        push_opt(&mut v, e.params.as_ref().map(|p| self.params(p)));
        for op in &e.ops {
            let kind = match op.kind {
                OpKind::Fn => "fn",
                OpKind::Esc => "esc",
                OpKind::Ctl => "ctl",
            };
            let mut ov = vec![name(&op.name), atom(kind), self.params(&op.params)];
            push_opt(&mut ov, labeled("ret", op.ret.as_ref().map(|t| self.expr(t))));
            v.push(node("op", ov));
        }
        bnode("effect", v)
    }

    fn foreign_member(&self, m: &ForeignMember) -> Sx {
        let mut v = self.attrs(&m.attrs);
        match &m.kind {
            ForeignMemberKind::Fn { name: n, sig } => {
                v.push(name(n));
                v.push(self.fn_sig("fn-type", sig, vec![]));
                node("foreign-fn", v)
            }
            ForeignMemberKind::Global { name: n, ty } => {
                v.push(name(n));
                v.push(self.expr(ty));
                node("foreign-global", v)
            }
        }
    }

    // --- patterns ---

    fn pattern(&self, p: &Pattern) -> Sx {
        match &p.kind {
            PatternKind::Wildcard => atom("wildcard"),
            PatternKind::Bind(id) => node("bind", vec![name(id)]),
            PatternKind::Lit(e) => node("lit", vec![self.expr(e)]),
            PatternKind::Null => atom("null-pat"),
            PatternKind::Range { lo, hi, inclusive } => {
                let mut v = vec![self.expr(lo), self.expr(hi)];
                if *inclusive {
                    v.push(atom("inclusive"));
                }
                node("range", v)
            }
            PatternKind::Err(inner) => node("err", vec![self.pattern(inner)]),
            PatternKind::Path { path, args } => {
                let mut v = vec![self.expr(path)];
                if let Some(args) = args {
                    let mut xs = vec![atom("args")];
                    xs.extend(args.iter().map(|a| self.pat_arg(a)));
                    v.push(list(xs));
                }
                node("path", v)
            }
            PatternKind::Tuple(args) => node("tuple", args.iter().map(|a| self.pat_arg(a)).collect()),
        }
    }

    fn pat_arg(&self, a: &PatArg) -> Sx {
        match &a.name {
            Some(n) => node("named", vec![name(n), self.pattern(&a.pat)]),
            None => self.pattern(&a.pat),
        }
    }
}
