/// @file logger_config.cpp
/// @brief Installed Logger focused-configuration header check.

#include "logger/config.h"

static_assert(GameWIP::Logger::Types::Config{}.logDirectory == "logs");
