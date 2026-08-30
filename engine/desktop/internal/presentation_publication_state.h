/// @file presentation_publication_state.h
/// @brief Stable atomic publication of renderer-facing Window presentation state.

#pragma once

#include "desktop/display.h"
#include "desktop/types.h"

#include <atomic>
#include <bit>
#include <cstdint>

namespace GameWIP::Desktop::Detail
{
    /// @brief Lazy, stable storage used after concurrent presentation reads are enabled.
    /// @details The Window owner thread publishes values; renderer threads only read them. Once allocated, this object persists until Window
    /// destruction and is reset rather than replaced across native close/reopen transitions.
    class PresentationPublicationState final
    {
    public:
        PresentationPublicationState() noexcept = default;

        void reset() noexcept
        {
            clientSize_.store(0, std::memory_order_release);
            framebufferSize_.store(0, std::memory_order_release);
            contentScale_.store(packFloats(1.0F, 1.0F), std::memory_order_release);
            dpi_.store(0, std::memory_order_release);
            monitor_.store(0, std::memory_order_release);
            flags_.store(0, std::memory_order_release);
        }

        void publishClientSize(Types::LogicalSize value) noexcept
        {
            clientSize_.store(packUnsigned(value.width, value.height), std::memory_order_release);
        }

        void publishFramebufferSize(Types::PixelSize value) noexcept
        {
            framebufferSize_.store(packUnsigned(value.width, value.height), std::memory_order_release);
        }

        void publishContentScale(Types::ContentScale value) noexcept
        {
            contentScale_.store(packFloats(value.x, value.y), std::memory_order_release);
        }

        void publishDpi(Types::Dpi value) noexcept
        {
            dpi_.store(packFloats(value.x, value.y), std::memory_order_release);
        }

        void publishMonitor(Types::Display::MonitorId value) noexcept
        {
            monitor_.store(value.value, std::memory_order_release);
        }

        void publishPresentationState(Types::PresentationState value) noexcept
        {
            updateFlags(kPresentationMask, static_cast<std::uint32_t>(value));
        }

        void publishVisible(bool value) noexcept
        {
            updateFlag(kVisible, value);
        }

        void publishInteractiveMoveResizeActive(bool value) noexcept
        {
            updateFlag(kInteractiveMoveResizeActive, value);
        }

        void publishOccluded(bool value) noexcept
        {
            updateFlag(kOccluded, value);
        }

        [[nodiscard]] Types::LogicalSize clientSize() const noexcept
        {
            const std::uint64_t value = clientSize_.load(std::memory_order_acquire);
            return {lowUnsigned(value), highUnsigned(value)};
        }

        [[nodiscard]] Types::PixelSize framebufferSize() const noexcept
        {
            const std::uint64_t value = framebufferSize_.load(std::memory_order_acquire);
            return {lowUnsigned(value), highUnsigned(value)};
        }

        [[nodiscard]] Types::ContentScale contentScale() const noexcept
        {
            const std::uint64_t value = contentScale_.load(std::memory_order_acquire);
            return {lowFloat(value), highFloat(value)};
        }

        [[nodiscard]] Types::Dpi dpi() const noexcept
        {
            const std::uint64_t value = dpi_.load(std::memory_order_acquire);
            return {lowFloat(value), highFloat(value)};
        }

        [[nodiscard]] Types::Display::MonitorId monitor() const noexcept
        {
            return {monitor_.load(std::memory_order_acquire)};
        }

        [[nodiscard]] Types::PresentationState presentation() const noexcept
        {
            return static_cast<Types::PresentationState>(flags_.load(std::memory_order_acquire) & kPresentationMask);
        }

        [[nodiscard]] bool visible() const noexcept
        {
            return testFlag(kVisible);
        }

        [[nodiscard]] bool interactiveMoveResizeActive() const noexcept
        {
            return testFlag(kInteractiveMoveResizeActive);
        }

        [[nodiscard]] bool occluded() const noexcept
        {
            return testFlag(kOccluded);
        }

    private:
        static constexpr std::uint32_t kPresentationMask = 0x3U;
        static constexpr std::uint32_t kVisible = 1U << 2U;
        static constexpr std::uint32_t kInteractiveMoveResizeActive = 1U << 3U;
        static constexpr std::uint32_t kOccluded = 1U << 4U;

        [[nodiscard]] static constexpr std::uint64_t packUnsigned(std::uint32_t low, std::uint32_t high) noexcept
        {
            return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
        }

        [[nodiscard]] static constexpr std::uint64_t packFloats(float low, float high) noexcept
        {
            return packUnsigned(std::bit_cast<std::uint32_t>(low), std::bit_cast<std::uint32_t>(high));
        }

        [[nodiscard]] static constexpr std::uint32_t lowUnsigned(std::uint64_t value) noexcept
        {
            return static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] static constexpr std::uint32_t highUnsigned(std::uint64_t value) noexcept
        {
            return static_cast<std::uint32_t>(value >> 32U);
        }

        [[nodiscard]] static constexpr float lowFloat(std::uint64_t value) noexcept
        {
            return std::bit_cast<float>(lowUnsigned(value));
        }

        [[nodiscard]] static constexpr float highFloat(std::uint64_t value) noexcept
        {
            return std::bit_cast<float>(highUnsigned(value));
        }

        void updateFlag(std::uint32_t flag, bool value) noexcept
        {
            updateFlags(flag, value ? flag : 0U);
        }

        void updateFlags(std::uint32_t mask, std::uint32_t value) noexcept
        {
            std::uint32_t current = flags_.load(std::memory_order_relaxed);
            while (!flags_.compare_exchange_weak(current, (current & ~mask) | value, std::memory_order_release, std::memory_order_relaxed))
            {
            }
        }

        [[nodiscard]] bool testFlag(std::uint32_t flag) const noexcept
        {
            return (flags_.load(std::memory_order_acquire) & flag) != 0;
        }

        std::atomic<std::uint64_t> clientSize_{0};
        std::atomic<std::uint64_t> framebufferSize_{0};
        std::atomic<std::uint64_t> contentScale_{packFloats(1.0F, 1.0F)};
        std::atomic<std::uint64_t> dpi_{0};
        std::atomic<std::uint64_t> monitor_{0};
        std::atomic<std::uint32_t> flags_{0};
    };
} // namespace GameWIP::Desktop::Detail
