/// @file win32_terminal.cpp
/// @brief Win32 backend for the Terminal library.

#include "terminal/internal/terminal_platform.h"
#include "terminal/internal/terminal_test_hooks.h"

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
#include <span>
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
        using InputStream = Terminal::Types::InputStream;
        using OutputStream = Terminal::Types::OutputStream;
        using ReadOutcome = Terminal::Types::ReadOutcome;
        using StreamKind = Terminal::Types::StreamKind;

#if INTERNAL_TERMINAL_TEST_HOOKS
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

        /// @brief Process-lifetime stdin mode snapshot and pending Unicode conversion state.
        struct InputState
        {
            bool defaultConsoleModeCaptured = false;
            HANDLE defaultConsoleHandle = nullptr;
            DWORD defaultConsoleMode = 0;
            PendingInputBuffer pendingBytes;
            wchar_t pendingHighSurrogate = L'\0';
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
            Terminal::Types::ConsumedLineEnding ending = Terminal::Types::ConsumedLineEnding::None;
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

        /// @brief Returns process-lifetime state for the only supported input stream.
        [[nodiscard]] InputState &inputState([[maybe_unused]] InputStream stream) noexcept
        {
            static InputState stdinState;
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

#if INTERNAL_TERMINAL_TEST_HOOKS
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
        void setStyleCapabilities(Terminal::Types::StyleCapabilities &capabilities, bool virtualTerminal) noexcept
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
                outText.__resize_and_overwrite(
                    text.size(),
                    [&text, sourceLength, flags, &wideLength, &conversionError](wchar_t *destination, std::size_t) noexcept
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

        /// @brief Strictly converts UTF-16 console input to UTF-8.
        [[nodiscard]] IO::Types::Status wideToUtf8(std::wstring_view text, std::string &outText)
        {
            outText.clear();
            if (text.empty())
            {
                return IO::successStatus();
            }

            if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded, 0, "Terminal text is too large for Win32 UTF-16 conversion.");
            }

            const int sourceLength = static_cast<int>(text.size());
            const DWORD flags = WC_ERR_INVALID_CHARS;
            const int utf8Length = WideCharToMultiByte(CP_UTF8, flags, text.data(), sourceLength, nullptr, 0, nullptr, nullptr);
            if (utf8Length <= 0)
            {
                const DWORD error = GetLastError();
                return statusFromWin32(ErrorCode::EncodingFailed, error, "Terminal UTF-16 to UTF-8 conversion failed.");
            }

            try
            {
                outText.resize(static_cast<std::size_t>(utf8Length));
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (const std::length_error &)
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }
            if (WideCharToMultiByte(CP_UTF8, flags, text.data(), sourceLength, outText.data(), utf8Length, nullptr, nullptr) != utf8Length)
            {
                const DWORD error = GetLastError();
                return statusFromWin32(ErrorCode::EncodingFailed, error, "Terminal UTF-16 to UTF-8 conversion failed.");
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

        /// @brief Waits for console/pipe input and maps timeout or wait failure to a read outcome.
        [[nodiscard]] ReadChunk waitForInput(HANDLE handle, std::chrono::milliseconds timeout)
        {
            const DWORD waitResult = WaitForSingleObject(handle, waitMilliseconds(timeout));
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

        /// @brief Captures the first observed native console mode for later default restoration.
        [[nodiscard]] IO::Types::Status captureDefaultInputMode(InputStream stream, HANDLE handle)
        {
            InputState &state = inputState(stream);
            if (state.defaultConsoleModeCaptured && state.defaultConsoleHandle == handle)
            {
                return IO::successStatus();
            }

            DWORD mode = 0;
            if (GetConsoleMode(handle, &mode) == FALSE)
            {
                const DWORD error = GetLastError();
                return statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleMode failed for terminal input mode.");
            }

            state.defaultConsoleMode = mode;
            state.defaultConsoleHandle = handle;
            state.defaultConsoleModeCaptured = true;
            return IO::successStatus();
        }

        /// @brief Converts Win32 console-mode flags into the portable input-mode shape.
        [[nodiscard]] Terminal::Types::InputMode inputModeFromConsoleMode(DWORD mode) noexcept
        {
            return {
                .lineBuffered = (mode & ENABLE_LINE_INPUT) != 0,
                .echoInput = (mode & ENABLE_ECHO_INPUT) != 0,
                .processControlKeys = (mode & ENABLE_PROCESSED_INPUT) != 0};
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

        /// @brief Inspects console input records for immediately readable character data.
        [[nodiscard]] Terminal::Types::InputAvailabilityResult consoleInputAvailability(HANDLE handle)
        {
            Terminal::Types::InputAvailabilityResult result;
            result.status = IO::successStatus();

            DWORD eventCount = 0;
            if (GetNumberOfConsoleInputEvents(handle, &eventCount) == FALSE)
            {
                const DWORD error = GetLastError();
                result.status = statusFromWin32(ErrorCode::StatFailed, error, "GetNumberOfConsoleInputEvents failed for terminal input.");
                return result;
            }
            if (eventCount == 0)
            {
                return result;
            }

            std::vector<INPUT_RECORD> records;
            try
            {
                records.resize(eventCount);
            }
            catch (const std::bad_alloc &)
            {
                result.status = IO::makeStatus(ErrorCode::OutOfMemory);
                return result;
            }
            catch (const std::length_error &)
            {
                result.status = IO::makeStatus(ErrorCode::SizeLimitExceeded);
                return result;
            }

            DWORD recordsRead = 0;
            if (PeekConsoleInputW(handle, records.data(), eventCount, &recordsRead) == FALSE)
            {
                const DWORD error = GetLastError();
                result.status = statusFromWin32(ErrorCode::StatFailed, error, "PeekConsoleInputW failed for terminal input.");
                return result;
            }

            DWORD mode = 0;
            if (GetConsoleMode(handle, &mode) == FALSE)
            {
                const DWORD error = GetLastError();
                result.status = statusFromWin32(ErrorCode::StatFailed, error, "GetConsoleMode failed for terminal input availability.");
                return result;
            }

            const bool lineBuffered = (mode & ENABLE_LINE_INPUT) != 0;
            for (DWORD index = 0; index < recordsRead; ++index)
            {
                const INPUT_RECORD &record = records[index];
                if (record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == FALSE)
                {
                    continue;
                }

                const wchar_t character = record.Event.KeyEvent.uChar.UnicodeChar;
                if (character == L'\0')
                {
                    continue;
                }
                if (!lineBuffered || character == L'\r' || character == L'\n')
                {
                    result.available = true;
                    return result;
                }
            }

            return result;
        }

        /// @brief Compares current and end positions for redirected disk input.
        [[nodiscard]] Terminal::Types::InputAvailabilityResult diskInputAvailability(HANDLE handle)
        {
            Terminal::Types::InputAvailabilityResult result;
            result.status = IO::successStatus();

            LARGE_INTEGER zero{};
            LARGE_INTEGER position{};
            LARGE_INTEGER size{};
            if (SetFilePointerEx(handle, zero, &position, FILE_CURRENT) == FALSE || GetFileSizeEx(handle, &size) == FALSE)
            {
                const DWORD error = GetLastError();
                result.status = statusFromWin32(ErrorCode::StatFailed, error, "File position query failed for terminal input availability.");
                return result;
            }

            if (position.QuadPart < 0 || size.QuadPart < position.QuadPart)
            {
                result.status = IO::makeStatus(ErrorCode::InvalidArgument, 0, "Redirected terminal input reported an invalid file position.");
                return result;
            }

            const auto remaining = static_cast<std::uint64_t>(size.QuadPart - position.QuadPart);
            result.available = remaining > 0;
            result.estimatedBytes = remaining;
            return result;
        }

        /// @brief Reads console UTF-16 input and returns one complete UTF-8 chunk.
        [[nodiscard]] ReadChunk readConsoleInputChunk(
            InputStream stream,
            HANDLE handle,
            std::chrono::milliseconds timeout,
            std::size_t requestedBytesHint)
        {
            if (timeout.count() >= 0)
            {
                return {
                    .status = IO::makeStatus(
                        ErrorCode::Unsupported,
                        0,
                        "Finite Win32 console reads are unsupported because cooked console reads cannot be cancelled reliably."),
                    .outcome = ReadOutcome::Completed,
                    .bytes = {}};
            }

            ReadChunk wait = waitForInput(handle, timeout);
            if (!wait.status.ok() || wait.outcome != ReadOutcome::Completed)
            {
                return wait;
            }

            InputState &state = inputState(stream);
            std::array<wchar_t, 512> wideBuffer{};
            std::array<wchar_t, 513> conversionBuffer{};
            const std::size_t requestedCharacters = std::clamp<std::size_t>(requestedBytesHint == 0 ? 2 : requestedBytesHint, 2, wideBuffer.size());

            while (true)
            {
                DWORD charactersRead = 0;
                if (ReadConsoleW(handle, wideBuffer.data(), static_cast<DWORD>(requestedCharacters), &charactersRead, nullptr) == FALSE)
                {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF)
                    {
                        if (state.pendingHighSurrogate != L'\0')
                        {
                            state.pendingHighSurrogate = L'\0';
                            return {
                                .status =
                                    IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal input ended in the middle of a UTF-16 surrogate pair."),
                                .outcome = ReadOutcome::EndOfStream,
                                .bytes = {}};
                        }
                        return {.status = IO::successStatus(), .outcome = ReadOutcome::EndOfStream, .bytes = {}};
                    }

                    return {
                        .status = statusFromWin32(readErrorCode(error), error, "ReadConsoleW failed for terminal input."),
                        .outcome = ReadOutcome::Completed,
                        .bytes = {}};
                }

                if (charactersRead == 0)
                {
                    if (state.pendingHighSurrogate != L'\0')
                    {
                        state.pendingHighSurrogate = L'\0';
                        return {
                            .status = IO::makeStatus(ErrorCode::EncodingFailed, 0, "Terminal input ended in the middle of a UTF-16 surrogate pair."),
                            .outcome = ReadOutcome::EndOfStream,
                            .bytes = {}};
                    }
                    return {.status = IO::successStatus(), .outcome = ReadOutcome::EndOfStream, .bytes = {}};
                }

                std::size_t conversionCharacters = 0;
                if (state.pendingHighSurrogate != L'\0')
                {
                    conversionBuffer[conversionCharacters++] = state.pendingHighSurrogate;
                    state.pendingHighSurrogate = L'\0';
                }
                std::copy_n(wideBuffer.data(), charactersRead, conversionBuffer.data() + conversionCharacters);
                conversionCharacters += charactersRead;

                const wchar_t last = conversionBuffer[conversionCharacters - 1];
                if (last >= 0xd800 && last <= 0xdbff)
                {
                    state.pendingHighSurrogate = last;
                    --conversionCharacters;
                }

                if (conversionCharacters == 0)
                {
                    continue;
                }

                ReadChunk chunk;
                chunk.status = wideToUtf8(std::wstring_view(conversionBuffer.data(), conversionCharacters), chunk.bytes);
                if (!chunk.status.ok())
                {
                    state.pendingHighSurrogate = L'\0';
                }
                return chunk;
            }
        }

        /// @brief Reads bytes from redirected disk/pipe/character input with bounded waiting.
        [[nodiscard]] ReadChunk readFileInputChunk(HANDLE handle, std::chrono::milliseconds timeout, std::size_t requestedBytesHint)
        {
            const DWORD type = fileType(handle);
            if (timeout.count() >= 0 && type == FILE_TYPE_PIPE)
            {
                const auto start = std::chrono::steady_clock::now();
                while (true)
                {
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

                    if (std::chrono::steady_clock::now() - start >= timeout)
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
        [[nodiscard]] ReadChunk readInputChunk(InputStream stream, std::chrono::milliseconds timeout, std::size_t requestedBytesHint)
        {
#if INTERNAL_TERMINAL_TEST_HOOKS
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
                return readConsoleInputChunk(stream, handle, timeout, requestedBytesHint);
            }

            if (timeout.count() >= 0 && fileType(handle) != FILE_TYPE_PIPE)
            {
                return {
                    .status = IO::makeStatus(ErrorCode::Unsupported, 0, "Finite reads are supported only for redirected Win32 pipe input."),
                    .outcome = ReadOutcome::Completed,
                    .bytes = {}};
            }

            return readFileInputChunk(handle, timeout, requestedBytesHint);
        }

        /// @brief Finds the largest valid complete UTF-8 prefix that fits maxBytes.
        [[nodiscard]] Utf8Prefix utf8Prefix(std::string_view bytes, std::size_t maxBytes) noexcept
        {
            Utf8Prefix result;

            while (result.bytes < bytes.size())
            {
                const auto first = static_cast<unsigned char>(bytes[result.bytes]);
                std::size_t length = 0;

                if (first <= 0x7f)
                {
                    length = 1;
                }
                else if (first >= 0xc2 && first <= 0xdf)
                {
                    length = 2;
                }
                else if (first >= 0xe0 && first <= 0xef)
                {
                    length = 3;
                }
                else if (first >= 0xf0 && first <= 0xf4)
                {
                    length = 4;
                }
                else
                {
                    result.valid = false;
                    return result;
                }

                if (result.bytes + length > maxBytes)
                {
                    result.stoppedByMax = true;
                    return result;
                }

                if (result.bytes + length > bytes.size())
                {
                    result.incomplete = true;
                    return result;
                }

                const auto continuation = [](unsigned char byte) noexcept
                {
                    return byte >= 0x80 && byte <= 0xbf;
                };

                if (length >= 2)
                {
                    const auto second = static_cast<unsigned char>(bytes[result.bytes + 1]);
                    if (!continuation(second))
                    {
                        result.valid = false;
                        return result;
                    }

                    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second > 0x9f) || (first == 0xf0 && second < 0x90) ||
                        (first == 0xf4 && second > 0x8f))
                    {
                        result.valid = false;
                        return result;
                    }
                }

                if (length >= 3 && !continuation(static_cast<unsigned char>(bytes[result.bytes + 2])))
                {
                    result.valid = false;
                    return result;
                }

                if (length == 4 && !continuation(static_cast<unsigned char>(bytes[result.bytes + 3])))
                {
                    result.valid = false;
                    return result;
                }

                result.bytes += length;
            }

            return result;
        }

        /// @brief Returns whether the complete byte sequence is valid, non-truncated UTF-8.
        [[nodiscard]] bool validUtf8(std::string_view bytes) noexcept
        {
            const Utf8Prefix prefix = utf8Prefix(bytes, bytes.size());
            return prefix.valid && !prefix.incomplete && prefix.bytes == bytes.size();
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
        [[nodiscard]] LineEndingMatch findLineEnding(std::string_view bytes, std::size_t startOffset, bool allowTrailingCr) noexcept
        {
            for (std::size_t index = std::min(startOffset, bytes.size()); index < bytes.size(); ++index)
            {
                if (bytes[index] == '\n')
                {
                    return {.found = true, .offset = index, .length = 1, .ending = Terminal::Types::ConsumedLineEnding::Lf};
                }

                if (bytes[index] == '\r')
                {
                    if (index + 1 < bytes.size() && bytes[index + 1] == '\n')
                    {
                        return {.found = true, .offset = index, .length = 2, .ending = Terminal::Types::ConsumedLineEnding::CrLf};
                    }

                    if (index + 1 < bytes.size() || allowTrailingCr)
                    {
                        return {.found = true, .offset = index, .length = 1, .ending = Terminal::Types::ConsumedLineEnding::Cr};
                    }
                }
            }

            return {};
        }

        /// @brief Moves a complete UTF-8 prefix to output and leaves unread bytes pending.
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
            Terminal::Types::LineReadResult &result,
            PendingInputBuffer &pendingBytes,
            const LineEndingMatch &ending,
            Terminal::Types::ReadLineEndingMode lineEndingMode,
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
            case Terminal::Types::ReadLineEndingMode::Strip:
                break;
            case Terminal::Types::ReadLineEndingMode::Keep:
                returnedLineEndingBytes = ending.length;
                break;
            case Terminal::Types::ReadLineEndingMode::NormalizeToLf:
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
            case Terminal::Types::ReadLineEndingMode::Strip:
                break;
            case Terminal::Types::ReadLineEndingMode::Keep:
                line.append(pendingBytes.data() + ending.offset, ending.length);
                break;
            case Terminal::Types::ReadLineEndingMode::NormalizeToLf:
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

    Terminal::Types::InputCapabilitiesResult getInputCapabilities(InputStream stream)
    {
        Terminal::Types::InputCapabilitiesResult result;
        result.status = IO::successStatus();

#if INTERNAL_TERMINAL_TEST_HOOKS
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
            result.capabilities.supportsRawInput = true;
            result.capabilities.supportsEchoControl = true;
            result.capabilities.supportsInputMode = true;
            result.capabilities.supportsInputAvailability = true;
            result.capabilities.supportsReadTimeout = false;
            return result;
        }

        const DWORD type = fileType(handle);
        result.capabilities.supportsInputAvailability = type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK;
        result.capabilities.supportsReadTimeout = type == FILE_TYPE_PIPE;
        return result;
    }

    Terminal::Types::OutputCapabilitiesResult getOutputCapabilities(OutputStream stream)
    {
        Terminal::Types::OutputCapabilitiesResult result;
        result.status = IO::successStatus();

#if INTERNAL_TERMINAL_TEST_HOOKS
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

    Terminal::Types::OutputCapabilitiesResult prepareOutput(OutputStream stream)
    {
#if INTERNAL_TERMINAL_TEST_HOOKS
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

        Terminal::Types::OutputCapabilitiesResult result = getOutputCapabilities(stream);
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

    IO::Types::Status validateCursorPosition([[maybe_unused]] OutputStream stream, Terminal::Types::CursorPosition position)
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

    Terminal::Types::InputAvailabilityResult getInputAvailability(InputStream stream)
    {
        Terminal::Types::InputAvailabilityResult result;
        result.status = IO::successStatus();

#if INTERNAL_TERMINAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextInputAvailabilityFailure, "Forced terminal input availability failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::InputHookState &state = HookDetail::terminalTestHookState.inputStreams[HookDetail::inputIndex(stream)];
            if (state.inputBytesOverrideEnabled)
            {
                result.available = !state.inputBytes.empty();
                result.estimatedBytes = state.inputBytes.size();
                return result;
            }
        }
#endif

        InputState &state = inputState(stream);
        if (!state.pendingBytes.empty())
        {
            result.available = true;
            result.estimatedBytes = state.pendingBytes.size();
            return result;
        }

        const HANDLE handle = inputHandle(stream);
        if (!isUsableHandle(handle))
        {
            result.status = IO::makeStatus(ErrorCode::NotOpen, 0, "Terminal input stream is detached.");
            return result;
        }

        if (isConsoleHandle(handle))
        {
            return consoleInputAvailability(handle);
        }

        const DWORD type = fileType(handle);
        if (type == FILE_TYPE_PIPE)
        {
            DWORD availableBytes = 0;
            DWORD error = ERROR_SUCCESS;
            if (!peekPipeBytes(handle, availableBytes, error))
            {
                if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF)
                {
                    result.available = false;
                    return result;
                }

                result.status = statusFromWin32(readErrorCode(error), error, "PeekNamedPipe failed for terminal input availability.");
                return result;
            }

            result.available = availableBytes > 0;
            result.estimatedBytes = availableBytes;
            return result;
        }

        if (type == FILE_TYPE_DISK)
        {
            return diskInputAvailability(handle);
        }

        result.status = IO::makeStatus(ErrorCode::Unsupported, 0, "Input availability is unsupported for this Win32 input stream type.");
        return result;
    }

    Terminal::Types::InputModeResult getInputMode(InputStream stream)
    {
        const InputModeSnapshotResult snapshotResult = captureInputMode(stream);
        return {.status = snapshotResult.status, .mode = snapshotResult.snapshot.mode};
    }

    InputModeSnapshotResult captureInputMode(InputStream stream)
    {
        InputModeSnapshotResult result;

#if INTERNAL_TERMINAL_TEST_HOOKS
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
                result.snapshot.mode = state.currentInputMode;
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

        IO::Types::Status status = captureDefaultInputMode(stream, handle);
        if (!status.ok())
        {
            result.status = status;
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

    IO::Types::Status setInputMode(InputStream stream, const Terminal::Types::InputMode &mode)
    {
        if (mode.echoInput && !mode.lineBuffered)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, "Win32 console echo input requires line-buffered input.");
        }

#if INTERNAL_TERMINAL_TEST_HOOKS
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
                state.currentInputMode = mode;
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

        IO::Types::Status status = captureDefaultInputMode(stream, handle);
        if (!status.ok())
        {
            return status;
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
        setFlag(ENABLE_VIRTUAL_TERMINAL_INPUT, !mode.lineBuffered);

        if (SetConsoleMode(handle, consoleMode) == FALSE)
        {
            const DWORD firstError = GetLastError();
            if (!mode.lineBuffered && (consoleMode & ENABLE_VIRTUAL_TERMINAL_INPUT) != 0)
            {
                const DWORD modeWithoutVirtualTerminalInput = consoleMode & ~static_cast<DWORD>(ENABLE_VIRTUAL_TERMINAL_INPUT);
                if (SetConsoleMode(handle, modeWithoutVirtualTerminalInput) != FALSE)
                {
                    return IO::successStatus();
                }
            }

            return statusFromWin32(ErrorCode::NativeFailure, firstError, "SetConsoleMode failed for terminal input mode.");
        }

        return IO::successStatus();
    }

    IO::Types::Status restoreInputMode(InputStream stream, const InputModeSnapshot &snapshot)
    {
#if INTERNAL_TERMINAL_TEST_HOOKS
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
                state.currentInputMode = snapshot.mode;
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
            return statusFromWin32(ErrorCode::NativeFailure, error, "SetConsoleMode failed while restoring a scoped terminal input mode.");
        }

        return IO::successStatus();
    }

    IO::Types::Status restoreDefaultInputMode(InputStream stream)
    {
#if INTERNAL_TERMINAL_TEST_HOOKS
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
                state.currentInputMode = state.defaultInputMode;
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

        IO::Types::Status status = captureDefaultInputMode(stream, handle);
        if (!status.ok())
        {
            return status;
        }

        if (SetConsoleMode(handle, inputState(stream).defaultConsoleMode) == FALSE)
        {
            const DWORD error = GetLastError();
            return statusFromWin32(ErrorCode::NativeFailure, error, "SetConsoleMode failed while restoring terminal input mode.");
        }

        return IO::successStatus();
    }

    Terminal::Types::TerminalSizeResult getTerminalSize(OutputStream stream)
    {
        Terminal::Types::TerminalSizeResult result;

#if INTERNAL_TERMINAL_TEST_HOOKS
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

    Terminal::Types::CursorPositionResult getCursorPosition(
        OutputStream stream,
        [[maybe_unused]] InputStream responseStream,
        [[maybe_unused]] const Terminal::Types::CursorPositionQueryOptions &options)
    {
        Terminal::Types::CursorPositionResult result;

#if INTERNAL_TERMINAL_TEST_HOOKS
        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextCursorPositionFailure, "Forced terminal cursor position failure."))
        {
            result.status = *failure;
            return result;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
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

    Terminal::Types::ByteReadResult readBytes(InputStream stream, std::span<std::byte> outputBuffer, const Terminal::Types::ByteReadOptions &options)
    {
        Terminal::Types::ByteReadResult result;
        result.status = IO::successStatus();

        if (outputBuffer.empty())
        {
            return result;
        }

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

            const std::chrono::milliseconds timeout = remainingTimeout(start, options.timeout);
            if (options.timeout.count() > 0 && timeout.count() == 0)
            {
                result.outcome = ReadOutcome::TimedOut;
                return result;
            }

            ReadChunk chunk = readInputChunk(stream, timeout, outputBuffer.size() - result.bytesRead);
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

    Terminal::Types::TextReadResult readText(InputStream stream, const Terminal::Types::TextReadOptions &options)
    {
        Terminal::Types::TextReadResult result;
        result.status = IO::successStatus();

        if (options.maxReturnedBytes == 0)
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal text read maxReturnedBytes must be greater than zero.");
            return result;
        }

        const std::size_t maxBytes = clampedMaxBytes(options.maxReturnedBytes);
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

            const std::chrono::milliseconds timeout = remainingTimeout(start, options.timeout);
            if (options.timeout.count() > 0 && timeout.count() == 0)
            {
                result.outcome = ReadOutcome::TimedOut;
                return result;
            }

            const std::size_t requestBytes = maxBytes >= 4096 ? 4096 : std::min<std::size_t>(maxBytes + std::min<std::size_t>(4, maxBytes), 4096);
            ReadChunk chunk = readInputChunk(stream, timeout, requestBytes);
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

    Terminal::Types::LineReadResult readLine(InputStream stream, const Terminal::Types::LineReadOptions &options)
    {
        Terminal::Types::LineReadResult result;
        result.status = IO::successStatus();

        if (options.maxReturnedBytes == 0)
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument, 0, "Terminal line read maxReturnedBytes must be greater than zero.");
            return result;
        }

        const std::size_t maxBytes = clampedMaxBytes(options.maxReturnedBytes);
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

            const std::chrono::milliseconds timeout = remainingTimeout(start, options.timeout);
            if (options.timeout.count() > 0 && timeout.count() == 0)
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

            ReadChunk chunk = readInputChunk(stream, timeout, 4096);
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
#if INTERNAL_TERMINAL_TEST_HOOKS
        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            ++HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)].textWriteCalls;
        }

        if (std::optional<IO::Types::Status> failure =
                consumeHookFailure(HookDetail::terminalTestHookState.nextTextWriteFailure, "Forced terminal text write failure."))
        {
            return *failure;
        }

        {
            std::lock_guard lock(HookDetail::terminalTestHookState.mutex);
            const HookDetail::OutputHookState &state = HookDetail::terminalTestHookState.outputStreams[HookDetail::outputIndex(stream)];
            if (state.captureEnabled)
            {
                appendCapturedOutput(stream, utf8Text);
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
#if INTERNAL_TERMINAL_TEST_HOOKS
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

#if INTERNAL_TERMINAL_TEST_HOOKS
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
} // namespace GameWIP::Terminal::Detail::Platform
