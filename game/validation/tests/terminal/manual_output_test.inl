/// @file manual_output_test.inl
/// @brief Manual Terminal output tests.

/// ------------------------------------------------------------
/// Text and style output
/// ------------------------------------------------------------

/// @brief Displays representative UTF-8 text for visual verification.
void testManualUnicodeOutput(TestSupport::Context &context)
{
    constexpr std::string_view sample = "Unicode: cafe\xCC\x81 | \xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC | "
                                        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E | \xF0\x9F\x98\x80";

    if(!requireManualOperation(context, "manual Unicode output", "writeLine", Terminal::writeLine(sample)))
    {
        return;
    }

    recordManualCheck(
        context,
        "manual Unicode output",
        "Does the preceding Unicode line show accented text, Greek, Japanese, and an emoji without replacement characters?");
}

/// @brief Displays portable basic colors and verifies that they remain visually distinct.
void testManualBasicColorOutput(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    if(!capabilities.basicColor)
    {
        context.skip("manual basic-color output", "the terminal does not report basic-color support");
        return;
    }

    Terminal::Types::Output::LineOptions redOptions;
    redOptions.styleMode = Terminal::Types::Style::Mode::Required;
    redOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightRed);

    Terminal::Types::Output::LineOptions greenOptions;
    greenOptions.styleMode = Terminal::Types::Style::Mode::Required;
    greenOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightGreen);

    Terminal::Types::Output::LineOptions blueOptions;
    blueOptions.styleMode = Terminal::Types::Style::Mode::Required;
    blueOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightBlue);

    if(!requireManualOperation(
           context,
           "manual basic-color output",
           "red basic-color writeLine",
           Terminal::writeLine("Basic color: bright red", redOptions)) ||
       !requireManualOperation(
           context,
           "manual basic-color output",
           "green basic-color writeLine",
           Terminal::writeLine("Basic color: bright green", greenOptions)) ||
       !requireManualOperation(
           context,
           "manual basic-color output",
           "blue basic-color writeLine",
           Terminal::writeLine("Basic color: bright blue", blueOptions)))
    {
        return;
    }

    recordManualCheck(
        context,
        "manual basic-color output",
        "Are the preceding basic-color lines visibly red, green, and blue according to the terminal palette?");
}

/// @brief Displays an RGB color when the terminal can honor exact RGB requests.
void testManualRgbColorOutput(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    if(!capabilities.rgbColor)
    {
        context.skip("manual RGB-color output", "the terminal does not report RGB-color support");
        return;
    }

    Terminal::Types::Output::LineOptions rgbOptions;
    rgbOptions.styleMode = Terminal::Types::Style::Mode::Required;
    rgbOptions.style.foreground = Terminal::rgbColor(40, 210, 120);

    if(!requireManualOperation(
           context,
           "manual RGB-color output",
           "RGB styled writeLine",
           Terminal::writeLine("RGB color: red=40 green=210 blue=120", rgbOptions)))
    {
        return;
    }
    recordManualCheck(context, "manual RGB-color output", "Does the preceding RGB line appear as the requested vivid green color?");
}

/// @brief Displays each text attribute independently when the terminal advertises it.
void testManualTextStyles(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    bool wroteAttribute = false;
    const auto testAttribute = [&](std::string_view label, bool supported, bool Terminal::Types::Style::Request::*attribute) -> bool
    {
        const std::string name = std::format("manual {} text style", label);

        if(!supported)
        {
            context.skip(name, "the terminal does not report this text-style capability");
            return true;
        }

        Terminal::Types::Output::LineOptions options;
        options.styleMode = Terminal::Types::Style::Mode::Required;
        options.style.*attribute = true;
        wroteAttribute = true;
        return requireManualOperation(
            context,
            name,
            std::format("{} styled writeLine", label),
            Terminal::writeLine(std::format("Text style: {}", label), options));
    };

    if(!testAttribute("bold", capabilities.bold, &Terminal::Types::Style::Request::bold) ||
       !testAttribute("dim", capabilities.dim, &Terminal::Types::Style::Request::dim) ||
       !testAttribute("italic", capabilities.italic, &Terminal::Types::Style::Request::italic) ||
       !testAttribute("underline", capabilities.underline, &Terminal::Types::Style::Request::underline) ||
       !testAttribute("inverse", capabilities.inverse, &Terminal::Types::Style::Request::inverse) ||
       !testAttribute("strikethrough", capabilities.strikethrough, &Terminal::Types::Style::Request::strikethrough))
    {
        return;
    }

    if(!wroteAttribute)
    {
        context.skip("manual text styles", "the terminal does not report any text-style capability");
        return;
    }

    recordManualCheck(context, "manual text styles", "Did each displayed text-style line visibly match its label?");
}

/// @brief Verifies that a styled write does not leak state into the following plain write.
void testManualStyleRestoration(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    if(!capabilities.basicColor)
    {
        context.skip("manual style restoration", "the terminal does not report basic-color support");
        return;
    }

    Terminal::Types::Output::LineOptions styledOptions;
    styledOptions.styleMode = Terminal::Types::Style::Mode::Required;
    styledOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightMagenta);
    styledOptions.style.bold = capabilities.bold;
    styledOptions.style.underline = capabilities.underline;

    if(!requireManualOperation(
           context,
           "manual style restoration",
           "styled writeLine",
           Terminal::writeLine("Style restoration: styled magenta line", styledOptions)) ||
       !requireManualOperation(
           context,
           "manual style restoration",
           "plain writeLine",
           Terminal::writeLine("Style restoration: terminal defaults restored")))
    {
        return;
    }

    recordManualCheck(
        context,
        "manual style restoration",
        "Is only the first restoration line styled, with the second line using the terminal defaults?");
}
