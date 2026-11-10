/// @file types.h
/// @brief Portable, generic device-element vocabulary for GameWIP Input.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// @brief Public passive data types shared by the GameWIP Input API.
namespace GameWIP::Input::Types
{
    /// @brief Runtime identity for one device in one open session.
    /// @details Runtime IDs are opaque lookup tokens valid only for the open session. Use persistent keys or control identities for bindings.
    struct DeviceId
    {
        std::uint64_t instance = 0;   ///< System-lifetime identity.
        std::uint32_t slot = 0;       ///< Device slot within the open session.
        std::uint32_t generation = 0; ///< Generation for slot reuse.

        /// @brief Reports whether this ID can identify a live or historical device.
        /// @return `true` when the instance and generation are nonzero.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return instance != 0 && generation != 0;
        }

        friend constexpr bool operator==(DeviceId, DeviceId) noexcept = default;
    };

    /// @brief Runtime identity for one element in one device.
    /// @details The device portion identifies the owning device; slot and generation identify the element within that device lifetime.
    struct ElementId
    {
        DeviceId device{};            ///< Owning device identity.
        std::uint32_t slot = 0;       ///< Element slot within the device lifetime.
        std::uint32_t generation = 0; ///< Generation for slot reuse.

        /// @brief Reports whether this ID can identify an element.
        /// @return `true` when the device and element generation are valid.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return device.isValid() && generation != 0;
        }

        friend constexpr bool operator==(ElementId, ElementId) noexcept = default;
    };

    /// @brief Runtime identity for one numeric component of an element.
    struct ComponentId
    {
        ElementId element{};    ///< Owning element identity.
        std::uint8_t index = 0; ///< Zero-based component index.

        friend constexpr bool operator==(ComponentId, ComponentId) noexcept = default;
    };

    /// @brief Runtime identity for a text-input surface.
    struct SurfaceId
    {
        std::uint64_t value = 0; ///< Nonzero runtime surface identity.

        /// @brief Reports whether this ID identifies a surface.
        /// @return `true` when the ID is nonzero.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(SurfaceId, SurfaceId) noexcept = default;
    };

    /// @brief Runtime identity for a spatial reference space.
    struct SpaceId
    {
        std::uint64_t value = 0; ///< Nonzero runtime reference-space identity.

        friend constexpr bool operator==(SpaceId, SpaceId) noexcept = default;
    };

    /// @brief Two-component value used for typed input observations.
    /// @tparam T Component storage type.
    template <typename T> struct Vector2
    {
        T x{}; ///< First component.
        T y{}; ///< Second component.

        friend constexpr bool operator==(Vector2, Vector2) noexcept = default;
    };

    /// @brief Three-component value used for typed input observations.
    /// @tparam T Component storage type.
    template <typename T> struct Vector3
    {
        T x{}; ///< First component.
        T y{}; ///< Second component.
        T z{}; ///< Third component.

        friend constexpr bool operator==(Vector3, Vector3) noexcept = default;
    };

    /// @brief Four-component quaternion value used for orientation observations.
    /// @tparam T Component storage type.
    template <typename T> struct Quaternion
    {
        T x{}; ///< X component.
        T y{}; ///< Y component.
        T z{}; ///< Z component.
        T w{}; ///< W component.

        friend constexpr bool operator==(Quaternion, Quaternion) noexcept = default;
    };

    using Vector2I64 = Vector2<std::int64_t>;  ///< Two signed 64-bit components.
    using Vector2U64 = Vector2<std::uint64_t>; ///< Two unsigned 64-bit components.
    using Vector2F32 = Vector2<float>;         ///< Two 32-bit floating-point components.
    using Vector2F64 = Vector2<double>;        ///< Two 64-bit floating-point components.
    using Vector3I64 = Vector3<std::int64_t>;  ///< Three signed 64-bit components.
    using Vector3U64 = Vector3<std::uint64_t>; ///< Three unsigned 64-bit components.
    using Vector3F32 = Vector3<float>;         ///< Three 32-bit floating-point components.
    using Vector3F64 = Vector3<double>;        ///< Three 64-bit floating-point components.
    using QuaternionF32 = Quaternion<float>;   ///< 32-bit floating-point quaternion.
    using QuaternionF64 = Quaternion<double>;  ///< 64-bit floating-point quaternion.

    /// @brief A coherent spatial observation with independently valid parts.
    struct Spatial
    {
        Vector3F64 position{};           ///< Position in referenceSpace when positionValid is true.
        QuaternionF64 orientation{};     ///< Orientation in referenceSpace when orientationValid is true.
        SpaceId referenceSpace{};        ///< Space in which valid spatial values are expressed.
        bool positionValid = false;      ///< Whether position contains a valid observation.
        bool orientationValid = false;   ///< Whether orientation contains a valid observation.
        bool positionTracked = false;    ///< Whether position is actively tracked rather than inferred.
        bool orientationTracked = false; ///< Whether orientation is actively tracked rather than inferred.

        friend constexpr bool operator==(Spatial, Spatial) noexcept = default;
    };

    /// @brief Numeric scalar returned by component queries.
    using Scalar = std::variant<std::int64_t, std::uint64_t, float, double>;

    /// @brief Typed value carried by an element state or event.
    /// @details The alternative index is intentionally kept in lockstep with ValueKind. A monostate represents no value.
    // The aliases and ValueKind enumerators deliberately share names so that
    // metadata, diagnostics, and source code use one vocabulary for a value's
    // storage type.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
    using Value = std::variant<
        std::monostate,
        bool,
        std::int64_t,
        std::uint64_t,
        float,
        double,
        Vector2I64,
        Vector2U64,
        Vector2F32,
        Vector2F64,
        Vector3I64,
        Vector3U64,
        Vector3F32,
        Vector3F64,
        QuaternionF32,
        QuaternionF64,
        Spatial>;

    /// @brief Public value alternatives supported by the generic input contract.
    enum class ValueKind : std::uint8_t
    {
        None,            ///< No typed value.
        Boolean,         ///< Boolean state.
        SignedInteger,   ///< Signed 64-bit integer.
        UnsignedInteger, ///< Unsigned 64-bit integer.
        Float32,         ///< 32-bit floating-point scalar.
        Float64,         ///< 64-bit floating-point scalar.
        Vector2I64,      ///< Two signed 64-bit components.
        Vector2U64,      ///< Two unsigned 64-bit components.
        Vector2F32,      ///< Two 32-bit floating-point components.
        Vector2F64,      ///< Two 64-bit floating-point components.
        Vector3I64,      ///< Three signed 64-bit components.
        Vector3U64,      ///< Three unsigned 64-bit components.
        Vector3F32,      ///< Three 32-bit floating-point components.
        Vector3F64,      ///< Three 64-bit floating-point components.
        QuaternionF32,   ///< 32-bit floating-point quaternion.
        QuaternionF64,   ///< 64-bit floating-point quaternion.
        Spatial          ///< Spatial observation.
    };

    static_assert(std::variant_size_v<Value> == static_cast<std::size_t>(ValueKind::Spatial) + 1);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    /// @brief Describes whether a value replaces or accumulates the previous value.
    enum class ReportingMode : std::uint8_t
    {
        Absolute, ///< Value is the current state.
        Relative  ///< Value is a delta since the previous observation.
    };

    /// @brief Unit metadata for numeric values.
    enum class Unit : std::uint8_t
    {
        Unspecified,           ///< Backend did not provide a unit.
        Count,                 ///< Dimensionless count.
        Pixel,                 ///< Screen or surface pixel.
        Metre,                 ///< Metre.
        Radian,                ///< Radian.
        Degree,                ///< Degree.
        Second,                ///< Second.
        MetrePerSecond,        ///< Metre per second.
        MetrePerSecondSquared, ///< Metre per second squared.
        RadianPerSecond,       ///< Radian per second.
        DegreePerSecond,       ///< Degree per second.
        Tesla,                 ///< Tesla.
        Newton,                ///< Newton.
        NormalizedByBackend    ///< Backend-defined normalized range.
    };

    /// @brief Quality of the most recent element observation.
    enum class ValueValidity : std::uint8_t
    {
        Unknown,    ///< No observation has been published.
        Valid,      ///< The value is current and usable.
        Stale,      ///< The value is retained but older than the source contract allows.
        Unavailable ///< The source cannot currently provide the value.
    };

    /// @brief Origin category for a device descriptor.
    enum class DeviceOrigin : std::uint8_t
    {
        Physical,              ///< Physical hardware.
        Virtual,               ///< Software-created or emulated device.
        OperatingSystemService ///< Operating-system input service.
    };

    /// @brief Confidence level for a device's persistent identity.
    enum class IdentityConfidence : std::uint8_t
    {
        None,           ///< No stable identity is known.
        ModelOnly,      ///< Identity describes a model or product family.
        ConnectionPath, ///< Identity includes a stable connection path.
        DeviceUnique    ///< Identity is unique to the physical device.
    };

    /// @brief Backend that authored or owns an input observation.
    enum class BackendKind : std::uint8_t
    {
        GameInput,          ///< Microsoft GameInput.
        Win32RawInput,      ///< Windows Raw Input.
        Win32WindowMessage, ///< Windows window-message input.
        Win32Hid,           ///< Windows HID access.
        XInput,             ///< Microsoft XInput.
        LinuxEvdev,         ///< Linux evdev.
        LinuxHidraw,        ///< Linux hidraw.
        Synthetic,          ///< Explicit software producer.
        OpenXr              ///< OpenXR runtime.
    };

    /// @brief Cause attached to a published event.
    enum class EventCause : std::uint8_t
    {
        Reported,      ///< Directly reported by an authoritative source.
        Synthesized,   ///< Produced by an explicit software source.
        FocusLost,     ///< Invalidated because input focus was lost.
        Disconnected,  ///< Invalidated because the source disconnected.
        SourceChanged, ///< Changed because an authoritative source changed.
        Resynchronized ///< Published while recovering from a discontinuity.
    };

    /// @brief Native numeric storage metadata.
    enum class NumericStorage : std::uint8_t
    {
        Unknown,    ///< Native storage is unknown.
        Signed8,    ///< Signed 8-bit integer.
        Signed16,   ///< Signed 16-bit integer.
        Signed32,   ///< Signed 32-bit integer.
        Signed64,   ///< Signed 64-bit integer.
        Unsigned8,  ///< Unsigned 8-bit integer.
        Unsigned16, ///< Unsigned 16-bit integer.
        Unsigned32, ///< Unsigned 32-bit integer.
        Unsigned64, ///< Unsigned 64-bit integer.
        Float32,    ///< 32-bit floating-point value.
        Float64     ///< 64-bit floating-point value.
    };

    /// @brief Provenance category for an authored value.
    enum class ValueProvenance : std::uint8_t
    {
        NativeDecoded,      ///< Decoded directly from native source data.
        NativeApiProcessed, ///< Produced by a native API after its own processing.
        Synthetic           ///< Produced by an explicit software source.
    };

    /// @brief Vocabulary domain for a recognized standard control.
    /// @note This is a naming domain, not a device class or backend selection.
    enum class StandardDomain : std::uint8_t
    {
        None,     ///< No standard domain.
        Keyboard, ///< Keyboard or keypad control.
        Mouse,    ///< Pointing-device control.
        Gamepad,  ///< Conventional gamepad control.
        Pointer   ///< Window-level pointer control.
    };

    /// @brief Strongly typed identity for a well-known input control.
    struct StandardId
    {
        StandardDomain domain = StandardDomain::None; ///< Vocabulary domain.
        std::uint32_t code = 0;                       ///< Domain-specific code.

        /// @brief Reports whether this standard identifier is complete.
        /// @return `true` when the domain and code are nonzero.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return domain != StandardDomain::None && code != 0;
        }

        friend constexpr bool operator==(StandardId, StandardId) noexcept = default;
    };

    /// @brief Canonical HID identity for one physical control or component.
    /// @details Usage identity remains meaningful across native backends when the same HID descriptor is exposed.
    struct HidControlId
    {
        std::uint32_t usagePage = 0;  ///< HID usage page.
        std::uint32_t usage = 0;      ///< HID usage within usagePage.
        std::uint32_t collection = 0; ///< HID link collection.
        std::uint32_t reportId = 0;   ///< HID input report identifier.
        std::uint32_t component = 0;  ///< Zero-based component within a repeated usage.

        /// @brief Reports whether the usage identity is complete.
        /// @return `true` when both the usage page and usage are nonzero.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return usagePage != 0 && usage != 0;
        }

        friend constexpr bool operator==(HidControlId, HidControlId) noexcept = default;
    };

    /// @brief Canonical identity exposed for one input control.
    /// @details Standard identity is preferred when recognized; HID identity remains available for unusual and vendor-defined controls.
    struct ControlIdentity
    {
        std::optional<StandardId> standard{}; ///< Optional portable standard-control identity.
        std::optional<HidControlId> hid{};    ///< Optional canonical HID identity.

        /// @brief Reports whether at least one canonical identity is present.
        /// @return `true` when a valid standard or HID identity is available.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return (standard.has_value() && standard->isValid()) || (hid.has_value() && hid->isValid());
        }

        friend constexpr bool operator==(ControlIdentity, ControlIdentity) noexcept = default;
    };

    /// @brief Persistent, pre-device selector for an input element.
    /// @details Selectors may combine a persistent key, standard or HID identity, value kind, and reporting mode. Runtime IDs are intentionally not
    /// part of this type.
    struct ElementSelector
    {
        std::optional<std::string_view> persistentKey{}; ///< Optional producer-defined persistent binding key.
        std::optional<StandardId> standard{};            ///< Optional well-known control identity.
        std::optional<HidControlId> hid{};               ///< Optional canonical HID control identity.
        std::optional<ValueKind> kind{};                 ///< Optional typed-value requirement.
        std::optional<ReportingMode> reporting{};        ///< Optional reporting-mode requirement.

        /// @brief Reports whether at least one selection criterion is present.
        /// @return `true` when the selector contains a criterion.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            const bool hasCriterion =
                persistentKey.has_value() || standard.has_value() || hid.has_value() || kind.has_value() || reporting.has_value();
            const bool persistentKeyValid = !persistentKey.has_value() || !persistentKey->empty();
            const bool standardValid = !standard.has_value() || standard->isValid();
            const bool hidValid = !hid.has_value() || hid->isValid();
            return hasCriterion && persistentKeyValid && standardValid && hidValid;
        }
    };

    /// @brief Numeric range, unit, and native-storage metadata.
    struct NumericInfo
    {
        NumericStorage nativeStorage = NumericStorage::Unknown; ///< Native storage representation.
        std::optional<Scalar> minimum{};                        ///< Optional minimum representable value.
        std::optional<Scalar> maximum{};                        ///< Optional maximum representable value.
        std::optional<Scalar> neutral{};                        ///< Optional neutral or center value.
        Unit unit = Unit::Unspecified;                          ///< Physical or backend-defined unit.
        std::int32_t unitExponent = 0;                          ///< Decimal exponent applied to unit values.
        bool nullStateSupported = false;                        ///< Whether the source has a distinct null state.
    };

    /// @brief Metadata for one numeric component of an element.
    struct ComponentInfo
    {
        std::uint8_t index = 0; ///< Zero-based component index.
        std::string name{};     ///< Human-readable UTF-8 component name.
        NumericInfo numeric{};  ///< Numeric metadata for this component.
    };

    /// @brief Stable descriptor for one input element.
    struct ElementInfo
    {
        ElementId id{};                                              ///< Runtime element identity.
        std::string name{};                                          ///< Human-readable UTF-8 name.
        std::string persistentKey{};                                 ///< Producer-owned persistent descriptor key.
        ControlIdentity identity{};                                  ///< Portable standard or canonical HID identity.
        ValueKind kind = ValueKind::None;                            ///< Typed value kind.
        ReportingMode reporting = ReportingMode::Absolute;           ///< Absolute or relative reporting.
        std::vector<ComponentInfo> components{};                     ///< Component metadata in index order.
        BackendKind authoritativeBackend = BackendKind::Synthetic;   ///< Backend that owns the value.
        ValueProvenance provenance = ValueProvenance::NativeDecoded; ///< Value provenance category.
    };

    /// @brief One backend source contributing to a logical device.
    /// @details A logical device may contain multiple sources when the backend
    /// exposes one physical device through more than one input interface.
    struct SourceInfo
    {
        BackendKind backend = BackendKind::Synthetic;                     ///< Backend category.
        std::string backendName{};                                        ///< Human-readable UTF-8 backend name.
        bool active = false;                                              ///< Whether this source is currently active.
        std::string persistentKey{};                                      ///< Source-specific persistent identity when available.
        IdentityConfidence identityConfidence = IdentityConfidence::None; ///< Confidence in persistentKey.
        std::optional<std::uint16_t> vendorId{};                          ///< Optional source vendor identifier.
        std::optional<std::uint16_t> productId{};                         ///< Optional source product identifier.
    };

    /// @brief Stable descriptor for one logical runtime device.
    /// @details Controls from the same confidently identified physical device
    /// are grouped here, while each contributing backend remains visible in
    /// sources. Ambiguous sources remain separate rather than being guessed.
    struct DeviceInfo
    {
        DeviceId id{};                                                    ///< Runtime device identity.
        DeviceOrigin origin = DeviceOrigin::Physical;                     ///< Physical, virtual, or service origin.
        std::string name{};                                               ///< Human-readable UTF-8 device name.
        std::string manufacturer{};                                       ///< Human-readable UTF-8 manufacturer name.
        std::string persistentKey{};                                      ///< Backend-neutral persistent identity.
        IdentityConfidence identityConfidence = IdentityConfidence::None; ///< Confidence in persistentKey.
        std::optional<std::uint16_t> vendorId{};                          ///< Optional vendor identifier.
        std::optional<std::uint16_t> productId{};                         ///< Optional product identifier.
        bool connected = false;                                           ///< Whether the descriptor represents a connected source.
        std::uint64_t descriptorRevision = 0;                             ///< Revision at which this descriptor was published.
        std::vector<SourceInfo> sources{};                                ///< Backend sources contributing to the device.
        std::vector<ElementInfo> elements{};                              ///< Elements exposed by the device.
    };

    /// @brief Monotonic clock used for portable input timestamps.
    using Clock = std::chrono::steady_clock;

    /// @brief Observation timestamp with optional native clock correlation.
    struct Timestamp
    {
        Clock::time_point receivedAt{};      ///< Portable receipt time; zero means unspecified.
        std::uint64_t nativeTicks = 0;       ///< Optional backend-native tick value.
        std::uint64_t nativeFrequency = 0;   ///< Optional native ticks per second.
        std::uint64_t nativeClockDomain = 0; ///< Optional native clock-domain identity.
    };

    /// @brief Latest value and interval transition counters for one element.
    struct ElementState
    {
        ElementId id{};                                  ///< Runtime element identity.
        Value value{};                                   ///< Latest typed value, retained when validity is not Valid.
        ValueValidity validity = ValueValidity::Unknown; ///< Quality of value.
        Timestamp lastObservation{};                     ///< Timestamp of the latest source observation.
        std::uint32_t becameTrueCount = 0;               ///< Boolean false-to-true transitions in the last frame interval.
        std::uint32_t becameFalseCount = 0;              ///< Boolean true-to-false transitions in the last frame interval.
        std::uint32_t repeatCount = 0;                   ///< Repeated reports in the last frame interval.
    };

    /// @brief Kind of event published in a frame.
    enum class EventKind : std::uint8_t
    {
        ElementChanged,     ///< Element value changed.
        ElementRepeated,    ///< Element repeated its current value.
        ElementInvalidated, ///< Element value became unavailable or stale.
        DeviceConnected,    ///< Device became available.
        DeviceDisconnected, ///< Device became unavailable.
        DeviceChanged,      ///< Device descriptor changed.
        TextCommitted,      ///< Text was committed to a surface.
        CompositionStarted, ///< Text composition began.
        CompositionUpdated, ///< Text composition changed.
        CompositionFinished ///< Text composition ended.
    };

    /// @brief Element value transition payload.
    struct ElementEvent
    {
        ElementId id{};             ///< Element whose value changed.
        Value value{};              ///< New or retained value.
        Value previousValue{};      ///< Previous value when previousKnown is true.
        bool previousKnown = false; ///< Whether previousValue was a valid prior observation.
    };

    /// @brief Device descriptor transition payload.
    struct DeviceEvent
    {
        DeviceId id{};                        ///< Device whose descriptor changed.
        std::uint64_t descriptorRevision = 0; ///< Revision associated with the transition.
    };

    /// @brief Text composition or commit payload encoded as UTF-8.
    struct TextEvent
    {
        SurfaceId surface{};                  ///< Text surface receiving the event.
        std::string utf8{};                   ///< UTF-8 text payload.
        std::size_t selectionStartBytes = 0;  ///< Selection start measured in UTF-8 bytes.
        std::size_t selectionLengthBytes = 0; ///< Selection length measured in UTF-8 bytes.
        bool canceled = false;                ///< Whether the composition was canceled.
    };

    /// @brief Payload alternatives carried by an Event.
    using EventPayload = std::variant<ElementEvent, DeviceEvent, TextEvent>;

    /// @brief One ordered event in a captured frame.
    struct Event
    {
        std::uint64_t sequence = 0;                 ///< Monotonic event sequence within the open session.
        Timestamp timestamp{};                      ///< Event observation time.
        EventKind kind = EventKind::ElementChanged; ///< Event category.
        EventCause cause = EventCause::Reported;    ///< Why the event was published.
        EventPayload data{};                        ///< Category-specific payload.
    };

    /// @brief Health counters accumulated during the current open session.
    struct Health
    {
        std::uint64_t totalDroppedEvents = 0;   ///< Events discarded after the bounded queue filled.
        std::uint64_t totalInvalidReports = 0;  ///< Reports rejected by a backend or producer.
        std::uint64_t totalBackendFailures = 0; ///< Backend failures observed by the system.
        std::size_t activeDeviceCount = 0;      ///< Currently connected device count.
    };

    /// @brief Open-time behavior and bounded-storage configuration.
    struct Configuration
    {
        std::size_t eventCapacity = 4096;                ///< Maximum queued events between captures.
        bool enableText = true;                          ///< Whether text events are accepted.
        std::size_t maximumPendingTextBytes = std::size_t{64} * std::size_t{1024}; ///< Maximum UTF-8 text retained between captures.
        bool captureRawReports = false;                  ///< Whether native raw reports are retained in frames.
        std::size_t rawReportCapacity = 256;             ///< Maximum raw reports retained between captures.
        std::size_t maximumRawReportBytes = 4096;        ///< Maximum bytes per retained raw report.
    };
} // namespace GameWIP::Input::Types
