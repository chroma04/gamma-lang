//! The playground's example list, emitted as `assets/examples.js`.

use serde_json::{Value, json};
use std::fs;
use std::path::Path;

/// Parses `list.txt`: lines of `path | Title [| ignored…]`; blank lines and `#` comments are ignored.
pub fn parse_list(text: &str) -> Vec<(String, String)> {
    text.lines()
        .map(str::trim)
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .filter_map(|l| {
            // Further `|`-separated fields belong to other tools.
            let mut fields = l.split('|');
            Some((fields.next()?.trim().to_string(), fields.next()?.trim().to_string()))
        })
        .collect()
}

/// Renders the JavaScript file that defines `window.GAMMA_EXAMPLES`.
pub fn to_js(examples: &[Value]) -> String {
    let items: Vec<String> = examples.iter().map(|e| e.to_string().replace("</", "<\\/")).collect();
    format!("window.GAMMA_EXAMPLES = [\n{}\n];\n", items.join(",\n"))
}

/// Reads the example list (a missing list means no examples) and the sources it names.
pub fn build(root: &Path) -> Result<String, String> {
    let list = fs::read_to_string(root.join("web/examples/list.txt")).unwrap_or_default();
    let mut examples = Vec::new();
    for (path, title) in parse_list(&list) {
        let source = fs::read_to_string(root.join(&path)).map_err(|e| format!("example {path}: {e}"))?;
        let file = path.rsplit('/').next().unwrap_or(&path);
        examples.push(json!({ "title": title, "file": file, "source": source }));
    }
    Ok(to_js(&examples))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn list_parsing() {
        let l = parse_list("# c\n\nexamples/a.gm | Hello | exit 1\n  examples/b.gm|B  \nbad line\n");
        assert_eq!(l, vec![("examples/a.gm".into(), "Hello".into()), ("examples/b.gm".into(), "B".into())]);
    }

    #[test]
    fn js_is_script_safe() {
        let js = to_js(&[json!({"source": "</script>\n\"q\""})]);
        assert!(js.contains("<\\/script>\\n\\\"q\\\""));
        assert!(!js.contains("</"));
    }
}
