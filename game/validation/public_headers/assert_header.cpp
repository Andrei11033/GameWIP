/// @file assert_header.cpp
/// @brief Assert public-header self-containment compile check.
///
/// This translation unit intentionally includes only `debug/assert/assert.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "debug/assert/assert.h"

#if defined(ASSERT_POPUP_ON_ASSERT) || defined(ASSERT_POPUP_ON_CHECK)
#error "Assert popup policy must remain private to the Assert runtime."
#endif
