/// @file desktop_jump_list_tests.inl
/// @brief Jump List publication validation tests.

#if DESKTOP_INTERNAL_TEST_HOOKS
/// @brief Publishes with an isolated application identity so normal application lists are untouched.
[[nodiscard]] int runJumpListPublicationChild(const std::filesystem::path &executable)
{
    // Keep publication away from the user's normal application list.
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

    if(!published.ok())
    {
        return 3;
    }

    if(!cleared.ok())
    {
        return 4;
    }

    return 0;
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
    // Build the action and inspect its native properties.
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

    if(context.expectTrue(
           "jump link exposes property store",
           SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&rawProperties))) && rawProperties != nullptr))
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

    // Verify tooltip, arguments, and icon metadata.
    std::array<wchar_t, 4096> text{};
    if(context.expectTrue("jump tooltip reads", SUCCEEDED(link->GetDescription(text.data(), static_cast<int>(text.size())))))
    {
        static_cast<void>(
            context.expectEq("tooltip uses description independently from title", std::wstring{L"Distinct task tooltip"}, std::wstring{text.data()}));
    }
    text.fill(L'\0');
    if(context.expectTrue("jump arguments read", SUCCEEDED(link->GetArguments(text.data(), static_cast<int>(text.size())))))
    {
        static_cast<void>(context.expectEq(
            "literal launch arguments preserve spaces and quotes",
            std::wstring{L"\"--desktop-test-child=shell-launch-probe\" \"a space and \\\"quoted\\\" argument\""},
            std::wstring{text.data()}));
    }
    text.fill(L'\0');
    int iconIndex = -1;
    if(context.expectTrue("jump icon location reads", SUCCEEDED(link->GetIconLocation(text.data(), static_cast<int>(text.size()), &iconIndex))))
    {
        static_cast<void>(context.expectEq(
            "jump icon comes from executable",
            std::filesystem::path(absoluteExecutable).make_preferred().wstring(),
            std::wstring{text.data()}));
        static_cast<void>(context.expectEq("jump icon uses first executable icon", 0, iconIndex));
    }

    // Reject unsupported actions and verify isolated publication cleanup.
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
