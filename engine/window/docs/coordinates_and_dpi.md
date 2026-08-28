@page window_coordinates_and_dpi Coordinate spaces and DPI

Window keeps logical layout coordinates separate from physical framebuffer
pixels and reports the scale connecting them. Use the named types rather than
assuming that one integer space can represent both.

## Coordinate types

Window keeps incompatible spaces distinct:

| Type | Unit and origin | Typical use |
| --- | --- | --- |
| `LogicalPosition` | DPI-independent, client-local | Cursor positions, file drops |
| `LogicalSize` | DPI-independent client extent | Requested client size and limits |
| `LogicalRect` | DPI-independent, client-local | Custom chrome and pointer regions |
| `ScreenPosition` | Physical Win32 virtual-screen coordinates | Desktop client placement |
| `ScreenRect` | Physical Win32 virtual-screen coordinates and pixels | Outer frames, monitor bounds, work areas |
| `PixelSize` | Physical pixels | Framebuffers and display modes |

Virtual-screen x and y coordinates may be negative. Monitor origins are never independently divided by monitor DPI; all monitor and Window screen
rectangles remain comparable in one physical desktop space.

## Conversion

`clientToScreen(LogicalPosition)` returns a `ScreenPositionResult`. `screenToClient(ScreenPosition)` returns a `LogicalPositionResult`. Win32 performs
the native client/screen conversion and Window applies DPI rounding at the boundary. Round trips near fractional scale boundaries may differ by one
logical unit because physical pixels are integral.

Conversion rejects a closed Window with `NotOpen` and wrong-thread native access with `ResourceBusy`. Logical scaling and client-to-frame placement
use widened arithmetic. A value that cannot be represented by Win32's signed physical coordinates returns `InvalidArgument` with an
arithmetic-overflow diagnostic instead of wrapping. Positive extents receive the same validation after DPI scaling and frame adjustment. Returned
positions describe confirmed native geometry rather than an unverified request.

## Framebuffer and scale

`clientSize()` is logical. `framebufferSize()` is the actual physical drawable extent. `contentScale()` reports physical pixels per baseline logical
unit and `effectiveDpi()` reports the effective DPI snapshot. Applications should resize renderer-owned attachments from
`Types::Events::FramebufferSizeChanged` or the cached framebuffer getter.

## DPI resize policy

`DpiResizePolicy::PreserveLogicalClientSize` is the default. A DPI transition keeps the logical client extent and changes its physical pixel extent,
preserving approximate apparent size.

`DpiResizePolicy::PreservePhysicalClientSize` keeps the physical client pixel extent and recalculates the logical extent. It is intended for capture,
deterministic rendering tests, and fixed-resolution workflows.

`setDpiResizePolicy()` affects future DPI transitions only; it does not resize immediately. A framebuffer-size change invalidates an active packed
pointer mask. Moving the Window without changing the framebuffer extent does not.

Custom cursor images, sizes, and hotspots use physical pixels. A DPI transition selects the nearest eagerly created `intendedDpi` variant, choosing
the higher-DPI variant on an equal-distance tie; it does not rescale or rebuild an image. See @ref window_custom_cursors.

ChildSurface geometry is parent-client-relative and always preserves its authoritative logical rectangle. It has no `DpiResizePolicy`; physical
position and extent are derived from that logical rectangle at the new effective DPI. See @ref window_child_surfaces.

## Events and authority

Native geometry and DPI callbacks update cached position, logical client size, framebuffer size, scale, DPI, and current monitor before publishing
events. Geometry events may coalesce, so cached getters are the authoritative current snapshot when intermediate notifications are dropped.

## Related pages

- @ref window_fullscreen_monitors
- @ref window_renderer_integration
- @ref window_custom_cursors
- @ref window_child_surfaces
- @ref window_manual_validation
