/// @file input_event_labels.cpp
/// @brief Stable event and metadata labels used by runtime Input diagnostics.

#include "input_event_format.h"

namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
{
    namespace Input = GameWIP::Input;

    // ------------------------------------------------------------
    // Event labels
    // ------------------------------------------------------------

    std::string_view eventKind(Input::Types::EventKind kind) noexcept
    {
        using Kind = Input::Types::EventKind;
        switch(kind)
        {
        case Kind::ElementChanged:
            return "element_changed";
        case Kind::ElementRepeated:
            return "element_repeated";
        case Kind::ElementInvalidated:
            return "element_invalidated";
        case Kind::DeviceConnected:
            return "device_connected";
        case Kind::DeviceDisconnected:
            return "device_disconnected";
        case Kind::DeviceChanged:
            return "device_changed";
        case Kind::TextCommitted:
            return "text_committed";
        case Kind::CompositionStarted:
            return "composition_started";
        case Kind::CompositionUpdated:
            return "composition_updated";
        case Kind::CompositionFinished:
            return "composition_finished";
        }
        return "unknown";
    }

    std::string_view eventCause(Input::Types::EventCause cause) noexcept
    {
        using Cause = Input::Types::EventCause;
        switch(cause)
        {
        case Cause::Reported:
            return "reported";
        case Cause::Synthesized:
            return "synthesized";
        case Cause::FocusLost:
            return "focus_lost";
        case Cause::Disconnected:
            return "disconnected";
        case Cause::SourceChanged:
            return "source_changed";
        case Cause::Resynchronized:
            return "resynchronized";
        }
        return "unknown";
    }

    // ------------------------------------------------------------
    // Value labels
    // ------------------------------------------------------------

    std::string_view valueKind(Input::Types::ValueKind kind) noexcept
    {
        using Kind = Input::Types::ValueKind;
        switch(kind)
        {
        case Kind::None:
            return "none";
        case Kind::Boolean:
            return "boolean";
        case Kind::SignedInteger:
            return "signed_integer";
        case Kind::UnsignedInteger:
            return "unsigned_integer";
        case Kind::Float32:
            return "float32";
        case Kind::Float64:
            return "float64";
        case Kind::Vector2I64:
            return "vector2_i64";
        case Kind::Vector2U64:
            return "vector2_u64";
        case Kind::Vector2F32:
            return "vector2_f32";
        case Kind::Vector2F64:
            return "vector2_f64";
        case Kind::Vector3I64:
            return "vector3_i64";
        case Kind::Vector3U64:
            return "vector3_u64";
        case Kind::Vector3F32:
            return "vector3_f32";
        case Kind::Vector3F64:
            return "vector3_f64";
        case Kind::QuaternionF32:
            return "quaternion_f32";
        case Kind::QuaternionF64:
            return "quaternion_f64";
        case Kind::Spatial:
            return "spatial";
        }
        return "unknown";
    }

    std::string_view reportingMode(Input::Types::ReportingMode mode) noexcept
    {
        switch(mode)
        {
        case Input::Types::ReportingMode::Absolute:
            return "absolute";
        case Input::Types::ReportingMode::Relative:
            return "relative";
        }
        return "unknown";
    }

    // ------------------------------------------------------------
    // Backend and identity labels
    // ------------------------------------------------------------

    std::string_view backend(Input::Types::BackendKind kind) noexcept
    {
        using Backend = Input::Types::BackendKind;
        switch(kind)
        {
        case Backend::GameInput:
            return "game_input";
        case Backend::Win32RawInput:
            return "win32_raw_input";
        case Backend::Win32WindowMessage:
            return "win32_window_message";
        case Backend::Win32Hid:
            return "win32_hid";
        case Backend::XInput:
            return "xinput";
        case Backend::LinuxEvdev:
            return "linux_evdev";
        case Backend::LinuxHidraw:
            return "linux_hidraw";
        case Backend::Synthetic:
            return "synthetic";
        case Backend::OpenXr:
            return "openxr";
        case Backend::DirectInput:
            return "directinput";
        }
        return "unknown";
    }

    std::string_view deviceOrigin(Input::Types::DeviceOrigin origin) noexcept
    {
        using Origin = Input::Types::DeviceOrigin;
        switch(origin)
        {
        case Origin::Physical:
            return "physical";
        case Origin::Virtual:
            return "virtual";
        case Origin::OperatingSystemService:
            return "operating_system_service";
        }
        return "unknown";
    }

    std::string_view identityConfidence(Input::Types::IdentityConfidence confidence) noexcept
    {
        using Confidence = Input::Types::IdentityConfidence;
        switch(confidence)
        {
        case Confidence::None:
            return "none";
        case Confidence::ModelOnly:
            return "model_only";
        case Confidence::ConnectionPath:
            return "connection_path";
        case Confidence::DeviceUnique:
            return "device_unique";
        }
        return "unknown";
    }

    // ------------------------------------------------------------
    // Control and value metadata
    // ------------------------------------------------------------

    std::string_view standardDomain(Input::Types::StandardDomain domain) noexcept
    {
        using Domain = Input::Types::StandardDomain;
        switch(domain)
        {
        case Domain::None:
            return "none";
        case Domain::Keyboard:
            return "keyboard";
        case Domain::Mouse:
            return "mouse";
        case Domain::Gamepad:
            return "gamepad";
        case Domain::Pointer:
            return "pointer";
        }
        return "unknown";
    }

    std::string_view numericStorage(Input::Types::NumericStorage storage) noexcept
    {
        using Storage = Input::Types::NumericStorage;
        switch(storage)
        {
        case Storage::Unknown:
            return "unknown";
        case Storage::Signed8:
            return "signed_8";
        case Storage::Signed16:
            return "signed_16";
        case Storage::Signed32:
            return "signed_32";
        case Storage::Signed64:
            return "signed_64";
        case Storage::Unsigned8:
            return "unsigned_8";
        case Storage::Unsigned16:
            return "unsigned_16";
        case Storage::Unsigned32:
            return "unsigned_32";
        case Storage::Unsigned64:
            return "unsigned_64";
        case Storage::Float32:
            return "float_32";
        case Storage::Float64:
            return "float_64";
        }
        return "unknown";
    }

    std::string_view unit(Input::Types::Unit unit) noexcept
    {
        using Unit = Input::Types::Unit;
        switch(unit)
        {
        case Unit::Unspecified:
            return "unspecified";
        case Unit::Count:
            return "count";
        case Unit::Pixel:
            return "pixel";
        case Unit::Metre:
            return "metre";
        case Unit::Radian:
            return "radian";
        case Unit::Degree:
            return "degree";
        case Unit::Second:
            return "second";
        case Unit::MetrePerSecond:
            return "metre_per_second";
        case Unit::MetrePerSecondSquared:
            return "metre_per_second_squared";
        case Unit::RadianPerSecond:
            return "radian_per_second";
        case Unit::DegreePerSecond:
            return "degree_per_second";
        case Unit::Tesla:
            return "tesla";
        case Unit::Newton:
            return "newton";
        case Unit::NormalizedByBackend:
            return "normalized_by_backend";
        }
        return "unknown";
    }

    std::string_view valueProvenance(Input::Types::ValueProvenance provenance) noexcept
    {
        using Provenance = Input::Types::ValueProvenance;
        switch(provenance)
        {
        case Provenance::NativeDecoded:
            return "native_decoded";
        case Provenance::NativeApiProcessed:
            return "native_api_processed";
        case Provenance::Synthetic:
            return "synthetic";
        }
        return "unknown";
    }
} // namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
