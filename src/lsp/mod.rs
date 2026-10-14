//! The language server behind `gamma lsp`: the Language Server Protocol over standard
//! input and output.
//!
//! The server is single-threaded apart from a thread that reads messages. Each open
//! document is kept as text, and the directory (module) that contains it is checked
//! with the open documents standing in for the files on disk; see `docs/architecture.md`.

mod analysis;
mod features;
mod position;
mod server;
mod transport;

use std::io::{self, BufReader};
use std::sync::mpsc;

/// Serves LSP on standard input and output until the client says `exit` or closes the
/// input. Returns the process exit code: 0 after `shutdown`, 1 otherwise.
pub fn run_stdio() -> Result<u8, String> {
    let (tx, rx) = mpsc::channel();
    std::thread::Builder::new()
        .name("gamma-lsp-reader".into())
        .spawn(move || {
            let stdin = io::stdin();
            let mut input = BufReader::new(stdin.lock());
            loop {
                match transport::read_message(&mut input) {
                    Ok(Some(m)) => {
                        if tx.send(Ok(m)).is_err() {
                            break;
                        }
                    }
                    Ok(None) => break,
                    // Well-framed text that is not JSON: report it and read on.
                    Err(e) if e.kind() == io::ErrorKind::InvalidData => {
                        if tx.send(Err(e.to_string())).is_err() {
                            break;
                        }
                    }
                    Err(_) => break,
                }
            }
        })
        .map_err(|e| format!("cannot start the reader thread: {}", e))?;
    let stdout = io::stdout();
    let mut server = server::Server::new(stdout.lock());
    server.recycle = true;
    Ok(server.run(rx))
}
