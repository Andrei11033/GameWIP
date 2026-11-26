/// @file desktop_manual_interaction.inl
/// @brief Manual results and owner-thread interaction helpers.

/// ------------------------------------------------------------
/// Manual results and interaction
/// ------------------------------------------------------------

/// @brief Records a manual response as a test pass, failure, or skip.
struct ManualObservationCounts
{
    // User-reported outcomes
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
};
ManualObservationCounts manualObservationCounts;

void recordManualAnswer(
    TestSupport::Context &context,
    std::string_view name,
    TestSupport::Types::Reporting::ManualAnswer answer,
    std::string_view skipReason = {})
{
    switch(answer)
    {
    case TestSupport::Types::Reporting::ManualAnswer::Yes:
        ++manualObservationCounts.passed;
        context.pass(name);
        return;
    case TestSupport::Types::Reporting::ManualAnswer::No:
        ++manualObservationCounts.failed;
        context.fail(name, "manual check rejected by user");
        return;
    case TestSupport::Types::Reporting::ManualAnswer::Skipped:
        ++manualObservationCounts.skipped;
        context.skip(name, skipReason.empty() ? "no skip reason supplied" : skipReason);
        return;
    }
}

/// @brief Prompts on a worker while the owner thread keeps native Window messages flowing.
TestSupport::Types::Reporting::ManualAnswer recordManualCheck(
    TestSupport::Context &context,
    Desktop::Window &window,
    std::string_view name,
    std::string_view question,
    const std::function<void()> &observe = {},
    ManualSurfaceLayout surfaceLayout = ManualSurfaceLayout::Standard)
{
    // Skip cases outside the selected manual run.
    if(!manualCaseSelected(name))
    {
        return TestSupport::Types::Reporting::ManualAnswer::Skipped;
    }

    ++manualCasesSelected;

    // Prepare the diagnostics window for this scenario.
    if(manualStatusWindow != nullptr)
    {
        manualStatusWindow->setScenario(name, question);

        if(!observe)
        {
            manualStatusWindow->setObservation({});
        }
        paintManualValidationSurface(window, surfaceLayout);
        manualStatusWindow->refresh(window);
    }

    // Prompt on a worker while the owner continues pumping events.
    std::atomic<bool> answered = false;
    TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::Types::Reporting::ManualAnswer::Skipped;
    std::string skipReason;
    std::jthread promptThread(
        [&]
        {
            answer = TestSupport::promptManualCheck(question, skipReason);
            answered.store(true, std::memory_order_release);
            static_cast<void>(window.wakeEventWait());
        });

    OperationStatus pumpFailure;

    while(!answered.load(std::memory_order_acquire))
    {
        const Desktop::Types::Events::PumpResult pump = Desktop::Events::wait(std::chrono::milliseconds{50});

        if(!pump.ok() && pumpFailure.ok())
        {
            pumpFailure = pump.status();
        }
        paintManualValidationSurface(window, surfaceLayout);

        if(observe)
        {
            observe();
        }

        if(manualStatusWindow != nullptr)
        {
            manualStatusWindow->refresh(window);
        }
    }
    promptThread.join();

    // Drain the final events and report any pump failure before the answer.
    const Desktop::Types::Events::PumpResult finalPump = Desktop::Events::poll();

    if(!finalPump.ok() && pumpFailure.ok())
    {
        pumpFailure = finalPump.status();
    }
    paintManualValidationSurface(window, surfaceLayout);

    if(observe)
    {
        observe();
    }

    if(manualStatusWindow != nullptr)
    {
        manualStatusWindow->refresh(window);
    }

    if(!pumpFailure.ok())
    {
        ++manualObservationCounts.failed;
        context.fail(name, manualStatusDetails(pumpFailure));
        return TestSupport::Types::Reporting::ManualAnswer::No;
    }

    recordManualAnswer(context, name, answer, skipReason);
    return answer;
}
