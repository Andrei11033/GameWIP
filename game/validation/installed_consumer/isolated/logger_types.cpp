/// @file logger_types.cpp
/// @brief Installed Logger focused-types header check.

#include "logger/types.h"

#include <string>
#include <type_traits>

static_assert(GameWIP::Logger::Types::Init::Info{}.outcome == GameWIP::Logger::Types::Init::Outcome::Disabled);
static_assert(GameWIP::Logger::Types::Report::Info{}.delivery == GameWIP::Logger::Types::Report::Delivery::None);
static_assert(GameWIP::Logger::Types::Health::Snapshot{}.state == GameWIP::Logger::Types::Health::State::Disabled);
static_assert(std::is_same_v<
              GameWIP::Logger::Types::Init::Result,
              GameWIP::Status::Types::Result<GameWIP::Logger::Types::Init::Info>>);
static_assert(std::is_same_v<
              GameWIP::Logger::Types::Report::Result,
              GameWIP::Status::Types::Result<GameWIP::Logger::Types::Report::Info>>);
static_assert(std::is_same_v<GameWIP::Logger::Types::LogFilePathResult, GameWIP::Status::Types::Result<std::string>>);
