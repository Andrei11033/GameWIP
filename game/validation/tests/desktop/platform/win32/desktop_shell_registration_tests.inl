/// @file desktop_shell_registration_tests.inl
/// @brief Win32 shell registration, Jump List, and launch tests.

namespace ShellTypes = Desktop::Types::Shell;
namespace TaskbarTypes = Desktop::Types::Taskbar;
namespace TrayTypes = Desktop::Types::Tray;
namespace NotificationTypes = Desktop::Types::Notifications;
namespace JumpListTypes = Desktop::Types::JumpLists;

#if DESKTOP_INTERNAL_TEST_HOOKS
/// @brief Publishes with an isolated application identity so normal application lists are untouched.
[[nodiscard]] int runJumpListPublicationChild(const std::filesystem::path &executable)
{
    const std::wstring appId = std::format(L"GameWIP.Validation.JumpLists.{}", GetCurrentProcessId());

    if(FAILED(SetCurrentProcessExplicitAppUserModelID(appId.c_str())))
    {
        return 2;
    }

    const std::filesystem::path absoluteExecutable = std::filesystem::absolute(executable);
    const std::array<ShellTypes::LaunchArgumentView, 1> arguments{ShellTypes::LiteralLaunchArgumentView{"--desktop-test-child=shell-launch-probe"}};
    const ShellTypes::LaunchActionView action{ShellTypes::PathTargetView{std::cref(absoluteExecutable)}, arguments};
    const std::array tasks{JumpListTypes::Task{"GameWIP publication regression", "Controlled launch probe", action}};
    JumpListTypes::Description description;
    description.tasks = tasks;
    const auto published = Desktop::JumpLists::publish(description);
    const auto cleared = Desktop::JumpLists::publish({});
    return !published.ok() ? 3 : !cleared.ok() ? 4 : 0;
}

void testJumpListNativePublication(TestSupport::Context &context, const std::filesystem::path &executable)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if(!context.expectTrue("metadata inspection STA initializes", SUCCEEDED(initialized)))
    {
        return;
    }

    struct ComInitialization
    {
        ~ComInitialization()
        {
            CoUninitialize();
        }
    } const initialization;
    const std::filesystem::path absoluteExecutable = std::filesystem::absolute(executable);
    const std::array<ShellTypes::LaunchArgumentView, 2> arguments{
        ShellTypes::LiteralLaunchArgumentView{"--desktop-test-child=shell-launch-probe"},
        ShellTypes::LiteralLaunchArgumentView{"a space and \"quoted\" argument"}};
    const ShellTypes::LaunchActionView action{ShellTypes::PathTargetView{std::cref(absoluteExecutable)}, arguments};
    const JumpListTypes::Task task{"Native title caf\xC3\xA9 \xF0\x9F\x99\x82", "Distinct task tooltip", action};
    void *rawLink = nullptr;
    const auto created = Win32TestHooks::createJumpLink(task, rawLink);
    if(!context.expectTrue("native jump link creates", created.ok() && rawLink != nullptr))
    {
        return;
    }
    const auto releaseLink = [](IShellLinkW *link)
    {
        link->Release();
    };
    const std::unique_ptr<IShellLinkW, decltype(releaseLink)> link(static_cast<IShellLinkW *>(rawLink), releaseLink);
    IPropertyStore *rawProperties = nullptr;

    if(context.expectTrue("jump link exposes property store", SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&rawProperties)))))
    {
        const auto releaseProperties = [](IPropertyStore *properties)
        {
            properties->Release();
        };
        const std::unique_ptr<IPropertyStore, decltype(releaseProperties)> properties(rawProperties, releaseProperties);
        PROPVARIANT title{};
        const HRESULT titleResult = properties->GetValue(PKEY_Title, &title);
        static_cast<void>(
            context.expectTrue("jump title is a string property", SUCCEEDED(titleResult) && title.vt == VT_LPWSTR && title.pwszVal != nullptr));

        if(title.vt == VT_LPWSTR && title.pwszVal != nullptr)
        {
            static_cast<void>(context.expectEq(
                "Unicode task title survives native property storage",
                manualDiagnosticWideText(task.title),
                std::wstring{title.pwszVal}));
        }
        static_cast<void>(PropVariantClear(&title));
    }

    std::array<wchar_t, 4096> text{};
    static_cast<void>(context.expectTrue("jump tooltip reads", SUCCEEDED(link->GetDescription(text.data(), static_cast<int>(text.size())))));
    static_cast<void>(
        context.expectEq("tooltip uses description independently from title", std::wstring{L"Distinct task tooltip"}, std::wstring{text.data()}));
    text.fill(L'\0');
    static_cast<void>(context.expectTrue("jump arguments read", SUCCEEDED(link->GetArguments(text.data(), static_cast<int>(text.size())))));
    static_cast<void>(context.expectEq(
        "literal launch arguments preserve spaces and quotes",
        std::wstring{L"\"--desktop-test-child=shell-launch-probe\" \"a space and \\\"quoted\\\" argument\""},
        std::wstring{text.data()}));
    text.fill(L'\0');
    int iconIndex = -1;
    static_cast<void>(
        context.expectTrue("jump icon location reads", SUCCEEDED(link->GetIconLocation(text.data(), static_cast<int>(text.size()), &iconIndex))));
    static_cast<void>(context.expectEq(
        "jump icon comes from executable",
        std::filesystem::path(absoluteExecutable).make_preferred().wstring(),
        std::wstring{text.data()}));
    static_cast<void>(context.expectEq("jump icon uses first executable icon", 0, iconIndex));

    const ShellTypes::LaunchActionView noArguments{ShellTypes::PathTargetView{std::cref(absoluteExecutable)}, {}};
    void *invalidLink = nullptr;
    static_cast<void>(context.expectEq(
        "Win32 jump links require launch arguments",
        ErrorCode::InvalidArgument,
        Win32TestHooks::createJumpLink({"Invalid argument-free task", {}, noArguments}, invalidLink).code));
    static_cast<void>(context.expectTrue("rejected jump link is not exposed", invalidLink == nullptr));

    TestSupport::Types::Process::Options child;
    child.executablePath = executable;
    child.arguments = {"--desktop-test-child=jump-list-publication"};
    child.waitTimeout = std::chrono::seconds{30};
    const auto result = TestSupport::runChildProcess(child);
    static_cast<void>(context.expectTrue("isolated jump-list publisher starts", result.status.ok()));
    static_cast<void>(context.expectEq("isolated jump-list publisher exits", TestSupport::Types::Process::Outcome::Exited, result.outcome));
    static_cast<void>(context.expectEq("valid jump-list publication and cleanup succeed", std::uint32_t{0}, result.exitCode));
}
#endif
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

/// @brief Exercises Explorer's handler discovery and native dispatch to a controlled child.
void testShellRegistrationLaunch(TestSupport::Context &context, const std::filesystem::path &executable)
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if(!context.expectTrue("association discovery enters STA", SUCCEEDED(apartment)))
    {
        return;
    }

    struct ApartmentCleanup
    {
        ~ApartmentCleanup()
        {
            CoUninitialize();
        }
    } const apartmentCleanup;
    // Always exercise generic separators, even when the test host was started by PowerShell.
    const std::filesystem::path absoluteExecutable = std::filesystem::absolute(executable);
    const std::string executableIdentity = std::format("{}-{}", GetCurrentProcessId(), GetTickCount64());
    const std::filesystem::path launchExecutable = absoluteExecutable.parent_path() / std::format("GameWIPLaunch-{}.exe", executableIdentity);
    std::error_code copyError;
    std::filesystem::copy_file(absoluteExecutable, launchExecutable, std::filesystem::copy_options::overwrite_existing, copyError);

    if(!context.expectTrue("shell launch executable fixture copies", !copyError))
    {
        return;
    }

    struct ExecutableCleanup
    {
        std::filesystem::path path;
        ~ExecutableCleanup()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } const executableCleanup{launchExecutable};

    // Register each association and resolve its selected target.
    for(const bool file : {true, false})
    {
        const std::string token = std::format("{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), file ? 1 : 2);
        const std::string target = std::format("{}gamewip-launch-{}", file ? "." : "", token);
        const std::string owner = "GameWIP.LaunchRegression." + token;
        const std::string tokenArgument = "--desktop-shell-launch-token=" + token;
        const std::string kind = file ? "file" : "URI";
        const TestSupport::ScopedTemporaryDirectory fixtures("desktop-association-launch");

        if(!context.expectTrue(kind + " launch fixture creates", fixtures.status().ok()))
        {
            continue;
        }
        const auto path = fixtures.path() / ("probe with spaces" + target);
        const std::wstring targetWide = manualDiagnosticWideText(target);
        // Use a canonical URI; Windows adds the root slash to a bare authority.
        const std::wstring launchTarget = file ? path.wstring() : targetWide + L"://probe/";
        const std::string expectedTargetArgument = "--desktop-shell-launch-expected-target=" + (file ? path.string() : target + "://probe/");
        const std::array<ShellTypes::LaunchArgumentView, 4> arguments{
            ShellTypes::LiteralLaunchArgumentView{"--desktop-test-child=shell-launch-probe"},
            ShellTypes::LiteralLaunchArgumentView{tokenArgument},
            ShellTypes::LiteralLaunchArgumentView{expectedTargetArgument},
            file ? ShellTypes::LaunchPlaceholder::SelectedPath : ShellTypes::LaunchPlaceholder::SelectedUri};
        const ShellTypes::LaunchActionView action{ShellTypes::PathTargetView{std::cref(launchExecutable)}, arguments};
        const auto receiptPath = shellLaunchReceiptPath(token);
        struct Cleanup
        {
            std::function<void()> remove;
            ~Cleanup()
            {
                remove();
            }
        } const cleanup{[&]
                        {
                            if(file)
                            {
                                static_cast<void>(Desktop::Registration::unregisterFileExtension(owner, target));
                            }
                            else
                            {
                                static_cast<void>(Desktop::Registration::unregisterUriScheme(owner, target));
                            }
                            static_cast<void>(TestSupport::removeIfExists(receiptPath));
                            static_cast<void>(TestSupport::removeIfExists(receiptPath.string() + ".arguments"));
                        }};
        RegistrationTypes::Result registration;

        if(file)
        {
            RegistrationTypes::FileExtensionDescription description;
            description.ownerKey = owner;
            description.extension = target;
            description.displayName = "GameWIP association launch regression";
            description.defaultAction = action;
            registration = Desktop::Registration::registerFileExtension(description);
        }
        else
        {
            RegistrationTypes::UriSchemeDescription description;
            description.ownerKey = owner;
            description.scheme = target;
            description.displayName = "GameWIP association launch regression";
            description.defaultAction = action;
            registration = Desktop::Registration::registerUriScheme(description);
        }

        if(!context.expectTrue(kind + " launch registration succeeds", registration.status.ok()))
        {
            continue;
        }
        std::wstring progId;

        if(file)
        {
            // Confirm the file handler appears in Windows Open With.
            IEnumAssocHandlers *handlers = nullptr;
            const HRESULT enumerated = SHAssocEnumHandlers(targetWide.c_str(), ASSOC_FILTER_NONE, &handlers);
            bool discovered = false;
            bool named = false;
            bool recommended = false;

            if(SUCCEEDED(enumerated) && handlers != nullptr)
            {
                IAssocHandler *handler = nullptr;

                while(handlers->Next(1, &handler, nullptr) == S_OK)
                {
                    PWSTR name = nullptr;

                    if(SUCCEEDED(handler->GetName(&name)) && name != nullptr)
                    {
                        if(std::filesystem::path{name} == launchExecutable)
                        {
                            discovered = true;
                            PWSTR uiName = nullptr;
                            const HRESULT nameStatus = handler->GetUIName(&uiName);
                            named =
                                SUCCEEDED(nameStatus) && uiName != nullptr && std::wstring_view{uiName} == L"GameWIP association launch regression";
                            context.info(
                                std::format(
                                    "handler display name: HRESULT={} matches={} recommended={}",
                                    nameStatus,
                                    named,
                                    handler->IsRecommended()));
                            CoTaskMemFree(uiName);
                            recommended = handler->IsRecommended() == S_OK;
                        }
                        CoTaskMemFree(name);
                    }
                    handler->Release();
                }
                handlers->Release();
            }
            static_cast<void>(context.expectTrue("Windows Open with enumerates the registered executable", discovered));
            static_cast<void>(context.expectTrue("Windows Open with resolves the registration display name", named));
            static_cast<void>(context.expectTrue("Windows Open with recommends the registered handler", recommended));
            std::array<wchar_t, 4096> value{};
            DWORD size = sizeof(value);
            const auto key = L"Software\\Classes\\" + targetWide;

            if(!context.expectEq(
                   "file dispatch ProgID reads",
                   LONG{ERROR_SUCCESS},
                   RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &size)))
            {
                continue;
            }
            progId = value.data();
        }

        if(file && !context.expectTrue("launch file writes", TestSupport::writeTextFile(path, "controlled launch").ok()))
        {
            continue;
        }

        // Dispatch the target and verify the child receives it intact.
        SHELLEXECUTEINFOW launch{};
        launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;

        if(file)
        {
            launch.fMask |= SEE_MASK_CLASSNAME;
            launch.lpClass = progId.c_str();
        }
        launch.lpVerb = L"open";
        launch.lpFile = launchTarget.c_str();
        launch.nShow = SW_HIDE;
        const bool launched = ShellExecuteExW(&launch) != FALSE;
        context.info(std::format("{} ShellExecuteEx: success={} native={}", kind, launched, launched ? 0 : GetLastError()));
        static_cast<void>(context.expectTrue(kind + " Windows dispatch launches the handler", launched));

        if(launch.hProcess != nullptr)
        {
            // URI dispatch may cross Explorer's broker before the controlled process exists;
            // sanitizer-instrumented binaries can make that broker handoff exceed the normal
            // launch latency while still remaining a valid launch.
            constexpr DWORD shellLaunchTimeoutMilliseconds = 30000;
            const DWORD finished = WaitForSingleObject(launch.hProcess, shellLaunchTimeoutMilliseconds);
            static_cast<void>(context.expectEq(kind + " controlled handler completes", (WAIT_OBJECT_0), finished));
            DWORD exitCode = 0;
            static_cast<void>(context.expectTrue(
                kind + " selected target arrives as one exact argument",
                GetExitCodeProcess(launch.hProcess, &exitCode) != FALSE && exitCode == 0));
            CloseHandle(launch.hProcess);
        }
        const auto receipt = TestSupport::readTextFile(receiptPath);
        const auto receivedArguments = TestSupport::readTextFile(receiptPath.string() + ".arguments");

        if(receivedArguments.status.ok())
        {
            context.info(kind + " received arguments:\n" + receivedArguments.text);
        }
        static_cast<void>(context.expectTrue(kind + " controlled launch receipt matches", launched && receipt.status.ok() && receipt.text == token));
    }
}
#endif
