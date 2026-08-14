/// @file utf8_files_test.inl
/// @brief Focused TestSupport UTF-8 text-file contract tests.

void testUtf8TextFileContracts(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path validPath = root / "utf8" / "valid.txt";
    const std::string validText = "Gr\xC3\xBC\xC3\x9F"
                                  "e \xCE\xBB";
    static_cast<void>(context.expectTrue("UTF-8 text write succeeds", TestSupport::writeTextFile(validPath, validText).ok()));
    const TestSupport::Types::TextResult validRead = TestSupport::readTextFile(validPath);
    static_cast<void>(context.expectTrue("UTF-8 text read succeeds", validRead.status.ok()));
    static_cast<void>(context.expectEq("UTF-8 text round trips unchanged", validText, validRead.text));

    const std::filesystem::path malformedPath = root / "utf8" / "malformed.txt";
    const std::string malformed = std::string("prefix") + std::string("\xE2\x82", 2);
    {
        std::ofstream raw(malformedPath, std::ios::binary | std::ios::trunc);
        raw.write(malformed.data(), static_cast<std::streamsize>(malformed.size()));
    }
    const TestSupport::Types::TextResult malformedRead = TestSupport::readTextFile(malformedPath);
    static_cast<void>(context.expectEq(
        "malformed text read reports encoding failure",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        malformedRead.status.error));
    static_cast<void>(context.expectEq("malformed text read preserves valid prefix", std::string("prefix"), malformedRead.text));

    const std::filesystem::path preservedPath = root / "utf8" / "preserved.txt";
    static_cast<void>(context.expectTrue("preexisting UTF-8 fixture write succeeds", TestSupport::writeTextFile(preservedPath, "sentinel").ok()));
    const TestSupport::Types::InfrastructureStatus malformedWrite = TestSupport::writeTextFile(preservedPath, malformed);
    static_cast<void>(context.expectEq(
        "malformed text write reports encoding failure",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        malformedWrite.error));
    const TestSupport::Types::TextResult preservedRead = TestSupport::readTextFile(preservedPath);
    static_cast<void>(context.expectTrue("malformed text write leaves destination readable", preservedRead.status.ok()));
    static_cast<void>(context.expectEq("malformed text write preserves destination", std::string("sentinel"), preservedRead.text));

    const std::filesystem::path untouchedParent = root / "utf8" / "not-created";
    const TestSupport::Types::InfrastructureStatus noSideEffect = TestSupport::writeTextFile(untouchedParent / "bad.txt", malformed);
    static_cast<void>(context.expectEq(
        "malformed text write rejects before filesystem side effects",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        noSideEffect.error));
    const TestSupport::Types::BoolResult parentExists = TestSupport::fileExists(untouchedParent);
    static_cast<void>(context.expectTrue("encoding rejection parent inspection succeeds", parentExists.status.ok()));
    static_cast<void>(context.expectFalse("encoding rejection does not create parent directory", parentExists.value));
}
