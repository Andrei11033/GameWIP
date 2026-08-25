/// @file win32_terminal.cpp
/// @brief Win32 backend for the Terminal library.

#include "terminal/internal/terminal_platform.h"
#include "base/platform/win32/dynamic_library.h"
#include "terminal/internal/terminal_test_hooks.h"
#include "terminal/platform/win32/win32_terminal_events.h"
#include "unicode/unicode.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Terminal::Detail::Platform
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;
        using InputStream = Terminal::Types::Input::Stream;
        using OutputStream = Terminal::Types::Output::Stream;
        using ReadOutcome = Terminal::Types::Input::ReadOutcome;
        using StreamKind = Terminal::Types::StreamKind;

#if TERMINAL_INTERNAL_TEST_HOOKS
        namespace HookDetail = GameWIP::Terminal::Detail::TestHooks;
#endif

        /// @brief Retains unread UTF-8 bytes across Win32 input calls with amortized front consumption.
        class PendingInputBuffer final
        {
        public:
            /// @brief Returns whether no unread bytes remain.
            [[nodiscard]] bool empty() const noexcept
            {
                return size() == 0;
            }

            /// @brief Returns the unread byte count after the consumed prefix.
            [[nodiscard]] std::size_t size() const noexcept
            {
                return storage_.size() - offset_;
            }

            /// @brief Returns the first unread byte pointer.
            [[nodiscard]] const char *data() const noexcept
            {
                return storage_.data() + offset_;
            }

            /// @brief Returns a view over all unread bytes.
            [[nodiscard]] std::string_view view() const noexcept
            {
                return {data(), size()};
            }

            /// @brief Appends bytes, compacting first when retained prefix waste is significant.
            void append(std::string_view bytes)
            {
                if (offset_ > 0 && (offset_ >= kCompactionThreshold || bytes.size() > storage_.capacity() - storage_.size()))
                {
                    compact();
                }
                storage_.append(bytes);
            }

            /// @brief Consumes up to the current unread byte count.
            void consume(std::size_t bytes) noexcept
            {
                if (bytes >= size())
                {
                    clear();
                    return;
                }

                offset_ += bytes;
            }

            /// @brief Removes all bytes and resets the front offset.
            void clear() noexcept
            {
                storage_.clear();
                offset_ = 0;
            }

            /// @brief Transfers unread bytes to an owning string and resets this buffer.
            [[nodiscard]] std::string take()
            {
                if (offset_ == 0)
                {
                    std::string result = std::move(storage_);
                    storage_.clear();
                    return result;
                }

                std::string result(view());
                clear();
                return result;
            }

        private:
            /// @brief Erases the consumed prefix while preserving unread bytes.
            void compact()
            {
                storage_.erase(0, offset_);
                offset_ = 0;
            }

            /// @brief Consumed-prefix size that justifies an eager compaction.
            static constexpr std::size_t kCompactionThreshold = 4096;
            /// @brief Combined consumed prefix and unread suffix storage.
            std::string storage_;
            /// @brief First unread byte in storage_.
            std::size_t offset_ = 0;
        };

        /// @brief Process-lifetime pending Unicode conversion state owned by the observed stdin endpoint.
        /// @details Pending bytes and surrogate state survive individual managed reads so chunk boundaries do not
        /// corrupt UTF-8 conversion. Endpoint replacement discards that endpoint-owned state.
        struct InputState
        {
            static constexpr std::size_t kNativeRecordBatchSize = 32;

            HANDLE endpointHandleValue = nullptr;
            HANDLE endpointIdentity = nullptr;
            HANDLE cancellationEvent = nullptr;
            PendingInputBuffer pendingBytes;
            Win32Events::DecoderState eventDecoder;
            std::array<INPUT_RECORD, kNativeRecordBatchSize> nativeRecords{};
            std::size_t nextNativeRecord = 0;
            std::size_t nativeRecordCount = 0;

            ~InputState() noexcept
            {
                if (endpointIdentity != nullptr && endpointIdentity != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(endpointIdentity);
                }
                if (cancellationEvent != nullptr && cancellationEvent != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(cancellationEvent);
                }
            }
        };

        /// @brief Reusable per-output-stream UTF-16 conversion storage.
        struct OutputConversionState
        {
            std::wstring wideText;
        };

        /// @brief UTF-16 scratch capacity retained after a write before releasing peak storage.
        inline constexpr std::size_t kRetainedConversionLimit = std::size_t{64} * 1024;
        /// @brief Largest control-sequence numeric parameter accepted by the VT backend.
        inline constexpr std::uint32_t kMaxVtParameter = 32767;
        /// @brief Maximum title payload accepted before OSC framing bytes.
        inline constexpr std::size_t kMaxVtTitleBytes = 254;

        /// @brief One backend input transfer before public UTF-8 and line processing.
        struct ReadChunk
        {
            IO::Types::Status status = IO::successStatus();
            ReadOutcome outcome = ReadOutcome::Completed;
            std::string bytes;
        };

        /// @brief Validation result for the largest complete UTF-8 prefix within a byte limit.
        struct Utf8Prefix
        {
            bool valid = true;
            bool incomplete = false;
            bool stoppedByMax = false;
            std::size_t bytes = 0;
        };

        /// @brief Location and kind of one recognized line ending in pending input.
        struct LineEndingMatch
        {
            bool found = false;
            std::size_t offset = 0;
            std::size_t length = 0;
            Terminal::Types::Input::ConsumedLineEnding ending = Terminal::Types::Input::ConsumedLineEnding::None;
        };

        /// @brief Maps a public output stream to its Win32 standard-handle identifier.
        [[nodiscard]] DWORD stdHandleId(OutputStream stream) noexcept
        {
            return stream == OutputStream::Stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
        }

        /// @brief Maps the supported input stream to the Win32 stdin identifier.
        [[nodiscard]] DWORD stdHandleId([[maybe_unused]] InputStream stream) noexcept
        {
            return STD_INPUT_HANDLE;
        }

        /// @brief Resolves the current process handle for stdout or stderr.
        [[nodiscard]] HANDLE outputHandle(OutputStream stream) noexcept
        {
            return GetStdHandle(stdHandleId(stream));
        }

        /// @brief Resolves the current process handle for stdin.
        [[nodiscard]] HANDLE inputHandle(InputStream stream) noexcept
        {
            return GetStdHandle(stdHandleId(stream));
        }

        [[nodiscard]] bool isUsableHandle(HANDLE handle) noexcept;

        /// @brief Resolves CompareObjectHandles without raising the minimum import-library requirement.
        [[nodiscard]] auto compareObjectHandlesFunction() noexcept
        {
            using Function = BOOL(WINAPI *)(HANDLE, HANDLE);
            static const Function function = []() noexcept -> Function
            {
                HMODULE module = GetModuleHandleW(L"kernelbase.dll");
                if (module == nullptr)
                {
                    module = GetModuleHandleW(L"kernel32.dll");
                }
                return GameWIP::Base::Win32::loadProcedure<Function>(module, "CompareObjectHandles");
            }();
            return function;
        }

        /// @brief Returns whether the current standard handle still names the observed native endpoint.
        [[nodiscard]] bool sameInputEndpoint(const InputState &state, HANDLE currentHandle) noexcept
        {
            if (!isUsableHandle(currentHandle))
            {
                return state.endpointIdentity == nullptr && state.endpointHandleValue == currentHandle;
            }
            if (!isUsableHandle(state.endpointIdentity))
            {
                return false;
            }
            if (const auto compare = compareObjectHandlesFunction())
            {
                return compare(state.endpointIdentity, currentHandle) != FALSE;
            }
            return state.endpointHandleValue == currentHandle;
        }

        /// @brief Retains an identity handle so numeric handle reuse is detected on later calls.
        void observeInputEndpoint(InputState &state, HANDLE currentHandle) noexcept
        {
            if (isUsableHandle(state.endpointIdentity))
            {
                CloseHandle(state.endpointIdentity);
            }
            state.endpointIdentity = nullptr;
            state.endpointHandleValue = currentHandle;

            if (isUsableHandle(currentHandle))
            {
                HANDLE duplicate = nullptr;
                if (DuplicateHandle(GetCurrentProcess(), currentHandle, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE)
                {
                    state.endpointIdentity = duplicate;
                }
            }
        }

        /// @brief Returns process-lifetime state for the only supported input stream.
        [[nodiscard]] InputState &inputState([[maybe_unused]] InputStream stream) noexcept
        {
            static InputState stdinState;
            const HANDLE currentHandle = inputHandle(stream);
            if (!sameInputEndpoint(stdinState, currentHandle))
            {
                stdinState.pendingBytes.clear();
                stdinState.eventDecoder.clear();
                stdinState.nextNativeRecord = 0;
                stdinState.nativeRecordCount = 0;
                observeInputEndpoint(stdinState, currentHandle);
            }
            return stdinState;
        }

        /// @brief Returns reusable conversion storage for one output stream.
        [[nodiscard]] OutputConversionState &outputConversionState(OutputStream stream) noexcept
        {
            static OutputConversionState stdoutState;
            static OutputConversionState stderrState;
            return stream == OutputStream::Stderr ? stderrState : stdoutState;
        }

        /// @brief Clears UTF-16 scratch and releases unusually large retained capacity.
        void releaseLargeConversionBuffer(OutputConversionState &state) noexcept
        {
            state.wideText.clear();
            if (state.wideText.capacity() > kRetainedConversionLimit)
            {
                std::wstring{}.swap(state.wideText);
            }
        }

        /// @brief Builds a portable status while preserving one Win32 error code.
        [[nodiscard]] IO::Types::Status statusFromWin32(ErrorCode code, DWORD nativeCode, std::string message = {})
        {
            return IO::makeStatus(code, static_cast<std::int64_t>(nativeCode), std::move(message));
        }

#if TERMINAL_INTERNAL_TEST_HOOKS
        /// @brief Converts a consumed one-shot hook failure into an operation status.
        [[nodiscard]] std::optional<IO::Types::Status> consumeHookFailure(HookDetail::HookFailure &failure, std::string_view message)
        {
            if (const std::optional<ErrorCode> code = HookDetail::consumeFailure(failure))
            {
                return IO::makeStatus(*code, 0, std::string(message));
            }

            return std::nullopt;
        }

        /// @brief Appends raw bytes to hook capture while the hook mutex is held by the caller.
        void appendCapturedOutput(OutputStream stream, std::span<const std::byte> bytes)
        {
            HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            state.capturedOutput.insert(state.capturedOutput.end(), bytes.begin(), bytes.end());
        }

        /// @brief Appends text bytes to hook capture without encoding conversion.
        void appendCapturedOutput(OutputStream stream, std::string_view text)
        {
            appendCapturedOutput(stream, std::as_bytes(std::span<const char>(text.data(), text.size())));
        }

        /// @brief Advances the deterministic ASCII rendering cursor while the hook mutex is held.
        void advanceCursorRenderingSimulation(HookDetail::OutputHookState &state, std::string_view text) noexcept
        {
            if (!state.cursorRenderingSimulationEnabled || state.terminalSizeOverride.columns == 0 || state.terminalSizeOverride.rows == 0)
            {
                return;
            }

            const std::uint32_t columns = state.terminalSizeOverride.columns;
            const std::uint32_t rows = state.terminalSizeOverride.rows;
            const auto keepVisible = [&state, rows]
            {
                if (state.cursorRenderingPosition.row >= state.cursorRenderingViewportOrigin.row + rows)
                {
                    state.cursorRenderingViewportOrigin.row = state.cursorRenderingPosition.row - rows + 1;
                }
            };

            for (const char character : text)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (byte == '\r')
                {
                    state.cursorRenderingPosition.column = 0;
                    continue;
                }
                if (byte == '\n')
                {
                    ++state.cursorRenderingPosition.row;
                    keepVisible();
                    continue;
                }

                // Managed rendering tests deliberately use ASCII fixtures. Treat one complete non-ASCII scalar as
                // one cell so unrelated hook-backed Unicode cases remain deterministic without pretending to define
                // a production terminal-width policy.
                if ((byte & 0xc0U) == 0x80U)
                {
                    continue;
                }

                ++state.cursorRenderingPosition.column;
                if (state.cursorRenderingPosition.column >= columns)
                {
                    state.cursorRenderingPosition.column = 0;
                    ++state.cursorRenderingPosition.row;
                    keepVisible();
                }
            }
        }

        /// @brief Applies deterministic row-major resize reflow while the hook mutex is held.
        void resizeCursorRenderingSimulation(HookDetail::OutputHookState &state, Terminal::Types::Size size) noexcept
        {
            if (!state.cursorRenderingSimulationEnabled || size.columns == 0 || size.rows == 0)
            {
                return;
            }

            const std::uint32_t oldColumns = state.terminalSizeOverride.columns;
            if (oldColumns > 0)
            {
                const std::uint64_t cursorLinear =
                    static_cast<std::uint64_t>(state.cursorRenderingPosition.row) * oldColumns + state.cursorRenderingPosition.column;
                const std::uint64_t viewportLinear =
                    static_cast<std::uint64_t>(state.cursorRenderingViewportOrigin.row) * oldColumns + state.cursorRenderingViewportOrigin.column;
                state.cursorRenderingPosition = {
                    .column = static_cast<std::uint32_t>(cursorLinear % size.columns),
                    .row = static_cast<std::uint32_t>(cursorLinear / size.columns)};
                state.cursorRenderingViewportOrigin = {
                    .column = static_cast<std::uint32_t>(viewportLinear % size.columns),
                    .row = static_cast<std::uint32_t>(viewportLinear / size.columns)};
            }

            state.terminalSizeOverrideEnabled = true;
            state.terminalSizeOverride = size;
            if (state.cursorRenderingPosition.row >= state.cursorRenderingViewportOrigin.row + size.rows)
            {
                state.cursorRenderingViewportOrigin.row = state.cursorRenderingPosition.row - size.rows + 1;
                state.cursorRenderingViewportOrigin.column = 0;
            }
        }

        /// @brief Returns deterministic hook input when enabled, otherwise delegates via nullopt.
        [[nodiscard]] std::optional<ReadChunk> readHookInputChunk(
            InputStream stream,
            std::chrono::milliseconds timeout,
            std::size_t requestedBytesHint)
        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);

            HookDetail::InputHookState &state = HookDetail::terminalTestHookState.inputStreams[HookDetail::inputIndex(stream)];
            if (!state.inputBytesOverrideEnabled)
            {
                return std::nullopt;
            }

            if (state.inputBytes.empty())
            {
                if (state.endOfStreamWhenInputEmpty)
                {
                    return ReadChunk{.status = IO::successStatus(), .outcome = ReadOutcome::EndOfStream, .bytes = {}};
                }

                return ReadChunk{
                    .status = IO::successStatus(),
                    .outcome = timeout.count() > 0 ? ReadOutcome::TimedOut : ReadOutcome::WouldBlock,
                    .bytes = {}};
            }

            const std::size_t requestedBytes = std::max<std::size_t>(requestedBytesHint, 1);
            const std::size_t count = std::min(requestedBytes, state.inputBytes.size());
            std::string bytes(state.inputBytes.data(), count);
            state.inputBytes.erase(0, count);
            return ReadChunk{.status = IO::successStatus(), .outcome = ReadOutcome::Completed, .bytes = std::move(bytes)};
        }
#endif

        /// @brief Maps Win32 write failures to portable IO error categories.
        [[nodiscard]] ErrorCode writeErrorCode(DWORD nativeCode) noexcept
        {
            switch (nativeCode)
            {
            case ERROR_ACCESS_DENIED:
                return ErrorCode::PermissionDenied;
            case ERROR_BROKEN_PIPE:
            case ERROR_NO_DATA:
                return ErrorCode::BrokenPipe;
            case ERROR_DISK_FULL:
            case ERROR_HANDLE_DISK_FULL:
                return ErrorCode::StorageFull;
            case ERROR_INVALID_HANDLE:
                return ErrorCode::NotOpen;
            case ERROR_OPERATION_ABORTED:
                return ErrorCode::Interrupted;
            default:
                return ErrorCode::WriteFailed;
            }
        }

        /// @brief Maps Win32 read failures to portable IO error categories.
        [[nodiscard]] ErrorCode readErrorCode(DWORD nativeCode) noexcept
        {
            switch (nativeCode)
            {
            case ERROR_ACCESS_DENIED:
                return ErrorCode::PermissionDenied;
            case ERROR_BROKEN_PIPE:
            case ERROR_HANDLE_EOF:
                return ErrorCode::EndOfStream;
            case ERROR_INVALID_HANDLE:
                return ErrorCode::NotOpen;
            case ERROR_OPERATION_ABORTED:
                return ErrorCode::Interrupted;
            default:
                return ErrorCode::ReadFailed;
            }
        }

        /// @brief Rejects detached and invalid Win32 standard handles.
        [[nodiscard]] bool isUsableHandle(HANDLE handle) noexcept
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }

        /// @brief Detects a real Win32 console through GetConsoleMode.
        [[nodiscard]] bool isConsoleHandle(HANDLE handle) noexcept
        {
            DWORD mode = 0;
            return isUsableHandle(handle) && GetConsoleMode(handle, &mode) != FALSE;
        }

        /// @brief Queries a usable handle's Win32 file type while preserving GetLastError.
        [[nodiscard]] DWORD fileType(HANDLE handle) noexcept
        {
            if (!isUsableHandle(handle))
            {
                return FILE_TYPE_UNKNOWN;
            }

            SetLastError(ERROR_SUCCESS);
            return GetFileType(handle);
        }

        /// @brief Classifies a standard handle as terminal, redirected, detached, or other.
        [[nodiscard]] StreamKind streamKind(HANDLE handle) noexcept
        {
            if (!isUsableHandle(handle))
            {
                return StreamKind::Detached;
            }

            if (isConsoleHandle(handle))
            {
                return StreamKind::Terminal;
            }

            const DWORD type = fileType(handle);
            if (type == FILE_TYPE_UNKNOWN && GetLastError() != ERROR_SUCCESS)
            {
                return StreamKind::Detached;
            }

            switch (type)
            {
            case FILE_TYPE_DISK:
            case FILE_TYPE_PIPE:
                return StreamKind::Redirected;
            case FILE_TYPE_CHAR:
            case FILE_TYPE_UNKNOWN:
            default:
                return StreamKind::Other;
            }
        }

        /// @brief Returns whether VT output processing is active for a console handle.
        [[nodiscard]] bool outputVirtualTerminalEnabled(HANDLE handle) noexcept
        {
            DWORD mode = 0;
            if (GetConsoleMode(handle, &mode) == FALSE)
            {
                return false;
            }

            return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        }

        /// @brief Populates the exact style subset supported by the current Win32 VT path.
        void setStyleCapabilities(Terminal::Types::Style::Capabilities &capabilities, bool virtualTerminal) noexcept
        {
            capabilities.basicColor = virtualTerminal;
            capabilities.rgbColor = false;
            capabilities.bold = virtualTerminal;
            capabilities.dim = false;
            capabilities.italic = false;
            capabilities.underline = virtualTerminal;
            capabilities.inverse = virtualTerminal;
            capabilities.strikethrough = false;
        }

        /// @brief Strictly converts UTF-8 text to UTF-16 for WriteConsoleW.
        [[nodiscard]] IO::Types::Status utf8ToWide(std::string_view text, std::wstring &outText)
        {
            outText.clear();
            if (text.empty())
            {
                return IO::successStatus();
            }

            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded, 0, "Terminal text is too large for Win32 UTF-8 conversion.");
            }

            const int sourceLength = static_cast<int>(text.size());
            const DWORD flags = MB_ERR_INVALID_CHARS;
            int wideLength = 0;
            DWORD conversionError = ERROR_SUCCESS;

            try
            {
                outText.resize_and_overwrite(
                    text.size(),
                    [&text, sourceLength, &wideLength, &conversionError](wchar_t *destination, std::size_t) noexcept
                    {
                        wideLength = MultiByteToWideChar(CP_UTF8, flags, text.data(), sourceLength, destination, sourceLength);
                        if (wideLength <= 0)
                        {
                            conversionError = GetLastError();
                            return std::size_t{0};
                        }

                        return static_cast<std::size_t>(wideLength);
                    });
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (const std::length_error &)
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }

            if (wideLength <= 0)
            {
                return statusFromWin32(ErrorCode::EncodingFailed, conversionError, "Terminal UTF-8 to UTF-16 conversion failed.");
            }

            return IO::successStatus();
        }

        /// @brief Writes a complete UTF-16 payload to a real console, retrying partial writes.
        [[nodiscard]] IO::Types::Status writeConsoleWide(HANDLE handle, std::wstring_view text)
        {
            const wchar_t *cursor = text.data();
            std::size_t remaining = text.size();

            while (remaining > 0)
            {
                const auto chunkCharacters = static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
                DWORD written = 0;
                if (WriteConsoleW(handle, cursor, chunkCharacters, &written, nullptr) == FALSE)
                {
                    const DWORD error = GetLastError();
                    return statusFromWin32(writeErrorCode(error), error, "WriteConsoleW failed for terminal output.");
                }

                if (written == 0)
                {
                    return IO::makeStatus(ErrorCode::WriteFailed, 0, "WriteConsoleW accepted no characters before completing terminal output.");
                }

                cursor += written;
                remaining -= written;
            }

            return IO::successStatus();
        }

        /// @brief Writes bytes to a redirected handle while preserving partial progress.
        [[nodiscard]] IO::Types::WriteResult writeFileBytes(HANDLE handle, std::span<const std::byte> bytes)
        {
            const std::byte *cursor = bytes.data();
            std::size_t remaining = bytes.size();
            std::size_t totalWritten = 0;

            while (remaining > 0)
            {
                const auto chunkBytes = static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
                DWORD written = 0;
                if (WriteFile(handle, cursor, chunkBytes, &written, nullptr) == FALSE)
                {
                    const DWORD error = GetLastError();
                    return {
                        .status = statusFromWin32(writeErrorCode(error), error, "WriteFile failed for terminal output."),
                        .bytesWritten = totalWritten};
                }

                if (written == 0)
                {
                    return {
                        .status = IO::makeStatus(ErrorCode::PartialWrite, 0, "WriteFile accepted no bytes before completing terminal output."),
                        .bytesWritten = totalWritten};
                }

                cursor += written;
                remaining -= written;
                totalWritten += written;
            }

            return {.status = IO::successStatus(), .bytesWritten = totalWritten};
        }

        /// @brief Writes UTF-8 bytes unchanged to a redirected handle.
        [[nodiscard]] IO::Types::Status writeFileText(HANDLE handle, std::string_view text)
        {
            const IO::Types::WriteResult result = writeFileBytes(handle, std::as_bytes(std::span<const char>(text.data(), text.size())));
            return result.status;
        }

        /// @brief Converts a chrono timeout to a bounded Win32 wait value.
        [[nodiscard]] DWORD waitMilliseconds(std::chrono::milliseconds timeout) noexcept
        {
            if (timeout.count() < 0)
            {
                return INFINITE;
            }

            const auto maxWait = static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<DWORD>::max() - 1);
            if (timeout.count() > maxWait)
            {
                return std::numeric_limits<DWORD>::max() - 1;
            }

            return static_cast<DWORD>(timeout.count());
        }

        /// @brief Computes the non-negative remainder of one total input timeout.
        [[nodiscard]] std::chrono::milliseconds remainingTimeout(
            std::chrono::steady_clock::time_point start,
            std::chrono::milliseconds timeout) noexcept
        {
            if (timeout.count() <= 0)
            {
                return timeout;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (elapsed >= timeout)
            {
                return std::chrono::milliseconds{0};
            }

            return timeout - elapsed;
        }

        /// @brief Converts the public optional deadline to the backend's internal negative-forever representation.
        [[nodiscard]] std::chrono::milliseconds backendTimeout(const std::optional<std::chrono::milliseconds> &timeout) noexcept
        {
            return timeout.value_or(std::chrono::milliseconds{-1});
        }

        struct StopEventSignal
        {
            HANDLE event = nullptr;

            void operator()() const noexcept
            {
                if (event != nullptr && event != INVALID_HANDLE_VALUE)
                {
                    static_cast<void>(SetEvent(event));
                }
            }
        };

        /// @brief Lazily creates one reusable cancellation wake event for the observed stdin endpoint state.
        [[nodiscard]] IO::Types::Status ensureCancellationEvent(InputState &state)
        {
            if (state.cancellationEvent != nullptr && state.cancellationEvent != INVALID_HANDLE_VALUE)
            {
                return IO::successStatus();
            }

            state.cancellationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (state.cancellationEvent == nullptr)
            {
                const DWORD error = GetLastError();
                return statusFromWin32(ErrorCode::NativeFailure, error, "CreateEventW failed for terminal input cancellation.");
            }
            return IO::successStatus();
        }

        /// @brief Waits for console records, caller cancellation, or one total-deadline remainder without polling.
        [[nodiscard]] ReadChunk waitForConsoleRecord(
            InputState &state,
            HANDLE handle,
            std::chrono::milliseconds timeout,
            const std::stop_token &stopToken)
        {
            if (stopToken.stop_requested())
            {
                return {.status = IO::successStatus(), .outcome = ReadOutcome::Cancelled, .bytes = {}};
            }

            DWORD waitResult = WAIT_FAILED;
            if (stopToken.stop_possible())
            {
                IO::Types::Status cancellationStatus = ensureCancellationEvent(state);
                if (!cancellationStatus.ok())
                {
                    return {.status = std::move(cancellationStatus), .outcome = ReadOutcome::Completed, .bytes = {}};
                }

                static_cast<void>(ResetEvent(state.cancellationEvent));
                std::stop_callback<StopEventSignal> callback(stopToken, StopEventSignal{.event = state.cancellationEvent});
                const std::array<HANDLE, 2> handles{handle, state.cancellationEvent};
                waitResult = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, waitMilliseconds(timeout));

                if (waitResult == WAIT_OBJECT_0 + 1)
                {
                    return {.status = IO::successStatus(), .outcome = ReadOutcome::Cancelled, .bytes = {}};
                }
            }
            else
            {
                waitResult = WaitForSingleObject(handle, waitMilliseconds(timeout));
            }

            if (waitResult == WAIT_OBJECT_0)
            {
                return {};
            }
            if (waitResult == WAIT_TIMEOUT)
            {
                return {
                    .status = IO::successStatus(),
                    .outcome = timeout.count() == 0 ? ReadOutcome::WouldBlock : ReadOutcome::TimedOut,
                    .bytes = {}};
            }

            const DWORD error = GetLastError();
            return {
                .status = statusFromWin32(ErrorCode::ReadFailed, error, "Waiting for terminal input failed."),
                .outcome = ReadOutcome::Completed,
                .bytes = {}};
        }

        /// @brief Converts Win32 console-mode flags into Terminal's internal managed input-mode shape.
        [[nodiscard]] InputMode inputModeFromConsoleMode(DWORD mode) noexcept
        {
            return {
                .lineBuffered = (mode & ENABLE_LINE_INPUT) != 0,
                .echoInput = (mode & ENABLE_ECHO_INPUT) != 0,
                .processControlKeys = (mode & ENABLE_PROCESSED_INPUT) != 0,
                .reportResizeEvents = (mode & ENABLE_WINDOW_INPUT) != 0,
                .reportPointerEvents = (mode & ENABLE_MOUSE_INPUT) != 0,
                .exclusiveEventDelivery = (mode & ENABLE_QUICK_EDIT_MODE) == 0};
        }

        [[nodiscard]] ReadChunk fillNativeConsoleRecords(
            InputState &state,
            HANDLE handle,
            std::chrono::milliseconds timeout,
            const std::stop_token &stopToken)
        {
            ReadChunk wait = waitForConsoleRecord(state, handle, timeout, stopToken);
            if (!wait.status.ok() || wait.outcome != ReadOutcome::Completed)
            {
                return wait;
            }

            if (stopToken.stop_requested())
            {
                return {.status = IO::successStatus(), .outcome = ReadOutcome::Cancelled, .bytes = {}};
            }

            DWORD recordsRead = 0;
            if (ReadConsoleInputW(handle, state.nativeRecords.data(), static_cast<DWORD>(state.nativeRecords.size()), &recordsRead) == FALSE)
            {
                const DWORD error = GetLastError();
                return {
                    .status = statusFromWin32(readErrorCode(error), error, "ReadConsoleInputW failed for terminal input."),
                    .outcome = ReadOutcome::Completed,
                    .bytes = {}};
            }

            state.nextNativeRecord = 0;
            state.nativeRecordCount = recordsRead;
            return {};
        }

        [[nodiscard]] Terminal::Types::Input::EventResult readConsoleEvent(
            InputStream stream,
            std::optional<OutputStream> outputStream,
            const Terminal::Types::Input::EventOptions &options)
        {
            Terminal::Types::Input::EventResult result;
            result.status = IO::successStatus();

            InputState &state = inputState(stream);
            if (std::optional<Terminal::Types::Event> pending = Win32Events::takePendingEvent(state.eventDecoder))
            {
                result.event = std::move(*pending);
                return result;
            }

            const std::chrono::milliseconds requestedTimeout = backendTimeout(options.timeout);
            const auto start = std::chrono::steady_clock::now();

            while (true)
            {
                if (std::optional<Terminal::Types::Event> pending = Win32Events::takePendingEvent(state.eventDecoder))
                {
                    result.event = std::move(*pending);
                    return result;
                }

                if (state.nextNativeRecord >= state.nativeRecordCount)
                {
                    const std::chrono::milliseconds timeout = remainingTimeout(start, requestedTimeout);
                    if (requestedTimeout.count() > 0 && timeout.count() == 0)
                    {
                        result.outcome = ReadOutcome::TimedOut;
                        return result;
                    }

                    ReadChunk filled = fillNativeConsoleRecords(state, inputHandle(stream), timeout, options.stopToken);
                    if (!filled.status.ok())
                    {
                        result.status = std::move(filled.status);
                        result.outcome = filled.outcome;
                        return result;
                    }
                    if (filled.outcome != ReadOutcome::Completed)
                    {
                        result.outcome = filled.outcome;
                        return result;
                    }
                    if (state.nativeRecordCount == 0)
                    {
                        continue;
                    }
                }

                const INPUT_RECORD record = state.nativeRecords[state.nextNativeRecord++];
                switch (record.EventType)
                {
                case KEY_EVENT:
                {
                    Win32Events::KeyDecodeResult decoded = Win32Events::decodeKeyRecord(record.Event.KeyEvent, state.eventDecoder);

                    if (!decoded.status.ok() || decoded.disposition == Win32Events::KeyDecodeDisposition::Failed)
                    {
                        result.status = std::move(decoded.status);
                        return result;
                    }
                    if (decoded.disposition == Win32Events::KeyDecodeDisposition::Produced)
                    {
                        result.event = std::move(decoded.event);
                        return result;
                    }
                    break;
                }

                case WINDOW_BUFFER_SIZE_EVENT:
                {
                    // Stream consumers do not expose resize events. Event consumers resolve through the
                    // bound output so ResizeEvent and getTerminalSize() have identical viewport semantics.
                    if (!outputStream.has_value())
                    {
                        break;
                    }

                    const Terminal::Types::SizeResult size = getTerminalSize(*outputStream);
                    if (!size.status.ok())
                    {
                        result.status = size.status;
                        return result;
                    }
                    result.event = Terminal::Types::Event{.data = Terminal::Types::Events::Resize{.size = size.size}};
                    return result;
                }

                case MOUSE_EVENT:
                    // Mouse is intentionally outside the current public Terminal event contract.
                    // Keep this dispatch point so adding portable terminal mouse events later does not
                    // require restructuring the record reader.
                    break;

                case FOCUS_EVENT:
                    if (record.Event.FocusEvent.bSetFocus == FALSE)
                    {
                        state.eventDecoder.clear();
                    }
                    break;

                case MENU_EVENT:
                default:
                    break;
                }
            }
        }

        [[nodiscard]] std::string_view streamBytesForNamedKey(Terminal::Types::Events::NamedKey key) noexcept
        {
            switch (key)
            {
            case Terminal::Types::Events::NamedKey::Backspace:
                return "\b";
            case Terminal::Types::Events::NamedKey::Tab:
                return "\t";
            case Terminal::Types::Events::NamedKey::Enter:
                return "\r\n";
            case Terminal::Types::Events::NamedKey::Escape:
                return "\x1b";
            case Terminal::Types::Events::NamedKey::Insert:
            case Terminal::Types::Events::NamedKey::Delete:
            case Terminal::Types::Events::NamedKey::Home:
            case Terminal::Types::Events::NamedKey::End:
            case Terminal::Types::Events::NamedKey::PageUp:
            case Terminal::Types::Events::NamedKey::PageDown:
            case Terminal::Types::Events::NamedKey::ArrowUp:
            case Terminal::Types::Events::NamedKey::ArrowDown:
            case Terminal::Types::Events::NamedKey::ArrowLeft:
            case Terminal::Types::Events::NamedKey::ArrowRight:
            case Terminal::Types::Events::NamedKey::Begin:
            case Terminal::Types::Events::NamedKey::CapsLock:
            case Terminal::Types::Events::NamedKey::NumLock:
            case Terminal::Types::Events::NamedKey::ScrollLock:
            case Terminal::Types::Events::NamedKey::PrintScreen:
            case Terminal::Types::Events::NamedKey::Pause:
            case Terminal::Types::Events::NamedKey::Menu:
                return {};
            }
            return {};
        }

        [[nodiscard]] IO::Types::Status appendRepeated(std::string &destination, std::string_view bytes, std::uint32_t count)
        {
            if (bytes.empty() || count == 0)
            {
                return IO::successStatus();
            }

            if (bytes.size() > destination.max_size() ||
                static_cast<std::size_t>(count) > (destination.max_size() - destination.size()) / bytes.size())
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }

            destination.reserve(destination.size() + bytes.size() * static_cast<std::size_t>(count));
            for (std::uint32_t index = 0; index < count; ++index)
            {
                destination.append(bytes);
            }
            return IO::successStatus();
        }

        [[nodiscard]] ReadChunk readConsoleEventTextChunk(
            InputStream stream,
            [[maybe_unused]] HANDLE handle,
            std::chrono::milliseconds timeout,
            const std::stop_token &stopToken,
            [[maybe_unused]] std::size_t requestedBytesHint)
        {
            ReadChunk chunk;
            chunk.status = IO::successStatus();

            const auto start = std::chrono::steady_clock::now();
            while (true)
            {
                Terminal::Types::Input::EventOptions eventOptions;
                if (timeout.count() >= 0)
                {
                    const std::chrono::milliseconds remaining = remainingTimeout(start, timeout);
                    if (timeout.count() > 0 && remaining.count() == 0)
                    {
                        chunk.outcome = ReadOutcome::TimedOut;
                        return chunk;
                    }
                    eventOptions.timeout = remaining;
                }
                eventOptions.stopToken = stopToken;

                Terminal::Types::Input::EventResult eventResult = readConsoleEvent(stream, std::nullopt, eventOptions);
                if (!eventResult.status.ok())
                {
                    chunk.status = std::move(eventResult.status);
                    chunk.outcome = eventResult.outcome;
                    return chunk;
                }
                if (eventResult.outcome != ReadOutcome::Completed || !eventResult.event.has_value())
                {
                    chunk.outcome = eventResult.outcome;
                    return chunk;
                }

                const Terminal::Types::Events::Key *keyEvent = eventResult.event->getIf<Terminal::Types::Events::Key>();
                if (keyEvent == nullptr || keyEvent->action == Terminal::Types::Events::KeyAction::Release)
                {
                    continue;
                }

                const std::uint32_t occurrences =
                    keyEvent->action == Terminal::Types::Events::KeyAction::Repeat ? std::max(keyEvent->repeatCount, 1U) : 1U;

                std::array<char, Unicode::Utf8::kMaximumScalarBytes> encodedBytes{};
                std::string_view bytes;

                if (const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&keyEvent->key))
                {
                    if (Terminal::Types::Events::hasModifier(keyEvent->modifiers, Terminal::Types::Events::KeyModifier::Control) &&
                        character->value >= U'a' && character->value <= U'z')
                    {
                        encodedBytes[0] = static_cast<char>(character->value - U'a' + static_cast<char32_t>(1));
                        bytes = std::string_view(encodedBytes.data(), 1);
                    }
                    else if (
                        Terminal::Types::Events::hasModifier(keyEvent->modifiers, Terminal::Types::Events::KeyModifier::Control) &&
                        character->value == U' ')
                    {
                        encodedBytes[0] = '\0';
                        bytes = std::string_view(encodedBytes.data(), 1);
                    }
                    else
                    {
                        const Unicode::Types::Utf8::EncodeResult encoded = Unicode::Utf8::encodeScalar(character->value);
                        if (encoded.outcome != Unicode::Types::EncodeOutcome::Encoded)
                        {
                            chunk.status = IO::makeStatus(ErrorCode::EncodingFailed);
                            return chunk;
                        }
                        std::copy_n(encoded.bytes.data(), encoded.byteCount, encodedBytes.data());
                        bytes = std::string_view(encodedBytes.data(), encoded.byteCount);
                    }
                }
                else if (const auto *named = std::get_if<Terminal::Types::Events::NamedKey>(&keyEvent->key))
                {
                    bytes = streamBytesForNamedKey(*named);
                }

                if (bytes.empty())
                {
                    continue;
                }

                chunk.status = appendRepeated(chunk.bytes, bytes, occurrences);
                return chunk;
            }
        }

        /// @brief Peeks a pipe and separates availability from the native failure code.
        [[nodiscard]] bool peekPipeBytes(HANDLE handle, DWORD &availableBytes, DWORD &error) noexcept
        {
            availableBytes = 0;
            error = ERROR_SUCCESS;

            if (PeekNamedPipe(handle, nullptr, 0, nullptr, &availableBytes, nullptr) != FALSE)
            {
                return true;
            }

            error = GetLastError();
            return false;
        }

        /// @brief Reads bytes from redirected disk/pipe/character input with bounded waiting and cooperative pipe cancellation.
        [[nodiscard]] ReadChunk readFileInputChunk(
            HANDLE handle,
            std::chrono::milliseconds timeout,
            const std::stop_token &stopToken,
            std::size_t requestedBytesHint)
        {
            const DWORD type = fileType(handle);
            if (type == FILE_TYPE_PIPE && (timeout.count() >= 0 || stopToken.stop_possible()))
            {
                const auto start = std::chrono::steady_clock::now();
                while (true)
                {
                    if (stopToken.stop_requested())
                    {
                        return {.status = IO::successStatus(), .outcome = ReadOutcome::Cancelled, .bytes = {}};
                    }

                    DWORD availableBytes = 0;
                    DWORD error = ERROR_SUCCESS;
                    if (!peekPipeBytes(handle, availableBytes, error))
                    {
                        if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF)
                        {
                            return {.status = IO::successStatus(), .outcome = ReadOutcome::EndOfStream, .bytes = {}};
                        }

                        return {
                            .status = statusFromWin32(readErrorCode(error), error, "PeekNamedPipe failed for terminal input."),
                            .outcome = ReadOutcome::Completed,
                            .bytes = {}};
                    }

                    if (availableBytes > 0)
                    {
                        break;
                    }

                    if (timeout.count() == 0)
                    {
                        return {.status = IO::successStatus(), .outcome = ReadOutcome::WouldBlock, .bytes = {}};
                    }

                    if (timeout.count() > 0 && std::chrono::steady_clock::now() - start >= timeout)
                    {
                        return {.status = IO::successStatus(), .outcome = ReadOutcome::TimedOut, .bytes = {}};
                    }

                    Sleep(1);
                }
            }

            const std::size_t requestedBytes = std::clamp<std::size_t>(requestedBytesHint == 0 ? 1 : requestedBytesHint, 1, 4096);
            std::array<char, 4096> byteBuffer{};

            DWORD bytesRead = 0;
            if (ReadFile(handle, byteBuffer.data(), static_cast<DWORD>(requestedBytes), &bytesRead, nullptr) == FALSE)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF)
                {
                    return {.status = IO::successStatus(), .outcome = ReadOutcome::EndOfStream, .bytes = {}};
                }

                return {
                    .status = statusFromWin32(readErrorCode(error), error, "ReadFile failed for terminal input."),
                    .outcome = ReadOutcome::Completed,
                    .bytes = {}};
            }

            if (bytesRead == 0)
            {
                return {.status = IO::successStatus(), .outcome = ReadOutcome::EndOfStream, .bytes = {}};
            }

            ReadChunk chunk;
            try
            {
                chunk.bytes.assign(byteBuffer.data(), bytesRead);
            }
            catch (const std::bad_alloc &)
            {
                chunk.status = IO::makeStatus(ErrorCode::OutOfMemory);
            }
            return chunk;
        }

        /// @brief Routes one input transfer through hooks, console Unicode input, or redirected bytes.
        [[nodiscard]] ReadChunk readInputChunk(
            InputStream stream,
            std::chrono::milliseconds timeout,
            const std::stop_token &stopToken,
            std::size_t requestedBytesHint)
        {
            if (stopToken.stop_requested())
            {
                return {.status = IO::successStatus(), .outcome = ReadOutcome::Cancelled, .bytes = {}};
            }

#if TERMINAL_INTERNAL_TEST_HOOKS
            HookDetail::waitAtBlock(HookDetail::terminalTestHookState.nextReadBlock);

            if (std::optional<IO::Types::Status> failure =
                    consumeHookFailure(HookDetail::terminalTestHookState.nextReadFailure, "Forced terminal read failure."))
            {
                return {.status = *failure, .outcome = ReadOutcome::Completed, .bytes = {}};
            }

            if (std::optional<ReadChunk> hookChunk = readHookInputChunk(stream, timeout, requestedBytesHint))
            {
                return *hookChunk;
            }
#endif

            const HANDLE handle = inputHandle(stream);
            if (!isUsableHandle(handle))
            {
                return {
                    .status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal input stream is detached."),
                    .outcome = ReadOutcome::Completed,
                    .bytes = {}};
            }

            if (isConsoleHandle(handle))
            {
                return readConsoleEventTextChunk(stream, handle, timeout, stopToken, requestedBytesHint);
            }

            if (timeout.count() >= 0 && fileType(handle) != FILE_TYPE_PIPE)
            {
                return {
                    .status = IO::makeStatus(ErrorCode::Unsupported, 0, "Finite reads are supported only for redirected Win32 pipe input."),
                    .outcome = ReadOutcome::Completed,
                    .bytes = {}};
            }

            return readFileInputChunk(handle, timeout, stopToken, requestedBytesHint);
        }

        /// @brief Finds the largest strict UTF-8 scalar prefix that fits maxBytes.
        [[nodiscard]] Utf8Prefix utf8Prefix(std::string_view bytes, std::size_t maxBytes) noexcept
        {
            Utf8Prefix result;

            while (result.bytes < bytes.size())
            {
                const Unicode::Types::Utf8::DecodeResult decoded = Unicode::Utf8::decodeScalar(bytes.substr(result.bytes));

                if (decoded.outcome == Unicode::Types::DecodeOutcome::Incomplete)
                {
                    result.incomplete = true;
                    return result;
                }
                if (decoded.outcome != Unicode::Types::DecodeOutcome::Decoded)
                {
                    result.valid = false;
                    return result;
                }
                if (decoded.bytesConsumed > maxBytes - std::min(maxBytes, result.bytes))
                {
                    result.stoppedByMax = true;
                    return result;
                }

                result.bytes += decoded.bytesConsumed;
            }

            return result;
        }

        /// @brief Returns whether the complete byte sequence is strict, non-truncated UTF-8.
        [[nodiscard]] bool validUtf8(std::string_view bytes) noexcept
        {
            return Unicode::Utf8::validate(bytes).outcome == Unicode::Types::ValidationOutcome::Valid;
        }

        /// @brief Clamps a public 64-bit byte limit to the process addressable size.
        [[nodiscard]] std::size_t clampedMaxBytes(std::uint64_t maxBytes) noexcept
        {
            if (maxBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            {
                return std::numeric_limits<std::size_t>::max();
            }

            return static_cast<std::size_t>(maxBytes);
        }

        /// @brief Finds LF, CRLF, or optionally terminal CR without rescanning an earlier prefix.
        /// @details The caller retains a scan offset so long lines remain linear; one trailing CR is rescanned because a
        /// following backend chunk can turn it into CRLF.
        [[nodiscard]] LineEndingMatch findLineEnding(std::string_view bytes, std::size_t startOffset, bool allowTrailingCr) noexcept
        {
            for (std::size_t index = std::min(startOffset, bytes.size()); index < bytes.size(); ++index)
            {
                if (bytes[index] == '\n')
                {
                    return {.found = true, .offset = index, .length = 1, .ending = Terminal::Types::Input::ConsumedLineEnding::Lf};
                }

                if (bytes[index] == '\r')
                {
                    if (index + 1 < bytes.size() && bytes[index + 1] == '\n')
                    {
                        return {.found = true, .offset = index, .length = 2, .ending = Terminal::Types::Input::ConsumedLineEnding::CrLf};
                    }

                    if (index + 1 < bytes.size() || allowTrailingCr)
                    {
                        return {.found = true, .offset = index, .length = 1, .ending = Terminal::Types::Input::ConsumedLineEnding::Cr};
                    }
                }
            }

            return {};
        }

        /// @brief Moves the largest complete UTF-8 prefix within the limit and leaves unread bytes pending.
        /// @details Returning SizeLimitExceeded when even one complete code point cannot fit prevents callers from ever
        /// receiving a partial UTF-8 encoding.
        [[nodiscard]] IO::Types::Status copyTruncatedUtf8Prefix(std::string &outText, PendingInputBuffer &pendingBytes, std::size_t maxBytes)
        {
            const Utf8Prefix prefix = utf8Prefix(pendingBytes.view(), maxBytes);
            if (!prefix.valid)
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal input is not valid UTF-8.");
            }

            if (prefix.bytes == 0 && prefix.stoppedByMax)
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded, 0, "Terminal read maxReturnedBytes is too small for the next UTF-8 code point.");
            }

            outText.assign(pendingBytes.data(), prefix.bytes);
            pendingBytes.consume(prefix.bytes);
            return IO::successStatus();
        }

        /// @brief Extracts a matched line from pending bytes according to the ending-retention policy.
        void completeLineFromPending(
            Terminal::Types::Input::LineResult &result,
            PendingInputBuffer &pendingBytes,
            const LineEndingMatch &ending,
            Terminal::Types::Input::LineEndingMode lineEndingMode,
            std::size_t maxBytes)
        {
            if (ending.offset > maxBytes)
            {
                result.status = copyTruncatedUtf8Prefix(result.line, pendingBytes, maxBytes);
                result.wasTruncated = result.status.ok();
                return;
            }

            std::string line(pendingBytes.data(), ending.offset);
            std::size_t returnedLineEndingBytes = 0;
            switch (lineEndingMode)
            {
            case Terminal::Types::Input::LineEndingMode::Strip:
                break;
            case Terminal::Types::Input::LineEndingMode::Keep:
                returnedLineEndingBytes = ending.length;
                break;
            case Terminal::Types::Input::LineEndingMode::NormalizeToLf:
                returnedLineEndingBytes = 1;
                break;
            default:
                result.status = IO::makeStatus(ErrorCode::InvalidArgument, 0, "Unknown terminal line-ending read mode.");
                return;
            }

            if (returnedLineEndingBytes > maxBytes - ending.offset)
            {
                if (!validUtf8(line))
                {
                    result.status = IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal line input is not valid UTF-8.");
                    return;
                }

                pendingBytes.consume(ending.offset + ending.length);
                result.status = IO::successStatus();
                result.line = std::move(line);
                result.consumedLineEnding = ending.ending;
                result.wasTruncated = true;
                return;
            }

            switch (lineEndingMode)
            {
            case Terminal::Types::Input::LineEndingMode::Strip:
                break;
            case Terminal::Types::Input::LineEndingMode::Keep:
                line.append(pendingBytes.data() + ending.offset, ending.length);
                break;
            case Terminal::Types::Input::LineEndingMode::NormalizeToLf:
                line.push_back('\n');
                break;
            default:
                result.status = IO::makeStatus(ErrorCode::InvalidArgument, 0, "Unknown terminal line-ending read mode.");
                return;
            }

            if (!validUtf8(line))
            {
                result.status = IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal line input is not valid UTF-8.");
                return;
            }

            pendingBytes.consume(ending.offset + ending.length);
            result.status = IO::successStatus();
            result.line = std::move(line);
            result.consumedLineEnding = ending.ending;
        }
    } // namespace

    std::string_view nativeLineEnding() noexcept
    {
        return "\r\n";
    }

    Terminal::Types::Input::CapabilitiesResult getInputCapabilities(InputStream stream)
    {
        Terminal::Types::Input::CapabilitiesResult result;
        result.status = IO::successStatus();

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextInputCapabilityFailure, "Forced terminal input capability failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::InputHookState &state = HookDetail::terminalTestHookState.inputStreams[HookDetail::inputIndex(stream)];
            if (state.capabilitiesOverrideEnabled)
            {
                result.capabilities = state.capabilitiesOverride;
                return result;
            }
        }
#endif

        const HANDLE handle = inputHandle(stream);
        result.capabilities.kind = streamKind(handle);

        if (result.capabilities.kind == StreamKind::Detached)
        {
            return result;
        }

        result.capabilities.supportsUtf8Text = true;
        result.capabilities.supportsByteInput = true;
        result.capabilities.supportsLineInput = true;

        if (result.capabilities.kind == StreamKind::Terminal)
        {
            result.capabilities.supportsEventInput = true;
            result.capabilities.supportsNonBlockingReads = true;
            result.capabilities.supportsFiniteTimeouts = true;
            result.capabilities.supportsCancellation = true;
            result.capabilities.supportsResizeEvents = true;
            result.capabilities.supportsPasteEvents = false;
            result.capabilities.supportsKeyRepeatEvents = true;
            result.capabilities.supportsKeyReleaseEvents = true;
            result.capabilities.supportsStandaloneModifierEvents = true;
            result.capabilities.supportsMediaKeyEvents = false;
            result.capabilities.supportsKeyLocation = true;
            result.capabilities.supportsModifierState = true;
            return result;
        }

        const DWORD type = fileType(handle);
        if (type == FILE_TYPE_PIPE)
        {
            result.capabilities.supportsNonBlockingReads = true;
            result.capabilities.supportsFiniteTimeouts = true;
            result.capabilities.supportsCancellation = true;
        }
        return result;
    }

    Terminal::Types::Output::CapabilitiesResult getOutputCapabilities(OutputStream stream)
    {
        Terminal::Types::Output::CapabilitiesResult result;
        result.status = IO::successStatus();

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextOutputCapabilityFailure, "Forced terminal output capability failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.capabilitiesOverrideEnabled)
            {
                result.capabilities =
                    state.prepared && state.preparedCapabilitiesOverrideEnabled ? state.preparedCapabilitiesOverride : state.capabilitiesOverride;
                return result;
            }
        }
#endif

        const HANDLE handle = outputHandle(stream);
        result.capabilities.kind = streamKind(handle);

        if (result.capabilities.kind == StreamKind::Detached)
        {
            return result;
        }

        result.capabilities.supportsUtf8Text = true;
        result.capabilities.supportsByteOutput = result.capabilities.kind != StreamKind::Terminal;
        result.capabilities.supportsFlush = true;

        if (result.capabilities.kind == StreamKind::Terminal)
        {
            const bool virtualTerminal = outputVirtualTerminalEnabled(handle);
            setStyleCapabilities(result.capabilities.style, virtualTerminal);

            result.capabilities.supportsTerminalSize = true;
            result.capabilities.supportsCursorMovement = virtualTerminal;
            result.capabilities.supportsCursorPositionQuery = true;
            result.capabilities.supportsCursorSaveRestore = virtualTerminal;
            result.capabilities.supportsCursorVisibility = virtualTerminal;
            result.capabilities.supportsClear = virtualTerminal;
            result.capabilities.supportsScroll = virtualTerminal;
            result.capabilities.supportsAlternateScreen = virtualTerminal;
            result.capabilities.supportsTitle = virtualTerminal;
            result.capabilities.supportsBell = true;
        }

        return result;
    }

    Terminal::Types::Output::CapabilitiesResult prepareOutput(OutputStream stream)
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            ++HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)].preparationCalls;
        }

        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextOutputPreparationFailure, "Forced terminal output preparation failure."))
        {
            return {.status = *failure, .capabilities = {}};
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.capabilitiesOverrideEnabled)
            {
                if (state.capabilitiesOverride.kind == StreamKind::Detached)
                {
                    return {
                        .status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached."),
                        .capabilities = state.capabilitiesOverride};
                }
                state.prepared = true;
                return {
                    .status = IO::successStatus(),
                    .capabilities = state.preparedCapabilitiesOverrideEnabled ? state.preparedCapabilitiesOverride : state.capabilitiesOverride};
            }
        }
#endif

        Terminal::Types::Output::CapabilitiesResult result = getOutputCapabilities(stream);
        if (!result.status.ok())
        {
            return result;
        }
        if (result.capabilities.kind == StreamKind::Detached)
        {
            result.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached.");
            return result;
        }
        if (result.capabilities.kind != StreamKind::Terminal || outputVirtualTerminalEnabled(outputHandle(stream)))
        {
            return result;
        }

        const HANDLE handle = outputHandle(stream);
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode) == FALSE)
        {
            const DWORD error = GetLastError();
            result.status = statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleMode failed while preparing terminal output.");
            return result;
        }
        // Preparation is intentionally persistent for the current standard handle. Terminal does not return a scope
        // that restores this capability because later styling and control calls share the prepared process endpoint.
        if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) == FALSE)
        {
            const DWORD error = GetLastError();
            result.status = statusFromWin32(ErrorCode::NativeFailure, error, "SetConsoleMode failed while preparing terminal output.");
            return result;
        }

        return getOutputCapabilities(stream);
    }

    IO::Types::Status validateCursorMovement([[maybe_unused]] OutputStream stream, std::uint32_t amount)
    {
        if (amount > kMaxVtParameter)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal cursor movement exceeds the Win32 VT parameter limit.");
        }
        return IO::successStatus();
    }

    IO::Types::Status validateCursorPosition([[maybe_unused]] OutputStream stream, Terminal::Types::Cursor::Position position)
    {
        if (position.row >= kMaxVtParameter || position.column >= kMaxVtParameter)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal cursor position exceeds the Win32 VT coordinate limit.");
        }
        return IO::successStatus();
    }

    IO::Types::Status validateScroll([[maybe_unused]] OutputStream stream, std::uint32_t lines)
    {
        if (lines > kMaxVtParameter)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal scroll amount exceeds the Win32 VT parameter limit.");
        }
        return IO::successStatus();
    }

    IO::Types::Status validateTitle([[maybe_unused]] OutputStream stream, std::string_view utf8Title)
    {
        if (utf8Title.size() > kMaxVtTitleBytes)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal title exceeds the Win32 VT title limit.");
        }
        return IO::successStatus();
    }

    InputModeSnapshotResult captureInputMode(InputStream stream)
    {
        InputModeSnapshotResult result;

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextInputModeFailure, "Forced terminal input mode failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::InputHookState &state = HookDetail::terminalTestHookState.inputStreams[HookDetail::inputIndex(stream)];
            if (state.inputModeOverrideEnabled)
            {
                result.status = IO::successStatus();
                result.snapshot.mode = {
                    .lineBuffered = state.lineBuffered,
                    .echoInput = state.echoInput,
                    .processControlKeys = state.processControlKeys,
                    .reportResizeEvents = state.reportResizeEvents,
                    .reportPointerEvents = state.reportPointerEvents,
                    .exclusiveEventDelivery = state.exclusiveEventDelivery,
                };
                return result;
            }
        }
#endif

        const HANDLE handle = inputHandle(stream);

        if (!isUsableHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal input stream is detached.");
            return result;
        }

        if (!isConsoleHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Terminal input mode is available only for real console input streams.");
            return result;
        }

        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode) == FALSE)
        {
            const DWORD error = GetLastError();
            result.status = statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleMode failed for terminal input mode.");
            return result;
        }

        result.status = IO::successStatus();
        result.snapshot.mode = inputModeFromConsoleMode(mode);
        result.snapshot.nativeMode = mode;
        result.snapshot.hasNativeMode = true;
        return result;
    }

    IO::Types::Status setInputMode(InputStream stream, const InputMode &mode)
    {
        if (mode.echoInput && !mode.lineBuffered)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Win32 console echo input requires line-buffered input.");
        }

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextInputModeFailure, "Forced terminal input mode failure."))
        {
            return *failure;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            HookDetail::InputHookState &state = HookDetail::terminalTestHookState.inputStreams[HookDetail::inputIndex(stream)];
            if (state.inputModeOverrideEnabled)
            {
                state.lineBuffered = mode.lineBuffered;
                state.echoInput = mode.echoInput;
                state.processControlKeys = mode.processControlKeys;
                state.reportResizeEvents = mode.reportResizeEvents;
                state.reportPointerEvents = mode.reportPointerEvents;
                state.exclusiveEventDelivery = mode.exclusiveEventDelivery;
                return IO::successStatus();
            }
        }
#endif

        const HANDLE handle = inputHandle(stream);
        if (!isUsableHandle(handle))
        {
            return IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal input stream is detached.");
        }

        if (!isConsoleHandle(handle))
        {
            return IO::makeStatus(ErrorCode::Unsupported, 0, "Terminal input mode is available only for real console input streams.");
        }

        DWORD consoleMode = 0;
        if (GetConsoleMode(handle, &consoleMode) == FALSE)
        {
            const DWORD error = GetLastError();
            return statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleMode failed before setting terminal input mode.");
        }

        const auto setFlag = [&consoleMode](DWORD flag, bool enabled) noexcept
        {
            if (enabled)
            {
                consoleMode |= flag;
            }
            else
            {
                consoleMode &= ~flag;
            }
        };

        setFlag(ENABLE_LINE_INPUT, mode.lineBuffered);
        setFlag(ENABLE_ECHO_INPUT, mode.echoInput);
        setFlag(ENABLE_PROCESSED_INPUT, mode.processControlKeys);
        setFlag(ENABLE_WINDOW_INPUT, mode.reportResizeEvents);
        setFlag(ENABLE_MOUSE_INPUT, mode.reportPointerEvents);

        // The native Win32 backend consumes INPUT_RECORD directly. VT-input translation would destroy
        // release/repeat/location information, so keep it disabled here and reserve it for VT/ConPTY backends.
        setFlag(ENABLE_VIRTUAL_TERMINAL_INPUT, false);

        // Quick Edit intercepts mouse activity and can suspend interactive console applications. The exact
        // original native DWORD is restored on close, so managed sessions may disable it without policy leakage.
        consoleMode |= ENABLE_EXTENDED_FLAGS;
        setFlag(ENABLE_QUICK_EDIT_MODE, !mode.exclusiveEventDelivery);

        if (SetConsoleMode(handle, consoleMode) == FALSE)
        {
            const DWORD error = GetLastError();
            return statusFromWin32(ErrorCode::NativeFailure, error, "SetConsoleMode failed for terminal input mode.");
        }

        return IO::successStatus();
    }

    IO::Types::Status restoreInputMode(InputStream stream, const InputModeSnapshot &snapshot)
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextInputModeFailure, "Forced terminal input mode failure."))
        {
            return *failure;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            HookDetail::InputHookState &state = HookDetail::terminalTestHookState.inputStreams[HookDetail::inputIndex(stream)];
            if (state.inputModeOverrideEnabled)
            {
                state.lineBuffered = snapshot.mode.lineBuffered;
                state.echoInput = snapshot.mode.echoInput;
                state.processControlKeys = snapshot.mode.processControlKeys;
                state.reportResizeEvents = snapshot.mode.reportResizeEvents;
                state.reportPointerEvents = snapshot.mode.reportPointerEvents;
                state.exclusiveEventDelivery = snapshot.mode.exclusiveEventDelivery;
                return IO::successStatus();
            }
        }
#endif

        const HANDLE handle = inputHandle(stream);
        if (!isUsableHandle(handle))
        {
            return IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal input stream is detached.");
        }

        if (!isConsoleHandle(handle))
        {
            return IO::makeStatus(ErrorCode::Unsupported, 0, "Terminal input mode is available only for real console input streams.");
        }

        if (!snapshot.hasNativeMode || snapshot.nativeMode > std::numeric_limits<DWORD>::max())
        {
            return setInputMode(stream, snapshot.mode);
        }

        if (SetConsoleMode(handle, static_cast<DWORD>(snapshot.nativeMode)) == FALSE)
        {
            const DWORD error = GetLastError();
            return statusFromWin32(ErrorCode::NativeFailure, error, "SetConsoleMode failed while restoring managed terminal input state.");
        }

        return IO::successStatus();
    }

    Terminal::Types::SizeResult getTerminalSize(OutputStream stream)
    {
        Terminal::Types::SizeResult result;

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextTerminalSizeFailure, "Forced terminal size failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.terminalSizeOverrideEnabled)
            {
                result.status = IO::successStatus();
                result.size = state.terminalSizeOverride;
                return result;
            }
        }
#endif

        const HANDLE handle = outputHandle(stream);

        if (!isUsableHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached.");
            return result;
        }

        if (!isConsoleHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Terminal size is available only for real console output streams.");
            return result;
        }

        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(handle, &info) == FALSE)
        {
            const DWORD error = GetLastError();
            result.status = statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleScreenBufferInfo failed for terminal size.");
            return result;
        }

        result.status = IO::successStatus();
        result.size.columns = static_cast<std::uint32_t>(info.srWindow.Right - info.srWindow.Left + 1);
        result.size.rows = static_cast<std::uint32_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
        return result;
    }

    Terminal::Types::Cursor::PositionResult getCursorPosition(
        OutputStream stream,
        [[maybe_unused]] InputStream responseStream,
        [[maybe_unused]] const Terminal::Types::Cursor::QueryOptions &options)
    {
        Terminal::Types::Cursor::PositionResult result;

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextCursorPositionFailure, "Forced terminal cursor position failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.cursorRenderingSimulationEnabled)
            {
                result.status = IO::successStatus();
                result.position.column = state.cursorRenderingPosition.column >= state.cursorRenderingViewportOrigin.column
                                             ? state.cursorRenderingPosition.column - state.cursorRenderingViewportOrigin.column
                                             : 0;
                result.position.row = state.cursorRenderingPosition.row >= state.cursorRenderingViewportOrigin.row
                                          ? state.cursorRenderingPosition.row - state.cursorRenderingViewportOrigin.row
                                          : 0;
                return result;
            }
            if (state.cursorPositionOverrideEnabled)
            {
                result.status = IO::successStatus();
                result.position = state.cursorPositionOverride;
                return result;
            }
        }
#endif

        const HANDLE handle = outputHandle(stream);

        if (!isUsableHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached.");
            return result;
        }

        if (!isConsoleHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Cursor position is available only for real console output streams.");
            return result;
        }

        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(handle, &info) == FALSE)
        {
            const DWORD error = GetLastError();
            result.status = statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleScreenBufferInfo failed for cursor position.");
            return result;
        }

        const int column = static_cast<int>(info.dwCursorPosition.X) - static_cast<int>(info.srWindow.Left);
        const int row = static_cast<int>(info.dwCursorPosition.Y) - static_cast<int>(info.srWindow.Top);

        result.status = IO::successStatus();
        result.position.column = static_cast<std::uint32_t>(std::max(column, 0));
        result.position.row = static_cast<std::uint32_t>(std::max(row, 0));
        return result;
    }

    Terminal::Types::Cursor::PositionResult getLineRenderingCursorPosition(OutputStream stream)
    {
        Terminal::Types::Cursor::PositionResult result;

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextCursorPositionFailure, "Forced terminal cursor position failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.cursorRenderingSimulationEnabled)
            {
                result.status = IO::successStatus();
                result.position = state.cursorRenderingPosition;
                return result;
            }
            if (state.cursorPositionOverrideEnabled)
            {
                result.status = IO::successStatus();
                result.position = state.cursorPositionOverride;
                return result;
            }
        }
#endif

        const HANDLE handle = outputHandle(stream);
        if (!isUsableHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached.");
            return result;
        }
        if (!isConsoleHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Managed line rendering requires a real console output stream.");
            return result;
        }

        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(handle, &info) == FALSE)
        {
            const DWORD error = GetLastError();
            result.status = statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleScreenBufferInfo failed for managed line rendering.");
            return result;
        }

        result.status = IO::successStatus();
        result.position.column = static_cast<std::uint32_t>(std::max<SHORT>(info.dwCursorPosition.X, 0));
        result.position.row = static_cast<std::uint32_t>(std::max<SHORT>(info.dwCursorPosition.Y, 0));
        return result;
    }

    IO::Types::Status setLineRenderingCursorPosition(OutputStream stream, Terminal::Types::Cursor::Position position)
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.cursorRenderingSimulationEnabled)
            {
                state.cursorRenderingPosition = position;
                state.cursorRenderingSetHistory.push_back(position);
                if (state.terminalSizeOverride.rows > 0 &&
                    state.cursorRenderingPosition.row >= state.cursorRenderingViewportOrigin.row + state.terminalSizeOverride.rows)
                {
                    state.cursorRenderingViewportOrigin.row = state.cursorRenderingPosition.row - state.terminalSizeOverride.rows + 1;
                    state.cursorRenderingViewportOrigin.column = 0;
                }
                return IO::successStatus();
            }
        }
#endif

        if (position.column > static_cast<std::uint32_t>(std::numeric_limits<SHORT>::max()) ||
            position.row > static_cast<std::uint32_t>(std::numeric_limits<SHORT>::max()))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Managed line rendering cursor position exceeds Win32 limits.");
        }

        const HANDLE handle = outputHandle(stream);
        if (!isUsableHandle(handle))
        {
            return IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached.");
        }
        if (!isConsoleHandle(handle))
        {
            return IO::makeStatus(ErrorCode::Unsupported, 0, "Managed line rendering requires a real console output stream.");
        }

        const COORD nativePosition{static_cast<SHORT>(position.column), static_cast<SHORT>(position.row)};
        if (SetConsoleCursorPosition(handle, nativePosition) == FALSE)
        {
            const DWORD error = GetLastError();
            return statusFromWin32(ErrorCode::NativeFailure, error, "SetConsoleCursorPosition failed for managed line rendering.");
        }
        return IO::successStatus();
    }

    Terminal::Types::Input::EventResult readEvent(InputStream stream, OutputStream outputStream, const Terminal::Types::Input::EventOptions &options)
    {
        Terminal::Types::Input::EventResult result;
        result.status = IO::successStatus();

        if (options.stopToken.stop_requested())
        {
            result.outcome = ReadOutcome::Cancelled;
            return result;
        }

#if TERMINAL_INTERNAL_TEST_HOOKS
        HookDetail::waitAtBlock(HookDetail::terminalTestHookState.nextReadBlock);

        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextReadFailure, "Forced terminal event read failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            HookDetail::InputHookState &state = HookDetail::terminalTestHookState.inputStreams[HookDetail::inputIndex(stream)];
            if (state.inputEventsOverrideEnabled)
            {
                if (state.nextInputEvent < state.inputEvents.size())
                {
                    result.event = state.inputEvents[state.nextInputEvent++];
                    if (const auto *resize = result.event->getIf<Terminal::Types::Events::Resize>())
                    {
                        HookDetail::OutputHookState &outputState =
                            HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(outputStream)];
                        resizeCursorRenderingSimulation(outputState, resize->size);
                    }
                    return result;
                }

                if (state.endOfStreamWhenEventsEmpty)
                {
                    result.outcome = ReadOutcome::EndOfStream;
                }
                else
                {
                    result.outcome = options.timeout.has_value() && options.timeout->count() > 0 ? ReadOutcome::TimedOut : ReadOutcome::WouldBlock;
                }
                return result;
            }
        }
#endif

        const HANDLE handle = inputHandle(stream);
        if (!isUsableHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal input stream is detached.");
            return result;
        }
        if (!isConsoleHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Structured event input requires a real Win32 console input stream.");
            return result;
        }

        return readConsoleEvent(stream, outputStream, options);
    }

    Terminal::Types::Input::ByteResult readBytes(
        InputStream stream,
        std::span<std::byte> outputBuffer,
        const Terminal::Types::Input::ByteOptions &options)
    {
        Terminal::Types::Input::ByteResult result;
        result.status = IO::successStatus();

        if (outputBuffer.empty())
        {
            return result;
        }

        const std::chrono::milliseconds requestedTimeout = backendTimeout(options.timeout);
        InputState &state = inputState(stream);
        const auto start = std::chrono::steady_clock::now();

        while (result.bytesRead < outputBuffer.size())
        {
            if (!state.pendingBytes.empty())
            {
                const std::size_t copied = std::min(outputBuffer.size() - result.bytesRead, state.pendingBytes.size());
                std::memcpy(outputBuffer.data() + result.bytesRead, state.pendingBytes.data(), copied);
                state.pendingBytes.consume(copied);
                result.bytesRead += copied;

                if (options.allowPartial || result.bytesRead == outputBuffer.size())
                {
                    return result;
                }
            }

            const std::chrono::milliseconds timeout = remainingTimeout(start, requestedTimeout);
            if (requestedTimeout.count() > 0 && timeout.count() == 0)
            {
                result.outcome = ReadOutcome::TimedOut;
                return result;
            }

            ReadChunk chunk = readInputChunk(stream, timeout, options.stopToken, outputBuffer.size() - result.bytesRead);
            if (!chunk.status.ok())
            {
                result.status = chunk.status;
                result.outcome = chunk.outcome;
                return result;
            }

            if (chunk.bytes.empty())
            {
                result.outcome = chunk.outcome;
                return result;
            }

            state.pendingBytes.append(chunk.bytes);
        }

        return result;
    }

    Terminal::Types::Input::TextResult readText(InputStream stream, const Terminal::Types::Input::TextOptions &options)
    {
        Terminal::Types::Input::TextResult result;
        result.status = IO::successStatus();

        if (options.maxReturnedBytes == 0)
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal text read maxReturnedBytes must be greater than zero.");
            return result;
        }

        const std::size_t maxBytes = clampedMaxBytes(options.maxReturnedBytes);
        const std::chrono::milliseconds requestedTimeout = backendTimeout(options.timeout);
        InputState &state = inputState(stream);
        const auto start = std::chrono::steady_clock::now();

        while (true)
        {
            if (!state.pendingBytes.empty())
            {
                const Utf8Prefix prefix = utf8Prefix(state.pendingBytes.view(), maxBytes);
                if (!prefix.valid)
                {
                    result.status = IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal input is not valid UTF-8.");
                    return result;
                }

                if (prefix.bytes > 0)
                {
                    result.text.assign(state.pendingBytes.data(), prefix.bytes);
                    state.pendingBytes.consume(prefix.bytes);
                    result.wasTruncated = prefix.stoppedByMax;
                    return result;
                }

                if (prefix.stoppedByMax)
                {
                    result.status = IO::makeStatus(
                        ErrorCode::SizeLimitExceeded,
                        0,
                        "Terminal text read maxReturnedBytes is too small for the next UTF-8 code point.");
                    return result;
                }
            }

            const std::chrono::milliseconds timeout = remainingTimeout(start, requestedTimeout);
            if (requestedTimeout.count() > 0 && timeout.count() == 0)
            {
                result.outcome = ReadOutcome::TimedOut;
                return result;
            }

            const std::size_t requestBytes = maxBytes >= 4096 ? 4096 : std::min<std::size_t>(maxBytes + std::min<std::size_t>(4, maxBytes), 4096);
            ReadChunk chunk = readInputChunk(stream, timeout, options.stopToken, requestBytes);
            if (!chunk.status.ok())
            {
                result.status = chunk.status;
                result.outcome = chunk.outcome;
                return result;
            }

            if (chunk.bytes.empty())
            {
                if (!state.pendingBytes.empty() && chunk.outcome == ReadOutcome::EndOfStream)
                {
                    result.status = IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal input ended in the middle of a UTF-8 code point.");
                    result.outcome = chunk.outcome;
                    return result;
                }

                result.outcome = chunk.outcome;
                return result;
            }

            state.pendingBytes.append(chunk.bytes);
        }
    }

    Terminal::Types::Input::LineResult readLine(InputStream stream, const Terminal::Types::Input::LineOptions &options)
    {
        Terminal::Types::Input::LineResult result;
        result.status = IO::successStatus();

        if (options.maxReturnedBytes == 0)
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal line read maxReturnedBytes must be greater than zero.");
            return result;
        }

        const std::size_t maxBytes = clampedMaxBytes(options.maxReturnedBytes);
        const std::chrono::milliseconds requestedTimeout = backendTimeout(options.timeout);
        InputState &state = inputState(stream);
        const auto start = std::chrono::steady_clock::now();
        std::size_t scanOffset = 0;

        while (true)
        {
            const std::string_view pending = state.pendingBytes.view();
            LineEndingMatch ending = findLineEnding(pending, scanOffset, false);
            if (ending.found)
            {
                completeLineFromPending(result, state.pendingBytes, ending, options.lineEndingMode, maxBytes);
                return result;
            }

            if (state.pendingBytes.size() >= maxBytes)
            {
                result.status = copyTruncatedUtf8Prefix(result.line, state.pendingBytes, maxBytes);
                result.wasTruncated = result.status.ok();
                return result;
            }

            scanOffset = !pending.empty() && pending.back() == '\r' ? pending.size() - 1 : pending.size();

            const std::chrono::milliseconds timeout = remainingTimeout(start, requestedTimeout);
            if (requestedTimeout.count() > 0 && timeout.count() == 0)
            {
                ending = findLineEnding(state.pendingBytes.view(), scanOffset, true);
                if (ending.found)
                {
                    completeLineFromPending(result, state.pendingBytes, ending, options.lineEndingMode, maxBytes);
                    return result;
                }

                if (!state.pendingBytes.empty())
                {
                    if (!validUtf8(state.pendingBytes.view()))
                    {
                        result.status = IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal line input is not valid UTF-8.");
                    }
                    else
                    {
                        result.line = state.pendingBytes.take();
                    }
                }

                result.outcome = ReadOutcome::TimedOut;
                return result;
            }

            ReadChunk chunk = readInputChunk(stream, timeout, options.stopToken, 4096);
            if (!chunk.status.ok())
            {
                result.status = chunk.status;
                result.outcome = chunk.outcome;
                return result;
            }

            if (chunk.bytes.empty())
            {
                ending = findLineEnding(state.pendingBytes.view(), scanOffset, true);
                if (ending.found)
                {
                    completeLineFromPending(result, state.pendingBytes, ending, options.lineEndingMode, maxBytes);
                    return result;
                }

                if (!state.pendingBytes.empty())
                {
                    if (!validUtf8(state.pendingBytes.view()))
                    {
                        result.status = IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal line input is not valid UTF-8.");
                        result.outcome = chunk.outcome;
                        return result;
                    }

                    result.line = state.pendingBytes.take();
                }

                result.outcome = chunk.outcome;
                return result;
            }

            state.pendingBytes.append(chunk.bytes);
        }
    }

    IO::Types::Status writeText(OutputStream stream, std::string_view utf8Text)
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            ++HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)].textWriteCalls;
        }

        HookDetail::waitAtBlock(HookDetail::terminalTestHookState.nextTextWriteBlock);

        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextTextWriteFailure, "Forced terminal text write failure."))
        {
            return *failure;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.captureEnabled || state.cursorRenderingSimulationEnabled)
            {
                if (state.captureEnabled)
                {
                    appendCapturedOutput(stream, utf8Text);
                }
                advanceCursorRenderingSimulation(state, utf8Text);
                return IO::successStatus();
            }
        }
#endif

        const HANDLE handle = outputHandle(stream);
        if (!isUsableHandle(handle))
        {
            return IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached.");
        }

        if (isConsoleHandle(handle))
        {
            OutputConversionState &state = outputConversionState(stream);
            IO::Types::Status status = utf8ToWide(utf8Text, state.wideText);
            if (!status.ok())
            {
                releaseLargeConversionBuffer(state);
                return status;
            }

            status = writeConsoleWide(handle, state.wideText);
            releaseLargeConversionBuffer(state);
            return status;
        }

        return writeFileText(handle, utf8Text);
    }

    IO::Types::WriteResult writeBytes(OutputStream stream, std::span<const std::byte> bytes)
    {
#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextByteWriteFailure, "Forced terminal byte write failure."))
        {
            return {.status = *failure, .bytesWritten = 0};
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.captureEnabled)
            {
                appendCapturedOutput(stream, bytes);
                return {.status = IO::successStatus(), .bytesWritten = bytes.size()};
            }
        }
#endif

        const HANDLE handle = outputHandle(stream);
        if (!isUsableHandle(handle))
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached."), .bytesWritten = 0};
        }

        if (isConsoleHandle(handle))
        {
            return {
                .status = IO::makeStatus(ErrorCode::Unsupported, 0, "Byte output is unsupported for real Windows console streams."),
                .bytesWritten = 0};
        }

        return writeFileBytes(handle, bytes);
    }

    IO::Types::Status flush(OutputStream stream, IO::Types::FlushMode mode)
    {
        if (!IO::isValidFlushMode(mode))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Unknown IO flush mode.");
        }
        if (mode == IO::Types::FlushMode::None)
        {
            return IO::successStatus();
        }

#if TERMINAL_INTERNAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextFlushFailure, "Forced terminal flush failure."))
        {
            return *failure;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.captureEnabled)
            {
                return IO::successStatus();
            }
        }
#endif

        const HANDLE handle = outputHandle(stream);
        if (!isUsableHandle(handle))
        {
            return IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal output stream is detached.");
        }

        if (isConsoleHandle(handle) || fileType(handle) != FILE_TYPE_DISK)
        {
            return IO::successStatus();
        }

        if (FlushFileBuffers(handle) == FALSE)
        {
            const DWORD error = GetLastError();
            return statusFromWin32(
                error == ERROR_ACCESS_DENIED ? ErrorCode::PermissionDenied : ErrorCode::FlushFailed,
                error,
                "FlushFileBuffers failed for redirected terminal output.");
        }

        return IO::successStatus();
    }

#if TERMINAL_INTERNAL_TEST_HOOKS
    namespace TestHooks
    {
        namespace
        {
            Win32Events::DecoderState testDecoderState;
        }

        void resetWin32KeyDecoder() noexcept
        {
            testDecoderState.clear();
        }

        Terminal::TestHooks::Win32KeyDecodeResult decodeWin32KeyRecord(
            bool keyDown,
            std::uint16_t virtualKey,
            char16_t unicodeCharacter,
            std::uint32_t controlState,
            std::uint16_t repeatCount,
            std::uint16_t scanCode) noexcept
        {
            KEY_EVENT_RECORD record{};
            record.bKeyDown = keyDown ? TRUE : FALSE;
            record.wRepeatCount = repeatCount;
            record.wVirtualKeyCode = virtualKey;
            record.wVirtualScanCode = scanCode;
            record.uChar.UnicodeChar = static_cast<wchar_t>(unicodeCharacter);
            record.dwControlKeyState = controlState;

            Win32Events::KeyDecodeResult decoded = Win32Events::decodeKeyRecord(record, testDecoderState);
            return {
                .status = std::move(decoded.status),
                .disposition = static_cast<Terminal::TestHooks::Win32KeyDecodeDisposition>(decoded.disposition),
                .event = std::move(decoded.event)};
        }

        std::optional<Terminal::Types::Event> takePendingWin32KeyEvent() noexcept
        {
            return Win32Events::takePendingEvent(testDecoderState);
        }

        void setPendingHighSurrogate(Terminal::Types::Input::Stream stream, std::uint16_t surrogate) noexcept
        {
            InputState &state = inputState(stream);
            state.eventDecoder.pendingHighSurrogate = static_cast<char16_t>(surrogate);
            state.eventDecoder.pendingHighSurrogateRecord = {};
        }

        bool hasPendingHighSurrogate(Terminal::Types::Input::Stream stream) noexcept
        {
            return inputState(stream).eventDecoder.pendingHighSurrogate != u'\0';
        }
    } // namespace TestHooks
#endif
} // namespace GameWIP::Terminal::Detail::Platform
