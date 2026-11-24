/// @file desktop_shell_launch_tests.inl
/// @brief Shell association discovery and launch tests.

#if DESKTOP_INTERNAL_TEST_HOOKS

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

        std::string pathText;

        if(file)
        {
            pathText = GameWIP::Test::Internal::pathToUtf8(context, path, "association fixture path converts to UTF-8");

            if(pathText.empty())
            {
                continue;
            }
        }

        // Use a canonical URI; Windows adds the root slash to a bare authority.
        const std::wstring launchTarget = file ? path.wstring() : targetWide + L"://probe/";
        const std::string expectedTargetArgument = "--desktop-shell-launch-expected-target=" + (file ? pathText : target + "://probe/");
        const std::array<ShellTypes::LaunchArgumentView, 4> arguments{
            ShellTypes::LiteralLaunchArgumentView{"--desktop-test-child=shell-launch-probe"},
            ShellTypes::LiteralLaunchArgumentView{tokenArgument},
            ShellTypes::LiteralLaunchArgumentView{expectedTargetArgument},
            file ? ShellTypes::LaunchPlaceholder::SelectedPath : ShellTypes::LaunchPlaceholder::SelectedUri};
        const ShellTypes::LaunchActionView action{ShellTypes::PathTargetView{std::cref(launchExecutable)}, arguments};
        const auto receiptPath = shellLaunchReceiptPath(token);
        std::filesystem::path argumentsPath = receiptPath;
        argumentsPath += ".arguments";
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
                            static_cast<void>(TestSupport::removeIfExists(argumentsPath));
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
        const auto receivedArguments = TestSupport::readTextFile(argumentsPath);

        if(receivedArguments.status.ok())
        {
            context.info(kind + " received arguments:\n" + receivedArguments.text);
        }
        static_cast<void>(context.expectTrue(kind + " controlled launch receipt matches", launched && receipt.status.ok() && receipt.text == token));
    }
}
#endif
