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

Open `GameWIP.code-workspace` to enable the bindings. Each shortcut runs the
corresponding `GameWIP: ...` entry in `.vscode/tasks.json`; the same workflows
remain available through **Terminal > Run Task** when a function key is
unavailable.

Game run tasks use a dedicated terminal for executable output. Their CMake
configure and build prerequisites remain in the shared task terminal, keeping
build diagnostics separate from the running game's output.

## Shortcut reference

| Shortcut | Workflow |
| --- | --- |
| `F5` | Configure, build, and run `dev`. |
| `Ctrl+F5` | Configure and build `dev` without launching it. |
| `Alt+F5` | Configure, build, and run `dev-no-tools`. |
| `F6` | Configure, build, run embedded correctness tests, and start `dev` when they pass. |
| `F7` | Configure, build, and run correctness tests. |
| `Alt+F7` | Run the standard benchmark profile and retain its tool-run results. |
| `F8` | Build, verify, and open the generated manual. |
| `Alt+F8` | Configure and run repository C++ analysis. |
| `F9` | Build the profile game, start Tracy, and run the game. |
| `Alt+F9` | Start Tracy and run profiled startup tests. |
| `F10` | Build and run tests, generate coverage, and open the report. |
| `F11` | Build and run CLANG64 AddressSanitizer tests. |
| `F12` | Configure, build, and run the release game. |

In the GameWIP workspace, `F5` and `Ctrl+F5` intentionally run repository tasks
instead of VS Code's default launch commands. The extension's `package.json`
is the source of truth for contributed bindings and the setup
script's generated user rules. Task behavior, customization rules, and
troubleshooting guidance are maintained in the generated
[development environment manual](../../../../docs/doxygen/environment_setup.md).
