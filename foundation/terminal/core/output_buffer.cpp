/// @file output_buffer.cpp
/// @brief Checked caller-owned Terminal text-buffer implementation.

#include "terminal/output.h"
#include "terminal/platform/backend.h"
#include "terminal/internal/results.h"
#include "terminal/internal/status.h"
#include "unicode/unicode.h"

#include <cstddef>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace GameWIP::Terminal
{
    /// ------------------------------------------------------------
    /// Buffer validation helpers
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        [[nodiscard]] bool validLineEnding(Types::Output::LineEnding lineEnding) noexcept
        {
            switch(lineEnding)
            {
            case Types::Output::LineEnding::Native:
            case Types::Output::LineEnding::Lf:
            case Types::Output::LineEnding::CrLf:
                return true;
            }

            return false;
        }

        [[nodiscard]] std::string_view lineEndingText(Types::Output::LineEnding lineEnding) noexcept
        {
            switch(lineEnding)
            {
            case Types::Output::LineEnding::Native:
                return Platform::nativeLineEnding();
            case Types::Output::LineEnding::Lf:
                return "\n";
            case Types::Output::LineEnding::CrLf:
                return "\r\n";
            }

            return {};
        }

        [[nodiscard]] GameWIP::Status::Types::Status invalidArgumentStatus(std::string_view message = {}) noexcept
        {
            return Internal::statusWithMessage(ErrorCode::InvalidArgument, message);
        }

        void rollbackString(std::string &text, std::size_t previousSize) noexcept
        {
            while(text.size() > previousSize)
            {
                text.pop_back();
            }
        }

        [[nodiscard]] bool validUtf8(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Buffer operations
    /// ------------------------------------------------------------

    Types::Output::LineEnding OutputBuffer::lineEnding() const noexcept
    {
        return lineEnding_;
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::setLineEnding(Types::Output::LineEnding lineEnding) noexcept
    {
        if(!validLineEnding(lineEnding))
        {
            return Internal::result(invalidArgumentStatus("Unknown terminal line ending."));
        }
        lineEnding_ = lineEnding;
        return GameWIP::Status::Types::Result<void>::success();
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::reserve(std::size_t bytes) noexcept
    {
        try
        {
            text_.reserve(bytes);
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    void OutputBuffer::clear() noexcept
    {
        text_.clear();
    }

    bool OutputBuffer::empty() const noexcept
    {
        return text_.empty();
    }

    std::size_t OutputBuffer::size() const noexcept
    {
        return text_.size();
    }

    std::string_view OutputBuffer::text() const noexcept
    {
        return text_;
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::appendText(std::string_view utf8Text) noexcept
    {
        if(!validUtf8(utf8Text))
        {
            return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::EncodingFailed));
        }

        const std::size_t previousSize = text_.size();
        try
        {
            text_.append(utf8Text);
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(...)
        {
            rollbackString(text_, previousSize);
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::appendLine(std::string_view utf8Text) noexcept
    {
        if(!validUtf8(utf8Text))
        {
            return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::EncodingFailed));
        }

        const std::size_t previousSize = text_.size();
        try
        {
            text_.append(utf8Text);
            text_.append(lineEndingText(lineEnding_));
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(...)
        {
            rollbackString(text_, previousSize);
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::vprint(std::string_view format, std::format_args arguments, bool appendLineEnding) noexcept
    {
        const std::size_t previousSize = text_.size();
        try
        {
            std::vformat_to(std::back_inserter(text_), format, arguments);

            if(appendLineEnding)
            {
                text_.append(lineEndingText(lineEnding_));
            }

            if(!validUtf8(std::string_view(text_).substr(previousSize)))
            {
                rollbackString(text_, previousSize);
                return Internal::result(GameWIP::Terminal::Internal::backendStatus(ErrorCode::EncodingFailed));
            }
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(...)
        {
            rollbackString(text_, previousSize);
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::writeTo(const Types::Output::TextOptions &options) const noexcept
    {
        return writeTo(Types::Output::Stream::Stdout, options);
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::writeTo(Types::Output::Stream stream, const Types::Output::TextOptions &options) const noexcept
    {
        try
        {
            return Terminal::writeText(stream, text_, options);
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::flushTo(const Types::Output::TextOptions &options) noexcept
    {
        return flushTo(Types::Output::Stream::Stdout, options);
    }

    GameWIP::Status::Types::Result<void> OutputBuffer::flushTo(Types::Output::Stream stream, const Types::Output::TextOptions &options) noexcept
    {
        GameWIP::Status::Types::Result<void> status = writeTo(stream, options);
        if(status.ok())
        {
            clear();
        }
        return status;
    }
} // namespace GameWIP::Terminal
