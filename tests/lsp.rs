//! End-to-end tests of `gamma lsp`: the real binary, driven over its standard input and
//! output with framed JSON-RPC messages.

use serde_json::{Value, json};
use std::io::{BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
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
        TempDir(dir.canonicalize().unwrap())
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
    format!("file://{}", p.display())
}

struct Lsp {
    child: Child,
    stdin: ChildStdin,
    rx: Receiver<Value>,
    notes: Vec<Value>,
    next_id: i64,
}

impl Lsp {
    fn start() -> Lsp {
        let mut child = Command::new(env!("CARGO_BIN_EXE_gamma"))
            .arg("lsp")
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
        Lsp { child, stdin, rx, notes: vec![], next_id: 1 }
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
            self.notes.push(m);
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
        if let Some(i) = self.notes.iter().position(wanted) {
            return self.notes.remove(i)["params"]["diagnostics"].as_array().unwrap().clone();
        }
        loop {
            let m = self.next_message();
            if wanted(&m) {
                return m["params"]["diagnostics"].as_array().unwrap().clone();
            }
            self.notes.push(m);
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

main :: fn() \\ {Out} {
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
    assert!(h["contents"]["value"].as_str().unwrap().contains("main :: fn() \\ {Out}"));

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
    let notes = dir.write("notes.md", "# not code\n");
    let mut lsp = Lsp::start();
    lsp.initialize();
    lsp.open(&notes, "# not code\n");
    let h = lsp.result("textDocument/hover", lsp.at(&notes, 0, 1));
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
