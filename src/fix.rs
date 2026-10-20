//! Applying the fixes that diagnostics carry (`gamma fix`).
//!
//! A diagnostic with exactly one fix is resolved by applying it; one with several leaves the
//! choice to the programmer (the language server offers each as a quick fix). Fixes are
//! applied whole: a fix that has an edit in a file that may not be changed, or an edit that
//! overlaps one already planned, is skipped, and a later round (after checking the changed
//! program again) can pick it up.

use crate::diag::{Diagnostic, Edit, Fix};
use crate::source::{FileId, Span};
use std::collections::BTreeMap;

/// The edits to make for `diags`, by file, sorted by position and not overlapping, and
/// how many fixes they come from. `editable` says which files may be changed.
pub fn plan<'d>(
    diags: impl IntoIterator<Item = &'d Diagnostic>,
    editable: impl Fn(FileId) -> bool,
) -> (BTreeMap<FileId, Vec<Edit>>, usize) {
    let mut by_file: BTreeMap<FileId, Vec<Edit>> = BTreeMap::new();
    let mut count = 0;
    for d in diags {
        let [fix] = d.fixes.as_slice() else { continue };
        if fix.edits.is_empty() || !fix.edits.iter().all(|e| e.span != Span::DUMMY && editable(e.span.file)) {
            continue;
        }
        // A fix planned already (by the same diagnostic of another instantiation of
        // generic code, say) is not applied twice.
        if fix.edits.iter().all(|e| by_file.get(&e.span.file).is_some_and(|v| v.contains(e))) {
            continue;
        }
        if !fits(&by_file, fix) {
            continue;
        }
        for e in &fix.edits {
            let v = by_file.entry(e.span.file).or_default();
            let at = v.partition_point(|x| (x.span.lo, x.span.hi) <= (e.span.lo, e.span.hi));
            v.insert(at, e.clone());
        }
        count += 1;
    }
    (by_file, count)
}

/// Whether no edit of `fix` overlaps another of its edits or one in `planned`. Two
/// insertions at the same place overlap (their order would be arbitrary).
fn fits(planned: &BTreeMap<FileId, Vec<Edit>>, fix: &Fix) -> bool {
    let overlap = |a: &Edit, b: &Edit| {
        a.span.file == b.span.file
            && (a.span.lo < b.span.hi && b.span.lo < a.span.hi || a.span.lo == b.span.lo || a.span.hi == b.span.hi)
    };
    for (i, e) in fix.edits.iter().enumerate() {
        if fix.edits[..i].iter().any(|o| overlap(e, o)) {
            return false;
        }
        if planned.get(&e.span.file).is_some_and(|v| v.iter().any(|o| overlap(e, o))) {
            return false;
        }
    }
    true
}

/// `src` with `edits` (sorted by position, not overlapping) made.
pub fn apply(src: &str, edits: &[Edit]) -> String {
    let mut out = String::with_capacity(src.len());
    let mut at = 0;
    for e in edits {
        let (lo, hi) = (e.span.lo as usize, e.span.hi as usize);
        out.push_str(&src[at..lo]);
        out.push_str(&e.text);
        at = hi;
    }
    out.push_str(&src[at..]);
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::source::SourceMap;

    fn sp(file: FileId, lo: u32, hi: u32) -> Span {
        Span { file, lo, hi }
    }

    #[test]
    fn applies_the_only_fix_of_each_diagnostic() {
        let mut sm = SourceMap::new();
        let f = sm.add("a.gm", "x := a && b\ny := 0X1F\n".to_string());
        let src = sm.file(f).src.clone();
        let amp = src.find("&&").unwrap() as u32;
        let hex = src.find("0X").unwrap() as u32;
        let diags = vec![
            Diagnostic::error(sp(f, amp, amp + 2), "and").with_fix("use `and`", sp(f, amp, amp + 2), "and"),
            Diagnostic::error(sp(f, hex, hex + 2), "radix").with_fix("lowercase", sp(f, hex, hex + 2), "0x"),
            // Two ways to resolve it: left alone.
            Diagnostic::error(sp(f, 0, 1), "two").with_fix("first", sp(f, 0, 1), "p").with_fix(
                "second",
                sp(f, 0, 1),
                "q",
            ),
        ];
        let (plan, n) = plan(&diags, |_| true);
        assert_eq!(n, 2);
        assert_eq!(apply(&src, &plan[&f]), "x := a and b\ny := 0x1F\n");
    }

    #[test]
    fn skips_overlapping_repeated_and_foreign_fixes() {
        let mut sm = SourceMap::new();
        let f = sm.add("a.gm", "abcdef".to_string());
        let g = sm.add("std.gm", "xyz".to_string());
        let e = |lo, hi, t: &str| Diagnostic::error(sp(f, lo, hi), "e").with_fix("f", sp(f, lo, hi), t);
        let diags = vec![
            e(1, 3, "BC"),
            // Overlaps the first.
            e(2, 4, "CD"),
            // The same fix again.
            e(1, 3, "BC"),
            // An insertion where another edit starts.
            e(1, 1, "!"),
            e(4, 4, "_"),
            // A fix with an edit in a file that may not be changed is skipped whole.
            Diagnostic::error(sp(f, 5, 6), "e").with_fix_edits(
                "f",
                vec![Edit { span: sp(f, 5, 6), text: "F".into() }, Edit { span: sp(g, 0, 1), text: "X".into() }],
            ),
        ];
        let (plan, n) = plan(&diags, |file| file == f);
        assert_eq!(n, 2);
        assert_eq!(apply("abcdef", &plan[&f]), "aBCd_ef");
        assert!(!plan.contains_key(&g));
    }

    #[test]
    fn a_fix_without_a_location_is_skipped() {
        let mut sm = SourceMap::new();
        let f = sm.add("a.gm", "abc".to_string());
        let diags = vec![
            Diagnostic::error(Span::DUMMY, "e").with_fix("f", Span::DUMMY, "x"),
            Diagnostic::error(sp(f, 0, 1), "e").with_fix("f", sp(f, 0, 1), "A"),
        ];
        let (plan, n) = plan(&diags, |file| sm.file(file).src.len() == 3);
        assert_eq!(n, 1);
        assert_eq!(apply("abc", &plan[&f]), "Abc");
    }

    #[test]
    fn adjacent_edits_both_apply() {
        let mut sm = SourceMap::new();
        let f = sm.add("a.gm", "abcdef".to_string());
        let e = |lo, hi, t: &str| Diagnostic::error(sp(f, lo, hi), "e").with_fix("f", sp(f, lo, hi), t);
        let (plan, n) = plan(&[e(0, 2, "AB"), e(3, 5, "DE")], |_| true);
        assert_eq!(n, 2);
        assert_eq!(apply("abcdef", &plan[&f]), "ABcDEf");
    }
}
