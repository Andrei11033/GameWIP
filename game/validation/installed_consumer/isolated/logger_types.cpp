/// @file logger_types.cpp
/// @brief Installed Logger focused-types header check.

#include "logger/types.h"

static_assert(GameWIP::Logger::Types::Init::Result{}.outcome == GameWIP::Logger::Types::Init::Outcome::Disabled);
static_assert(GameWIP::Logger::Types::Report::Result{}.delivery == GameWIP::Logger::Types::Report::Delivery::None);
static_assert(GameWIP::Logger::Types::Health::Snapshot{}.state == GameWIP::Logger::Types::Health::State::Disabled);
