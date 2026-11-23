/// @file desktop_apartment_tests.inl
/// @brief Dialog COM apartment contract tests.

#if DESKTOP_INTERNAL_TEST_HOOKS

struct ApartmentObservation
{
    HRESULT result = E_FAIL;
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
};

[[nodiscard]] ApartmentObservation observeApartment() noexcept
{
    ApartmentObservation observation;
    observation.result = CoGetApartmentType(&observation.type, &observation.qualifier);
    return observation;
}

/// @brief Keeps the strictly uninitialized case independent of assistive clients in the parent.
[[nodiscard]] int runColdApartmentChild()
{
    if(observeApartment().result != CO_E_NOTINITIALIZED)
    {
        return 2;
    }

    if(!Win32TestHooks::testDialogApartment().ok() || observeApartment().result != CO_E_NOTINITIALIZED)
    {
        return 3;
    }

    return Win32TestHooks::testShellApartment().ok() && observeApartment().result == CO_E_NOTINITIALIZED ? 0 : 4;
}

/// ------------------------------------------------------------
/// Dialog apartment contracts
/// ------------------------------------------------------------

void testDialogApartmentContracts(TestSupport::Context &context, const std::filesystem::path &executable)
{
    // Verify apartment leases balance in a process with no prior COM clients.
    TestSupport::Types::Process::Options cold;
    cold.executablePath = executable;
    cold.arguments = {"--desktop-test-child=cold-com-apartment"};
    cold.waitTimeout = std::chrono::seconds{30};
    const auto coldResult = TestSupport::runChildProcess(cold);
    static_cast<void>(context.expectTrue("isolated COM probe starts", coldResult.status.ok()));
    static_cast<void>(context.expectEq("isolated COM probe exits", TestSupport::Types::Process::Outcome::Exited, coldResult.outcome));
    static_cast<void>(context.expectEq("uninitialized leases balance in a clean process", std::uint32_t{0}, coldResult.exitCode));

    struct MtaUsage
    {
        CO_MTA_USAGE_COOKIE cookie = nullptr;
        HRESULT result = CoIncrementMTAUsage(&cookie);
        ~MtaUsage()
        {
            if(cookie != nullptr)
            {
                static_cast<void>(CoDecrementMTAUsage(cookie));
            }
        }
    } const mtaUsage;

    if(!context.expectTrue("controlled process MTA is available", SUCCEEDED(mtaUsage.result)))
    {
        return;
    }

    // Compare implicit MTA, caller-owned STA, and explicit MTA behavior.
    using Probe = IO::Types::Status (*)() noexcept;
    const std::array probes{
        std::pair<std::string_view, Probe>{"dialogs", &Win32TestHooks::testDialogApartment},
        std::pair<std::string_view, Probe>{"shell", &Win32TestHooks::testShellApartment}};

    for(const auto &[name, probe] : probes)
    {
        ApartmentObservation before;
        ApartmentObservation after;
        IO::Types::Status leaseStatus;
        std::thread implicit(
            [&]
            {
                before = observeApartment();
                leaseStatus = probe();
                after = observeApartment();
            });
        implicit.join();
        context.info(
            std::format(
                "{} implicit COM before: result={} type={} qualifier={}; after: result={} type={} qualifier={}",
                name,
                before.result,
                static_cast<int>(before.type),
                static_cast<int>(before.qualifier),
                after.result,
                static_cast<int>(after.type),
                static_cast<int>(after.qualifier)));
        static_cast<void>(context.expectEq(std::format("{} fresh thread sees process MTA", name), S_OK, before.result));
        static_cast<void>(context.expectEq(std::format("{} MTA is implicit", name), APTTYPEQUALIFIER_IMPLICIT_MTA, before.qualifier));
        static_cast<void>(context.expectTrue(std::format("{} implicit MTA can acquire STA lease", name), leaseStatus.ok()));
        static_cast<void>(context.expectEq(std::format("{} balances lease HRESULT", name), before.result, after.result));
        static_cast<void>(context.expectEq(std::format("{} balances apartment type", name), before.type, after.type));
        static_cast<void>(context.expectEq(std::format("{} balances apartment qualifier", name), before.qualifier, after.qualifier));

        // Reuse a caller-owned STA without changing its initialization state.
        HRESULT initialized = E_FAIL;
        std::thread sta(
            [&]
            {
                initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

                if(SUCCEEDED(initialized))
                {
                    before = observeApartment();
                    leaseStatus = probe();
                    after = observeApartment();
                    CoUninitialize();
                }
            });
        sta.join();
        static_cast<void>(context.expectTrue(std::format("{} caller STA initializes", name), SUCCEEDED(initialized)));
        static_cast<void>(context.expectTrue(std::format("{} existing STA is reused", name), leaseStatus.ok()));
        static_cast<void>(context.expectEq(std::format("{} preserves caller STA initialization", name), S_OK, after.result));
        static_cast<void>(context.expectEq(std::format("{} preserves caller STA type", name), before.type, after.type));

        // Reject explicit MTA calls without disturbing the caller's apartment.
        initialized = E_FAIL;
        std::thread mta(
            [&]
            {
                initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                if(SUCCEEDED(initialized))
                {
                    leaseStatus = probe();
                    after = observeApartment();
                    CoUninitialize();
                }
            });
        mta.join();
        static_cast<void>(context.expectTrue(std::format("{} caller MTA initializes", name), SUCCEEDED(initialized)));
        static_cast<void>(context.expectEq(std::format("{} explicit MTA is rejected", name), ErrorCode::ResourceBusy, leaseStatus.code));
        static_cast<void>(context.expectEq(std::format("{} preserves caller MTA initialization", name), S_OK, after.result));
        static_cast<void>(context.expectEq(std::format("{} preserves caller MTA type", name), APTTYPE_MTA, after.type));
        static_cast<void>(context.expectEq(std::format("{} preserves explicit MTA qualifier", name), APTTYPEQUALIFIER_NONE, after.qualifier));
    }
}

#endif
