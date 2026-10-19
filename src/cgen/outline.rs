//! Long runs of statements moved into C functions of their own.
//!
//! C compilers take time superlinear in the size of one function, so a Gamma function
//! with thousands of statements compiles slowly as one C function, even when every
//! statement is simple. In a function whose body is long (see [`long_body`]), the
//! statements of a long block are dealt out in runs to helper functions `gm_cN_K`, each
//! called where its run was.
//!
//! A helper shares the frame: it gets its address, and inside it `F` is a macro for
//! `(*gm_Fp)`, so a statement's code is the same as it would be inline. The function's
//! locals then all live in the frame (`Gen::frame_locals`). The few C variables that
//! statements read besides (`env`, `cx`, `inst`, `gm_out`, the backtrace record `B`,
//! the node of an enclosing `with` and the parameters read in place) are passed along.
//!
//! An exit from a run jumps to a label outside the helper (a rung of the ladder, a
//! loop's end, the function's return): the helper returns a number for that label
//! instead, and the caller jumps there. Only [`FnGen::jump`] emits such jumps, so it
//! decides which ones leave the helper.

use super::CResult;
use super::func::FnGen;
use crate::tir::{Expr, FnDef, Stmt};
use std::fmt::Write;

/// The size of a function body, in expression nodes, above which its long blocks are
/// split into helpers. The largest functions of ordinary programs stay below it.
const LONG_BODY: u32 = 1_500;

/// The most expression nodes in one run of statements moved into a helper. A helper of
/// that size has some 1,000 C names, well below `GM_LONG_FN`'s threshold.
const RUN_WEIGHT: u32 = 400;

/// Runs this small are left inline: a call would cost more than it saves.
const MIN_RUN: u32 = RUN_WEIGHT / 8;

/// A helper being generated: the depth of the scope stack where its run starts (scopes
/// below it are outside the helper) and the labels outside it that its code jumps to,
/// with the index of their scope (`None` for the function's own labels).
pub struct Run {
    depth: usize,
    exits: Vec<(String, Option<usize>)>,
}

/// True if `def`'s body is long enough to split into helpers (see the module docs).
pub fn long_body(def: &FnDef) -> bool {
    def.body.as_ref().is_some_and(|b| weight(b, LONG_BODY + 1) > LONG_BODY)
}

/// The number of expression nodes in `e`, counting up to `cap` at most.
fn weight(e: &Expr, cap: u32) -> u32 {
    let mut n = 1;
    e.for_each_child(&mut |c| {
        if n < cap {
            n += weight(c, cap - n);
        }
    });
    n.min(cap)
}

/// The number of expression nodes in statement `s`, counting up to `cap` at most.
fn stmt_weight(s: &Stmt, cap: u32) -> u32 {
    let mut n = 1;
    let mut add = |e: &Expr| {
        if n < cap {
            n += weight(e, cap - n);
        }
    };
    match s {
        Stmt::Let { init, .. } => init.iter().for_each(add),
        Stmt::LetTuple { init, .. } => add(init),
        Stmt::Assign { place, value } => {
            add(place);
            add(value);
        }
        Stmt::AssignTuple { places, value } => {
            places.iter().flatten().for_each(&mut add);
            add(value);
        }
        Stmt::Expr(e) | Stmt::Defer { body: e, .. } => add(e),
    }
    n.min(cap)
}

impl<'g, 'a> FnGen<'g, 'a> {
    /// Jumps to C label `label`, whose scope has index `scope` in the scope stack (`None`
    /// for a label of the function itself: the ladder's last rung, the return).
    pub fn jump(&mut self, label: &str, scope: Option<usize>) {
        if let Some(run) = self.runs.last_mut()
            && scope.is_none_or(|i| i < run.depth)
        {
            let k = match run.exits.iter().position(|(l, _)| l == label) {
                Some(k) => k,
                None => {
                    run.exits.push((label.to_string(), scope));
                    run.exits.len() - 1
                }
            };
            self.line(&format!("return {};", k + 1));
        } else {
            self.line(&format!("goto {label};"));
        }
    }

    /// Generates `f` with no helper in progress and with the scope stack set aside, as
    /// the code of a deferred statement or of a function of its own is: its jumps are
    /// all within it.
    pub fn without_runs<T>(&mut self, f: impl FnOnce(&mut Self) -> T) -> T {
        let saved = std::mem::take(&mut self.runs);
        let r = f(self);
        self.runs = saved;
        r
    }

    /// The statements of a block, each in a C block of its own (so the C compiler can
    /// reuse their temporaries' stack space), runs of them in helpers in a long function.
    pub fn stmt_list(&mut self, stmts: &'a [Stmt]) -> CResult<()> {
        if !self.split_runs || stmts.len() < 2 {
            return stmts.iter().try_for_each(|s| self.stmt_block(s));
        }
        let weights: Vec<u32> = stmts.iter().map(|s| stmt_weight(s, RUN_WEIGHT + 1)).collect();
        if weights.iter().map(|&w| w as u64).sum::<u64>() <= RUN_WEIGHT as u64 {
            return stmts.iter().try_for_each(|s| self.stmt_block(s));
        }
        let mut i = 0;
        while i < stmts.len() {
            // A statement too big for a run stays here: its own blocks are split.
            if weights[i] > RUN_WEIGHT {
                self.stmt_block(&stmts[i])?;
                i += 1;
                continue;
            }
            let start = i;
            let mut total = 0;
            while i < stmts.len() && weights[i] <= RUN_WEIGHT && total + weights[i] <= RUN_WEIGHT {
                total += weights[i];
                i += 1;
            }
            if total < MIN_RUN {
                stmts[start..i].iter().try_for_each(|s| self.stmt_block(s))?;
            } else {
                self.outline_run(&stmts[start..i])?;
            }
        }
        Ok(())
    }

    /// Moves the statements `stmts` into a helper and calls it (see the module docs).
    fn outline_run(&mut self, stmts: &'a [Stmt]) -> CResult<()> {
        let k = self.fresh();
        let name = format!("gm_c{}_{k}", self.tag);
        let outer_cx = self.cx.clone();
        let own_cx = outer_cx == "cx";
        let saved_out = std::mem::take(&mut self.out);
        let saved_indent = std::mem::replace(&mut self.indent, 1);
        let saved_names = std::mem::take(&mut self.names);
        if !own_cx {
            self.cx = "gm_hcx".into();
        }
        self.runs.push(Run { depth: self.scopes.len(), exits: vec![] });
        let r = stmts.iter().try_for_each(|s| self.stmt_block(s));
        self.end_lines();
        let run = self.runs.pop().unwrap();
        let body = std::mem::replace(&mut self.out, saved_out);
        let names = std::mem::replace(&mut self.names, saved_names);
        self.indent = saved_indent;
        self.cx = outer_cx.clone();
        r?;

        let fid = self.fid.0;
        let mut params = vec![format!("struct gm_fr{fid} *gm_Fp"), "void *env".into(), "gm_node *cx".into()];
        params.push("gm_node *inst".into());
        let mut args = vec!["&F".to_string(), "env".into(), "cx".into(), "inst".into()];
        // A `with … on` body (`outlined`) is a function without `gm_out` or parameters.
        if !self.outlined {
            if self.g.ty.by_ref(self.def.ret) {
                params.push("void *gm_out".into());
                args.push("gm_out".into());
            }
            for (i, p) in self.def.params.iter().enumerate() {
                if self.borrowed[i] {
                    let t = self.def.locals[p.index()].ty;
                    params.push(format!("{} a{i}", self.g.ty.param_cty(t)));
                    args.push(format!("a{i}"));
                }
            }
        }
        let debug = self.g.opts.debug;
        if debug {
            params.push("gm_bt *gm_Bp".into());
            args.push("&B".into());
        }
        if !own_cx {
            params.push("gm_node *gm_hcx".into());
            args.push(outer_cx);
        }
        let mut attrs = String::from("GM_NOINLINE ");
        if self.def.hints.fast_math {
            attrs.push_str("GM_FAST_MATH_ATTR ");
        }
        attrs.push_str(super::func::size_attrs(names));
        let head = format!("static {attrs}int {name}({})", params.join(", "));
        let _ = writeln!(self.g.protos, "{head};");
        let mut text = format!("{head} {{\n#define F (*gm_Fp)\n");
        if debug {
            text.push_str("#define B (*gm_Bp)\n");
        }
        if self.def.hints.fast_math {
            text.push_str("    GM_FAST_MATH_BODY\n");
        }
        text.push_str(&body);
        text.push_str("    return 0;\n#undef F\n");
        if debug {
            text.push_str("#undef B\n");
        }
        text.push_str("}\n");
        self.g.bodies.push_str(&text);

        let call = format!("{name}({})", args.join(", "));
        match run.exits.len() {
            0 => self.line(&format!("{call};")),
            _ => {
                self.line(&format!("switch ({call}) {{"));
                for (k, (label, scope)) in run.exits.iter().enumerate() {
                    self.line(&format!("case {}:", k + 1));
                    self.indent += 1;
                    self.jump(label, *scope);
                    self.indent -= 1;
                }
                self.line("}");
            }
        }
        Ok(())
    }
}
