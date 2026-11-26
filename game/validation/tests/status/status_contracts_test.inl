/// @file status_contracts_test.inl
/// @brief Status code, diagnostic, and result contract checks.

void testCodeNames(TestSupport::Context &context)
{
    const std::uint8_t lastCode = std::to_underlying(Code::Unknown);

    for(std::uint16_t value = 0; value <= lastCode; ++value)
    {
        const Code code = static_cast<Code>(value);
        const std::string_view name = StatusLib::Types::codeName(code);

        static_cast<void>(context.expectTrue("Every declared status code has a name", !name.empty()));
        if(code != Code::Unknown)
        {
            static_cast<void>(context.expectNe("Declared codes do not fall through to Unknown", std::string_view{"Unknown"}, name));
        }
    }

    static_cast<void>(
        context.expectEq("Unknown code has its stable diagnostic name", std::string_view{"Unknown"}, StatusLib::Types::codeName(Code::Unknown)));
    static_cast<void>(context.expectEq(
        "Invalid code values use the fallback name",
        std::string_view{"Unknown"},
        StatusLib::Types::codeName(static_cast<Code>(0xFF))));

    for(std::uint8_t value = 0; value <= std::to_underlying(NativeCodeDomain::Other); ++value)
    {
        const NativeCodeDomain domain = static_cast<NativeCodeDomain>(value);
        static_cast<void>(context.expectTrue("Every native-code domain has a name", !StatusLib::Types::nativeCodeDomainName(domain).empty()));
    }

    static_cast<void>(context.expectEq(
        "Invalid native-code domains use the fallback name",
        std::string_view{"Other"},
        StatusLib::Types::nativeCodeDomainName(static_cast<NativeCodeDomain>(0xFF))));
}

void testStatusDetails(TestSupport::Context &context)
{
    const Status success;
    static_cast<void>(context.expectTrue("Default status means success", success.ok()));
    static_cast<void>(context.expectEq("Default status uses Success", Code::Success, success.code()));
    static_cast<void>(context.expectTrue("Default status has no diagnostic", success.diagnostic() == nullptr));

    const Status codeOnly(Code::NotFound);
    static_cast<void>(context.expectFalse("Code-only failure is not successful", codeOnly.ok()));
    static_cast<void>(context.expectEq("Code-only failure preserves its code", Code::NotFound, codeOnly.code()));
    static_cast<void>(context.expectTrue("Code-only status has no allocated detail", codeOnly.diagnostic() == nullptr));

    const Status messageStatus = Status::withDiagnostic(Code::ReadFailed, "reading settings failed");
    static_cast<void>(context.expectEq("Message diagnostic preserves status code", Code::ReadFailed, messageStatus.code()));
    static_cast<void>(context.expectTrue("Message diagnostic is present", messageStatus.diagnostic() != nullptr));
    if(messageStatus.diagnostic() != nullptr)
    {
        static_cast<void>(
            context.expectEq("Message diagnostic is preserved", std::string_view{"reading settings failed"}, messageStatus.diagnostic()->message));
        static_cast<void>(context.expectTrue("Message-only diagnostic has no native code", !messageStatus.diagnostic()->nativeCode.has_value()));
    }

    const NativeCode nativeCode{.domain = NativeCodeDomain::Errno, .value = 5};
    const Status nativeStatus = Status::withDiagnostic(Code::OpenFailed, nativeCode, "open failed");
    static_cast<void>(context.expectEq("Native detail preserves portable code", Code::OpenFailed, nativeStatus.code()));
    if(nativeStatus.diagnostic() != nullptr && nativeStatus.diagnostic()->nativeCode.has_value())
    {
        static_cast<void>(
            context.expectEq("Native detail preserves its domain", NativeCodeDomain::Errno, nativeStatus.diagnostic()->nativeCode->domain));
        static_cast<void>(context.expectEq("Native detail preserves its value", std::int64_t{5}, nativeStatus.diagnostic()->nativeCode->value));
        static_cast<void>(
            context.expectEq("Native detail preserves its message", std::string_view{"open failed"}, nativeStatus.diagnostic()->message));
    }
    else
    {
        context.fail("Native detail is present", "the diagnostic or native code is missing");
    }

    const Status copied = nativeStatus;
    static_cast<void>(context.expectTrue("Status copies share immutable diagnostics", copied.diagnostic() == nativeStatus.diagnostic()));
    static_cast<void>(context.expectTrue(
        "Empty message does not allocate message-only detail",
        Status::withDiagnostic(Code::Cancelled, "").diagnostic() == nullptr));
}

void testValueResults(TestSupport::Context &context)
{
    const Result<int> success = Result<int>::success(42);
    static_cast<void>(context.expectTrue("Successful result is ok", success.ok()));
    static_cast<void>(context.expectTrue("Successful result has a value", success.hasValue()));
    static_cast<void>(context.expectEq("Successful result returns its value", 42, success.value()));
    static_cast<void>(context.expectEq("Successful result carries Success", Code::Success, success.status().code()));

    const Result<int> failure = Result<int>::failure(Status(Code::NotFound));
    static_cast<void>(context.expectFalse("Failed result is not ok", failure.ok()));
    static_cast<void>(context.expectFalse("Failed result without progress has no value", failure.hasValue()));
    static_cast<void>(context.expectEq("Failed result retains its outcome", Code::NotFound, failure.status().code()));

    const Result<std::vector<int>> partial = Result<std::vector<int>>::failure(Status(Code::PartialRead), {3, 5, 8});
    static_cast<void>(context.expectFalse("Partial result remains failed", partial.ok()));
    static_cast<void>(context.expectTrue("Partial result retains its payload", partial.hasValue()));
    static_cast<void>(context.expectEq("Partial result retains its status", Code::PartialRead, partial.status().code()));
    static_cast<void>(context.expectEq("Partial result retains its data", std::vector<int>{3, 5, 8}, partial.value()));

    Result<std::unique_ptr<int>> moveOnly = Result<std::unique_ptr<int>>::success(std::make_unique<int>(9));
    static_cast<void>(context.expectTrue("Move-only payload result is ok", moveOnly.ok()));
    static_cast<void>(context.expectTrue("Move-only payload is preserved", moveOnly.value() != nullptr && *moveOnly.value() == 9));
}

void testVoidResults(TestSupport::Context &context)
{
    const Result<void> success = Result<void>::success();
    static_cast<void>(context.expectTrue("Successful void result is ok", success.ok()));
    static_cast<void>(context.expectEq("Successful void result carries Success", Code::Success, success.status().code()));

    const Result<void> failure = Result<void>::failure(Status(Code::Cancelled));
    static_cast<void>(context.expectFalse("Failed void result is not ok", failure.ok()));
    static_cast<void>(context.expectEq("Failed void result retains its outcome", Code::Cancelled, failure.status().code()));
}
