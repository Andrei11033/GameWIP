@page filesystem_test_hooks Validation seam

@warning This source-tree-only interface exists for race and ownership-cleanup validation. It is not installed, consumer API, or part of the package ABI.

## Move coordination

The Win32 validation backend exposes two bounded coordination points for strict `movePath()` tests:

- `armMoveDestinationValidatedPause()` pauses after the destination parent has been validated and before native commit.
- `armMoveCommittedPause()` pauses after native commit and before the operation returns.
- `waitForMovePause()` observes the worker reaching the selected point.
- `releaseMovePause()` allows the worker to continue.
- `reset()` clears both unlock-failure state and move-pause state.

These hooks support externally meaningful race tests by validating that a strict move remains anchored to the original parent.
They also validate that post-commit namespace changes do not alter the operation's result.

## Failed unlock cleanup

`setFileUnlockFailure(true)` makes native `FileLock` release attempts return `UnlockFailed` until the hook is disabled or `reset()` is called.
This focused FileLock ownership/destructor-cleanup seam verifies that destructor cleanup still releases the native lock and decrements the
owning `File`'s lock count, allowing a competitor to acquire the lock afterward.

`reset()` clears both unlock-failure state and move-pause state. The retained seam is limited to move race coordination and FileLock ownership/destructor
cleanup; it does not provide per-operation native-call failure injection. Ordinary public API tests cover the documented status and cleanup contracts.
