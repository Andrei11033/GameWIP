/// @file profiling.h
/// @brief Executable-owned Tracy profiling hooks that compile to nothing when Tracy is disabled.
///
/// Call sites use these hooks unconditionally, so the GAMEWIP_TRACY_ENABLED check lives only here.
/// Zone and plot names must be string literals because Tracy stores their addresses.

#pragma once

#include <string_view>

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

/// @brief Colors shared by profiling zones so related phases look consistent on the Tracy timeline.
namespace GameWIP::Profiling::Color
{
    inline constexpr auto kProcess = 0x4C78A8;
    inline constexpr auto kRuntime = 0x4C78A8;
    inline constexpr auto kInitialization = 0x72B7B2;
    inline constexpr auto kFrame = 0x54A24B;
    inline constexpr auto kWait = 0x9D9DA1;
    inline constexpr auto kShutdown = 0xE45756;
    inline constexpr auto kValidation = 0xF58518;
    inline constexpr auto kBenchmark = 0xB279A2;
} // namespace GameWIP::Profiling::Color

namespace GameWIP::Profiling
{
    /// @brief Records a text message on the Tracy timeline; Tracy copies the text before this returns.
    inline void message(std::string_view text) noexcept
    {
#if GAMEWIP_TRACY_ENABLED
        TracyMessage(text.data(), text.size());
#else
        static_cast<void>(text);
#endif
    }

    /// @brief Marks the end of a frame on the Tracy timeline.
    inline void frameMark() noexcept
    {
#if GAMEWIP_TRACY_ENABLED
        FrameMark;
#endif
    }
} // namespace GameWIP::Profiling

#if GAMEWIP_TRACY_ENABLED
/// @brief Opens a colored profiling zone that ends at the end of the enclosing scope.
#define GAMEWIP_PROFILE_ZONE(name, color) ZoneScopedNC(name, color)
/// @brief Plots one numeric sample under a literal series name.
#define GAMEWIP_PROFILE_PLOT(name, value) TracyPlot(name, value)
/// @brief Names the calling thread on the Tracy timeline.
#define GAMEWIP_PROFILE_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define GAMEWIP_PROFILE_ZONE(name, color) static_cast<void>(0)
// sizeof keeps variables used only for plotting referenced without evaluating the expression.
#define GAMEWIP_PROFILE_PLOT(name, value) static_cast<void>(sizeof(value))
#define GAMEWIP_PROFILE_THREAD_NAME(name) static_cast<void>(0)
#endif
