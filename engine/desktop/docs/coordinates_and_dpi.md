@page desktop_coordinates_and_dpi Coordinate spaces and DPI

Window keeps logical layout coordinates separate from physical framebuffer pixels and reports the scale connecting them.

## Coordinate types

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

`clientToScreen(LogicalPosition)` returns `Types::Result<ScreenPosition>`; `screenToClient(ScreenPosition)` returns `Types::Result<LogicalPosition>`. Win32 does the native conversion and Window applies DPI rounding at the boundary, so round trips near fractional scales may differ by one
logical unit.

A closed Window returns `NotOpen`; wrong-thread access returns `ResourceBusy`. Scaling and client-to-frame placement
use widened arithmetic; a value unrepresentable in Win32's signed physical coordinates (including positive extents after DPI scaling and frame adjustment) returns `InvalidArgument` with an
arithmetic-overflow diagnostic instead of wrapping. Returned
positions are confirmed native geometry, not an unverified request.

## Framebuffer and scale

`clientSize()` is logical; `framebufferSize()` is the physical drawable extent; `contentScale()` is physical pixels per baseline logical
unit; `effectiveDpi()` is the DPI snapshot. Resize renderer attachments from
`Types::Events::FramebufferSizeChanged` or the cached framebuffer getter.

After `Renderer::enableConcurrentPresentationReads()` succeeds (@ref desktop_renderer_integration), these four getters and `currentMonitor()` use atomic publication and may be read from a renderer
thread; a returned pair never mixes publications, but separate calls are not a transactional snapshot. Before opt-in they are owner-thread cached getters.
Renderers may use `currentMonitor()` to select output-dependent color, HDR, or presentation policy, then re-query display details through owner-thread APIs.

## DPI resize policy

`DpiResizePolicy::PreserveLogicalClientSize` (default) keeps the logical client extent across a DPI transition, changing the physical extent to preserve apparent size.

`DpiResizePolicy::PreservePhysicalClientSize` keeps the physical extent and recalculates the logical one, for capture, deterministic rendering tests, and fixed-resolution workflows.

`setDpiResizePolicy()` affects future transitions only. A framebuffer-size change invalidates an active packed
pointer mask; moving the Window without changing the extent does not.

Custom cursor images, sizes, and hotspots use physical pixels. A DPI transition selects the nearest eagerly created `intendedDpi` variant, choosing
the higher-DPI variant on an equal-distance tie; it does not rescale or rebuild an image. See @ref desktop_custom_cursors.

ChildSurface geometry is parent-client-relative and always preserves its authoritative logical rectangle. It has no `DpiResizePolicy`; physical
position and extent are derived from that logical rectangle at the new effective DPI. See @ref desktop_child_surfaces.

## Events and authority

Native geometry and DPI callbacks update cached position, logical client size, framebuffer size, scale, DPI, and current monitor before publishing
events. Geometry events may coalesce, so cached getters provide the authoritative current values when intermediate notifications are dropped.

## Related pages

- @ref desktop_fullscreen_monitors
- @ref desktop_renderer_integration
- @ref desktop_custom_cursors
- @ref desktop_child_surfaces
- @ref desktop_manual_validation
