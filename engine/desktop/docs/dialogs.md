@page desktop_dialogs Native dialogs and operation progress

Include @c desktop/dialogs.h to opt into native file, folder, message, prompt,
and progress presentations. The header remains separate from
@c desktop/window.h; applications that do not use dialogs acquire no dialog
state, registration, or runtime activity.

## File and folder selection

@c Dialogs::openFile(), @c openFiles(), @c saveFile(), @c selectFolder(), and
@c selectFolders() synchronously enter the platform dialog and return after the
user accepts or dismisses it. @c status describes whether the operation itself
ran successfully. A successful dismissal is represented separately by
@c Types::Dialogs::Outcome::Cancelled and is not a native failure.

File filters preserve caller order. Each extension is a literal portable token
such as @c png or @c tar.gz, without a leading period, wildcard, semicolon, or
path separator; extensions are not native pattern lists.
An empty extension span means all files. Filter indices are zero-based in the
portable API. Suggested directories are native paths and are passed as
suggestions without normalization; a save dialog does not rewrite the returned
path using @c suggestedExtension.

On Windows these operations use the modern shell file-dialog interfaces. They
run in the calling thread's existing STA or initialize an STA for the duration
when COM is uninitialized. An MTA, neutral, or otherwise incompatible apartment
returns @c ResourceBusy; Desktop does not move the operation to a hidden thread.

## Messages and prompts

@c Dialogs::showMessage() presents one of the fixed portable button sets and
maps the selected native button back to @c Types::Dialogs::Message::Button.
Escape, the native close affordance, and a semantic Cancel button produce a
successful @c Cancelled outcome.

@c Dialogs::showPrompt() supports caller-defined buttons with optional
explanatory secondary text, mutually exclusive options, expanded details,
supplemental text, and an optional checkbox. Portable button and option IDs are
opaque and may overlap ordinary platform IDs; the backend remaps them for the
native presentation. The current Windows backend may present secondary button
text using TaskDialog command links, but that widget choice is not part of the
portable contract. The final option and checkbox state are returned for both
accepted and cancelled outcomes.

The Windows backend uses @c TaskDialogIndirect. The final application executable
must therefore opt into Common Controls v6 through the existing application
manifest helper. Applications that also create @c Window objects normally attach
both executable requirements in one call:

```cmake
gamewip_attach_application_manifest(
    TARGET MyApplication
    COMMON_CONTROLS_V6
    PER_MONITOR_V2
)
```

Manifest ownership remains with the executable; Desktop does not attach or
propagate a library manifest. Exact layout, wording treatment, and other
presentation details remain backend-controlled.

## Owners and text

Every one-shot dialog may be ownerless. A non-null owner must be a live
@c Window owned by the calling thread. A closed or natively lost owner produces
@c NotOpen; a foreign owner produces @c ResourceBusy. Ownership is retained only
for the duration of the synchronous call.

All public text is strict UTF-8. Malformed input returns @c EncodingFailed, and
embedded U+0000 returns @c InvalidArgument at a NUL-terminated native boundary.
Desktop neither normalizes nor repairs text.

## Persistent progress

@c ProgressDialog is a non-copyable, non-movable, modeless native presentation.
Open, mutation, cancellation inspection, and close belong to the thread that
successfully opened it. @c ownedByCurrentThread() is the only method intended
for arbitrary-thread queries. It uses a process-local portable thread token
and performs no native call.

@c Progress::Description::owner is a borrowed C++ @c Window pointer. When it is
non-null, that @c Window object must outlive the complete open lifetime of the
@c ProgressDialog, through successful close or native-lifetime loss finalization.

Determinate mode maps progress from [0, 1] to a stable native range.
Indeterminate mode uses the native marquee presentation while retaining the
most recent numeric value; switching back to determinate exposes that retained
value. The title, heading, message, mode, and value can be changed while open.

When cancellation is enabled, the Cancel button and native close affordance set
a sticky cancellation request without closing the dialog or cancelling
application work. @c clearCancelRequest() clears that intent. The application
owns the work and decides when to call @c close(). A successful close fully
resets the lifetime so the same object may be opened again.

Progress does not create a worker thread or a second event loop. Continue
pumping @c Desktop::Events on the owner/UI thread. Work performed elsewhere must
publish updates through an application-owned synchronization mechanism and
apply them to @c ProgressDialog on its owner thread.

An owned progress dialog with @c blocksOwner set disables only the owner's
effective native interaction state. The Window's requested
@c userInteractionEnabled value remains authoritative. Multiple blockers are
counted by active state: the owner remains disabled until the last blocker
closes, then its latest requested state is restored. Ownerless dialogs and
dialogs with @c blocksOwner disabled do not block a Window.

If an owner Window loses its native lifetime, associated progress windows are
destroyed before the owner handle becomes unusable. Their public objects retain
portable finalization state, report not open on the owner thread, and may be
closed safely before reuse. Wrong-thread destruction transfers live native
cleanup to the existing owner-thread dispatcher. Dispatcher shutdown finalizes
progress resources before tearing down its Windows.
