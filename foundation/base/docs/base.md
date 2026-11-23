@page internal_base Base

# Purpose

Base contains small mechanisms shared by independent GameWIP components. Keep
domain policy in its owning library.

# Dependency boundary

`GameWIP::Base` is a header-only internal target for source-tree use. It is not
installed or exported, has no GameWIP dependencies, and may use the C++ standard
library and required platform SDK headers.

# Checked arithmetic

Include `base/checked_arithmetic.h` for allocation-free, `constexpr`, `noexcept`
predicates on unsigned integral values:

- `GameWIP::Base::wouldAddOverflow(left, right)` checks whether addition exceeds
  the value type's maximum.
- `GameWIP::Base::wouldMultiplyOverflow(left, right)` checks the equivalent for
  multiplication, including zero operands.

A `false` result guarantees that the operation is representable in that type.

# Win32 procedure lookup

Win32 implementation code may include
`base/platform/win32/dynamic_library.h` for
`GameWIP::Base::Win32::loadProcedure<FunctionType>(module, name)`. The template
requires the exact function-pointer type and returns null for null arguments or
an unresolved export. It borrows the module and name; the caller keeps the module
loaded while using the returned procedure.

# Keep Base narrow

Add a mechanism only when independent components already need the same
implementation and it has no domain-specific policy. Unicode conversion,
logging, error policy, FileSystem and Window behavior, DPI and cursor selection,
simulation time, generic utilities, scope-exit policy, and native-resource RAII
belong with their owning libraries.

# Validation

`game/validation/tests/base` covers arithmetic boundaries and typed Win32
procedure lookup. See @ref project_testing for build and run commands.

# Related pages

- @ref project_cmake_infrastructure
- @ref project_platform_backend_contract
- @ref project_structure
