/// @file desktop_shell_application_registration_tests.inl
/// @brief Shell application-record ownership and cleanup tests.

#if DESKTOP_INTERNAL_TEST_HOOKS
/// ------------------------------------------------------------
/// Application registration ownership
/// ------------------------------------------------------------

/// @brief Verifies application-record ownership conflicts and retryable registry cleanup failures.
void testShellRegistrationApplicationOwnership(TestSupport::Context &context, const std::filesystem::path &executable)
{
    // Prepare a unique executable identity and registry fixtures.
    const TestSupport::ScopedTemporaryDirectory fixtures("desktop-shell-application-ownership");

    if(!context.expectTrue("application ownership fixture creates", fixtures.status().ok()))
    {
        return;
    }

    const std::filesystem::path absoluteExecutable = std::filesystem::absolute(executable);
    const std::string identity = std::format("{}-{}", GetCurrentProcessId(), GetTickCount64());
    const std::filesystem::path applicationExecutable = fixtures.path() / std::format("GameWIPCollision-{}.exe", identity);
    std::error_code copyError;
    std::filesystem::copy_file(absoluteExecutable, applicationExecutable, std::filesystem::copy_options::overwrite_existing, copyError);

    if(!context.expectTrue("application ownership executable fixture copies", !copyError))
    {
        return;
    }

    const std::array<ShellTypes::LaunchArgumentView, 1> arguments{ShellTypes::LiteralLaunchArgumentView{"--desktop-test-child=shell-launch-probe"}};
    const ShellTypes::LaunchActionView action{ShellTypes::PathTargetView{std::cref(applicationExecutable)}, arguments};
    const std::string target = ".gamewip-application-conflict-" + identity;
    const std::string owner = "GameWIP.ApplicationConflict." + identity;
    const std::string foreignOwner = owner + ".Foreign";
    const std::wstring applicationName = applicationExecutable.filename().wstring();
    const std::wstring applicationPath = L"Software\\Classes\\Applications\\" + applicationName;
    const std::wstring targetPath = L"Software\\Classes\\" + manualDiagnosticWideText(target);

    const auto removeApplicationFixture = [&]
    {
        const LONG removed = RegDeleteTreeW(HKEY_CURRENT_USER, applicationPath.c_str());
        return removed == ERROR_SUCCESS || removed == ERROR_FILE_NOT_FOUND;
    };
    struct Cleanup
    {
        std::function<void()> remove;
        ~Cleanup()
        {
            remove();
        }
    } const cleanup{[&]
                    {
                        Desktop::Internal::TestHooks::resetFailures();
                        static_cast<void>(Desktop::Registration::unregisterFileExtension(owner, target));
                        static_cast<void>(removeApplicationFixture());
                    }};

    const auto seedApplication = [&](bool withOwner)
    {
        HKEY application = nullptr;
        DWORD disposition = 0;
        const LONG created = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            applicationPath.c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_WRITE,
            nullptr,
            &application,
            &disposition);

        if(created != ERROR_SUCCESS || application == nullptr)
        {
            if(application != nullptr)
            {
                RegCloseKey(application);
            }

            return false;
        }
        LONG ownerResult = ERROR_SUCCESS;

        if(withOwner)
        {
            const std::wstring foreignOwnerWide = manualDiagnosticWideText(foreignOwner);
            ownerResult = RegSetValueExW(
                application,
                L"GameWIP.OwnerKey",
                0,
                REG_SZ,
                reinterpret_cast<const BYTE *>(foreignOwnerWide.c_str()),
                static_cast<DWORD>((foreignOwnerWide.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(application);
        return ownerResult == ERROR_SUCCESS;
    };

    const auto registerDescription = [&]
    {
        RegistrationTypes::FileExtensionDescription description;
        description.ownerKey = owner;
        description.extension = target;
        description.displayName = "GameWIP application ownership regression";
        description.icon = RegistrationTypes::Icon{ShellTypes::PathTargetView{std::cref(applicationExecutable)}, 0};
        description.defaultAction = action;
        description.conflictPolicy = RegistrationTypes::ConflictPolicy::Coexist;
        return Desktop::Registration::registerFileExtension(description);
    };

    // Reject foreign and unowned application records without mutating them.
    if(!context.expectTrue("foreign application fixture seeds", seedApplication(true)))
    {
        return;
    }

    const auto foreignCollision = registerDescription();
    static_cast<void>(context.expectEq("foreign application ownership blocks registration", ErrorCode::AlreadyExists, foreignCollision.status.code));
    static_cast<void>(context.expectEq("foreign application conflict is reported", std::size_t{1}, foreignCollision.conflicts.size()));

    if(foreignCollision.conflicts.size() == 1)
    {
        static_cast<void>(context.expectEq(
            "foreign application conflict has the application kind",
            RegistrationTypes::TargetKind::ApplicationRecord,
            foreignCollision.conflicts.front().kind));
        static_cast<void>(context.expectEq(
            "foreign application conflict names the executable",
            applicationExecutable.filename().string(),
            foreignCollision.conflicts.front().target));
        static_cast<void>(
            context.expectEq("foreign application conflict reports its owner", foreignOwner, foreignCollision.conflicts.front().ownerKey));
    }
    HKEY targetKey = nullptr;
    const LONG targetStatus = RegOpenKeyExW(HKEY_CURRENT_USER, targetPath.c_str(), 0, KEY_READ, &targetKey);

    if(targetKey != nullptr)
    {
        RegCloseKey(targetKey);
    }
    static_cast<void>(context.expectEq("foreign application collision leaves target absent", LONG{ERROR_FILE_NOT_FOUND}, targetStatus));
    std::array<wchar_t, 128> value{};
    DWORD bytes = static_cast<DWORD>(sizeof(value));
    static_cast<void>(context.expectEq(
        "foreign application collision does not add FriendlyAppName",
        LONG{ERROR_FILE_NOT_FOUND},
        RegGetValueW(HKEY_CURRENT_USER, applicationPath.c_str(), L"FriendlyAppName", RRF_RT_REG_SZ, nullptr, value.data(), &bytes)));
    value.fill(L'\0');
    bytes = static_cast<DWORD>(sizeof(value));
    static_cast<void>(context.expectEq(
        "foreign application collision does not add SupportedTypes",
        LONG{ERROR_FILE_NOT_FOUND},
        RegGetValueW(
            HKEY_CURRENT_USER,
            (applicationPath + L"\\SupportedTypes").c_str(),
            manualDiagnosticWideText(target).c_str(),
            RRF_RT_REG_SZ,
            nullptr,
            value.data(),
            &bytes)));

    static_cast<void>(context.expectTrue("foreign application fixture removes", removeApplicationFixture()));
    const auto unownedSeed = seedApplication(false);
    static_cast<void>(context.expectTrue("unowned application fixture seeds", unownedSeed));
    const auto unownedCollision = registerDescription();
    static_cast<void>(context.expectEq("unowned application record blocks registration", ErrorCode::AlreadyExists, unownedCollision.status.code));
    static_cast<void>(context.expectEq("unowned application conflict is reported", std::size_t{1}, unownedCollision.conflicts.size()));

    if(unownedCollision.conflicts.size() == 1)
    {
        static_cast<void>(context.expectTrue("unowned application conflict has no owner", unownedCollision.conflicts.front().ownerKey.empty()));
    }
    static_cast<void>(context.expectTrue("unowned application fixture removes", removeApplicationFixture()));

    // Verify cleanup failure retains enough state for a retry.
    const auto created = registerDescription();
    if(!context.expectTrue("application registration succeeds after conflict removal", created.status.ok()))
    {
        return;
    }

    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ShellRegistryDelete);
    const auto failedCleanup = Desktop::Registration::unregisterFileExtension(owner, target);
    static_cast<void>(context.expectEq("registry deletion failure is propagated", ErrorCode::PermissionDenied, failedCleanup.status.code));
    Desktop::Internal::TestHooks::resetFailures();
    HKEY retained = nullptr;
    const LONG retainedStatus = RegOpenKeyExW(HKEY_CURRENT_USER, applicationPath.c_str(), 0, KEY_READ, &retained);

    if(retained != nullptr)
    {
        RegCloseKey(retained);
    }
    static_cast<void>(context.expectEq("failed cleanup retains application record for retry", LONG{ERROR_SUCCESS}, retainedStatus));
    HKEY retainedTarget = nullptr;
    const LONG retainedTargetStatus = RegOpenKeyExW(HKEY_CURRENT_USER, targetPath.c_str(), 0, KEY_READ, &retainedTarget);

    if(retainedTarget != nullptr)
    {
        RegCloseKey(retainedTarget);
    }
    static_cast<void>(context.expectEq("failed cleanup retains association for retry", LONG{ERROR_SUCCESS}, retainedTargetStatus));
    value.fill(L'\0');
    bytes = static_cast<DWORD>(sizeof(value));
    static_cast<void>(context.expectEq(
        "failed cleanup restores application supported type",
        LONG{ERROR_SUCCESS},
        RegGetValueW(
            HKEY_CURRENT_USER,
            (applicationPath + L"\\SupportedTypes").c_str(),
            manualDiagnosticWideText(target).c_str(),
            RRF_RT_REG_SZ,
            nullptr,
            value.data(),
            &bytes)));
    static_cast<void>(context.expectTrue("cleanup retry succeeds", Desktop::Registration::unregisterFileExtension(owner, target).status.ok()));
    HKEY removed = nullptr;
    const LONG removedStatus = RegOpenKeyExW(HKEY_CURRENT_USER, applicationPath.c_str(), 0, KEY_READ, &removed);

    if(removed != nullptr)
    {
        RegCloseKey(removed);
    }
    static_cast<void>(context.expectEq("cleanup retry removes application record", LONG{ERROR_FILE_NOT_FOUND}, removedStatus));
}

#endif
