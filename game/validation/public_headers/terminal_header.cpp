/// @file terminal_header.cpp
/// @brief Terminal public-header self-containment compile check.
///
/// This translation unit intentionally includes only `terminal/terminal.h` first to prove
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "terminal/terminal.h"
