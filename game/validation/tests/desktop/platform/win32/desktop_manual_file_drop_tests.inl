/// @file desktop_manual_file_drop_tests.inl
/// @brief Manual validation for Window file drops and icons.

/// ------------------------------------------------------------
/// File drop and icon behavior
/// ------------------------------------------------------------

void testManualFileDropAndIcon(TestSupport::Context &context, Desktop::Window &window, const Desktop::Types::Capabilities &capabilities)
{
    // Validate file-drop behavior.
    if(capabilities.supports(Desktop::Types::Capability::FileDrop))
    {
        const auto runDrop = [&](std::string_view name, std::string_view instruction, std::size_t minimumPaths)
        {
            window.clearEvents();
            std::size_t groupedEvents = 0;
            std::size_t pathCount = 0;
            const auto observeDrops = [&]
            {
                Desktop::Types::Event event;

                while(window.popEvent(event))
                {
                    if(const auto *drop = event.getIf<Desktop::Types::Events::FilesDropped>())
                    {
                        ++groupedEvents;
                        pathCount += drop->paths.size();
                        std::string pathList;

                        for(const auto &path : drop->paths)
                        {
                            const std::u8string utf8 = path.generic_u8string();

                            if(!pathList.empty())
                            {
                                pathList.append("\r\n");
                            }
                            pathList.append(reinterpret_cast<const char *>(utf8.data()), utf8.size());
                        }
                        const std::string observation = std::format(
                            "Received FilesDroppedEvent: groupedEvents={} paths={} clientPosition={}\r\n{}",
                            groupedEvents,
                            pathCount,
                            drop->clientPosition.has_value(),
                            pathList);

                        if(manualStatusWindow != nullptr)
                        {
                            manualStatusWindow->setObservation(observation);
                        }
                        static_cast<void>(window.setTitle(std::format("Drop received: {} path(s)", pathCount)));
                    }
                }
            };
            static_cast<void>(window.setTitle("Waiting for file drop..."));

            if(manualStatusWindow != nullptr)
            {
                manualStatusWindow->setObservation("Waiting for FilesDroppedEvent.");
            }
            const TestSupport::Types::Reporting::ManualAnswer answer = recordManualCheck(context, window, name, instruction, observeDrops);

            if(answer != TestSupport::Types::Reporting::ManualAnswer::Yes)
            {
                return;
            }

            static_cast<void>(context.expectEq("file drop produces one grouped event", std::size_t{1}, groupedEvents));
            static_cast<void>(context.expectTrue("file drop retains expected paths", pathCount >= minimumPaths));
        };

        runDrop("single-file drop", "Drag one ordinary file into the Window, then answer yes if one drop was accepted.", 1);
        runDrop("multiple-file drop", "Drag at least two files together into the Window, then answer yes if one grouped drop was accepted.", 2);
        runDrop("Unicode-path drop", "Drag a file whose path contains non-ASCII characters, then answer yes if it was accepted intact.", 1);
        runDrop("space-containing-path drop", "Drag a file whose path contains spaces, then answer yes if it was accepted intact.", 1);

        static_cast<void>(context.expectTrue("file drops disable", window.setFileDropEnabled(false).ok()));
        window.clearEvents();
        const TestSupport::Types::Reporting::ManualAnswer disabledAnswer =
            recordManualCheck(context, window, "disabled file drops", "Try dropping a file. Was it rejected with no observable drop behavior?");

        if(disabledAnswer == TestSupport::Types::Reporting::ManualAnswer::Yes)
        {
            static_cast<void>(
                context.expectFalse("disabled file drops queue no event", consumeEventOfType<Desktop::Types::Events::FilesDropped>(window)));
        }
    }
    else
    {
        context.skip("Window file drops", "backend does not advertise FileDrop");
    }

    // Validate icon behavior.
    if(capabilities.supports(Desktop::Types::Capability::WindowIcon))
    {
        const auto makeIcon = [](std::uint32_t extent)
        {
            std::vector<std::byte> pixels(static_cast<std::size_t>(extent) * extent * 4u);

            for(std::uint32_t y = 0; y < extent; ++y)
            {
                for(std::uint32_t x = 0; x < extent; ++x)
                {
                    const bool cyan = x < std::max(2u, extent / 5u) || y < std::max(2u, extent / 5u) || x == y || x + y + 1u == extent;
                    const std::size_t offset = (static_cast<std::size_t>(y) * extent + x) * 4u;
                    pixels[offset] = cyan ? std::byte{70} : std::byte{22};
                    pixels[offset + 1u] = cyan ? std::byte{210} : std::byte{70};
                    pixels[offset + 2u] = cyan ? std::byte{255} : std::byte{126};
                    pixels[offset + 3u] = std::byte{255};
                }
            }

            return pixels;
        };
        std::vector<std::byte> smallPixels = makeIcon(16u);
        std::vector<std::byte> largePixels = makeIcon(32u);
        const std::array images{Desktop::Types::IconImageView{{16, 16}, smallPixels}, Desktop::Types::IconImageView{{32, 32}, largePixels}};
        static_cast<void>(context.expectTrue("custom shell icons apply", window.setIcon(images).ok()));
        recordManualCheck(
            context,
            window,
            "custom shell icons",
            "Does the blue/cyan GameWIP validation icon appear clearly at the appropriate small title-bar and larger taskbar shell sizes?");
        static_cast<void>(window.clearIcon());
    }
}
