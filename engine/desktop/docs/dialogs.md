@page desktop_dialogs Native dialogs and operation progress

Include @c desktop/dialogs.h to opt into native file, folder, message, prompt,
and progress presentations. It is separate from @c desktop/window.h; non-users acquire no dialog state or runtime activity.

## File and folder selection

@c Dialogs::openFile(), @c openFiles(), @c saveFile(), @c selectFolder(), and
@c selectFolders() enter the platform dialog synchronously and return after the
user accepts or dismisses it. @c status says whether the operation ran; a dismissal is a successful
@c Types::Dialogs::Outcome::Cancelled, not a native failure.

Filters keep caller order. Each extension is a literal token such as @c png or @c tar.gz, with no leading period, wildcard, semicolon,
path separator, or NUL (not a native pattern list); an empty span means all files. Filter indices are zero-based.
Suggested directories are native paths passed unnormalized; a save dialog does not rewrite the returned
path using @c suggestedExtension, which follows the same rules but allows semicolons.

On Windows these use the modern shell file-dialog interfaces in the calling thread's existing STA, or a temporary STA when
COM is uninitialized (including an implicit MTA inherited from another thread). An explicitly initialized MTA, neutral, or otherwise incompatible apartment
returns @c ResourceBusy; there is no hidden thread.

## Messages and prompts

@c Dialogs::showMessage() presents a fixed portable button set and
maps the native button back to @c Types::Dialogs::Message::Button.
Escape, the native close affordance, and a semantic Cancel button yield a
successful @c Cancelled outcome.

@c Dialogs::showPrompt() supports caller-defined buttons with optional
secondary text, mutually exclusive options, expanded details,
supplemental text, and an optional checkbox. Button and option IDs are
opaque, may overlap platform IDs, and are remapped by the backend. Windows may show secondary button
text as TaskDialog command links; that is not part of the contract. Final option and checkbox state is returned for both
accepted and cancelled outcomes.

The Windows backend uses @c TaskDialogIndirect, so the final executable
must opt into Common Controls v6 through the manifest helper; applications that also create @c Window objects normally attach
both requirements in one call:

```cmake
gamewip_attach_application_manifest(
    TARGET MyApplication
    COMMON_CONTROLS_V6
    PER_MONITOR_V2
)
```

The executable owns the manifest; Desktop does not attach or
propagate one. Layout, wording, and other presentation are backend-controlled.

## Owners and text

Dialogs may be ownerless. A non-null owner must be a live
@c Window owned by the calling thread: a closed or natively lost owner gives
@c NotOpen, a foreign one @c ResourceBusy. Ownership lasts only for the call.

All text is strict UTF-8. Malformed input returns @c EncodingFailed; embedded U+0000 returns
@c InvalidArgument at a NUL-terminated native boundary. Text is never normalized or repaired.

## Persistent progress

@c ProgressDialog is a non-copyable, non-movable, modeless native presentation.
Open, mutation, cancellation inspection, and close belong to the opening thread
(@ref desktop_lifecycle_events). @c ownedByCurrentThread() is the only method for
arbitrary-thread queries; it uses a process-local thread token with no native call.

@c Progress::Description::owner is a borrowed C++ @c Window pointer; when
non-null, that @c Window must outlive the dialog's whole open lifetime, through close or native-loss finalization.

Determinate mode maps [0, 1] to a stable native range. Indeterminate mode shows the native marquee while retaining the
last numeric value, which returns on switching back. Title, heading, message, mode, and value can change while open.

When cancellation is enabled, the Cancel button and native close affordance set
a sticky request without closing the dialog or cancelling work; @c clearCancelRequest() clears it. The application
owns the work and decides when to call @c close(). A successful close fully
resets the object for reopening.

Progress creates no worker thread or second event loop: keep pumping @c Desktop::Events on the owner thread and
apply worker updates there through application-owned synchronization.

With @c blocksOwner set, an owned dialog disables only the owner's
effective native interaction; the Window's requested
@c userInteractionEnabled stays authoritative. Blockers are counted: the owner stays disabled until the last one
closes, then its latest requested state is restored. Ownerless dialogs and those with @c blocksOwner disabled do not block.

If an owner loses its native lifetime, its progress windows are
destroyed first. Their public objects retain portable finalization state, report not open, and may be
closed safely before reuse. Wrong-thread destruction hands live native
cleanup to the owner-thread dispatcher, which finalizes
progress resources before tearing down its Windows.
