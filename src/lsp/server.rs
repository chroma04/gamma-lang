//! The server loop: documents, scheduling of analyses, and request dispatch.

use super::analysis::{Analysis, canon, path_to_uri, uri_to_path};
use super::features::{self, Ctx};
use super::position::Encoding;
use super::refs;
use super::transport::write_message;
use super::workspace::Workspace;
use serde_json::{Value, json};
use std::collections::{HashMap, HashSet};
use std::io::Write;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{Receiver, TryRecvError};

/// A message from the client, or the reason one could not be read.
pub type Incoming = Result<Value, String>;

const PARSE_ERROR: i64 = -32700;
const INVALID_REQUEST: i64 = -32600;
const METHOD_NOT_FOUND: i64 = -32601;
const SERVER_NOT_INITIALIZED: i64 = -32002;
const INVALID_PARAMS: i64 = -32602;
const INTERNAL_ERROR: i64 = -32603;
const REQUEST_FAILED: i64 = -32803;

struct Doc {
    uri: String,
    version: i64,
    text: String,
}

pub struct Server<W: Write> {
    out: W,
    enc: Encoding,
    docs: HashMap<PathBuf, Doc>,
    /// The latest analysis of each module directory that has an open document.
    analyses: HashMap<PathBuf, Analysis>,
    /// Directories whose analysis is out of date.
    stale: HashSet<PathBuf>,
    /// For each analyzed directory, the files it published diagnostics for.
    published: HashMap<PathBuf, HashSet<PathBuf>>,
    /// The other modules of the packages of the open documents.
    workspace: Workspace,
    shutdown: bool,
    /// Whether `initialize` has been received.
    initialized: bool,
    /// Free the compiler's types after each analysis (see [`Analysis::run`]).
    pub recycle: bool,
}

pub enum Flow {
    Continue,
    Exit(u8),
}

fn dir_of(path: &Path) -> PathBuf {
    path.parent().map(Path::to_path_buf).unwrap_or_default()
}

impl<W: Write> Server<W> {
    pub fn new(out: W) -> Server<W> {
        Server {
            out,
            enc: Encoding::Utf16,
            docs: HashMap::new(),
            analyses: HashMap::new(),
            stale: HashSet::new(),
            published: HashMap::new(),
            workspace: Workspace::default(),
            shutdown: false,
            initialized: false,
            recycle: false,
        }
    }

    /// Serves messages until `exit` or the end of the input; returns the exit code.
    pub fn run(&mut self, rx: Receiver<Incoming>) -> u8 {
        loop {
            // Analyze when no newer message is waiting, so that a burst of edits costs one run.
            let msg = if self.needs_analysis() {
                match rx.try_recv() {
                    Ok(m) => m,
                    Err(TryRecvError::Empty) => {
                        self.refresh();
                        continue;
                    }
                    Err(TryRecvError::Disconnected) => break,
                }
            } else {
                match rx.recv() {
                    Ok(m) => m,
                    Err(_) => break,
                }
            };
            if let Flow::Exit(code) = self.handle_incoming(msg) {
                return code;
            }
        }
        if self.shutdown { 0 } else { 1 }
    }

    /// Handles one message; returns the exit code if it was `exit`.
    pub fn handle_incoming(&mut self, msg: Incoming) -> Flow {
        match msg {
            Ok(m) => self.handle(m),
            Err(e) => {
                self.respond_error(Value::Null, PARSE_ERROR, &e);
                Flow::Continue
            }
        }
    }

    fn handle(&mut self, msg: Value) -> Flow {
        let Some(method) = msg.get("method").and_then(Value::as_str).map(str::to_string) else {
            // A response to a request of ours (there are none): ignore.
            return Flow::Continue;
        };
        let params = msg.get("params").cloned().unwrap_or(Value::Null);
        if !self.initialized && method != "initialize" {
            // Before `initialize`, requests fail and notifications (except `exit`) are dropped.
            match msg.get("id").cloned() {
                Some(id) => self.respond_error(id, SERVER_NOT_INITIALIZED, "the server is not initialized"),
                None if method == "exit" => return self.notification(&method, params),
                None => {}
            }
            return Flow::Continue;
        }
        // A bug in a handler must not stop the server: answer with an error and go on.
        let guarded = catch_unwind(AssertUnwindSafe(|| match msg.get("id").cloned() {
            Some(id) => {
                self.request(id, &method, params);
                Flow::Continue
            }
            None => self.notification(&method, params),
        }));
        match guarded {
            Ok(flow) => flow,
            Err(_) => {
                if let Some(id) = msg.get("id").cloned() {
                    self.respond_error(id, INTERNAL_ERROR, &format!("internal error handling {}", method));
                }
                Flow::Continue
            }
        }
    }

    // --- sending ---

    fn send(&mut self, msg: Value) {
        if write_message(&mut self.out, &msg).is_err() {
            // The client went away; the loop ends when its input does.
        }
    }

    fn respond(&mut self, id: Value, result: Value) {
        self.send(json!({"jsonrpc": "2.0", "id": id, "result": result}));
    }

    fn respond_error(&mut self, id: Value, code: i64, message: &str) {
        self.send(json!({"jsonrpc": "2.0", "id": id, "error": {"code": code, "message": message}}));
    }

    fn notify(&mut self, method: &str, params: Value) {
        self.send(json!({"jsonrpc": "2.0", "method": method, "params": params}));
    }

    // --- notifications ---

    fn notification(&mut self, method: &str, params: Value) -> Flow {
        match method {
            "exit" => return Flow::Exit(if self.shutdown { 0 } else { 1 }),
            "textDocument/didOpen" => {
                let td = &params["textDocument"];
                if let (Some(uri), Some(text)) = (td["uri"].as_str(), td["text"].as_str())
                    && let Some(path) = uri_to_path(uri)
                    && path.extension().is_some_and(|e| e == "gm")
                {
                    let version = td["version"].as_i64().unwrap_or(0);
                    self.docs.insert(canon(&path), Doc { uri: uri.to_string(), version, text: text.to_string() });
                    self.invalidate(&canon(&path));
                }
            }
            "textDocument/didChange" => {
                let td = &params["textDocument"];
                if let Some(uri) = td["uri"].as_str()
                    && let Some(path) = uri_to_path(uri).map(|p| canon(&p))
                    && let Some(doc) = self.docs.get_mut(&path)
                {
                    doc.version = td["version"].as_i64().unwrap_or(doc.version);
                    // Full synchronization: the last change holds the whole text.
                    if let Some(text) =
                        params["contentChanges"].as_array().and_then(|c| c.last()).and_then(|c| c["text"].as_str())
                    {
                        doc.text = text.to_string();
                    }
                    self.invalidate(&path);
                }
            }
            "textDocument/didSave" => {
                if let Some(path) = params["textDocument"]["uri"].as_str().and_then(uri_to_path) {
                    self.invalidate(&canon(&path));
                }
            }
            "textDocument/didClose" => {
                if let Some(uri) = params["textDocument"]["uri"].as_str()
                    && let Some(path) = uri_to_path(uri).map(|p| canon(&p))
                    && self.docs.remove(&path).is_some()
                {
                    self.notify("textDocument/publishDiagnostics", json!({"uri": uri, "diagnostics": []}));
                    self.invalidate(&path);
                    let dir = dir_of(&path);
                    if !self.docs.keys().any(|p| dir_of(p) == dir) {
                        self.analyses.remove(&dir);
                        self.stale.remove(&dir);
                        self.published.remove(&dir);
                    }
                }
            }
            _ => {}
        }
        Flow::Continue
    }

    /// Marks the analyses that read the file at `path` as out of date.
    fn invalidate(&mut self, path: &Path) {
        let dir = dir_of(path);
        self.stale.insert(dir.clone());
        for (d, a) in &self.analyses {
            if a.files.keys().any(|f| dir_of(f) == dir) {
                self.stale.insert(d.clone());
            }
        }
    }

    // --- analysis ---

    fn open_dirs(&self) -> Vec<PathBuf> {
        let mut dirs: Vec<PathBuf> = self.docs.keys().map(|p| dir_of(p)).collect();
        dirs.sort();
        dirs.dedup();
        dirs
    }

    fn needs_analysis(&self) -> bool {
        self.open_dirs().iter().any(|d| !self.analyses.contains_key(d) || self.stale.contains(d))
    }

    /// Re-analyzes the directories that need it and publishes their diagnostics.
    fn refresh(&mut self) {
        for dir in self.open_dirs() {
            self.ensure(&dir);
        }
    }

    /// Makes the analysis of `dir` current.
    fn ensure(&mut self, dir: &Path) {
        if self.analyses.contains_key(dir) && !self.stale.contains(dir) {
            return;
        }
        let overlays = self.overlays();
        let mut open: Vec<&PathBuf> = self.docs.keys().filter(|p| dir_of(p) == dir).collect();
        open.sort();
        let Some(primary) = open.first().map(|p| (*p).clone()) else { return };
        let analysis = Analysis::run(dir, &overlays, &primary, self.recycle);
        self.stale.remove(dir);
        self.publish(dir, &analysis);
        self.analyses.insert(dir.to_path_buf(), analysis);
    }

    fn overlays(&self) -> HashMap<PathBuf, String> {
        self.docs.iter().map(|(p, d)| (p.clone(), d.text.clone())).collect()
    }

    fn uri_map(&self) -> HashMap<PathBuf, String> {
        self.docs.iter().map(|(p, d)| (p.clone(), d.uri.clone())).collect()
    }

    fn publish(&mut self, dir: &Path, a: &Analysis) {
        let uris = self.uri_map();
        let uri_of = |p: &Path| uris.get(p).cloned().unwrap_or_else(|| path_to_uri(p));
        let diags = a.diagnostics(self.enc, &uri_of);
        let mut targets: HashSet<PathBuf> = diags.keys().cloned().collect();
        targets.extend(self.docs.keys().filter(|p| dir_of(p) == dir).cloned());
        if let Some(old) = self.published.get(dir) {
            targets.extend(old.iter().cloned());
        }
        let mut sorted: Vec<PathBuf> = targets.into_iter().collect();
        sorted.sort();
        for path in sorted {
            let mut params = json!({
                "uri": uri_of(&path),
                "diagnostics": diags.get(&path).cloned().unwrap_or_default(),
            });
            if let Some(doc) = self.docs.get(&path) {
                params["version"] = json!(doc.version);
            }
            self.notify("textDocument/publishDiagnostics", params);
        }
        self.published.insert(dir.to_path_buf(), diags.keys().cloned().collect());
    }

    // --- requests ---

    fn request(&mut self, id: Value, method: &str, params: Value) {
        if self.shutdown && method != "shutdown" {
            return self.respond_error(id, INVALID_REQUEST, "the server is shutting down");
        }
        match method {
            "initialize" => {
                self.initialized = true;
                let result = self.initialize(&params);
                self.respond(id, result)
            }
            "shutdown" => {
                self.shutdown = true;
                self.respond(id, Value::Null)
            }
            "textDocument/hover"
            | "textDocument/definition"
            | "textDocument/references"
            | "textDocument/prepareRename"
            | "textDocument/rename"
            | "textDocument/completion"
            | "textDocument/documentSymbol"
            | "textDocument/signatureHelp"
            | "textDocument/formatting" => match self.language_request(method, &params) {
                Ok(v) => self.respond(id, v),
                Err((code, msg)) => self.respond_error(id, code, &msg),
            },
            _ => self.respond_error(id, METHOD_NOT_FOUND, &format!("method not found: {}", method)),
        }
    }

    fn initialize(&mut self, params: &Value) -> Value {
        let offered = params["capabilities"]["general"]["positionEncodings"].as_array();
        if offered.is_some_and(|e| e.iter().any(|x| x == "utf-8")) {
            self.enc = Encoding::Utf8;
        }
        json!({
            "capabilities": {
                "positionEncoding": self.enc.name(),
                "textDocumentSync": {"openClose": true, "change": 1, "save": {"includeText": false}},
                "hoverProvider": true,
                "definitionProvider": true,
                "referencesProvider": true,
                "renameProvider": {"prepareProvider": true},
                "documentSymbolProvider": true,
                "documentFormattingProvider": true,
                "completionProvider": {"triggerCharacters": ["."], "resolveProvider": false},
                "signatureHelpProvider": {"triggerCharacters": ["(", ","]},
            },
            "serverInfo": {"name": "gamma", "version": env!("CARGO_PKG_VERSION")},
        })
    }

    /// The document a request is about, and the analysis of its module.
    fn target(&mut self, params: &Value) -> Result<PathBuf, (i64, String)> {
        let uri =
            params["textDocument"]["uri"].as_str().ok_or((INVALID_PARAMS, "missing textDocument.uri".to_string()))?;
        let path = uri_to_path(uri).map(|p| canon(&p)).ok_or((INVALID_PARAMS, format!("unsupported URI {}", uri)))?;
        Ok(path)
    }

    fn language_request(&mut self, method: &str, params: &Value) -> Result<Value, (i64, String)> {
        let path = self.target(params)?;
        if method == "textDocument/formatting" {
            return self.formatting(&path);
        }
        if !self.docs.contains_key(&path) {
            return Ok(Value::Null);
        }
        let dir = dir_of(&path);
        self.ensure(&dir);
        let uris = self.uri_map();
        let overlays = self.overlays();
        let uri_of = |p: &Path| uris.get(p).cloned().unwrap_or_else(|| path_to_uri(p));
        let Some(a) = self.analyses.get(&dir) else { return Ok(Value::Null) };
        let Some(file) = a.file_id(&path) else { return Ok(Value::Null) };
        let cx = Ctx { a, enc: self.enc, uri_of: &uri_of };
        let pos = &params["position"];
        let at = |cx: &Ctx| {
            cx.offset(file, pos["line"].as_u64().unwrap_or(0) as u32, pos["character"].as_u64().unwrap_or(0) as u32)
        };
        Ok(match method {
            "textDocument/hover" => features::hover(&cx, file, at(&cx)),
            "textDocument/definition" => features::definition(&cx, file, at(&cx)),
            "textDocument/references" | "textDocument/prepareRename" | "textDocument/rename" => {
                let off = at(&cx);
                let mut rq = refs::Request {
                    a,
                    ws: &mut self.workspace,
                    dir: &dir,
                    overlays: &overlays,
                    recycle: self.recycle,
                    enc: self.enc,
                    uri_of: &uri_of,
                };
                match method {
                    "textDocument/references" => {
                        let include = params["context"]["includeDeclaration"].as_bool().unwrap_or(true);
                        refs::references(&mut rq, file, off, include)
                    }
                    "textDocument/prepareRename" => {
                        refs::prepare_rename(&mut rq, file, off).map_err(|m| (REQUEST_FAILED, m))?
                    }
                    _ => {
                        let new = params["newName"].as_str().ok_or((INVALID_PARAMS, "missing newName".to_string()))?;
                        refs::rename(&mut rq, file, off, new).map_err(|m| (REQUEST_FAILED, m))?
                    }
                }
            }
            "textDocument/documentSymbol" => features::document_symbols(&cx, file),
            "textDocument/signatureHelp" => features::signature_help(&cx, file, at(&cx)),
            "textDocument/completion" => {
                let off = at(&cx);
                self.completion(&path, off)
            }
            _ => Value::Null,
        })
    }

    fn formatting(&mut self, path: &Path) -> Result<Value, (i64, String)> {
        let Some(doc) = self.docs.get(path) else { return Ok(Value::Null) };
        // A document that does not parse is left alone.
        let mut session = crate::driver::Session::new();
        let id = session.sources.add("<format>", doc.text.clone());
        let _ = session.parse(id);
        if session.diags.has_errors() {
            return Ok(Value::Null);
        }
        match crate::fmt::format_source(&doc.text) {
            Ok(new) if new == doc.text => Ok(json!([])),
            Ok(new) => Ok(features::full_edit(&doc.text, &new, self.enc)),
            // Text that does not parse cannot be formatted: no edits.
            Err(_) => Ok(Value::Null),
        }
    }

    fn completion(&mut self, path: &Path, off: u32) -> Value {
        let dir = dir_of(path);
        let uris = self.uri_map();
        let uri_of = |p: &Path| uris.get(p).cloned().unwrap_or_else(|| path_to_uri(p));
        let a = &self.analyses[&dir];
        let file = a.file_id(path).expect("checked by the caller");
        let text = a.text(file).to_string();
        let off = off as usize;
        let items = match features::member_context(&text, off) {
            Some((lo, dot)) => {
                let cx = Ctx { a, enc: self.enc, uri_of: &uri_of };
                match features::members(&cx, file, lo as u32, dot as u32) {
                    Some(items) => items,
                    None => {
                        // The text up to the dot did not check as a receiver: check it
                        // again without the dot and what follows it up to the cursor.
                        let patched = format!("{}{}", &text[..dot], &text[off..]);
                        let mut overlays = self.overlays();
                        overlays.insert(path.to_path_buf(), patched);
                        let b = Analysis::run(&dir, &overlays, path, self.recycle);
                        match b.file_id(path) {
                            Some(f) => {
                                let cx = Ctx { a: &b, enc: self.enc, uri_of: &uri_of };
                                features::members(&cx, f, lo as u32, dot as u32).unwrap_or_default()
                            }
                            None => vec![],
                        }
                    }
                }
            }
            None => {
                let cx = Ctx { a, enc: self.enc, uri_of: &uri_of };
                features::names(&cx, file, off as u32)
            }
        };
        json!({"isIncomplete": false, "items": items})
    }
}
