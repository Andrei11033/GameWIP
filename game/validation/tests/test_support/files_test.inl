/// @file files_test.inl
/// @brief Private TestSupport file and scoped-resource tests.

/// @brief Verifies text helpers and scoped temporary/current-directory lifetime behavior.
void testFileHelpers(TestSupport::Context &context, const std::filesystem::path &root)
{
    TestSupport::ScopedProcessState processState;
    static_cast<void>(context.expectTrue("Process-state scope construction succeeds", processState.status().ok()));

    // Exercise file creation, content queries, and cleanup.
    const std::filesystem::path path = root / "files" / "sample.txt";
    static_cast<void>(context.expectTrue("writeTextFile succeeds", TestSupport::writeTextFile(path, "alpha beta alpha beta").ok()));
    const std::filesystem::path existingDirectory = root / "files" / "existing";
    static_cast<void>(context.expectTrue("createDirectories succeeds", TestSupport::createDirectories(existingDirectory).ok()));
    static_cast<void>(context.expectTrue("createDirectories handles existing directory", TestSupport::createDirectories(existingDirectory).ok()));

    const TestSupport::Types::BoolResult existingFile = TestSupport::fileExists(path);
    static_cast<void>(context.expectTrue("fileExists written-file inspection succeeds", existingFile.ok()));
    static_cast<void>(context.expectTrue("fileExists detects written file", existingFile.ok() && existingFile.value()));

    const std::filesystem::path missingPath = root / "files" / "missing.txt";
    const TestSupport::Types::BoolResult missingFile = TestSupport::fileExists(missingPath);
    static_cast<void>(context.expectTrue("fileExists missing-path inspection succeeds", missingFile.ok()));
    static_cast<void>(context.expectFalse("fileExists missing path false", missingFile.ok() && missingFile.value()));

    const TestSupport::Types::BoolResult missingContains = TestSupport::fileContains(missingPath, "");
    static_cast<void>(context.expectFalse("fileContains missing file reports failed status", missingContains.ok()));
    static_cast<void>(context.expectFalse("fileContains missing file has no result value", missingContains.hasValue()));

    const TestSupport::Types::BoolResult existingDirectoryResult = TestSupport::fileExists(existingDirectory);
    static_cast<void>(context.expectTrue(
        "createDirectories produces an inspectable directory",
        existingDirectoryResult.ok() && existingDirectoryResult.value()));

    const TestSupport::Types::TextResult readResult = TestSupport::readTextFile(path);
    static_cast<void>(context.expectTrue("readTextFile succeeds", readResult.ok()));
    if(readResult.hasValue())
    {
        static_cast<void>(context.expectEq("readTextFile returns contents", std::string("alpha beta alpha beta"), readResult.value()));
    }

    const TestSupport::Types::BoolResult containsBeta = TestSupport::fileContains(path, "beta");
    static_cast<void>(context.expectTrue("fileContains read succeeds", containsBeta.ok()));
    static_cast<void>(context.expectTrue("fileContains finds text", containsBeta.ok() && containsBeta.value()));
    const TestSupport::Types::BoolResult containsGamma = TestSupport::fileContains(path, "gamma");
    static_cast<void>(context.expectTrue("fileContains missing-text read succeeds", containsGamma.ok()));
    static_cast<void>(context.expectFalse("fileContains rejects missing text", containsGamma.ok() && containsGamma.value()));

    const TestSupport::Types::CountResult alphaCount = TestSupport::countFileOccurrences(path, "alpha");
    static_cast<void>(context.expectTrue("countFileOccurrences read succeeds", alphaCount.ok()));
    if(alphaCount.ok())
    {
        static_cast<void>(context.expectEq("countFileOccurrences counts non-overlapping matches", std::size_t{2}, alphaCount.value()));
    }
    const TestSupport::Types::CountResult emptyCount = TestSupport::countFileOccurrences(path, "");
    static_cast<void>(context.expectTrue("countFileOccurrences empty-needle read succeeds", emptyCount.ok()));
    if(emptyCount.ok())
    {
        static_cast<void>(context.expectEq("countFileOccurrences empty needle is zero", std::size_t{0}, emptyCount.value()));
    }

    static_cast<void>(context.expectTrue("removeIfExists succeeds", TestSupport::removeIfExists(path).ok()));
    const TestSupport::Types::BoolResult removedFile = TestSupport::fileExists(path);
    static_cast<void>(context.expectTrue("removed-file inspection succeeds", removedFile.ok()));
    static_cast<void>(context.expectFalse("removeIfExists removes file", removedFile.ok() && removedFile.value()));

    // Verify unique temporary directories and recursive cleanup.
    std::filesystem::path firstTemporaryPath;
    std::filesystem::path secondTemporaryPath;
    {
        const TestSupport::ScopedTemporaryDirectory first("scoped directory");
        const TestSupport::ScopedTemporaryDirectory second("scoped directory");
        static_cast<void>(context.expectTrue("First temporary-directory construction succeeds", first.status().ok()));
        static_cast<void>(context.expectTrue("Second temporary-directory construction succeeds", second.status().ok()));
        firstTemporaryPath = first.path();
        secondTemporaryPath = second.path();
        static_cast<void>(context.expectTrue(
            "ScopedTemporaryDirectory nested fixture write succeeds",
            TestSupport::writeTextFile(firstTemporaryPath / "nested" / "artifact.txt", "temporary").ok()));

        const TestSupport::Types::BoolResult firstExists = TestSupport::fileExists(firstTemporaryPath);
        static_cast<void>(context.expectTrue("ScopedTemporaryDirectory creates its directory", firstExists.ok() && firstExists.value()));
        const TestSupport::Types::BoolResult artifactExists = TestSupport::fileExists(firstTemporaryPath / "nested" / "artifact.txt");
        static_cast<void>(
            context.expectTrue("ScopedTemporaryDirectory supports nested artifacts", artifactExists.ok() && artifactExists.value()));
        static_cast<void>(context.expectNe("ScopedTemporaryDirectory paths are unique", firstTemporaryPath, secondTemporaryPath));
    }
    const TestSupport::Types::BoolResult firstRemoved = TestSupport::fileExists(firstTemporaryPath);
    const TestSupport::Types::BoolResult secondRemoved = TestSupport::fileExists(secondTemporaryPath);
    static_cast<void>(context.expectTrue("First temporary-directory cleanup is inspectable", firstRemoved.ok()));
    static_cast<void>(context.expectFalse("ScopedTemporaryDirectory removes its directory tree", firstRemoved.ok() && firstRemoved.value()));
    static_cast<void>(context.expectTrue("Second temporary-directory cleanup is inspectable", secondRemoved.ok()));
    static_cast<void>(context.expectFalse("ScopedTemporaryDirectory removes each unique directory", secondRemoved.ok() && secondRemoved.value()));

    // Verify current-directory restoration and conflict detection.
    const std::filesystem::path originalCurrentPath = std::filesystem::current_path();
    {
        TestSupport::ScopedCurrentPath temporaryCurrentPath(processState, root);
        static_cast<void>(context.expectTrue("ScopedCurrentPath construction succeeds", temporaryCurrentPath.status().ok()));
        static_cast<void>(context.expectEq("ScopedCurrentPath stores the previous path", originalCurrentPath, temporaryCurrentPath.previousPath()));
        static_cast<void>(context.expectEq("ScopedCurrentPath changes the process path", root, std::filesystem::current_path()));
        static_cast<void>(context.expectTrue("Explicit current-path restoration succeeds", temporaryCurrentPath.restore().ok()));
        static_cast<void>(
            context.expectEq("Explicit current-path restoration restores the previous path", originalCurrentPath, std::filesystem::current_path()));
    }
    static_cast<void>(context.expectEq("ScopedCurrentPath restores the process path", originalCurrentPath, std::filesystem::current_path()));

    {
        TestSupport::ScopedProcessState conflictProcessState;
        {
            const TestSupport::ScopedCurrentPath destructorOnly(conflictProcessState, root);
            static_cast<void>(context.expectTrue("Destructor-only current-path conflict setup succeeds", destructorOnly.status().ok()));
            std::error_code error;
            std::filesystem::current_path(originalCurrentPath, error);
            static_cast<void>(context.expectFalse("Destructor-only current-path conflict setup has no filesystem error", error.value() != 0));
        }
        static_cast<void>(context.expectEq(
            "Destructor-only current-path restoration conflict is recorded",
            GameWIP::Status::Types::Code::ProcessStateConflict,
            conflictProcessState.restorationStatus().code()));
    }
}

/// @brief Verifies strict UTF-8 reads/writes, valid-prefix preservation, and preflight rejection.
void testUtf8TextFileContracts(TestSupport::Context &context, const std::filesystem::path &root)
{
    const auto writeRaw = [](const std::filesystem::path &path, std::string_view bytes)
    {
        std::ofstream raw(path, std::ios::binary | std::ios::trunc);
        raw.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };

    // Round-trip valid UTF-8 without changing its bytes.
    const std::filesystem::path validPath = root / "utf8" / "valid.txt";
    const std::string validText = "Gr\xC3\xBC\xC3\x9F"
                                  "e \xCE\xBB";
    static_cast<void>(context.expectTrue("UTF-8 text write succeeds", TestSupport::writeTextFile(validPath, validText).ok()));
    const TestSupport::Types::TextResult validRead = TestSupport::readTextFile(validPath);
    static_cast<void>(context.expectTrue("UTF-8 text read succeeds", validRead.ok()));
    if(validRead.hasValue())
    {
        static_cast<void>(context.expectEq("UTF-8 text round trips unchanged", validText, validRead.value()));
    }

    // Preserve only the complete valid prefix from malformed or incomplete input.
    const std::string malformed = std::string("prefix") + std::string("\xFF", 1);
    const std::filesystem::path malformedPath = root / "utf8" / "malformed.txt";
    writeRaw(malformedPath, malformed);
    const TestSupport::Types::TextResult malformedRead = TestSupport::readTextFile(malformedPath);
    static_cast<void>(context.expectEq(
        "malformed text read reports encoding failure",
        GameWIP::Status::Types::Code::EncodingFailed,
        malformedRead.status().code()));
    static_cast<void>(context.expectTrue("malformed text read preserves a partial value", malformedRead.hasValue()));
    if(malformedRead.hasValue())
    {
        static_cast<void>(context.expectEq("malformed text read preserves valid prefix", std::string("prefix"), malformedRead.value()));
    }

    const std::string incomplete = std::string("prefix") + std::string("\xE2\x82", 2);
    const std::filesystem::path incompletePath = root / "utf8" / "incomplete.txt";
    writeRaw(incompletePath, incomplete);
    const TestSupport::Types::TextResult incompleteRead = TestSupport::readTextFile(incompletePath);
    static_cast<void>(context.expectEq(
        "fully read incomplete suffix reports encoding failure",
        GameWIP::Status::Types::Code::EncodingFailed,
        incompleteRead.status().code()));
    static_cast<void>(context.expectTrue("incomplete text read preserves a partial value", incompleteRead.hasValue()));
    if(incompleteRead.hasValue())
    {
        static_cast<void>(context.expectEq("incomplete text read preserves valid prefix", std::string("prefix"), incompleteRead.value()));
    }

    // Reject invalid writes before truncating existing files.
    const std::filesystem::path preservedPath = root / "utf8" / "preserved.txt";
    static_cast<void>(context.expectTrue("preexisting UTF-8 fixture write succeeds", TestSupport::writeTextFile(preservedPath, "sentinel").ok()));

    const TestSupport::Types::Result<void> malformedWrite = TestSupport::writeTextFile(preservedPath, malformed);
    static_cast<void>(context.expectEq(
        "malformed text write reports encoding failure",
        GameWIP::Status::Types::Code::EncodingFailed,
        malformedWrite.status().code()));
    const TestSupport::Types::TextResult malformedPreservedRead = TestSupport::readTextFile(preservedPath);
    static_cast<void>(context.expectTrue("malformed text write leaves destination readable", malformedPreservedRead.ok()));
    if(malformedPreservedRead.ok())
    {
        static_cast<void>(context.expectEq("malformed text write preserves destination", std::string("sentinel"), malformedPreservedRead.value()));
    }

    const TestSupport::Types::Result<void> incompleteWrite = TestSupport::writeTextFile(preservedPath, incomplete);
    static_cast<void>(context.expectEq(
        "incomplete text write reports encoding failure",
        GameWIP::Status::Types::Code::EncodingFailed,
        incompleteWrite.status().code()));
    const TestSupport::Types::TextResult incompletePreservedRead = TestSupport::readTextFile(preservedPath);
    static_cast<void>(context.expectTrue("incomplete text write leaves destination readable", incompletePreservedRead.ok()));
    if(incompletePreservedRead.ok())
    {
        static_cast<void>(context.expectEq("incomplete text write preserves destination", std::string("sentinel"), incompletePreservedRead.value()));
    }

    // Reject invalid writes before creating parent directories.
    const std::filesystem::path malformedParent = root / "utf8" / "malformed-not-created";
    const TestSupport::Types::Result<void> malformedNoSideEffect = TestSupport::writeTextFile(malformedParent / "bad.txt", malformed);
    static_cast<void>(context.expectEq(
        "malformed text write rejects before filesystem side effects",
        GameWIP::Status::Types::Code::EncodingFailed,
        malformedNoSideEffect.status().code()));
    const TestSupport::Types::BoolResult malformedParentExists = TestSupport::fileExists(malformedParent);
    static_cast<void>(context.expectTrue("malformed rejection parent inspection succeeds", malformedParentExists.ok()));
    static_cast<void>(context.expectFalse("malformed rejection does not create parent directory", malformedParentExists.ok() && malformedParentExists.value()));

    const std::filesystem::path incompleteParent = root / "utf8" / "incomplete-not-created";
    const TestSupport::Types::Result<void> incompleteNoSideEffect = TestSupport::writeTextFile(incompleteParent / "bad.txt", incomplete);
    static_cast<void>(context.expectEq(
        "incomplete text write rejects before filesystem side effects",
        GameWIP::Status::Types::Code::EncodingFailed,
        incompleteNoSideEffect.status().code()));
    const TestSupport::Types::BoolResult incompleteParentExists = TestSupport::fileExists(incompleteParent);
    static_cast<void>(context.expectTrue("incomplete rejection parent inspection succeeds", incompleteParentExists.ok()));
    static_cast<void>(context.expectFalse("incomplete rejection does not create parent directory", incompleteParentExists.ok() && incompleteParentExists.value()));
}
