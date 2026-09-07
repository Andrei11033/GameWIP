@page desktop_manual_validation Manual validation scenarios

Run these scenarios in a normal interactive desktop session. Record the OS version, display topology, scale factors, GPU and driver, and pass/fail
notes. Before ending a scenario, restore the desktop and Window to their original display mode, cursor-confinement, topmost, and opacity settings.

The opt-in validation runner provides the complete guided workflow:

```powershell
.\build\test\GameWIPTests.exe --test-module=desktop --manual-tests
```

Answer `yes`, `no`, or `skip` for every manual observation. The runner assigns `GameWIP.Validation.DesktopManualTests` as its explicit process
AppUserModelID so independent validation Windows appear in a dedicated GameWIP taskbar group instead of being grouped under the launching editor or
terminal. Windows may combine multiple independent validation Windows into that one group according to the user's taskbar settings. The diagnostics
companion and intentionally owned tool Windows remain excluded from independent taskbar entries.

The always-on-top `GameWIP Window manual-test diagnostics` companion appears near the upper-right of the primary work area and shows the expected
outcome and live Window state while the runner keeps native events pumping. Its native section reports the actual HWND frame/client rectangles,
monitor rectangle, DPI, style bits, fullscreen conformance, and taskbar eligibility independently from the portable cache. File-drop checks show
receipt immediately in that companion and in the validation Window title. A failed setup operation reports portable and native diagnostics and
suppresses dependent questions.

The validation executable paints a renderer-free blue/cyan GDI surface while each prompt waits. The inset cyan marker makes the current client
boundary visible after resize and fullscreen transitions. Custom-chrome checks draw the installed drag and caption-control regions directly on that
surface, then draw the old strip as inactive and the replacement strip in a distinct color after the layout changes. The artwork is owned by the
Desktop validation module because it depends on Window state and Win32 native interop; TestSupport continues to own only the reusable prompt and
result-recording contract.

Hardware or topology that is unavailable must be recorded as skipped rather than passed. Renderer-dependent transparency and framebuffer-alpha
observations require a renderer-backed host and therefore skip in `GameWIPTests`. Deterministic automated suites own unsafe internal lifecycle,
failure-injection, event-ordering, and pointer-mask generation cases identified below.

To repeat one section, add `--desktop-manual-suite=<name>`. The accepted names are documented in @ref desktop_testing. Use `borderless`, `exclusive`,
or `topology` to isolate the corresponding part of the complete `fullscreen` workflow. Manual report output is flushed per line and includes
timestamped before/after cached and native mode geometry for every display-changing request.

## Lifecycle and multiple windows

| Scenario | Expected behavior |
| --- | --- |
| Visible focused Window | Resize, move, minimize, maximize, restore, decline one system close with `clearCloseRequest()`, then accept a later request and close explicitly. |
| Two independent Windows on one thread | Events route to the correct queue, and closing either Window leaves the other operational without WindowManager. |
| Owned tool Window | Activation, close, and runtime owner replacement or removal retain native, stable z-order and minimization behavior. |
| Non-activating show | Showing a hidden Window while another application is focused does not activate it; `requestFocus()` reports the OS-policy result separately. |
| Wrong-thread destruction | Where safely reproducible, owner-dispatcher pumping releases native resources, IDs, icons, cursor state, and exclusive display state exactly once. |
| Unexpected native destruction | `isOpen()` becomes false, lifetime state becomes `NativeDestroyedPendingFinalize`, one typed `NativeDestroyed` event is delivered, native mutations return `NotOpen`, reopen returns `AlreadyOpen` before finalization, and owner-thread `close()` permits a later reopen. |
| Owner-thread exit | The dispatcher restores exclusive state and destroys the HWND before a surviving portable object is released, without duplicate cleanup or a stale ID. |

## Custom chrome

| Scenario | Expected behavior |
| --- | --- |
| Initial labeled regions | The teal drag region and visible system, minimize, maximize, and close regions provide native dragging, edge/corner resizing, snap layouts, system menu, minimize, maximize/restore, and close behavior. |
| Runtime region replacement | The red former strip and its controls stop responding; only the green replacement strip remains draggable. |
| DPI variation | The same behavior holds at 100%, 125%, 150%, and 200% scale and after movement between differently scaled monitors. |

## Native child surfaces

| Scenario | Expected behavior |
| --- | --- |
| Native descendant | With `--desktop-manual-suite=child-surface`, the labeled Win32 button is a real descendant inside the ChildSurface region. |
| Mixed-DPI movement | The host preserves its logical rectangle while its physical extent follows the destination DPI. |
| Shutdown order | The external descendant is destroyed before `ChildSurface::close()`, and the parent then closes without stale native UI or taskbar state. |

## Layered and pointer behavior

| Scenario | Expected behavior |
| --- | --- |
| Whole-Window opacity | Values of 1.0, intermediate opacity, and 0.0 do not alter input behavior. |
| Transparent framebuffer | Compositor transparency and redraw remain coherent. |
| Whole-Window `ClickThrough` | Client and system-frame input reaches a different application below; restoring `Normal` restores title-bar, resize-border, and client input. |
| Unsupported region modes | `AcceptRegions` and `IgnoreRegions` return `Unsupported` without changing the current pointer mode while `PointerRegions` is false. |
| Future regional capability | Rectangular or per-pixel pass-through is valid only when a backend advertises it and routing reaches a different underlying application; same-thread-only routing is insufficient. |
| Pointer-mask revisions | Movement preserves a published first/last-pixel mask, framebuffer resize invalidates it, clearing removes it, and an out-of-order stale GPU readback cannot replace the newest revision. |

## DPI and coordinates

| Scenario | Expected behavior |
| --- | --- |
| Mixed-DPI topology | Monitors may lie on either side of the primary and use negative x or y virtual-screen origins. |
| `PreserveLogicalClientSize` | Crossing a DPI boundary preserves logical size while framebuffer pixels change. |
| `PreservePhysicalClientSize` | Crossing a DPI boundary preserves framebuffer pixels while logical size changes. |
| Coordinate conversion | `clientToScreen()` and `screenToClient()` produce the expected integral-pixel rounding near every edge. |
| Display rectangles | Monitor bounds and work areas remain comparable physical virtual-screen rectangles and are not independently scaled. |

## Cursor

| Scenario | Expected behavior |
| --- | --- |
| Standard shapes | Every standard cursor renders at the center of the visible client. |
| Focused cursor modes | Hidden, confined, hidden-confined, and relative modes match their documented behavior. |
| Focus transitions | Alt-tab, minimize, hide, restore, and close always release confined or relative system state; exclusive relative centering resumes only while focused. |
| Warping and DPI | Warping to client corners reports the expected logical positions at each DPI scale. |
| Custom cursor restoration | Normal mode restores the same custom image after hidden and relative modes. |
| Multi-variant custom cursor | Physical size and hotspot follow the destination DPI without a visible resource rebuild. |
| Shared custom cursor | Restoring a system shape on one of two Windows does not change the other Window's custom selection. |

## Files and shell behavior

| Scenario | Expected behavior |
| --- | --- |
| Enabled lightweight file drops | One file, multiple files, Unicode paths, and paths containing spaces arrive as one grouped event with an optional client position. |
| Disabled lightweight file drops | No file-drop event is produced. |
| Shell-visible Window state | The blue/cyan patterned icon is correct at small and large shell sizes; attention, focusability, disabled interaction, topmost state, and standard controls follow their requested state. |
| Resizable/maximizable combinations | Every valid combination works, invalid transition orders fail without partial change, and closable/minimizable remain independent. |
| Owned Window taskbar state | An owned Window has no independent entry by default; removing and restoring the owner restores the corresponding styles and taskbar behavior. |

## Clipboard interoperability

These scenarios use normal desktop applications and do not require an open GameWIP Window:

| Scenario | Expected behavior |
| --- | --- |
| Text in both directions | ASCII, multibyte UTF-8, and non-BMP text is exact when pasted into Notepad; text copied from Notepad returns as the expected UTF-8 from `readText()`. |
| Paths in both directions | Published absolute Unicode or nonexistent paths remain inspectable by a compatible Explorer/desktop workflow; `readFiles()` preserves the native order and spelling of real Explorer files without reading their contents. |
| Images in both directions | Published RGBA pixels retain orientation, channel order, and alpha in an image-capable application; imported RGB pixels use alpha 255 when native alpha is not explicit. |
| Registered custom format | Two independent processes agreeing on a name and schema exchange opaque bytes including `0x00`; names differing only by case resolve to the same Win32 format. |
| Busy Clipboard | `kNoWait` returns promptly, a finite timeout remains bounded without busy spinning, and the next operation succeeds after the external owner releases the Clipboard. |
| Zero-byte custom publication | Immediate publication reports `Unsupported` without clearing existing contents and without substituting a byte or delayed renderer. |

Record the applications/versions used and whether each direction passed. Custom interoperability proves only the agreed name/schema, not universal
interpretation of arbitrary registered formats.

## Native data drag and drop

Record the source and target applications and exact formats for each direction.
Run the dedicated guided source/target harness from the repository root:

```powershell
.\build\test\GameWIPTests.exe --test-module=desktop --manual-tests --desktop-manual-suite=drag-drop
```

The green source Window starts `beginDrag()` when the requested mouse button is
held inside it. The blue target uses a whole-client Region 1 with `Copy`
preferred; its inset green Region 2 overlaps it and prefers `Move`. Live
`Entered`, `Moved`, region-transition, `Left`, and `Dropped` counts appear in the
diagnostics Window.
The runner checks same-process payload bytes and negotiated effects after each
accepted prompt. Answer `skip`—never `yes`—when a controlled custom or malformed
`IDataObject` provider/consumer is unavailable.

| Scenario | Expected behavior |
| --- | --- |
| Overlapping and resizable regions | The last matching supplied region wins, the whole-client region follows resize, one `Entered` and one `Left` delimit the top-level session, and region changes appear only as `Moved` previous/current IDs. |
| Foreign sources into GameWIP | UTF-8 text, single and multiple files including Unicode paths, an image with alpha/orientation markers, and an agreed custom binary format produce a final `Dropped` event that owns the complete payload in accepted-region order. |
| GameWIP sources into foreign targets | Explorer or another compatible application consumes each portable format. Repository paths advertise `Copy`; any foreign `Move` uses disposable files. Repeated requests for one native format continue succeeding after caller source storage has gone out of scope. |
| Effect negotiation | Target preference and the `Copy`/`Move`/`Link` fallback choose among multiple advertised effects; advertising one source effect forces it. Ctrl, Shift, and Alt do not alter portable negotiation. |
| Trigger-button termination | Separate Left, Right, and Middle runs reject an unheld configured button before modal entry, complete when that button is released, report Escape as successful cancellation, and ignore unrelated-button changes for termination. |
| Same-process transfer | A GameWIP source Window can drop onto a second GameWIP target while normal geometry and presentation events continue through the modal loop; the target receives a complete `Dropped` event. |
| Malformed or pathological foreign provider | A later selected-format failure, malformed Unicode/DIB/HDROP data, or excessive enumeration returns `Effect::None` and never queues a successful `Dropped` event. |
| Lightweight-mode conflict | Full target open returns `ResourceBusy` without disabling active lightweight file drops, and enabling lightweight mode returns `ResourceBusy` while the full target is open. |
| Cancellation and source ownership | Cancellation and completed `Copy`, `Move`, and `Link` outcomes never cause GameWIP itself to delete, rename, or mutate source data. Disposable paths isolate any mutation performed by a foreign target implementing `Move`. |

Win32 immediate publication cannot represent an exact zero-byte custom
`HGLOBAL`; record `Unsupported` without substituting a byte or delayed provider.

## Fullscreen and display topology

| Scenario | Expected behavior |
| --- | --- |
| Borderless fullscreen on each monitor | The blue surface and cyan inset marker reach every display edge; native popup/visible styles and HWND bounds match the monitor; the Window remains reachable through the taskbar or Alt+Tab; saved windowed placement returns on exit. |
| Enumerated exclusive mode | The focused Window covers the display, Alt+Tab exposes active and suspended states, the inactive state reports `suspended=true`, and desktop mode is restored on exit and close. |
| Unsupported exact mode | The request fails without changing Window or display state. |
| Movement between different DPI values | Logical client geometry, physical framebuffer extent, scale/DPI events, and current monitor remain coherent. |
| Display topology change | Re-enumeration follows the display event, and stale monitor IDs fail cleanly. |
| Active target disconnection | Exclusive display state is restored and the Window visibly recovers to windowed mode on the surviving primary monitor. |
| Recovery event sequence | Recovery clears `FullscreenInfo` and orders display configuration, mode, optional monitor, optional geometry/framebuffer, then optional DPI/content-scale events; restoration or repositioning failure reaches the pump without stale fullscreen state. |

## HDR and advanced color

| Scenario | Expected behavior |
| --- | --- |
| SDR-only display | Monitor and Window queries agree on identity; HDR is unsupported and disabled; active color is SDR or unknown only when the driver cannot classify it; unavailable optional metadata remains zero. |
| HDR-capable display with HDR disabled | Support remains true, `hdrEnabled` remains false, and extra channel precision alone does not produce an HDR classification. |
| HDR state change | Pumping delivers `DisplayConfigurationChanged`; a new query reflects enablement and `Hdr10Pq` when the driver reports PQ output, then returns to the disabled state after HDR is disabled. |
| Movement between SDR and HDR monitors | `MonitorChanged` is delivered, and the Window query follows the destination monitor and its state. |
| Luminance metadata | Where supported, minimum, peak, and full-frame values agree with the display/driver report; SDR white level uses nits, including 200 nits for native value 2500. |
| Display disconnect/reconnect | The stale `MonitorId` fails safely, and a newly enumerated ID returns current metadata. |
| Windows 10 compatibility | Without the Windows 11 advanced-color query, the documented legacy query remains functional and unavailable WCG-specific metadata remains unknown rather than fabricated. This is optional compatibility coverage outside the supported Windows 11 development host. |

## Modern Windows capabilities

| Scenario | Expected behavior |
| --- | --- |
| System backdrops | Windows 11 build 22621 or newer applies and clears every `BackdropEffect`; older supported builds return `Unsupported`. |
| Redirection-bitmap alpha | Windows 11 build 26100 or newer presents renderer-provided premultiplied alpha through `DWMWA_REDIRECTIONBITMAP_ALPHA`; older builds return `Unsupported` from open without a partial Window. |
| Opacity independence | Whole-Window opacity remains independent from framebuffer alpha. |

## Failure observations

For every failed checked operation, record the portable error code, native code, diagnostic text, `isOpen()`, and relevant cached properties. Verify
the Window remains either unchanged and retryable or completely closed according to @ref desktop_lifecycle_events.

## Related pages

- @ref desktop_coordinates_and_dpi
- @ref desktop_fullscreen_monitors
- @ref desktop_testing
- @ref desktop_clipboard
