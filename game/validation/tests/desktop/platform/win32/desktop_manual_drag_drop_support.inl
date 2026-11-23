/// @file desktop_manual_drag_drop_support.inl
/// @brief State and helpers for native drag-and-drop manual checks.

/// ------------------------------------------------------------
/// Drag-and-drop manual support
/// ------------------------------------------------------------

struct ManualDragDropCapture
{
    // Event counts
    std::size_t entered = 0;
    std::size_t moved = 0;
    std::size_t left = 0;
    std::size_t dropped = 0;
    std::size_t regionTransitions = 0;

    // Last delivered payload
    std::optional<Desktop::Types::DragDrop::Events::Dropped> lastDrop;

    void clear(Desktop::DragDropTarget &target)
    {
        target.clearEvents();
        *this = {};
    }

    void drain(Desktop::DragDropTarget &target)
    {
        namespace DDEvents = Desktop::Types::DragDrop::Events;
        Desktop::Types::DragDrop::Event event;

        while(target.popEvent(event))
        {
            if(event.getIf<DDEvents::Entered>() != nullptr)
            {
                ++entered;
            }
            else if(const auto *movement = event.getIf<DDEvents::Moved>())
            {
                ++moved;

                if(movement->previousRegion != movement->region)
                {
                    ++regionTransitions;
                }
            }
            else if(event.getIf<DDEvents::Left>() != nullptr)
            {
                ++left;
            }
            else if(auto *drop = event.getIf<DDEvents::Dropped>())
            {
                ++dropped;
                lastDrop.emplace(std::move(*drop));
            }
        }

        if(manualStatusWindow != nullptr)
        {
            manualStatusWindow->setObservation(observation(target));
        }
    }

    [[nodiscard]] std::string observation(const Desktop::DragDropTarget &target) const
    {
        std::string text = std::format(
            "DragDrop events: Entered={} Moved={} transitions={} Left={} Dropped={} pending={} queueDrops={}",
            entered,
            moved,
            regionTransitions,
            left,
            dropped,
            target.eventQueueInfo().pendingEvents,
            target.eventQueueInfo().droppedEvents);

        if(lastDrop)
        {
            for(const auto &item : lastDrop->payload)
            {
                if(const auto *value = std::get_if<Desktop::Types::DataTransfer::Text>(&item))
                {
                    text += std::format("\r\nText ({} UTF-8 bytes): {}", value->text.size(), value->text);
                }
                else if(const auto *files = std::get_if<Desktop::Types::DataTransfer::FileList>(&item))
                {
                    text += std::format("\r\nFiles: {}", files->paths.size());

                    for(const auto &path : files->paths)
                    {
                        const auto name = path.u8string();
                        text += "\r\n" + std::string(name.begin(), name.end());
                    }
                }
                else if(const auto *image = std::get_if<Desktop::Types::DataTransfer::Image>(&item))
                {
                    text += std::format("\r\nImage: {} x {}, {} RGBA bytes", image->size.width, image->size.height, image->rgba8.size());
                }
                else if(const auto *custom = std::get_if<Desktop::Types::DataTransfer::CustomData>(&item))
                {
                    text += std::format("\r\nCustom: {}, {} bytes", custom->formatName, custom->bytes.size());
                }
            }
        }

        return text;
    }
};

[[nodiscard]] bool manualDragButtonDown(Desktop::Types::DragDrop::TriggerButton button) noexcept
{
    int key = VK_LBUTTON;

    if(button == Desktop::Types::DragDrop::TriggerButton::Right)
    {
        key = VK_RBUTTON;
    }
    else if(button == Desktop::Types::DragDrop::TriggerButton::Middle)
    {
        key = VK_MBUTTON;
    }

    return (static_cast<std::uint16_t>(GetAsyncKeyState(key)) & 0x8000U) != 0;
}

[[nodiscard]] bool manualPointerInside(const Desktop::Window &window) noexcept
{
    const auto native = Desktop::Native::Win32::getHandle(window);
    if(!native.status.ok() || native->window == nullptr)
    {
        return false;
    }

    POINT pointer{};
    RECT client{};
    return GetCursorPos(&pointer) != FALSE && ScreenToClient(native->window, &pointer) != FALSE && GetClientRect(native->window, &client) != FALSE &&
           PtInRect(&client, pointer) != FALSE;
}

struct ManualDragDropRun
{
    // User interaction
    TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::Types::Reporting::ManualAnswer::Skipped;
    bool attempted = false;
    std::size_t attempts = 0;

    // Last native result
    Desktop::Types::DragDrop::Result result;
};

ManualDragDropRun runManualDragDropSource(
    TestSupport::Context &context,
    Desktop::Window &source,
    Desktop::Window *targetWindow,
    Desktop::DragDropTarget *target,
    ManualDragDropCapture *capture,
    std::string_view name,
    std::string_view instruction,
    const Desktop::Types::DragDrop::Description &description)
{
    ManualDragDropRun run;

    if(!manualCaseSelected(name))
    {
        return run;
    }

    bool wasDown = manualDragButtonDown(description.triggerButton);
    const auto effectName = [](Desktop::Types::DragDrop::Effect effect) -> std::string_view
    {
        switch(effect)
        {
        case Desktop::Types::DragDrop::Effect::None:
            return "None";
        case Desktop::Types::DragDrop::Effect::Copy:
            return "Copy";
        case Desktop::Types::DragDrop::Effect::Move:
            return "Move";
        case Desktop::Types::DragDrop::Effect::Link:
            return "Link";
        default:
            return "Combined";
        }
    };

    const auto observe = [&]
    {
        if(target != nullptr && capture != nullptr)
        {
            capture->drain(*target);
        }

        if(targetWindow != nullptr)
        {
            paintManualValidationSurface(*targetWindow, ManualSurfaceLayout::DragDropTarget);
        }
        const bool down = manualDragButtonDown(description.triggerButton);

        if(!wasDown && down && manualPointerInside(source))
        {
            run.attempted = true;
            ++run.attempts;

            if(target != nullptr && capture != nullptr)
            {
                capture->clear(*target);
            }
            run.result = Desktop::DragDrop::beginDrag(source, description);

            if(target != nullptr && capture != nullptr)
            {
                capture->drain(*target);
            }
        }
        wasDown = down;

        if(manualStatusWindow != nullptr)
        {
            manualStatusWindow->setObservation(
                std::format(
                    "Attempt {}: {} | effect={} ({}) | status={} native={}\r\n{}\r\nRelease the button, then drag again to retry. The last attempt "
                    "is checked.",
                    run.attempts,
                    !run.attempted                                                     ? "Waiting"
                    : run.result.outcome == Desktop::Types::DragDrop::Outcome::Dropped ? "Dropped"
                                                                                       : "Cancelled",
                    effectName(run.result.effect),
                    static_cast<int>(run.result.effect),
                    IO::errorCodeName(run.result.status.code),
                    run.result.status.nativeCode,
                    target != nullptr && capture != nullptr ? capture->observation(*target) : std::string{}));
        }
    };

    const std::string retryInstruction =
        std::string(instruction) +
        " Retry with a fresh press in the source if you miss. Confirm the last attempt; its named effect remains in diagnostics.";
    run.answer = recordManualCheck(context, source, name, retryInstruction, observe, ManualSurfaceLayout::DragDropSource);

    if(run.answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        static_cast<void>(context.expectTrue(std::format("{} starts from the requested source button", name), run.attempted));

        if(run.attempted)
        {
            static_cast<void>(context.expectTrue(std::format("{} completes without a native error", name), run.result.status.ok()));
        }
    }

    return run;
}
