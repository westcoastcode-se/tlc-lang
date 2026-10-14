use zed_extension_api::{self as zed, Command, LanguageServerId, Result, Worktree};

struct TlcExtension;

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
        // Wasm cannot see the project tree. The shell prefers <root>/build/tlc, then tlc on PATH.
        if root.is_empty() {
            let command = worktree.which("tlc").ok_or_else(|| {
                "tlc was not found. Build it with: cmake --build build --target tlc".to_string()
            })?;
            return Ok(Command {
                command,
                args: vec!["lsp".into()],
                env: worktree.shell_env(),
            });
        }
        let script = r#"if [ -n "$1" ] && [ -x "$1/build/tlc" ]; then exec "$1/build/tlc" lsp; fi
if command -v tlc >/dev/null 2>&1; then exec tlc lsp; fi
echo "tlc was not found. Build it with: cmake --build build --target tlc" >&2
exit 1"#;
        Ok(Command {
            command: "/bin/sh".into(),
            args: vec!["-c".into(), script.into(), "sh".into(), root],
            env: worktree.shell_env(),
        })
    }
}

zed::register_extension!(TlcExtension);
