/// @file key_decoder_test.inl
/// @brief Win32 Terminal input tests.

#if TERMINAL_INTERNAL_TEST_HOOKS
/// ------------------------------------------------------------
/// Key decoding
/// ------------------------------------------------------------

/// @brief Verifies Win32 native key records normalize into the portable Terminal key contract.
void testWin32EventDecoder(TestSupport::Context &context)
{
    const auto expectKeyEvent = [&context](std::string_view label, const Hooks::Win32KeyDecodeResult &decoded) -> const Terminal::Types::Events::Key *
    {
        static_cast<void>(context.expectTrue(std::format("{} status", label), decoded.status.ok()));
        static_cast<void>(context.expectEq(std::format("{} disposition", label), Hooks::Win32KeyDecodeDisposition::Produced, decoded.disposition));

        if(!decoded.event.has_value())
        {
            context.fail(label, "decoder produced no portable event");
            return nullptr;
        }
        const Terminal::Types::Events::Key *key = decoded.event->getIf<Terminal::Types::Events::Key>();
        static_cast<void>(context.expectTrue(std::format("{} payload", label), key != nullptr));
        return key;
    };

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'A', u'a');
        const Terminal::Types::Events::Key *key = expectKeyEvent("character A", decoded);

        if(key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("character A alternative", character != nullptr));

            if(character != nullptr)
            {
                static_cast<void>(context.expectEq("character A scalar", U'a', character->value));
            }
            static_cast<void>(context.expectEq("character A press", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("character A standard location", Terminal::Types::Events::KeyLocation::Standard, key->location));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'A', static_cast<char16_t>(0x0001), LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("Ctrl+A", decoded);

        if(key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("Ctrl+A character alternative", character != nullptr));

            if(character != nullptr)
            {
                static_cast<void>(context.expectEq("Ctrl+A normalized scalar", U'a', character->value));
            }
            static_cast<void>(context.expectTrue(
                "Ctrl+A control modifier",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Control)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, 'Q', u'@', RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("AltGr character", decoded);

        if(key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("AltGr character alternative", character != nullptr));

            if(character != nullptr)
            {
                static_cast<void>(context.expectEq("AltGr translated scalar", U'@', character->value));
            }
            static_cast<void>(context.expectFalse(
                "AltGr removes synthetic control",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Control)));
            static_cast<void>(context.expectFalse(
                "AltGr removes synthetic alt",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Alt)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, VK_LCONTROL, u'\0', LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("left control", decoded);

        if(key != nullptr)
        {
            const auto *modifier = std::get_if<Terminal::Types::Events::ModifierKey>(&key->key);
            static_cast<void>(context.expectTrue("left control modifier alternative", modifier != nullptr));

            if(modifier != nullptr)
            {
                static_cast<void>(context.expectEq("left control logical key", Terminal::Types::Events::ModifierKey::Control, *modifier));
            }
            static_cast<void>(context.expectEq("left control location", Terminal::Types::Events::KeyLocation::Left, key->location));
            static_cast<void>(context.expectFalse(
                "standalone modifier excludes itself",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Control)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult left = Hooks::decodeWin32KeyRecord(true, VK_CONTROL, u'\0', LEFT_CTRL_PRESSED);
        const Terminal::Types::Events::Key *key = expectKeyEvent("generic left control", left);

        if(key != nullptr)
        {
            static_cast<void>(context.expectEq("generic left control press", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("generic left control location", Terminal::Types::Events::KeyLocation::Left, key->location));
        }

        const Hooks::Win32KeyDecodeResult right =
            Hooks::decodeWin32KeyRecord(true, VK_CONTROL, u'\0', LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED | ENHANCED_KEY);
        key = expectKeyEvent("generic right control", right);

        if(key != nullptr)
        {
            static_cast<void>(context.expectEq("other-side control is a press", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("generic right control location", Terminal::Types::Events::KeyLocation::Right, key->location));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult super = Hooks::decodeWin32KeyRecord(true, VK_LWIN);
        static_cast<void>(expectKeyEvent("left super", super));

        const Hooks::Win32KeyDecodeResult character = Hooks::decodeWin32KeyRecord(true, 'A', u'a');
        const Terminal::Types::Events::Key *key = expectKeyEvent("Super+A", character);

        if(key != nullptr)
        {
            static_cast<void>(context.expectTrue(
                "tracked super accompanies following key",
                Terminal::Types::Events::hasModifier(key->modifiers, Terminal::Types::Events::KeyModifier::Super)));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult dedicated = Hooks::decodeWin32KeyRecord(true, VK_LEFT, u'\0', ENHANCED_KEY);
        const Terminal::Types::Events::Key *key = expectKeyEvent("dedicated left arrow", dedicated);

        if(key != nullptr)
        {
            const auto *named = std::get_if<Terminal::Types::Events::NamedKey>(&key->key);
            static_cast<void>(context.expectTrue("left arrow named alternative", named != nullptr));

            if(named != nullptr)
            {
                static_cast<void>(context.expectEq("left arrow logical key", Terminal::Types::Events::NamedKey::ArrowLeft, *named));
            }
            static_cast<void>(context.expectEq("dedicated arrow location", Terminal::Types::Events::KeyLocation::Standard, key->location));
        }

        Hooks::resetWin32KeyDecoder();
        const Hooks::Win32KeyDecodeResult numpad = Hooks::decodeWin32KeyRecord(true, VK_LEFT);
        key = expectKeyEvent("numpad left arrow", numpad);

        if(key != nullptr)
        {
            static_cast<void>(context.expectEq("numpad arrow location", Terminal::Types::Events::KeyLocation::Numpad, key->location));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult decoded = Hooks::decodeWin32KeyRecord(true, VK_F24);
        const Terminal::Types::Events::Key *key = expectKeyEvent("F24", decoded);

        if(key != nullptr)
        {
            const auto *function = std::get_if<Terminal::Types::Events::FunctionKey>(&key->key);
            static_cast<void>(context.expectTrue("F24 function alternative", function != nullptr));

            if(function != nullptr)
            {
                static_cast<void>(context.expectEq("F24 number", std::uint16_t{24}, function->number));
            }
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult first = Hooks::decodeWin32KeyRecord(true, 'A', u'a', 0, 3);
        const Terminal::Types::Events::Key *key = expectKeyEvent("combined repeat press", first);

        if(key != nullptr)
        {
            static_cast<void>(context.expectEq("combined repeat first action", Terminal::Types::Events::KeyAction::Press, key->action));
            static_cast<void>(context.expectEq("combined repeat press count", std::uint32_t{1}, key->repeatCount));
        }

        const std::optional<Terminal::Types::Event> pending = Hooks::takePendingWin32KeyEvent();
        static_cast<void>(context.expectTrue("combined repeat retains follow-up event", pending.has_value()));

        if(pending.has_value())
        {
            const Terminal::Types::Events::Key *repeat = pending->getIf<Terminal::Types::Events::Key>();
            static_cast<void>(context.expectTrue("combined repeat follow-up payload", repeat != nullptr));

            if(repeat != nullptr)
            {
                static_cast<void>(context.expectEq("combined repeat action", Terminal::Types::Events::KeyAction::Repeat, repeat->action));
                static_cast<void>(context.expectEq("combined repeat count", std::uint32_t{2}, repeat->repeatCount));
            }
        }

        const Hooks::Win32KeyDecodeResult released = Hooks::decodeWin32KeyRecord(false, 'A', u'a');
        key = expectKeyEvent("character release", released);

        if(key != nullptr)
        {
            static_cast<void>(context.expectEq("character release action", Terminal::Types::Events::KeyAction::Release, key->action));
        }
    }

    Hooks::resetWin32KeyDecoder();
    {
        const Hooks::Win32KeyDecodeResult high = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xd83d));
        static_cast<void>(context.expectEq("surrogate high waits", Hooks::Win32KeyDecodeDisposition::Pending, high.disposition));

        const Hooks::Win32KeyDecodeResult low = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xde00));
        const Terminal::Types::Events::Key *key = expectKeyEvent("surrogate pair", low);

        if(key != nullptr)
        {
            const auto *character = std::get_if<Terminal::Types::Events::CharacterKey>(&key->key);
            static_cast<void>(context.expectTrue("surrogate pair character alternative", character != nullptr));

            if(character != nullptr)
            {
                static_cast<void>(context.expectEq("surrogate pair scalar", static_cast<char32_t>(0x1f600), character->value));
            }
        }

        Hooks::resetWin32KeyDecoder();
        const Hooks::Win32KeyDecodeResult malformed = Hooks::decodeWin32KeyRecord(true, VK_PACKET, static_cast<char16_t>(0xde00));
        static_cast<void>(context.expectEq("lone low surrogate fails", ErrorCode::EncodingFailed, malformed.status.code));
        static_cast<void>(context.expectEq("lone low surrogate disposition", Hooks::Win32KeyDecodeDisposition::Failed, malformed.disposition));
    }
}
#endif
