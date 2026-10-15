//! Classifying what was typed: top-level declarations, statements, or an
//! unfinished input that needs more lines.

use crate::driver::Session;
use crate::syntax::ast::{BindingKind, ExprKind, File, FnBody, ItemKind, StmtKind};
use crate::syntax::token::{Punct, Token, TokenKind};
use std::collections::HashMap;

/// One top-level declaration of an input.
#[derive(Debug)]
pub struct ItemInfo {
    /// The names it declares (empty for handlers, tests and static assertions).
    pub names: Vec<String>,
    /// Its source text.
    pub text: String,
    /// A plain `name :: value` binding whose value is not a function, type or
    /// import: it may equally be meant as a local variable.
    pub plain_const: bool,
}

/// An input read as the statements of a function body.
#[derive(Debug)]
pub struct StmtInfo {
    /// The names bound by top-level `:=` and `::` statements, with the byte
    /// range of each name in the input.
    pub bindings: Vec<(String, u32, u32)>,
    /// The input is exactly one expression statement.
    pub single_expr: bool,
}

#[derive(Debug, Default)]
pub struct Classified {
    pub items: Option<Vec<ItemInfo>>,
    pub stmts: Option<StmtInfo>,
    /// Neither reading parsed, and the input stops where more text is expected.
    pub incomplete: bool,
}

const STMT_PREFIX: &str = "__repl_input :: fn() {\n";
const STMT_SUFFIX: &str = "\n}\n";

pub fn lex(text: &str) -> (Vec<Token>, bool) {
    let mut s = Session::new();
    let id = s.sources.add("<repl>", text.to_string());
    let out = s.lex(id);
    let unfinished = s.diags.iter().any(|d| d.message.contains("unterminated block comment"));
    (out.tokens, unfinished)
}

/// Whether `text` has a closing bracket that matches no opening one.
fn has_stray_closer(text: &str) -> bool {
    let mut s = Session::new();
    let id = s.sources.add("<repl>", text.to_string());
    s.lex(id);
    s.diags.iter().any(|d| d.message.contains("unmatched closing"))
}

/// How deeply the brackets of `text` are nested at its end.
fn depth(tokens: &[Token]) -> i32 {
    let mut d = 0;
    for t in tokens {
        if let TokenKind::Punct(p) = &t.kind {
            match p {
                Punct::LParen | Punct::LBracket | Punct::LBrace => d += 1,
                Punct::RParen | Punct::RBracket | Punct::RBrace => d -= 1,
                _ => {}
            }
        }
    }
    d
}

/// Whether brackets are still open at the end of `text`.
pub fn open_brackets(text: &str) -> bool {
    let (tokens, unfinished) = lex(text);
    unfinished || depth(&tokens) > 0
}

/// Parses `text` as top-level items. Returns the file and the byte offset of the
/// first error, if any.
fn parse_items(text: &str) -> (Option<File>, Option<u32>) {
    let mut s = Session::new();
    let id = s.sources.add("<repl>", text.to_string());
    let ast = s.parse(id);
    let first = s.diags.iter().map(|d| d.span.lo).min();
    if s.diags.has_errors() { (None, first) } else { (Some(ast), None) }
}

fn parse_stmts(text: &str) -> (Option<File>, Option<u32>) {
    let src = format!("{}{}{}", STMT_PREFIX, text, STMT_SUFFIX);
    let mut s = Session::new();
    let id = s.sources.add("<repl>", src);
    let ast = s.parse(id);
    let first = s.diags.iter().map(|d| d.span.lo).min();
    if s.diags.has_errors() { (None, first) } else { (Some(ast), None) }
}

fn is_definition(kind: &ExprKind) -> bool {
    matches!(
        kind,
        ExprKind::Fn(_) | ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Effect(_) | ExprKind::Import(_)
    )
}

pub fn classify(text: &str) -> Classified {
    let (tokens, unfinished) = lex(text);
    if unfinished || depth(&tokens) > 0 {
        return Classified { incomplete: true, ..Classified::default() };
    }
    if has_stray_closer(text) {
        // A stray closing bracket can never be completed by more lines: let the
        // syntax error show at once.
        return Classified::default();
    }
    let mut c = Classified::default();
    let (items_ast, items_err) = parse_items(text);
    if let Some(file) = items_ast
        && !file.items.is_empty()
    {
        let mut items = vec![];
        for it in &file.items {
            let mut names = vec![];
            let mut plain_const = false;
            if let ItemKind::Binding(b) = &it.kind {
                names = b.names.iter().map(|n| n.name.as_str().to_string()).collect();
                plain_const = b.kind == BindingKind::Const
                    && b.ty.is_none()
                    && !is_definition(&b.init.kind)
                    && !it.attrs.iter().any(|a| a.name.name.as_str() == "global");
            }
            let t = &text[it.span.lo as usize..it.span.hi as usize];
            items.push(ItemInfo { names, text: t.to_string(), plain_const });
        }
        // `x := e` parses as a top-level item but is a local variable here.
        let all_decls = file.items.iter().all(|it| match &it.kind {
            ItemKind::Binding(b) => {
                b.kind == BindingKind::Const || it.attrs.iter().any(|a| a.name.name.as_str() == "global")
            }
            _ => true,
        });
        if all_decls {
            c.items = Some(items);
        }
    }
    let (stmts_ast, stmts_err) = parse_stmts(text);
    if let Some(file) = stmts_ast
        && file.items.len() == 1
        && let ItemKind::Binding(b) = &file.items[0].kind
        && let ExprKind::Fn(f) = &b.init.kind
        && let FnBody::Block(block) = &f.body
    {
        let off = STMT_PREFIX.len() as u32;
        let mut bindings = vec![];
        for s in &block.stmts {
            if let StmtKind::Binding { binding, .. } = &s.kind {
                for n in &binding.names {
                    bindings.push((n.name.as_str().to_string(), n.span.lo - off, n.span.hi - off));
                }
            }
        }
        let single_expr = block.stmts.len() == 1 && matches!(block.stmts[0].kind, StmtKind::Expr(_));
        if !block.stmts.is_empty() {
            c.stmts = Some(StmtInfo { bindings, single_expr });
        }
    }
    if c.items.is_none() && c.stmts.is_none() {
        let end = text.trim_end().len() as u32;
        let stmts_at_end = stmts_err.is_some_and(|p| p >= STMT_PREFIX.len() as u32 + end);
        let items_at_end = items_err.is_some_and(|p| p >= end);
        c.incomplete = stmts_at_end || items_at_end;
    }
    c
}

/// Whether `text` is empty apart from whitespace and comments. Input with a
/// lexical error (a stray closing bracket, an unfinished comment) is not blank:
/// the error is for the user to see.
pub fn is_blank(text: &str) -> bool {
    let mut s = Session::new();
    let id = s.sources.add("<repl>", text.to_string());
    let out = s.lex(id);
    !s.diags.has_errors() && out.tokens.iter().all(|t| matches!(t.kind, TokenKind::Newline | TokenKind::Eof))
}

/// Whether the expression `text` may change variables: it takes an address
/// (`&x`) or calls a method (`x.next()`).
pub fn may_mutate(text: &str) -> bool {
    let (tokens, _) = lex(text);
    tokens.iter().enumerate().any(|(i, t)| match &t.kind {
        TokenKind::Punct(Punct::Amp) => {
            i == 0 || !matches!(tokens[i - 1].kind, TokenKind::Ident(_) | TokenKind::Int(_))
        }
        TokenKind::Punct(Punct::Dot) => {
            matches!(tokens.get(i + 1).map(|t| &t.kind), Some(TokenKind::Ident(_)))
                && matches!(tokens.get(i + 2).map(|t| &t.kind), Some(TokenKind::Punct(Punct::LParen)))
        }
        _ => false,
    })
}

/// Rewrites identifiers in `text`: the binding names at the byte ranges in
/// `defs` become their new names, and every other identifier found in `uses`
/// becomes its entry there (except member names after `.` and field names
/// before `:`).
pub fn rename(text: &str, defs: &[(u32, u32, String)], uses: &HashMap<String, String>) -> String {
    let (tokens, _) = lex(text);
    let mut edits: Vec<(usize, usize, String)> =
        defs.iter().map(|(lo, hi, n)| (*lo as usize, *hi as usize, n.clone())).collect();
    for (i, t) in tokens.iter().enumerate() {
        let TokenKind::Ident(name) = &t.kind else { continue };
        let Some(new) = uses.get(name.as_str()) else { continue };
        let (lo, hi) = (t.span.lo as usize, t.span.hi as usize);
        if edits.iter().any(|e| e.0 == lo) {
            continue;
        }
        if i > 0 && tokens[i - 1].kind == TokenKind::Punct(Punct::Dot) {
            continue;
        }
        if tokens.get(i + 1).is_some_and(|n| n.kind == TokenKind::Punct(Punct::Colon)) {
            continue;
        }
        edits.push((lo, hi, new.clone()));
    }
    edits.sort_by_key(|e| std::cmp::Reverse(e.0));
    let mut out = text.to_string();
    for (lo, hi, new) in edits {
        out.replace_range(lo..hi, &new);
    }
    out
}

/// The text to show for syntax errors in `text`: the diagnostics of whichever
/// reading got further.
pub fn syntax_errors(text: &str, format: crate::diag::DiagFormat) -> String {
    let (_, items_err) = parse_items(text);
    let (_, stmts_err) = parse_stmts(text);
    let pre = STMT_PREFIX.len() as u32;
    if !has_stray_closer(text)
        && stmts_err.is_some_and(|p| p >= pre + text.trim_end().len() as u32)
        && items_err.is_some()
    {
        return "error: the input ends where more is expected\n".to_string();
    }
    let use_stmts = match (items_err, stmts_err) {
        _ if has_stray_closer(text) => false,
        (Some(i), Some(s)) => s.saturating_sub(pre) > i,
        _ => false,
    };
    let mut s = Session::new();
    if use_stmts {
        let src = format!("{}{}{}", STMT_PREFIX, text, STMT_SUFFIX);
        let id = s.sources.add("<repl>", src);
        let _ = s.parse(id);
        super::report::render_region(&s, id, pre, text, format)
    } else {
        let id = s.sources.add("<repl>", text.to_string());
        let _ = s.parse(id);
        super::report::render_region(&s, id, 0, text, format)
    }
}
