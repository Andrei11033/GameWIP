@page desktop_clipboard Clipboard and data transfer

`GameWIP::Desktop::Clipboard` is a synchronous stateless service for the
operating-system clipboard. Include `desktop/clipboard.h`; it includes the shared
`desktop/data_transfer.h` vocabulary. Both headers are opt-in parts of the
existing `GameWIP::Desktop` package and are not included by `desktop/window.h`.

Clipboard is desktop/process state, not state owned by a `Window`. Operations
work without opening a GameWIP Window and may run on ordinary application
threads. The service does not interpret Ctrl+C, Ctrl+X, Ctrl+V, selections,
widgets, editor commands, or drag and drop. Higher-level UI owns those choices.

## Shared transfer values

`Types::DataTransfer` is shared with native drag and drop:

- `FormatView` borrows a caller-supplied format description; `Format` owns the
  custom name returned by enumeration.
- `TextView`, `FileListView`, `ImageView`, and `CustomView` are call-scoped input
  views collected by `ItemView`.
- `Text`, `FileList`, `Image`, and `CustomData` are owning values collected by
  `Item` and `Payload`.

Text is strict UTF-8. Files use `FileSystem::Types::Path`. Images are sRGB,
8-bit RGBA, straight alpha, and top-to-bottom. Owned images are tightly packed.
Custom data is an opaque native byte block identified by an arbitrary nonempty
strict UTF-8 native format name. GameWIP adds no prefix, header, schema, or
framing.

Custom-format interoperability requires both applications to agree on the
native name and the payload schema. For an arbitrary foreign format,
`readCustomData()` can report only the native allocation extent. That extent may
include padding chosen by another implementation; an exact logical length must
come from the format's own schema.

## Timeouts and contention

Every operation has a convenience overload and an explicit-timeout overload.
The convenience form forwards to `kDefaultAccessTimeout`, currently 100 ms.
`kNoWait` is zero and performs exactly one acquisition attempt. A positive
timeout retries with private bounded backoff against `std::chrono::steady_clock`;
a negative timeout is `InvalidArgument`. The service never busy-spins or waits
forever.

The timeout bounds only GameWIP-controlled clipboard acquisition. Windows may
synchronously ask a foreign owner to render delayed data after acquisition;
Win32 provides no cancellation for that already-entered native call. Clipboard
does not create a worker thread to pretend otherwise.

## Queries and reads

`hasFormat()` returns successful `available=false` when a valid format is
absent. Standard categories require an empty `customName`; Custom requires a
nonempty strict UTF-8 name without embedded U+0000. A custom query enumerates
existing registered formats and does not register a new native identity.

`getFormats()` preserves meaningful native priority and returns owning names.
Portable text and image categories are deduplicated across native or synthesized
representations. Registered third-party names such as `HTML Format` or
`Rich Text Format` remain Custom values; unsupported unnamed standard native
formats are skipped. A failure after materialization preserves the useful
format prefix with the failure status.

The typed reads report `NotFound` for absence. Text conversion is strict and
does not normalize, repair, or change BOMs. File order is native order and no
file-system IO occurs. Images are converted into tightly packed portable RGBA8;
a valid available native image that cannot be converted reports `Unsupported`.

## Writing and external mutation

Single-format writes use the same pipeline as `write()`. Before clearing the
clipboard, the implementation validates every item, converts every value,
resolves native format identities, rejects duplicate identities, checks all
size arithmetic, and allocates every publication block. A preparation failure
returns `CommitState::NotStarted`, publishes zero formats, and leaves previous
clipboard contents untouched.

After a successful clear, items publish in caller order because Windows uses
format order as preference. Publication stops at the first failure.
`WriteResult` describes the externally committed caller-order prefix:

| State | Meaning |
| --- | --- |
| `NotStarted` | The previous contents were not cleared; count is zero. |
| `Cleared` | Clear succeeded but no requested format published. |
| `PartiallyPublished` | A nonempty proper prefix published. |
| `Published` | Every requested format published. |

`clear()` separately reports `cleared=true` as soon as native clearing succeeds.
A later close failure does not hide that side effect. Likewise, cleanup failure
does not erase a primary failure or an already-published count/state.

## Format-specific validation

Text may be empty, but malformed UTF-8 and embedded U+0000 are invalid at the
Win32 NUL-terminated text boundary. Win32 uses `CF_UNICODETEXT`.

File-list writes require at least one absolute path. Paths need not exist and
are not canonicalized or queried. Win32 uses normal Unicode `CF_HDROP`.

An image requires positive dimensions, `width * 4` packed row bytes, a zero
stride or a stride at least that large, and exactly `resolvedStride * height`
input bytes. Every multiplication and addition is overflow checked. Win32
publishes a top-down `CF_DIBV5` with sRGB color space, image rendering intent,
and explicit RGBA masks;
reads support common 24-bit RGB and standard-mask 32-bit DIB forms. RGB sources
without explicit alpha become alpha 255.

Custom names follow Win32 registered-format identity, which is
case-insensitive. Several distinct names may publish together; duplicates that
resolve to the same native identity are rejected. Custom bytes are copied
directly without a GameWIP prefix/header.

Win32 cannot faithfully publish a zero-byte custom block through this immediate
`HGLOBAL` path: a zero-sized movable allocation is discarded and rejected by
`SetClipboardData`; one byte would change the opaque extent, while `nullptr`
requests delayed rendering that #52 intentionally does not implement.
`writeCustomData()` therefore returns `Unsupported` before mutation for an
empty payload. Reading a zero-sized block supplied by another valid owner
remains supported.

## Example

```cpp
#include "desktop/clipboard.h"

using namespace GameWIP::Desktop;

void copySelection(std::string_view selectedUtf8)
{
    const Types::Clipboard::WriteResult result =
        Clipboard::writeText(selectedUtf8);
    if (!result.status.ok())
    {
        // Preserve result.commitState and result.formatsPublished in diagnostics.
    }
}

std::string pasteText()
{
    const Types::Clipboard::TextResult result = Clipboard::readText();
    return result.status.ok() ? result.text : std::string{};
}
```

The application decides when these helpers correspond to shortcuts, menus, or
selection commands. Clipboard itself performs only data exchange.

## Win32 ownership and lifetime

Publication uses a minimal call-scoped hidden native owner so it works without
a GameWIP top-level Window. All native blocks are prepared before that owner is
created and before `EmptyClipboard`. Ownership of each accepted `HGLOBAL`
transfers permanently to Windows; untransferred blocks are released. No global
mutex, persistent helper window, event dispatcher registration, listener,
worker, or per-Window storage is added.

## Relationship to drag and drop

`desktop/drag_drop.h` reuses this exact transfer vocabulary while keeping drag
lifecycle, effects, regions, sessions, and target events in its own opt-in
surface. Clipboard and DragDrop share private Win32 wire-format conversion but
retain separate native ownership and transaction rules. Clipboard adds no drag
behavior, and the lightweight `FilesDropped` Window event remains separate. See
@ref desktop_drag_drop.

## Related pages

- @ref desktop_public_api
- @ref desktop_examples
- @ref desktop_testing
- @ref desktop_troubleshooting
- @ref desktop_manual_validation
- @ref desktop_drag_drop
