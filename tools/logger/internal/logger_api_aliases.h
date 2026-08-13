/// @file logger_api_aliases.h
/// @brief Private migration aliases for Logger implementation sources.

#pragma once

#include "logger/types.h"

// Source-tree-only aliases keep implementation churn separate from the public
// pre-1.0 migration. This header is not installed or documented as public API.
namespace GameWIP::Logger::Types
{
    using InitOutcome = Init::Outcome;
    using InitAdjustment = Init::Adjustment;
    using InitResult = Init::Result;

    using ReportOutcome = Report::Outcome;
    using ReportDelivery = Report::Delivery;
    using ReportResult = Report::Result;

    using HealthState = Health::State;
    using FailureSource = Health::FailureSource;
    using HealthSnapshot = Health::Snapshot;
} // namespace GameWIP::Logger::Types
