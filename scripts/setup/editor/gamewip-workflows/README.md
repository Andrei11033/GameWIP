# GameWIP workflow keybindings

This declarative VS Code extension contributes repository-scoped shortcuts for
the complete workflows in `.vscode/tasks.json`. The shortcuts are active only
when `gamewip.keybindings.enabled` is true, as it is in
`GameWIP.code-workspace`.

The Windows setup script installs this unpacked extension into the current
user's VS Code extension directory. It also generates a managed block at the
end of the user's `keybindings.json`, where VS Code gives it priority over
older personal bindings. Every managed rule keeps the repository setting in
its `when` clause, so it is inactive outside a GameWIP workspace. Setup keeps a
one-time `.gamewip-backup` of the original file and only replaces its marked
managed block on later runs.
