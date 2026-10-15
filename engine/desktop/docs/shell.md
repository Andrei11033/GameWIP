@page desktop_shell Shell integration

Opt-in resources and stateless services for taskbar, tray, notification, jump-list, recent-item,
and current-user registration. Shell resources keep only their own native state; Window is unaffected.

## Common rules

All shell text is UTF-8. Call-scoped views (strings, spans, icons, recursive menus) are copied
before the call returns, so sources may be temporary. A returned `Types::Shell::Event` owns its payload.

`Desktop::ShellEventQueue` is fixed-capacity, non-copyable and non-movable. Owner-thread rules:
@ref desktop_lifecycle_events. Native shell resources may publish from other threads. The queue
is borrowed by resources and must outlive every bound resource; closing while one is bound
returns `ResourceBusy`.

Overflow drops the newest event and increments the cumulative dropped counter. Sequence numbers
start at one per open lifetime. `clearEvents()` removes retained payloads;
`clearDroppedEventCount()` resets only the counter.

The default `open()` allocates `Events::kDefaultQueueCapacity` slots. Otherwise pass
`Types::Shell::QueueConfiguration::owned(capacity)` or `borrowed(eventStorage)` (non-empty,
alive and unmoved while open).

Pump `Desktop::Events::poll()`/`wait()` on the owner thread, then consume the shell queue; a tray
icon or notification center alone suffices, an open queue alone does not. Shell events are not
in the pump's Window event counts.

Close shell resources explicitly on their owner thread before destroying them or their borrowed
queue; they do not share Window's deferred wrong-thread destruction. If native close fails,
keep the resource and queue and retry on the owner thread.

Check `Desktop::Shell::getCapabilities()` (or `supports()`, same cached snapshot) before using optional
features. Unsupported fields are a capability boundary; do not emulate another shell model.

## Taskbar

`Desktop::TaskbarItem` binds one taskbar presentation to one open `Window` lifetime (one binding
per lifetime); it is owner-thread-affine.

Progress and overlay updates replace the previous snapshot transactionally; `std::nullopt` clears.
Thumbnail buttons require the queue overload and publish `TaskbarThumbnailButtonInvoked`; the set
is replaced as a whole and an empty span clears it. Close the item before its queue or Window.

Determinate progress fractions must be finite and in `[0, 1]`; Indeterminate ignores the fraction.
On Win32, thumbnail command IDs must fit a nonzero 16-bit value and at most seven buttons exist.

## Tray icons

`Desktop::TrayIcon` is process-local and independent of `Window`. A passive open publishes only
icon and tooltip. A non-empty recursive menu requires a queue; a passive icon may bind one queue
(`bindEventQueue()`) before its first non-empty menu, and it cannot be replaced or detached.

Command identities are unique across the whole menu tree. Check and radio entries carry their
copied checked state; separators and submenus have no command identity. Activation, command
invocation, and native-state loss arrive as typed shell events. `setIcon()`, `setTooltip()`, and
`setMenu()` replace presentation; an empty tooltip or menu clears it.

Win32 uses version-4 notification callbacks and enables standard tooltips. Keyboard selection and
keyboard/mouse context menus deliver activation events. A hidden top-level tool HWND receives
Explorer's `TaskbarCreated`, republishes the icon and restores its callback version.

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

const std::array<D::Types::Tray::MenuItem, 1> items{
    D::Types::Tray::MenuItems::command(D::Types::Shell::CommandId{1}, "Quit")};
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

`Desktop::NotificationCenter` owns one owner-thread publication lifetime and may retain several
notification identities. Basic notifications need no queue; actions, text inputs, and dismissal
events require the queue overload.

`publish()` returns `Types::Result<NotificationId>`; check `ok()` first. `update(id, description)`
preserves the ID (a failed update keeps the previous description); `dismiss(id)` removes it. IDs
are valid only for the open center that issued them, not across close/reopen. Publication success
does not guarantee that OS policy displays the notification.

Win32 advertises only basic notifications and sound policy; action/input, media, scheduling,
grouping, urgency, badge, and progress fields return `Unsupported`. Branch on the capability
snapshot. Build media with `Types::Notifications::Media::image()`/`uri()` and scheduling with
`Schedule::once()`/`recurring()`.

Basic publication uses a Win32 balloon; activation and expiry events need the queue overload and
owner-thread pumping. This helper assumes the center was opened on the calling thread; keep it
open for the publication's lifetime, then dismiss the returned ID and close explicitly:

```cpp
#include "desktop/shell_notifications.h"

GameWIP::Desktop::Types::Result<GameWIP::Desktop::Types::Shell::NotificationId> publishExportComplete(
    GameWIP::Desktop::NotificationCenter &center)
{
    GameWIP::Desktop::Types::Notifications::Description description;
    description.title = "Export complete";
    description.body = "The exported file is ready.";
    return center.publish(description);
}
```

## Jump lists and recent items

`Desktop::JumpLists::publish()` is a stateless, process-level replacement. It accepts tasks,
labeled categories, and path or URI recent items. Launch actions need an executable path and
literal or explicit target arguments; selected-target placeholders are allowed in registration
actions but not jump-list actions (no selection exists at publish time).

An empty description clears the application-owned list. It does not change the default
application or register file associations. Calls are serialized process-wide; each success
replaces the prior snapshot.

`Types::Shell::PathTargetView` borrows a `FileSystem::Types::Path`, `UriTargetView` borrows UTF-8
URI text, and `LaunchActionView` pairs an executable path with ordered structured arguments.
Pass literal arguments separately; never build a quoted command line.

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
        D::Types::Shell::LaunchArguments::literal("--new-document")};
    const D::Types::Shell::LaunchActionView action{
        D::Types::Shell::Targets::path(executable), arguments};
    const std::array tasks{D::Types::JumpLists::Task{"New document", {}, action}};
    D::Types::JumpLists::Description description;
    description.tasks = tasks;
    return D::JumpLists::publish(description);
}
```

## Current-user registration

`Desktop::Registration` writes only under the current user's shell classes, never elevates, and
never forces the default application. Stable owner keys identify the application's registrations
for same-owner replacement or cleanup.

Conflict policies: `Report` never writes an existing target; `Coexist` leaves a foreign file or
URI target untouched; `ReplaceOwned` replaces only a same-owner target. Always inspect the
returned conflict list. A foreign or unowned executable application record always blocks a file
registration with a default action: `Coexist` never reports success while omitting the record's
required `FriendlyAppName` and `SupportedTypes`. Unregistering a record no longer owned by the
caller reports the same `ApplicationRecord` conflict and leaves the association intact. Registry
cleanup failures are returned; the operation is transactional and retryable.

Registration outlives the process. Keep owner keys stable across updates and call
`unregisterFileExtension(ownerKey, extension)` or `unregisterUriScheme(ownerKey, scheme)` on
uninstall or test cleanup. Schemes omit the trailing colon; extensions look like `.example`.

File registrations advertise their owned ProgID through `OpenWithProgids`, even when Coexist
retains a foreign default; cleanup removes only that owner's alternative and values. The conflict
kind is `ApplicationRecord` when the executable basename under `Applications\\` is owned by
another registration; its target is that basename and its owner is empty when the record has no
GameWIP ownership marker. URI registrations publish verbs/commands beneath the scheme key;
Coexist never replaces a foreign URI command and its owned secondary ProgID does not become the
active handler. Publication and cleanup notify the shell that associations changed.
Win32 launch commands convert executable paths to native separators (the caller's value is
unchanged; generic separators are accepted). Selected path and URI placeholders are quoted as one
argument; selected-path sets keep the shell's multiple-argument expansion.

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
        D::Types::Shell::Targets::path(executable), arguments};
    description.conflictPolicy = D::Types::Registration::ConflictPolicy::ReplaceOwned;
    return D::Registration::registerUriScheme(description);
}
```

Inspect both `status` and `conflicts`. Registration only changes shell metadata; the application
owns command-line parsing and target validation at launch. Stateless jump-list and registration
calls need no Window or queue.
Win32 taskbar and jump-list calls need an STA-compatible COM apartment and return `ResourceBusy`
for an incompatible explicitly initialized one; a caller that has not initialized COM may acquire
a temporary STA even if another thread has an implicit MTA.
Win32 jump-list tasks need at least one launch argument; the title becomes the display-name
property, the description the tooltip, and the executable's first icon is used.

## Testing and manual validation

Automated coverage and the interactive `--desktop-manual-suite=shell` workflow (taskbar, tray,
notifications, Explorer file/URI activation, jump lists; `files-window` and `files-shell` for file
drops and frame policies) are described in @ref desktop_testing and @ref desktop_manual_validation.
Native smoke checks skip when the shell service is unavailable. See also @ref desktop_public_api
and @ref desktop_test_hooks.
