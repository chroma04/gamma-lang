//! JSON-RPC 2.0 messages with `Content-Length` framing (the base protocol of LSP).

use serde_json::Value;
use std::io::{self, BufRead, Read, Write};

/// The longest header line and the largest body that are accepted.
const MAX_HEADER_LINE: u64 = 8 * 1024;
const MAX_BODY: u64 = 256 * 1024 * 1024;

/// Reads one message. Returns `None` at the end of the input.
pub fn read_message(input: &mut impl BufRead) -> io::Result<Option<Value>> {
    let mut length: Option<usize> = None;
    loop {
        let mut line = String::new();
        if input.by_ref().take(MAX_HEADER_LINE + 1).read_line(&mut line)? == 0 {
            return Ok(None);
        }
        if line.len() as u64 > MAX_HEADER_LINE {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "header line is too long"));
        }
        let line = line.trim_end_matches(['\r', '\n']);
        if line.is_empty() {
            if length.is_some() {
                break;
            }
            // A stray blank line between messages.
            continue;
        }
        if let Some((name, value)) = line.split_once(':')
            && name.trim().eq_ignore_ascii_case("content-length")
        {
            length = Some(
                value
                    .trim()
                    .parse()
                    .map_err(|_| io::Error::new(io::ErrorKind::InvalidData, "bad Content-Length header"))?,
            );
        }
        // Other headers (`Content-Type`) are ignored.
    }
    let length = length.unwrap_or(0) as u64;
    if length > MAX_BODY {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "message body is too large"));
    }
    // Read at most `length` bytes without trusting it for an allocation.
    let mut body = Vec::new();
    input.take(length).read_to_end(&mut body)?;
    if (body.len() as u64) < length {
        return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "message body is shorter than its Content-Length"));
    }
    serde_json::from_slice(&body).map(Some).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
}

/// Writes one message with its header.
pub fn write_message(out: &mut impl Write, msg: &Value) -> io::Result<()> {
    let body = serde_json::to_vec(msg).map_err(io::Error::other)?;
    write!(out, "Content-Length: {}\r\n\r\n", body.len())?;
    out.write_all(&body)?;
    out.flush()
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn frame(body: &str) -> Vec<u8> {
        format!("Content-Length: {}\r\n\r\n{}", body.len(), body).into_bytes()
    }

    #[test]
    fn reads_consecutive_messages() {
        let mut data = frame(r#"{"jsonrpc":"2.0","method":"a"}"#);
        data.extend(frame(r#"{"jsonrpc":"2.0","method":"b"}"#));
        let mut input = io::Cursor::new(data);
        assert_eq!(read_message(&mut input).unwrap().unwrap()["method"], "a");
        assert_eq!(read_message(&mut input).unwrap().unwrap()["method"], "b");
        assert!(read_message(&mut input).unwrap().is_none());
    }

    #[test]
    fn length_counts_bytes_not_characters() {
        let body = r#"{"text":"héllo ✓"}"#;
        assert!(body.len() > body.chars().count());
        let mut input = io::Cursor::new(frame(body));
        assert_eq!(read_message(&mut input).unwrap().unwrap()["text"], "héllo ✓");
    }

    #[test]
    fn accepts_other_headers_and_any_case() {
        let body = r#"{"a":1}"#;
        let data = format!(
            "content-type: application/vscode-jsonrpc; charset=utf-8\r\nCONTENT-LENGTH: {}\r\n\r\n{}",
            body.len(),
            body
        );
        let mut input = io::Cursor::new(data.into_bytes());
        assert_eq!(read_message(&mut input).unwrap().unwrap()["a"], 1);
    }

    #[test]
    fn truncated_body_is_an_error() {
        let mut input = io::Cursor::new(b"Content-Length: 50\r\n\r\n{}".to_vec());
        assert!(read_message(&mut input).is_err());
    }

    #[test]
    fn huge_length_is_an_error_not_an_allocation() {
        let mut input = io::Cursor::new(b"Content-Length: 99999999999999\r\n\r\n{}".to_vec());
        assert!(read_message(&mut input).is_err());
    }

    #[test]
    fn an_endless_header_line_is_an_error() {
        let mut input = io::Cursor::new(vec![b'x'; 100_000]);
        assert!(read_message(&mut input).is_err());
    }

    #[test]
    fn a_body_over_the_cap_is_refused_before_reading() {
        let mut input = io::Cursor::new(b"Content-Length: 300000000\r\n\r\n{}".to_vec());
        assert!(read_message(&mut input).is_err());
    }

    #[test]
    fn a_bad_length_does_not_swallow_the_next_message() {
        let mut data = b"Content-Length: nope\r\n\r\n".to_vec();
        data.extend(frame(r#"{"a":1}"#));
        let mut input = io::Cursor::new(data);
        assert!(read_message(&mut input).is_err());
        assert_eq!(read_message(&mut input).unwrap().unwrap()["a"], 1);
    }

    #[test]
    fn invalid_json_is_an_error() {
        let mut input = io::Cursor::new(frame("{nope"));
        assert!(read_message(&mut input).is_err());
    }

    #[test]
    fn written_messages_round_trip() {
        let mut out = vec![];
        write_message(&mut out, &json!({"jsonrpc": "2.0", "id": 1, "result": null})).unwrap();
        assert!(out.starts_with(b"Content-Length: "));
        let mut input = io::Cursor::new(out);
        assert_eq!(read_message(&mut input).unwrap().unwrap()["id"], 1);
    }
}
