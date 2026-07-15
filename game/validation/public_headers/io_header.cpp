/// @file io_header.cpp
/// @brief IO public-header self-containment compile check.
///
/// This translation unit intentionally includes only `io/io.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "io/io.h"
