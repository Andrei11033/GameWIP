@page window_custom_cursors Custom native cursors

Custom cursors are immutable shared resources created from application-provided
RGBA8 pixels. Include `window/cursor.h` only in consumers that create, retain,
or select them. The Win32 backend advertises
`Types::Capability::CustomCursor`.

Portable consumers may query that capability before creating a resource. A
successful capability query describes backend support; use
`hasCustomCursor()` to inspect the owner-thread selection state of one open
Window.

## Images and validation

`Types::Cursor::ImageView` describes one physical-pixel image and hotspot. Rows
run from top to bottom and pixels use straight-alpha RGBA8 channel order.
`rowStrideBytes == 0` means tightly packed `width * 4` rows; a nonzero stride
may include trailing row padding. `createCursor()` copies the complete payload
and never retains the caller's span.

Width and height must be nonzero and representable by the native backend. The
hotspot must be inside the image, `intendedDpi` must be nonzero, the resolved
stride must hold one complete row, and the payload size must equal
`resolvedStride * height`. A variant set must be non-empty and contain at most
one image for each intended DPI. Invalid input returns `InvalidArgument` and an
invalid `Cursor`.

The single-image overload accepts one `ImageView`. The span overload eagerly
materializes every variant before publishing a valid resource. If any native
creation or allocation fails, already-created native variants are released and
the result contains no partial `Cursor`.

## DPI variants

Supply images at the physical sizes appropriate for the DPI values used by the
application. Selection uses the variant whose `intendedDpi` is nearest to the
Window's effective DPI; an equal-distance tie chooses the higher-DPI variant.
Input order does not affect that rule.

A `WM_DPICHANGED` transition reselects an already-created variant without
rebuilding native resources. The hotspot belongs to each variant and remains a
physical-pixel coordinate; it is not scaled independently. See
@ref window_coordinates_and_dpi for the other Window coordinate spaces.

## Selection and lifetime

`Cursor` is default-constructible, copyable, movable, and cheaply shared. A
default-constructed handle is invalid. Copies share the same immutable native
variants, and those variants remain alive until the final `Cursor` handle and
Window binding release them.

`setCursor(window, cursor)` requires a valid cursor and an open Window on its
owner thread. The Window retains its own shared reference, so the caller may
release the handle after a successful selection. The same `Cursor` may be
selected by several Windows without duplicating native variants.

`hasCustomCursor(window)` returns true only for an open Window queried on its
owner thread while a custom binding is active. It returns false for a closed
Window or a wrong-thread query.

Close, unexpected native destruction, deferred owner-thread cleanup, and a
successful system-shape replacement release the Window binding. Reopening a
Window does not restore the previous custom cursor.

## Cursor mode and system shape

A custom binding overrides the effective system `Types::CursorShape` without
changing the cached fallback shape. `CursorMode::Hidden`,
`CursorMode::HiddenConfined`, and `CursorMode::Relative` suppress cursor
display while retaining that binding. Returning to `CursorMode::Normal`
restores the custom cursor. Confinement changes pointer movement bounds
independently and does not clear the custom binding.

Calling `Window::setCursorShape()` successfully replaces the custom binding
and makes the requested system shape effective. Select the custom `Cursor`
again to restore it. A failed system-shape or custom-cursor replacement leaves
the previous selection unchanged.

## Single image

```cpp
#include "window/cursor.h"

#include <array>
#include <cstddef>

std::array<std::byte, 32 * 32 * 4> rgba8{};
GameWIP::Window::Types::Cursor::ImageView image{
    .size = {32, 32},
    .hotspot = {4, 3},
    .intendedDpi = 96,
    .rgba8 = rgba8};

auto created = GameWIP::Window::createCursor(image);
if (created.status.ok())
    static_cast<void>(GameWIP::Window::setCursor(window, created.cursor));
```

## Multiple DPI variants

```cpp
std::array<GameWIP::Window::Types::Cursor::ImageView, 3> variants{
    GameWIP::Window::Types::Cursor::ImageView{{32, 32}, {4, 3}, 96, 0, rgba96},
    GameWIP::Window::Types::Cursor::ImageView{{48, 48}, {6, 5}, 144, 0, rgba144},
    GameWIP::Window::Types::Cursor::ImageView{{64, 64}, {8, 6}, 192, 0, rgba192}};

auto created = GameWIP::Window::createCursor(variants);
if (created.status.ok())
    static_cast<void>(GameWIP::Window::setCursor(window, created.cursor));
```

## Restore and share

```cpp
if (created.status.ok())
{
    static_cast<void>(GameWIP::Window::setCursor(primary, created.cursor));
    static_cast<void>(GameWIP::Window::setCursor(tools, created.cursor));

    // A system shape clears only primary's custom binding.
    static_cast<void>(primary.setCursorShape(GameWIP::Window::Types::CursorShape::Arrow));

    // Select the same shared resource again when the override is needed.
    static_cast<void>(GameWIP::Window::setCursor(primary, created.cursor));
}
```

## Related pages

- @ref window_public_api
- @ref window_coordinates_and_dpi
- @ref window_lifecycle_events
- @ref window_chrome_and_pointer_input
