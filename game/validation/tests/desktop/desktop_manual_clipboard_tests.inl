/// @file desktop_manual_clipboard_tests.inl
/// @brief Controlled text interoperability with a separate native editor.

void testManualClipboard(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Desktop clipboard interoperability"))
    {
        return;
    }
    TestSupport::ScopedTemporaryDirectory fixtures("desktop-clipboard-manual");

    if (!context.expectTrue("clipboard fixture directory creates", fixtures.status().ok()))
    {
        return;
    }
    constexpr std::string_view outbound =
        "GameWIP outbound: caf\xC3\xA9 e\xCC\x81 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x99\x82\r\nSecond line\twith \"quotes\".";
    constexpr std::string_view inbound =
        "Foreign editor inbound: caf\xC3\xA9 e\xCC\x81 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x99\x82\r\nDifferent line\twith \"quotes\".";
    const auto outboundFile = fixtures.path() / "outbound-reference.txt";
    const auto inboundFile = fixtures.path() / "inbound-copy.txt";

    if (!context.expectTrue("outbound reference writes", TestSupport::writeTextFile(outboundFile, outbound).ok()) ||
        !context.expectTrue("inbound fixture writes", TestSupport::writeTextFile(inboundFile, inbound).ok()))
    {
        return;
    }
    Desktop::Window window;

    if (!openManualWindow(context, window, "GameWIP clipboard interoperability"))
    {
        return;
    }
    context.manual(
        "This suite replaces clipboard contents with controlled fixtures. Use a separate text editor such as Notepad. "
        "Enter 's reason' if that editor or its copy/paste action is unavailable. Text written by Desktop and text copied back are deliberately "
        "different.");

    if (manualCaseSelected("clipboard text pasted into foreign editor"))
    {
        const auto published = Desktop::Clipboard::writeText(outbound);

        if (!requireManualStatus(context, "outbound clipboard publication", published.status))
        {
            return;
        }
        recordManualCheck(
            context,
            window,
            "clipboard text pasted into foreign editor",
            std::format(
                "Paste into an empty document in the separate editor. Does the result match {} including accents, combining text, Japanese, emoji, "
                "line "
                "breaks and tab?",
                outboundFile.string()));
    }
    const auto answer = recordManualCheck(
        context,
        window,
        "clipboard text copied from foreign editor",
        std::format(
            "Open {} in the separate editor, select all and Copy. Answer yes after copying; Desktop will check the exact inbound bytes.",
            inboundFile.string()));

    if (answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        const auto copied = Desktop::Clipboard::readText();
        static_cast<void>(context.expectTrue("foreign clipboard text reads", copied.ok()));
        static_cast<void>(context.expectTrue("foreign Unicode text and line endings match exact fixture", copied.ok() && *copied == inbound));
    }
    const auto unicodeFile = fixtures.path() / L"caf\u00e9-\u65e5\u672c\u8a9e-\U0001f600.txt";
    const auto secondFile = fixtures.path() / "second-file.txt";
    const auto copyDirectory = fixtures.path() / "clipboard-copy";
    const std::array<GameWIP::FileSystem::Types::Path, 2> filePaths{unicodeFile, secondFile};

    if (!context.expectTrue(
            "clipboard file fixtures write",
            TestSupport::writeTextFile(unicodeFile, "Unicode clipboard file\n").ok() &&
                TestSupport::writeTextFile(secondFile, "Second clipboard file\n").ok() && TestSupport::createDirectories(copyDirectory).ok()))
    {
        return;
    }

    if ((manualCaseSelected("clipboard files pasted into Explorer")) &&
        (requireManualStatus(context, "file-list clipboard publication", Desktop::Clipboard::writeFiles(filePaths).status)))

    {
        const auto pasted = recordManualCheck(
            context,
            window,
            "clipboard files pasted into Explorer",
            std::format(
                "Open {} in Explorer and press Ctrl+V. Did both files arrive with the Unicode filename displayed correctly?",
                copyDirectory.string()));

        if (pasted == TestSupport::Types::Reporting::ManualAnswer::Yes)
        {
            for (const auto &path : filePaths)
            {
                const auto original = TestSupport::readTextFile(path);
                const auto copy = TestSupport::readTextFile(copyDirectory / path.filename());
                static_cast<void>(context.expectTrue(
                    "Explorer pasted file contents match exact fixture",
                    original.status.ok() && copy.status.ok() && original.text == copy.text));
            }
        }
    }

    const auto filesCopied = recordManualCheck(
        context,
        window,
        "clipboard files copied from Explorer",
        std::format(
            "Open {} in Explorer. Select just the Unicode file and second-file.txt, then Ctrl+C. Confirm after copying; exact paths and order will "
            "be checked.",
            fixtures.path().string()));

    if (filesCopied == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        const auto imported = Desktop::Clipboard::readFiles();
        static_cast<void>(context.expectTrue("Explorer clipboard file list reads", imported.ok()));
        static_cast<void>(context.expectEq(
            "Explorer clipboard preserves exact Unicode paths and order",
            std::vector(filePaths.begin(), filePaths.end()),
            *imported));
    }
    const auto image = manualInteropImage();
    const auto bitmapFile = fixtures.path() / "four-quadrants.bmp";

    if (!requireManualStatus(context, "Paint reference bitmap writes", writeManualInteropBitmap(bitmapFile, image)))
    {
        return;
    }

    if ((manualCaseSelected("clipboard image pasted into Paint")) &&
        (requireManualStatus(
            context,
            "image clipboard publication",
            Desktop::Clipboard::writeImage(Desktop::Types::DataTransfer::ImageView::packed(image.size, image.rgba8)).status)))

    {
        recordManualCheck(
            context,
            window,
            "clipboard image pasted into Paint",
            "In Paint, paste with Ctrl+V. Expect a 128x128 image: red/green across the top, blue/white across the bottom. Confirm channel order "
            "and row orientation.",
            [&]
            {
                paintManualImagePreview(window, image);
            });
    }

    const auto imageCopied = recordManualCheck(
        context,
        window,
        "clipboard image copied from Paint",
        std::format(
            "Open {} in Paint, Ctrl+A then Ctrl+C. Confirm after copying the entire 128x128 image; Desktop will compare every RGBA byte.",
            bitmapFile.string()),
        [&]
        {
            paintManualImagePreview(window, image);
        });

    if (imageCopied == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        const auto imported = Desktop::Clipboard::readImage();
        static_cast<void>(context.expectTrue("Paint clipboard image reads", imported.ok()));
        static_cast<void>(context.expectEq("Paint image has exact dimensions", image.size, imported->size));
        static_cast<void>(context.expectEq("Paint image has exact channels, orientation and opaque alpha", image.rgba8, imported->rgba8));
    }
    static_cast<void>(requireManualStatus(context, "clipboard fixture cleanup", Desktop::Clipboard::clear().status));
    static_cast<void>(window.close());
}
