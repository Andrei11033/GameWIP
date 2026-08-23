@page internal_base Internal Base

# Purpose

Base is source-tree-only infrastructure for mechanisms shared by otherwise independent GameWIP implementations. Its admission rule is: share
mechanisms, not domain policy.

# Dependency boundary

`GameWIPBase` is an internal, header-only target exposed in the source tree as `GameWIP::Base`. It is not installed or exported, has no dependency
on another GameWIP target, and may use only the C++ standard library and required platform SDK headers. Installed package interfaces must not refer
to Base or its include directory.

# What belongs in Base

Code belongs here only when it is a small, stable mechanism needed unchanged by multiple first-party components. Checked unsigned arithmetic and
typed native procedure lookup are explicit yes examples.

# What does not belong in Base

Unicode conversion, logging, error policy, FileSystem semantics, Window semantics, DPI or cursor selection, and simulation time are explicit no
examples. Base also does not own generic utilities, scope-exit policy, or native-resource RAII in this version.

# Portable mechanisms

`checked_arithmetic.h` provides allocation-free, `constexpr`, `noexcept` predicates for unsigned addition and multiplication overflow.

# Win32 mechanisms

`platform/win32/dynamic_library.h` is the single typed `GetProcAddress` conversion boundary. Callers retain their exact function typedef and receive
null when resolution fails.

# Admission checklist

- The mechanism is already duplicated across independent components.
- Its behavior contains no domain-specific result, error, lifetime, or policy decision.
- It can remain independent of every GameWIP library target.
- Its public surface is narrow and directly testable.

# Testing

Base tests exercise arithmetic identity values, maximum and safe boundaries, overflow boundaries, and representative storage calculations. Native
procedure lookup is exercised through the Win32 consumers and build warning validation.

# Adding a helper

Document the repeated mechanism and its consumers, add focused tests, preserve the dependency boundary, and update this page. A new domain policy
belongs in its owning library instead.

# Related pages

- @ref project_cmake_infrastructure
- @ref project_platform_backend_contract
- @ref project_structure
