//! Generates a random, well-typed, terminating Gamma program from a seed.
//!
//! Every expression is generated for a requested type and fully parenthesized, so the
//! operator-mixing rules of the language never apply. Termination comes from construction:
//! loops have constant bounds, a function calls only functions generated before it, and
//! recursion is bounded by a depth argument. A running cost estimate keeps nested loops
//! and calls from multiplying into long runs. Checked operations may trap; the point of the
//! test is that every back end traps in the same way.

use crate::rng::Rng;

mod extras;

use extras::Features;

/// An integer type.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct It {
    bits: u32,
    signed: bool,
}

const INTS: [It; 8] = [
    It { bits: 8, signed: true },
    It { bits: 16, signed: true },
    It { bits: 32, signed: true },
    It { bits: 64, signed: true },
    It { bits: 8, signed: false },
    It { bits: 16, signed: false },
    It { bits: 32, signed: false },
    It { bits: 64, signed: false },
];

/// 128-bit integers, which are picked less often.
const WIDE: [It; 2] = [It { bits: 128, signed: true }, It { bits: 128, signed: false }];

const U8: It = INTS[4];
const U32: It = INTS[6];

impl It {
    fn name(self) -> String {
        format!("{}{}", if self.signed { 'i' } else { 'u' }, self.bits)
    }

    /// The largest magnitude of a non-negative value.
    fn max_mag(self) -> u128 {
        match (self.signed, self.bits) {
            (true, b) => (1u128 << (b - 1)) - 1,
            (false, 128) => u128::MAX,
            (false, b) => (1u128 << b) - 1,
        }
    }

    /// Whether `T(x)` is a lossless conversion from `from`.
    fn widens_from(self, from: It) -> bool {
        (self.signed == from.signed && from.bits <= self.bits) || (!from.signed && self.signed && from.bits < self.bits)
    }
}

#[derive(Clone, PartialEq, Debug)]
enum Ty {
    Int(It),
    Bool,
    /// The struct `P`.
    Pt,
    Arr(It, usize),
    Opt(It),
    /// `Er!T`.
    Res(It),
    /// The union `Sh`.
    Sh,
    /// A tuple of two integers.
    Tup(It, It),
    /// `f32` or `f64`, by width.
    Flt(u32),
    /// A string.
    Str,
    /// The generic struct `Pair(T)`.
    Gp(It),
}

impl Ty {
    fn name(&self) -> String {
        match self {
            Ty::Int(t) => t.name(),
            Ty::Bool => "bool".into(),
            Ty::Pt => "P".into(),
            Ty::Arr(t, n) => format!("[{}]{}", n, t.name()),
            Ty::Opt(t) => format!("?{}", t.name()),
            Ty::Res(t) => format!("Er!{}", t.name()),
            Ty::Sh => "Sh".into(),
            Ty::Tup(a, b) => format!("({}, {})", a.name(), b.name()),
            Ty::Flt(b) => format!("f{b}"),
            Ty::Str => "string".into(),
            Ty::Gp(t) => format!("Pair({})", t.name()),
        }
    }
}

struct Var {
    name: String,
    ty: Ty,
    /// Whether generated code may assign to it (loop counters and bindings may not).
    assignable: bool,
}

struct Func {
    name: String,
    params: Vec<Ty>,
    ret: Ty,
    /// Estimated work of one call.
    cost: u64,
    /// The first parameter is a recursion depth.
    recursive: bool,
    /// Performs (so needs) the effect `Ev` / `Co`.
    ev: bool,
    co: bool,
}

const COST_LIMIT: u64 = 6000;

pub struct Gen {
    rng: Rng,
    funcs: Vec<Func>,
    /// Percentage of arithmetic operations that are the plain, trapping kind.
    trap_bias: usize,
    p_fields: [It; 3],
    sh_circle: It,
    sh_rect: [It; 2],
    er_bad: It,
    // State of the function being generated.
    vars: Vec<Var>,
    counter: usize,
    cost: u64,
    mult: u64,
    loops: usize,
    block_depth: usize,
    /// The return type of the function, or `None` in `main`.
    ret: Option<Ty>,
    /// Functions `0..callable` may be called.
    callable: usize,
    /// Which of the optional language areas this program uses.
    feat: Features,
    /// Operand types of the effects `Ev` and `Co`.
    ev_t: [It; 3],
    co_t: [It; 2],
    /// Whether the code being generated may perform `Ev` / `Co` operations.
    ev: bool,
    co: bool,
    in_main: bool,
    /// Element type of the channels of the fiber blocks.
    fib_it: It,
}

fn pick_int(rng: &mut Rng) -> It {
    if rng.chance(8) { *rng.pick(&WIDE) } else { *rng.pick(&INTS) }
}

fn indent(n: usize) -> String {
    "    ".repeat(n)
}

impl Gen {
    pub fn program(seed: u64) -> String {
        let mut rng = Rng::new(seed);
        let trap_bias = *rng.pick(&[0, 0, 1, 3, 10, 40]);
        let pick_it = pick_int;
        let p_fields = [pick_it(&mut rng), pick_it(&mut rng), pick_it(&mut rng)];
        let sh_circle = pick_it(&mut rng);
        let sh_rect = [pick_it(&mut rng), pick_it(&mut rng)];
        let er_bad = pick_it(&mut rng);
        let feat = Features::pick(&mut rng);
        let ev_t = [pick_it(&mut rng), pick_it(&mut rng), pick_it(&mut rng)];
        let co_t = [pick_it(&mut rng), pick_it(&mut rng)];
        let fib_it = pick_it(&mut rng);
        let mut g = Gen {
            rng,
            funcs: vec![],
            trap_bias,
            p_fields,
            sh_circle,
            sh_rect,
            er_bad,
            vars: vec![],
            counter: 0,
            cost: 0,
            mult: 1,
            loops: 0,
            block_depth: 0,
            ret: None,
            callable: 0,
            feat,
            ev_t,
            co_t,
            ev: false,
            co: false,
            in_main: false,
            fib_it,
        };
        let mut out = String::new();
        out.push_str(&format!("// difftest seed {seed}\n"));
        out.push_str(&format!(
            "P :: struct {{ a: {}, b: {}, c: {} }}\n",
            p_fields[0].name(),
            p_fields[1].name(),
            p_fields[2].name()
        ));
        out.push_str(&format!(
            "Sh :: union {{ Circle: {}, Rect: (w: {}, h: {}), Empty }}\n",
            sh_circle.name(),
            sh_rect[0].name(),
            sh_rect[1].name()
        ));
        out.push_str(&format!("Er :: union {{ Bad: {}, Worse }}\n", er_bad.name()));
        out.push_str(&g.extra_decls());
        out.push('\n');
        // Constructors of results: the variant follows the first argument.
        for t in INTS.iter().chain(&WIDE).copied() {
            let bad = g.conv(U8, er_bad, "k".to_string());
            out.push_str(&format!(
                "mk_{n} :: fn(k: u8, v: {n}) -> Er!{n} {{\n    if k % 4 is 0 {{ return !.Worse }}\n    if k % 4 is 1 {{ return !.Bad({bad}) }}\n    return v\n}}\n",
                n = t.name()
            ));
        }
        for t in INTS.iter().chain(&WIDE).copied() {
            out.push_str(&format!(
                "sum_{n} :: fn(xs: []const {n}) -> {n} {{\n    total: {n} = 0\n    for x in xs {{ total +%= x }}\n    return total\n}}\n",
                n = t.name()
            ));
        }
        out.push('\n');
        let nfuncs = 2 + g.rng.below(6);
        for i in 0..nfuncs {
            let text = g.function(i);
            out.push_str(&text);
            out.push('\n');
        }
        out.push_str(&g.fiber_decls());
        out.push_str(&g.main_fn());
        out
    }

    // ----- types and leaves -------------------------------------------------------------

    fn any_int(&mut self) -> It {
        pick_int(&mut self.rng)
    }

    /// A type a function may return: no strings, whose storage the function might free.
    fn rand_ret_ty(&mut self) -> Ty {
        loop {
            let t = self.rand_ty();
            if t != Ty::Str {
                return t;
            }
        }
    }

    fn rand_ty(&mut self) -> Ty {
        if self.feat.generics && self.rng.chance(9) {
            return Ty::Gp(self.any_int());
        }
        if self.feat.strings && self.rng.chance(7) {
            return Ty::Str;
        }
        match self.rng.below(100) {
            0..=54 => Ty::Int(self.any_int()),
            55..=64 => Ty::Bool,
            65..=71 => Ty::Pt,
            72..=79 => {
                let t = self.any_int();
                Ty::Arr(t, 1 + self.rng.below(5))
            }
            80..=87 => Ty::Opt(self.any_int()),
            88..=92 => Ty::Res(self.any_int()),
            93..=94 => Ty::Sh,
            95..=96 => Ty::Tup(self.any_int(), self.any_int()),
            _ => Ty::Flt(*self.rng.pick(&[32, 64])),
        }
    }

    fn fresh(&mut self, prefix: &str) -> String {
        self.counter += 1;
        format!("{}{}", prefix, self.counter)
    }

    fn lit(&mut self, t: It) -> String {
        let max = t.max_mag();
        // A sign and a magnitude, since a 128-bit value does not fit a wider type.
        let (neg, mag): (bool, u128) = match self.rng.below(20) {
            0..=7 => (false, self.rng.below(10) as u128),
            8 | 9 if t.signed => (true, self.rng.below(10) as u128),
            8..=11 => (false, (self.rng.below(200) as u128).min(max)),
            12 if t.signed => (true, max + 1),
            12 => (false, 0),
            13 => (false, max),
            14 => (false, max - 1),
            15 if t.signed => (true, max),
            15 => (false, 1),
            16 if t.signed => (true, 1),
            16 => (false, 1),
            _ => {
                let r = ((self.rng.next() as u128) << 64) | self.rng.next() as u128;
                let mag = if max == u128::MAX { r } else { r % (max + 1) };
                (t.signed && self.rng.chance(50), mag)
            }
        };
        let name = t.name();
        if neg && mag == max + 1 {
            return format!("@min_value({name})");
        }
        if !neg && mag == max && t.signed && self.rng.chance(60)
            || !neg && mag == max && !t.signed && self.rng.chance(60)
        {
            return format!("@max_value({name})");
        }
        if neg && mag > 0 { format!("(-{mag}{name})") } else { format!("({mag}{name})") }
    }

    /// Whether `e` mentions a variable in scope (so that it is not a compile-time constant).
    fn mentions_var(&self, e: &str) -> bool {
        e.split(|c: char| !(c.is_alphanumeric() || c == '_')).any(|w| self.vars.iter().any(|v| v.name == w))
    }

    fn vars_of(&self, ty: &Ty) -> Vec<usize> {
        self.vars.iter().enumerate().filter(|(_, v)| &v.ty == ty).map(|(i, _)| i).collect()
    }

    fn assignable_vars(&self) -> Vec<usize> {
        self.vars.iter().enumerate().filter(|(_, v)| v.assignable).map(|(i, _)| i).collect()
    }

    fn funcs_returning(&self, ty: &Ty) -> Vec<usize> {
        (0..self.callable)
            .filter(|&i| {
                let f = &self.funcs[i];
                &f.ret == ty && (!f.ev || self.ev) && (!f.co || self.co)
            })
            .collect()
    }

    /// An expression of integer type `from` converted to `to`.
    fn conv(&mut self, from: It, to: It, e: String) -> String {
        if from == to {
            return e;
        }
        if to.widens_from(from) {
            return format!("{}({})", to.name(), e);
        }
        match self.rng.below(3) {
            0 => format!("{}.trunc({})", e, to.name()),
            1 => format!("{}.sat({})", e, to.name()),
            _ => {
                let fb = self.lit(to);
                format!("({}.checked({}) else {})", e, to.name(), fb)
            }
        }
    }

    /// A call of a function returning `ty`, if one exists and the cost budget allows.
    fn call(&mut self, ty: &Ty, d: u32) -> Option<String> {
        let cands = self.funcs_returning(ty);
        if cands.is_empty() || d == 0 {
            return None;
        }
        let i = *self.rng.pick(&cands);
        let c = self.funcs[i].cost.saturating_mul(self.mult);
        if self.cost + c > COST_LIMIT {
            return None;
        }
        self.cost += c;
        let name = self.funcs[i].name.clone();
        let params = self.funcs[i].params.clone();
        let recursive = self.funcs[i].recursive;
        let mut args = vec![];
        for (k, p) in params.iter().enumerate() {
            if recursive && k == 0 {
                args.push(format!("({}u32)", self.rng.below(5)));
            } else {
                args.push(self.expr(p, d - 1));
            }
        }
        Some(format!("{}({})", name, args.join(", ")))
    }

    // ----- expressions ------------------------------------------------------------------

    fn expr(&mut self, ty: &Ty, d: u32) -> String {
        match ty {
            Ty::Int(t) => self.int_expr(*t, d),
            Ty::Bool => self.bool_expr(d),
            Ty::Pt => self.pt_expr(d),
            Ty::Arr(t, n) => self.arr_expr(*t, *n, d),
            Ty::Opt(t) => self.opt_expr(*t, d),
            Ty::Res(t) => self.res_expr(*t, d),
            Ty::Sh => self.sh_expr(d),
            Ty::Tup(a, b) => self.tup_expr(*a, *b, d),
            Ty::Flt(b) => self.flt_expr(*b, d),
            Ty::Str => self.str_expr(d),
            Ty::Gp(t) => self.gp_expr(*t, d),
        }
    }

    fn int_leaf(&mut self, t: It) -> String {
        let same = self.vars_of(&Ty::Int(t));
        match self.rng.below(10) {
            0..=3 if !same.is_empty() => self.vars[*self.rng.pick(&same)].name.clone(),
            4 | 5 => {
                // A field or element of the right type, or another integer variable converted.
                let mut c: Vec<(Option<It>, String)> = vec![];
                for v in &self.vars {
                    match &v.ty {
                        Ty::Pt => {
                            for (k, f) in ["a", "b", "c"].iter().enumerate() {
                                if self.p_fields[k] == t {
                                    c.push((None, format!("{}.{}", v.name, f)));
                                }
                            }
                        }
                        Ty::Arr(et, n) if *et == t => {
                            c.push((None, format!("{}[{}]", v.name, self.rng.below(*n))));
                        }
                        Ty::Gp(et) if *et == t => {
                            c.push((None, format!("{}.a", v.name)));
                            c.push((None, format!("{}.b", v.name)));
                        }
                        Ty::Tup(a, b) => {
                            if *a == t {
                                c.push((None, format!("{}.0", v.name)));
                            }
                            if *b == t {
                                c.push((None, format!("{}.1", v.name)));
                            }
                        }
                        Ty::Int(s) if *s != t => c.push((Some(*s), v.name.clone())),
                        _ => {}
                    }
                }
                if c.is_empty() {
                    return self.lit(t);
                }
                let (from, text) = c[self.rng.below(c.len())].clone();
                match from {
                    Some(s) => self.conv(s, t, text),
                    None => text,
                }
            }
            _ => self.lit(t),
        }
    }

    fn int_expr(&mut self, t: It, d: u32) -> String {
        if d == 0 || self.rng.chance(12) {
            return self.int_leaf(t);
        }
        let d1 = d - 1;
        if self.feat.any()
            && self.rng.chance(10)
            && let Some(e) = self.extra_int(t, d1)
        {
            return e;
        }
        let name = t.name();
        match self.rng.below(100) {
            0..=29 => {
                let a = self.int_expr(t, d1);
                let b = self.int_expr(t, d1);
                let op = *self.rng.pick(&["+", "-", "*"]);
                if self.rng.chance(self.trap_bias) {
                    format!("({a} {op} {b})")
                } else {
                    match self.rng.below(3) {
                        0 => format!("({a} {op}% {b})"),
                        1 => format!("({a} {op}| {b})"),
                        _ => {
                            let fb = self.int_expr(t, 0);
                            format!("({a} {op}? {b} else {fb})")
                        }
                    }
                }
            }
            30..=37 => {
                let a = self.int_expr(t, d1);
                let b = self.int_expr(t, d1);
                let op = *self.rng.pick(&["/", "%"]);
                if self.rng.chance(self.trap_bias) {
                    format!("({a} {op} {b})")
                } else {
                    format!("({a} {op} ({b} | 1))")
                }
            }
            38..=47 => {
                let a = self.int_expr(t, d1);
                let b = self.int_expr(t, d1);
                let op = *self.rng.pick(&["&", "|", "~"]);
                format!("({a} {op} {b})")
            }
            48..=57 => {
                let a = self.int_expr(t, d1);
                let amount = self.int_expr(U8, d1);
                let masked =
                    if self.rng.chance(self.trap_bias.min(10)) { amount } else { format!("({amount} % {}u8)", t.bits) };
                match self.rng.below(5) {
                    0 => format!("({a} >> {masked})"),
                    1 if self.rng.chance(self.trap_bias) => format!("({a} << {masked})"),
                    1 => format!("({a} <<% {masked})"),
                    2 => format!("({a} <<% {masked})"),
                    3 => format!("({a} <<| {masked})"),
                    _ => {
                        let fb = self.int_expr(t, 0);
                        format!("({a} <<? {masked} else {fb})")
                    }
                }
            }
            58..=62 => {
                let a = self.int_expr(t, d1);
                if t.signed && self.rng.chance(self.trap_bias) {
                    format!("(-{a})")
                } else if self.rng.chance(50) {
                    format!("(~{a})")
                } else {
                    format!("({} -% {a})", self.lit(t))
                }
            }
            63..=75 if self.rng.chance(25) => {
                let bits = *self.rng.pick(&[32, 64]);
                let f = self.flt_expr(bits, d1);
                match self.rng.below(5) {
                    0 if self.rng.chance(self.trap_bias) => format!("{f}.trunc({name})"),
                    0 | 1 => format!("{f}.sat({name})"),
                    2 if self.rng.chance(self.trap_bias) => {
                        format!("{f}.{}({name})", self.rng.pick(&["floor", "ceil", "round"]))
                    }
                    2 => format!("{f}.sat({name})"),
                    _ => {
                        let fb = self.int_expr(t, 0);
                        format!("({f}.checked({name}) else {fb})")
                    }
                }
            }
            63..=75 => {
                let s = self.any_int();
                let e = self.int_expr(s, d1);
                self.conv(s, t, e)
            }
            76..=80 => {
                let c = self.bool_expr(d1);
                let a = self.int_expr(t, d1);
                let b = self.int_expr(t, d1);
                format!("(if {c} {{ {a} }} else {{ {b} }})")
            }
            81..=84 => match self.call(&Ty::Int(t), d1) {
                Some(c) => c,
                None => self.int_leaf(t),
            },
            85 | 86 => {
                let arrs: Vec<usize> =
                    (0..self.vars.len()).filter(|&i| matches!(self.vars[i].ty, Ty::Arr(et, _) if et == t)).collect();
                if arrs.is_empty() {
                    return self.int_leaf(t);
                }
                let Var { name: a, ty: Ty::Arr(_, n), .. } = &self.vars[*self.rng.pick(&arrs)] else { unreachable!() };
                let (a, n) = (a.clone(), *n);
                match self.rng.below(3) {
                    0 => format!("{a}.min()?"),
                    1 => format!("{a}.max()?"),
                    _ => {
                        let lo = self.rng.below(n);
                        let hi = lo + self.rng.below(n - lo + 1);
                        format!("sum_{name}({a}[{lo}..{hi}])")
                    }
                }
            }
            87..=90 => {
                let o = self.opt_expr(t, d1);
                if self.rng.chance(self.trap_bias.min(10)) {
                    format!("({o})?")
                } else {
                    let fb = self.int_expr(t, d1);
                    format!("({o} else {fb})")
                }
            }
            91..=93 => {
                let r = self.res_expr(t, d1);
                let fb = self.int_expr(t, d1);
                format!("({r} else {fb})")
            }
            94..=96 => {
                let s = self.sh_expr(d1);
                let (c, w, h) = (self.fresh("m"), self.fresh("m"), self.fresh("m"));
                let a = self.conv(self.sh_circle, t, c.clone());
                let b = self.conv(self.sh_rect[0], t, w.clone());
                let b2 = self.conv(self.sh_rect[1], t, h.clone());
                let e = self.int_expr(t, d1);
                format!("(match {s} {{ .Circle({c}) => {a}; .Rect({w}, {h}) => ({b} +% {b2}); .Empty => {e} }})")
            }
            _ => {
                let r = self.res_expr(t, d1);
                let (v, bad) = (self.fresh("m"), self.fresh("m"));
                let a = self.conv(self.er_bad, t, bad.clone());
                let w = self.int_expr(t, d1);
                let _ = name;
                format!("(match {r} {{ !.Bad({bad}) => {a}; !.Worse => {w}; {v} => {v} }})")
            }
        }
    }

    fn bool_expr(&mut self, d: u32) -> String {
        let vars = self.vars_of(&Ty::Bool);
        if d == 0 || self.rng.chance(10) {
            if !vars.is_empty() && self.rng.chance(70) {
                return self.vars[*self.rng.pick(&vars)].name.clone();
            }
            return (if self.rng.chance(50) { "true" } else { "false" }).to_string();
        }
        let d1 = d - 1;
        if self.feat.strings && self.rng.chance(10) {
            return self.str_bool(d1);
        }
        match self.rng.below(100) {
            0..=44 => {
                let t = self.any_int();
                let a = self.int_expr(t, d1);
                let b = self.int_expr(t, d1);
                let op = *self.rng.pick(&["is", "is not", "<", "<=", ">", ">="]);
                format!("({a} {op} {b})")
            }
            45..=52 => {
                let a = self.bool_expr(d1);
                format!("(not {a})")
            }
            53..=64 => {
                let a = self.bool_expr(d1);
                let b = self.bool_expr(d1);
                format!("({a} {} {b})", if self.rng.chance(50) { "and" } else { "or" })
            }
            65..=70 => {
                let t = self.any_int();
                let a = self.int_expr(t, d1);
                let lo = self.int_expr(t, 0);
                let hi = self.int_expr(t, 0);
                format!("({a} in {lo}..={hi})")
            }
            71..=76 => {
                let t = self.any_int();
                let o = self.opt_expr(t, d1);
                format!("({o} {} null)", if self.rng.chance(50) { "is" } else { "is not" })
            }
            77..=81 => {
                let t = self.any_int();
                let r = self.res_expr(t, d1);
                format!("({r} {} !_)", if self.rng.chance(50) { "is" } else { "is not" })
            }
            82..=86 => {
                let a = self.pt_expr(d1);
                let b = self.pt_expr(d1);
                format!("({a} {} {b})", if self.rng.chance(50) { "is" } else { "is not" })
            }
            87..=90 => {
                let t = self.any_int();
                let n = 1 + self.rng.below(4);
                let a = self.arr_expr(t, n, d1);
                let b = self.arr_expr(t, n, d1);
                format!("({a} is {b})")
            }
            91..=92 if self.rng.chance(50) => {
                let bits = *self.rng.pick(&[32, 64]);
                let a = self.flt_expr(bits, d1);
                let b = self.flt_expr(bits, d1);
                let op = *self.rng.pick(&["<", "<=", ">", ">="]);
                format!("({a} {op} {b})")
            }
            91..=92 => {
                let s = self.sh_expr(d1);
                format!("({s} is .Empty)")
            }
            93 => {
                let (x, y) = (self.any_int(), self.any_int());
                let a = self.tup_expr(x, y, d1);
                let b = self.tup_expr(x, y, d1);
                // The right side of `is` would be read as a pattern.
                match *self.rng.pick(&["==", "!=", "<", "<=", ">", ">="]) {
                    "==" => format!("(({a} <= {b}) and ({a} >= {b}))"),
                    "!=" => format!("(({a} < {b}) or ({a} > {b}))"),
                    op => format!("({a} {op} {b})"),
                }
            }
            _ => match self.call(&Ty::Bool, d1) {
                Some(c) => c,
                None => self.bool_expr(0),
            },
        }
    }

    fn pt_expr(&mut self, d: u32) -> String {
        let vars = self.vars_of(&Ty::Pt);
        let r = self.rng.below(10);
        if (d == 0 || r < 3) && !vars.is_empty() {
            return self.vars[*self.rng.pick(&vars)].name.clone();
        }
        let d1 = d.saturating_sub(1);
        if r == 3
            && let Some(c) = self.call(&Ty::Pt, d1)
        {
            return c;
        }
        if r == 4 && !vars.is_empty() {
            let v = self.vars[*self.rng.pick(&vars)].name.clone();
            let k = self.rng.below(3);
            let e = self.int_expr(self.p_fields[k], d1);
            return format!("P(..{}, {}: {})", v, ["a", "b", "c"][k], e);
        }
        let f: Vec<String> = (0..3).map(|k| self.int_expr(self.p_fields[k], d1)).collect();
        format!("P(a: {}, b: {}, c: {})", f[0], f[1], f[2])
    }

    fn arr_expr(&mut self, t: It, n: usize, d: u32) -> String {
        let ty = Ty::Arr(t, n);
        let vars = self.vars_of(&ty);
        let r = self.rng.below(10);
        if (d == 0 || r < 3) && !vars.is_empty() {
            return self.vars[*self.rng.pick(&vars)].name.clone();
        }
        let d1 = d.saturating_sub(1);
        match r {
            3 => {
                if let Some(c) = self.call(&ty, d1) {
                    return c;
                }
            }
            4 | 5 => {
                let a = self.arr_expr(t, n, d1);
                let b = self.arr_expr(t, n, d1);
                let op = if self.rng.chance(self.trap_bias) {
                    *self.rng.pick(&["+", "-", "*"])
                } else {
                    *self.rng.pick(&["+%", "-%", "*%", "+|"])
                };
                return format!("({a} {op} {b})");
            }
            6 => {
                let e = self.int_expr(t, d1);
                return format!("[{n}]{}(fill: {e})", t.name());
            }
            _ => {}
        }
        let items: Vec<String> = (0..n).map(|_| self.int_expr(t, d1)).collect();
        format!("[{n}]{}({})", t.name(), items.join(", "))
    }

    fn opt_expr(&mut self, t: It, d: u32) -> String {
        let ty = Ty::Opt(t);
        let vars = self.vars_of(&ty);
        let r = self.rng.below(10);
        if (d == 0 || r < 2) && !vars.is_empty() {
            return self.vars[*self.rng.pick(&vars)].name.clone();
        }
        let d1 = d.saturating_sub(1);
        match r {
            2 => {
                if let Some(c) = self.call(&ty, d1) {
                    return c;
                }
            }
            3 | 4 => {
                let a = self.int_expr(t, d1);
                let b = self.int_expr(t, d1);
                let op = *self.rng.pick(&["+?", "-?", "*?"]);
                return format!("({a} {op} {b})");
            }
            5 => {
                let s = self.any_int();
                if s != t {
                    let e = self.int_expr(s, d1);
                    return format!("{e}.checked({})", t.name());
                }
            }
            6 => {
                let arrs: Vec<usize> =
                    (0..self.vars.len()).filter(|&i| matches!(self.vars[i].ty, Ty::Arr(et, _) if et == t)).collect();
                if !arrs.is_empty() {
                    let a = self.vars[*self.rng.pick(&arrs)].name.clone();
                    let i = self.int_expr(U8, d1);
                    let m = *self.rng.pick(&["first", "last", "min", "max"]);
                    return if self.rng.chance(50) { format!("{a}.get({i})") } else { format!("{a}.{m}()") };
                }
            }
            7 => return format!("?{}(null)", t.name()),
            _ => {}
        }
        let e = self.int_expr(t, d1);
        format!("?{}({})", t.name(), e)
    }

    fn res_expr(&mut self, t: It, d: u32) -> String {
        let ty = Ty::Res(t);
        let vars = self.vars_of(&ty);
        let r = self.rng.below(10);
        if (d == 0 || r < 3) && !vars.is_empty() {
            return self.vars[*self.rng.pick(&vars)].name.clone();
        }
        let d1 = d.saturating_sub(1);
        if let Some(c) = self.call(&ty, d1) {
            return c;
        }
        // No function returns it: build one with the helper of that type.
        let e = self.int_expr(t, d1);
        let k = self.int_expr(U8, d1);
        format!("mk_{}({k}, {e})", t.name())
    }

    fn flt_lit(&mut self, bits: u32) -> String {
        let name = format!("f{bits}");
        let v = match self.rng.below(14) {
            0 => "0.0".to_string(),
            1 => "1.0".to_string(),
            2 => "1.5".to_string(),
            3 => "-2.25".to_string(),
            4 => "0.1".to_string(),
            5 => "3.14159".to_string(),
            6 => "1e-5".to_string(),
            7 => "123456789.0".to_string(),
            8 => (if bits == 32 { "3.0e38" } else { "1.7e308" }).to_string(),
            9 => (if bits == 32 { "1.0e-40" } else { "5.0e-324" }).to_string(),
            10 => "-0.0".to_string(),
            11 => format!("{}.{}", self.rng.below(1000), self.rng.below(1000)),
            12 => format!("-{}.{}e{}", self.rng.below(10), self.rng.below(100), self.rng.below(30)),
            _ => format!("{}.5", self.rng.below(70000)),
        };
        if let Some(rest) = v.strip_prefix('-') { format!("(-{rest}{name})") } else { format!("({v}{name})") }
    }

    fn flt_expr(&mut self, bits: u32, d: u32) -> String {
        let ty = Ty::Flt(bits);
        let vars = self.vars_of(&ty);
        if d == 0 || self.rng.chance(12) {
            if !vars.is_empty() && self.rng.chance(60) {
                return self.vars[*self.rng.pick(&vars)].name.clone();
            }
            return self.flt_lit(bits);
        }
        let d1 = d - 1;
        let name = format!("f{bits}");
        match self.rng.below(100) {
            0..=44 => {
                let a = self.flt_expr(bits, d1);
                let b = self.flt_expr(bits, d1);
                let op = *self.rng.pick(&["+", "-", "*", "/"]);
                format!("({a} {op} {b})")
            }
            45..=52 => format!("(-{})", self.flt_expr(bits, d1)),
            53..=70 => {
                let t = self.any_int();
                let e = self.int_expr(t, d1);
                let exact = t.bits <= if bits == 32 { 16 } else { 32 };
                if exact && self.rng.chance(60) { format!("{name}({e})") } else { format!("{e}.to({name})") }
            }
            71..=78 => {
                let other = if bits == 32 { 64 } else { 32 };
                let e = self.flt_expr(other, d1);
                if bits == 64 { format!("f64({e})") } else { format!("{e}.to(f32)") }
            }
            79..=86 => {
                let c = self.bool_expr(d1);
                let a = self.flt_expr(bits, d1);
                let b = self.flt_expr(bits, d1);
                format!("(if {c} {{ {a} }} else {{ {b} }})")
            }
            87..=92 => match self.call(&ty, d1) {
                Some(c) => c,
                None => self.flt_lit(bits),
            },
            _ => self.flt_lit(bits),
        }
    }

    fn tup_expr(&mut self, a: It, b: It, d: u32) -> String {
        let ty = Ty::Tup(a, b);
        let vars = self.vars_of(&ty);
        let r = self.rng.below(10);
        if (d == 0 || r < 3) && !vars.is_empty() {
            return self.vars[*self.rng.pick(&vars)].name.clone();
        }
        let d1 = d.saturating_sub(1);
        if r == 3
            && let Some(c) = self.call(&ty, d1)
        {
            return c;
        }
        format!("({}, {})", self.int_expr(a, d1), self.int_expr(b, d1))
    }

    fn sh_expr(&mut self, d: u32) -> String {
        let vars = self.vars_of(&Ty::Sh);
        let r = self.rng.below(10);
        if (d == 0 || r < 3) && !vars.is_empty() {
            return self.vars[*self.rng.pick(&vars)].name.clone();
        }
        let d1 = d.saturating_sub(1);
        if r == 3
            && let Some(c) = self.call(&Ty::Sh, d1)
        {
            return c;
        }
        match self.rng.below(3) {
            0 => format!("Sh.Circle({})", self.int_expr(self.sh_circle, d1)),
            1 => {
                let w = self.int_expr(self.sh_rect[0], d1);
                let h = self.int_expr(self.sh_rect[1], d1);
                format!("Sh.Rect(w: {w}, h: {h})")
            }
            _ => "Sh.Empty".to_string(),
        }
    }

    // ----- statements -------------------------------------------------------------------

    fn expr_depth(&mut self) -> u32 {
        1 + self.rng.below(4) as u32
    }

    fn block(&mut self, out: &mut String, level: usize, count: usize) {
        let mark = self.vars.len();
        self.block_depth += 1;
        for _ in 0..count {
            self.stmt(out, level);
        }
        self.block_depth -= 1;
        self.vars.truncate(mark);
    }

    fn push_var(&mut self, name: String, ty: Ty, assignable: bool) {
        self.vars.push(Var { name, ty, assignable });
    }

    fn stmt(&mut self, out: &mut String, level: usize) {
        let ind = indent(level);
        self.cost += self.mult;
        let nested_ok = self.block_depth < 4 && self.cost < COST_LIMIT;
        let loop_ok = nested_ok && self.loops < 2;
        if self.feat.any() && self.rng.chance(12) && self.extra_stmt(out, level, nested_ok) {
            return;
        }
        match self.rng.below(100) {
            0..=21 => {
                let ty = self.rand_ty();
                let d = self.expr_depth();
                let e = self.expr(&ty, d);
                let name = self.fresh("v");
                if self.rng.chance(85) || !self.mentions_var(&e) {
                    out.push_str(&format!("{ind}{name}: {} = {e}\n", ty.name()));
                    // A string may point into a buffer that its scope frees, so it is never reassigned.
                    let assignable = ty != Ty::Str;
                    self.push_var(name, ty, assignable);
                } else {
                    out.push_str(&format!("{ind}{name}: {} : {e}\n", ty.name()));
                    self.push_var(name, ty, false);
                }
            }
            22..=41 => {
                let cands = self.assignable_vars();
                if cands.is_empty() {
                    return;
                }
                let vi = *self.rng.pick(&cands);
                let name = self.vars[vi].name.clone();
                let ty = self.vars[vi].ty.clone();
                let d = self.expr_depth();
                match ty {
                    Ty::Int(t) => {
                        let e = self.int_expr(t, d);
                        if self.rng.chance(50) {
                            let ops = ["+%=", "-%=", "*%=", "+|=", "-|=", "*|=", "&=", "|=", "~="];
                            let op = if self.rng.chance(self.trap_bias) {
                                *self.rng.pick(&["+=", "-=", "*="])
                            } else {
                                *self.rng.pick(&ops)
                            };
                            out.push_str(&format!("{ind}{name} {op} {e}\n"));
                        } else {
                            out.push_str(&format!("{ind}{name} = {e}\n"));
                        }
                    }
                    Ty::Tup(a, b) if self.rng.chance(50) => {
                        let (k, t) = if self.rng.chance(50) { (0, a) } else { (1, b) };
                        let e = self.int_expr(t, d);
                        out.push_str(&format!("{ind}{name}.{k} = {e}\n"));
                    }
                    Ty::Pt if self.rng.chance(50) => {
                        let k = self.rng.below(3);
                        let e = self.int_expr(self.p_fields[k], d);
                        let f = ["a", "b", "c"][k];
                        let op = if self.rng.chance(self.trap_bias.min(40)) { "+=" } else { "=" };
                        out.push_str(&format!("{ind}{name}.{f} {op} {e}\n"));
                    }
                    Ty::Gp(t) => match self.rng.below(3) {
                        0 => out.push_str(&format!("{ind}pair_swap(&{name})\n")),
                        1 => {
                            let e = self.int_expr(t, d);
                            out.push_str(&format!("{ind}{name}.a = {e}\n"));
                        }
                        _ => {
                            let e = self.expr(&Ty::Gp(t), d);
                            out.push_str(&format!("{ind}{name} = {e}\n"));
                        }
                    },
                    Ty::Arr(t, n) => match self.rng.below(5) {
                        0 => out.push_str(&format!("{ind}{name}[..].sort()\n")),
                        1 => {
                            let (i, j) = (self.rng.below(n), self.rng.below(n));
                            out.push_str(&format!("{ind}{name}.swap({i}, {j})\n"));
                        }
                        2 => {
                            let e = self.int_expr(t, d);
                            out.push_str(&format!("{ind}{name}.fill({e})\n"));
                        }
                        _ => {
                            let e = self.int_expr(t, d);
                            let i = self.int_expr(U8, 1);
                            out.push_str(&format!("{ind}{name}[({i} % {n}u8)] = {e}\n"));
                        }
                    },
                    ty => {
                        let e = self.expr(&ty, d);
                        out.push_str(&format!("{ind}{name} = {e}\n"));
                    }
                }
            }
            42..=61 => {
                let n = 1 + self.rng.below(3);
                let mut fmt = vec![];
                let mut args = vec![];
                for _ in 0..n {
                    let ty = self.rand_ty();
                    let d = self.expr_depth();
                    args.push(self.expr(&ty, d));
                    fmt.push("{}");
                }
                out.push_str(&format!("{ind}print(\"{}\\n\", {})\n", fmt.join(" "), args.join(", ")));
            }
            62..=70 if nested_ok => {
                let c = self.bool_expr(3);
                out.push_str(&format!("{ind}if {c} {{\n"));
                let n = 1 + self.rng.below(3);
                self.block(out, level + 1, n);
                if self.rng.chance(50) {
                    out.push_str(&format!("{ind}}} else {{\n"));
                    let n = 1 + self.rng.below(3);
                    self.block(out, level + 1, n);
                }
                out.push_str(&format!("{ind}}}\n"));
            }
            71..=76 if loop_ok => {
                let k = 1 + self.rng.below(5) as u64;
                let counter = self.fresh("w");
                out.push_str(&format!("{ind}{counter}: u8 = 0\n"));
                out.push_str(&format!("{ind}while {counter} < {k}u8 {{\n"));
                out.push_str(&format!("{ind}    {counter} += 1\n"));
                self.push_var(counter, Ty::Int(U8), false);
                self.loop_body(out, level + 1, k);
                self.vars.pop();
                out.push_str(&format!("{ind}}}\n"));
            }
            77..=82 if loop_ok => {
                let k = 1 + self.rng.below(6) as u64;
                let t = *self.rng.pick(&[U8, U32, INTS[2], INTS[7]]);
                let i = self.fresh("it");
                let range =
                    if self.rng.chance(50) { format!("0..{k}{}", t.name()) } else { format!("1..={k}{}", t.name()) };
                out.push_str(&format!("{ind}for {i} in {range} {{\n"));
                self.push_var(i, Ty::Int(t), false);
                self.loop_body(out, level + 1, k);
                self.vars.pop();
                out.push_str(&format!("{ind}}}\n"));
            }
            83..=86 if loop_ok => {
                let arrs: Vec<usize> =
                    (0..self.vars.len()).filter(|&i| matches!(self.vars[i].ty, Ty::Arr(..))).collect();
                if arrs.is_empty() {
                    return;
                }
                let a = *self.rng.pick(&arrs);
                let Var { name, ty: Ty::Arr(t, n), .. } = &self.vars[a] else { return };
                let (name, t, n) = (name.clone(), *t, *n);
                let x = self.fresh("x");
                out.push_str(&format!("{ind}for {x} in {name} {{\n"));
                self.push_var(x, Ty::Int(t), false);
                self.loop_body(out, level + 1, n as u64);
                self.vars.pop();
                out.push_str(&format!("{ind}}}\n"));
            }
            87..=90 if nested_ok => {
                let t = self.any_int();
                let o = self.opt_expr(t, 2);
                let b = self.fresh("b");
                out.push_str(&format!("{ind}if {b} := {o} {{\n"));
                self.push_var(b, Ty::Int(t), false);
                let n = 1 + self.rng.below(2);
                self.block(out, level + 1, n);
                self.vars.pop();
                if self.rng.chance(50) {
                    out.push_str(&format!("{ind}}} else {{\n"));
                    let n = 1 + self.rng.below(2);
                    self.block(out, level + 1, n);
                }
                out.push_str(&format!("{ind}}}\n"));
            }
            91..=94 if nested_ok => {
                let s = self.sh_expr(2);
                let (c, w, h) = (self.fresh("c"), self.fresh("w"), self.fresh("h"));
                out.push_str(&format!("{ind}match {s} {{\n"));
                out.push_str(&format!("{ind}    .Circle({c}) => {{\n"));
                self.push_var(c, Ty::Int(self.sh_circle), false);
                let n = 1 + self.rng.below(2);
                self.block(out, level + 2, n);
                self.vars.pop();
                out.push_str(&format!("{ind}    }}\n{ind}    .Rect({w}, {h}) => {{\n"));
                self.push_var(w, Ty::Int(self.sh_rect[0]), false);
                self.push_var(h, Ty::Int(self.sh_rect[1]), false);
                let n = 1 + self.rng.below(2);
                self.block(out, level + 2, n);
                self.vars.pop();
                self.vars.pop();
                out.push_str(&format!("{ind}    }}\n{ind}    .Empty => {{\n"));
                self.block(out, level + 2, 1);
                out.push_str(&format!("{ind}    }}\n{ind}}}\n"));
            }
            95..=97 if nested_ok => {
                let t = self.any_int();
                let r = self.res_expr(t, 2);
                let (b, v) = (self.fresh("e"), self.fresh("r"));
                out.push_str(&format!("{ind}match {r} {{\n"));
                out.push_str(&format!("{ind}    !.Bad({b}) => {{\n"));
                self.push_var(b, Ty::Int(self.er_bad), false);
                let n = 1 + self.rng.below(2);
                self.block(out, level + 2, n);
                self.vars.pop();
                out.push_str(&format!("{ind}    }}\n{ind}    !.Worse => {{\n"));
                self.block(out, level + 2, 1);
                out.push_str(&format!("{ind}    }}\n{ind}    {v} => {{\n"));
                self.push_var(v, Ty::Int(t), false);
                let n = 1 + self.rng.below(2);
                self.block(out, level + 2, n);
                self.vars.pop();
                out.push_str(&format!("{ind}    }}\n{ind}}}\n"));
            }
            98 if nested_ok => {
                // Deferred statements run when the enclosing block ends, also on a trap.
                let ty = self.rand_ty();
                // A deferred statement cannot suspend.
                let saved_co = std::mem::replace(&mut self.co, false);
                let e = self.expr(&ty, 2);
                self.co = saved_co;
                out.push_str(&format!("{ind}defer print(\"defer {{}}\\n\", {e})\n"));
            }
            99 if nested_ok => {
                let (x, y) = (self.any_int(), self.any_int());
                let e = self.tup_expr(x, y, 3);
                let (n1, n2) = (self.fresh("d"), self.fresh("d"));
                out.push_str(&format!("{ind}{n1}, {n2} := {e}\n"));
                self.push_var(n1, Ty::Int(x), true);
                self.push_var(n2, Ty::Int(y), true);
            }
            _ => self.early_exit(out, level),
        }
    }

    /// An `if` that leaves the loop or the function (with a value fitting its return type),
    /// or a `try` that propagates absence or an error.
    fn early_exit(&mut self, out: &mut String, level: usize) {
        let ind = indent(level);
        let c = self.bool_expr(3);
        if self.loops > 0 && self.rng.chance(50) {
            let kw = if self.rng.chance(50) { "break" } else { "continue" };
            out.push_str(&format!("{ind}if {c} {{ {kw} }}\n"));
            return;
        }
        let Some(ret) = self.ret.clone() else { return };
        match ret {
            Ty::Opt(_) if self.rng.chance(60) => {
                if self.rng.chance(50) {
                    out.push_str(&format!("{ind}if {c} {{ return null }}\n"));
                } else {
                    let u = self.any_int();
                    let o = self.opt_expr(u, 2);
                    let tgt = self.fresh("o");
                    out.push_str(&format!("{ind}{tgt}: {} = try {o}\n", u.name()));
                    self.push_var(tgt, Ty::Int(u), true);
                }
            }
            Ty::Res(_) if self.rng.chance(80) => match self.rng.below(3) {
                0 => out.push_str(&format!("{ind}if {c} {{ return !.Worse }}\n")),
                1 => {
                    let e = self.int_expr(self.er_bad, 2);
                    out.push_str(&format!("{ind}if {c} {{ return !.Bad({e}) }}\n"));
                }
                _ => {
                    let u = self.any_int();
                    let r = self.res_expr(u, 2);
                    let tgt = self.fresh("o");
                    out.push_str(&format!("{ind}{tgt}: {} = try {r}\n", u.name()));
                    self.push_var(tgt, Ty::Int(u), true);
                }
            },
            ty => {
                let e = self.expr(&ty, 3);
                out.push_str(&format!("{ind}if {c} {{ return {e} }}\n"));
            }
        }
    }

    fn loop_body(&mut self, out: &mut String, level: usize, k: u64) {
        let saved = self.mult;
        self.mult = self.mult.saturating_mul(k);
        self.loops += 1;
        let n = 1 + self.rng.below(4);
        self.block(out, level, n);
        self.loops -= 1;
        self.mult = saved;
    }

    // ----- functions --------------------------------------------------------------------

    fn begin_function(&mut self, ret: Option<Ty>, callable: usize, ev: bool, co: bool) {
        self.ev = ev;
        self.co = co;
        self.vars.clear();
        self.cost = 1;
        self.mult = 1;
        self.loops = 0;
        self.block_depth = 0;
        self.ret = ret;
        self.callable = callable;
    }

    fn function(&mut self, index: usize) -> String {
        let name = format!("f{index}");
        let ret = self.rand_ret_ty();
        let recursive = matches!(ret, Ty::Int(_)) && self.rng.chance(25);
        let ev = self.feat.effects && self.rng.chance(30);
        let co = self.feat.effects && self.rng.chance(15);
        self.begin_function(Some(ret.clone()), index, ev, co);
        let mut params = vec![];
        let mut sig = vec![];
        if recursive {
            sig.push("d: u32".to_string());
            params.push(Ty::Int(U32));
            self.push_var("d".into(), Ty::Int(U32), false);
        }
        for k in 0..self.rng.below(4) {
            let ty = self.rand_ty();
            let pname = format!("p{k}");
            sig.push(format!("{pname}: {}", ty.name()));
            self.push_var(pname, ty.clone(), false);
            params.push(ty);
        }
        let needs = self.needs(ev, co);
        let mut text = format!("{name} :: fn({}) -> {} needs {needs} {{\n", sig.join(", "), ret.name());
        if recursive {
            let e = self.expr(&ret, 3);
            text.push_str(&format!("    if d is 0 {{ return {e} }}\n"));
        }
        let n = 2 + self.rng.below(6);
        for _ in 0..n {
            self.stmt(&mut text, 1);
        }
        if let (true, Ty::Int(t)) = (recursive, &ret) {
            let t = *t;
            let mut acc = vec![];
            for _ in 0..1 + self.rng.below(2) {
                let mut args = vec!["d - 1".to_string()];
                for p in &params[1..] {
                    args.push(self.expr(p, 3));
                }
                let r = self.fresh("rec");
                text.push_str(&format!("    {r}: {} = {name}({})\n", t.name(), args.join(", ")));
                acc.push(r);
            }
            let first = self.int_expr(t, 2);
            let combined = acc.iter().fold(first, |a, r| format!("({a} +% {r})"));
            text.push_str(&format!("    return {combined}\n"));
        } else {
            let e = self.expr(&ret, 4);
            text.push_str(&format!("    return {e}\n"));
        }
        text.push_str("}\n");
        let cost = if recursive { self.cost.saturating_mul(32) } else { self.cost };
        self.funcs.push(Func { name, params, ret, cost, recursive, ev, co });
        text
    }

    fn main_fn(&mut self) -> String {
        let n = self.funcs.len();
        self.begin_function(None, n, false, false);
        self.in_main = true;
        let needs = self.needs(false, false);
        let mut text = format!("main :: fn() needs {needs} {{\n");
        for _ in 0..(3 + self.rng.below(6)) {
            self.stmt(&mut text, 1);
        }
        self.fiber_blocks(&mut text);
        // Print the result of every function at least once.
        for i in 0..n {
            let params = self.funcs[i].params.clone();
            let recursive = self.funcs[i].recursive;
            let (ev, co) = (self.funcs[i].ev, self.funcs[i].co);
            let ret = self.funcs[i].ret.clone();
            // A function that performs effects is called under handlers for them.
            let w = self.open_with(&mut text, 1, ev, co, &ret);
            (self.ev, self.co) = (ev, co);
            let mut args = vec![];
            for (k, p) in params.iter().enumerate() {
                if recursive && k == 0 {
                    args.push(format!("({}u32)", self.rng.below(6)));
                } else {
                    args.push(self.expr(p, 2));
                }
            }
            (self.ev, self.co) = (false, false);
            let name = self.funcs[i].name.clone();
            let call = w.wrap(&format!("{}({})", name, args.join(", ")));
            text.push_str(&format!("    print(\"{}: {{}}\\n\", {})\n", name, call));
        }
        text.push_str("}\n");
        text
    }
}
