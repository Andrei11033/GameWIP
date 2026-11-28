/// @file unicode_test_helpers.inl
/// @brief Shared UTF-8 and UTF-16 validation helpers.

/// ------------------------------------------------------------
/// Scalar and codec helpers
/// ------------------------------------------------------------

/// @brief Builds a byte string from explicit unsigned octets without depending on char signedness.
std::string bytes(std::initializer_list<std::uint8_t> values)
{
    std::string result;
    result.reserve(values.size());

    for(const std::uint8_t value : values)
    {
        result.push_back(static_cast<char>(value));
    }

    return result;
}

/// @brief Checks a successful encoding's length before exposing its UTF-8 storage.
std::string_view encodedBytes(const Unicode::Types::Utf8::EncodeResult &result)
{
    if(result.byteCount == 0 || result.byteCount > result.bytes.size())
    {
        throw std::runtime_error(std::format("UTF-8 encode length {} is outside 1..{}", result.byteCount, result.bytes.size()));
    }
    return {result.bytes.data(), result.byteCount};
}

/// @brief Checks a successful encoding's length before exposing its UTF-16 storage.
std::span<const char16_t> encodedCodeUnits(const Unicode::Types::Utf16::EncodeResult &result)
{
    if(result.codeUnitCount == 0 || result.codeUnitCount > result.codeUnits.size())
    {
        throw std::runtime_error(std::format("UTF-16 encode length {} is outside 1..{}", result.codeUnitCount, result.codeUnits.size()));
    }
    return std::span{result.codeUnits}.first(result.codeUnitCount);
}

/// @brief Verifies one UTF-8 decode attempt and all deterministic failure fields.
void expectUtf8Decode(
    TestSupport::Context &context,
    std::string_view name,
    std::string_view input,
    DecodeOutcome expectedOutcome,
    char32_t expectedScalar = U'\0',
    std::uint8_t expectedBytesConsumed = 0)
{
    const Unicode::Types::Utf8::DecodeResult result = Unicode::Utf8::decodeScalar(input);
    static_cast<void>(context.expectEq(std::format("{} outcome", name), expectedOutcome, result.outcome));
    static_cast<void>(context.expectEq(std::format("{} scalar", name), expectedScalar, result.scalar));
    static_cast<void>(context.expectEq(std::format("{} consumed", name), expectedBytesConsumed, result.bytesConsumed));
}

/// @brief Verifies one UTF-16 decode attempt and all deterministic failure fields.
void expectUtf16Decode(
    TestSupport::Context &context,
    std::string_view name,
    std::span<const char16_t> input,
    DecodeOutcome expectedOutcome,
    char32_t expectedScalar = U'\0',
    std::uint8_t expectedCodeUnitsConsumed = 0)
{
    const Unicode::Types::Utf16::DecodeResult result = Unicode::Utf16::decodeScalar(input);
    static_cast<void>(context.expectEq(std::format("{} outcome", name), expectedOutcome, result.outcome));
    static_cast<void>(context.expectEq(std::format("{} scalar", name), expectedScalar, result.scalar));
    static_cast<void>(context.expectEq(std::format("{} consumed", name), expectedCodeUnitsConsumed, result.codeUnitsConsumed));
}
