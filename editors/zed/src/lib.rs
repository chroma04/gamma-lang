//! The Zed extension for Gamma: it starts the language server, `gamma lsp`.

use zed_extension_api::{self as zed, LanguageServerId, Result};

struct GammaExtension;

impl zed::Extension for GammaExtension {
    fn new() -> Self {
        GammaExtension
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let path = worktree.which("gamma").ok_or_else(|| {
            "cannot find `gamma` on your PATH; install it (https://github.com/chroma04/gamma-lang#installing) and restart Zed"
                .to_string()
        })?;
        Ok(zed::Command { command: path, args: vec!["lsp".to_string()], env: worktree.shell_env() })
    }
}

zed::register_extension!(GammaExtension);
