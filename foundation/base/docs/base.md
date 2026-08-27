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

Include `base/checked_arithmetic.h` for two allocation-free, `constexpr`,
`noexcept` predicates:

- `GameWIP::Base::wouldAddOverflow(left, right)` reports whether unsigned
  addition would exceed the value type's maximum without performing the
  overflowing operation.
- `GameWIP::Base::wouldMultiplyOverflow(left, right)` provides the equivalent
  check for unsigned multiplication, including zero operands.

Both operands use the same unsigned integral type. A `false` result guarantees
that the corresponding operation is representable in that type.

# Win32 mechanisms

Include `base/platform/win32/dynamic_library.h` only in Win32 implementation
code. `GameWIP::Base::Win32::loadProcedure<FunctionType>(module, name)` is the
single typed `GetProcAddress` conversion boundary. `FunctionType` must be an
exact function-pointer type.

The function borrows the module handle and procedure name, performs no
allocation, and returns null for a null argument or unresolved export. It does
not load, retain, or release the module. A returned procedure remains valid
only while the caller keeps that module loaded.

# Generated API reference

The generated namespace pages for `GameWIP::Base` and
`GameWIP::Base::Win32` contain the exact template constraints, parameters, and
return contracts. Base is documented for source-tree maintainers; it is not an
installed consumer API.

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
