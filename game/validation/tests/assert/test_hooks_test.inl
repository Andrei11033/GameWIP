/// @file test_hooks_test.inl
/// @brief Private Assert correctness cases for deterministic source-tree test hooks.

/// @brief Verifies dialog, debugger, and popup-suppression test hooks and reset behavior.
void testAssertTestHooks(TestContext &context)
{
#if ASSERT_INTERNAL_TEST_HOOKS
    using GameWIP::Debug::Assert::FailureAction;
    namespace AssertHooks = GameWIP::Debug::Assert::TestHooks;
    namespace AssertHookDetail = GameWIP::Debug::Assert::Detail::TestHooks;

    AssertHooks::reset();
    AssertHooks::setDebuggerAttachedOverride(true);
    context.expectTrue("hook debugger attached override true", AssertHooks::debuggerAttachedForTest());
    AssertHooks::setDebuggerAttachedOverride(false);
    context.expectTrue("hook debugger attached override false", !AssertHooks::debuggerAttachedForTest());
    AssertHooks::clearDebuggerAttachedOverride();

    AssertHooks::forceNextActionDialogFailure();
    AssertHooks::forceNextFallbackActionDialogFailure();
    const FailureAction fallbackAction = AssertHooks::showFailureActionDialogForTest(
        "Assert hook test",
        "Primary and fallback action dialogs are forced to fail; default action should be returned.",
        FailureAction::IgnoreOnce);
    context.expectTrue("hook action dialog failure consumed", !AssertHookDetail::consumeNextActionDialogFailure());
    context.expectTrue("hook fallback action dialog failure consumed", !AssertHookDetail::consumeNextFallbackActionDialogFailure());
    context.expectTrue("hook action dialog default fallback", fallbackAction == FailureAction::IgnoreOnce);

    AssertHooks::setPopupSuppressedOverride(true);
    AssertHooks::showErrorPopupForTest("Assert hook popup suppression", "This popup should be suppressed by the test hook.");
    context.pass("hook popup suppression override returned without UI");

    AssertHooks::clearPopupSuppressedOverride();
    AssertHooks::forceNextDiagnosticPreparationFailure();
    AssertHooks::showErrorPopupForTest("Assert preparation failure", "The static popup fallback must remain noexcept.");
    context.expectTrue("diagnostic preparation failure consumed by popup", !AssertHookDetail::consumeNextDiagnosticPreparationFailure());

    AssertHooks::forceNextDiagnosticPreparationFailure();
    AssertHooks::forceNextFallbackActionDialogFailure();
    const FailureAction emergencyAction = AssertHooks::showFailureActionDialogForTest(
        "Assert preparation failure",
        "The allocation-free action fallback must remain safe.",
        FailureAction::IgnoreOnce);
    context.expectTrue("diagnostic preparation failure reaches action emergency path", emergencyAction == FailureAction::IgnoreOnce);
    context.expectTrue("action emergency fallback consumes preparation failure", !AssertHookDetail::consumeNextDiagnosticPreparationFailure());
    context.expectTrue("action emergency fallback remains one-shot", !AssertHookDetail::consumeNextFallbackActionDialogFailure());

    AssertHooks::reset();
#else
    context.pass("assert test hooks skipped because ASSERT_INTERNAL_TEST_HOOKS=0");
#endif
}
