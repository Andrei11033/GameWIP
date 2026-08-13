/// @file output_buffer.cpp
/// @brief Checked caller-owned Terminal text-buffer implementation.

#include "terminal/terminal.h"
#include "terminal/internal/terminal_platform.h"
#include "unicode/unicode.h"

#include <cstddef>
#include <format>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace GameWIP::Terminal
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        [[nodiscard]] bool validLineEnding(Types::Output::LineEnding lineEnding) noexcept
        {
            switch (lineEnding)
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
            switch (lineEnding)
            {
            case Types::Output::LineEnding::Native:
                return Detail::Platform::nativeLineEnding();
            case Types::Output::LineEnding::Lf:
                return "\n";
            case Types::Output::LineEnding::CrLf:
                return "\r\n";
            }
            return {};
        }

        [[nodiscard]] IO::Types::Status statusWithMessage(ErrorCode code, std::string_view message) noexcept
        {
            if (message.empty())
            {
                return IO::makeStatus(code);
            }
            try
            {
                return IO::makeStatus(code, 0, std::string(message));
            }
            catch (...)
            {
                return IO::makeStatus(code);
            }
        }

        [[nodiscard]] IO::Types::Status invalidArgumentStatus(std::string_view message = {}) noexcept
        {
            return statusWithMessage(ErrorCode::InvalidArgument, message);
        }

        [[nodiscard]] IO::Types::Status exceptionStatus() noexcept
        {
            try
            {
                throw;
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (const std::length_error &)
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }
            catch (const std::format_error &)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        void rollbackString(std::string &text, std::size_t previousSize) noexcept
        {
            while (text.size() > previousSize)
            {
                text.pop_back();
            }
        }

        [[nodiscard]] bool isValidUtf8(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid;
        }
    } // namespace

    Types::Output::LineEnding OutputBuffer::lineEnding() const noexcept
    {
        return lineEnding_;
    }

    IO::Types::Status OutputBuffer::setLineEnding(Types::Output::LineEnding lineEnding) noexcept
    {
        if (!validLineEnding(lineEnding))
        {
            return invalidArgumentStatus("Unknown terminal line ending.");
        }

        lineEnding_ = lineEnding;
        return IO::successStatus();
    }

    IO::Types::Status OutputBuffer::reserve(std::size_t bytes) noexcept
    {
        try
        {
            text_.reserve(bytes);
            return IO::successStatus();
        }
        catch (...)
        {
            return exceptionStatus();
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

    IO::Types::Status OutputBuffer::appendText(std::string_view utf8Text) noexcept
    {
        if (!isValidUtf8(utf8Text))
        {
            return IO::makeStatus(ErrorCode::EncodingFailed);
        }

        const std::size_t previousSize = text_.size();
        try
        {
            text_.append(utf8Text);
            return IO::successStatus();
        }
        catch (...)
        {
            rollbackString(text_, previousSize);
            return exceptionStatus();
        }
    }

    IO::Types::Status OutputBuffer::appendLine(std::string_view utf8Text) noexcept
    {
        if (!isValidUtf8(utf8Text))
        {
            return IO::makeStatus(ErrorCode::EncodingFailed);
        }

        const std::size_t previousSize = text_.size();
        try
        {
            text_.append(utf8Text);
            text_.append(lineEndingText(lineEnding_));
            return IO::successStatus();
        }
        catch (...)
        {
            rollbackString(text_, previousSize);
            return exceptionStatus();
        }
    }

    IO::Types::Status OutputBuffer::vprint(std::string_view format, std::format_args arguments, bool appendLineEnding) noexcept
    {
        const std::size_t previousSize = text_.size();
        try
        {
            std::vformat_to(std::back_inserter(text_), format, arguments);
            if (appendLineEnding)
            {
                text_.append(lineEndingText(lineEnding_));
            }
            if (!isValidUtf8(std::string_view(text_).substr(previousSize)))
            {
                rollbackString(text_, previousSize);
                return IO::makeStatus(ErrorCode::EncodingFailed);
            }
            return IO::successStatus();
        }
        catch (...)
        {
            rollbackString(text_, previousSize);
            return exceptionStatus();
        }
    }

    IO::Types::Status OutputBuffer::writeTo(const Types::Output::TextOptions &options) const noexcept
    {
        return writeTo(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status OutputBuffer::writeTo(Types::Output::Stream stream, const Types::Output::TextOptions &options) const noexcept
    {
        try
        {
            return Terminal::writeText(stream, text_, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status OutputBuffer::flushTo(const Types::Output::TextOptions &options) noexcept
    {
        return flushTo(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status OutputBuffer::flushTo(Types::Output::Stream stream, const Types::Output::TextOptions &options) noexcept
    {
        IO::Types::Status status = writeTo(stream, options);
        if (status.ok())
        {
            clear();
        }

        return status;
    }

} // namespace GameWIP::Terminal
