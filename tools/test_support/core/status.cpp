/// @file status.cpp
/// @brief Shared status formatting for TestSupport reports.

#include "test_support/types.h"

#include <format>
#include <string>

namespace GameWIP::TestSupport
{
    /// ------------------------------------------------------------
    /// Status formatting
    /// ------------------------------------------------------------

    std::string formatStatus(const Types::Status &status)
    {
        using GameWIP::Status::Types::codeName;
        using GameWIP::Status::Types::nativeCodeDomainName;

        std::string formatted(codeName(status.code()));
        const GameWIP::Status::Types::Diagnostic *diagnostic = status.diagnostic();

        if(diagnostic == nullptr)
        {
            return formatted;
        }

        if(diagnostic->nativeCode)
        {
            formatted += std::format(" (nativeCode={}:{})", nativeCodeDomainName(diagnostic->nativeCode->domain), diagnostic->nativeCode->value);
        }

        if(!diagnostic->message.empty())
        {
            formatted += std::format(" ({})", diagnostic->message);
        }

        return formatted;
    }
} // namespace GameWIP::TestSupport
