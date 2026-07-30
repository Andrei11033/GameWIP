@page window_fullscreen_and_monitors Fullscreen, monitors, and DPI

## Monitor snapshots

`getMonitors()` materializes current monitor snapshots. Each snapshot contains a process-local ID, UTF-8 display name, logical bounds and work area, content scale and effective DPI, physical dimensions where available, and primary status.

Monitor IDs persist for a known native display device within the process but are not serialization IDs. Display reconfiguration may invalidate an ID. Resolve it through `getMonitor()` before reuse; `NotFound` means the monitor is no longer current.

## Display modes

Display-mode resolution is physical pixels. Refresh rates are integer millihertz, color depth is bits per pixel, and interlacing is explicit. Enumeration removes exact duplicates. Current and preferred queries return backend snapshots.

## Mode transitions

Windowed mode uses the configured decorations, controls, resizability, and saved placement. Borderless fullscreen applies a popup frame at the selected monitor bounds without changing its display mode. Exclusive fullscreen selects an exact requested native mode when supplied; without one it uses the current mode.

Transitions snapshot windowed placement and native styles. A failed transition restores prior styles, placement, and display mode where possible and returns the native failure. Leaving exclusive mode and explicit close restore the saved desktop mode. Exclusive mode suspends on focus loss and resumes on focus acquisition; `FullscreenInfo::suspended` exposes this cached state.

An invalid target monitor is rejected. An exclusive mode not present in the monitor's enumerated native modes is rejected as `InvalidArgument`.

## Logical and physical geometry

Client positions, client sizes, complete frame rectangles, insets, custom regions, and pointer regions use logical units. `framebufferSize()` and display-mode resolutions use physical pixels. Content scale is physical pixels per baseline logical unit. Effective DPI is reported separately.

The Win32 backend requests per-monitor-v2 DPI awareness before its first native Window creation. Moving between monitors refreshes cached logical geometry, framebuffer size, content scale, effective DPI, and current monitor. Corresponding events may coalesce, but getters remain authoritative.

## Display changes

A native display change queues `DisplayConfigurationChangedEvent` and refreshes the current monitor. Applications holding monitor snapshots should enumerate again. Monitor unplug and exclusive-display scenarios require manual validation on representative hardware.
