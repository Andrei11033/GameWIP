/// @file desktop_manual_shell_launch_support.inl
/// @brief Controlled launch helpers for manual shell checks.

/// ------------------------------------------------------------
/// Shell launch probe
/// ------------------------------------------------------------

[[nodiscard]] std::filesystem::path shellLaunchReceiptPath(std::string_view token)
{
    return std::filesystem::temp_directory_path() / std::format("GameWIP-shell-launch-{}.txt", token);
}

[[nodiscard]] std::filesystem::path shellGenericLaunchReceiptPath(const std::filesystem::path &targetPath)
{
    return targetPath.parent_path() / ".gamewip-generic-launch-receipt.txt";
}

/// @brief Confirms a controlled shell launch without starting the full validation runner again.
[[nodiscard]] int runShellLaunchProbeChild(int argc, char **argv)
{
    constexpr std::string_view prefix = "--desktop-shell-launch-token=";
    constexpr std::string_view receiptPrefix = "--desktop-shell-launch-receipt=";
    constexpr std::string_view targetPrefix = "--desktop-shell-launch-expected-target=";
    const auto arguments = GameWIP::Validation::processArguments(argc, argv);
    std::string_view expectedTarget;
    std::filesystem::path receiptPath;

    for(const char *argument : arguments)
    {
        if(argument != nullptr && std::string_view{argument}.starts_with(targetPrefix))
        {
            expectedTarget = std::string_view{argument}.substr(targetPrefix.size());
        }

        if(argument != nullptr && std::string_view{argument}.starts_with(receiptPrefix))
        {
            receiptPath = std::filesystem::path(std::string_view{argument}.substr(receiptPrefix.size()));
        }
    }

    for(const char *argument : arguments)
    {
        if(argument == nullptr || !std::string_view{argument}.starts_with(prefix))
        {
            continue;
        }
        const std::string_view token = std::string_view{argument}.substr(prefix.size());

        if(token.empty() || token.size() > 64 ||
           !std::ranges::all_of(
               token,
               [](char value)
               {
                   return (value >= '0' && value <= '9') || value == '-';
               }))
        {
            return 2;
        }

        if(!expectedTarget.empty())
        {
            std::string receivedArguments;

            for(const char *received : arguments)
            {
                receivedArguments += received == nullptr ? "<null>" : received;
                receivedArguments.push_back('\n');
            }
            const std::filesystem::path expectedArgumentsPath = receiptPath.empty() ? shellLaunchReceiptPath(token) : receiptPath;
            std::filesystem::path argumentsPath = expectedArgumentsPath;
            argumentsPath += ".arguments";
            static_cast<void>(TestSupport::writeTextFile(argumentsPath, receivedArguments));

            if(arguments.size() < 5 || arguments.back() == nullptr || std::string_view{arguments.back()} != expectedTarget)
            {
                return 4;
            }
        }

        if(receiptPath.empty())
        {
            receiptPath = shellLaunchReceiptPath(token);
        }

        return TestSupport::writeTextFile(receiptPath, token).ok() ? 0 : 3;
    }

    return 0;
}
