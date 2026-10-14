//! Format-preserving edits of `gamma.toml` for `gamma add` and `gamma remove`.

use toml_edit::{DocumentMut, InlineTable, Item, Table, Value};

/// A dependency to add.
pub enum NewDep {
    Path(String),
    Git { url: String, reference: Option<(&'static str, String)>, subdir: Option<String> },
    Url { url: String, sha256: Option<String>, subdir: Option<String> },
}

fn parse(text: &str) -> Result<DocumentMut, String> {
    text.parse::<DocumentMut>().map_err(|e| format!("gamma.toml: {}", e.message()))
}

/// `text` with the dependency `name` added to `[dependencies]`, which is created if missing.
pub fn add_dependency(text: &str, name: &str, dep: &NewDep) -> Result<String, String> {
    let mut doc = parse(text)?;
    let deps = doc.entry("dependencies").or_insert(Item::Table(Table::new()));
    let table = deps.as_table_like_mut().ok_or("`dependencies` in gamma.toml is not a table")?;
    if table.contains_key(name) {
        return Err(format!("dependency `{}` already exists in gamma.toml", name));
    }
    let mut it = InlineTable::new();
    match dep {
        NewDep::Path(p) => {
            it.insert("path", Value::from(p.as_str()));
        }
        NewDep::Url { url, sha256, subdir } => {
            it.insert("url", Value::from(url.as_str()));
            if let Some(h) = sha256 {
                it.insert("sha256", Value::from(h.as_str()));
            }
            if let Some(s) = subdir {
                it.insert("subdir", Value::from(s.as_str()));
            }
        }
        NewDep::Git { url, reference, subdir } => {
            it.insert("git", Value::from(url.as_str()));
            if let Some((k, v)) = reference {
                it.insert(*k, Value::from(v.as_str()));
            }
            if let Some(s) = subdir {
                it.insert("subdir", Value::from(s.as_str()));
            }
        }
    }
    table.insert(name, Item::Value(Value::InlineTable(it)));
    Ok(doc.to_string())
}

/// `text` without the dependency `name`.
pub fn remove_dependency(text: &str, name: &str) -> Result<String, String> {
    let mut doc = parse(text)?;
    let removed = doc.get_mut("dependencies").and_then(Item::as_table_like_mut).and_then(|t| t.remove(name)).is_some();
    if !removed {
        return Err(format!("gamma.toml has no dependency `{}`", name));
    }
    Ok(doc.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn git(reference: Option<(&'static str, &str)>) -> NewDep {
        NewDep::Git { url: "https://h/x".into(), reference: reference.map(|(k, v)| (k, v.to_string())), subdir: None }
    }

    #[test]
    fn adds_to_an_existing_table_and_keeps_comments() {
        let text = "# my app\n[package]\nname = \"app\" # the name\n\n[dependencies]\n# json is needed\njson = { path = \"../json\" }\n";
        let out = add_dependency(text, "http", &git(Some(("version", "^1.2.3")))).unwrap();
        assert!(out.starts_with("# my app\n[package]\nname = \"app\" # the name\n"));
        assert!(out.contains("# json is needed\njson = { path = \"../json\" }\n"));
        assert!(out.ends_with("http = { git = \"https://h/x\", version = \"^1.2.3\" }\n"), "{}", out);
    }

    #[test]
    fn creates_the_table() {
        let out = add_dependency("[package]\nname = \"app\"\n", "u", &NewDep::Path("../u".into())).unwrap();
        assert_eq!(out, "[package]\nname = \"app\"\n\n[dependencies]\nu = { path = \"../u\" }\n");
        let out = add_dependency("", "u", &git(None)).unwrap();
        assert!(out.contains("[dependencies]\nu = { git = \"https://h/x\" }"), "{}", out);
    }

    #[test]
    fn refuses_duplicates_and_removes() {
        let text = "[dependencies]\na = { path = \"a\" }\nb = { path = \"b\" }\n";
        assert!(add_dependency(text, "a", &NewDep::Path("x".into())).unwrap_err().contains("already exists"));
        assert_eq!(remove_dependency(text, "a").unwrap(), "[dependencies]\nb = { path = \"b\" }\n");
        assert!(remove_dependency(text, "zz").is_err());
        assert!(remove_dependency("[package]\nname = \"x\"\n", "a").is_err());
    }
}
