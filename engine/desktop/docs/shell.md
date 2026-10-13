@page desktop_shell Shell integration

Desktop shell integration is an opt-in set of focused resources and stateless services for
Windows taskbar, tray, notification, jump-list, recent-item, and current-user registration
behavior. A Window keeps its normal lifetime and
event state, while shell resources retain only the native state needed by their own contracts.

## Common rules

All shell text is UTF-8. Call-scoped views are copied before the receiving operation returns, so
the source strings, spans, icons, and recursive menu storage may be temporary. A returned
`Types::Shell::Event` owns its payload and is valid independently of the source native callback.

`Desktop::ShellEventQueue` is a fixed-capacity, non-copyable, non-movable queue. Opening binds it
to the calling thread. The owner thread opens, closes, consumes, and mutates the queue; native
shell resources may publish from other threads. A queue is borrowed by resources and must outlive
every resource bound to it. Closing while a resource is bound returns `ResourceBusy`.

Queue overflow drops the newest event and increments the cumulative dropped-event counter. Event
sequence numbers start at one for each open lifetime. `clearEvents()` removes retained payloads,
and `clearDroppedEventCount()` resets only the diagnostic counter.

The default `open()` overload allocates `Events::kDefaultQueueCapacity` slots;
the capacity overload accepts a positive slot count, and the span overload
borrows nonempty `Types::Shell::Event` storage until close. External storage
must remain alive and unmoved while the queue is open.

Keep pumping `Desktop::Events::poll()` or `wait()` on the resource owner thread,
then consume its shell queue. This works with only a tray icon or notification
center open. Shell events are not included in the pump's Window event counts.
An open queue alone has no native resource to pump.

Close shell resources explicitly on their owner thread before destroying them
or their borrowed queue. These resources do not share Window's deferred
wrong-thread destruction contract. If native close fails, retain the resource
and queue and retry on the owner thread; a destructor cannot report that error.

Use `Desktop::Shell::getCapabilities()` before enabling optional presentation or interaction.
`supports()` is a convenience query over the same cached snapshot. Unsupported optional fields
must be treated as a capability boundary, not as a request to emulate a different shell model.

## Taskbar

`Desktop::TaskbarItem` binds one taskbar presentation to one open `Window` lifetime. Only one
binding may be open for that Window lifetime. The resource is owner-thread-affine and does not add
callbacks or persistent shell state to `Window`.

Progress and overlay updates replace the previous snapshot transactionally. Thumbnail buttons
require the queue overload and publish `TaskbarThumbnailButtonInvoked` events. The button set is
replaced as a whole; an empty span clears it. Close the taskbar item before closing its queue or
Window.

`setProgress()` and `setOverlayIcon()` accept `std::nullopt` to clear their
presentation. Progress fractions must be finite and within `[0, 1]` for
determinate states; Indeterminate ignores the finite fraction. On Win32,
thumbnail command IDs must fit a nonzero 16-bit value and the native button
limit is seven.

## Tray icons

`Desktop::TrayIcon` is a process-local resource independent of `Window`. A passive open can
publish only icon and tooltip presentation. A nonempty recursive menu requires a queue, and a
passive resource may bind one queue before its first nonempty menu is published. The queue cannot
be replaced or detached after binding.

Menu command identities are unique across the complete recursive tree. Check and radio entries
carry their copied checked state; separators and submenus do not carry command identities.
Tray activation, command invocation, and native-state loss are delivered as typed shell events.
Use `setIcon()`, `setTooltip()`, and `setMenu()` to replace presentation;
an empty tooltip or menu clears that part. `bindEventQueue()` performs the
one-time binding for a previously passive icon.

Win32 uses version-4 notification callbacks and explicitly enables standard
tooltips. Both keyboard selection and keyboard/mouse context menus deliver
activation events. A hidden top-level tool HWND receives Explorer's
`TaskbarCreated` broadcast, republishes the icon and restores its callback version.

Given an application-owned `iconImages` span of valid `Types::IconImageView`
values, this owner-thread example binds a passive icon before publishing a menu:

```cpp
#include "desktop/shell_tray.h"
#include <array>

namespace D = GameWIP::Desktop;
D::ShellEventQueue queue;
if (!queue.open().ok())
    return;

D::TrayIcon tray;
D::Types::Tray::Description description;
description.icons = iconImages;
description.tooltip = "Example editor";
if (!tray.open(description).ok())
    return;
if (!tray.bindEventQueue(queue).ok())
    return;

std::array<D::Types::Tray::MenuItem, 1> items{};
items[0].commandId = {1};
items[0].label = "Quit";
if (!tray.setMenu({items}).ok())
    return;

bool quit = false;
while (!quit)
{
    const auto pump = D::Events::wait(std::chrono::milliseconds{16});
    if (!pump.status.ok())
        break;
    D::Types::Shell::Event event;
    while (queue.popEvent(event))
    {
        if (const auto *command =
                event.getIf<D::Types::Shell::Events::TrayCommandInvoked>())
        {
            quit = quit || (command->iconId == tray.id() && command->commandId.value == 1);
        }
    }
}
const auto closed = tray.close();
if (closed.ok())
    static_cast<void>(queue.close());
```

## Notifications

`Desktop::NotificationCenter` owns one owner-thread publication lifetime and may retain multiple
notification identities. Basic notifications may be published without a queue. Actions, text
inputs, and user-dismissal events require the queue overload so interaction has an explicit
destination. Update preserves the notification identity; dismiss removes it from the center.

`publish()` returns a new `NotificationId`; `update(id, description)` preserves
it and `dismiss(id)` removes it. IDs are accepted only by the owning open
center. Retaining an ID across center close/reopen does not revive it. A failed
update preserves the previously retained description. Successful publication
does not guarantee that OS notification policy displays the notification.

The Win32 backend currently advertises basic notifications and sound policy. Rich action/input,
media, scheduling, grouping, urgency, badge, and progress fields return `Unsupported` until a
backend can represent them without silently changing their meaning. Applications should branch on
the capability snapshot before presenting those fields.

Basic publication uses a Win32 balloon notification. Activation and expiry
events require the queue overload and owner-thread pumping. This helper assumes
the center was opened on the calling thread; keep it open for the publication's
lifetime, then dismiss the returned ID and close explicitly:

```cpp
#include "desktop/shell_notifications.h"

GameWIP::Desktop::Types::Notifications::PublishResult publishExportComplete(
    GameWIP::Desktop::NotificationCenter &center)
{
    GameWIP::Desktop::Types::Notifications::Description description;
    description.title = "Export complete";
    description.body = "The exported file is ready.";
    return center.publish(description);
}
```

## Jump lists and recent items

`Desktop::JumpLists::publish()` is a stateless, process-level replacement operation. It accepts
application tasks, labeled categories, and filesystem-path or URI recent items. Launch actions
must contain an executable path and may use literal or explicit target arguments; registration
actions may use selected-target placeholders, but jump-list actions may not because no selection
context exists when the list is published.

An empty description clears the application-owned jump-list publication. The operation does not
change the default application and does not register file associations.

Calls are serialized process-wide; each success replaces the prior snapshot.
`Types::Shell::PathTargetView` borrows a `FileSystem::Types::Path` and
`UriTargetView` borrows UTF-8 URI text. `LaunchActionView` pairs an executable
path with ordered structured arguments. Supply literal arguments separately,
without building a quoted native command line yourself.

For an application-owned executable path, this complete task set opens a new
document. The call copies both the argument and task arrays before returning:

```cpp
#include "desktop/shell_jump_lists.h"
#include <array>
#include <functional>

GameWIP::IO::Types::Status publishEditorTasks(
    const GameWIP::FileSystem::Types::Path &executable)
{
    namespace D = GameWIP::Desktop;
    const std::array<D::Types::Shell::LaunchArgumentView, 1> arguments{
        D::Types::Shell::LiteralLaunchArgumentView{"--new-document"}};
    const D::Types::Shell::LaunchActionView action{
        D::Types::Shell::PathTargetView{std::cref(executable)}, arguments};
    const std::array tasks{D::Types::JumpLists::Task{"New document", {}, action}};
    D::Types::JumpLists::Description description;
    description.tasks = tasks;
    return D::JumpLists::publish(description);
}
```

## Current-user registration

`Desktop::Registration` writes only under the current user's shell classes. It never elevates and
never forces the default application. Stable owner keys identify registrations created by the
application and allow same-owner replacement or cleanup.

Use `ConflictPolicy::Report` when the application must not write an existing target,
`ConflictPolicy::Coexist` when a foreign file or URI target may remain untouched, and
`ConflictPolicy::ReplaceOwned` when only a same-owner target may be replaced. Always inspect the
returned conflict list and remove registrations with the same owner key during uninstall or test
cleanup. A foreign or unowned executable application record is always a blocking conflict for a
file registration that supplies a default action: `Coexist` never permits the library to report
success while omitting the record's required `FriendlyAppName` and `SupportedTypes` metadata.
If unregistration encounters an application record that is no longer owned by the caller, it
reports the same `ApplicationRecord` conflict and leaves the association intact. Registry cleanup
failures are returned to the caller; the operation is transactional and can be retried after the
underlying access problem is resolved.

Registration persists after the process exits. Keep owner keys stable across
application updates and call `unregisterFileExtension(ownerKey, extension)` or
`unregisterUriScheme(ownerKey, scheme)` during uninstall. URI schemes omit a
trailing colon; extensions use a spelling such as `.example`.

File registrations advertise their owned ProgID through `OpenWithProgids`,
including when Coexist retains a foreign default. Cleanup removes only that
owner's alternative and owned values, preserving other handlers and metadata.
The returned conflict kind is `ApplicationRecord` when the executable basename
under `Applications\\` is already owned by another registration; its target is
the executable basename and its owner is empty when the existing record has no
GameWIP ownership marker.
URI registrations publish native verbs/commands beneath the scheme key itself.
Coexist never replaces a foreign URI command; its owned secondary ProgID does
not become the active scheme handler. Successful publication and cleanup notify
the shell that associations changed.
Win32 launch commands convert executable paths to native separators before
passing them to shell links or association command parsing. Portable callers may
supply generic path separators; the caller's path value remains unchanged.
Selected path and URI placeholders are quoted as one command-line argument;
selected-path sets keep the shell's multiple-argument expansion.

This helper registers a URI launch argument. `executable` is an
application-owned path; the returned result includes any ownership conflicts:

```cpp
#include "desktop/shell_registration.h"
#include <array>
#include <functional>

GameWIP::Desktop::Types::Registration::Result registerExampleUri(
    const GameWIP::FileSystem::Types::Path &executable)
{
    namespace D = GameWIP::Desktop;
    const std::array<D::Types::Shell::LaunchArgumentView, 1> arguments{
        D::Types::Shell::LaunchPlaceholder::SelectedUri};
    D::Types::Registration::UriSchemeDescription description;
    description.ownerKey = "Example.Editor";
    description.scheme = "example-editor";
    description.displayName = "Example editor link";
    description.defaultAction = D::Types::Shell::LaunchActionView{
        D::Types::Shell::PathTargetView{std::cref(executable)}, arguments};
    description.conflictPolicy = D::Types::Registration::ConflictPolicy::ReplaceOwned;
    return D::Registration::registerUriScheme(description);
}
```

Inspect both `status` and `conflicts`. Registration changes shell metadata;
the application owns command-line parsing and target validation when launched.
Stateless jump-list and registration calls need no Window or shell event queue.
Win32 taskbar and jump-list calls require an STA-compatible COM apartment and
return `ResourceBusy` for an incompatible explicitly initialized apartment.
A caller that has not initialized COM may acquire a temporary STA even when
another thread in the process has established an implicit MTA.
Win32 jump-list tasks require at least one launch argument. The backend stores
the title as the shell display-name property, the description as the tooltip,
and uses the executable's first icon.

## Testing and manual validation

The automated Desktop module covers queue FIFO/overflow/reopen behavior, thread ownership,
descriptor validation, one-taskbar-binding enforcement, queue borrowing, notification identity
lifecycle, and deterministic shell-native failure/retry paths. Native smoke checks skip when the
current environment cannot provide the corresponding shell service.

Interactive validation remains useful for the visible surface. Run:

```powershell
.\build\test\GameWIPTests.exe --test-module=desktop --manual-tests --desktop-manual-suite=shell
```

That workflow should verify visible taskbar progress and thumbnail buttons, tray activation and
recursive menus, notification appearance and dismissal, Explorer file/URI activation, and jump
list ordering. Progress states remain active until each observation is answered. Controlled
launches write a receipt shown in the diagnostics and exit without running the test suite again.
Use `files-window` for lightweight file drops and frame policies, or `files-shell` for both groups.
An unexpected jump-list publication failure is a failed check. These observations are intentionally not prerequisites for the deterministic
automated test suite.

See @ref desktop_public_api, @ref desktop_testing, @ref desktop_test_hooks, and
@ref desktop_manual_validation for the declaration map, automated coverage, source-tree hooks,
and interactive workflow.
