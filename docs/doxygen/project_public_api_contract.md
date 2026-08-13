@page project_public_api_contract Public API contract

This page defines the durable project-wide contract for reusable public C++ APIs. Owning library manuals may narrow a rule when their domain requires it, but they must state the narrower contract explicitly and must not silently contradict this page.

## Naming

- Public types and enum values use `UpperCamelCase`; functions, methods, variables, and fields use `lowerCamelCase`.
- Boolean predicates describe the property they answer. Add `is...`, `has...`, `supports...`, or similar context when it makes the question clearer; keep a shorter conventional name such as `canSeek()` when it is already unambiguous. Avoid a bare `valid()` when `isValid()` expresses the same contract more clearly.
- Keep conventional operation names such as `flush()` when the owning type or namespace already supplies the context. Use a differentiated form such as `flushTo(destination)` only when the destination or semantic distinction is real; do not mechanically restate context in the name.
- Mutating operations use verbs. A name must disclose destructive side effects that are not otherwise obvious; for example, a write-and-clear operation says both actions.
- Use one property vocabulary across a type. A getter and setter should describe the same property unless the operations intentionally have different semantics.
- Names must describe actual semantics rather than historical implementation. Event names identify the changed property or observed occurrence, and a plural native-handle view uses a plural name.

## Text and bytes

Public APIs that call data text use UTF-8. Encoding-agnostic or unvalidated data is called bytes.
A Text API has the same UTF-8 validity contract regardless of whether the active backend is a console, redirected byte stream, file, pipe, or another native endpoint. Backend selection must not decide whether malformed text is accepted.

UTF-8 text continues to use `std::string`, `std::string_view`, and caller-owned `std::span<char>` storage. GameWIP does not migrate public UTF-8 APIs to `std::u8string` or `std::u8string_view`.

- Do not normalize text automatically.
- Do not insert or remove a byte-order mark automatically.
- Do not silently replace or repair malformed text.
- Embedded `U+0000` is valid Unicode text. An API may reject it when its native representation cannot preserve an embedded null, but that restriction must be documented and checked at the boundary.
- Validate external, untrusted, or native input before exposing it as valid UTF-8.
- Validate and convert in one pass where practical. Trusted internal forwarding does not repeatedly rescan text already established as valid.
- An API whose contract accepts valid UTF-8 may rely on that precondition on a measured hot path when no new trust or native boundary is crossed.

IO `Reader`, `Writer`, and byte helpers remain encoding-agnostic byte primitives. IO text helpers enforce the project UTF-8 contract and may use the foundational Unicode library to do so; that dependency must not add Unicode work to byte-only paths.

## Predicates, units, and offsets

Predicate names follow the `is...`, `has...`, and `supports...` vocabulary. Capability queries report implemented semantics and do not infer support from platform presence alone.

Public units must be visible in the type, member name, or directly adjacent contract. Distinguish logical coordinates, physical pixels, durations, byte counts, code-unit counts, scalar counts, and grapheme counts. Text offsets state their unit and required boundary. UTF-8 storage normally uses byte offsets, with callers required to supply valid code-point or grapheme boundaries where the operation requires them.

## Status, results, and diagnostics

An operation status reports whether the operation completed and why it failed. Expected domain outcomes such as cancellation, timeout policy successfully enforced, user choice, fallback, adjustment, or truncation remain distinct from operational failure.

- Return a status directly when no value or domain outcome is needed.
- Use a named result containing status plus values or outcomes when callers need both.
- Preserve meaningful partial progress.
- Associate diagnostics with the operation result or a coherent health snapshot. Do not use mutable process-wide or thread-local last-operation pairings.
- Successful status construction and common code-only failures do not allocate diagnostic text.

## Exceptions and cleanup

Checked public operations contain expected implementation-owned allocation, conversion, formatting, standard-library, and platform failures and translate them to the documented status model. Mark such operations `noexcept` when the complete implementation boundary honors that contract.

The boundary begins after caller-owned arguments have been constructed. An API must not claim to catch an exception thrown before function entry.

RAII owners use a closed or inert default state when practical, an explicit checked acquisition operation such as `open()`, `start()`, or `init()`, and an explicit observable cleanup operation such as `close()`, `stop()`, or `shutdown()`. Destructors are non-throwing and perform best-effort cleanup; an explicit cleanup failure remains observable and retryable where practical.

## Allocation, threading, and performance

Performance is part of reusable API design. Correctness remains mandatory, but an API cleanup must not add redundant linear scans, allocation, mutex traffic, hidden state, or background work.

- Validate once at a trust or native boundary and combine conversion with validation where practical.
- Preserve caller-owned and reusable buffers.
- Avoid per-scalar, per-event, and fixed-result allocation.
- Avoid success-path diagnostic allocation.
- Keep hot passive values compact and generated lookup data out of public headers.
- Optional features remain lazy and impose no storage, allocation, worker thread, registration, or runtime activity when unused.
- Do not add a mandatory worker thread or unnecessary global lock.
- Batch transfers and reuse scratch, queue, arena, and conversion storage where the owning design permits it.
- Benchmark meaningful hot paths and pathological usage patterns. Benchmarks diagnose regressions; correctness tests do not use timing thresholds.

## Compatibility and ABI

GameWIP is pre-1.0. Public APIs may be corrected directly instead of carrying deprecated aliases when consumers can migrate in the same planned change. Preserve an old name only when an explicit compatibility requirement justifies the cost.

Installed libraries expose C++23 APIs and assume an ABI-compatible compiler, standard library, runtime, architecture, build configuration, and exact GameWIP package version. They do not promise a universal stable C ABI or binary compatibility across incompatible toolchains or package versions.

Use an explicit enum underlying type when representation is relevant to public ABI, bitmasks, serialization, storage, or interoperation. Validate values received across an ABI or trust boundary rather than assuming every underlying value names an enumerator.

## Passive types and ownership

Passive descriptions, options, IDs, enums, flags, events, views, statuses, and result values live in the owning library's single `Types` namespace when that library uses the project pattern. `Types` is a namespace, not a struct used as a namespace substitute.

When a large `Types` surface contains a real, independently understandable domain, organize it beneath `Types::<Domain>`. Do not create nesting merely for symmetry or for one or two trivial results. Keep broadly shared/core passive values directly under `Types`, and omit redundant domain words from type names when the nested namespace already supplies that context.

Resource-owning RAII types and stateful services live at the owning library namespace level. Focused stateless operations may use a descriptive service namespace. Public ownership, borrowing, lifetime, threading, and invalidation rules must be explicit.

## Configuration macros

Use `GAMEWIP_*` for configuration that genuinely belongs to the project as a whole. Library-specific configuration uses the owning library's prefix. Internal implementation and test-hook definitions use `<LIBRARY>_INTERNAL_*` (for example, `IO_INTERNAL_*`, `FILESYSTEM_INTERNAL_*`, or `TERMINAL_INTERNAL_*`). Do not add the project prefix merely because a library lives in the GameWIP repository.

## Public headers

Public headers expose portable supported declarations and do not leak internal storage, validation hooks, or platform types except through an explicitly platform-scoped interop header.

Umbrella headers may remain supported while focused headers are added. Splitting a physical header does not require splitting its namespace, target, or package. Give an important evolving configuration/state model or optional feature a focused header when that creates a real ownership, discoverability, dependency, or usability boundary; do not create one file per type mechanically. Header decomposition follows conceptual ownership rather than line count and is not automatically a release or feature blocker.
Public-header boundaries are independent from implementation-source and correctness-test boundaries. One cohesive umbrella header may be backed by several responsibility-focused `.cpp` files, and one test module may use several behavior-focused case files.

Generated lookup tables and other large implementation data remain in private implementation headers or source files. Every supported public entry header is compiled in isolation and exercised through installed-consumer validation.

## Related pages

- @ref project_contracts
- @ref project_library_compatibility
- @ref project_platform_backend_contract
- @ref project_decisions
