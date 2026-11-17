# GameWIP workflow keybindings

This small VS Code extension turns the repository workflows in
`.vscode/tasks.json` into convenient function-key shortcuts. The bindings are
active only when `gamewip.keybindings.enabled` is true, as it is in
`GameWIP.code-workspace`, so they do not take over the same keys in unrelated
workspaces.

Setup packages this extension as a VSIX and installs it for the current user.
It also generates a managed block at the
end of the user's `keybindings.json`, where VS Code gives it priority over
older personal bindings. Every managed rule keeps the repository setting in
its `when` clause, so it is inactive outside a GameWIP workspace. Setup keeps a
one-time `.gamewip-backup` of the original file and only replaces its marked
managed block on later runs.

Open `GameWIP.code-workspace` to enable the bindings. Each shortcut runs the
corresponding `GameWIP: ...` entry in `.vscode/tasks.json`; the same workflows
remain available through **Terminal > Run Task** when a function key is
unavailable.

Game run tasks use a dedicated terminal for executable output. Tasks call
`gamewip.bat` on Windows and `bash ./gamewip.sh` on Linux. Configure and build
tasks use the shared task terminal, keeping build diagnostics separate from
the running game's output.

On Linux, run and test tasks report that execution is unavailable. VS Code debug
launch is Windows-only; executable paths and runtime settings follow the
selected target on Windows.

## Shortcut reference

`Ctrl` runs the primary workflow from a clean build. `Alt` selects an alternate
workflow where one is available. Coverage and sanitizer workflows always use
clean build trees.

| Shortcut | Workflow |
| --- | --- |
| `F5` | Configure, build, and run `dev`. |
| `Alt+F5` | Configure and build `dev` without launching it. |
| `Ctrl+F5` | Clean-build `dev`, then run it. |
| `F6` | Configure, build, run embedded correctness tests, and start `dev` when they pass. |
| `Ctrl+F6` | Clean-build `dev`, then run embedded correctness tests. |
| `F7` | Configure, build, and run correctness tests. |
| `Ctrl+F7` | Clean-build and run correctness tests. |
| `Alt+F7` | Run the standard benchmark profile and retain its tool-run results. |
| `F8` | Build, verify, and open the generated manual. |
| `Ctrl+F8` | Clean-build, verify, and open the generated manual. |
| `Alt+F8` | Configure and run repository C++ analysis. |
| `F9` | Build the profile game, start Tracy, and run the game. |
| `Ctrl+F9` | Clean-build the profile game, start Tracy, and run it. |
| `Alt+F9` | Start Tracy and run profiled startup tests. |
| `F10` | Clean-build and run coverage tests, then open the report. |
| `F11` | Clean-build and run AddressSanitizer tests. |
| `Alt+F11` | Clean-build and run UndefinedBehaviorSanitizer tests. |
| `F12` | Configure, build, and run the release game. |
| `Alt+F12` | Configure and build `release` without launching it. |
| `Ctrl+F12` | Clean-build `release`, then run it. |

In the GameWIP workspace, `F5` and `Ctrl+F5` intentionally run repository tasks
instead of VS Code's default launch commands. The extension's `package.json`
is the source of truth for contributed bindings and the setup
script's generated user rules. Task behavior, customization rules, and
troubleshooting guidance are maintained in the generated
[development environment manual](../../../../docs/doxygen/environment_setup.md).
