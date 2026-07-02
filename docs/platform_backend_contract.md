# GameWIP Platform Backend Contract

## Purpose

This file defines repository-wide rules for separating portable library code from operating-system implementations. It does not duplicate each library's internal backend function list; those declarations and comments are authoritative in the owning internal header.

Use this contract when adding a backend, reviewing platform boundaries, or deciding where new behavior belongs.

## Core rule

Portable public APIs and core behavior must not depend on native headers, handles, error codes, path encodings, dialogs, console structures, or process primitives.

```text
public API
    -> portable core
        -> owning internal backend contract
            -> selected platform implementation
```

Platform code implements a contract; it does not define product policy or redesign the public API.

## Platform selection

`cmake/LibraryPlatform.cmake` resolves one repository platform ID:

```text
Windows -> win32
Apple   -> macos
Unix    -> linux
```

The current implemented/supported ID is `win32`. Other mappings reserve stable names; a library that requires a backend must fail configuration clearly when the selected backend directory or source is absent.

An explicit `GAMEWIP_PLATFORM_ID` may select a controlled port or test configuration. It must not silently choose a backend whose ABI does not match the active compiler/runtime.

## Directory and file layout

Platform behavior belongs under the owning library:

```text
<library>/
  core/
  internal/
    <library>_platform.h
  platform/
    <platform-id>/
      <platform-id>_<feature>.cpp
      platform.cmake        # only when backend-local build setup is needed
```

The internal header declares the smallest backend contract needed by portable core code. Platform `.cpp` files implement it. `platform.cmake` may add system libraries, resources, generated files, or backend-only compile definitions; it must not own portable sources or project-wide options.

Avoid platform branches in public headers and portable core files. A narrowly justified compile-time branch is acceptable only when it expresses a compiler/language constraint that cannot be isolated behind the backend.

## Public API boundary

Public APIs prefer:

- standard C++ value types;
- `std::string` for UTF-8 text;
- `std::filesystem::path` for native filesystem paths;
- library-owned enums, options, results, and RAII types.

Public APIs do not expose:

- `HANDLE`, `HWND`, `DWORD`, Win32 structs, or platform headers;
- POSIX descriptors or structs as portable contracts;
- native error codes as the only failure model;
- backend storage layouts;
- test-hook state;
- a platform-specific string encoding disguised as portable text.

When callers genuinely need native integration, add an explicitly platform-scoped adapter rather than weakening the portable primary API.

## Internal contract standard

Each internal backend declaration must document enough for another platform implementation:

- purpose and observable result;
- ownership and lifetime of handles/storage;
- input/output encoding;
- blocking and thread-safety behavior;
- units, ranges, timeout semantics, and partial progress;
- native failure mapping and fallback rules;
- whether cleanup can fail and how retry/ownership state behaves;
- performance constraints that affect the portable caller.

Backend functions should return portable owning-library result types or narrow internal result structures. Core code translates those results into the public contract and owns cross-platform policy.

## Unicode and path rules

Project-owned text crossing a public boundary is UTF-8 unless the API explicitly states otherwise. Win32 backends convert at the native boundary and use wide-character operating-system APIs.

Filesystem paths use `std::filesystem::path` because path encoding and syntax are native concepts. A UTF-8 string is not treated as a path without an explicit conversion operation and error result.

Conversions must reject invalid input rather than substitute replacement characters in identifiers, paths, or other security-sensitive values. Text intended only for best-effort diagnostics may use a documented fallback if losing the message entirely would be worse.

## Error and cleanup rules

- Expected native failures map to the owning library's portable status/result model.
- Unknown native errors map to a stable general category while retaining internal diagnostic context where available.
- Invalid public arguments are rejected before a native call when practical.
- Partial transfers preserve observable progress.
- Destructors do not throw; explicit close/flush/unlock operations expose failures.
- A failed cleanup operation must not falsely discard ownership if the caller can retry safely.
- Security-sensitive path operations do not use check-then-reopen sequences that reintroduce replacement races.

## Concurrency and process state

The internal contract states whether synchronization is per object, per stream, per process, or caller-owned. Native callbacks and asynchronous completions must not outlive the state they access.

Operations that change process-wide state, such as current directory, environment variables, console mode, or signal/exception handling, require explicit scoped restoration or documented global coordination.

Timeouts use monotonic time for elapsed-duration decisions. A timeout result distinguishes “operation did not complete within the bound” from “no useful work occurred” whenever that distinction is observable.

## Internal test hooks

Hooks may make rare native failures deterministic, but they remain source-tree maintainer interfaces:

- compile-time gated by the owning library option;
- declared under the owning `internal/` directory;
- named in backend-neutral terms when core tests share them;
- resettable between scenarios;
- absent from public file sets and installed target interfaces;
- documented only in clearly labeled maintainer validation pages.

Hooks do not replace normal public behavior tests or manual verification of genuinely interactive native UI.

## Add a platform backend

1. Read every owning internal backend declaration and its contract comments.
2. Add `platform/<platform-id>/` implementations without changing public API merely to fit the new OS.
3. Add only backend-local requirements in `platform.cmake`.
4. Extend the central platform mapping only if the ID is new.
5. Implement Unicode, error, ownership, cleanup, timeout, and synchronization semantics explicitly.
6. Add platform-focused tests behind existing public behavior and internal hook boundaries.
7. Run public-header, package/install, correctness, static-analysis, documentation, and shipping validation on the new platform.
8. Document real behavioral differences in the consumer manual; document implementation mechanics in source comments.

A port is incomplete if it compiles but weakens failure behavior, cleanup ownership, encoding, security policy, or thread-safety guarantees.

## Review checklist

- [ ] Public headers include no native headers or unscoped native types.
- [ ] Core code does not contain scattered operating-system branches.
- [ ] Internal declarations are narrow and fully documented.
- [ ] Backend-local build requirements stay in the backend directory.
- [ ] UTF-8/native conversions and failure behavior are explicit.
- [ ] Partial progress, timeout, cleanup, and retry ownership are preserved.
- [ ] Process-global state is restored or coordinated.
- [ ] Hooks are gated, resettable, source-tree-only, and non-installed.
- [ ] Public behavior and package isolation pass on the target platform.

## Updating this contract

Add only rules that apply across libraries or future platforms. Put library-specific function signatures in internal headers, consumer-visible behavior in the owning library manual, implementation status in `implementation_checklist.md`, and proof status in `testing_checklist.md`.
