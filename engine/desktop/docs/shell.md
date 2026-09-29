@page desktop_shell Shell integration

Desktop shell integration is an opt-in set of focused resources and stateless services for
Windows taskbar, tray, notification, jump-list, recent-item, and current-user registration
behavior. The API is deliberately separate from `Window`: a Window keeps its normal lifetime and
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

## Tray icons

`Desktop::TrayIcon` is a process-local resource independent of `Window`. A passive open can
publish only icon and tooltip presentation. A nonempty recursive menu requires a queue, and a
passive resource may bind one queue before its first nonempty menu is published. The queue cannot
be replaced or detached after binding.

Menu command identities are unique across the complete recursive tree. Check and radio entries
carry their copied checked state; separators and submenus do not carry command identities.
Tray activation, command invocation, and native-state loss are delivered as typed shell events.
Explorer restart recovery is native behavior; applications should continue to treat the queue as
the sole event-delivery surface.

## Notifications

`Desktop::NotificationCenter` owns one owner-thread publication lifetime and may retain multiple
notification identities. Basic notifications may be published without a queue. Actions, text
inputs, and user-dismissal events require the queue overload so interaction has an explicit
destination. Update preserves the notification identity; dismiss removes it from the center.

The Win32 backend currently advertises basic notifications and sound policy. Rich action/input,
media, scheduling, grouping, urgency, badge, and progress fields return `Unsupported` until a
backend can represent them without silently changing their meaning. Applications should branch on
the capability snapshot before presenting those fields.

## Jump lists and recent items

`Desktop::JumpLists::publish()` is a stateless, process-level replacement operation. It accepts
application tasks, labeled categories, and filesystem-path or URI recent items. Launch actions
must contain an executable path and may use literal or explicit target arguments; registration
actions may use selected-target placeholders, but jump-list actions may not because no selection
context exists when the list is published.

An empty description clears the application-owned jump-list publication. The operation does not
change the default application and does not register file associations.

## Current-user registration

`Desktop::Registration` writes only under the current user's shell classes. It never elevates and
never forces the default application. Stable owner keys identify registrations created by the
application and allow same-owner replacement or cleanup.

Use `ConflictPolicy::Report` when the application must not write an existing target,
`ConflictPolicy::Coexist` when a foreign target may remain untouched, and
`ConflictPolicy::ReplaceOwned` when only a same-owner target may be replaced. Always inspect the
returned conflict list and remove registrations with the same owner key during uninstall or test
cleanup.

## Testing and manual validation

The automated Desktop module covers queue FIFO/overflow/reopen behavior, thread ownership,
descriptor validation, one-taskbar-binding enforcement, queue borrowing, notification identity
lifecycle, and deterministic shell-native failure/retry paths. Native smoke checks skip when the
current environment cannot provide the corresponding shell service.

Interactive validation remains useful for the visible surface. Run:

```powershell
.\build\test\GameWIPTests.exe --test-module=desktop --manual-tests --desktop-manual-suite=files-shell
```

That workflow should verify visible taskbar progress and thumbnail buttons, tray activation and
recursive menus, notification appearance and dismissal, Explorer file/URI activation, and jump
list ordering. These observations are intentionally not prerequisites for the deterministic
automated test suite.

See @ref desktop_public_api, @ref desktop_testing, @ref desktop_test_hooks, and
@ref desktop_manual_validation for the declaration map, automated coverage, source-tree hooks,
and interactive workflow.
