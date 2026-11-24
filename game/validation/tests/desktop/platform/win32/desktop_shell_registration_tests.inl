/// @file desktop_shell_registration_tests.inl
/// @brief Shell registration ownership and cleanup tests.

#if DESKTOP_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// Shell registration and launch
/// ------------------------------------------------------------

void testShellRegistrationOwnership(TestSupport::Context &context, const std::filesystem::path &executable)
{
    // Create isolated executables for owner and conflict scenarios.
    const TestSupport::ScopedTemporaryDirectory fixtures("desktop-shell-registration-ownership");

    if(!context.expectTrue("shell registration ownership fixture creates", fixtures.status().ok()))
    {
        return;
    }

    const std::filesystem::path absoluteExecutable = std::filesystem::absolute(executable);
    const std::string identity = std::format("{}-{}", GetCurrentProcessId(), GetTickCount64());
    const std::filesystem::path applicationExecutable = fixtures.path() / std::format("GameWIPRegistration-{}.exe", identity);
    const std::filesystem::path foreignApplicationExecutable = fixtures.path() / std::format("GameWIPForeign-{}.exe", identity);
    std::error_code copyError;
    std::filesystem::copy_file(absoluteExecutable, applicationExecutable, std::filesystem::copy_options::overwrite_existing, copyError);

    if(!context.expectTrue("shell registration primary executable fixture copies", !copyError))
    {
        return;
    }
    copyError.clear();
    std::filesystem::copy_file(absoluteExecutable, foreignApplicationExecutable, std::filesystem::copy_options::overwrite_existing, copyError);

    if(!context.expectTrue("shell registration foreign executable fixture copies", !copyError))
    {
        return;
    }
    const std::array<ShellTypes::LaunchArgumentView, 1> arguments{ShellTypes::LiteralLaunchArgumentView{"--desktop-test-child=shell-launch-probe"}};
    const ShellTypes::LaunchActionView action{ShellTypes::PathTargetView{std::cref(applicationExecutable)}, arguments};
    const ShellTypes::LaunchActionView foreignAction{ShellTypes::PathTargetView{std::cref(foreignApplicationExecutable)}, arguments};
    const std::string owner = std::format("GameWIP.NativeRegistration.{}.caf\xC3\xA9", identity);
    const std::string foreignOwner = owner + ".Foreign";

    // Exercise file and URI registration with each conflict policy.
    for(const bool file : {true, false})
    {
        const std::string target = std::format("{}gamewip-regression-{}", file ? "." : "", identity);
        const auto unregister = [&](std::string_view key)
        {
            return file ? Desktop::Registration::unregisterFileExtension(key, target) : Desktop::Registration::unregisterUriScheme(key, target);
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
                            static_cast<void>(unregister(foreignOwner));
                            static_cast<void>(unregister(owner));
                        }};
        const RegistrationTypes::Verb malformedVerb{"invalid-label", std::string_view{"\xC3", 1}, action};
        const auto registerOwner = [&](std::string_view key,
                                       RegistrationTypes::ConflictPolicy policy,
                                       const ShellTypes::LaunchActionView &registrationAction,
                                       bool failAfterMutation = false)
        {
            if(file)
            {
                RegistrationTypes::FileExtensionDescription description;
                description.ownerKey = key;
                description.extension = target;
                description.displayName = failAfterMutation ? "Mutated before rollback" : "GameWIP native registration regression";
                description.defaultAction = registrationAction;

                if(failAfterMutation)
                {
                    description.verbs = std::span{&malformedVerb, 1};
                }
                description.conflictPolicy = policy;
                return Desktop::Registration::registerFileExtension(description);
            }

            RegistrationTypes::UriSchemeDescription description;
            description.ownerKey = key;
            description.scheme = target;
            description.displayName = failAfterMutation ? "Mutated before rollback" : "GameWIP native registration regression";
            description.defaultAction = registrationAction;

            if(failAfterMutation)
            {
                description.verbs = std::span{&malformedVerb, 1};
            }
            description.conflictPolicy = policy;
            return Desktop::Registration::registerUriScheme(description);
        };
        const std::string kind = file ? "file" : "URI";
        const auto first = registerOwner(owner, RegistrationTypes::ConflictPolicy::Coexist, action);

        if(!context.expectTrue(kind + " registration creates", first.status.ok()))
        {
            continue;
        }

        if(file)
        {
            // The modern Open With surface uses the executable application record as
            // its discovery metadata. Keep these checks beside the ownership test so
            // a future registration change cannot silently drop that record again.
            const std::wstring applicationName = applicationExecutable.filename().wstring();
            const std::wstring targetRegistrationPath = L"Software\\Classes\\" + manualDiagnosticWideText(target);
            std::array<wchar_t, 4096> value{};
            DWORD bytes = static_cast<DWORD>(sizeof(value));
            const LONG progIdRead =
                RegGetValueW(HKEY_CURRENT_USER, targetRegistrationPath.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &bytes);

            if(context.expectEq("file registration exposes a ProgID", LONG{ERROR_SUCCESS}, progIdRead))
            {
                const std::wstring programPath = L"Software\\Classes\\" + std::wstring{value.data()};
                value.fill(L'\0');
                bytes = static_cast<DWORD>(sizeof(value));
                static_cast<void>(context.expectEq(
                    "file ProgID has direct FriendlyAppName",
                    LONG{ERROR_SUCCESS},
                    RegGetValueW(HKEY_CURRENT_USER, programPath.c_str(), L"FriendlyAppName", RRF_RT_REG_SZ, nullptr, value.data(), &bytes)));
                static_cast<void>(context.expectEq(
                    "file ProgID FriendlyAppName matches display",
                    std::wstring{L"GameWIP native registration regression"},
                    std::wstring{value.data()}));
                value.fill(L'\0');
                bytes = static_cast<DWORD>(sizeof(value));
                static_cast<void>(context.expectEq(
                    "file ProgID records application executable",
                    LONG{ERROR_SUCCESS},
                    RegGetValueW(HKEY_CURRENT_USER, programPath.c_str(), L"GameWIP.ApplicationKey", RRF_RT_REG_SZ, nullptr, value.data(), &bytes)));
                static_cast<void>(context.expectEq("file ProgID application executable is basename", applicationName, std::wstring{value.data()}));
            }
            const std::wstring applicationPath = L"Software\\Classes\\Applications\\" + applicationName;
            value.fill(L'\0');
            bytes = static_cast<DWORD>(sizeof(value));
            static_cast<void>(context.expectEq(
                "file application record has FriendlyAppName",
                LONG{ERROR_SUCCESS},
                RegGetValueW(HKEY_CURRENT_USER, applicationPath.c_str(), L"FriendlyAppName", RRF_RT_REG_SZ, nullptr, value.data(), &bytes)));
            static_cast<void>(context.expectEq(
                "file application FriendlyAppName matches display",
                std::wstring{L"GameWIP native registration regression"},
                std::wstring{value.data()}));
            const std::wstring supportedTypePath = applicationPath + L"\\SupportedTypes";
            value.fill(L'\0');
            bytes = static_cast<DWORD>(sizeof(value));
            static_cast<void>(context.expectEq(
                "file application record advertises extension",
                LONG{ERROR_SUCCESS},
                RegGetValueW(
                    HKEY_CURRENT_USER,
                    supportedTypePath.c_str(),
                    manualDiagnosticWideText(target).c_str(),
                    RRF_RT_REG_SZ,
                    nullptr,
                    value.data(),
                    &bytes)));
            const std::wstring commandPath = applicationPath + L"\\shell\\open\\command";
            value.fill(L'\0');
            bytes = static_cast<DWORD>(sizeof(value));
            const LONG commandRead = RegGetValueW(HKEY_CURRENT_USER, commandPath.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &bytes);
            static_cast<void>(context.expectTrue(
                "file application record has a selected-path launch command",
                commandRead == ERROR_SUCCESS && std::wstring_view{value.data()}.contains(applicationExecutable.wstring()) &&
                    std::wstring_view{value.data()}.contains(L"%1")));
        }

        const auto replaced = registerOwner(owner, RegistrationTypes::ConflictPolicy::ReplaceOwned, action);
        context.info(
            std::format(
                "{} replacement: portable={} native={} {}",
                kind,
                static_cast<int>(replaced.status.code),
                replaced.status.nativeCode,
                replaced.status.message));
        static_cast<void>(context.expectTrue(kind + " same-owner replacement succeeds", replaced.status.ok() && replaced.replacedOwned));
        static_cast<void>(context.expectTrue(kind + " same-owner replacement has no foreign conflicts", replaced.conflicts.empty()));

        // Verify failed replacement restores the previous registry state.
        const auto failed = registerOwner(owner, RegistrationTypes::ConflictPolicy::ReplaceOwned, action, true);
        static_cast<void>(context.expectEq(kind + " malformed verb fails after mutation", ErrorCode::InvalidArgument, failed.status.code));
        std::array<wchar_t, 4096> registryText{};
        DWORD registryBytes = static_cast<DWORD>(sizeof(registryText));
        const std::wstring targetPath = L"Software\\Classes\\" + manualDiagnosticWideText(target);
        const LONG targetRead = RegGetValueW(
            HKEY_CURRENT_USER,
            targetPath.c_str(),
            file ? nullptr : L"GameWIP.ProgId",
            RRF_RT_REG_SZ,
            nullptr,
            registryText.data(),
            &registryBytes);

        if(context.expectEq(kind + " rollback restores target ProgID", LONG{ERROR_SUCCESS}, targetRead))
        {
            const std::wstring programPath = L"Software\\Classes\\" + std::wstring{registryText.data()};
            registryText.fill(L'\0');
            registryBytes = static_cast<DWORD>(sizeof(registryText));
            static_cast<void>(context.expectEq(
                kind + " restored program reads",
                LONG{ERROR_SUCCESS},
                RegGetValueW(HKEY_CURRENT_USER, programPath.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, registryText.data(), &registryBytes)));
            static_cast<void>(context.expectEq(
                kind + " failed replacement restores previous display name",
                std::wstring{L"GameWIP native registration regression"},
                std::wstring{registryText.data()}));
        }

        // Verify foreign owners are reported and remain protected during cleanup.
        const auto rejected = registerOwner(foreignOwner, RegistrationTypes::ConflictPolicy::Report, foreignAction);
        static_cast<void>(context.expectEq(kind + " foreign replacement is rejected", ErrorCode::AlreadyExists, rejected.status.code));
        static_cast<void>(context.expectEq(kind + " reports one foreign conflict", std::size_t{1}, rejected.conflicts.size()));

        if(rejected.conflicts.size() == 1)
        {
            static_cast<void>(context.expectEq(kind + " conflict retains exact Unicode owner", owner, rejected.conflicts.front().ownerKey));
        }
        const auto foreignCleanup = unregister(foreignOwner);
        static_cast<void>(
            context.expectEq(kind + " foreign cleanup cannot remove registration", ErrorCode::AlreadyExists, foreignCleanup.status.code));
        const auto coexist = registerOwner(foreignOwner, RegistrationTypes::ConflictPolicy::Coexist, foreignAction);
        static_cast<void>(context.expectTrue(kind + " foreign handler coexists", coexist.status.ok() && coexist.conflicts.size() == 1));
        static_cast<void>(context.expectTrue(kind + " coexisting handler cleanup succeeds", unregister(foreignOwner).status.ok()));
        const auto retained = registerOwner(owner, RegistrationTypes::ConflictPolicy::ReplaceOwned, action);
        static_cast<void>(context.expectTrue(kind + " original owner remains after foreign cleanup", retained.status.ok() && retained.replacedOwned));

        if(file)
        {
            static_cast<void>(context.expectTrue(
                "file alternative republishes",
                registerOwner(foreignOwner, RegistrationTypes::ConflictPolicy::Coexist, foreignAction).status.ok()));
        }
        static_cast<void>(context.expectTrue(kind + " owner cleanup succeeds", unregister(owner).status.ok()));

        if(file)
        {
            // Confirm the application record remains until its final handler is removed.
            const std::wstring applicationName = applicationExecutable.filename().wstring();
            const std::wstring applicationPath = L"Software\\Classes\\Applications\\" + applicationName;
            HKEY application = nullptr;
            const LONG applicationStatus = RegOpenKeyExW(HKEY_CURRENT_USER, applicationPath.c_str(), 0, KEY_READ, &application);

            if(application != nullptr)
            {
                RegCloseKey(application);
            }
            static_cast<void>(
                context.expectEq("last owned file handler cleanup removes application record", LONG{ERROR_FILE_NOT_FOUND}, applicationStatus));
            HKEY alternatives = nullptr;
            const std::wstring alternativesPath = targetPath + L"\\OpenWithProgids";
            const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, alternativesPath.c_str(), 0, KEY_READ, &alternatives);
            DWORD values = 0;

            if(opened == ERROR_SUCCESS)
            {
                RegQueryInfoKeyW(alternatives, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
                RegCloseKey(alternatives);
            }
            static_cast<void>(
                context.expectTrue("primary-owner cleanup preserves foreign Open with handler", opened == ERROR_SUCCESS && values == 1));
            static_cast<void>(context.expectTrue("last coexisting file handler cleanup succeeds", unregister(foreignOwner).status.ok()));
            HKEY remaining = nullptr;
            const LONG remainingStatus = RegOpenKeyExW(HKEY_CURRENT_USER, targetPath.c_str(), 0, KEY_READ, &remaining);

            if(remaining != nullptr)
            {
                RegCloseKey(remaining);
            }
            static_cast<void>(context.expectEq("last handler cleanup removes empty extension key", LONG{ERROR_FILE_NOT_FOUND}, remainingStatus));
        }
        static_cast<void>(context.expectEq(kind + " removed registration is absent", ErrorCode::NotFound, unregister(owner).status.code));
    }
}
#endif
