use zed_extension_api::{self as zed, Command, LanguageServerId, Result, Worktree};

struct TlcExtension;

// Wasm cannot see the project tree, so the path is handed to Zed and started there.
fn project_binary(root: &str) -> String {
    if root.ends_with('/') {
        format!("{root}build/tlc")
    } else {
        format!("{root}/build/tlc")
    }
}

impl zed::Extension for TlcExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &Worktree,
    ) -> Result<Command> {
        let root = worktree.root_path();
        let command = if root.is_empty() {
            worktree.which("tlc").ok_or_else(|| {
                "tlc was not found. Build it with: cmake --build build --target tlc".to_string()
            })?
        } else {
            project_binary(&root)
        };
        Ok(Command {
            command,
            args: vec!["lsp".into()],
            env: worktree.shell_env(),
        })
    }
}

zed::register_extension!(TlcExtension);
