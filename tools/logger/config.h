/// @file config.h
/// @brief Public Logger configuration model and presets.

#pragma once

#include "logger/logger_export.h"
#include "logger/types.h"
#include "logger/detail/layout_template.h"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace GameWIP::Logger
{
    // ------------------------------------------------------------
    // Colors and themes
    // ------------------------------------------------------------

    /// @brief Compile-time-friendly foreground color used by Logger themes and sources.
    struct Color
    {
        /// @brief Selects whether the color is default, basic, or RGB.
        enum class Kind : std::uint8_t
        {
            Default,
            Basic,
            Rgb
        };

        /// @brief ANSI basic foreground colors supported by Logger.
        enum class Basic : std::uint8_t
        {
            Black,
            Red,
            Green,
            Yellow,
            Blue,
            Magenta,
            Cyan,
            White,
            BrightBlack,
            BrightRed,
            BrightGreen,
            BrightYellow,
            BrightBlue,
            BrightMagenta,
            BrightCyan,
            BrightWhite
        };

        // Preserve conventional terminal color names in the public API.
        // NOLINTBEGIN(readability-identifier-naming)
        static const Color Default;       ///< Uses the terminal's default foreground.
        static const Color Black;         ///< ANSI basic black.
        static const Color Red;           ///< ANSI basic red.
        static const Color Green;         ///< ANSI basic green.
        static const Color Yellow;        ///< ANSI basic yellow.
        static const Color Blue;          ///< ANSI basic blue.
        static const Color Magenta;       ///< ANSI basic magenta.
        static const Color Cyan;          ///< ANSI basic cyan.
        static const Color White;         ///< ANSI basic white.
        static const Color BrightBlack;   ///< ANSI bright black (often rendered as gray).
        static const Color BrightRed;     ///< ANSI bright red.
        static const Color BrightGreen;   ///< ANSI bright green.
        static const Color BrightYellow;  ///< ANSI bright yellow.
        static const Color BrightBlue;    ///< ANSI bright blue.
        static const Color BrightMagenta; ///< ANSI bright magenta.
        static const Color BrightCyan;    ///< ANSI bright cyan.
        static const Color BrightWhite;   ///< ANSI bright white.
        // NOLINTEND(readability-identifier-naming)

        Kind kind = Kind::Default;  ///< Color representation selected for this value.
        Basic basic = Basic::White; ///< Basic color when kind is Basic.
        std::uint8_t red = 0;       ///< Red channel when kind is Rgb.
        std::uint8_t green = 0;     ///< Green channel when kind is Rgb.
        std::uint8_t blue = 0;      ///< Blue channel when kind is Rgb.

        /// @brief Creates a basic foreground color.
        [[nodiscard]] static constexpr Color basicColor(Basic value) noexcept
        {
            return {.kind = Kind::Basic, .basic = value};
        }

        /// @brief Creates an RGB foreground color from 8-bit channels.
        [[nodiscard]] static constexpr Color rgb(std::uint8_t redValue, std::uint8_t greenValue, std::uint8_t blueValue) noexcept
        {
            return {.kind = Kind::Rgb, .red = redValue, .green = greenValue, .blue = blueValue};
        }
    };

    inline constexpr Color Color::Default{};
    inline constexpr Color Color::Black = Color::basicColor(Color::Basic::Black);
    inline constexpr Color Color::Red = Color::basicColor(Color::Basic::Red);
    inline constexpr Color Color::Green = Color::basicColor(Color::Basic::Green);
    inline constexpr Color Color::Yellow = Color::basicColor(Color::Basic::Yellow);
    inline constexpr Color Color::Blue = Color::basicColor(Color::Basic::Blue);
    inline constexpr Color Color::Magenta = Color::basicColor(Color::Basic::Magenta);
    inline constexpr Color Color::Cyan = Color::basicColor(Color::Basic::Cyan);
    inline constexpr Color Color::White = Color::basicColor(Color::Basic::White);
    inline constexpr Color Color::BrightBlack = Color::basicColor(Color::Basic::BrightBlack);
    inline constexpr Color Color::BrightRed = Color::basicColor(Color::Basic::BrightRed);
    inline constexpr Color Color::BrightGreen = Color::basicColor(Color::Basic::BrightGreen);
    inline constexpr Color Color::BrightYellow = Color::basicColor(Color::Basic::BrightYellow);
    inline constexpr Color Color::BrightBlue = Color::basicColor(Color::Basic::BrightBlue);
    inline constexpr Color Color::BrightMagenta = Color::basicColor(Color::Basic::BrightMagenta);
    inline constexpr Color Color::BrightCyan = Color::basicColor(Color::Basic::BrightCyan);
    inline constexpr Color Color::BrightWhite = Color::basicColor(Color::Basic::BrightWhite);

    /// @brief Foreground colors used for the logger's semantic output parts.
    struct Theme
    {
        Color timestamp = Color::BrightBlack;  ///< Timestamp text and literals scoped as timestamps.
        Color message = Color::White;          ///< Fallback color when messageUsesSeverityColor is false.
        bool messageUsesSeverityColor = true;  ///< Use the severity color for unmarked message text.
        Color trace = Color::BrightGreen;      ///< Trace severity and literals scoped as severities.
        Color debug = Color::Cyan;             ///< Debug severity.
        Color info = Color::BrightBlack;       ///< Info severity.
        Color warn = Color::Yellow;            ///< Warning severity.
        Color error = Color::Red;              ///< Error severity.
        Color fatal = Color::rgb(255, 70, 70); ///< Fatal severity.
        Color area = Color::White;             ///< Registered area labels and literals scoped as areas.
        Color field = Color::BrightBlack;      ///< Record field names and literals scoped as fields.
        Color code = Color::Magenta;           ///< Values marked with `code()` and code scopes.
        Color path = Color::Cyan;              ///< Values marked with `path()` and path scopes.
        Color number = Color::Yellow;          ///< Values marked with `number()` and number scopes.
        Color quoted = Color::Green;           ///< Values marked with `quoted()` and quoted scopes.
    };

    /// @cond INTERNAL
    namespace Detail
    {

        [[nodiscard]] constexpr bool isValidColor(Color value) noexcept
        {
            switch(value.kind)
            {
            case Color::Kind::Default:
            case Color::Kind::Rgb:
                return true;
            case Color::Kind::Basic:
                return static_cast<std::uint8_t>(value.basic) <= static_cast<std::uint8_t>(Color::Basic::BrightWhite);
            }
            return false;
        }

        [[nodiscard]] constexpr bool isValidTheme(const Theme &value) noexcept
        {
            return isValidColor(value.timestamp) && isValidColor(value.message) && isValidColor(value.trace) && isValidColor(value.debug) &&
                   isValidColor(value.info) && isValidColor(value.warn) && isValidColor(value.error) && isValidColor(value.fatal) &&
                   isValidColor(value.area) && isValidColor(value.field) && isValidColor(value.code) && isValidColor(value.path) &&
                   isValidColor(value.number) && isValidColor(value.quoted);
        }

        [[nodiscard]] constexpr bool isValidTimestampFormat(std::string_view format) noexcept
        {
            if(format.empty() || format.size() > 63)
            {
                return false;
            }
            for(std::size_t index = 0; index < format.size(); ++index)
            {
                const unsigned char character = static_cast<unsigned char>(format[index]);
                if(character < 0x20u || character == 0x7Fu)
                {
                    return false;
                }
                if(format[index] == '%')
                {
                    if(index + 1 == format.size())
                    {
                        return false;
                    }
                    ++index;
                }
            }
            return true;
        }
    } // namespace Detail
    /// @endcond

    // ------------------------------------------------------------
    // Record and output layouts
    // ------------------------------------------------------------

    /// @brief Field presentation used when a record contains optional fields.
    enum class FieldLayout : std::uint8_t
    {
        Adaptive,
        Compact,
        Expanded,
        Parenthesized
    };

    /// @cond INTERNAL
    namespace Detail
    {
        [[nodiscard]] constexpr bool isValidFieldLayout(FieldLayout value) noexcept
        {
            return value == FieldLayout::Adaptive || value == FieldLayout::Compact || value == FieldLayout::Expanded ||
                   value == FieldLayout::Parenthesized;
        }
    } // namespace Detail
    /// @endcond

    /// @brief Factory helpers for built-in field layouts.
    namespace Layouts
    {
        /// @brief Uses the default layout, adapting field presentation to the record.
        [[nodiscard]] constexpr FieldLayout adaptive() noexcept
        {
            return FieldLayout::Adaptive;
        }

        /// @brief Places fields inline before the message.
        [[nodiscard]] constexpr FieldLayout compact() noexcept
        {
            return FieldLayout::Compact;
        }

        /// @brief Places each field on an indented line below the message.
        [[nodiscard]] constexpr FieldLayout expanded() noexcept
        {
            return FieldLayout::Expanded;
        }

        /// @brief Places fields in parentheses after the message.
        [[nodiscard]] constexpr FieldLayout parenthesized() noexcept
        {
            return FieldLayout::Parenthesized;
        }
    } // namespace Layouts

    /// @brief Compile-time-validated custom record layout.
    struct LayoutTemplate
    {
        std::string_view text{}; ///< UTF-8 layout string containing supported placeholders and color scopes.
    };

    /// @brief Creates a layout and rejects malformed placeholders or color scopes at compile time.
    /// @details Use `{@role}` and `{/}` to color literal text with a semantic theme role. Color scopes may nest up to eight levels.
    template <std::size_t Size> [[nodiscard]] consteval LayoutTemplate makeLayout(const char (&text)[Size])
    {
        const std::string_view layout{text, Size - 1};
        if(!Detail::isValidLayoutTemplate(layout) || !Detail::isValidUtf8(layout))
        {
            throw std::invalid_argument("Logger layout must use valid UTF-8 and known placeholders");
        }
        return {layout};
    }

    // ------------------------------------------------------------
    // Initialization configuration
    // ------------------------------------------------------------

    namespace Types
    {
        /// @brief Formatting strategy used before a message enters Logger-owned storage.
        enum class FormatPolicy : std::uint8_t
        {
            StrictBounded, ///< Bound formatting work to configured retained-message storage.
            FastNormal     ///< Favor the normal formatting path and contain allocation failure if it occurs.
        };

        /// @brief Associates a SourceId with its UTF-8 display name during initialization.
        struct SourceDefinition
        {
            SourceId id = 0;                           ///< Source identifier.
            std::string_view name = {};                ///< Valid non-empty UTF-8 display name.
            std::optional<Color> color = std::nullopt; ///< Optional foreground color for the source label.
        };

        /// @brief Initial enabled state for one registered source.
        struct SourceFilter
        {
            SourceId source = 0; ///< Registered source to configure.
            bool enabled = true; ///< Whether the source is initially enabled.
        };

        /// @brief Initial enabled state for one exact severity level.
        struct LevelFilter
        {
            Level level = Level::Trace; ///< Exact level to configure.
            bool enabled = true;        ///< Whether the level is initially enabled.
        };

        /// @brief Complete Logger initialization configuration.
        struct Config
        {
            OutputMode output = OutputMode::Both;                    ///< Requested normal-output sinks.
            Level minLevel = Level::Info;                            ///< Minimum accepted severity.
            std::size_t maxQueueSize = 1024;                         ///< Soft queue limit.
            double hardQueueMultiplier = 1.25;                       ///< Hard-limit multiplier applied to the soft limit.
            std::size_t maxMessageLength = 4096;                     ///< Maximum retained UTF-8 message bytes.
            FormatPolicy formatPolicy = FormatPolicy::StrictBounded; ///< Formatting memory policy.
            std::size_t inlineMessageCapacity = 256;                 ///< Inline bytes reserved per queued message.
            std::size_t workerBatchSize = 256;                       ///< Maximum records drained per worker batch.
            std::string_view logDirectory = "logs";                  ///< UTF-8 file-output directory.
            bool fallbackToConsoleOnFileFailure = true;              ///< Allow File to fall back to Console.
            std::span<const SourceDefinition> sources = {};          ///< Registered source definitions copied by init().
            std::span<const SourceFilter> sourceFilters = {};        ///< Initial per-source filters.
            std::span<const LevelFilter> levelFilters = {};          ///< Initial exact-level filters.
            bool enableConsoleColor = true;                          ///< Enable supported console styling.
            Theme theme{};                                           ///< Console theme used for semantic log parts.
            FieldLayout fieldLayout = FieldLayout::Adaptive;         ///< Default layout for record fields.
            LayoutTemplate layout{};                                 ///< Optional compile-time custom output layout.
            std::string_view timestampFormat = "%H:%M:%S.%f";        ///< Local-time strftime format; `%f` expands to milliseconds.
            std::size_t maxRecordFields = 8;                         ///< Maximum fields accepted by a Record.
            std::size_t maxFieldKeyLength = 64;                      ///< Maximum UTF-8 bytes retained per field key.
            std::size_t maxFieldValueLength = 512;                   ///< Maximum UTF-8 bytes retained per field value.
            bool enableDebugOutput = true;                           ///< Mirror reports to the debugger channel.
            bool enableFatalPopup = true;                            ///< Enable the fatal-report popup channel.
            bool flushFileEveryBatch = false;                        ///< Flush File after each worker batch.
            bool flushConsoleEveryWrite = false;                     ///< Flush Console after each normal write.
            bool releaseMessageMemoryAfterWrite = false;             ///< Release excess message scratch after writes.
            bool releaseStorageOnShutdown = true;                    ///< Release queue storage during shutdown.
        };
    } // namespace Types

    // ------------------------------------------------------------
    // Registered source enums
    // ------------------------------------------------------------

    /// @cond INTERNAL
    namespace Detail::Core
    {
        template <typename Enum, bool IsEnum = std::is_enum_v<Enum>> struct SourceEnumTraits : std::false_type
        {
        };

        template <typename Enum>
        struct SourceEnumTraits<Enum, true>
            : std::bool_constant<
                  std::is_unsigned_v<std::underlying_type_t<Enum>> && sizeof(std::underlying_type_t<Enum>) <= sizeof(Types::SourceId) &&
                  !std::is_same_v<Enum, Types::Level> && !std::is_same_v<Enum, Types::OutputMode> && !std::is_same_v<Enum, Types::FormatPolicy> &&
                  !std::is_same_v<Enum, Types::Init::Outcome> && !std::is_same_v<Enum, Types::Init::Adjustment> &&
                  !std::is_same_v<Enum, Types::FlushOutcome> && !std::is_same_v<Enum, Types::Report::Outcome> &&
                  !std::is_same_v<Enum, Types::Report::Delivery> && !std::is_same_v<Enum, Types::Health::State> &&
                  !std::is_same_v<Enum, Types::Health::FailureSource>>
        {
        };

        template <typename Enum>
        concept SourceEnum = SourceEnumTraits<std::remove_cvref_t<Enum>>::value;

        template <typename Enum> inline constexpr bool kIsSourceEnum = SourceEnum<Enum>;

        template <typename Enum>
            requires(SourceEnum<Enum>)
        constexpr Types::SourceId sourceId(Enum value) noexcept
        {
            using Underlying = std::underlying_type_t<std::remove_cvref_t<Enum>>;
            return static_cast<Types::SourceId>(static_cast<Underlying>(value));
        }
    } // namespace Detail::Core
    /// @endcond

    /// @brief Concept for source enums accepted by enum-based Logger APIs.
    /// @details The enum must use an unsigned underlying type no wider than SourceId and must not be a Logger-owned enum.
    /// Registration of individual values remains a runtime Config concern.
    template <typename Enum>
    concept SourceEnum = Detail::Core::SourceEnum<Enum>;

    /// @brief Creates a registered-source definition from an unsigned enum value and UTF-8 name.
    template <typename Enum>
        requires(SourceEnum<Enum>)
    constexpr Types::SourceDefinition defineSource(Enum value, std::string_view name) noexcept
    {
        return {Detail::Core::sourceId(value), name};
    }

    /// @brief Creates a registered-source definition with a custom area-label color.
    template <typename Enum>
        requires(SourceEnum<Enum>)
    constexpr Types::SourceDefinition defineSource(Enum value, std::string_view name, Color color) noexcept
    {
        return {Detail::Core::sourceId(value), name, color};
    }

    // ------------------------------------------------------------
    // Configuration presets
    // ------------------------------------------------------------

    /// @brief Returns the balanced default Logger configuration.
    [[nodiscard]] LOGGER_EXPORT Types::Config defaultConfig() noexcept;
    /// @brief Returns a configuration favoring lower retained memory.
    [[nodiscard]] LOGGER_EXPORT Types::Config lowMemoryConfig() noexcept;
    /// @brief Returns a configuration favoring logging throughput.
    [[nodiscard]] LOGGER_EXPORT Types::Config throughputConfig() noexcept;
} // namespace GameWIP::Logger
