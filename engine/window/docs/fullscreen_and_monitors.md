@page window_fullscreen_and_monitors Fullscreen, monitors, and DPI

## Monitor snapshots

`getMonitors()` materializes current monitor snapshots. Each snapshot contains a process-local ID, UTF-8 display name, physical virtual-screen bounds and work area, content scale and effective DPI, physical dimensions where available, and primary status. Bounds may have negative origins.

Monitor IDs persist for a known native display device within the process but are not serialization IDs. Display reconfiguration may invalidate an ID. Resolve it through `getMonitor()` before reuse; `NotFound` means the monitor is no longer current.

## Display modes

Display-mode resolution is physical pixels. Refresh rates are integer millihertz, color depth is bits per pixel, and interlacing is explicit. Enumeration removes exact duplicates and sorts deterministically.

The Win32 current-mode query augments `EnumDisplaySettingsExW` with the active DisplayConfig path's rational target refresh and scan-line order. Rational rates are rounded safely to the nearest millihertz; zero/unknown denominators remain zero and overflow saturates. The preferred query resolves the active GDI source through `QueryDisplayConfig()` and asks `DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE)` for the connected target's actual preferred/native mode. It does not treat the registry-stored desktop mode as preferred.

## Mode transitions

Windowed mode uses the configured decorations, controls, resizability, and saved placement. Borderless fullscreen applies a popup frame at the selected monitor bounds without changing its display mode. Exclusive fullscreen selects an exact requested native mode when supplied; without one it uses the current mode.

Transitions snapshot windowed placement and native styles. A failed transition restores prior styles, placement, and display mode where possible and returns the native failure. Leaving exclusive mode and explicit close restore the saved desktop mode. Exclusive mode suspends on focus loss and resumes on focus acquisition; `FullscreenInfo::suspended` exposes this cached state.

An invalid target monitor is rejected. An exclusive mode not present in the monitor's enumerated native modes is rejected as `InvalidArgument`.

## Geometry and DPI

Client-local positions, client sizes, custom chrome, and pointer regions use logical units. Desktop placement, complete frame rectangles, monitor bounds, and work areas use physical virtual-screen coordinates. `framebufferSize()` and display-mode resolutions use physical pixels. See @ref window_coordinates_and_dpi.

The host executable establishes Per-Monitor-V2 awareness through its manifest. Window validates that context and returns `Unsupported` when it is incompatible; it does not mutate process DPI policy.

## Display changes

A native display change refreshes monitor resolution. Applications holding monitor snapshots should enumerate again.

If a borderless or exclusive fullscreen target disappeared, Window first attempts exclusive-mode restoration, clears all stale fullscreen ownership, changes to `Windowed`, applies windowed styles, and centers a clamped saved windowed frame in the surviving primary monitor's work area. Cached mode, fullscreen data, geometry, framebuffer, DPI, scale, and monitor are updated before notifications. Recovery never leaves `FullscreenInfo` referring to the removed target.

Recovery notification order is `DisplayConfigurationChangedEvent`, `ModeChangedEvent`, optional `MonitorChangedEvent`, optional movement/client/framebuffer events, then optional `ContentScaleChangedEvent`. A failed display restore or placement is returned through the active pump result after portable state has been made non-stale.

## Related pages

- @ref window_coordinates_and_dpi
- @ref window_manual_validation
- @ref window_troubleshooting
