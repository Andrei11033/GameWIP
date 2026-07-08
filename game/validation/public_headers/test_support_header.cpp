/// @file test_support_header.cpp
/// @brief TestSupport public-header self-containment compile check.
///
/// This translation unit intentionally includes only `test_support/test_support.h` first to prove
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "test_support/test_support.h"
