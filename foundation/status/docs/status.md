@page status Status

`GameWIP::Status` provides shared operation outcomes and results. It has no
platform or library dependencies.

## Outcomes and diagnostics

`Status::Types::Code` is the portable outcome used for decisions.
`Status::Types::Status::ok()` is true only for `Code::Success`. Codes such as
`EndOfStream`, `Cancelled`, `TimedOut`, `WouldBlock`, and `DestinationTooSmall`
describe outcomes; callers decide whether they are errors in context.

Native codes and diagnostic text are optional details. Native values include a
`NativeCodeDomain` and never replace the portable code. Messages are UTF-8,
unvalidated, and unstable for parsing. Code-only statuses allocate no
diagnostic storage. Requested details are immutable and shared when copied. If
detail allocation fails, the portable code remains and the detail is omitted.

```cpp
using GameWIP::Status::Types::Code;
using GameWIP::Status::Types::Status;

const Status status(Code::NotFound);
if(!status.ok())
{
    // Handle the portable outcome.
}

const Status diagnosed = Status::withDiagnostic(
    Code::ReadFailed,
    {GameWIP::Status::Types::NativeCodeDomain::Errno, 5},
    "reading configuration failed");
```

## Results

Use `Result<T>` when an operation can fail and returns a value; use
`Result<void>` when it returns no payload. `Result<T>::ok()` requires both a
successful status and a value. A failed result may retain partial data, so
check `hasValue()` before accessing `value()`. Accessing an absent value
terminates.

```cpp
using GameWIP::Status::Types::Result;

const Result<int> result = Result<int>::failure(Status(Code::PartialRead), 3);
if(!result.ok() && result.hasValue())
{
    const int completedUnits = result.value();
}
```

`Result<void>::ok()` reflects its status. Results store the operation status
and optional payload directly; payload storage and allocation behavior depend
on `T`.

## Dependency boundary

Status is a header-only package exported as `GameWIP::Status`. Higher-level libraries may depend on it; Status must not depend on those libraries.
