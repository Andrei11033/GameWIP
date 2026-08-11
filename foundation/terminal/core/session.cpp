/// @file session.cpp
/// @brief Managed Terminal input ownership, Session lifecycle, and checked read entry points.

#include "terminal/terminal.h"

#include "terminal/internal/terminal_input.h"
#include "terminal/internal/terminal_platform.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Terminal
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        enum class ReadOperation
        {
            Event,
            Bytes,
            Text,
            Line
        };

        struct InputCoordinator
        {
            std::mutex ownershipMutex;
            std::mutex ioMutex;
            const void *owner = nullptr;
        };

        struct PreparedInputOwnership
        {
            Types::InputCapabilities capabilities{};
            Detail::Platform::InputModeSnapshot previousMode{};
            bool hasPreviousMode = false;
            bool claimed = false;
        };

        struct ReadDecision
        {
            IO::Types::Status status = IO::successStatus();
            bool cancelled = false;
        };

        [[nodiscard]] InputCoordinator &inputCoordinator([[maybe_unused]] Types::InputStream stream) noexcept
        {
            static InputCoordinator stdinCoordinator;
            return stdinCoordinator;
        }

        [[nodiscard]] bool validInputStream(Types::InputStream stream) noexcept
        {
            return stream == Types::InputStream::Stdin;
        }

        [[nodiscard]] bool validOutputStream(Types::OutputStream stream) noexcept
        {
            return stream == Types::OutputStream::Stdout || stream == Types::OutputStream::Stderr;
        }

        [[nodiscard]] bool validDeliveryMode(Types::InputDeliveryMode mode) noexcept
        {
            return mode == Types::InputDeliveryMode::Events || mode == Types::InputDeliveryMode::Stream;
        }

        [[nodiscard]] bool validControlKeyMode(Types::ControlKeyMode mode) noexcept
        {
            return mode == Types::ControlKeyMode::NativeProcessing || mode == Types::ControlKeyMode::ReportAsInput;
        }

        [[nodiscard]] bool validReadLineEndingMode(Types::ReadLineEndingMode mode) noexcept
        {
            return mode == Types::ReadLineEndingMode::Strip || mode == Types::ReadLineEndingMode::Keep ||
                   mode == Types::ReadLineEndingMode::NormalizeToLf;
        }

        [[nodiscard]] IO::Types::Status invalidArgumentStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        [[nodiscard]] IO::Types::Status unsupportedStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::Unsupported);
        }

        [[nodiscard]] IO::Types::Status notOpenStatus() noexcept
        {
            return IO::makeStatus(ErrorCode::NotOpen);
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

        [[nodiscard]] bool operationSupported(const Types::InputCapabilities &capabilities, ReadOperation operation) noexcept
        {
            switch (operation)
            {
            case ReadOperation::Event:
                return capabilities.supportsEventInput;
            case ReadOperation::Bytes:
                return capabilities.supportsByteInput;
            case ReadOperation::Text:
                return capabilities.supportsUtf8Text;
            case ReadOperation::Line:
                return capabilities.supportsLineInput;
            }

            return false;
        }

        [[nodiscard]] bool deliverySupported(const Types::InputCapabilities &capabilities, Types::InputDeliveryMode deliveryMode) noexcept
        {
            if (deliveryMode == Types::InputDeliveryMode::Events)
            {
                return capabilities.supportsEventInput;
            }

            return capabilities.supportsByteInput || capabilities.supportsUtf8Text || capabilities.supportsLineInput;
        }

        [[nodiscard]] IO::Types::Status validateTimeout(const std::optional<std::chrono::milliseconds> &timeout)
        {
            if (timeout.has_value() && timeout->count() < 0)
            {
                return invalidArgumentStatus();
            }

            return IO::successStatus();
        }

        [[nodiscard]] ReadDecision validateReadContract(
            const Types::InputCapabilities &capabilities,
            ReadOperation operation,
            const std::optional<std::chrono::milliseconds> &timeout,
            std::stop_token stopToken)
        {
            ReadDecision decision;
            decision.status = validateTimeout(timeout);
            if (!decision.status.ok())
            {
                return decision;
            }

            if (stopToken.stop_requested())
            {
                decision.cancelled = true;
                return decision;
            }

            if (capabilities.kind == Types::StreamKind::Detached)
            {
                decision.status = notOpenStatus();
                return decision;
            }

            if (!operationSupported(capabilities, operation))
            {
                decision.status = unsupportedStatus();
                return decision;
            }

            if (timeout.has_value())
            {
                if (timeout->count() == 0 && !capabilities.supportsNonBlockingReads)
                {
                    decision.status = unsupportedStatus();
                    return decision;
                }
                if (timeout->count() > 0 && !capabilities.supportsFiniteTimeouts)
                {
                    decision.status = unsupportedStatus();
                    return decision;
                }
            }

            const bool canBlock = !timeout.has_value() || timeout->count() > 0;
            if (canBlock && stopToken.stop_possible() && !capabilities.supportsCancellation)
            {
                decision.status = unsupportedStatus();
            }

            return decision;
        }

        [[nodiscard]] Detail::Platform::InputMode managedMode(
            const Types::SessionOptions &options,
            Detail::Platform::InputMode previousMode,
            const Types::InputCapabilities &capabilities) noexcept
        {
            Detail::Platform::InputMode mode = previousMode;
            mode.processControlKeys = options.controlKeyMode == Types::ControlKeyMode::NativeProcessing;

            // Event delivery, and Stream delivery on event-capable terminals, use one immediate native engine.
            // A backend without structured-event support keeps its existing Stream line discipline until its
            // own event decoder exists.
            if (options.deliveryMode == Types::InputDeliveryMode::Events || capabilities.supportsEventInput)
            {
                mode.lineBuffered = false;
                mode.echoInput = false;
                mode.reportResizeEvents = capabilities.supportsResizeEvents;
                mode.reportPointerEvents = false;
                mode.exclusiveEventDelivery = true;
            }
            return mode;
        }

        void appendRestorationDiagnostic(IO::Types::Status &primary, const IO::Types::Status &restoration) noexcept
        {
            if (primary.ok() || restoration.ok())
            {
                return;
            }

            try
            {
                if (!primary.message.empty())
                {
                    primary.message.append(" ");
                }
                primary.message.append("Terminal input restoration also failed");
                if (!restoration.message.empty())
                {
                    primary.message.append(": ");
                    primary.message.append(restoration.message);
                }
                primary.message.push_back('.');
            }
            catch (...)
            {
                // The primary failure remains authoritative when diagnostic enrichment cannot allocate.
            }
        }

        [[nodiscard]] IO::Types::Status restoreManagedInput(
            Types::InputStream stream,
            PreparedInputOwnership &ownership,
            const void *owner,
            bool retainOwnershipOnFailure) noexcept
        {
            IO::Types::Status restoration = IO::successStatus();

            try
            {
                if (ownership.hasPreviousMode)
                {
                    std::lock_guard lock(Detail::inputIoMutex(stream));
                    restoration = Detail::Platform::restoreInputMode(stream, ownership.previousMode);
                }
            }
            catch (const std::bad_alloc &)
            {
                restoration = IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                restoration = IO::makeStatus(ErrorCode::Unknown);
            }

            if (!restoration.ok() && retainOwnershipOnFailure)
            {
                return restoration;
            }

            if (ownership.claimed)
            {
                Detail::releaseInput(stream, owner);
                ownership.claimed = false;
            }
            if (restoration.ok() || !retainOwnershipOnFailure)
            {
                ownership.hasPreviousMode = false;
            }
            return restoration;
        }

        [[nodiscard]] IO::Types::Status acquireManagedInput(
            const Types::SessionOptions &options,
            const void *owner,
            PreparedInputOwnership &ownership) noexcept
        {
            ownership = {};

            IO::Types::Status status = Detail::claimInput(options.input, owner);
            if (!status.ok())
            {
                return status;
            }
            ownership.claimed = true;

            try
            {
                {
                    std::lock_guard lock(Detail::inputIoMutex(options.input));
                    Types::InputCapabilitiesResult capabilityResult = Detail::Platform::getInputCapabilities(options.input);
                    if (!capabilityResult.status.ok())
                    {
                        status = std::move(capabilityResult.status);
                    }
                    else
                    {
                        ownership.capabilities = capabilityResult.capabilities;
                        if (ownership.capabilities.kind == Types::StreamKind::Detached)
                        {
                            status = notOpenStatus();
                        }
                        else if (!deliverySupported(ownership.capabilities, options.deliveryMode))
                        {
                            status = unsupportedStatus();
                        }
                        else if (ownership.capabilities.kind == Types::StreamKind::Terminal)
                        {
                            Detail::Platform::InputModeSnapshotResult snapshot = Detail::Platform::captureInputMode(options.input);
                            status = snapshot.status;
                            if (status.ok())
                            {
                                ownership.previousMode = snapshot.snapshot;
                                ownership.hasPreviousMode = true;
                                status = Detail::Platform::setInputMode(
                                    options.input,
                                    managedMode(options, ownership.previousMode.mode, ownership.capabilities));
                            }
                        }
                    }
                }
            }
            catch (const std::bad_alloc &)
            {
                status = IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                status = IO::makeStatus(ErrorCode::Unknown);
            }

            if (status.ok())
            {
                return status;
            }

            const IO::Types::Status restoration = restoreManagedInput(options.input, ownership, owner, false);
            appendRestorationDiagnostic(status, restoration);
            return status;
        }

        class DirectInputLease final
        {
        public:
            explicit DirectInputLease(Types::InputStream stream) noexcept
                : options_{.input = stream, .output = Types::OutputStream::Stdout}
            {
            }

            DirectInputLease(const DirectInputLease &) = delete;
            DirectInputLease &operator=(const DirectInputLease &) = delete;

            ~DirectInputLease() noexcept
            {
                static_cast<void>(restoreManagedInput(options_.input, ownership_, this, false));
            }

            [[nodiscard]] IO::Types::Status open(
                Types::InputDeliveryMode deliveryMode,
                Types::ControlKeyMode controlKeyMode = Types::ControlKeyMode::NativeProcessing) noexcept
            {
                options_.deliveryMode = deliveryMode;
                options_.controlKeyMode = controlKeyMode;
                return acquireManagedInput(options_, this, ownership_);
            }

            [[nodiscard]] const Types::InputCapabilities &capabilities() const noexcept
            {
                return ownership_.capabilities;
            }

            [[nodiscard]] IO::Types::Status finish(IO::Types::Status primary) noexcept
            {
                const IO::Types::Status restoration = restoreManagedInput(options_.input, ownership_, this, false);
                if (primary.ok())
                {
                    return restoration;
                }

                appendRestorationDiagnostic(primary, restoration);
                return primary;
            }

        private:
            Types::SessionOptions options_{};
            PreparedInputOwnership ownership_{};
        };

        template <typename Result> [[nodiscard]] Result failedResult(IO::Types::Status status)
        {
            Result result;
            result.status = std::move(status);
            return result;
        }

        template <typename Result> [[nodiscard]] Result cancelledResult()
        {
            Result result;
            result.status = IO::successStatus();
            result.outcome = Types::ReadOutcome::Cancelled;
            return result;
        }

        template <typename Options> [[nodiscard]] ReadDecision validateDirectReadOptions(const Options &options)
        {
            ReadDecision decision;
            decision.status = validateTimeout(options.timeout);
            if (!decision.status.ok())
            {
                return decision;
            }
            if (options.stopToken.stop_requested())
            {
                decision.cancelled = true;
            }
            return decision;
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

        [[nodiscard]] bool hasShortcutModifier(Types::KeyModifier modifiers) noexcept
        {
            return Types::hasModifier(modifiers, Types::KeyModifier::Control) ||
                   Types::hasModifier(modifiers, Types::KeyModifier::Alt) ||
                   Types::hasModifier(modifiers, Types::KeyModifier::Super) ||
                   Types::hasModifier(modifiers, Types::KeyModifier::Hyper) ||
                   Types::hasModifier(modifiers, Types::KeyModifier::Meta);
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

                if (cursor_.seek(byteOffset).outcome != Unicode::Types::BoundaryOutcome::Found)
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

                const Unicode::Types::Utf8BoundaryResult result = cursor_.previous();
                if (result.outcome == Unicode::Types::BoundaryOutcome::AtBeginning)
                {
                    return std::nullopt;
                }
                if (result.outcome != Unicode::Types::BoundaryOutcome::Found)
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

                const Unicode::Types::Utf8BoundaryResult result = cursor_.next();
                if (result.outcome == Unicode::Types::BoundaryOutcome::AtEnd)
                {
                    return std::nullopt;
                }
                if (result.outcome != Unicode::Types::BoundaryOutcome::Found)
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

                const Unicode::Types::Utf8BoundaryResult exact = cursor_.seek(byteOffset);
                if (exact.outcome == Unicode::Types::BoundaryOutcome::Found)
                {
                    return IO::successStatus();
                }

                const Unicode::Types::Utf8BoundaryResult next =
                    Unicode::Utf8::nextGraphemeBoundary(text, byteOffset);
                if (next.outcome == Unicode::Types::BoundaryOutcome::Found)
                {
                    byteOffset = next.byteOffset;
                    static_cast<void>(cursor_.seek(byteOffset));
                    return IO::successStatus();
                }
                if (next.outcome == Unicode::Types::BoundaryOutcome::AtEnd)
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

                Unicode::Types::Utf8GraphemeIndexResult indexed =
                    cursor_.reset(text, std::span<std::size_t>(storage_.data(), storage_.size()));

                if (indexed.outcome == Unicode::Types::GraphemeIndexOutcome::DestinationTooSmall)
                {
                    storage_.resize(indexed.requiredBoundaryCount);
                    indexed = cursor_.reset(text, std::span<std::size_t>(storage_.data(), storage_.size()));
                }

                if (indexed.outcome != Unicode::Types::GraphemeIndexOutcome::Indexed)
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
            LineEcho(Types::InputStream input, Types::OutputStream output, bool enabled) noexcept
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

                const Types::OutputCapabilitiesResult prepared = prepareOutput(output_);
                if (!prepared.status.ok())
                {
                    return prepared.status;
                }
                if (!prepared.capabilities.supportsCursorMovement ||
                    !prepared.capabilities.supportsCursorPositionQuery ||
                    !prepared.capabilities.supportsTerminalSize)
                {
                    return unsupportedStatus();
                }

                const Types::TerminalSizeResult size = getTerminalSize(output_);
                if (!size.status.ok())
                {
                    return size.status;
                }
                size_ = size.size;

                const Types::CursorPositionResult position = queryPosition();
                if (!position.status.ok())
                {
                    return position.status;
                }

                origin_ = position.position;
                active_ = true;
                renderedCells_ = 0;
                renderedCellsKnown_ = true;
                return IO::successStatus();
            }

            void updateSize(Types::TerminalSize size) noexcept
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
                if (status.ok())
                {
                    // We intentionally avoid a cursor query on the normal typing hot path. If a later
                    // edit needs redraw, establish the rendered span once from the then-current cursor.
                    renderedCellsKnown_ = false;
                }
                return status;
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

                const Types::CursorPositionResult current = queryPosition();
                if (!current.status.ok())
                {
                    return current.status;
                }

                Types::CursorPosition previous = current.position;
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

                IO::Types::Status status = setCursorPosition(output_, previous);
                if (!status.ok())
                {
                    return status;
                }
                status = writeText(output_, " ");
                if (!status.ok())
                {
                    return status;
                }
                status = setCursorPosition(output_, previous);
                if (status.ok())
                {
                    renderedCellsKnown_ = false;
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

                if (!renderedCellsKnown_)
                {
                    const Types::CursorPositionResult current = queryPosition();
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
                    renderedCellsKnown_ = true;
                }

                IO::Types::Status status = setCursorPosition(output_, origin_);
                if (!status.ok())
                {
                    return status;
                }

                status = writeText(output_, line);
                if (!status.ok())
                {
                    return status;
                }

                const Types::CursorPositionResult end = queryPosition();
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
                renderedCellsKnown_ = true;

                status = setCursorPosition(output_, origin_);
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
            [[nodiscard]] Types::CursorPositionResult queryPosition() const
            {
                Types::CursorPositionQueryOptions options;
                options.flushMode = IO::Types::FlushMode::None;
                return getCursorPosition(output_, input_, options);
            }

            [[nodiscard]] std::optional<std::size_t> cellDistance(
                Types::CursorPosition begin,
                Types::CursorPosition end) const noexcept
            {
                if (size_.columns == 0 || end.row < begin.row)
                {
                    return std::nullopt;
                }

                const std::uint64_t beginLinear =
                    static_cast<std::uint64_t>(begin.row) * size_.columns + begin.column;
                const std::uint64_t endLinear =
                    static_cast<std::uint64_t>(end.row) * size_.columns + end.column;
                if (endLinear < beginLinear || endLinear - beginLinear > std::numeric_limits<std::size_t>::max())
                {
                    return std::nullopt;
                }
                return static_cast<std::size_t>(endLinear - beginLinear);
            }

            [[nodiscard]] IO::Types::Status writeSpaces(std::size_t count)
            {
                static constexpr std::string_view spaces =
                    "                                                                ";

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

            Types::InputStream input_;
            Types::OutputStream output_;
            Types::TerminalSize size_{};
            Types::CursorPosition origin_{};
            std::size_t renderedCells_ = 0;
            bool enabled_ = false;
            bool active_ = false;
            bool renderedCellsKnown_ = true;
        };

        [[nodiscard]] IO::Types::Status insertScalar(
            std::string &line,
            std::size_t &caret,
            char32_t scalar,
            std::size_t maxBytes,
            bool &wasTruncated,
            GraphemeIndex &graphemes)
        {
            const Unicode::Types::Utf8EncodeResult encoded = Unicode::Utf8::encodeScalar(scalar);
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
                const Unicode::Types::Utf8DecodeResult decoded = Unicode::Utf8::decodeScalar(text.substr(accepted));
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
                line.insert(caret, text.data(), accepted);
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
            Types::LineReadResult &result,
            std::string line,
            const Types::LineReadOptions &options)
        {
            result.consumedLineEnding = Types::ConsumedLineEnding::CrLf;

            std::string_view ending;
            switch (options.lineEndingMode)
            {
            case Types::ReadLineEndingMode::Strip:
                ending = {};
                break;
            case Types::ReadLineEndingMode::Keep:
                ending = "\r\n";
                break;
            case Types::ReadLineEndingMode::NormalizeToLf:
                ending = "\n";
                break;
            }

            const std::size_t maxBytes =
                options.maxReturnedBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
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

        [[nodiscard]] Types::EventReadResult readManagedEvent(
            Types::InputStream input,
            Types::OutputStream output,
            const Types::EventReadOptions &options)
        {
            std::lock_guard lock(Detail::inputIoMutex(input));
            return Detail::Platform::readEvent(input, output, options);
        }

        [[nodiscard]] Types::LineReadResult managedTerminalLineRead(
            Types::InputStream input,
            Types::OutputStream output,
            const Types::LineReadOptions &options,
            std::vector<std::size_t> &graphemeStorage)
        {
            Types::LineReadResult result;
            result.status = IO::successStatus();

            const std::size_t maxBytes =
                options.maxReturnedBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
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
                if (options.timeout.has_value() && options.timeout->count() > 0 &&
                    std::chrono::steady_clock::now() - start >= *options.timeout)
                {
                    result.outcome = Types::ReadOutcome::TimedOut;
                    result.line = std::move(line);
                    return result;
                }

                Types::EventReadOptions eventOptions;
                eventOptions.timeout = remainingReadTimeout(start, options.timeout);
                eventOptions.stopToken = options.stopToken;

                Types::EventReadResult eventResult = readManagedEvent(input, output, eventOptions);
                if (!eventResult.status.ok())
                {
                    result.status = std::move(eventResult.status);
                    result.outcome = eventResult.outcome;
                    result.line = std::move(line);
                    return result;
                }
                if (eventResult.outcome != Types::ReadOutcome::Completed || !eventResult.event.has_value())
                {
                    result.outcome = eventResult.outcome;
                    result.line = std::move(line);
                    return result;
                }

                if (const Types::ResizeEvent *resize = eventResult.event->getIf<Types::ResizeEvent>())
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

                if (const Types::PasteEvent *paste = eventResult.event->getIf<Types::PasteEvent>())
                {
                    const std::size_t oldSize = line.size();
                    const std::size_t oldCaret = caret;
                    result.status = insertPaste(line, caret, paste->text, maxBytes, result.wasTruncated, graphemes);
                    if (!result.status.ok())
                    {
                        result.line = std::move(line);
                        return result;
                    }

                    result.status =
                        oldCaret == oldSize
                            ? echo.append(std::string_view(line).substr(oldSize))
                            : echo.redraw(line, caret);
                    if (!result.status.ok())
                    {
                        result.line = std::move(line);
                        return result;
                    }
                    continue;
                }

                const Types::KeyEvent *keyEvent = eventResult.event->getIf<Types::KeyEvent>();
                if (keyEvent == nullptr || keyEvent->action == Types::KeyAction::Release)
                {
                    continue;
                }

                const std::uint32_t occurrences =
                    keyEvent->action == Types::KeyAction::Repeat ? std::max(keyEvent->repeatCount, 1U) : 1U;

                if (const auto *character = std::get_if<Types::CharacterKey>(&keyEvent->key))
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

                    result.status =
                        oldCaret == oldSize
                            ? echo.append(std::string_view(line).substr(oldSize))
                            : echo.redraw(line, caret);
                    if (!result.status.ok())
                    {
                        result.line = std::move(line);
                        return result;
                    }
                    continue;
                }

                const auto *named = std::get_if<Types::NamedKey>(&keyEvent->key);
                if (named == nullptr)
                {
                    continue;
                }

                if (*named == Types::NamedKey::Enter)
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
                    case Types::NamedKey::Backspace:
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
                        const bool singleCellAscii =
                            suffixDeletion &&
                            caret - *previous == 1 &&
                            static_cast<unsigned char>(line[*previous]) >= 0x20 &&
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
                    case Types::NamedKey::Delete:
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
                    case Types::NamedKey::ArrowLeft:
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
                    case Types::NamedKey::ArrowRight:
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
                    case Types::NamedKey::Home:
                        redrawRequired = redrawRequired || caret != 0;
                        caret = 0;
                        break;
                    case Types::NamedKey::End:
                        redrawRequired = redrawRequired || caret != line.size();
                        caret = line.size();
                        break;
                    case Types::NamedKey::Tab:
                        if (!Types::hasModifier(keyEvent->modifiers, Types::KeyModifier::Shift))
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
                    default:
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
    } // namespace

    namespace Detail
    {
        std::mutex &inputIoMutex(Types::InputStream stream) noexcept
        {
            return inputCoordinator(stream).ioMutex;
        }

        IO::Types::Status claimInput(Types::InputStream stream, const void *owner) noexcept
        {
            if (!validInputStream(stream) || owner == nullptr)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);
                if (coordinator.owner != nullptr)
                {
                    return IO::makeStatus(ErrorCode::ResourceBusy);
                }

                coordinator.owner = owner;
                return IO::successStatus();
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        void releaseInput(Types::InputStream stream, const void *owner) noexcept
        {
            if (!validInputStream(stream) || owner == nullptr)
            {
                return;
            }

            try
            {
                InputCoordinator &coordinator = inputCoordinator(stream);
                std::lock_guard lock(coordinator.ownershipMutex);
                if (coordinator.owner == owner)
                {
                    coordinator.owner = nullptr;
                }
            }
            catch (...)
            {
            }
        }
    } // namespace Detail

    struct Session::State
    {
        enum class PersistentOutputState : std::uint8_t
        {
            CursorHidden,
            AlternateScreen
        };

        // Shared operations may proceed together; open/close/destruction take exclusive lifecycle ownership.
        std::shared_mutex lifecycleMutex;
        std::mutex inputOperationMutex;
        std::mutex persistentOutputMutex;
        std::atomic_bool open = false;
        Types::SessionOptions options{};
        PreparedInputOwnership ownership{};

        // Retain caller-backed Unicode boundary storage across line reads so steady-state editing reuses capacity.
        std::vector<std::size_t> lineBoundaryStorage;

        CursorHiddenScope cursorHiddenScope;
        AlternateScreenScope alternateScreenScope;
        std::array<PersistentOutputState, 2> outputRestoreOrder{};
        std::size_t outputRestoreCount = 0;
    };

    Session::Session() noexcept = default;

    Session::Session(Session &&other) noexcept = default;

    IO::Types::Status Session::restoreOutputState(bool retainOnFailure) noexcept
    {
        if (!state_)
        {
            return IO::successStatus();
        }

        try
        {
            std::lock_guard lock(state_->persistentOutputMutex);
            IO::Types::Status firstFailure = IO::successStatus();

            while (state_->outputRestoreCount > 0)
            {
                const State::PersistentOutputState restore =
                    state_->outputRestoreOrder[state_->outputRestoreCount - 1];

                IO::Types::Status status = IO::successStatus();
                switch (restore)
                {
                case State::PersistentOutputState::CursorHidden:
                    status = state_->cursorHiddenScope.restore();
                    break;
                case State::PersistentOutputState::AlternateScreen:
                    status = state_->alternateScreenScope.leave();
                    break;
                }

                if (!status.ok())
                {
                    if (firstFailure.ok())
                    {
                        firstFailure = status;
                    }
                    if (retainOnFailure)
                    {
                        return status;
                    }
                }

                --state_->outputRestoreCount;
            }

            return firstFailure;
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    Session::~Session() noexcept
    {
        if (!state_)
        {
            return;
        }

        try
        {
            std::unique_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return;
            }

            static_cast<void>(restoreOutputState(false));
            static_cast<void>(restoreManagedInput(state_->options.input, state_->ownership, state_.get(), false));
            state_->open.store(false, std::memory_order_release);
        }
        catch (...)
        {
            static_cast<void>(restoreOutputState(false));
            Detail::releaseInput(state_->options.input, state_.get());
            state_->open.store(false, std::memory_order_release);
        }
    }

    bool Session::isOpen() const noexcept
    {
        return state_ && state_->open.load(std::memory_order_acquire);
    }

    IO::Types::Status Session::open(const Types::SessionOptions &options) noexcept
    {
        if (!validInputStream(options.input) || !validOutputStream(options.output) || !validDeliveryMode(options.deliveryMode) ||
            !validControlKeyMode(options.controlKeyMode))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        try
        {
            if (!state_)
            {
                state_ = std::make_unique<State>();
            }

            std::unique_lock lock(state_->lifecycleMutex);
            if (state_->open.load(std::memory_order_acquire))
            {
                return IO::makeStatus(ErrorCode::AlreadyOpen);
            }

            PreparedInputOwnership ownership;
            IO::Types::Status status = acquireManagedInput(options, state_.get(), ownership);
            if (!status.ok())
            {
                return status;
            }

            state_->options = options;
            state_->ownership = std::move(ownership);
            state_->open.store(true, std::memory_order_release);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status Session::close() noexcept
    {
        if (!state_)
        {
            return IO::successStatus();
        }

        try
        {
            std::unique_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return IO::successStatus();
            }

            IO::Types::Status status = restoreOutputState(true);
            if (!status.ok())
            {
                return status;
            }

            status = restoreManagedInput(state_->options.input, state_->ownership, state_.get(), true);
            if (!status.ok())
            {
                return status;
            }

            state_->open.store(false, std::memory_order_release);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    Types::EventReadResult Session::readEvent(const Types::EventReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::EventReadResult>(notOpenStatus());
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::EventReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Events)
            {
                return failedResult<Types::EventReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Event, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::EventReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::EventReadResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readEvent(state_->options.input, state_->options.output, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::ByteReadResult Session::readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::ByteReadResult>(notOpenStatus());
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::ByteReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Stream)
            {
                return failedResult<Types::ByteReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Bytes, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::ByteReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::ByteReadResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readBytes(state_->options.input, outputBuffer, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::TextReadResult Session::readText(const Types::TextReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::TextReadResult>(notOpenStatus());
        }

        if (options.maxReturnedBytes == 0)
        {
            return failedResult<Types::TextReadResult>(invalidArgumentStatus());
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::TextReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Stream)
            {
                return failedResult<Types::TextReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Text, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::TextReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::TextReadResult>();
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readText(state_->options.input, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::LineReadResult Session::readLine(const Types::LineReadOptions &options) noexcept
    {
        if (!state_)
        {
            return failedResult<Types::LineReadResult>(notOpenStatus());
        }

        if (options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
        {
            return failedResult<Types::LineReadResult>(invalidArgumentStatus());
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return failedResult<Types::LineReadResult>(notOpenStatus());
            }
            if (state_->options.deliveryMode != Types::InputDeliveryMode::Stream)
            {
                return failedResult<Types::LineReadResult>(unsupportedStatus());
            }

            const ReadDecision decision =
                validateReadContract(state_->ownership.capabilities, ReadOperation::Line, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                return failedResult<Types::LineReadResult>(decision.status);
            }
            if (decision.cancelled)
            {
                return cancelledResult<Types::LineReadResult>();
            }

            if (state_->ownership.capabilities.kind == Types::StreamKind::Terminal &&
                state_->ownership.capabilities.supportsEventInput)
            {
                return managedTerminalLineRead(
                    state_->options.input,
                    state_->options.output,
                    options,
                    state_->lineBoundaryStorage);
            }

            std::lock_guard inputLock(Detail::inputIoMutex(state_->options.input));
            return Detail::Platform::readLine(state_->options.input, options);
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::InputCapabilitiesResult Session::getInputCapabilities() const noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }
            return {.status = IO::successStatus(), .capabilities = state_->ownership.capabilities};
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
        }
    }

    Types::OutputCapabilitiesResult Session::getOutputCapabilities() const noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }
            return Terminal::getOutputCapabilities(state_->options.output);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
        }
    }

    Types::OutputCapabilitiesResult Session::prepareOutput() noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .capabilities = {}};
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .capabilities = {}};
            }
            return Terminal::prepareOutput(state_->options.output);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
        }
    }

    Types::TerminalSizeResult Session::getTerminalSize() const noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .size = {}};
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .size = {}};
            }
            return Terminal::getTerminalSize(state_->options.output);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .size = {}};
        }
    }

    IO::Types::Status Session::writeText(std::string_view utf8Text, const Types::TextWriteOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeText(state_->options.output, utf8Text, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::writeLine(std::string_view utf8Text, const Types::LineWriteOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeLine(state_->options.output, utf8Text, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::WriteResult Session::writeBytes(std::span<const std::byte> bytes, const Types::ByteWriteOptions &options) noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .bytesWritten = 0};
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .bytesWritten = 0};
            }
            return Terminal::writeBytes(state_->options.output, bytes, options);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .bytesWritten = 0};
        }
    }

    IO::Types::Status Session::writeSegments(
        std::span<const Types::WriteSegment> segments,
        const Types::SegmentWriteOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::writeSegments(state_->options.output, segments, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::vprint(
        const Types::TextWriteOptions &options,
        std::string_view format,
        std::format_args arguments) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Detail::vprint(state_->options.output, options, format, arguments);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::vprintln(
        const Types::LineWriteOptions &options,
        std::string_view format,
        std::format_args arguments) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Detail::vprintln(state_->options.output, options, format, arguments);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::flush(IO::Types::FlushMode mode) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::flush(state_->options.output, mode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::resetStyle(const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::resetStyle(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::moveCursor(
        Types::CursorMoveDirection direction,
        std::uint32_t amount,
        const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::moveCursor(state_->options.output, direction, amount, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::setCursorPosition(Types::CursorPosition position, const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::setCursorPosition(state_->options.output, position, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    Types::CursorPositionResult Session::getCursorPosition(const Types::CursorPositionQueryOptions &options) noexcept
    {
        if (!state_)
        {
            return {.status = notOpenStatus(), .position = {}};
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            std::lock_guard inputOperationLock(state_->inputOperationMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return {.status = notOpenStatus(), .position = {}};
            }
            return Terminal::getCursorPosition(state_->options.output, state_->options.input, options);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .position = {}};
        }
    }

    IO::Types::Status Session::saveCursorPosition(const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::saveCursorPosition(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::restoreCursorPosition(const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::restoreCursorPosition(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::setCursorVisible(bool visible, const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }

            std::lock_guard persistentLock(state_->persistentOutputMutex);
            if (!visible)
            {
                if (state_->cursorHiddenScope.active())
                {
                    return IO::successStatus();
                }

                CursorHiddenScope scope = scopedCursorHidden(state_->options.output, options);
                if (!scope.status().ok())
                {
                    return scope.status();
                }

                state_->cursorHiddenScope = std::move(scope);
                state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::CursorHidden;
                return IO::successStatus();
            }

            if (!state_->cursorHiddenScope.active())
            {
                return Terminal::setCursorVisible(state_->options.output, true, options);
            }

            IO::Types::Status status = state_->cursorHiddenScope.restore();
            if (!status.ok())
            {
                return status;
            }

            for (std::size_t index = 0; index < state_->outputRestoreCount; ++index)
            {
                if (state_->outputRestoreOrder[index] == State::PersistentOutputState::CursorHidden)
                {
                    for (std::size_t next = index + 1; next < state_->outputRestoreCount; ++next)
                    {
                        state_->outputRestoreOrder[next - 1] = state_->outputRestoreOrder[next];
                    }
                    --state_->outputRestoreCount;
                    break;
                }
            }
            return IO::successStatus();
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::clear(Types::ClearTarget target, const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::clear(state_->options.output, target, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::scroll(
        Types::ScrollDirection direction,
        std::uint32_t lines,
        const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::scroll(state_->options.output, direction, lines, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::enterAlternateScreen(const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }

            std::lock_guard persistentLock(state_->persistentOutputMutex);
            if (state_->alternateScreenScope.active())
            {
                return IO::successStatus();
            }

            AlternateScreenScope scope = scopedAlternateScreen(state_->options.output, options);
            if (!scope.status().ok())
            {
                return scope.status();
            }

            state_->alternateScreenScope = std::move(scope);
            state_->outputRestoreOrder[state_->outputRestoreCount++] = State::PersistentOutputState::AlternateScreen;
            return IO::successStatus();
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::leaveAlternateScreen(const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lifecycleLock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }

            std::lock_guard persistentLock(state_->persistentOutputMutex);
            if (!state_->alternateScreenScope.active())
            {
                return Terminal::leaveAlternateScreen(state_->options.output, options);
            }

            IO::Types::Status status = state_->alternateScreenScope.leave();
            if (!status.ok())
            {
                return status;
            }

            for (std::size_t index = 0; index < state_->outputRestoreCount; ++index)
            {
                if (state_->outputRestoreOrder[index] == State::PersistentOutputState::AlternateScreen)
                {
                    for (std::size_t next = index + 1; next < state_->outputRestoreCount; ++next)
                    {
                        state_->outputRestoreOrder[next - 1] = state_->outputRestoreOrder[next];
                    }
                    --state_->outputRestoreCount;
                    break;
                }
            }
            return IO::successStatus();
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::setTitle(std::string_view utf8Title, const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::setTitle(state_->options.output, utf8Title, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status Session::ringBell(const Types::ControlOptions &options) noexcept
    {
        if (!state_)
        {
            return notOpenStatus();
        }

        try
        {
            std::shared_lock lock(state_->lifecycleMutex);
            if (!state_->open.load(std::memory_order_acquire))
            {
                return notOpenStatus();
            }
            return Terminal::ringBell(state_->options.output, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    Types::EventReadResult readEvent(const Types::EventReadOptions &options) noexcept
    {
        return readEvent(Types::InputStream::Stdin, options);
    }

    Types::EventReadResult readEvent(Types::InputStream stream, const Types::EventReadOptions &options) noexcept
    {
        if (!validInputStream(stream))
        {
            return failedResult<Types::EventReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::EventReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::EventReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Events);
            if (!status.ok())
            {
                return failedResult<Types::EventReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Event, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::EventReadResult>(std::move(status));
            }
            if (decision.cancelled)
            {
                Types::EventReadResult result = cancelledResult<Types::EventReadResult>();
                result.status = lease.finish(std::move(result.status));
                return result;
            }

            Types::EventReadResult result;
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readEvent(stream, Types::OutputStream::Stdout, options);
            }
            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::EventReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::ByteReadResult readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options) noexcept
    {
        return readBytes(Types::InputStream::Stdin, outputBuffer, options);
    }

    Types::ByteReadResult readBytes(Types::InputStream stream, std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options) noexcept
    {
        if (!validInputStream(stream))
        {
            return failedResult<Types::ByteReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::ByteReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::ByteReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Stream);
            if (!status.ok())
            {
                return failedResult<Types::ByteReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Bytes, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::ByteReadResult>(std::move(status));
            }

            Types::ByteReadResult result;
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readBytes(stream, outputBuffer, options);
            }
            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::ByteReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::TextReadResult readText(const Types::TextReadOptions &options) noexcept
    {
        return readText(Types::InputStream::Stdin, options);
    }

    Types::TextReadResult readText(Types::InputStream stream, const Types::TextReadOptions &options) noexcept
    {
        if (!validInputStream(stream) || options.maxReturnedBytes == 0)
        {
            return failedResult<Types::TextReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::TextReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::TextReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Stream);
            if (!status.ok())
            {
                return failedResult<Types::TextReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Text, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::TextReadResult>(std::move(status));
            }

            Types::TextReadResult result;
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readText(stream, options);
            }
            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::TextReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }

    Types::LineReadResult readLine(const Types::LineReadOptions &options) noexcept
    {
        return readLine(Types::InputStream::Stdin, options);
    }

    Types::LineReadResult readLine(Types::InputStream stream, const Types::LineReadOptions &options) noexcept
    {
        if (!validInputStream(stream) || options.maxReturnedBytes == 0 || !validReadLineEndingMode(options.lineEndingMode))
        {
            return failedResult<Types::LineReadResult>(invalidArgumentStatus());
        }

        const ReadDecision initial = validateDirectReadOptions(options);
        if (!initial.status.ok())
        {
            return failedResult<Types::LineReadResult>(initial.status);
        }
        if (initial.cancelled)
        {
            return cancelledResult<Types::LineReadResult>();
        }

        try
        {
            DirectInputLease lease(stream);
            IO::Types::Status status = lease.open(Types::InputDeliveryMode::Stream);
            if (!status.ok())
            {
                return failedResult<Types::LineReadResult>(std::move(status));
            }

            const ReadDecision decision = validateReadContract(lease.capabilities(), ReadOperation::Line, options.timeout, options.stopToken);
            if (!decision.status.ok())
            {
                status = lease.finish(decision.status);
                return failedResult<Types::LineReadResult>(std::move(status));
            }

            Types::LineReadResult result;
            if (lease.capabilities().kind == Types::StreamKind::Terminal && lease.capabilities().supportsEventInput)
            {
                std::vector<std::size_t> graphemeStorage;
                result = managedTerminalLineRead(
                    stream,
                    Types::OutputStream::Stdout,
                    options,
                    graphemeStorage);
            }
            else
            {
                std::lock_guard lock(Detail::inputIoMutex(stream));
                result = Detail::Platform::readLine(stream, options);
            }

            result.status = lease.finish(std::move(result.status));
            return result;
        }
        catch (const std::bad_alloc &)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
        {
            return failedResult<Types::LineReadResult>(IO::makeStatus(ErrorCode::Unknown));
        }
    }
} // namespace GameWIP::Terminal
