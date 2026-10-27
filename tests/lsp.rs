//! End-to-end tests of `gamma lsp`: the real binary, driven over its standard input and
//! output with framed JSON-RPC messages.

mod common;

use serde_json::{Value, json};
use std::io::{BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Stdio};
use std::sync::mpsc::{Receiver, channel};
use std::time::Duration;

struct TempDir(PathBuf);

impl TempDir {
    fn new() -> TempDir {
        static N: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
        let n = N.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        let dir = std::env::temp_dir().join(format!("gamma-lsp-test-{}-{}", std::process::id(), n));
        std::fs::create_dir_all(&dir).unwrap();
        // The server compares canonical paths.
        TempDir(gamma::source::canonicalize(&dir).unwrap())
    }

    fn write(&self, name: &str, text: &str) -> PathBuf {
        let p = self.0.join(name);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(&p, text).unwrap();
        p
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn uri(p: &Path) -> String {
    let text = p.display().to_string();
    if cfg!(windows) { format!("file:///{}", text.replace('\\', "/")) } else { format!("file://{text}") }
}

/// The path of a `file:` URI, as [`uri`] writes them.
fn uri_path(u: &str) -> PathBuf {
    let rest = u.strip_prefix("file://").expect("a file URI");
    if cfg!(windows) { PathBuf::from(rest.trim_start_matches('/')) } else { PathBuf::from(rest) }
}

struct Lsp {
    child: Child,
    stdin: ChildStdin,
    rx: Receiver<Value>,
    queued: Vec<Value>,
    next_id: i64,
}

impl Lsp {
    fn start() -> Lsp {
        Lsp::start_with(&[])
    }

    /// Starts the server with extra environment variables.
    fn start_with(envs: &[(&str, &Path)]) -> Lsp {
        let mut child = common::gamma()
            .arg("lsp")
            .envs(envs.iter().map(|(k, v)| (*k, *v)))
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .expect("cannot start gamma lsp");
        let stdin = child.stdin.take().unwrap();
        let stdout = child.stdout.take().unwrap();
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let mut r = BufReader::new(stdout);
            loop {
                let mut header = Vec::new();
                let mut byte = [0u8; 1];
                while !header.ends_with(b"\r\n\r\n") {
                    if r.read(&mut byte).unwrap_or(0) == 0 {
                        return;
                    }
                    header.push(byte[0]);
                }
                let text = String::from_utf8(header).unwrap();
                let len: usize = text
                    .lines()
                    .find_map(|l| l.strip_prefix("Content-Length: "))
                    .expect("Content-Length header")
                    .trim()
                    .parse()
                    .unwrap();
                let mut body = vec![0; len];
                r.read_exact(&mut body).unwrap();
                if tx.send(serde_json::from_slice(&body).unwrap()).is_err() {
                    return;
                }
            }
        });
        Lsp { child, stdin, rx, queued: vec![], next_id: 1 }
    }

    fn send_raw(&mut self, body: &str) {
        write!(self.stdin, "Content-Length: {}\r\n\r\n{}", body.len(), body).unwrap();
        self.stdin.flush().unwrap();
    }

    fn notify(&mut self, method: &str, params: Value) {
        let msg = json!({"jsonrpc": "2.0", "method": method, "params": params});
        self.send_raw(&msg.to_string());
    }

    fn next_message(&mut self) -> Value {
        self.rx.recv_timeout(Duration::from_secs(60)).expect("the server did not answer in time")
    }

    /// Sends a request and returns its response; notifications that arrive meanwhile are kept.
    fn request(&mut self, method: &str, params: Value) -> Value {
        let id = self.next_id;
        self.next_id += 1;
        let msg = json!({"jsonrpc": "2.0", "id": id, "method": method, "params": params});
        self.send_raw(&msg.to_string());
        loop {
            let m = self.next_message();
            if m.get("id") == Some(&json!(id)) {
                return m;
            }
            self.queued.push(m);
        }
    }

    fn result(&mut self, method: &str, params: Value) -> Value {
        let r = self.request(method, params);
        assert!(r.get("error").is_none(), "{} failed: {}", method, r);
        r["result"].clone()
    }

    fn initialize(&mut self) -> Value {
        let r = self.result("initialize", json!({"processId": null, "rootUri": null, "capabilities": {}}));
        self.notify("initialized", json!({}));
        r
    }

    fn open(&mut self, path: &Path, text: &str) {
        self.notify(
            "textDocument/didOpen",
            json!({"textDocument": {"uri": uri(path), "languageId": "gamma", "version": 1, "text": text}}),
        );
    }

    fn change(&mut self, path: &Path, version: i64, text: &str) {
        self.notify(
            "textDocument/didChange",
            json!({"textDocument": {"uri": uri(path), "version": version}, "contentChanges": [{"text": text}]}),
        );
    }

    /// The next diagnostics published for `path` at `version`.
    fn diagnostics(&mut self, path: &Path, version: i64) -> Vec<Value> {
        let wanted = |m: &Value| {
            m["method"] == "textDocument/publishDiagnostics"
                && m["params"]["uri"] == uri(path)
                && m["params"]["version"] == version
        };
        if let Some(i) = self.queued.iter().position(wanted) {
            return self.queued.remove(i)["params"]["diagnostics"].as_array().unwrap().clone();
        }
        loop {
            let m = self.next_message();
            if wanted(&m) {
                return m["params"]["diagnostics"].as_array().unwrap().clone();
            }
            self.queued.push(m);
        }
    }

    fn at(&self, path: &Path, line: u32, character: u32) -> Value {
        json!({"textDocument": {"uri": uri(path)}, "position": {"line": line, "character": character}})
    }

    fn shutdown(mut self) -> i32 {
        let r = self.result("shutdown", Value::Null);
        assert_eq!(r, Value::Null);
        self.notify("exit", Value::Null);
        self.child.wait().unwrap().code().unwrap_or(-1)
    }
}

const PROGRAM: &str = "\
Point :: struct {
    x: i32
    y: i32
}

/// Adds the coordinates.
sum :: fn(p: Point) -> i32 {
    return p.x + p.y
}

main :: fn() needs Out {
    pt := Point(x: 1, y: 2)
    total := sum(pt)
    print(\"{}\\n\", total)
}
";

#[test]
fn lifecycle_and_capabilities() {
    let mut lsp = Lsp::start();
    let init = lsp.initialize();
    let caps = &init["capabilities"];
    assert_eq!(caps["positionEncoding"], "utf-16");
    assert_eq!(caps["hoverProvider"], true);
    assert_eq!(caps["definitionProvider"], true);
    assert_eq!(caps["documentSymbolProvider"], true);
    assert_eq!(caps["documentFormattingProvider"], true);
    assert_eq!(caps["completionProvider"]["triggerCharacters"][0], ".");
    assert_eq!(caps["textDocumentSync"]["change"], 1);
    assert_eq!(init["serverInfo"]["name"], "gamma");
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn utf8_is_negotiated_when_offered() {
    let mut lsp = Lsp::start();
    let r = lsp.result("initialize", json!({"capabilities": {"general": {"positionEncodings": ["utf-8", "utf-16"]}}}));
    assert_eq!(r["capabilities"]["positionEncoding"], "utf-8");
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn exit_without_shutdown_is_a_failure() {
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.notify("exit", Value::Null);
    assert_eq!(lsp.child.wait().unwrap().code(), Some(1));
}

#[test]
fn end_of_input_ends_the_server() {
    let mut lsp = Lsp::start();
    lsp.initialize();
    drop(lsp.stdin);
    assert!(lsp.child.wait().unwrap().code().is_some());
}

#[test]
fn unknown_requests_are_answered_and_unknown_notifications_ignored() {
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.notify("workspace/somethingNew", json!({}));
    let r = lsp.request("textDocument/somethingNew", json!({}));
    assert_eq!(r["error"]["code"], -32601);
    // Still alive.
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn invalid_json_gets_a_parse_error() {
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.send_raw("{not json");
    let m = lsp.next_message();
    assert_eq!(m["error"]["code"], -32700);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn diagnostics_follow_the_buffer() {
    let dir = TempDir::new();
    let bad = "main :: fn() {\n    x := \"é😀\" + undefined_name\n}\n";
    let path = dir.write("main.gm", "main :: fn() {}\n");
    let mut lsp = Lsp::start();
    lsp.initialize();
    // The buffer differs from the file on disk: the buffer is what is checked.
    lsp.open(&path, bad);
    let d = lsp.diagnostics(&path, 1);
    assert!(!d.is_empty());
    let err = d.iter().find(|d| d["message"].as_str().unwrap().contains("undefined_name")).expect("the unknown name");
    assert_eq!(err["severity"], 1);
    assert_eq!(err["source"], "gamma");
    // `    x := "é😀" + ` is 17 characters before the name in UTF-16 units (the emoji counts two).
    let line = err["range"]["start"]["line"].as_u64().unwrap();
    assert_eq!(line, 1);
    assert_eq!(err["range"]["start"]["character"], 17);
    assert_eq!(err["range"]["end"]["character"], 17 + "undefined_name".len());
    // Fixing the buffer clears the diagnostics.
    lsp.change(&path, 2, "main :: fn() {\n    x := 1\n    _ = x\n}\n");
    assert_eq!(lsp.diagnostics(&path, 2), Vec::<Value>::new());
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn diagnostics_carry_related_locations() {
    let dir = TempDir::new();
    let text = "f :: fn() {}\nf :: fn() {}\nmain :: fn() {}\n";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, text);
    let d = lsp.diagnostics(&path, 1);
    let dup = d.iter().find(|d| d["message"].as_str().unwrap().contains("declared twice")).expect("duplicate");
    assert_eq!(dup["range"]["start"]["line"], 1);
    let related = &dup["relatedInformation"][0];
    assert_eq!(related["location"]["uri"], uri(&path));
    assert_eq!(related["location"]["range"]["start"]["line"], 0);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn fixes_are_offered_as_quick_fixes() {
    let dir = TempDir::new();
    let text = "f :: fn() -> i32 => 1\n\nmain :: fn() {\n    f()\n}\n";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    let init = lsp.initialize();
    assert_eq!(init["capabilities"]["codeActionProvider"]["codeActionKinds"][0], "quickfix");
    lsp.open(&path, text);
    let d = lsp.diagnostics(&path, 1);
    let unused = d.iter().find(|d| d["message"].as_str().unwrap().contains("unused")).expect("the unused value");
    let params = |diags: Vec<Value>| {
        json!({
            "textDocument": {"uri": uri(&path)},
            "range": {"start": {"line": 3, "character": 0}, "end": {"line": 3, "character": 0}},
            "context": {"diagnostics": diags},
        })
    };
    let actions = lsp.result("textDocument/codeAction", params(vec![unused.clone()]));
    assert_eq!(actions.as_array().unwrap().len(), 1, "{actions}");
    let a = &actions[0];
    assert_eq!(a["kind"], "quickfix");
    assert_eq!(a["isPreferred"], true);
    assert_eq!(a["diagnostics"][0]["message"], unused["message"]);
    // An insertion before `f()`: an empty range, not widened as diagnostics are.
    let edit = &a["edit"]["changes"][uri(&path)][0];
    assert_eq!(edit["newText"], "_ = ");
    assert_eq!(edit["range"]["start"], json!({"line": 3, "character": 4}));
    assert_eq!(edit["range"]["end"], json!({"line": 3, "character": 4}));
    // A diagnostic with two fixes offers both, neither preferred.
    lsp.change(&path, 2, "g :: fn() -> ?i32 => 1\n\nh :: fn() -> ?i32 {\n    x := try g() else 0\n    return x\n}\n");
    let d = lsp.diagnostics(&path, 2);
    let amb = d.iter().find(|d| d["message"].as_str().unwrap().contains("ambiguous")).expect("`try x else d`");
    let actions = lsp.result("textDocument/codeAction", params(vec![amb.clone()]));
    let titles: Vec<&str> = actions.as_array().unwrap().iter().map(|a| a["title"].as_str().unwrap()).collect();
    assert_eq!(titles, ["write `try (x else d)`", "write `(try x) else d`"]);
    assert!(actions.as_array().unwrap().iter().all(|a| a["isPreferred"] == false));
    // Only quick fixes are offered.
    let mut p = params(vec![amb.clone()]);
    p["context"]["only"] = json!(["refactor"]);
    assert_eq!(lsp.result("textDocument/codeAction", p), json!([]));
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn syntax_errors_are_reported() {
    let dir = TempDir::new();
    let text = "main :: fn() {\n    x := (1 +\n}\n";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, text);
    let d = lsp.diagnostics(&path, 1);
    assert!(!d.is_empty());
    assert!(d.iter().all(|d| d["range"]["start"]["line"].as_u64().unwrap() <= 2));
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn hover_definition_references_and_symbols() {
    let dir = TempDir::new();
    let path = dir.write("main.gm", PROGRAM);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, PROGRAM);
    assert_eq!(lsp.diagnostics(&path, 1), Vec::<Value>::new());

    // Hover on `total` (line 13, column 12 of `    print("{}\n", total)`).
    let h = lsp.result("textDocument/hover", lsp.at(&path, 13, 20));
    let value = h["contents"]["value"].as_str().unwrap();
    assert!(value.starts_with("```gamma\ntotal: i32\n```"), "{}", value);
    assert_eq!(h["range"]["start"], json!({"line": 13, "character": 18}));

    // Hover on `sum` in its call: signature, effects and the doc comment.
    let h = lsp.result("textDocument/hover", lsp.at(&path, 12, 14));
    let value = h["contents"]["value"].as_str().unwrap();
    assert!(value.contains("sum :: fn(p: Point) -> i32"), "{}", value);
    assert!(value.contains("Adds the coordinates."), "{}", value);

    // Hover on `main`'s effect signature.
    let h = lsp.result("textDocument/hover", lsp.at(&path, 10, 1));
    assert!(h["contents"]["value"].as_str().unwrap().contains("main :: fn() needs Out"));

    // Definition of the call of `sum`.
    let d = lsp.result("textDocument/definition", lsp.at(&path, 12, 14));
    assert_eq!(d["uri"], uri(&path));
    assert_eq!(d["range"]["start"], json!({"line": 6, "character": 0}));
    assert_eq!(d["range"]["end"], json!({"line": 6, "character": 3}));

    // Definition of the field `x`.
    let d = lsp.result("textDocument/definition", lsp.at(&path, 7, 14));
    assert_eq!(d["range"]["start"], json!({"line": 1, "character": 4}));

    // References to `Point`: its declaration, the parameter and the construction.
    let refs = lsp.result(
        "textDocument/references",
        json!({"textDocument": {"uri": uri(&path)}, "position": {"line": 0, "character": 1},
               "context": {"includeDeclaration": true}}),
    );
    assert_eq!(refs.as_array().unwrap().len(), 3);

    // The outline.
    let syms = lsp.result("textDocument/documentSymbol", json!({"textDocument": {"uri": uri(&path)}}));
    let names: Vec<&str> = syms.as_array().unwrap().iter().map(|s| s["name"].as_str().unwrap()).collect();
    assert_eq!(names, vec!["Point", "sum", "main"]);
    assert_eq!(syms[0]["kind"], 23);
    assert_eq!(syms[0]["children"][0]["name"], "x");
    assert_eq!(syms[0]["children"][0]["kind"], 8);
    assert_eq!(syms[1]["selectionRange"]["start"], json!({"line": 6, "character": 0}));
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn definition_in_an_imported_module() {
    let dir = TempDir::new();
    let util = dir.write("util/util.gm", "/// Twice.\npub double :: fn(n: i32) -> i32 => n * 2\n");
    let text = "util :: import \"./util\"\n\nmain :: fn() {\n    x := util.double(2)\n    _ = x\n}\n";
    let main = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, text);
    assert_eq!(lsp.diagnostics(&main, 1), Vec::<Value>::new());
    let d = lsp.result("textDocument/definition", lsp.at(&main, 3, 16));
    assert_eq!(d["uri"], uri(&util));
    assert_eq!(d["range"]["start"], json!({"line": 1, "character": 4}));
    let h = lsp.result("textDocument/hover", lsp.at(&main, 3, 16));
    let value = h["contents"]["value"].as_str().unwrap();
    assert!(value.contains("double :: fn(n: i32) -> i32") && value.contains("Twice."), "{}", value);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn definition_in_the_standard_library() {
    let dir = TempDir::new();
    let text = "str :: import \"std/str\"\n\nmain :: fn() {\n    n := str.trim(\" a \")\n    _ = n\n}\n";
    let main = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, text);
    let d = lsp.result("textDocument/definition", lsp.at(&main, 3, 15));
    let target = d["uri"].as_str().unwrap_or_default();
    assert!(target.contains("/lib/std/str/"), "{}", d);
    let h = lsp.result("textDocument/hover", lsp.at(&main, 3, 15));
    assert!(h["contents"]["value"].as_str().unwrap().contains("trim :: fn("), "{}", h);
    // A use of the import binding leads to the binding.
    let d = lsp.result("textDocument/definition", lsp.at(&main, 3, 10));
    assert_eq!(d["uri"], uri(&main));
    assert_eq!(d["range"]["start"], json!({"line": 0, "character": 0}));
    // The import itself leads to the module's first file.
    let d = lsp.result("textDocument/definition", lsp.at(&main, 0, 14));
    assert!(d["uri"].as_str().unwrap().contains("/lib/std/str/"), "{}", d);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn completion_of_names_and_members() {
    let dir = TempDir::new();
    let text = "\
Point :: struct {
    x: i32
    y: i32
}

shift :: fn(p: Point) -> i32 => p.x

main :: fn() {
    pt := Point(x: 1, y: 2)
    count := 3
    _ = pt
    _ = count
}
";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, text);
    assert_eq!(lsp.diagnostics(&path, 1), Vec::<Value>::new());

    // Names: locals first, then the module's declarations.
    let r = lsp.result("textDocument/completion", lsp.at(&path, 11, 4));
    let items = r["items"].as_array().unwrap();
    let label = |l: &str| items.iter().find(|i| i["label"] == l).cloned();
    let pt = label("pt").expect("local pt");
    assert_eq!(pt["detail"], "Point");
    assert_eq!(pt["kind"], 6);
    assert!(label("count").is_some());
    assert_eq!(label("Point").unwrap()["kind"], 22);
    assert!(label("shift").is_some());
    assert!(label("print").is_some());
    assert!(label("match").is_some_and(|k| k["kind"] == 14));

    // Members after a dot: the buffer `pt.` does not check, so the server re-checks without it.
    let with_dot = text.replace("    _ = pt\n", "    pt.\n");
    lsp.change(&path, 2, &with_dot);
    let r = lsp.result("textDocument/completion", lsp.at(&path, 10, 7));
    let labels: Vec<&str> = r["items"].as_array().unwrap().iter().map(|i| i["label"].as_str().unwrap()).collect();
    assert_eq!(labels, vec!["x", "y", "shift"]);
    assert_eq!(r["items"][0]["kind"], 5);
    assert_eq!(r["items"][0]["detail"], "i32");
    assert_eq!(r["items"][2]["kind"], 2);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn completion_of_module_members() {
    let dir = TempDir::new();
    dir.write("util/util.gm", "pub double :: fn(n: i32) -> i32 => n * 2\npub LIMIT :: 10\nhidden :: fn() {}\n");
    let text = "util :: import \"./util\"\n\nmain :: fn() {\n    util.\n}\n";
    let main = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, text);
    let r = lsp.result("textDocument/completion", lsp.at(&main, 3, 9));
    let labels: Vec<&str> = r["items"].as_array().unwrap().iter().map(|i| i["label"].as_str().unwrap()).collect();
    assert_eq!(labels, vec!["double", "LIMIT"]);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn signature_help_marks_the_argument() {
    let dir = TempDir::new();
    let text = "\
Counter :: struct { n: i32 }

/// Adds two numbers.
add :: fn(a: i32, b: i32) -> i32 => a + b

bump :: fn(c: ^Counter, by: i32 = 1) {
    c.n += by
}

main :: fn() {
    x := add(1, 2)
    c := Counter(n: 0)
    c.bump(by: 2)
    _ = x
}
";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, text);
    assert_eq!(lsp.diagnostics(&path, 1), Vec::<Value>::new());
    // Between the comma and the second argument of `add(1, 2)`.
    let r = lsp.result("textDocument/signatureHelp", lsp.at(&path, 10, 16));
    assert_eq!(r["activeParameter"], 1);
    let sig = &r["signatures"][0];
    assert_eq!(sig["label"], "add(a: i32, b: i32) -> i32");
    assert_eq!(sig["parameters"][1]["label"], "b: i32");
    assert_eq!(sig["documentation"]["value"], "Adds two numbers.");
    // A method call: the receiver is not a parameter, and `by:` names the argument.
    let r = lsp.result("textDocument/signatureHelp", lsp.at(&path, 12, 15));
    assert_eq!(r["signatures"][0]["label"], "bump(by: i32 = 1)");
    assert_eq!(r["activeParameter"], 0);
    // Outside any call.
    let r = lsp.result("textDocument/signatureHelp", lsp.at(&path, 13, 5));
    assert_eq!(r, Value::Null);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn only_gamma_files_are_analyzed() {
    let dir = TempDir::new();
    let readme = dir.write("readme.md", "# not code\n");
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&readme, "# not code\n");
    let h = lsp.result("textDocument/hover", lsp.at(&readme, 0, 1));
    assert_eq!(h, Value::Null);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn formatting_returns_a_whole_document_edit() {
    let dir = TempDir::new();
    let messy = "main :: fn() {\n  x   :=   1\n    _ = x\n}\n";
    let path = dir.write("main.gm", messy);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, messy);
    let edits = lsp.result(
        "textDocument/formatting",
        json!({"textDocument": {"uri": uri(&path)}, "options": {"tabSize": 4, "insertSpaces": true}}),
    );
    let edit = &edits[0];
    assert_eq!(edit["range"]["start"], json!({"line": 0, "character": 0}));
    assert_eq!(edit["range"]["end"], json!({"line": 4, "character": 0}));
    let expected = gamma::fmt::format_source(messy).unwrap();
    assert_eq!(edit["newText"], expected);
    assert_ne!(expected, messy);

    // Already formatted: no edits. Unparsable: no edits.
    lsp.change(&path, 2, &expected);
    let edits = lsp.result("textDocument/formatting", json!({"textDocument": {"uri": uri(&path)}, "options": {}}));
    assert_eq!(edits, json!([]));
    lsp.change(&path, 3, "main :: fn( {\n");
    let edits = lsp.result("textDocument/formatting", json!({"textDocument": {"uri": uri(&path)}, "options": {}}));
    assert_eq!(edits, Value::Null);
    // Parse errors that the lexer accepts also leave the document alone.
    lsp.change(&path, 4, "x ::= \n");
    let edits = lsp.result("textDocument/formatting", json!({"textDocument": {"uri": uri(&path)}, "options": {}}));
    assert_eq!(edits, Value::Null);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn requests_before_initialize_are_refused() {
    let mut lsp = Lsp::start();
    let r = lsp.request("textDocument/hover", json!({}));
    assert_eq!(r["error"]["code"], -32002);
    // Notifications are dropped; the server still initializes normally.
    lsp.notify("textDocument/didOpen", json!({}));
    lsp.initialize();
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn a_huge_content_length_does_not_crash_the_server() {
    let mut lsp = Lsp::start();
    lsp.stdin.write_all(b"Content-Length: 99999999999999\r\n\r\n{}").unwrap();
    lsp.stdin.flush().unwrap();
    drop(lsp.stdin);
    // It stops cleanly (an exit code, not an abort by a signal).
    assert!(lsp.child.wait().unwrap().code().is_some());
}

#[test]
fn out_of_range_positions_clamp_instead_of_wrapping() {
    let dir = TempDir::new();
    let path = dir.write("main.gm", PROGRAM);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, PROGRAM);
    // 2^32 would wrap to a position that has a hover; clamped, it is past the end.
    assert!(!lsp.result("textDocument/hover", lsp.at(&path, 13, 20)).is_null());
    let h = lsp.result(
        "textDocument/hover",
        json!({"textDocument": {"uri": uri(&path)}, "position": {"line": 4294967296u64 + 13, "character": 20}}),
    );
    assert!(h.is_null(), "{:?}", h);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn closing_a_document_clears_its_diagnostics() {
    let dir = TempDir::new();
    let text = "main :: fn() {\n    nope\n}\n";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, text);
    assert!(!lsp.diagnostics(&path, 1).is_empty());
    lsp.notify("textDocument/didClose", json!({"textDocument": {"uri": uri(&path)}}));
    // The clearing notification has no version.
    loop {
        let m = lsp.next_message();
        if m["method"] == "textDocument/publishDiagnostics"
            && m["params"]["uri"] == uri(&path)
            && m["params"].get("version").is_none()
        {
            assert_eq!(m["params"]["diagnostics"], json!([]));
            break;
        }
    }
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn closing_the_last_document_clears_diagnostics_of_its_siblings() {
    let dir = TempDir::new();
    let text = "main :: fn() {\n    helper()\n}\n";
    let main = dir.write("main.gm", text);
    let util = dir.write("util.gm", "helper :: fn() {\n    x: i32 = \"s\"\n}\n");
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, text);
    // The sibling is not open; its error is published all the same.
    loop {
        let m = lsp.next_message();
        if m["method"] == "textDocument/publishDiagnostics" && uri_path(m["params"]["uri"].as_str().unwrap()) == util {
            assert!(!m["params"]["diagnostics"].as_array().unwrap().is_empty());
            break;
        }
    }
    lsp.notify("textDocument/didClose", json!({"textDocument": {"uri": uri(&main)}}));
    loop {
        let m = lsp.next_message();
        if m["method"] == "textDocument/publishDiagnostics" && uri_path(m["params"]["uri"].as_str().unwrap()) == util {
            assert_eq!(m["params"]["diagnostics"], json!([]));
            break;
        }
    }
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn an_unsaved_import_target_is_seen_by_its_importer() {
    let dir = TempDir::new();
    let util = dir.write("util/util.gm", "pub double :: fn(n: i32) -> i32 => n * 2\n");
    let text = "util :: import \"./util\"\n\nmain :: fn() {\n    x := util.double(2)\n    _ = x\n}\n";
    let main = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, text);
    assert_eq!(lsp.diagnostics(&main, 1), Vec::<Value>::new());
    // The dependency's buffer (never saved) drops the function.
    lsp.open(&util, "pub other :: fn() {}\n");
    let d = lsp.diagnostics(&main, 1);
    assert!(d.iter().any(|d| d["message"].as_str().unwrap().contains("no declaration `double`")), "{:?}", d);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn requests_on_unopened_documents_return_null() {
    let dir = TempDir::new();
    let path = dir.write("main.gm", "main :: fn() {}\n");
    let mut lsp = Lsp::start();
    lsp.initialize();
    let h = lsp.result("textDocument/hover", lsp.at(&path, 0, 1));
    assert_eq!(h, Value::Null);
    assert_eq!(lsp.shutdown(), 0);
}

// --- references across modules ---

/// A package with a library module `util`, its importer `main.gm`, a second importer in
/// `app`, and a module `lone` that does not import `util`.
fn package(dir: &TempDir) -> (PathBuf, PathBuf, PathBuf, PathBuf) {
    dir.write("gamma.toml", "name = \"pkg\"\nversion = \"0.1.0\"\n");
    let util = dir.write(
        "util/util.gm",
        "\
pub Shape :: union {
    Circle: i32
    Square: i32
}

pub LIMIT :: 10

pub Point :: struct {
    pub x: i32
    pub y: i32
}

pub double :: fn(n: i32) -> i32 => n * 2

pub area :: fn(s: Shape, scale: i32) -> i32 {
    big := LIMIT * scale
    return match s {
        .Circle(r) => r * big
        .Square(side) => side * scale
    }
}
",
    );
    let main = dir.write(
        "main.gm",
        "\
util :: import \"./util\"

main :: fn() {
    p := util.Point(x: 1, y: 2)
    d := util.double(p.x)
    a := util.area(util.Shape.Circle(3), util.LIMIT)
    _ = d + a + p.y
}
",
    );
    let app = dir.write(
        "app/app.gm",
        "\
util :: import \"../util\"

other :: fn() -> i32 {
    q := util.Point(x: 5, y: 6)
    return util.double(q.y)
}
",
    );
    let lone = dir.write("lone/lone.gm", "double :: fn(n: i32) -> i32 => n + n\n\ncall :: fn() -> i32 => double(1)\n");
    (util, main, app, lone)
}

fn locations(v: &Value, root: &Path) -> Vec<(String, u64, u64)> {
    let mut out: Vec<(String, u64, u64)> = v
        .as_array()
        .expect("an array of locations")
        .iter()
        .map(|l| {
            let file = l["uri"].as_str().unwrap().strip_prefix(&format!("{}/", uri(root))).unwrap().to_string();
            (file, l["range"]["start"]["line"].as_u64().unwrap(), l["range"]["start"]["character"].as_u64().unwrap())
        })
        .collect();
    out.sort();
    out
}

fn refs_params(lsp: &Lsp, path: &Path, line: u32, character: u32, declaration: bool) -> Value {
    let mut p = lsp.at(path, line, character);
    p["context"] = json!({"includeDeclaration": declaration});
    p
}

#[test]
fn references_cross_module_boundaries() {
    let dir = TempDir::new();
    let (util, main, app, lone) = package(&dir);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    // From a use: the declaration, the uses in this module and in the other importer.
    // `lone` declares its own `double` and is not listed.
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &main, 4, 15, true));
    let want =
        vec![("app/app.gm".to_string(), 4, 16), ("main.gm".to_string(), 4, 14), ("util/util.gm".to_string(), 12, 4)];
    assert_eq!(locations(&refs, &dir.0), want);
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &main, 4, 15, false));
    assert_eq!(locations(&refs, &dir.0), want[..2].to_vec());

    // From the declaration, in a module that does not use it itself.
    lsp.open(&util, &std::fs::read_to_string(&util).unwrap());
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &util, 12, 5, false));
    assert_eq!(locations(&refs, &dir.0), want[..2].to_vec());

    // The type `Point` and its field `x`.
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &util, 7, 5, true));
    assert_eq!(
        locations(&refs, &dir.0),
        vec![("app/app.gm".into(), 3, 14), ("main.gm".into(), 3, 14), ("util/util.gm".into(), 7, 4)]
    );
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &util, 8, 9, true));
    assert_eq!(locations(&refs, &dir.0).len(), 4, "{}", refs);

    // An unsaved edit of the other importer is seen, and so is its removal.
    lsp.open(&app, "util :: import \"../util\"\n\nother :: fn() -> i32 => 1\n");
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &main, 4, 15, true));
    assert_eq!(locations(&refs, &dir.0), vec![want[1].clone(), want[2].clone()]);
    lsp.change(&app, 2, "util :: import \"../util\"\n\nother :: fn() -> i32 => util.double(2)\n");
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &main, 4, 15, false));
    assert_eq!(locations(&refs, &dir.0), vec![("app/app.gm".into(), 2, 29), want[1].clone()]);
    // The module that is not an importer is untouched.
    lsp.open(&lone, &std::fs::read_to_string(&lone).unwrap());
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &lone, 2, 24, true));
    assert_eq!(locations(&refs, &dir.0), vec![("lone/lone.gm".into(), 0, 0), ("lone/lone.gm".into(), 2, 23)]);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn references_include_path_dependencies() {
    let dir = TempDir::new();
    dir.write("dep/gamma.toml", "name = \"dep\"\nversion = \"0.1.0\"\n");
    let dep = dir.write("dep/dep.gm", "pub triple :: fn(n: i32) -> i32 => n * 3\n");
    dir.write("app/gamma.toml", "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\ndep = { path = \"../dep\" }\n");
    let main = dir.write("app/main.gm", "dep :: import \"dep\"\n\nmain :: fn() {\n    _ = dep.triple(2)\n}\n");
    let other = dir.write("app/sub/sub.gm", "dep :: import \"dep\"\n\nsub :: fn() -> i32 => dep.triple(3)\n");
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &main, 3, 13, true));
    assert_eq!(
        locations(&refs, &dir.0),
        vec![("app/main.gm".into(), 3, 12), ("app/sub/sub.gm".into(), 2, 26), ("dep/dep.gm".into(), 0, 4)],
        "{}",
        refs
    );
    // A dependency does not know the packages that use it: from there, only its own modules.
    lsp.open(&dep, &std::fs::read_to_string(&dep).unwrap());
    let refs = lsp.result("textDocument/references", refs_params(&lsp, &dep, 0, 5, false));
    assert_eq!(locations(&refs, &dir.0), vec![], "{} {:?}", refs, other);
    assert_eq!(lsp.shutdown(), 0);
}

// --- checking past syntax errors ---

#[test]
fn names_after_a_syntax_error_are_still_resolved() {
    let dir = TempDir::new();
    // The first function lacks its closing brace; the second is a complete declaration
    // in the first column.
    let text = "\
helper :: fn(n: i32) -> i32 => n + 1

broken :: fn() {
    x := helper(1)
    _ = x

later :: fn() -> i32 {
    y := helper(2)
    return y
}
";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, text);
    let d = lsp.diagnostics(&path, 1);
    assert!(d.iter().any(|d| d["message"].as_str().unwrap().contains("unclosed `{`")), "{:?}", d);
    // Definition and hover work in the function before and in the one after the error.
    for line in [3, 7] {
        let def = lsp.result("textDocument/definition", lsp.at(&path, line, 11));
        assert_eq!(def["range"]["start"], json!({"line": 0, "character": 0}), "line {}", line);
    }
    let h = lsp.result("textDocument/hover", lsp.at(&path, 7, 4));
    assert!(h["contents"]["value"].as_str().unwrap().contains("y: i32"), "{}", h);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn an_unclosed_paren_does_not_hide_later_declarations() {
    let dir = TempDir::new();
    let text = "\
helper :: fn(n: i32) -> i32 => n + 1

broken :: fn() {
    x := helper(1
    _ = x
}

later :: fn() -> i32 => helper(2)
";
    let path = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, text);
    assert!(!lsp.diagnostics(&path, 1).is_empty());
    let def = lsp.result("textDocument/definition", lsp.at(&path, 7, 28));
    assert_eq!(def["range"]["start"], json!({"line": 0, "character": 0}));
    assert_eq!(lsp.shutdown(), 0);
}

// --- the core library ---

const LIST_SRC: &str = include_str!("../lib/core/list.gm");

/// A program that calls `list` (line 1, column 11).
const CORE_PROGRAM: &str = "main :: fn() needs Alloc {\n    xs := list(i32)\n    _ = xs\n}\n";

#[test]
fn definition_in_the_core_library_points_into_the_source_tree() {
    let dir = TempDir::new();
    let path = dir.write("main.gm", CORE_PROGRAM);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, CORE_PROGRAM);
    let d = lsp.result("textDocument/definition", lsp.at(&path, 1, 11));
    let target = d["uri"].as_str().unwrap_or_default();
    assert!(target.ends_with("/lib/core/list.gm"), "{}", d);
    assert_eq!(std::fs::read_to_string(uri_path(target)).unwrap(), LIST_SRC);
    let line = d["range"]["start"]["line"].as_u64().unwrap() as usize;
    assert!(LIST_SRC.lines().nth(line).unwrap().starts_with("pub list ::"), "{}", d);
    // Hover shows the documentation of the core declaration.
    let h = lsp.result("textDocument/hover", lsp.at(&path, 1, 11));
    let value = h["contents"]["value"].as_str().unwrap();
    assert!(value.contains("list :: fn(T: type) -> List(T)") && value.contains("An empty list"), "{}", value);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn definition_in_the_core_library_without_a_source_tree_uses_a_read_only_copy() {
    let dir = TempDir::new();
    let path = dir.write("main.gm", CORE_PROGRAM);
    // A standard library root without the core sources next to it, and a cache directory.
    let std = dir.write("nostd/std/placeholder.txt", "");
    let cache = dir.0.join("cache");
    let mut lsp = Lsp::start_with(&[("GAMMA_STD", std.parent().unwrap()), ("GAMMA_CACHE_DIR", cache.as_path())]);
    lsp.initialize();
    lsp.open(&path, CORE_PROGRAM);
    let d = lsp.result("textDocument/definition", lsp.at(&path, 1, 11));
    let target = uri_path(d["uri"].as_str().unwrap());
    assert!(target.starts_with(cache.join("core")), "{}", d);
    assert_eq!(target.file_name().unwrap(), "list.gm");
    assert_eq!(std::fs::read_to_string(&target).unwrap(), LIST_SRC);
    assert!(std::fs::metadata(&target).unwrap().permissions().readonly());
    let l = d["range"]["start"]["line"].as_u64().unwrap() as usize;
    assert!(LIST_SRC.lines().nth(l).unwrap().starts_with("pub list ::"), "{}", d);
    // A second request finds the file in place.
    let again = lsp.result("textDocument/definition", lsp.at(&path, 1, 11));
    assert_eq!(again, d);
    assert_eq!(lsp.shutdown(), 0);
}

// --- unsaved files ---

#[test]
fn a_new_unsaved_file_belongs_to_its_directorys_module() {
    let dir = TempDir::new();
    let text = "main :: fn() -> i32 => helper(1)\n";
    let main = dir.write("main.gm", text);
    let extra = dir.0.join("extra.gm");
    assert!(!extra.exists());
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, text);
    let d = lsp.diagnostics(&main, 1);
    assert!(d.iter().any(|d| d["message"].as_str().unwrap().contains("unknown name `helper`")), "{:?}", d);
    // The new buffer is never saved.
    lsp.open(&extra, "/// Adds one.\nhelper :: fn(n: i32) -> i32 => n + 1\n");
    assert_eq!(lsp.diagnostics(&main, 1), Vec::<Value>::new());
    let def = lsp.result("textDocument/definition", lsp.at(&main, 0, 26));
    assert_eq!(def["uri"], uri(&extra));
    assert_eq!(def["range"]["start"], json!({"line": 1, "character": 0}));
    let h = lsp.result("textDocument/hover", lsp.at(&main, 0, 26));
    assert!(h["contents"]["value"].as_str().unwrap().contains("Adds one."), "{}", h);
    // Closing it removes it from the module again.
    lsp.notify("textDocument/didClose", json!({"textDocument": {"uri": uri(&extra)}}));
    let d = lsp.diagnostics(&main, 1);
    assert!(!d.is_empty());
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn a_file_in_a_new_directory_is_analyzed() {
    let dir = TempDir::new();
    let path = dir.0.join("fresh/mod.gm");
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&path, "twice :: fn(n: i32) -> i32 => n * 2\n\nuse_it :: fn() -> i32 => twice(2)\n");
    assert_eq!(lsp.diagnostics(&path, 1), Vec::<Value>::new());
    let def = lsp.result("textDocument/definition", lsp.at(&path, 2, 26));
    assert_eq!(def["uri"], uri(&path));
    assert_eq!(lsp.shutdown(), 0);
}

// --- rename ---

/// Applies a `WorkspaceEdit` (ASCII files only) to the files on disk, returning the new
/// text of each changed file by its path relative to `root`.
fn apply_edit(edit: &Value, root: &Path) -> std::collections::BTreeMap<String, String> {
    let mut out = std::collections::BTreeMap::new();
    for (u, edits) in edit["changes"].as_object().expect("changes") {
        let path = uri_path(u);
        let text = std::fs::read_to_string(&path).unwrap();
        let starts: Vec<usize> = std::iter::once(0).chain(text.match_indices('\n').map(|(i, _)| i + 1)).collect();
        let offset =
            |p: &Value| starts[p["line"].as_u64().unwrap() as usize] + p["character"].as_u64().unwrap() as usize;
        let mut list: Vec<(usize, usize, String)> = edits
            .as_array()
            .unwrap()
            .iter()
            .map(|e| {
                (offset(&e["range"]["start"]), offset(&e["range"]["end"]), e["newText"].as_str().unwrap().to_string())
            })
            .collect();
        list.sort();
        let mut new = text.clone();
        for (lo, hi, t) in list.into_iter().rev() {
            new.replace_range(lo..hi, &t);
        }
        out.insert(path.strip_prefix(root).unwrap().display().to_string().replace('\\', "/"), new);
    }
    out
}

fn rename_params(lsp: &Lsp, path: &Path, line: u32, character: u32, new: &str) -> Value {
    let mut p = lsp.at(path, line, character);
    p["newName"] = json!(new);
    p
}

/// Renames and returns the edited files.
fn renamed(
    lsp: &mut Lsp,
    dir: &TempDir,
    path: &Path,
    line: u32,
    character: u32,
    new: &str,
) -> std::collections::BTreeMap<String, String> {
    let edit = lsp.result("textDocument/rename", rename_params(lsp, path, line, character, new));
    apply_edit(&edit, &dir.0)
}

/// The message of the error that a rename fails with.
fn rename_error(lsp: &mut Lsp, path: &Path, line: u32, character: u32, new: &str) -> String {
    let r = lsp.request("textDocument/rename", rename_params(lsp, path, line, character, new));
    r["error"]["message"].as_str().unwrap_or_else(|| panic!("no error: {}", r)).to_string()
}

#[test]
fn rename_is_advertised() {
    let mut lsp = Lsp::start();
    let caps = lsp.initialize();
    assert_eq!(caps["capabilities"]["renameProvider"]["prepareProvider"], true);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn rename_a_function_in_every_module_that_uses_it() {
    let dir = TempDir::new();
    let (_, main, _, _) = package(&dir);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    let p = lsp.result("textDocument/prepareRename", lsp.at(&main, 4, 16));
    assert_eq!(p["placeholder"], "double");
    assert_eq!(p["range"], json!({"start": {"line": 4, "character": 14}, "end": {"line": 4, "character": 20}}));
    let files = renamed(&mut lsp, &dir, &main, 4, 16, "twice");
    assert_eq!(files.keys().collect::<Vec<_>>(), ["app/app.gm", "main.gm", "util/util.gm"]);
    assert!(files["main.gm"].contains("d := util.twice(p.x)"));
    assert!(files["app/app.gm"].contains("return util.twice(q.y)"));
    assert!(files["util/util.gm"].contains("pub twice :: fn(n: i32)"));
    assert!(files.values().all(|t| !t.contains("double")));
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn rename_types_fields_variants_and_constants() {
    let dir = TempDir::new();
    let (util, main, _, _) = package(&dir);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&util, &std::fs::read_to_string(&util).unwrap());
    // A type, from its declaration.
    let files = renamed(&mut lsp, &dir, &util, 7, 6, "Pt");
    assert_eq!(files.keys().collect::<Vec<_>>(), ["app/app.gm", "main.gm", "util/util.gm"]);
    assert!(files["main.gm"].contains("p := util.Pt(x: 1, y: 2)"));
    assert!(files["util/util.gm"].contains("pub Pt :: struct"));
    // A field, from a use in another module: its declaration, its labels and its reads.
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    let files = renamed(&mut lsp, &dir, &main, 4, 23, "px");
    assert!(files["main.gm"].contains("util.Point(px: 1, y: 2)") && files["main.gm"].contains("double(p.px)"));
    assert!(files["app/app.gm"].contains("util.Point(px: 5, y: 6)"));
    assert!(files["util/util.gm"].contains("pub px: i32"));
    // A variant, from its declaration: constructions and patterns.
    let files = renamed(&mut lsp, &dir, &util, 1, 5, "Disc");
    assert!(files["util/util.gm"].contains("    Disc: i32") && files["util/util.gm"].contains(".Disc(r) => r * big"));
    assert!(files["main.gm"].contains("util.Shape.Disc(3)"));
    // A constant, from a use in another module.
    let files = renamed(&mut lsp, &dir, &main, 5, 47, "MAX");
    assert!(files["main.gm"].contains("util.MAX"));
    assert!(files["util/util.gm"].contains("pub MAX :: 10") && files["util/util.gm"].contains("big := MAX * scale"));
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn rename_locals_and_parameters() {
    let dir = TempDir::new();
    let (util, main, _, _) = package(&dir);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&util, &std::fs::read_to_string(&util).unwrap());
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    // A parameter, from its use.
    let files = renamed(&mut lsp, &dir, &util, 15, 25, "factor");
    assert_eq!(files.keys().collect::<Vec<_>>(), ["util/util.gm"]);
    let t = &files["util/util.gm"];
    assert!(t.contains("fn(s: Shape, factor: i32)") && t.contains("LIMIT * factor") && t.contains("side * factor"));
    assert!(!t.contains("scale"));
    // A local, from its declaration, leaving the other module's `p` alone.
    let files = renamed(&mut lsp, &dir, &main, 3, 4, "pt");
    assert_eq!(files.keys().collect::<Vec<_>>(), ["main.gm"]);
    assert!(files["main.gm"].contains("pt := util.Point") && files["main.gm"].contains("double(pt.x)"));
    assert!(files["main.gm"].contains("d + a + pt.y"));
    // A pattern binding.
    let files = renamed(&mut lsp, &dir, &util, 17, 17, "radius");
    assert!(files["util/util.gm"].contains(".Circle(radius) => radius * big"));
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn rename_rejects_bad_names() {
    let dir = TempDir::new();
    let (_, main, _, _) = package(&dir);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    assert!(rename_error(&mut lsp, &main, 4, 16, "1abc").contains("not an identifier"));
    assert!(rename_error(&mut lsp, &main, 4, 16, "two words").contains("not an identifier"));
    assert!(rename_error(&mut lsp, &main, 4, 16, "").contains("not an identifier"));
    assert!(rename_error(&mut lsp, &main, 4, 16, "fn").contains("keyword"));
    assert!(rename_error(&mut lsp, &main, 4, 16, "match").contains("keyword"));
    assert!(rename_error(&mut lsp, &main, 4, 16, "i32").contains("builtin type"));
    assert!(rename_error(&mut lsp, &main, 4, 16, "_").contains("`_`"));
    // Nothing is renamed where there is no name.
    let r = lsp.request("textDocument/rename", rename_params(&lsp, &main, 1, 0, "x"));
    assert!(r.get("error").is_some(), "{}", r);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn rename_rejects_clashes() {
    let dir = TempDir::new();
    let (util, main, app, _) = package(&dir);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&util, &std::fs::read_to_string(&util).unwrap());
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    lsp.open(&app, &std::fs::read_to_string(&app).unwrap());
    // Two functions of a module.
    assert!(rename_error(&mut lsp, &util, 12, 5, "area").contains("already declared"));
    // Two fields; two variants.
    assert!(rename_error(&mut lsp, &util, 8, 9, "y").contains("already declared"));
    assert!(rename_error(&mut lsp, &util, 1, 5, "Square").contains("already declared"));
    // Locals and parameters whose scopes overlap.
    assert!(rename_error(&mut lsp, &util, 15, 5, "scale").contains("already declared"));
    assert!(rename_error(&mut lsp, &util, 14, 26, "big").contains("already declared"));
    assert!(rename_error(&mut lsp, &main, 3, 4, "d").contains("already declared"));
    // A local that would hide a declaration of the module.
    assert!(rename_error(&mut lsp, &util, 15, 5, "LIMIT").contains("hide"));
    // A function whose new name is a local where it is used (in another module).
    assert!(rename_error(&mut lsp, &util, 12, 5, "q").contains("local name"));
    // Nothing was changed by the failures: a fine name still works.
    let files = renamed(&mut lsp, &dir, &util, 12, 5, "twice");
    assert_eq!(files.len(), 3);
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn rename_refuses_names_outside_the_package() {
    let dir = TempDir::new();
    // The core library.
    let core = dir.write("core/main.gm", CORE_PROGRAM);
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&core, CORE_PROGRAM);
    assert!(rename_error(&mut lsp, &core, 1, 11, "mine").contains("core library"));
    let r = lsp.request("textDocument/prepareRename", lsp.at(&core, 1, 11));
    assert!(r["error"]["message"].as_str().unwrap().contains("core library"), "{}", r);
    // The standard library.
    let text = "str :: import \"std/str\"\n\nmain :: fn() {\n    n := str.trim(\" a \")\n    _ = n\n}\n";
    let user = dir.write("user/main.gm", text);
    lsp.open(&user, text);
    assert!(rename_error(&mut lsp, &user, 3, 15, "cut").contains("standard library"));
    // A dependency on a path outside the package.
    dir.write("dep/gamma.toml", "name = \"dep\"\nversion = \"0.1.0\"\n");
    dir.write("dep/dep.gm", "pub triple :: fn(n: i32) -> i32 => n * 3\n");
    dir.write("app/gamma.toml", "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\ndep = { path = \"../dep\" }\n");
    let main = dir.write("app/main.gm", "dep :: import \"dep\"\n\nmain :: fn() {\n    _ = dep.triple(2)\n}\n");
    lsp.open(&main, &std::fs::read_to_string(&main).unwrap());
    assert!(rename_error(&mut lsp, &main, 3, 14, "thrice").contains("dependency"));
    let r = lsp.request("textDocument/prepareRename", lsp.at(&main, 3, 14));
    assert!(r["error"]["message"].as_str().unwrap().contains("dependency"), "{}", r);
    assert_eq!(lsp.shutdown(), 0);
}

// --- watched files ---

impl Lsp {
    /// Initializes a client that supports dynamic registration of file watchers.
    fn initialize_watching(&mut self) {
        let caps = json!({"workspace": {"didChangeWatchedFiles": {"dynamicRegistration": true}}});
        self.result("initialize", json!({"processId": null, "rootUri": null, "capabilities": caps}));
        self.notify("initialized", json!({}));
    }

    /// The next request of the server to the client.
    fn server_request(&mut self) -> Value {
        let is_request = |m: &Value| m.get("id").is_some() && m.get("method").is_some();
        if let Some(i) = self.queued.iter().position(is_request) {
            return self.queued.remove(i);
        }
        loop {
            let m = self.next_message();
            if is_request(&m) {
                return m;
            }
            self.queued.push(m);
        }
    }
}

#[test]
fn file_watchers_are_registered_and_changes_on_disk_refresh_importers() {
    let dir = TempDir::new();
    let util = dir.write("util/util.gm", "pub double :: fn(n: i32) -> i32 => n * 2\n");
    let text = "util :: import \"./util\"\n\nmain :: fn() {\n    x := util.double(2)\n    _ = x\n}\n";
    let main = dir.write("main.gm", text);
    let mut lsp = Lsp::start();
    lsp.initialize_watching();
    let reg = lsp.server_request();
    assert_eq!(reg["method"], "client/registerCapability");
    let r = &reg["params"]["registrations"][0];
    assert_eq!(r["method"], "workspace/didChangeWatchedFiles");
    let watchers = r["registerOptions"]["watchers"].as_array().unwrap();
    let globs: Vec<&str> = watchers.iter().map(|w| w["globPattern"].as_str().unwrap()).collect();
    assert_eq!(globs, ["**/*.gm", "**/gamma.toml"]);
    let reply = json!({"jsonrpc": "2.0", "id": reg["id"], "result": null});
    lsp.send_raw(&reply.to_string());

    lsp.open(&main, text);
    assert_eq!(lsp.diagnostics(&main, 1), Vec::<Value>::new());
    // Another program rewrites the imported file, which is not open.
    std::fs::write(&util, "pub other :: fn() {}\n").unwrap();
    lsp.notify("workspace/didChangeWatchedFiles", json!({"changes": [{"uri": uri(&util), "type": 2}]}));
    let d = lsp.diagnostics(&main, 1);
    assert!(d.iter().any(|d| d["message"].as_str().unwrap().contains("no declaration `double`")), "{:?}", d);
    // A file restored (as a creation) is noticed too.
    std::fs::write(&util, "pub double :: fn(n: i32) -> i32 => n * 2\n").unwrap();
    lsp.notify("workspace/didChangeWatchedFiles", json!({"changes": [{"uri": uri(&util), "type": 1}]}));
    assert_eq!(lsp.diagnostics(&main, 1), Vec::<Value>::new());
    assert_eq!(lsp.shutdown(), 0);
}

#[test]
fn no_watchers_are_registered_without_client_support() {
    let dir = TempDir::new();
    let main = dir.write("main.gm", "main :: fn() {}\n");
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&main, "main :: fn() {}\n");
    lsp.diagnostics(&main, 1);
    assert!(lsp.queued.iter().all(|m| m["method"] != "client/registerCapability"), "{:?}", lsp.queued);
    // The notification is accepted all the same.
    lsp.notify("workspace/didChangeWatchedFiles", json!({"changes": [{"uri": uri(&main), "type": 2}]}));
    assert_eq!(lsp.diagnostics(&main, 1), Vec::<Value>::new());
    assert_eq!(lsp.shutdown(), 0);
}
