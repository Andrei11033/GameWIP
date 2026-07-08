/// @file assert_header.cpp
/// @brief Assert public-header self-containment compile check.
///
/// This translation unit intentionally includes only `debug/assert/assert.h` first to prove
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "debug/assert/assert.h"
