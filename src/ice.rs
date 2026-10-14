//! Reporting of internal compiler errors (panics in the compiler or interpreter).
//!
//! [`install`] replaces the default panic hook with one that records the panic's
//! message and location without printing. The command-line driver prints the report
//! with [`take_report`] if the compiler thread ends in a panic, so panics that
//! are caught and handled (such as an interpreter fiber's) stay silent.
//! With `RUST_BACKTRACE` set the default hook runs as well, to show the backtrace.

use std::panic::PanicHookInfo;
use std::sync::Mutex;

static LAST: Mutex<Option<String>> = Mutex::new(None);

/// Installs the hook. Call once, early, in a program that wants [`take_report`].
pub fn install() {
    let default = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        // With `/` on every system, as diagnostics name files.
        let location = info.location().map(|l| format!("{}:{}:{}", l.file().replace('\\', "/"), l.line(), l.column()));
        if let Ok(mut last) = LAST.lock() {
            *last = Some(describe(&message(info), location.as_deref()));
        }
        if std::env::var_os("RUST_BACKTRACE").is_some() {
            default(info);
        }
    }));
}

fn message(info: &PanicHookInfo) -> String {
    let p = info.payload();
    p.downcast_ref::<&str>()
        .map(|s| s.to_string())
        .or_else(|| p.downcast_ref::<String>().cloned())
        .unwrap_or_else(|| "unknown panic".to_string())
}

/// `internal compiler error: <message> at <location>`.
pub fn describe(message: &str, location: Option<&str>) -> String {
    match location {
        Some(l) => format!("internal compiler error: {} at {}", message, l),
        None => format!("internal compiler error: {}", message),
    }
}

/// The text to print for the last recorded panic, with the request to report it.
pub fn take_report() -> String {
    let what = LAST.lock().ok().and_then(|mut l| l.take()).unwrap_or_else(|| describe("unknown panic", None));
    format!(
        "{what}\n\nThis is a bug in gamma, not in your program. Please report it, with the input that \
         triggers it and the output of `gamma version` ({}).\nRun with RUST_BACKTRACE=1 for a backtrace.",
        env!("CARGO_PKG_VERSION")
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn describes_message_and_location() {
        assert_eq!(describe("boom", Some("src/x.rs:1:2")), "internal compiler error: boom at src/x.rs:1:2");
        assert_eq!(describe("boom", None), "internal compiler error: boom");
    }
}
