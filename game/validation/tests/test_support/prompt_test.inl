/// @file prompt_test.inl
/// @brief Private TestSupport prompt tests.

/// ------------------------------------------------------------
/// Manual prompts
/// ------------------------------------------------------------

/// @brief Verifies accepted manual-answer spellings, retries, and EOF behavior.
void testPromptManualCheck(TestSupport::Context &context)
{
    // Check accepted answers, retries, and end-of-input behavior.
    static_cast<void>(
        context.expectTrue("promptManualCheck accepts yes", promptWithInput("yes\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
    static_cast<void>(context.expectTrue("promptManualCheck accepts y", promptWithInput("y\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
    static_cast<void>(context.expectTrue("promptManualCheck accepts no", promptWithInput("no\n") == TestSupport::Types::Reporting::ManualAnswer::No));
    static_cast<void>(context.expectTrue("promptManualCheck accepts n", promptWithInput("n\n") == TestSupport::Types::Reporting::ManualAnswer::No));
    static_cast<void>(
        context.expectTrue("promptManualCheck accepts skipped", promptWithInput("skip\n") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
    static_cast<void>(
        context.expectTrue("promptManualCheck accepts s", promptWithInput("s\n") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
    static_cast<void>(context.expectTrue(
        "promptManualCheck retries invalid input",
        promptWithInput("maybe\nYes\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
    static_cast<void>(
        context.expectTrue("promptManualCheck returns skipped on EOF", promptWithInput("") == TestSupport::Types::Reporting::ManualAnswer::Skipped));

    // Check skip reasons and stale-reason clearing.
    const auto promptReason = [&](std::string_view input, std::string_view expected)
    {
        const ScopedPromptStreams streams(input);
        std::string reason = "stale reason";
        const auto answer = TestSupport::promptManualCheck("reason check", reason);
        return answer == TestSupport::Types::Reporting::ManualAnswer::Skipped && reason == expected;
    };
    static_cast<void>(
        context.expectTrue("manual prompt preserves tool limitation", promptReason("s tool cannot cross windows\n", "tool cannot cross windows")));
    static_cast<void>(
        context.expectTrue("manual prompt preserves scope exclusion", promptReason("skip outside requested scope\n", "outside requested scope")));
    static_cast<void>(context.expectTrue("manual prompt identifies EOF", promptReason("", "input ended before an answer")));
    static_cast<void>(context.expectTrue("manual prompt identifies unspecified skip", promptReason("s\n", "no skip reason supplied")));
}

/// @brief Converts one expected manual response into a pass, skip, or failure.
void recordExpectedManualAnswer(
    TestSupport::Context &context,
    std::string_view name,
    std::string_view question,
    TestSupport::Types::Reporting::ManualAnswer expectedAnswer)
{
    const TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::promptManualCheck(question);

    if(answer == expectedAnswer)
    {
        context.pass(name);
        return;
    }

    if(answer == TestSupport::Types::Reporting::ManualAnswer::Skipped)
    {
        context.skip(name, "manual check skipped by user");
        return;
    }
    context.fail(name, "manual answer did not match the requested response");
}

/// @brief Runs gated human prompt checks or records their disabled skip.
void testManualPromptChecks(TestSupport::Context &context, const TestSupportTestOptions &options)
{
    if(!options.enableManualTests)
    {
        context.skip("TestSupport manual prompt checks", "disabled by TestSupportTestOptions");
        return;
    }

    recordExpectedManualAnswer(
        context,
        "manual prompt accepts yes",
        "TestSupport manual check: type yes to validate ManualAnswer::Yes.",
        TestSupport::Types::Reporting::ManualAnswer::Yes);
    recordExpectedManualAnswer(
        context,
        "manual prompt accepts no",
        "TestSupport manual check: type no to validate ManualAnswer::No.",
        TestSupport::Types::Reporting::ManualAnswer::No);
    recordExpectedManualAnswer(
        context,
        "manual prompt accepts skipped",
        "TestSupport manual check: type skip to validate ManualAnswer::Skipped.",
        TestSupport::Types::Reporting::ManualAnswer::Skipped);
}
