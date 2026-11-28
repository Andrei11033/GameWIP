/// @file desktop_drag_drop_tests.inl
/// @brief DragDrop validation entry point.

/// ------------------------------------------------------------
/// DragDrop validation
/// ------------------------------------------------------------

void testDragDrop(TestSupport::Context &context)
{
    // Keep contract checks ahead of the native target lifecycle.
    testDragDropContracts(context);
    testDragDropTargetLifecycle(context);
}
