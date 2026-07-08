/// @file logger_header.cpp
/// @brief Logger public-header self-containment compile check.
///
/// This translation unit intentionally includes only `logger/logger.h` first to prove
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "logger/logger.h"
