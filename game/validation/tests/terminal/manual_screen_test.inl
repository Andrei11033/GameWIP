/// @file manual_screen_test.inl
/// @brief Manual Terminal screen tests.

/// ------------------------------------------------------------
/// Cursor and screen behavior
/// ------------------------------------------------------------

/// @brief Verifies visible cursor save/restore behavior without changing the final cursor position.
void testManualCursorBehavior(TestSupport::Context &context, const Terminal::Types::Output::Capabilities &capabilities)
{
    if(!capabilities.supportsCursorSaveRestore)
    {
        context.skip("manual cursor behavior", "the terminal does not report cursor save/restore support");
        return;
    }

    if(!requireManualOperation(context, "manual cursor behavior", "saveCursorPosition", Terminal::saveCursorPosition()) ||
       !requireManualOperation(
           context,
           "manual cursor behavior",
           "placeholder writeText",
           Terminal::writeText("Cursor: this placeholder should be replaced                 ")) ||
       !requireManualOperation(context, "manual cursor behavior", "restoreCursorPosition", Terminal::restoreCursorPosition()) ||
       !requireManualOperation(
           context,
           "manual cursor behavior",
           "replacement writeLine",
           Terminal::writeLine("Cursor: PASS - saved position restored                     ")))
    {
        return;
    }

    recordManualCheck(context, "manual cursor behavior", "Is there one Cursor: PASS line above, with no visible placeholder line?");
}

/// @brief Enters an alternate screen, checks its contents, and restores the main screen.
void testManualAlternateScreen(TestSupport::Context &context, const Terminal::Types::Output::Capabilities &capabilities)
{
    if(!capabilities.supportsAlternateScreen || !capabilities.supportsClear)
    {
        context.skip("manual alternate screen", "the terminal does not report alternate-screen and clear support");
        return;
    }

    Terminal::AlternateScreenScope alternateScreen = Terminal::scopedAlternateScreen();

    if(!requireManualOperation(context, "manual alternate screen", "enter alternate screen", alternateScreen.status()))
    {
        return;
    }

    if(!alternateScreen.active())
    {
        context.fail("manual alternate screen", "alternate-screen scope did not become active");
        return;
    }

    TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::Types::Reporting::ManualAnswer::Skipped;
    std::string skipReason;
    const bool contentReady = requireManualOperation(context, "manual alternate screen", "clear alternate screen", Terminal::clear()) &&
                              requireManualOperation(
                                  context,
                                  "manual alternate screen",
                                  "alternate-screen writeLine",
                                  Terminal::writeLine("GameWIP Terminal alternate-screen check"));

    if(contentReady)
    {
        answer = TestSupport::promptManualCheck("Is this prompt displayed on a clean alternate screen?", skipReason);
    }

    const bool restored = requireManualOperation(context, "manual alternate screen", "leave alternate screen", alternateScreen.leave());

    if(contentReady && restored)
    {
        recordManualAnswer(context, "manual alternate screen", answer, skipReason);
    }
}
