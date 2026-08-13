@page io_test_hooks Test hooks

@warning These hooks are source-tree maintainer interfaces. They are not installed, exported as consumer API, or covered by installed-package compatibility guarantees.

## Availability

Configure with:

```cmake
-DIO_ENABLE_TEST_HOOKS=ON
```

The root test workflow enables the option for focused validation. Installed packages do not expose `io/internal/io_test_hooks.h` or the `IO_INTERNAL_TEST_HOOKS` definition.

## Protocol

Include `io/internal/io_test_hooks.h`, call `reset()` before and after an isolated scenario, then arm exactly one failure with `forceNextFailure(point, kind)`. A hook is consumed only by its matching point and is one-shot.

`FailurePoint` selects MemoryWriter write, reserve, or text-copy allocation and the scratch, byte-result, or text-result allocation in a whole-stream read. `FailureKind` selects `OutOfMemory`, `LengthError`, or `Unexpected`; validation checks their translation to `OutOfMemory`, `SizeLimitExceeded`, or `Unknown`.

Hooks exist to prove exception containment without depending on host memory pressure or container implementation limits. Production code must not include the internal header or branch behavior on these definitions.

## Related pages

- @ref io_testing
- @ref io_reader_writer_contract
- @ref project_testing
