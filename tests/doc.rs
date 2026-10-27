//! Tests of the documentation generator (`gamma doc`).
//!
//! * Golden tests render the fixture module in `tests/doc/fixture/shapes`, which has one
//!   of every kind of public declaration, and compare with the files in `tests/doc/golden`.
//!   `GAMMA_BLESS=1 cargo test --test doc` rewrites the goldens after an intended change.
//! * The command-line tests run the `gamma` binary.
//! * `std_reference_is_current` fails when the checked-in standard library reference in
//!   `docs/std/` no longer matches `lib/std`.

mod common;

use gamma::doc::{self, Format};
use std::path::{Path, PathBuf};
use std::process::Command;

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn fixture() -> PathBuf {
    root().join("tests/doc/fixture/shapes")
}

fn gamma() -> Command {
    common::gamma()
}

fn check_golden(name: &str, actual: &str) {
    let path = root().join("tests/doc/golden").join(name);
    if std::env::var_os("GAMMA_BLESS").is_some() {
        std::fs::write(&path, actual).unwrap();
        return;
    }
    let expected = std::fs::read_to_string(&path)
        .unwrap_or_else(|_| panic!("missing golden file {}; create it with GAMMA_BLESS=1", path.display()));
    assert!(
        expected == actual,
        "{} differs from the generated output; if the change is intended, run `GAMMA_BLESS=1 cargo test --test doc`\n--- expected\n{}\n--- actual\n{}",
        path.display(),
        expected,
        actual
    );
}

#[test]
fn fixture_markdown() {
    let modules = doc::load_modules(&fixture(), false).unwrap();
    assert_eq!(modules.len(), 1);
    check_golden("shapes.md", &doc::render_module(&modules[0], Format::Markdown, None));
    check_golden("index.md", &doc::render_index("Fixture modules", &modules, Format::Markdown));
}

#[test]
fn fixture_html() {
    let modules = doc::load_modules(&fixture(), false).unwrap();
    check_golden("shapes.html", &doc::render_module(&modules[0], Format::Html, Some("index.html")));
    check_golden("index.html", &doc::render_index("Fixture modules", &modules, Format::Html));
}

#[test]
fn private_declarations_are_omitted() {
    let m = &doc::load_modules(&fixture(), false).unwrap()[0];
    let names: Vec<&str> = m.decls.iter().map(|d| d.name.as_str()).collect();
    for hidden in ["sides_of", "Hidden", "handler Events for Hidden", "mem"] {
        assert!(!names.contains(&hidden), "{} should not be documented", hidden);
    }
    assert!(names.contains(&"handler Events for ^Counter"));
}

#[test]
fn html_is_escaped() {
    let m = &doc::load_modules(&fixture(), false).unwrap()[0];
    let html = doc::render_module(m, Format::Html, None);
    assert!(html.contains("a &lt; b &amp;&amp; c &gt; d"));
    assert!(!html.contains("a < b"));
    assert!(!html.contains("<script"));
}

#[test]
fn output_is_deterministic() {
    let a = doc::load_modules(&fixture(), false).unwrap();
    let b = doc::load_modules(&fixture(), false).unwrap();
    for f in [Format::Markdown, Format::Html] {
        assert_eq!(doc::render_module(&a[0], f, None), doc::render_module(&b[0], f, None));
    }
}

#[test]
fn missing_lists_undocumented_items() {
    let m = &doc::load_modules(&fixture(), false).unwrap()[0];
    let warnings = m.missing().join("\n");
    for name in ["NO_DOC", "twice", "undocumented"] {
        assert!(warnings.contains(&format!("`{}`", name)), "{}", warnings);
    }
    assert!(!warnings.contains("`area`"), "{}", warnings);
    assert!(!warnings.contains("module"), "the fixture has module documentation: {}", warnings);
}

#[test]
fn inline_markdown_and_headings_in_doc_comments() {
    let dir = temp_dir("inline");
    let src = "\
//! Module text.
//!
//! # Overview
//!
//! More.

/// Spans: `` a`b `` and ``` x``y ``` and `` ` `` and `lone ` tick, ***both*** and **s**.
///
/// # Details
///
///     # not a heading
pub f :: fn() {}
";
    std::fs::write(dir.join("m.gm"), src).unwrap();
    let m = &doc::load_modules(&dir, false).unwrap()[0];
    let html = doc::render_module(m, Format::Html, None);
    assert!(html.contains("<code>a`b</code>"), "{html}");
    assert!(html.contains("<code>x``y</code>"), "{html}");
    assert!(html.contains("<code>`</code>"), "{html}");
    assert!(html.contains("<code>lone </code> tick") || html.contains("`lone"), "{html}");
    assert!(html.contains("<em><strong>both</strong></em>"), "{html}");
    assert!(html.contains("<strong>s</strong>"), "{html}");
    assert!(html.contains("<h4>Overview</h4>") && html.contains("<h4>Details</h4>"), "{html}");
    let md = doc::render_module(m, Format::Markdown, None);
    assert!(md.contains("#### Overview") && md.contains("#### Details"), "{md}");
    assert!(!md.lines().any(|l| l == "# Overview" || l == "# Details"), "{md}");
    assert!(md.contains("    # not a heading"), "{md}");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_signature_with_backticks_cannot_close_its_fence() {
    let dir = temp_dir("fence");
    std::fs::write(dir.join("m.gm"), "/// After the fence.\npub X :: \"```\"\n").unwrap();
    let m = &doc::load_modules(&dir, false).unwrap()[0];
    let md = doc::render_module(m, Format::Markdown, None);
    assert!(md.contains("````gamma\nX :: \"```\"\n````\n"), "{md}");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn index_links_are_percent_encoded() {
    let dir = temp_dir("links");
    let file = dir.join("m.gm");
    std::fs::write(&file, "//! Text.\npub f :: fn() {}\n").unwrap();
    let m = doc::load_module(&[file], "m", "my dir/a#b)").unwrap();
    let md = doc::render_index("T", std::slice::from_ref(&m), Format::Markdown);
    assert!(md.contains("(my%20dir/a%23b%29.md)"), "{md}");
    let html = doc::render_index("T", std::slice::from_ref(&m), Format::Html);
    assert!(html.contains("href=\"my%20dir/a%23b%29.html\""), "{html}");
    std::fs::remove_dir_all(&dir).ok();
}

/// The values of the `href` and `src` attributes in `html`.
fn urls(html: &str) -> Vec<String> {
    let mut out = Vec::new();
    for attr in ["href=\"", "src=\""] {
        let mut rest = html;
        while let Some(i) = rest.find(attr) {
            rest = &rest[i + attr.len()..];
            out.push(rest[..rest.find('"').unwrap()].to_string());
        }
    }
    out
}

#[test]
fn html_output_never_contains_active_content() {
    let hostile = [
        "<script>alert(1)</script>",
        "<img src=x onerror=alert(1)>",
        "<iframe src=\"javascript:alert(1)\"></iframe>",
        "[x](javascript:alert(1))",
        "[x](JaVaScRiPt:alert(1))",
        "[x]( javascript:alert(1))",
        "[x](&#106;avascript:alert(1))",
        "[x](jav&#x09;ascript:alert(1))",
        "[x](vbscript:msgbox(1))",
        "[x](data:text/html;base64,PHNjcmlwdD4=)",
        "[x](file:///etc/passwd)",
        "[<b>x</b>](http://a.test/\" onmouseover=\"alert(1))",
        "[x](http://a.test/<script>)",
        "**<u>bold</u>** `<script>` *<svg onload=alert(1)>*",
        "> <script>alert(1)</script>",
        "- <img src=x onerror=alert(1)>",
        "| <script> | b |\n| --- | --- |\n| <img onerror=x> | [x](javascript:1) |",
        "# <script>alert(1)</script>",
        "    <script>alert(1)</script>",
        "```\n</code></pre><script>alert(1)</script>\n```",
    ];
    let dir = temp_dir("inject");
    for (n, text) in hostile.iter().enumerate() {
        let module_doc: String = text.lines().map(|l| format!("//! {l}\n")).collect();
        let item_doc: String = text.lines().map(|l| format!("/// {l}\n")).collect();
        let member_doc: String = text.lines().map(|l| format!("    /// {l}\n")).collect();
        let src = format!(
            "{module_doc}\n{item_doc}pub f :: fn() {{}}\n\n/// Variants.\npub E :: union {{\n{member_doc}    A: i64\n}}\n"
        );
        std::fs::write(dir.join("m.gm"), src).unwrap();
        let m = &doc::load_modules(&dir, false).unwrap()[0];
        let html = doc::render_module(m, Format::Html, Some("index.html"));
        let lower = html.to_ascii_lowercase();
        for tag in ["<script", "<img", "<iframe", "<svg", "<u>", "<b>"] {
            assert!(!lower.contains(tag), "case {n}: {tag} in {html}");
        }
        // Attribute-like text may appear as escaped text, but never inside a tag.
        for tag in lower.split('<').skip(1) {
            let tag = &tag[..tag.find('>').unwrap_or(tag.len())];
            assert!(!tag.contains(" on"), "case {n}: event handler in <{tag}> in {html}");
        }
        for url in urls(&html) {
            let ok = match url.find([':', '/', '#', '?']) {
                Some(i) if url.as_bytes()[i] == b':' => {
                    matches!(url[..i].to_ascii_lowercase().as_str(), "http" | "https" | "mailto")
                }
                _ => true,
            };
            assert!(ok && !url.contains(['<', '>', ' ']), "case {n}: unsafe URL {url:?} in {html}");
        }
    }
    std::fs::remove_dir_all(&dir).ok();
}

fn temp_dir(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("gamma-doc-test-{}-{}", std::process::id(), name));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn cli_prints_one_module_to_stdout() {
    let out = gamma().arg("doc").arg(fixture()).output().unwrap();
    assert!(out.status.success());
    let text = String::from_utf8(out.stdout).unwrap();
    assert_eq!(text, std::fs::read_to_string(root().join("tests/doc/golden/shapes.md")).unwrap());
}

#[test]
fn cli_writes_a_tree_with_an_index() {
    let dir = temp_dir("tree");
    let out = gamma()
        .args(["doc", "--all", "--format=html", "-o"])
        .arg(&dir)
        .arg(root().join("tests/doc/fixture"))
        .output()
        .unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let index = std::fs::read_to_string(dir.join("index.html")).unwrap();
    assert!(index.contains("href=\"shapes/shapes.html\"") || index.contains("href=\"shapes.html\""), "{}", index);
    let found = std::fs::read_dir(&dir).unwrap().count();
    assert!(found >= 2);
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn cli_writes_a_single_file() {
    let dir = temp_dir("file");
    let file = dir.join("shapes.md");
    let out = gamma().arg("doc").arg(fixture()).arg("-o").arg(&file).output().unwrap();
    assert!(out.status.success());
    assert!(std::fs::read_to_string(&file).unwrap().starts_with("# Module `shapes`"));
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn cli_missing_warns_on_stderr() {
    let out = gamma().arg("doc").arg(fixture()).arg("--missing").output().unwrap();
    assert!(out.status.success());
    let err = String::from_utf8(out.stderr).unwrap();
    assert!(err.contains("warning: public function `undocumented` has no doc comment"), "{}", err);
}

#[test]
fn cli_rejects_a_module_that_does_not_parse() {
    let dir = temp_dir("broken");
    std::fs::write(dir.join("broken.gm"), "pub f :: fn( {\n").unwrap();
    let out = gamma().arg("doc").arg(&dir).output().unwrap();
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stderr).contains("broken.gm"));
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn cli_usage_errors() {
    let out = gamma().args(["doc", "--format=pdf", "x"]).output().unwrap();
    assert_eq!(out.status.code(), Some(2));
    let out = gamma().args(["doc", "--all"]).arg(root().join("lib/std")).output().unwrap();
    assert!(!out.status.success(), "--all without -o is refused");
    let out = gamma().args(["doc", "no/such/dir"]).output().unwrap();
    assert!(!out.status.success());
}

/// Every file below `dir`, as a path relative to it.
fn files_below(dir: &Path, rel: &Path, out: &mut Vec<String>) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for e in rd.flatten() {
        let p = e.path();
        let r = rel.join(e.file_name());
        if p.is_dir() {
            files_below(&p, &r, out);
        } else {
            out.push(r.to_string_lossy().replace('\\', "/"));
        }
    }
}

#[test]
fn std_reference_is_current() {
    const HOW: &str = "The standard library reference in docs/std/ is out of date. Regenerate it with\n\n    cargo run -- doc lib/std --all -o docs/std\n\nand commit the result.";
    let modules = doc::load_modules(&root().join("lib/std"), true).unwrap();
    let expected = doc::output_files("std modules", &modules, Format::Markdown);
    let dir = root().join("docs/std");
    let mut stale = Vec::new();
    for (rel, text) in &expected {
        match std::fs::read_to_string(dir.join(rel)) {
            Ok(have) if &have == text => {}
            Ok(_) => stale.push(format!("changed: docs/std/{}", rel)),
            Err(_) => stale.push(format!("missing: docs/std/{}", rel)),
        }
    }
    let mut present = Vec::new();
    files_below(&dir, Path::new(""), &mut present);
    for p in present {
        if !expected.iter().any(|(rel, _)| *rel == p) {
            stale.push(format!("obsolete: docs/std/{}", p));
        }
    }
    assert!(stale.is_empty(), "{}\n\n{}", HOW, stale.join("\n"));
}
