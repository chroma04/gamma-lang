//! The optional language areas of generated programs: generic functions and types, strings,
//! user-defined effects with handlers (including suspending `ctl` operations that run on
//! fibers), and the cooperative scheduler of `std/fiber` with its channels. Each area is
//! switched on per program by the seed, so some programs stay small and focused.
//!
//! Lifetimes need care wherever strings are involved. A string built in a buffer points into
//! that buffer, which its scope frees, so such strings are never reassigned and never
//! returned from a function.

use super::{Gen, It, Ty, U8, U32, Var, indent};
use crate::rng::Rng;

#[derive(Clone, Copy, Default)]
pub struct Features {
    pub generics: bool,
    pub strings: bool,
    pub effects: bool,
    pub fibers: bool,
}

impl Features {
    pub fn pick(rng: &mut Rng) -> Features {
        Features { generics: rng.chance(55), strings: rng.chance(50), effects: rng.chance(55), fibers: rng.chance(15) }
    }

    /// Whether any area that adds expressions and statements to ordinary code is on.
    pub fn any(&self) -> bool {
        self.generics || self.strings || self.effects
    }
}

const STRINGS: [&str; 16] = [
    "",
    "a",
    "hello",
    "Gamma",
    "hello, world",
    "0123456789",
    "zzz",
    "abc abc",
    "42",
    "-7",
    "255",
    "x1",
    "caf\u{e9}",
    "tab\\there",
    "q\\\"uote",
    "\u{65e5}\u{672c}",
];

const NEEDLES: [&str; 10] = ["a", "e", "l", "o", "ab", "hello", "12", "-", "z", " "];

/// The handlers installed around a block (see [`Gen::open_with`]).
pub struct With {
    ev: Option<String>,
    co: Option<(String, String)>,
}

impl With {
    /// `body` run under the handlers: a `with` expression.
    pub fn wrap(&self, body: &str) -> String {
        let inner = match &self.co {
            Some((h, stk)) => format!("with {h} on {stk}[..] {{ {body} }}"),
            None => body.to_string(),
        };
        match &self.ev {
            Some(h) => format!("with {h} {{ {inner} }}"),
            None => inner,
        }
    }
}

impl Gen {
    /// The effect set of a function: `Out`, `Alloc` where strings or fibers need it, and the
    /// program's own effects.
    pub(super) fn needs(&self, ev: bool, co: bool) -> String {
        let mut s = String::from("Out");
        if self.feat.strings || self.feat.fibers {
            s.push_str(" + Alloc");
        }
        if ev {
            s.push_str(" + Ev");
        }
        if co {
            s.push_str(" + Co");
        }
        s
    }

    /// Imports, generic definitions and effect declarations.
    pub(super) fn extra_decls(&self) -> String {
        let mut s = String::new();
        if self.feat.strings {
            s.push_str("str :: import \"std/str\"\n");
        }
        if self.feat.fibers {
            s.push_str("fiber :: import \"std/fiber\"\nchan :: import \"std/fiber/channel\"\n");
        }
        if self.feat.generics {
            s.push_str(
                "Pair :: struct(T: type) { a: T, b: T }\n\
                 g_pick :: fn(T: type, a: T, b: T, c: bool) -> T {\n    if c { return a }\n    return b\n}\n\
                 g_max :: fn(T: type, a: T, b: T) -> T where Ord(T) => if a > b { a } else { b }\n\
                 g_min :: fn(T: type, a: T, b: T) -> T where Ord(T) => if a < b { a } else { b }\n\
                 g_mix :: fn(T: type, a: T, b: T) -> T where Int(T) => (a ~ b) +% (a & b)\n\
                 g_fold :: fn(T: type, xs: []const T) -> T where Int(T) {\n    total: T = 0\n    for x in xs { total +%= x }\n    return total\n}\n\
                 g_twice :: fn(T: type, x: T, f: fn(T) -> T) -> T { return f(f(x)) }\n\
                 pair_sum :: fn(T: type, p: ^const Pair(T)) -> T where Int(T) => p.a +% p.b\n\
                 pair_max :: fn(T: type, p: ^const Pair(T)) -> T where Ord(T) => if p.a > p.b { p.a } else { p.b }\n\
                 pair_swap :: fn(T: type, p: ^Pair(T)) {\n    t := p.a\n    p.a = p.b\n    p.b = t\n}\n",
            );
        }
        if self.feat.effects {
            let [a, b, f] = self.ev_t;
            let [q, r] = self.co_t;
            s.push_str(&format!(
                "Ev :: effect {{\n    ev_get: fn(x: {}) -> {}\n    ev_log: fn(x: {})\n    ev_fail: esc(x: {}) -> never\n}}\n",
                a.name(),
                b.name(),
                a.name(),
                f.name()
            ));
            s.push_str(&format!(
                "Co :: effect {{\n    co_ask: ctl(q: {}) -> {}\n    co_tick: ctl()\n}}\n",
                q.name(),
                r.name()
            ));
        }
        s
    }

    // ----- expressions ------------------------------------------------------------------

    /// An integer expression from one of the optional areas, if one applies (`d` is the
    /// remaining depth for operands).
    pub(super) fn extra_int(&mut self, t: It, d: u32) -> Option<String> {
        let mut cats = vec![];
        if self.feat.generics {
            cats.push(0);
        }
        if self.feat.strings {
            cats.push(1);
        }
        if self.feat.effects && (self.ev || self.co) {
            cats.push(2);
        }
        if cats.is_empty() {
            return None;
        }
        match *self.rng.pick(&cats) {
            0 => Some(self.generic_int(t, d)),
            1 => Some(self.string_int(t, d)),
            _ => Some(self.effect_int(t, d)),
        }
    }

    fn generic_int(&mut self, t: It, d: u32) -> String {
        let n = t.name();
        // Sometimes the type argument is written out instead of inferred.
        let explicit = if self.rng.chance(25) { format!("T: {n}, ") } else { String::new() };
        match self.rng.below(7) {
            0 => {
                let (a, b, c) = (self.int_expr(t, d), self.int_expr(t, d), self.bool_expr(d));
                format!("g_pick({explicit}{a}, {b}, {c})")
            }
            1 => {
                let f = if self.rng.chance(50) { "g_max" } else { "g_min" };
                let (a, b) = (self.int_expr(t, d), self.int_expr(t, d));
                format!("{f}({explicit}{a}, {b})")
            }
            2 => {
                let (a, b) = (self.int_expr(t, d), self.int_expr(t, d));
                format!("g_mix({explicit}{a}, {b})")
            }
            3 => {
                let arrs: Vec<usize> =
                    (0..self.vars.len()).filter(|&i| matches!(self.vars[i].ty, Ty::Arr(et, _) if et == t)).collect();
                if arrs.is_empty() {
                    return self.int_expr(t, 0);
                }
                let Var { name, ty: Ty::Arr(_, len), .. } = &self.vars[*self.rng.pick(&arrs)] else { unreachable!() };
                let (name, len) = (name.clone(), *len);
                let lo = self.rng.below(len);
                let hi = lo + self.rng.below(len - lo + 1);
                format!("g_fold({explicit}{name}[{lo}..{hi}])")
            }
            4 => {
                // A closure that captures variables, called twice by the generic function.
                let x = self.int_expr(t, d);
                let e = self.int_expr(t, d.min(1));
                let op = *self.rng.pick(&["+%", "-%", "~", "*%"]);
                let v = self.fresh("cv");
                format!("g_twice({explicit}{x}, fn({v}) => ({v} {op} {e}))")
            }
            _ => {
                let pairs = self.vars_of(&Ty::Gp(t));
                if pairs.is_empty() {
                    return self.int_expr(t, 0);
                }
                let v = self.vars[*self.rng.pick(&pairs)].name.clone();
                let f = if self.rng.chance(50) { "pair_sum" } else { "pair_max" };
                format!("{f}(&{v})")
            }
        }
    }

    /// A `usize` expression converted to `t`.
    fn usize_to(&mut self, e: String, t: It) -> String {
        if self.rng.chance(50) { format!("({e}).trunc({})", t.name()) } else { format!("({e}).sat({})", t.name()) }
    }

    fn string_int(&mut self, t: It, d: u32) -> String {
        let s = self.str_leaf();
        match self.rng.below(7) {
            0 => self.usize_to(format!("{s}.len"), t),
            1 => {
                let fb = self.lit(U8);
                let m = if self.rng.chance(50) { "first" } else { "last" };
                let e = format!("({s}.{m}() else {fb})");
                self.conv(U8, t, e)
            }
            2 => {
                let i = self.int_expr(U8, d.min(1));
                let fb = self.lit(U8);
                let e = format!("({s}.get({i}) else {fb})");
                self.conv(U8, t, e)
            }
            3 => {
                let nd = self.needle();
                let k = self.rng.below(5);
                self.usize_to(format!("(str.find({s}, {nd}) else {k}usize)"), t)
            }
            4 => {
                let fb = self.int_expr(t, 0);
                format!("(str.parse_int({}, {s}) else {fb})", t.name())
            }
            5 => {
                let nd = self.needle();
                self.usize_to(format!("str.count({s}, {nd})"), t)
            }
            _ => {
                let c = self.int_expr(U8, d.min(1));
                self.usize_to(format!("({s}.index_of({c}) else 0usize)"), t)
            }
        }
    }

    fn effect_int(&mut self, t: It, d: u32) -> String {
        let use_co = self.co && (!self.ev || self.rng.chance(40));
        if use_co {
            let [q, r] = self.co_t;
            let a = self.int_expr(q, d.min(2));
            let e = format!("co_ask({a})");
            self.conv(r, t, e)
        } else {
            let [a, b, _] = self.ev_t;
            let x = self.int_expr(a, d.min(2));
            let e = format!("ev_get({x})");
            self.conv(b, t, e)
        }
    }

    fn needle(&mut self) -> String {
        format!("\"{}\"", self.rng.pick(&NEEDLES))
    }

    fn str_lit(&mut self) -> String {
        format!("\"{}\"", self.rng.pick(&STRINGS))
    }

    /// A variable or a literal: cheap to evaluate twice.
    fn str_leaf(&mut self) -> String {
        let vars = self.vars_of(&Ty::Str);
        if !vars.is_empty() && self.rng.chance(60) {
            return self.vars[*self.rng.pick(&vars)].name.clone();
        }
        self.str_lit()
    }

    pub(super) fn str_expr(&mut self, d: u32) -> String {
        if d == 0 || self.rng.chance(30) {
            return self.str_leaf();
        }
        let d1 = d - 1;
        match self.rng.below(6) {
            0 | 1 => {
                // A slice that is always in bounds.
                let v = self.str_leaf();
                let k = self.rng.below(8);
                if self.rng.chance(50) {
                    format!("({v}[0..({k}usize).min({v}.len)])")
                } else {
                    format!("({v}[({k}usize).min({v}.len)..{v}.len])")
                }
            }
            2 => {
                let c = self.bool_expr(d1);
                let (a, b) = (self.str_expr(d1), self.str_expr(d1));
                format!("(if {c} {{ {a} }} else {{ {b} }})")
            }
            3 => {
                let v = self.str_leaf();
                let nd = self.needle();
                format!("(str.strip_prefix({v}, {nd}) else {v})")
            }
            4 => {
                let v = self.str_leaf();
                format!("str.trim({v})")
            }
            _ => self.str_leaf(),
        }
    }

    pub(super) fn str_bool(&mut self, d: u32) -> String {
        let (a, b) = (self.str_expr(d), self.str_expr(d));
        match self.rng.below(8) {
            0 => format!("({a} is {b})"),
            1 => format!("({a} is not {b})"),
            2..=4 => format!("({a} {} {b})", self.rng.pick(&["<", "<=", ">", ">="])),
            5 => format!("({a}).starts_with({b})"),
            6 => format!("({a}).ends_with({b})"),
            _ => {
                let nd = self.needle();
                format!("str.contains({a}, {nd})")
            }
        }
    }

    pub(super) fn gp_expr(&mut self, t: It, d: u32) -> String {
        let ty = Ty::Gp(t);
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
        let (a, b) = (self.int_expr(t, d1), self.int_expr(t, d1));
        format!("Pair({})(a: {a}, b: {b})", t.name())
    }

    // ----- statements -------------------------------------------------------------------

    /// A statement from one of the optional areas. Returns whether it wrote one.
    pub(super) fn extra_stmt(&mut self, out: &mut String, level: usize, nested_ok: bool) -> bool {
        let ind = indent(level);
        let mut opts: Vec<u8> = vec![];
        if self.feat.strings && nested_ok {
            opts.extend([0, 0]);
        }
        if self.feat.effects {
            if self.ev {
                opts.extend([1, 2]);
            }
            if self.co {
                opts.push(3);
            }
            if nested_ok {
                opts.extend([4, 4, 4]);
            }
        }
        if opts.is_empty() {
            return false;
        }
        match *self.rng.pick(&opts) {
            0 => self.string_builder(out, level),
            1 => {
                let e = self.int_expr(self.ev_t[0], 2);
                out.push_str(&format!("{ind}ev_log({e})\n"));
            }
            2 => {
                let c = self.bool_expr(2);
                let e = self.int_expr(self.ev_t[2], 2);
                out.push_str(&format!("{ind}if {c} {{ ev_fail({e}) }}\n"));
            }
            3 => out.push_str(&format!("{ind}co_tick()\n")),
            _ => self.with_stmt(out, level),
        }
        true
    }

    /// Builds a string in a growable buffer, with `write` calls and templates, and binds it.
    fn string_builder(&mut self, out: &mut String, level: usize) {
        let ind = indent(level);
        let sb = self.fresh("sb");
        out.push_str(&format!("{ind}{sb} := list(u8)\n{ind}defer {sb}.deinit()\n"));
        for _ in 0..1 + self.rng.below(3) {
            let looped = self.rng.chance(35);
            let (mark, saved_mult) = (self.vars.len(), self.mult);
            let mut ind2 = ind.clone();
            if looped {
                let k = 1 + self.rng.below(3);
                let i = self.fresh("bi");
                out.push_str(&format!("{ind}for {i} in 0..{k}u8 {{\n"));
                self.push_var(i, Ty::Int(U8), false);
                self.mult = self.mult.saturating_mul(k as u64);
                ind2 = indent(level + 1);
            }
            self.cost += self.mult;
            let mut tmpl = String::new();
            let mut args = vec![];
            for _ in 0..1 + self.rng.below(3) {
                let ty = self.rand_ty();
                let spec = match ty {
                    Ty::Int(_) | Ty::Str | Ty::Bool if self.rng.chance(40) => {
                        *self.rng.pick(&["{:>6}", "{:<5}", "{:^7}", "{:04}", "[{}]"])
                    }
                    _ => "{}",
                };
                // `{:04}` pads numbers only.
                let spec = if spec == "{:04}" && !matches!(ty, Ty::Int(_)) { "{}" } else { spec };
                tmpl.push_str(spec);
                tmpl.push_str(self.rng.pick(&["", "-", ",", " ", "/"]));
                args.push(self.expr(&ty, 2));
            }
            out.push_str(&format!("{ind2}{sb}.write(\"{tmpl}\", {}) else e => {{}}\n", args.join(", ")));
            if looped {
                out.push_str(&format!("{ind}}}\n"));
                self.mult = saved_mult;
            }
            self.vars.truncate(mark);
        }
        let v = self.fresh("v");
        out.push_str(&format!("{ind}{v}: string = {sb}.items\n"));
        self.push_var(v, Ty::Str, false);
    }

    /// Declares the handler for `Ev`, which counts its calls in a captured variable.
    fn open_ev(&mut self, out: &mut String, level: usize, rty: &Ty) -> String {
        let ind = indent(level);
        let (c, h) = (self.fresh("cn"), self.fresh("hd"));
        out.push_str(&format!("{ind}{c}: u32 = 0\n"));
        self.push_var(c.clone(), Ty::Int(U32), false);
        let [a, b, f] = self.ev_t;
        let (x, y, z) = (self.fresh("q"), self.fresh("q"), self.fresh("q"));
        out.push_str(&format!("{ind}{h} :: handler Ev {{\n"));
        self.push_var(x.clone(), Ty::Int(a), false);
        let e = self.int_expr(b, 2);
        self.vars.pop();
        out.push_str(&format!("{ind}    ev_get({x}) => {{\n{ind}        {c} += 1\n{ind}        {e}\n{ind}    }}\n"));
        out.push_str(&format!("{ind}    ev_log({y}) => {{ print(\"log {{}} {{}}\\n\", {y}, {c}) }}\n"));
        self.push_var(z.clone(), Ty::Int(f), false);
        let r = self.expr(rty, 2);
        self.vars.pop();
        out.push_str(&format!(
            "{ind}    ev_fail({z}) => {{\n{ind}        print(\"fail {{}}\\n\", {z})\n{ind}        break {r}\n{ind}    }}\n{ind}}}\n"
        ));
        h
    }

    /// Declares a stack and the handler for `Co`: it answers questions, and sometimes
    /// cancels the suspended code and leaves the `with` instead of resuming it.
    fn open_co(&mut self, out: &mut String, level: usize, rty: &Ty) -> (String, String) {
        let ind = indent(level);
        let (stk, n, h) = (self.fresh("stk"), self.fresh("nn"), self.fresh("hc"));
        out.push_str(&format!("{ind}{stk}: [131072]u8 = ---\n{ind}{n}: u32 = 0\n"));
        self.push_var(n.clone(), Ty::Int(U32), false);
        // The clauses run on the driver's stack and cannot suspend.
        let saved_co = std::mem::replace(&mut self.co, false);
        let [q, r] = self.co_t;
        let qv = self.fresh("q");
        let limit = 1 + self.rng.below(4);
        out.push_str(&format!("{ind}{h} :: handler Co {{\n"));
        self.push_var(qv.clone(), Ty::Int(q), false);
        let res = self.int_expr(r, 2);
        self.vars.pop();
        out.push_str(&format!("{ind}    co_ask({qv}, k) => {{\n{ind}        {n} += 1\n"));
        if self.rng.chance(50) {
            let brk = self.expr(rty, 2);
            out.push_str(&format!(
                "{ind}        if {n} > {limit} {{\n{ind}            cancel k\n{ind}            break {brk}\n{ind}        }}\n"
            ));
        }
        out.push_str(&format!("{ind}        resume k({res})\n{ind}    }}\n"));
        out.push_str(&format!("{ind}    co_tick(k) => {{\n{ind}        {n} += 1\n"));
        if self.rng.chance(40) {
            out.push_str(&format!("{ind}        print(\"tick {{}}\\n\", {n})\n"));
        }
        if self.rng.chance(25) {
            let brk = self.expr(rty, 2);
            out.push_str(&format!(
                "{ind}        if {n} > {} {{\n{ind}            cancel k\n{ind}            break {brk}\n{ind}        }}\n",
                limit + 3
            ));
        }
        out.push_str(&format!("{ind}        resume k()\n{ind}    }}\n{ind}}}\n"));
        self.co = saved_co;
        (h, stk)
    }

    /// Declares the handlers for the wanted effects. `rty` is the type of the `with`
    /// expression, which handlers that exit produce a value of. The caller then generates
    /// the body with the effects enabled and passes it to [`With::wrap`].
    pub(super) fn open_with(&mut self, out: &mut String, level: usize, ev: bool, co: bool, rty: &Ty) -> With {
        let ev = ev.then(|| self.open_ev(out, level, rty));
        let co = co.then(|| self.open_co(out, level, rty));
        With { ev, co }
    }

    /// `r: T = with handlers { statements; value }`.
    fn with_stmt(&mut self, out: &mut String, level: usize) {
        let ind = indent(level);
        let want_co = self.in_main && !self.co && self.rng.chance(50);
        let want_ev = !want_co || self.rng.chance(40);
        let rty = self.rand_ret_ty();
        let w = self.open_with(out, level, want_ev, want_co, &rty);
        let saved = (self.ev, self.co, self.ret.take(), self.loops);
        self.ev |= want_ev;
        self.co |= want_co;
        self.loops = 0;
        let mark = self.vars.len();
        self.block_depth += 1;
        let mut body = String::new();
        for _ in 0..1 + self.rng.below(3) {
            self.stmt(&mut body, level + 1);
        }
        let e = self.expr(&rty, 2);
        body.push_str(&format!("{}{e}\n", indent(level + 1)));
        self.block_depth -= 1;
        self.vars.truncate(mark);
        (self.ev, self.co, self.ret, self.loops) = saved;
        let name = self.fresh("r");
        out.push_str(&format!("{ind}{name}: {} = {}\n", rty.name(), w.wrap(&format!("\n{body}{ind}"))));
        self.push_var(name, rty, true);
    }

    // ----- fibers -----------------------------------------------------------------------

    /// The task functions of the fiber blocks: a producer and a consumer talking through a
    /// channel, under the round-robin scheduler of `std/fiber`.
    pub(super) fn fiber_decls(&mut self) -> String {
        if !self.feat.fibers {
            return String::new();
        }
        let it = self.fib_it;
        let mut text = format!(
            "Task :: fiber.Sched(Out + Alloc)\nFSTACK :: 65536\nChan :: chan.Channel({})\nJob :: struct {{\n    id: u8\n    ch: ?^Chan\n    acc: {} = 0\n}}\n",
            it.name(),
            it.name()
        );
        for producer in [true, false] {
            let n = self.funcs.len();
            self.begin_function(None, n, false, false);
            self.push_var("j.acc".into(), Ty::Int(it), false);
            let (head, mult) = if producer {
                let k = 2 + self.rng.below(4);
                (
                    format!(
                        "producer :: fn(j: ^Job) needs Task + Out + Alloc {{\n    out := j.ch?\n    defer out.close()\n    for i in 0..{k}u8 {{\n"
                    ),
                    k as u64,
                )
            } else {
                ("consumer :: fn(j: ^Job) needs Task + Out + Alloc {\n    inp := j.ch?\n    while x := inp.recv() {\n".to_string(), 4)
            };
            text.push_str(&head);
            self.push_var(
                if producer { "i".into() } else { "x".into() },
                Ty::Int(if producer { U8 } else { it }),
                false,
            );
            self.mult = mult;
            self.loops = 1;
            let e = self.int_expr(it, 3);
            text.push_str(&format!("        j.acc = {e}\n"));
            if producer {
                let v = self.fresh("fv");
                let e = self.int_expr(it, 2);
                text.push_str(&format!(
                    "        {v}: {} = {e}\n        print(\"p{{}} sends {{}}\\n\", j.id, {v})\n        _ = out.send({v})\n",
                    it.name()
                ));
            } else {
                text.push_str("        print(\"c{} got {}\\n\", j.id, x)\n");
            }
            if self.rng.chance(60) {
                text.push_str("        fiber.yield_now()\n");
            }
            for _ in 0..self.rng.below(3) {
                self.stmt(&mut text, 2);
            }
            text.push_str("    }\n}\n");
        }
        text
    }

    /// Runs one or two producer/consumer pairs under a scheduler.
    pub(super) fn fiber_blocks(&mut self, out: &mut String) {
        if !self.feat.fibers {
            return;
        }
        let it = self.fib_it.name();
        for _ in 0..1 + self.rng.below(2) {
            let pairs = 1 + self.rng.below(2);
            let sc = self.fresh("sched");
            let ms = self.fresh("ms");
            out.push_str(&format!("    {ms}: [FSTACK]u8 = ---\n"));
            let mut spawns = vec![];
            let mut report = vec![];
            for p in 0..pairs {
                let (c, a, b, s1, s2) =
                    (self.fresh("ch"), self.fresh("ja"), self.fresh("jb"), self.fresh("st"), self.fresh("st"));
                let cap = 1 + self.rng.below(3);
                out.push_str(&format!(
                    "    {c} := chan.channel({it}, {cap}) else trap(\"alloc\")\n    defer {c}.deinit()\n    {a} := Job(id: {}, ch: &{c})\n    {b} := Job(id: {}, ch: &{c})\n    {s1}: [FSTACK]u8 = ---\n    {s2}: [FSTACK]u8 = ---\n",
                    2 * p + 1,
                    2 * p + 2
                ));
                let first = format!("fiber.spawn(producer, &{a}, {s1}[..])");
                let second = format!("fiber.spawn(consumer, &{b}, {s2}[..])");
                if self.rng.chance(50) {
                    spawns.push(first);
                    spawns.push(second);
                } else {
                    spawns.push(second);
                    spawns.push(first);
                }
                report.push(format!("{a}.acc"));
                report.push(format!("{b}.acc"));
            }
            out.push_str(&format!("    {sc} := fiber.scheduler(Out + Alloc)\n    defer {sc}.deinit()\n"));
            out.push_str(&format!("    with &{sc} on {ms}[..] {{\n"));
            for sp in spawns {
                out.push_str(&format!("        {sp}\n"));
                if self.rng.chance(30) {
                    out.push_str("        fiber.yield_now()\n");
                }
            }
            let e = self.int_expr(self.fib_it, 2);
            out.push_str(&format!("        print(\"main {{}}\\n\", {e})\n        fiber.join()\n    }}\n"));
            let fmt = vec!["{}"; report.len()].join(" ");
            out.push_str(&format!("    print(\"fibers {fmt}\\n\", {})\n", report.join(", ")));
        }
    }
}
