/// @file session.h
/// @brief Public managed Terminal session contract.

#pragma once

#include "terminal/input.h"
#include "terminal/output_types.h"
#include "status/result.h"

#include <format>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace GameWIP::Terminal
{
    namespace Types
    {
        /// @brief Configuration retained by one persistent Terminal input session.
        struct SessionOptions
        {
            /// @brief Standard input stream owned by the session.
            Types::Input::Stream input = Types::Input::Stream::Stdin;

            /// @brief Primary output stream bound to the session for later output operations.
            Types::Output::Stream output = Types::Output::Stream::Stdout;

            /// @brief Input representation selected for the complete open session.
            Types::Input::DeliveryMode deliveryMode = Types::Input::DeliveryMode::Events;

            /// @brief Native control-key policy applied while the session owns terminal input.
            Types::Input::ControlKeyMode controlKeyMode = Types::Input::ControlKeyMode::NativeProcessing;
        };
    } // namespace Types

    /// @brief Persistent managed Terminal input owner with one bound primary output stream.
    /// @details A Session is closed by default. open() acquires exclusive Terminal ownership of its input stream,
    /// configures required native state, and retains that state until close() or best-effort destruction. Input-consuming
    /// calls are serialized with each other while bound output remains independently serialized by the shared Terminal
    /// output coordinator. Active-operation leases keep the binding valid without holding a lifecycle mutex across
    /// backend work or user formatter code. close() waits for active operations and restores Session-owned persistent state.
    class TERMINAL_EXPORT Session final
    {
    public:
        /// @name Lifecycle
        /// @{

        /// @brief Creates a closed session without allocating or touching terminal state.
        Session() noexcept;

        /// @brief Session ownership cannot be copied.
        Session(const Session &) = delete;
        /// @brief Session ownership cannot be copy-assigned.
        Session &operator=(const Session &) = delete;

        /// @brief Transfers an existing session, including any open input ownership and output-restoration obligations.
        Session(Session &&other) noexcept;

        /// @brief Move assignment is disabled so replacing an open session cannot hide restoration failure.
        Session &operator=(Session &&other) = delete;

        /// @brief Makes a best-effort non-throwing close and releases any remaining process-wide ownership.
        ~Session() noexcept;

        /// @brief Returns whether this object owns an open Terminal session.
        [[nodiscard]] bool isOpen() const noexcept;

        /// @brief Opens the session and acquires exclusive managed ownership of the selected input stream.
        /// @param options Input/output binding and delivery/control policy retained until close().
        /// @return Success, AlreadyOpen, ResourceBusy, Unsupported, or another checked setup failure.
        [[nodiscard]] GameWIP::Status::Types::Result<void> open(const Types::SessionOptions &options = {}) noexcept;

        /// @brief Restores Session-owned persistent output state in reverse order, then exact native input state.
        /// @return Success when closed. ResourceBusy when called reentrantly from the same Session operation. Restoration
        /// failure leaves the session open and retryable ownership retained.
        [[nodiscard]] GameWIP::Status::Types::Result<void> close() noexcept;

        /// @}

        /// @name Capabilities and geometry
        /// @{

        /// @brief Returns the input capabilities captured when this Session opened.
        [[nodiscard]] Types::Input::CapabilitiesResult getInputCapabilities() const noexcept;

        /// @brief Observes capabilities for the bound primary output stream.
        [[nodiscard]] Types::Output::CapabilitiesResult getOutputCapabilities() const noexcept;

        /// @brief Prepares the bound primary output stream and returns resulting capabilities.
        [[nodiscard]] Types::Output::CapabilitiesResult prepareOutput() noexcept;

        /// @brief Returns terminal dimensions for the bound primary output stream.
        [[nodiscard]] Types::SizeResult getTerminalSize() const noexcept;

        /// @}

        /// @name Input
        /// @{

        /// @brief Reads one structured input event in an Events-delivery session.
        /// @return Status, stopping outcome, and optional event payload.
        [[nodiscard]] Types::Input::EventResult readEvent(const Types::Input::EventOptions &options = {}) noexcept;

        /// @brief Reads bytes into caller storage in a Stream-delivery session.
        /// @param outputBuffer Caller-owned byte destination.
        /// @param options Deadline, cancellation, and partial-read behavior.
        [[nodiscard]] Types::Input::ByteResult readBytes(std::span<std::byte> outputBuffer, const Types::Input::ByteOptions &options = {}) noexcept;

        /// @brief Reads one valid UTF-8 text chunk in a Stream-delivery session.
        [[nodiscard]] Types::Input::TextResult readText(const Types::Input::TextOptions &options = {}) noexcept;

        /// @brief Reads one valid UTF-8 line in a Stream-delivery session.
        [[nodiscard]] Types::Input::LineResult readLine(const Types::Input::LineOptions &options = {}) noexcept;

        /// @}

        /// @name Output
        /// @{

        /// @brief Writes UTF-8 text to the bound primary output stream.
        [[nodiscard]] GameWIP::Status::Types::Result<void> writeText(std::string_view utf8Text, const Types::Output::TextOptions &options = {}) noexcept;

        /// @brief Writes UTF-8 text followed by a line ending to the bound primary output stream.
        [[nodiscard]] GameWIP::Status::Types::Result<void> writeLine(std::string_view utf8Text = {}, const Types::Output::LineOptions &options = {}) noexcept;

        /// @brief Writes arbitrary bytes to the bound primary output stream.
        [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeBytes(std::span<const std::byte> bytes, const Types::Output::ByteOptions &options = {}) noexcept;

        /// @brief Writes one atomic logical batch of text, styled text, and bytes to the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> writeSegments(
            std::span<const Types::Output::Segment> segments,
            const Types::Output::SegmentOptions &options = {}) noexcept;

        /// @brief Formats UTF-8 text and writes it to the bound output.
        template <class... Args> [[nodiscard]] GameWIP::Status::Types::Result<void> print(std::format_string<Args...> format, Args &&...args) noexcept;

        /// @brief Formats UTF-8 text with explicit write options and writes it to the bound output.
        template <class... Args>
        [[nodiscard]] GameWIP::Status::Types::Result<void> print(const Types::Output::TextOptions &options, std::format_string<Args...> format, Args &&...args) noexcept;

        /// @brief Formats UTF-8 text and writes it followed by a line ending to the bound output.
        template <class... Args> [[nodiscard]] GameWIP::Status::Types::Result<void> println(std::format_string<Args...> format, Args &&...args) noexcept;

        /// @brief Formats UTF-8 text with explicit line options and writes it to the bound output.
        template <class... Args>
        [[nodiscard]] GameWIP::Status::Types::Result<void> println(
            const Types::Output::LineOptions &options,
            std::format_string<Args...> format,
            Args &&...args) noexcept;

        /// @brief Flushes the bound primary output stream.
        [[nodiscard]] GameWIP::Status::Types::Result<void> flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept;

        /// @}

        /// @name Terminal controls
        /// @{

        /// @brief Resets text style on the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> resetStyle(const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Moves the cursor on the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> moveCursor(
            Types::Cursor::MoveDirection direction,
            std::uint32_t amount = 1,
            const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Sets the cursor position on the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> setCursorPosition(
            Types::Cursor::Position position,
            const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Queries cursor position using the Session's bound output and owned input.
        /// @note The query is an input-consuming operation on backends that require a terminal response.
        [[nodiscard]] Types::Cursor::PositionResult getCursorPosition(const Types::Cursor::QueryOptions &options = {}) noexcept;

        /// @brief Saves cursor position on the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> saveCursorPosition(const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Restores cursor position on the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> restoreCursorPosition(const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Changes cursor visibility and tracks Session-owned hiding for close-time restoration.
        [[nodiscard]] GameWIP::Status::Types::Result<void> setCursorVisible(bool visible, const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Clears a screen or line region on the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> clear(
            Types::Output::ClearTarget target = Types::Output::ClearTarget::EntireScreen,
            const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Scrolls the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> scroll(
            Types::Output::ScrollDirection direction,
            std::uint32_t lines = 1,
            const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Enters alternate-screen mode and records a close-time leave obligation.
        [[nodiscard]] GameWIP::Status::Types::Result<void> enterAlternateScreen(const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Leaves Session-owned alternate-screen mode, or performs an explicit leave when not Session-owned.
        [[nodiscard]] GameWIP::Status::Types::Result<void> leaveAlternateScreen(const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Sets the terminal title through the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> setTitle(std::string_view utf8Title, const Types::Output::ControlOptions &options = {}) noexcept;

        /// @brief Emits the terminal bell through the bound output.
        [[nodiscard]] GameWIP::Status::Types::Result<void> ringBell(const Types::Output::ControlOptions &options = {}) noexcept;

        /// @}

    private:
        [[nodiscard]] GameWIP::Status::Types::Status restoreOutputState(bool retainOnFailure) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Result<void> vprint(
            const Types::Output::TextOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Result<void> vprintln(
            const Types::Output::LineOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept;

        struct State;
        std::unique_ptr<State> state_;
    };

    template <class... Args> GameWIP::Status::Types::Result<void> Session::print(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return print(Types::Output::TextOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> Session::print(const Types::Output::TextOptions &options, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return vprint(options, format.get(), std::make_format_args(args...));
    }

    template <class... Args> GameWIP::Status::Types::Result<void> Session::println(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return println(Types::Output::LineOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> Session::println(const Types::Output::LineOptions &options, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return vprintln(options, format.get(), std::make_format_args(args...));
    }
} // namespace GameWIP::Terminal
