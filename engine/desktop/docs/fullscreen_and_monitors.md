@page desktop_fullscreen_monitors Fullscreen and displays

Window distinguishes monitor identity, physical display modes, and the policy
used to place a Window on a display. That distinction is essential when moving
between windowed, borderless, and exclusive fullscreen states.

## Display types

Monitor identity and physical modes are grouped under `Desktop::Types::Display`:

- `MonitorId` is a process-local identity for a currently known monitor and uses `isValid()`.
- `Mode` describes physical resolution, millihertz refresh, color depth, and interlace state.
- `ModesResult` and `ModeResult` are checked mode-query results.

Rich monitor snapshots and OS color state are opt-in through `desktop/display_info.h`:

- `Info`, `MonitorsResult`, and `InfoResult` describe monitor geometry, work area, DPI/scale, physical size, name, and primary state.
- `ColorSpace`, `ColorInfo`, and `ColorInfoResult` report OS HDR/WCG/color information.

## Display operations

`Desktop::Display` owns display discovery and inspection. Mode-only code can include `desktop/display.h` and call `getModes()`, `getCurrentMode()`, or
`getPreferredMode()`.

Code that needs monitor enumeration or color inspection includes `desktop/display_info.h` and uses `getMonitors()`, `getPrimaryMonitor()`,
`getMonitor()`, and `getColorInfo()`.

`getColorInfo(const Window&)` is a checked convenience query for the display currently relevant to a Window. It exists because selecting the relevant
native display is platform behavior; equivalent overloads are not added to unrelated operations merely for symmetry.

## Window modes

Top-level Window mode is `Types::Mode`:

- `Windowed` keeps ordinary desktop placement.
- `BorderlessFullscreen` fills the selected monitor without changing its physical display mode.
- `ExclusiveFullscreen` may switch to an exact requested `Types::Display::Mode`.

`Types::ModeRequest` combines the top-level mode, target monitor, and optional exact exclusive mode. An invalid monitor ID means the documented
current/primary fallback; an unknown nonzero ID is rejected.

`Types::FullscreenInfo` caches the active monitor, optional exclusive mode, exact-mode state, and suspension state. Windowed geometry mutations remain
invalid while a fullscreen mode is active.

## Display changes

The Win32 backend recovers fullscreen state when display topology changes and queues `Types::Events::DisplayConfigurationChanged` / `MonitorChanged` /
`ModeChanged` as appropriate. Color configuration changes are also surfaced through display-configuration events; callers re-query
`Display::getColorInfo()` for current facts.

The Win32 color query uses a current-thread DXGI factory to observe configuration currency while the calling thread owns a Window. Standalone monitor
queries release that factory before returning. Closing the thread's final Window also releases it, keeping COM cleanup inside an ordinary Desktop
operation rather than process-exit thread-local destruction.
