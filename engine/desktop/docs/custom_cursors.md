@page desktop_custom_cursors Custom native cursors

Custom cursors are immutable shared resources created from application-provided
RGBA8 pixels. Include `desktop/cursor.h` only where you create, retain, or select them.
Win32 advertises `Types::Capability::CustomCursor`; that describes backend support, while
`hasCustomCursor()` reports the owner-thread selection state of one open Window.

## Images and validation

`Types::Cursor::ImageView` describes one physical-pixel, top-to-bottom, straight-alpha RGBA8 image and hotspot.
Use `ImageView::packed()` for `width * 4` rows and `ImageView::strided()` for padded rows.
`createCursor()` copies the whole payload and never retains the caller's span.

Width and height must be nonzero and native-representable, the
hotspot inside the image, `intendedDpi` nonzero, the resolved
stride at least one row, and the payload size exactly `resolvedStride * height`. A variant set must be non-empty with at most
one image per intended DPI. Invalid input returns a failed
`Types::Result<Cursor>` with no value.

The span overload eagerly creates every variant before publishing; if any native
creation or allocation fails, created variants are released and no partial `Cursor` results.

## DPI variants

Selection uses the variant whose `intendedDpi` is nearest the
Window's effective DPI; a tie chooses the higher DPI, regardless of input order.
`WM_DPICHANGED` reselects an already-created variant without
rebuilding. The hotspot belongs to each variant and stays in physical pixels (not scaled independently). See
@ref desktop_coordinates_and_dpi.

## Selection and lifetime

`Cursor` is default-constructible (invalid), copyable, movable, and cheaply shared; copies share the same immutable native
variants, which live until the last `Cursor` handle and Window binding release them.

`setCursor(window, cursor)` requires a valid cursor and an open Window on its
owner thread. The Window keeps its own shared reference, so the caller may
release the handle afterward; several Windows may select the same `Cursor`.

`hasCustomCursor(window)` is true only for an open Window queried on its
owner thread with an active custom binding.

Close, unexpected native destruction, deferred owner-thread cleanup, and a
successful system-shape replacement release the binding. Reopening does not restore it.

## Cursor mode and system shape

A custom binding overrides the effective system `Types::CursorShape` without
changing the cached fallback shape. `CursorMode::Hidden`,
`HiddenConfined`, and `Relative` suppress display while keeping the binding; `Normal`
restores it. Confinement changes movement bounds independently and does not clear it.

A successful `Window::setCursorShape()` replaces the custom binding;
select the `Cursor` again to restore it. A failed shape or cursor replacement keeps
the previous selection.

## Single image

```cpp
#include "desktop/cursor.h"

#include <array>
#include <cstddef>

std::array<std::byte, 32 * 32 * 4> rgba8{};
auto image = GameWIP::Desktop::Types::Cursor::ImageView::packed(
    {32, 32}, {4, 3}, rgba8, 96);

auto created = GameWIP::Desktop::createCursor(image);
if (created.ok())
    static_cast<void>(GameWIP::Desktop::setCursor(window, *created));
```

## Multiple DPI variants

```cpp
std::array<GameWIP::Desktop::Types::Cursor::ImageView, 3> variants{
    GameWIP::Desktop::Types::Cursor::ImageView::packed({32, 32}, {4, 3}, rgba96, 96),
    GameWIP::Desktop::Types::Cursor::ImageView::packed({48, 48}, {6, 5}, rgba144, 144),
    GameWIP::Desktop::Types::Cursor::ImageView::packed({64, 64}, {8, 6}, rgba192, 192)};

auto created = GameWIP::Desktop::createCursor(variants);
if (created.ok())
    static_cast<void>(GameWIP::Desktop::setCursor(window, *created));
```

## Restore and share

```cpp
if (created.ok())
{
    static_cast<void>(GameWIP::Desktop::setCursor(primary, *created));
    static_cast<void>(GameWIP::Desktop::setCursor(tools, *created));

    // A system shape clears only primary's custom binding.
    static_cast<void>(primary.setCursorShape(GameWIP::Desktop::Types::CursorShape::Arrow));

    // Select the same shared resource again when the override is needed.
    static_cast<void>(GameWIP::Desktop::setCursor(primary, *created));
}
```

## Related pages

- @ref desktop_public_api
- @ref desktop_coordinates_and_dpi
- @ref desktop_lifecycle_events
- @ref desktop_chrome_and_pointer_input
