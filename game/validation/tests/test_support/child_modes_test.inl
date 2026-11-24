/// @file child_modes_test.inl
/// @brief Private TestSupport child-process modes.

/// ------------------------------------------------------------
/// Child-process routes
/// ------------------------------------------------------------

int runStartGateChild();

/// @brief Implements child modes used to verify capture, timeout, environment, and exit behavior.
int runTestSupportChild(int argc, char **argv)
{
    if(GameWIP::Validation::hasProcessArgument(argc, argv, kStartGateChildArgument))
    {
        return runStartGateChild();
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kEnvironmentChildArgument))
    {
        const char *setValue = std::getenv(std::string(kChildSetVariable).c_str());
        const char *unsetValue = std::getenv(std::string(kChildUnsetVariable).c_str());

        std::cout << "child-set=" << (setValue != nullptr ? setValue : "<unset>") << '\n';
        std::cout << "child-unset=" << (unsetValue != nullptr ? unsetValue : "<unset>") << '\n';
        return setValue != nullptr && std::string_view(setValue) == "child-value" && unsetValue == nullptr ? 0 : 9;
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kCurrentPathChildArgument))
    {
        std::cout << std::filesystem::current_path().generic_string() << '\n';
        return 0;
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kEchoChildArgument))
    {
        std::cout << "testing child echo\n";
        std::cerr << "testing child stderr\n";
        return 0;
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kSleepChildArgument))
    {
        std::this_thread::sleep_for(5s);
        return 0;
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kExitCodeChildArgument))
    {
        const std::string exitCodeText = argumentAfter(argc, argv, kExitCodeChildArgument);
        return exitCodeText.empty() ? 7 : std::stoi(exitCodeText);
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kOutputChildArgument))
    {
        const std::string byteCountText = argumentAfter(argc, argv, kOutputChildArgument);
        const std::size_t byteCount = byteCountText.empty() ? 0 : static_cast<std::size_t>(std::stoull(byteCountText));
        const std::string output(byteCount, 'x');
        std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
        return 0;
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kHandleInheritanceChildArgument))
    {
        const std::string handleText = argumentAfter(argc, argv, kHandleInheritanceChildArgument);
        const auto handleValue = static_cast<std::uintptr_t>(std::stoull(handleText));
        GameWIP::Test::TestSupportPlatform::signalChildEvent(handleValue);
        return 0;
    }

    if(GameWIP::Validation::hasProcessArgument(argc, argv, kDescendantChildArgument))
    {
        const std::filesystem::path executablePath = GameWIP::Validation::currentExecutablePath();
        return GameWIP::Test::TestSupportPlatform::runDescendantChild(executablePath, kSleepChildArgument);
    }

    return 2;
}
