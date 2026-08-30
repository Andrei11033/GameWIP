/// @file line_input.cpp
/// @brief Terminal-managed Unicode line editing and echo implementation.

#include "terminal/input.h"
#include "terminal/output.h"
#include "terminal/internal/terminal_input.h"
#include "base/checked_arithmetic.h"
#include "terminal/internal/terminal_platform.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace GameWIP::Terminal
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status unsupportedStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::Unsupported);
        }

        [[nodiscard]] IO::Types::Status invalidArgumentStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        [[nodiscard]] std::optional<std::chrono::milliseconds> remainingReadTimeout(
            std::chrono::steady_clock::time_point start,
            const std::optional<std::chrono::milliseconds> &timeout) noexcept
        {
            if (!timeout.has_value() || timeout->count() == 0)
            {
                return timeout;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (elapsed >= *timeout)
            {
                return std::chrono::milliseconds{0};
            }
            return *timeout - elapsed;
        }

        [[nodiscard]] bool hasShortcutModifier(Types::Events::KeyModifier modifiers) noexcept
        {
            return Types::Events::hasModifier(modifiers, Types::Events::KeyModifier::Control) ||
                   Types::Events::hasModifier(modifiers, Types::Events::KeyModifier::Alt) ||
                   Types::Events::hasModifier(modifiers, Types::Events::KeyModifier::Super) ||
                   Types::Events::hasModifier(modifiers, Types::Events::KeyModifier::Hyper) ||
                   Types::Events::hasModifier(modifiers, Types::Events::KeyModifier::Meta);
        }

        class GraphemeIndex final
        {
        public:
            explicit GraphemeIndex(std::vector<std::size_t> &storage) noexcept
                : storage_(storage)
            {
            }

            void invalidate() noexcept
            {
                ready_ = false;
                cursor_.clear();
            }

            [[nodiscard]] IO::Types::Status seek(std::string_view text, std::size_t byteOffset)
            {
                IO::Types::Status status = ensure(text);
                if (!status.ok())
                {
                    return status;
                }

                if (cursor_.seek(byteOffset).outcome != Unicode::Types::Utf8::BoundaryOutcome::Found)
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed);
                }
                return IO::successStatus();
            }

            [[nodiscard]] std::optional<std::size_t> previous(std::string_view text, std::size_t byteOffset, IO::Types::Status &status)
            {
                status = seek(text, byteOffset);
                if (!status.ok())
                {
                    return std::nullopt;
                }

                const Unicode::Types::Utf8::BoundaryResult result = cursor_.previous();
                if (result.outcome == Unicode::Types::Utf8::BoundaryOutcome::AtBeginning)
                {
                    return std::nullopt;
                }
                if (result.outcome != Unicode::Types::Utf8::BoundaryOutcome::Found)
                {
                    status = IO::makeStatus(ErrorCode::EncodingFailed);
                    return std::nullopt;
                }
                return result.byteOffset;
            }

            [[nodiscard]] std::optional<std::size_t> next(std::string_view text, std::size_t byteOffset, IO::Types::Status &status)
            {
                status = seek(text, byteOffset);
                if (!status.ok())
                {
                    return std::nullopt;
                }

                const Unicode::Types::Utf8::BoundaryResult result = cursor_.next();
                if (result.outcome == Unicode::Types::Utf8::BoundaryOutcome::AtEnd)
                {
                    return std::nullopt;
                }
                if (result.outcome != Unicode::Types::Utf8::BoundaryOutcome::Found)
                {
                    status = IO::makeStatus(ErrorCode::EncodingFailed);
                    return std::nullopt;
                }
                return result.byteOffset;
            }

            void retainThroughCurrent() noexcept
            {
                if (ready_)
                {
                    cursor_.discardAfterCurrent();
                }
            }

            [[nodiscard]] IO::Types::Status normalizeCaret(std::string_view text, std::size_t &byteOffset)
            {
                IO::Types::Status status = ensure(text);
                if (!status.ok())
                {
                    return status;
                }

                const Unicode::Types::Utf8::BoundaryResult exact = cursor_.seek(byteOffset);
                if (exact.outcome == Unicode::Types::Utf8::BoundaryOutcome::Found)
                {
                    return IO::successStatus();
                }

                const Unicode::Types::Utf8::BoundaryResult next = Unicode::Utf8::nextGraphemeBoundary(text, byteOffset);
                if (next.outcome == Unicode::Types::Utf8::BoundaryOutcome::Found)
                {
                    byteOffset = next.byteOffset;
                    static_cast<void>(cursor_.seek(byteOffset));
                    return IO::successStatus();
                }
                if (next.outcome == Unicode::Types::Utf8::BoundaryOutcome::AtEnd)
                {
                    byteOffset = text.size();
                    static_cast<void>(cursor_.seek(byteOffset));
                    return IO::successStatus();
                }
                return IO::makeStatus(ErrorCode::EncodingFailed);
            }

        private:
            [[nodiscard]] IO::Types::Status ensure(std::string_view text)
            {
                if (ready_)
                {
                    return IO::successStatus();
                }

                Unicode::Types::Utf8::GraphemeIndexResult indexed = cursor_.reset(text, storage_);

                if (indexed.outcome == Unicode::Types::Utf8::GraphemeIndexOutcome::DestinationTooSmall)
                {
                    storage_.resize(indexed.requiredBoundaryCount);
                    indexed = cursor_.reset(text, storage_);
                }

                if (indexed.outcome != Unicode::Types::Utf8::GraphemeIndexOutcome::Indexed)
                {
                    cursor_.clear();
                    return IO::makeStatus(ErrorCode::EncodingFailed);
                }

                ready_ = true;
                return IO::successStatus();
            }

            std::vector<std::size_t> &storage_;
            Unicode::Utf8::GraphemeCursor cursor_;
            bool ready_ = false;
        };

        class LineEcho final
        {
        public:
            LineEcho(Types::Input::Stream input, Types::Output::Stream output, bool enabled) noexcept
                : input_(input)
                , output_(output)
                , enabled_(enabled)
            {
            }

            [[nodiscard]] IO::Types::Status begin()
            {
                if (!enabled_)
                {
                    return IO::successStatus();
                }

                const Types::Output::CapabilitiesResult prepared = prepareOutput(output_);
                if (!prepared.status.ok())
                {
                    return prepared.status;
                }
                if (!prepared.capabilities.supportsCursorMovement || !prepared.capabilities.supportsCursorPositionQuery ||
                    !prepared.capabilities.supportsTerminalSize)
                {
                    return unsupportedStatus();
                }

                const Types::SizeResult size = getTerminalSize(output_);
                if (!size.status.ok())
                {
                    return size.status;
                }
                size_ = size.size;

                const Types::Cursor::PositionResult position = queryRenderingPosition();
                if (!position.status.ok())
                {
                    return position.status;
                }

                origin_ = position.position;
                active_ = true;
                renderedCells_ = 0;
                caretCells_ = 0;
                return IO::successStatus();
            }

            void updateSize(Types::Size size) noexcept
            {
                if (size.columns > 0 && size.rows > 0)
                {
                    size_ = size;
                }
            }

            /// @brief Echoes an append-at-end edit without rewriting the existing line.
            [[nodiscard]] IO::Types::Status append(std::string_view appendedText)
            {
                if (!enabled_ || !active_ || appendedText.empty())
                {
                    return IO::successStatus();
                }

                IO::Types::Status status = writeText(output_, appendedText);
                if (!status.ok())
                {
                    return status;
                }

                const bool simpleAscii = std::all_of(
                    appendedText.begin(),
                    appendedText.end(),
                    [](unsigned char byte)
                    {
                        return byte >= 0x20 && byte < 0x7f;
                    });
                if (simpleAscii && !GameWIP::Base::wouldAddOverflow(renderedCells_, appendedText.size()))
                {
                    // Ordinary append-at-end typing remains query-free. Printable ASCII advances by one cell;
                    // Unicode and control text take the measured slow path so Terminal does not invent a width policy.
                    renderedCells_ += appendedText.size();
                    caretCells_ = renderedCells_;
                    return IO::successStatus();
                }

                const Types::Cursor::PositionResult current = queryRenderingPosition();
                if (!current.status.ok())
                {
                    return current.status;
                }
                const std::optional<std::size_t> cells = cellDistance(origin_, current.position);
                if (!cells.has_value())
                {
                    return unsupportedStatus();
                }
                renderedCells_ = *cells;
                caretCells_ = renderedCells_;
                return IO::successStatus();
            }

            /// @brief Erases one known single-cell ASCII suffix without rewriting the line.
            [[nodiscard]] IO::Types::Status eraseTrailingAsciiCell()
            {
                if (!enabled_ || !active_)
                {
                    return IO::successStatus();
                }
                if (size_.columns == 0)
                {
                    return unsupportedStatus();
                }

                const Types::Cursor::PositionResult current = queryRenderingPosition();
                if (!current.status.ok())
                {
                    return current.status;
                }

                const std::optional<Types::Cursor::Position> currentOrigin = positionBefore(current.position, caretCells_);
                if (!currentOrigin.has_value())
                {
                    return unsupportedStatus();
                }

                Types::Cursor::Position previous = current.position;
                if (previous.column > 0)
                {
                    --previous.column;
                }
                else if (previous.row > 0)
                {
                    --previous.row;
                    previous.column = size_.columns - 1;
                }
                else
                {
                    return unsupportedStatus();
                }

                IO::Types::Status status = setRenderingPosition(previous);
                if (!status.ok())
                {
                    return status;
                }
                status = writeText(output_, " ");
                if (!status.ok())
                {
                    return status;
                }
                status = setRenderingPosition(previous);
                if (status.ok())
                {
                    origin_ = *currentOrigin;
                    --renderedCells_;
                    --caretCells_;
                }
                return status;
            }

            /// @brief Redraws when navigation or an edit invalidates the displayed suffix.
            [[nodiscard]] IO::Types::Status redraw(std::string_view line, std::size_t caretByteOffset)
            {
                if (!enabled_ || !active_)
                {
                    return IO::successStatus();
                }
                if (caretByteOffset > line.size() || size_.columns == 0)
                {
                    return invalidArgumentStatus();
                }

                const Types::Cursor::PositionResult current = queryRenderingPosition();
                if (!current.status.ok())
                {
                    return current.status;
                }
                const std::optional<Types::Cursor::Position> rebuiltOrigin = positionBefore(current.position, caretCells_);
                if (!rebuiltOrigin.has_value())
                {
                    return unsupportedStatus();
                }
                origin_ = *rebuiltOrigin;

                IO::Types::Status status = setRenderingPosition(origin_);
                if (!status.ok())
                {
                    return status;
                }

                status = writeText(output_, line);
                if (!status.ok())
                {
                    return status;
                }

                const Types::Cursor::PositionResult end = queryRenderingPosition();
                if (!end.status.ok())
                {
                    return end.status;
                }

                const std::optional<std::size_t> newCells = cellDistance(origin_, end.position);
                if (!newCells.has_value())
                {
                    return unsupportedStatus();
                }

                if (renderedCells_ > *newCells)
                {
                    status = writeSpaces(renderedCells_ - *newCells);
                    if (!status.ok())
                    {
                        return status;
                    }
                }

                renderedCells_ = *newCells;

                status = setRenderingPosition(origin_);
                if (!status.ok())
                {
                    return status;
                }
                if (caretByteOffset > 0)
                {
                    status = writeText(output_, line.substr(0, caretByteOffset));
                    if (!status.ok())
                    {
                        return status;
                    }

                    const Types::Cursor::PositionResult caretPosition = queryRenderingPosition();
                    if (!caretPosition.status.ok())
                    {
                        return caretPosition.status;
                    }
                    const std::optional<std::size_t> cells = cellDistance(origin_, caretPosition.position);
                    if (!cells.has_value())
                    {
                        return unsupportedStatus();
                    }
                    caretCells_ = *cells;
                }
                else
                {
                    caretCells_ = 0;
                }
                return IO::successStatus();
            }

            [[nodiscard]] IO::Types::Status finish(std::string_view line, std::size_t caretByteOffset)
            {
                if (!enabled_ || !active_)
                {
                    return IO::successStatus();
                }

                if (caretByteOffset != line.size())
                {
                    IO::Types::Status status = redraw(line, line.size());
                    if (!status.ok())
                    {
                        return status;
                    }
                }
                return writeText(output_, "\r\n");
            }

        private:
            [[nodiscard]] Types::Cursor::PositionResult queryRenderingPosition() const
            {
                return Detail::getLineRenderingCursorPosition(output_, input_);
            }

            [[nodiscard]] IO::Types::Status setRenderingPosition(Types::Cursor::Position position) const
            {
                return Detail::setLineRenderingCursorPosition(output_, position);
            }

            [[nodiscard]] std::optional<Types::Cursor::Position> positionBefore(Types::Cursor::Position end, std::size_t cells) const noexcept
            {
                if (size_.columns == 0 || end.column >= size_.columns)
                {
                    return std::nullopt;
                }

                const std::uint64_t endLinear = static_cast<std::uint64_t>(end.row) * size_.columns + end.column;
                if (cells > endLinear)
                {
                    return std::nullopt;
                }
                const std::uint64_t beginLinear = endLinear - cells;
                return Types::Cursor::Position{
                    .column = static_cast<std::uint32_t>(beginLinear % size_.columns),
                    .row = static_cast<std::uint32_t>(beginLinear / size_.columns)};
            }

            [[nodiscard]] std::optional<std::size_t> cellDistance(Types::Cursor::Position begin, Types::Cursor::Position end) const noexcept
            {
                if (size_.columns == 0 || begin.column >= size_.columns || end.column >= size_.columns || end.row < begin.row)
                {
                    return std::nullopt;
                }

                const std::uint64_t beginLinear = static_cast<std::uint64_t>(begin.row) * size_.columns + begin.column;
                const std::uint64_t endLinear = static_cast<std::uint64_t>(end.row) * size_.columns + end.column;
                if (endLinear < beginLinear || endLinear - beginLinear > std::numeric_limits<std::size_t>::max())
                {
                    return std::nullopt;
                }
                return static_cast<std::size_t>(endLinear - beginLinear);
            }

            [[nodiscard]] IO::Types::Status writeSpaces(std::size_t count)
            {
                static constexpr std::string_view spaces = "                                                                ";

                while (count > 0)
                {
                    const std::size_t chunk = std::min(count, spaces.size());
                    IO::Types::Status status = writeText(output_, spaces.substr(0, chunk));
                    if (!status.ok())
                    {
                        return status;
                    }
                    count -= chunk;
                }
                return IO::successStatus();
            }

            Types::Input::Stream input_;
            Types::Output::Stream output_;
            Types::Size size_{};
            Types::Cursor::Position origin_{};
            std::size_t renderedCells_ = 0;
            std::size_t caretCells_ = 0;
            bool enabled_ = false;
            bool active_ = false;
        };

        [[nodiscard]] IO::Types::Status insertScalar(
            std::string &line,
            std::size_t &caret,
            char32_t scalar,
            std::size_t maxBytes,
            bool &wasTruncated,
            GraphemeIndex &graphemes)
        {
            const Unicode::Types::Utf8::EncodeResult encoded = Unicode::Utf8::encodeScalar(scalar);
            if (encoded.outcome != Unicode::Types::EncodeOutcome::Encoded)
            {
                return IO::makeStatus(ErrorCode::EncodingFailed);
            }

            const std::size_t bytes = encoded.byteCount;
            if (bytes > maxBytes - std::min(maxBytes, line.size()))
            {
                wasTruncated = true;
                return IO::successStatus();
            }

            const bool appendedAtEnd = caret == line.size();
            line.insert(caret, encoded.bytes.data(), bytes);
            caret += bytes;
            graphemes.invalidate();
            return appendedAtEnd ? IO::successStatus() : graphemes.normalizeCaret(line, caret);
        }

        [[nodiscard]] IO::Types::Status insertPaste(
            std::string &line,
            std::size_t &caret,
            std::string_view text,
            std::size_t maxBytes,
            bool &wasTruncated,
            GraphemeIndex &graphemes)
        {
            const std::size_t remaining = maxBytes - std::min(maxBytes, line.size());
            std::size_t accepted = 0;
            while (accepted < text.size())
            {
                const Unicode::Types::Utf8::DecodeResult decoded = Unicode::Utf8::decodeScalar(text.substr(accepted));
                if (decoded.outcome != Unicode::Types::DecodeOutcome::Decoded)
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed);
                }
                if (decoded.bytesConsumed > remaining - std::min(remaining, accepted))
                {
                    break;
                }
                accepted += decoded.bytesConsumed;
            }

            if (accepted < text.size())
            {
                wasTruncated = true;
            }
            if (accepted > 0)
            {
                const bool appendedAtEnd = caret == line.size();
                line.insert(caret, text.substr(0, accepted));
                caret += accepted;
                graphemes.invalidate();
                if (!appendedAtEnd)
                {
                    return graphemes.normalizeCaret(line, caret);
                }
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status completeManagedLine(
            Types::Input::LineResult &result,
            std::string line,
            const Types::Input::LineOptions &options)
        {
            result.consumedLineEnding = Types::Input::ConsumedLineEnding::CrLf;

            std::string_view ending;
            switch (options.lineEndingMode)
            {
            case Types::Input::LineEndingMode::Strip:
                ending = {};
                break;
            case Types::Input::LineEndingMode::Keep:
                ending = "\r\n";
                break;
            case Types::Input::LineEndingMode::NormalizeToLf:
                ending = "\n";
                break;
            }

            const std::size_t maxBytes = options.maxReturnedBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
                                             ? std::numeric_limits<std::size_t>::max()
                                             : static_cast<std::size_t>(options.maxReturnedBytes);

            if (ending.size() <= maxBytes - std::min(maxBytes, line.size()))
            {
                line.append(ending);
            }
            else
            {
                result.wasTruncated = true;
            }

            result.line = std::move(line);
            return IO::successStatus();
        }

        [[nodiscard]] Types::Input::EventResult readManagedEvent(
            Types::Input::Stream input,
            Types::Output::Stream output,
            const Types::Input::EventOptions &options)
        {
            std::lock_guard lock(Detail::inputIoMutex(input));
            return Detail::Platform::readEvent(input, output, options);
        }

    } // namespace

    Types::Input::LineResult Detail::managedTerminalLineRead(
        Types::Input::Stream input,
        Types::Output::Stream output,
        const Types::Input::LineOptions &options,
        std::vector<std::size_t> &graphemeStorage)
    {
        Types::Input::LineResult result;
        result.status = IO::successStatus();

        const std::size_t maxBytes = options.maxReturnedBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
                                         ? std::numeric_limits<std::size_t>::max()
                                         : static_cast<std::size_t>(options.maxReturnedBytes);

        std::string line;
        line.reserve(std::min<std::size_t>(maxBytes, 256));
        std::size_t caret = 0;
        GraphemeIndex graphemes(graphemeStorage);
        LineEcho echo(input, output, options.echo);

        result.status = echo.begin();
        if (!result.status.ok())
        {
            return result;
        }

        const auto start = std::chrono::steady_clock::now();

        while (true)
        {
            if (options.timeout.has_value() && options.timeout->count() > 0 && std::chrono::steady_clock::now() - start >= *options.timeout)
            {
                result.outcome = Types::Input::ReadOutcome::TimedOut;
                result.line = std::move(line);
                return result;
            }

            Types::Input::EventOptions eventOptions;
            eventOptions.timeout = remainingReadTimeout(start, options.timeout);
            eventOptions.stopToken = options.stopToken;

            Types::Input::EventResult eventResult = readManagedEvent(input, output, eventOptions);
            if (!eventResult.status.ok())
            {
                result.status = std::move(eventResult.status);
                result.outcome = eventResult.outcome;
                result.line = std::move(line);
                return result;
            }
            if (eventResult.outcome != Types::Input::ReadOutcome::Completed || !eventResult.event.has_value())
            {
                result.outcome = eventResult.outcome;
                result.line = std::move(line);
                return result;
            }

            if (const Types::Events::Resize *resize = eventResult.event->getIf<Types::Events::Resize>())
            {
                echo.updateSize(resize->size);
                result.status = echo.redraw(line, caret);
                if (!result.status.ok())
                {
                    result.line = std::move(line);
                    return result;
                }
                continue;
            }

            if (const Types::Events::Paste *paste = eventResult.event->getIf<Types::Events::Paste>())
            {
                const std::size_t oldSize = line.size();
                const std::size_t oldCaret = caret;
                result.status = insertPaste(line, caret, paste->text, maxBytes, result.wasTruncated, graphemes);
                if (!result.status.ok())
                {
                    result.line = std::move(line);
                    return result;
                }

                result.status = oldCaret == oldSize ? echo.append(std::string_view(line).substr(oldSize)) : echo.redraw(line, caret);
                if (!result.status.ok())
                {
                    result.line = std::move(line);
                    return result;
                }
                continue;
            }

            const Types::Events::Key *keyEvent = eventResult.event->getIf<Types::Events::Key>();
            if (keyEvent == nullptr || keyEvent->action == Types::Events::KeyAction::Release)
            {
                continue;
            }

            const std::uint32_t occurrences = keyEvent->action == Types::Events::KeyAction::Repeat ? std::max(keyEvent->repeatCount, 1U) : 1U;

            if (const auto *character = std::get_if<Types::Events::CharacterKey>(&keyEvent->key))
            {
                if (hasShortcutModifier(keyEvent->modifiers))
                {
                    continue;
                }

                const std::size_t oldSize = line.size();
                const std::size_t oldCaret = caret;
                for (std::uint32_t occurrence = 0; occurrence < occurrences; ++occurrence)
                {
                    result.status = insertScalar(line, caret, character->value, maxBytes, result.wasTruncated, graphemes);
                    if (!result.status.ok())
                    {
                        result.line = std::move(line);
                        return result;
                    }
                }

                result.status = oldCaret == oldSize ? echo.append(std::string_view(line).substr(oldSize)) : echo.redraw(line, caret);
                if (!result.status.ok())
                {
                    result.line = std::move(line);
                    return result;
                }
                continue;
            }

            const auto *named = std::get_if<Types::Events::NamedKey>(&keyEvent->key);
            if (named == nullptr)
            {
                continue;
            }

            if (*named == Types::Events::NamedKey::Enter)
            {
                result.status = echo.finish(line, caret);
                if (!result.status.ok())
                {
                    result.line = std::move(line);
                    return result;
                }
                result.status = completeManagedLine(result, std::move(line), options);
                return result;
            }

            if (hasShortcutModifier(keyEvent->modifiers))
            {
                continue;
            }

            bool redrawRequired = false;
            for (std::uint32_t occurrence = 0; occurrence < occurrences; ++occurrence)
            {
                IO::Types::Status navigationStatus = IO::successStatus();

                switch (*named)
                {
                case Types::Events::NamedKey::Backspace:
                {
                    const std::optional<std::size_t> previous = graphemes.previous(line, caret, navigationStatus);
                    if (!navigationStatus.ok())
                    {
                        result.status = navigationStatus;
                        result.line = std::move(line);
                        return result;
                    }
                    if (!previous.has_value())
                    {
                        break;
                    }

                    const bool suffixDeletion = caret == line.size();
                    const bool singleCellAscii = suffixDeletion && caret - *previous == 1 && static_cast<unsigned char>(line[*previous]) >= 0x20 &&
                                                 static_cast<unsigned char>(line[*previous]) < 0x7f;

                    if (suffixDeletion)
                    {
                        line.resize(*previous);
                        caret = *previous;
                        graphemes.retainThroughCurrent();

                        if (singleCellAscii && !redrawRequired)
                        {
                            result.status = echo.eraseTrailingAsciiCell();
                            if (result.status.code == ErrorCode::Unsupported)
                            {
                                result.status = IO::successStatus();
                                redrawRequired = true;
                            }
                            else if (!result.status.ok())
                            {
                                result.line = std::move(line);
                                return result;
                            }
                        }
                        else
                        {
                            redrawRequired = true;
                        }
                    }
                    else
                    {
                        line.erase(*previous, caret - *previous);
                        caret = *previous;
                        graphemes.invalidate();
                        navigationStatus = graphemes.normalizeCaret(line, caret);
                        if (!navigationStatus.ok())
                        {
                            result.status = navigationStatus;
                            result.line = std::move(line);
                            return result;
                        }
                        redrawRequired = true;
                    }
                    break;
                }
                case Types::Events::NamedKey::Delete:
                {
                    const std::optional<std::size_t> next = graphemes.next(line, caret, navigationStatus);
                    if (!navigationStatus.ok())
                    {
                        result.status = navigationStatus;
                        result.line = std::move(line);
                        return result;
                    }
                    if (!next.has_value())
                    {
                        break;
                    }

                    redrawRequired = true;
                    if (*next == line.size())
                    {
                        line.resize(caret);
                        navigationStatus = graphemes.seek(line, caret);
                        if (!navigationStatus.ok())
                        {
                            graphemes.invalidate();
                        }
                        else
                        {
                            graphemes.retainThroughCurrent();
                        }
                    }
                    else
                    {
                        line.erase(caret, *next - caret);
                        graphemes.invalidate();
                        navigationStatus = graphemes.normalizeCaret(line, caret);
                        if (!navigationStatus.ok())
                        {
                            result.status = navigationStatus;
                            result.line = std::move(line);
                            return result;
                        }
                    }
                    break;
                }
                case Types::Events::NamedKey::ArrowLeft:
                {
                    const std::optional<std::size_t> previous = graphemes.previous(line, caret, navigationStatus);
                    if (!navigationStatus.ok())
                    {
                        result.status = navigationStatus;
                        result.line = std::move(line);
                        return result;
                    }
                    if (previous.has_value())
                    {
                        caret = *previous;
                        redrawRequired = true;
                    }
                    break;
                }
                case Types::Events::NamedKey::ArrowRight:
                {
                    const std::optional<std::size_t> next = graphemes.next(line, caret, navigationStatus);
                    if (!navigationStatus.ok())
                    {
                        result.status = navigationStatus;
                        result.line = std::move(line);
                        return result;
                    }
                    if (next.has_value())
                    {
                        caret = *next;
                        redrawRequired = true;
                    }
                    break;
                }
                case Types::Events::NamedKey::Home:
                    redrawRequired = redrawRequired || caret != 0;
                    caret = 0;
                    break;
                case Types::Events::NamedKey::End:
                    redrawRequired = redrawRequired || caret != line.size();
                    caret = line.size();
                    break;
                case Types::Events::NamedKey::Tab:
                    if (!Types::Events::hasModifier(keyEvent->modifiers, Types::Events::KeyModifier::Shift))
                    {
                        const std::size_t oldSize = line.size();
                        const std::size_t oldCaret = caret;
                        result.status = insertScalar(line, caret, U'\t', maxBytes, result.wasTruncated, graphemes);
                        if (!result.status.ok())
                        {
                            result.line = std::move(line);
                            return result;
                        }

                        if (oldCaret == oldSize && !redrawRequired)
                        {
                            result.status = echo.append(std::string_view(line).substr(oldSize));
                            if (!result.status.ok())
                            {
                                result.line = std::move(line);
                                return result;
                            }
                        }
                        else
                        {
                            redrawRequired = true;
                        }
                    }
                    break;
                case Types::Events::NamedKey::Enter:
                case Types::Events::NamedKey::Escape:
                case Types::Events::NamedKey::Insert:
                case Types::Events::NamedKey::PageUp:
                case Types::Events::NamedKey::PageDown:
                case Types::Events::NamedKey::ArrowUp:
                case Types::Events::NamedKey::ArrowDown:
                case Types::Events::NamedKey::Begin:
                case Types::Events::NamedKey::CapsLock:
                case Types::Events::NamedKey::NumLock:
                case Types::Events::NamedKey::ScrollLock:
                case Types::Events::NamedKey::PrintScreen:
                case Types::Events::NamedKey::Pause:
                case Types::Events::NamedKey::Menu:
                    break;
                }
            }

            if (redrawRequired)
            {
                result.status = echo.redraw(line, caret);
                if (!result.status.ok())
                {
                    result.line = std::move(line);
                    return result;
                }
            }
        }
    }
} // namespace GameWIP::Terminal
