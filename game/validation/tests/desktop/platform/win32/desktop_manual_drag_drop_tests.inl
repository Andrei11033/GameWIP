/// @file desktop_manual_drag_drop_tests.inl
/// @brief Native drag-and-drop manual validation cases.

/// ------------------------------------------------------------
/// Native drag-and-drop behavior
/// ------------------------------------------------------------

/// @brief Exercises real native source/target interoperability with guided, reportable prompts.
void testManualDragDrop(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    namespace DD = Desktop::Types::DragDrop;
    namespace Transfer = Desktop::Types::DataTransfer;
    using TestSupport::Types::Reporting::ManualAnswer;

    if(!beginManualSuite(context, options, "Window native data drag and drop"))
    {
        return;
    }
    context.manual(
        "This suite requires an interactive Windows desktop. Keep both GameWIP Windows visible. "
        "Answer skip when a requested foreign diagnostic provider or consumer is unavailable.");

    // Open visible source and target windows for native interaction.
    Desktop::Types::Description sourceDescription;
    sourceDescription.title = "GameWIP native drag source";
    sourceDescription.windowed.clientSize = {560, 360};
    sourceDescription.activation.visible = true;
    sourceDescription.activation.requestFocus = true;
    Desktop::Types::Description targetDescription = sourceDescription;
    targetDescription.title = "GameWIP native drop target";
    Desktop::Window sourceWindow;
    Desktop::Window targetWindow;

    if(!requireManualStatus(context, "drag source Window setup", sourceWindow.open(sourceDescription, Desktop::Events::owned(64))) ||
       !requireManualStatus(context, "drop target Window setup", targetWindow.open(targetDescription, Desktop::Events::owned(128))))
    {
        return;
    }
    static_cast<void>(sourceWindow.setClientPosition({60, 100}));
    static_cast<void>(targetWindow.setClientPosition({700, 100}));

    constexpr DD::Effect allEffects = DD::Effect::Copy | DD::Effect::Move | DD::Effect::Link;
    constexpr std::string_view customName = "GameWIP.Validation.NativeDragDrop";

    // Configure target regions that accept each supported payload format.
    std::array<Transfer::FormatView, 4> allFormats{
        {Transfer::FormatView::text(), Transfer::FormatView::files(), Transfer::FormatView::image(), Transfer::FormatView::custom(customName)}};
    std::array<DD::RegionDescription, 2> regions{
        {{DD::RegionId{1}, std::nullopt, allFormats, DD::EffectPolicy::allow(allEffects, DD::Effect::Copy)},
         {DD::RegionId{2}, Desktop::Types::LogicalRect{{120, 90}, {360, 180}}, allFormats, DD::EffectPolicy::allow(allEffects, DD::Effect::Move)}}};
    Desktop::DragDropTarget target;

    if(!requireManualStatus(
           context,
           "native DragDrop target setup",
           target.open(targetWindow, DD::TargetDescription{regions}, DD::QueueConfiguration::owned(128))))
    {
        static_cast<void>(targetWindow.close());
        static_cast<void>(sourceWindow.close());
        return;
    }

    paintManualValidationSurface(sourceWindow, ManualSurfaceLayout::DragDropSource);
    paintManualValidationSurface(targetWindow, ManualSurfaceLayout::DragDropTarget);

    // Create controlled files, text, image, and custom-format payloads.
    const TestSupport::ScopedTemporaryDirectory fixtures("desktop-drag-interop");

    if(!context.expectTrue("drag fixture directory creates", fixtures.status().ok()))
    {
        return;
    }
    const auto singleFile = fixtures.path() / "single-file.txt";
    const auto unicodeFile = fixtures.path() / L"caf\u00e9-\u65e5\u672c\u8a9e-\U0001f600.txt";
    const auto readme = fixtures.path() / "README.md";
    const auto cmake = fixtures.path() / "CMakeLists.txt";
    const auto copyDirectory = fixtures.path() / "outbound-copy";
    const auto textPage = fixtures.path() / "unicode-drag.html";
    constexpr std::string_view unicodeText = kManualInteropText;

    if(!context.expectTrue(
           "controlled drag fixtures write",
           TestSupport::writeTextFile(singleFile, "single file fixture\n").ok() &&
               TestSupport::writeTextFile(unicodeFile, "Unicode file fixture\n").ok() &&
               TestSupport::writeTextFile(readme, "GameWIP outbound README fixture\n").ok() &&
               TestSupport::writeTextFile(cmake, "# GameWIP outbound CMake fixture\n").ok() && TestSupport::createDirectories(copyDirectory).ok() &&
               TestSupport::writeTextFile(
                   textPage,
                   std::format(
                       "<!doctype html><meta charset=\"utf-8\"><title>GameWIP drag fixture</title><p>Select this exact text and drag it into the "
                       "GameWIP target:</p><p>{}</p><p>Drop outbound text into this box:</p><div contenteditable=\"true\" style=\"border:2px solid "
                       "green;min-height:200px\"></div>",
                       unicodeText))
                   .ok()))
    {
        return;
    }

    context.info(std::format("Drag fixtures: {}", GameWIP::Test::Internal::pathToUtf8(context, fixtures.path(), "format drag fixture directory")));
    const std::array<GameWIP::FileSystem::Types::Path, 2> files{{readme, cmake}};
    const std::array<std::byte, 16> pixels{
        {std::byte{255},
         std::byte{0},
         std::byte{0},
         std::byte{255},
         std::byte{0},
         std::byte{255},
         std::byte{0},
         std::byte{192},
         std::byte{0},
         std::byte{0},
         std::byte{255},
         std::byte{128},
         std::byte{255},
         std::byte{255},
         std::byte{255},
         std::byte{64}}};
    const std::array<std::byte, 6> customBytes{{std::byte{'G'}, std::byte{'W'}, std::byte{0}, std::byte{0x58}, std::byte{0x3A}, std::byte{0xFF}}};
    const std::array<Transfer::ItemView, 4> allItems{
        {Transfer::TextView{unicodeText},
         Transfer::FileListView{files},
         Transfer::ImageView::packed({2, 2}, pixels),
         Transfer::CustomView{customName, customBytes}}};
    const std::array<Transfer::ItemView, 1> textItem{{Transfer::TextView{unicodeText}}};

    // Reject attempts to begin a drag without the configured trigger held.
    for(const DD::TriggerButton button : {DD::TriggerButton::Left, DD::TriggerButton::Right, DD::TriggerButton::Middle})
    {
        const DD::Result result = Desktop::DragDrop::beginDrag(sourceWindow, DD::Description{textItem, DD::EffectPolicy::source(allEffects), button});
        static_cast<void>(context.expectEq("unheld configured trigger is InvalidArgument", ErrorCode::InvalidArgument, result.status.code));
    }

    ManualDragDropCapture capture;
    capture.clear(target);

    // Verify same-process delivery and exact payload ownership.
    const ManualDragDropRun sameProcess = runManualDragDropSource(
        context,
        sourceWindow,
        &targetWindow,
        &target,
        &capture,
        "same-process all-format drop",
        "Hold LEFT in the green source, drag into the green Region 2, move across its boundary at least once, then release. "
        "Answer yes only if the Windows stayed responsive and the diagnostics report a drop.",
        DD::Description{allItems, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

    if(sameProcess.answer == ManualAnswer::Yes && sameProcess.attempted)
    {
        static_cast<void>(context.expectEq("same-process source reports Dropped", DD::Outcome::Dropped, sameProcess.result.outcome));
        static_cast<void>(context.expectEq("Region 2 selects Move", DD::Effect::Move, sameProcess.result.effect));
        static_cast<void>(context.expectTrue("target receives Entered", capture.entered > 0));
        static_cast<void>(context.expectTrue("target receives movement", capture.moved > 0));
        static_cast<void>(context.expectEq("target receives one terminal drop", std::size_t{1}, capture.dropped));
        static_cast<void>(context.expectTrue("target owns the completed payload", capture.lastDrop.has_value()));

        if(capture.lastDrop)
        {
            static_cast<void>(context.expectEq("drop selects Region 2", DD::RegionId{2}, capture.lastDrop->region));
            static_cast<void>(context.expectEq("drop materializes all four formats", std::size_t{4}, capture.lastDrop->payload.size()));

            if(capture.lastDrop->payload.size() == 4)
            {
                const auto *text = std::get_if<Transfer::Text>(&capture.lastDrop->payload[0]);
                const auto *fileList = std::get_if<Transfer::FileList>(&capture.lastDrop->payload[1]);
                const auto *image = std::get_if<Transfer::Image>(&capture.lastDrop->payload[2]);
                const auto *custom = std::get_if<Transfer::CustomData>(&capture.lastDrop->payload[3]);
                static_cast<void>(context.expectTrue("Unicode text round-trips exactly", text != nullptr && text->text == unicodeText));
                static_cast<void>(context.expectTrue(
                    "file list round-trips exactly",
                    fileList != nullptr && fileList->paths == std::vector(files.begin(), files.end())));
                static_cast<void>(context.expectTrue(
                    "RGBA image round-trips exactly",
                    image != nullptr && image->size == Desktop::Types::PixelSize{2, 2} && image->rgba8 == std::vector(pixels.begin(), pixels.end())));
                static_cast<void>(context.expectTrue(
                    "custom identity and bytes round-trip exactly",
                    custom != nullptr && custom->formatName == customName && custom->bytes == std::vector(customBytes.begin(), customBytes.end())));
            }
        }
    }

    struct EffectCase
    {
        DD::Effect source;
        DD::Effect target;
        DD::Effect preferred;
        DD::Effect expected;
        std::string_view label;
    };

    // Check effect selection across source and target policy combinations.
    constexpr std::array effectCases{
        EffectCase{DD::Effect::Copy, DD::Effect::Copy, DD::Effect::Copy, DD::Effect::Copy, "Copy"},
        EffectCase{DD::Effect::Move, DD::Effect::Move, DD::Effect::Move, DD::Effect::Move, "Move"},
        EffectCase{DD::Effect::Link, DD::Effect::Link, DD::Effect::Link, DD::Effect::Link, "Link"},
        EffectCase{DD::Effect::Copy | DD::Effect::Move, allEffects, DD::Effect::Move, DD::Effect::Move, "Copy|Move preferred Move"},
        EffectCase{DD::Effect::Copy | DD::Effect::Link, allEffects, DD::Effect::Link, DD::Effect::Link, "Copy|Link preferred Link"},
        EffectCase{DD::Effect::Move | DD::Effect::Link, allEffects, DD::Effect::Link, DD::Effect::Link, "Move|Link preferred Link"},
        EffectCase{allEffects, allEffects, DD::Effect::Move, DD::Effect::Move, "Copy|Move|Link preferred Move"},
        EffectCase{DD::Effect::Copy, DD::Effect::Move, DD::Effect::Move, DD::Effect::None, "no intersection"}};

    for(const EffectCase &effectCase : effectCases)
    {
        std::array<Transfer::FormatView, 1> formats{{Transfer::FormatView::text()}};
        std::array<DD::RegionDescription, 1> effectRegion{
            {{DD::RegionId{1}, std::nullopt, formats, DD::EffectPolicy::allow(effectCase.target, effectCase.preferred)}}};
        static_cast<void>(context.expectTrue(std::format("{} target policy applies", effectCase.label), target.setRegions(effectRegion).ok()));
        capture.clear(target);
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            &targetWindow,
            &target,
            &capture,
            std::format("effect matrix {}", effectCase.label),
            std::format("Hold LEFT in the source, drag to the blue target, and release. Expected effect: {}.", effectCase.label),
            DD::Description{textItem, DD::EffectPolicy::source(effectCase.source), DD::TriggerButton::Left});

        if(run.answer == ManualAnswer::Yes && run.attempted)
        {
            const DD::Outcome expectedOutcome = effectCase.expected == DD::Effect::None ? DD::Outcome::Cancelled : DD::Outcome::Dropped;
            static_cast<void>(context.expectEq(std::format("{} outcome", effectCase.label), expectedOutcome, run.result.outcome));
            static_cast<void>(context.expectEq(std::format("{} negotiated effect", effectCase.label), effectCase.expected, run.result.effect));
        }
    }

    static_cast<void>(target.setRegions(regions));

    for(const DD::TriggerButton button : {DD::TriggerButton::Right, DD::TriggerButton::Middle})
    {
        capture.clear(target);
        const std::string_view buttonName = button == DD::TriggerButton::Right ? "RIGHT" : "MIDDLE";
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            &targetWindow,
            &target,
            &capture,
            std::format("{} trigger", buttonName),
            std::format("Hold {} in the source, drag to either target region, then release that same button.", buttonName),
            DD::Description{textItem, DD::EffectPolicy::source(allEffects), button});

        if(run.answer == ManualAnswer::Yes && run.attempted)
        {
            static_cast<void>(context.expectEq(std::format("{} trigger drops", buttonName), DD::Outcome::Dropped, run.result.outcome));
        }
    }

    capture.clear(target);
    const ManualDragDropRun cancellation = runManualDragDropSource(
        context,
        sourceWindow,
        &targetWindow,
        &target,
        &capture,
        "Escape and leave cancellation",
        "Hold LEFT in the source, enter and leave the target, press and release an unrelated mouse button, then press Escape while LEFT remains "
        "held.",
        DD::Description{textItem, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

    if(cancellation.answer == ManualAnswer::Yes && cancellation.attempted)
    {
        static_cast<void>(context.expectEq("Escape reports Cancelled", DD::Outcome::Cancelled, cancellation.result.outcome));
        static_cast<void>(context.expectEq("cancelled drag reports no effect", DD::Effect::None, cancellation.result.effect));
        static_cast<void>(context.expectTrue("cancelled target traversal enters", capture.entered > 0));
        static_cast<void>(context.expectTrue("leaving target queues Left", capture.left > 0));
        static_cast<void>(context.expectEq("cancellation queues no drop", std::size_t{0}, capture.dropped));
    }

    for(const std::string_view modifier : {"Ctrl", "Shift", "Alt"})
    {
        capture.clear(target);
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            &targetWindow,
            &target,
            &capture,
            std::format("{} modifier independence", modifier),
            std::format("Hold LEFT in the source, keep {} held, drop in green Region 2, then release {}.", modifier, modifier),
            DD::Description{textItem, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

        if(run.answer == ManualAnswer::Yes && run.attempted)
        {
            static_cast<void>(context.expectEq(std::format("{} does not override target preference", modifier), DD::Effect::Move, run.result.effect));
        }
    }

    const auto observeForeign = [&]
    {
        capture.drain(context, target);
        paintManualValidationSurface(targetWindow, ManualSurfaceLayout::DragDropTarget);

        if(capture.lastDrop)
        {
            for(const auto &item : capture.lastDrop->payload)
            {
                if(const auto *image = std::get_if<Transfer::Image>(&item))
                {
                    paintManualImagePreview(targetWindow, *image);
                }
            }
        }
    };
    const auto inbound = [&](std::string_view name, std::string_view instruction, const Transfer::Item *expected = nullptr)
    {
        capture.clear(target);
        const ManualAnswer answer = recordManualCheck(context, targetWindow, name, instruction, observeForeign, ManualSurfaceLayout::DragDropTarget);

        if(answer == ManualAnswer::Yes)
        {
            static_cast<void>(context.expectEq(std::format("{} yields one complete drop", name), std::size_t{1}, capture.dropped));

            if(expected != nullptr)
            {
                static_cast<void>(context.expectTrue(
                    std::format("{} matches exact controlled payload", name),
                    capture.lastDrop && std::ranges::find(capture.lastDrop->payload, *expected) != capture.lastDrop->payload.end()));
            }
        }
    };

    const Transfer::Item expectedSingle = Transfer::FileList{{singleFile}};
    const Transfer::Item expectedMultiple = Transfer::FileList{{unicodeFile, singleFile}};
    const Transfer::Item expectedText = Transfer::Text{std::string(unicodeText)};
    const Transfer::Item expectedCustom = Transfer::CustomData{std::string(customName), std::vector(customBytes.begin(), customBytes.end())};
    inbound(
        "Explorer single file",
        std::format(
            "From Explorer, drop {}. The exact imported path will be checked.",
            GameWIP::Test::Internal::pathToUtf8(context, singleFile, "format single-file drag fixture path")),
        &expectedSingle);
    inbound(
        "Explorer multiple Unicode files",
        std::format(
            "In {}, sort by Name ascending, select single-file.txt and the Unicode filename together and drop them. Exact paths in the visible order "
            "will be checked.",
            GameWIP::Test::Internal::pathToUtf8(context, fixtures.path(), "format multi-file drag fixture directory")),
        &expectedMultiple);
    inbound(
        "foreign strict Unicode text",
        std::format(
            "Open {} in a browser. Select just the GameWIP strict Unicode paragraph and drag it into the target. Exact UTF-8 bytes will be checked.",
            GameWIP::Test::Internal::pathToUtf8(context, textPage, "format text drag fixture path")),
        &expectedText);
    inbound(
        "foreign DIB image",
        "Drag a known oriented/color test image from a DIB-capable application and verify orientation, RGBA channels, and supported alpha after the "
        "drop.");
    inbound(
        "controlled custom provider",
        "Using a controlled provider for GameWIP.Validation.NativeDragDrop, send the exact documented bytes. Answer skip if that provider is "
        "unavailable. Expected bytes: 47 57 00 58 3A FF.",
        &expectedCustom);

    capture.clear(target);
    const ManualAnswer malformed = recordManualCheck(
        context,
        targetWindow,
        "malformed foreign IDataObject",
        "Use the controlled malformed IDataObject provider to exercise malformed text, HDROP, DIB, FORMATETC, and partial-materialization cases. "
        "Answer yes only if every case was rejected without a drop or crash; answer skip if unavailable.",
        observeForeign,
        ManualSurfaceLayout::DragDropTarget);

    if(malformed == ManualAnswer::Yes)
    {
        static_cast<void>(context.expectEq("malformed provider queues no successful drop", std::size_t{0}, capture.dropped));
    }

    const auto outbound =
        [&](std::string_view name, std::string_view instruction, std::span<const Transfer::ItemView> items, DD::EffectPolicy effects)
    {
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            nullptr,
            nullptr,
            nullptr,
            name,
            instruction,
            DD::Description{items, effects, DD::TriggerButton::Left});

        if(run.answer == ManualAnswer::Yes && run.attempted)
        {
            static_cast<void>(context.expectEq(std::format("{} reports Dropped", name), DD::Outcome::Dropped, run.result.outcome));

            if(name == "GameWIP files to Explorer")
            {
                for(const auto &path : files)
                {
                    const auto original = TestSupport::readTextFile(path);
                    const auto copy = TestSupport::readTextFile(copyDirectory / path.filename());
                    static_cast<void>(context.expectTrue(
                        "Explorer drag copies exact file contents",
                        original.status.ok() && copy.status.ok() && original.text == copy.text));
                }
            }
        }
    };

    const std::array<Transfer::ItemView, 1> fileItem{{Transfer::FileListView{files}}};
    const std::array<Transfer::ItemView, 1> imageItem{{Transfer::ImageView::packed({2, 2}, pixels)}};
    const std::array<Transfer::ItemView, 1> customItem{{Transfer::CustomView{customName, customBytes}}};
    outbound(
        "GameWIP files to Explorer",
        std::format(
            "Hold LEFT in the source and drag to {} in Explorer. Verify Copy is reported and README.md/CMakeLists.txt arrive.",
            GameWIP::Test::Internal::pathToUtf8(context, copyDirectory, "format outbound drag copy directory")),
        fileItem,
        DD::EffectPolicy::source(DD::Effect::Copy));
    outbound(
        "GameWIP Unicode text to editor",
        "Hold LEFT in the source and drag into a compatible text editor. Verify the exact accented, Japanese, punctuation, and emoji text.",
        textItem,
        DD::EffectPolicy::source(allEffects));
    outbound(
        "GameWIP image to foreign target",
        "Hold LEFT in the source and drag to a DIB-capable image target. Verify orientation, R/G/B order, and supported alpha.",
        imageItem,
        DD::EffectPolicy::source(allEffects));
    outbound(
        "GameWIP custom data to diagnostic consumer",
        "Hold LEFT in the source and drag to a controlled consumer that requests the custom format repeatedly. Answer skip if unavailable.",
        customItem,
        DD::EffectPolicy::source(allEffects));

    capture.clear(target);
    const ManualDragDropRun modalGeometry = runManualDragDropSource(
        context,
        sourceWindow,
        &targetWindow,
        &target,
        &capture,
        "modal presentation and interactive geometry",
        "Hold LEFT in the source and keep the drag active. Move or resize the target with Windows keyboard/shell controls where practical, "
        "cross both target regions, then drop. Answer yes only if geometry/presentation stayed coherent with no deadlock.",
        DD::Description{textItem, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

    if(modalGeometry.answer == ManualAnswer::Yes && modalGeometry.attempted)
    {
        static_cast<void>(context.expectEq("modal geometry scenario drops", DD::Outcome::Dropped, modalGeometry.result.outcome));
    }

    static_cast<void>(context.expectTrue("full target closes before lightweight mode", target.close().ok()));
    static_cast<void>(context.expectTrue("lightweight file drop enables after full target", targetWindow.setFileDropEnabled(true).ok()));
    static_cast<void>(context.expectEq(
        "full target conflicts with lightweight mode",
        ErrorCode::ResourceBusy,
        target.open(targetWindow, DD::TargetDescription{regions}, DD::QueueConfiguration::owned(8)).code));
    static_cast<void>(context.expectTrue("lightweight file drop disables", targetWindow.setFileDropEnabled(false).ok()));
    static_cast<void>(context.expectTrue(
        "full target reopens after lightweight mode",
        target.open(targetWindow, DD::TargetDescription{regions}, DD::QueueConfiguration::owned(8)).ok()));
    static_cast<void>(target.close());
    static_cast<void>(targetWindow.close());
    static_cast<void>(sourceWindow.close());
}
