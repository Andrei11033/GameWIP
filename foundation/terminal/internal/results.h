/// @file results.h
/// @brief Private Terminal operation builders and shared result aliases.

#pragma once

#include "io/stream.h"
#include "status/result.h"
#include "terminal/input.h"
#include "terminal/output_types.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace GameWIP::Terminal::Internal
{
    template <typename T> struct ResultValue;

    template <typename T> struct ResultValue<GameWIP::Status::Types::Result<T>>
    {
        using Type = T;
    };

    using InputCapabilitiesResult = Terminal::Types::Input::CapabilitiesResult;
    using OutputCapabilitiesResult = Terminal::Types::Output::CapabilitiesResult;
    using SizeResult = Terminal::Types::SizeResult;
    using PositionResult = Terminal::Types::Cursor::PositionResult;
    using EventResult = Terminal::Types::Input::EventResult;
    using ByteResult = Terminal::Types::Input::ByteResult;
    using TextResult = Terminal::Types::Input::TextResult;
    using LineResult = Terminal::Types::Input::LineResult;
    using ByteWriteResult = GameWIP::Status::Types::Result<IO::Types::WriteProgress>;

    // Mutable backend payloads are converted to shared Results at operation boundaries.

    struct InputCapabilitiesOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Input::Capabilities capabilities{};

        [[nodiscard]] operator InputCapabilitiesResult() &&;
    };

    struct OutputCapabilitiesOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Output::Capabilities capabilities{};

        [[nodiscard]] operator OutputCapabilitiesResult() &&;
    };

    struct SizeOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Size size{};

        [[nodiscard]] operator SizeResult() &&;
    };

    struct PositionOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Cursor::Position position{};

        [[nodiscard]] operator PositionResult() &&;
    };

    struct EventOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Input::ReadOutcome outcome = Terminal::Types::Input::ReadOutcome::Completed;
        std::optional<Terminal::Types::Event> event{};

        [[nodiscard]] operator EventResult() &&;
    };

    struct ByteOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Input::ReadOutcome outcome = Terminal::Types::Input::ReadOutcome::Completed;
        std::size_t bytesRead = 0;

        [[nodiscard]] operator ByteResult() &&;
    };

    struct TextOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Input::ReadOutcome outcome = Terminal::Types::Input::ReadOutcome::Completed;
        std::string text{};
        bool wasTruncated = false;

        [[nodiscard]] operator TextResult() &&;
    };

    struct LineOperation
    {
        GameWIP::Status::Types::Status status{};
        Terminal::Types::Input::ReadOutcome outcome = Terminal::Types::Input::ReadOutcome::Completed;
        std::string line{};
        Terminal::Types::Input::ConsumedLineEnding consumedLineEnding = Terminal::Types::Input::ConsumedLineEnding::None;
        bool wasTruncated = false;

        [[nodiscard]] operator LineResult() &&;
    };

    struct ByteWriteOperation
    {
        GameWIP::Status::Types::Status status{};
        std::size_t bytesWritten = 0;

        [[nodiscard]] operator ByteWriteResult() &&;
    };

    // Convert backend outcomes while preserving useful partial payloads.

    [[nodiscard]] inline GameWIP::Status::Types::Result<void> result(GameWIP::Status::Types::Status status) noexcept
    {
        return status.ok() ? GameWIP::Status::Types::Result<void>::success()
                           : GameWIP::Status::Types::Result<void>::failure(std::move(status));
    }

    template <typename T> [[nodiscard]] GameWIP::Status::Types::Result<T> result(GameWIP::Status::Types::Result<T> value)
    {
        return value;
    }

    template <typename T>
    [[nodiscard]] GameWIP::Status::Types::Result<T> withStatus(
        GameWIP::Status::Types::Result<T> value,
        GameWIP::Status::Types::Status status)
    {
        if(status.ok())
        {
            return value.hasValue() ? GameWIP::Status::Types::Result<T>::success(std::move(value).value())
                                    : GameWIP::Status::Types::Result<T>::failure(
                                          GameWIP::Status::Types::Status(GameWIP::Status::Types::Code::Unknown));
        }

        if(value.hasValue())
        {
            return GameWIP::Status::Types::Result<T>::failure(std::move(status), std::move(value).value());
        }

        return GameWIP::Status::Types::Result<T>::failure(std::move(status));
    }

    template <typename T> [[nodiscard]] GameWIP::Status::Types::Result<T> failure(GameWIP::Status::Types::Status status) noexcept
    {
        if(status.ok())
        {
            status = GameWIP::Status::Types::Status(GameWIP::Status::Types::Code::Unknown);
        }
        return GameWIP::Status::Types::Result<T>::failure(std::move(status));
    }

    template <typename Result> [[nodiscard]] Result failedResult(GameWIP::Status::Types::Status status) noexcept
    {
        using Value = typename ResultValue<Result>::Type;
        return failure<Value>(std::move(status));
    }

    template <typename Result> [[nodiscard]] Result cancelledResult() noexcept
    {
        using Value = typename ResultValue<Result>::Type;
        Value value{};
        value.outcome = Terminal::Types::Input::ReadOutcome::Cancelled;
        return Result::success(std::move(value));
    }

    [[nodiscard]] inline InputCapabilitiesResult result(InputCapabilitiesOperation value) noexcept
    {
        if(!value.status.ok())
        {
            return InputCapabilitiesResult::failure(std::move(value.status));
        }
        return InputCapabilitiesResult::success(std::move(value.capabilities));
    }

    [[nodiscard]] inline OutputCapabilitiesResult result(OutputCapabilitiesOperation value) noexcept
    {
        if(!value.status.ok())
        {
            return OutputCapabilitiesResult::failure(std::move(value.status));
        }
        return OutputCapabilitiesResult::success(std::move(value.capabilities));
    }

    [[nodiscard]] inline SizeResult result(SizeOperation value) noexcept
    {
        if(!value.status.ok())
        {
            return SizeResult::failure(std::move(value.status));
        }
        return SizeResult::success(value.size);
    }

    [[nodiscard]] inline PositionResult result(PositionOperation value) noexcept
    {
        if(!value.status.ok())
        {
            return PositionResult::failure(std::move(value.status));
        }
        return PositionResult::success(value.position);
    }

    [[nodiscard]] inline EventResult result(EventOperation operation)
    {
        Terminal::Types::Input::EventInfo value{operation.outcome, std::move(operation.event)};
        if(!operation.status.ok())
        {
            return EventResult::failure(std::move(operation.status), std::move(value));
        }
        return EventResult::success(std::move(value));
    }

    [[nodiscard]] inline ByteResult result(ByteOperation operation) noexcept
    {
        Terminal::Types::Input::ByteInfo value{operation.outcome, operation.bytesRead};
        if(!operation.status.ok())
        {
            return ByteResult::failure(std::move(operation.status), value);
        }
        return ByteResult::success(value);
    }

    [[nodiscard]] inline TextResult result(TextOperation operation)
    {
        Terminal::Types::Input::TextInfo value{operation.outcome, std::move(operation.text), operation.wasTruncated};
        if(!operation.status.ok())
        {
            return TextResult::failure(std::move(operation.status), std::move(value));
        }
        return TextResult::success(std::move(value));
    }

    [[nodiscard]] inline LineResult result(LineOperation operation)
    {
        Terminal::Types::Input::LineInfo value{
            operation.outcome,
            std::move(operation.line),
            operation.consumedLineEnding,
            operation.wasTruncated};
        if(!operation.status.ok())
        {
            return LineResult::failure(std::move(operation.status), std::move(value));
        }
        return LineResult::success(std::move(value));
    }

    [[nodiscard]] inline ByteWriteResult result(ByteWriteOperation operation) noexcept
    {
        IO::Types::WriteProgress value{operation.bytesWritten};
        if(!operation.status.ok())
        {
            return ByteWriteResult::failure(std::move(operation.status), value);
        }
        return ByteWriteResult::success(value);
    }

    inline InputCapabilitiesOperation::operator InputCapabilitiesResult() &&
    {
        return result(std::move(*this));
    }

    inline OutputCapabilitiesOperation::operator OutputCapabilitiesResult() &&
    {
        return result(std::move(*this));
    }

    inline SizeOperation::operator SizeResult() &&
    {
        return result(std::move(*this));
    }

    inline PositionOperation::operator PositionResult() &&
    {
        return result(std::move(*this));
    }

    inline EventOperation::operator EventResult() &&
    {
        return result(std::move(*this));
    }

    inline ByteOperation::operator ByteResult() &&
    {
        return result(std::move(*this));
    }

    inline TextOperation::operator TextResult() &&
    {
        return result(std::move(*this));
    }

    inline LineOperation::operator LineResult() &&
    {
        return result(std::move(*this));
    }

    inline ByteWriteOperation::operator ByteWriteResult() &&
    {
        return result(std::move(*this));
    }
} // namespace GameWIP::Terminal::Internal
