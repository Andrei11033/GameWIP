/// @file filesystem_header.cpp
/// @brief FileSystem public-header self-containment compile check.
///
/// This translation unit intentionally includes only `filesystem/filesystem.h` first to prove
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "filesystem/filesystem.h"
