@page desktop_fullscreen_monitors Fullscreen and displays

Window distinguishes monitor identity, physical display modes, and the policy
used to place a Window on a display. That distinction is essential when moving
between windowed, borderless, and exclusive fullscreen states.

## Display types

Monitor identity and physical modes are grouped under `Desktop::Types::Display`:

- `MonitorId` is a process-local identity for a known monitor and uses `isValid()`.
- `Mode` describes physical resolution, millihertz refresh, color depth, and interlace state.
- Display queries return `Types::Result<T>`; their optional `value` is present
  only when `status.ok()` is true.

Rich monitor snapshots and OS color state are opt-in through `desktop/display_info.h`:

- `Info` describes monitor geometry, work area, DPI/scale, physical size, name, and primary state.
- Monitor and color queries return `Types::Result<T>`.
- `ColorSpace` and `ColorInfo` report OS HDR/WCG/color information; physical
  measurements are optional when the operating system does not provide them.

## Display operations

`Desktop::Display` owns display discovery and inspection. Mode-only code can include `desktop/display.h` and call `getModes()`, `getCurrentMode()`, or
`getPreferredMode()`.

Code that needs monitor enumeration or color inspection includes `desktop/display_info.h` and uses `getMonitors()`, `getPrimaryMonitor()`,
`getMonitor()`, and `getColorInfo()`.

`getColorInfo(const Window&)` is a checked convenience query for the display relevant to a Window. It exists because selecting the relevant
native display is platform behavior; equivalent overloads are not added to unrelated operations merely for symmetry.

## Window modes

Top-level Window presentation is cached as `Types::WindowMode`:

- `Windowed` keeps ordinary desktop placement.
- `BorderlessFullscreen` fills the selected monitor without changing its physical display mode.
- `ExclusiveFullscreen` may switch to an exact requested `Types::Display::Mode`.

Callers construct a `Types::WindowModeRequest` with named factories. The
request owns only presentation concerns: `windowed()` restores ordinary
windowing, `borderlessFullscreen(monitor)` fills a monitor without changing
physical mode, and `exclusiveFullscreen(monitor, mode)` requests a physical
mode. The no-argument `exclusiveFullscreen()` keeps the selected monitor's
current physical mode. A missing target monitor means current/nearest, then
primary; an unknown nonzero ID is rejected.

`Types::FullscreenState` reports the active state. `requestedDisplayMode` is
what the caller asked for, while `activeDisplayMode` is what the monitor is
using. `displayModeMatchedExactly` distinguishes an exact match from a
fallback. `monitor` and both display modes are optional because the state is
empty in windowed mode or when the platform cannot report a value.

`windowed.clientSize` is logical geometry and remains the restored windowed
size. It is not the same thing as `Display::Mode::resolution`, which is physical
monitor geometry. Windowed geometry mutations remain invalid while fullscreen
is active.

## Display changes

The Win32 backend recovers fullscreen state when display topology changes and queues `Types::Events::DisplayConfigurationChanged` / `MonitorChanged` /
`WindowModeChanged` as appropriate. Color configuration changes are also surfaced through display-configuration events; callers re-query
`Display::getColorInfo()` for current facts.

The Win32 color query uses a current-thread DXGI factory to observe configuration currency while the calling thread owns a Window. Standalone monitor
queries release that factory before returning. Closing the thread's final Window also releases it, keeping COM cleanup inside an ordinary Desktop
operation rather than process-exit thread-local destruction.
