/// @file logger_config_header.cpp
/// @brief Logger configuration public-header self-containment compile check.
///
/// This translation unit intentionally includes only `logger/config.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "logger/config.h"
