/// @file files_test.inl
/// @brief Private TestSupport correctness cases grouped by behavioral responsibility.

/// @brief Verifies text helpers and scoped temporary/current-directory lifetime behavior.
void testFileHelpers(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path path = root / "files" / "sample.txt";
    static_cast<void>(context.expectTrue("writeTextFile succeeds", TestSupport::writeTextFile(path, "alpha beta alpha beta").ok()));
    const std::filesystem::path existingDirectory = root / "files" / "existing";
    static_cast<void>(context.expectTrue("createDirectories succeeds", TestSupport::createDirectories(existingDirectory).ok()));
    static_cast<void>(context.expectTrue("createDirectories handles existing directory", TestSupport::createDirectories(existingDirectory).ok()));

    const TestSupport::Types::BoolResult existingFile = TestSupport::fileExists(path);
    static_cast<void>(context.expectTrue("fileExists written-file inspection succeeds", existingFile.status.ok()));
    static_cast<void>(context.expectTrue("fileExists detects written file", existingFile.value));

    const std::filesystem::path missingPath = root / "files" / "missing.txt";
    const TestSupport::Types::BoolResult missingFile = TestSupport::fileExists(missingPath);
    static_cast<void>(context.expectTrue("fileExists missing-path inspection succeeds", missingFile.status.ok()));
    static_cast<void>(context.expectFalse("fileExists missing path false", missingFile.value));

    const TestSupport::Types::BoolResult missingContains = TestSupport::fileContains(missingPath, "");
    static_cast<void>(context.expectFalse("fileContains missing file reports failed status", missingContains.status.ok()));
    static_cast<void>(context.expectFalse("fileContains missing file has no match", missingContains.value));

    const TestSupport::Types::BoolResult existingDirectoryResult = TestSupport::fileExists(existingDirectory);
    static_cast<void>(context.expectTrue(
        "createDirectories produces an inspectable directory",
        existingDirectoryResult.status.ok() && existingDirectoryResult.value));

    const TestSupport::Types::TextResult readResult = TestSupport::readTextFile(path);
    static_cast<void>(context.expectTrue("readTextFile succeeds", readResult.status.ok()));
    static_cast<void>(context.expectEq("readTextFile returns contents", std::string("alpha beta alpha beta"), readResult.text));

    const TestSupport::Types::BoolResult containsBeta = TestSupport::fileContains(path, "beta");
    static_cast<void>(context.expectTrue("fileContains read succeeds", containsBeta.status.ok()));
    static_cast<void>(context.expectTrue("fileContains finds text", containsBeta.value));
    const TestSupport::Types::BoolResult containsGamma = TestSupport::fileContains(path, "gamma");
    static_cast<void>(context.expectTrue("fileContains missing-text read succeeds", containsGamma.status.ok()));
    static_cast<void>(context.expectFalse("fileContains rejects missing text", containsGamma.value));

    const TestSupport::Types::CountResult alphaCount = TestSupport::countFileOccurrences(path, "alpha");
    static_cast<void>(context.expectTrue("countFileOccurrences read succeeds", alphaCount.status.ok()));
    static_cast<void>(context.expectEq("countFileOccurrences counts non-overlapping matches", std::size_t{2}, alphaCount.count));
    const TestSupport::Types::CountResult emptyCount = TestSupport::countFileOccurrences(path, "");
    static_cast<void>(context.expectTrue("countFileOccurrences empty-needle read succeeds", emptyCount.status.ok()));
    static_cast<void>(context.expectEq("countFileOccurrences empty needle is zero", std::size_t{0}, emptyCount.count));

    static_cast<void>(context.expectTrue("removeIfExists succeeds", TestSupport::removeIfExists(path).ok()));
    const TestSupport::Types::BoolResult removedFile = TestSupport::fileExists(path);
    static_cast<void>(context.expectTrue("removed-file inspection succeeds", removedFile.status.ok()));
    static_cast<void>(context.expectFalse("removeIfExists removes file", removedFile.value));

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
        static_cast<void>(context.expectTrue("ScopedTemporaryDirectory creates its directory", firstExists.status.ok() && firstExists.value));
        const TestSupport::Types::BoolResult artifactExists = TestSupport::fileExists(firstTemporaryPath / "nested" / "artifact.txt");
        static_cast<void>(
            context.expectTrue("ScopedTemporaryDirectory supports nested artifacts", artifactExists.status.ok() && artifactExists.value));
        static_cast<void>(context.expectNe("ScopedTemporaryDirectory paths are unique", firstTemporaryPath, secondTemporaryPath));
    }
    const TestSupport::Types::BoolResult firstRemoved = TestSupport::fileExists(firstTemporaryPath);
    const TestSupport::Types::BoolResult secondRemoved = TestSupport::fileExists(secondTemporaryPath);
    static_cast<void>(context.expectTrue("First temporary-directory cleanup is inspectable", firstRemoved.status.ok()));
    static_cast<void>(context.expectFalse("ScopedTemporaryDirectory removes its directory tree", firstRemoved.value));
    static_cast<void>(context.expectTrue("Second temporary-directory cleanup is inspectable", secondRemoved.status.ok()));
    static_cast<void>(context.expectFalse("ScopedTemporaryDirectory removes each unique directory", secondRemoved.value));

    const std::filesystem::path originalCurrentPath = std::filesystem::current_path();
    {
        const TestSupport::ScopedCurrentPath temporaryCurrentPath(root);
        static_cast<void>(context.expectTrue("ScopedCurrentPath construction succeeds", temporaryCurrentPath.status().ok()));
        static_cast<void>(context.expectEq("ScopedCurrentPath stores the previous path", originalCurrentPath, temporaryCurrentPath.previousPath()));
        static_cast<void>(context.expectEq("ScopedCurrentPath changes the process path", root, std::filesystem::current_path()));
    }
    static_cast<void>(context.expectEq("ScopedCurrentPath restores the process path", originalCurrentPath, std::filesystem::current_path()));

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
    {
        using FailurePoint = TestSupport::TestHooks::FileFailurePoint;
        constexpr std::uint64_t nativeCode = 0x2001u;
        TestSupport::TestHooks::reset();

        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Read, nativeCode);
        const TestSupport::Types::TextResult injectedRead = TestSupport::readTextFile(path);
        static_cast<void>(context.expectEq(
            "Injected file read reports file failure",
            TestSupport::Types::InfrastructureError::FileOperationFailed,
            injectedRead.status.error));
        static_cast<void>(context.expectEq("Injected file read preserves native code", nativeCode, injectedRead.status.nativeCode));

        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Write, nativeCode);
        static_cast<void>(context.expectEq(
            "Injected file write preserves native code",
            nativeCode,
            TestSupport::writeTextFile(root / "injected_write.txt", "text").nativeCode));

        constexpr std::uint64_t permissionDeniedCode = 5u;
        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Write, permissionDeniedCode);
        const TestSupport::Types::InfrastructureStatus permissionFailure = TestSupport::writeTextFile(root / "permission_denied.txt", "text");
        static_cast<void>(context.expectEq(
            "Injected permission denial reports file failure",
            TestSupport::Types::InfrastructureError::FileOperationFailed,
            permissionFailure.error));
        static_cast<void>(
            context.expectEq("Injected permission denial preserves native access-denied code", permissionDeniedCode, permissionFailure.nativeCode));

        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Exists, nativeCode);
        const TestSupport::Types::BoolResult injectedExists = TestSupport::fileExists(root);
        static_cast<void>(context.expectFalse("Injected existence failure is not absence", injectedExists.status.ok()));
        static_cast<void>(context.expectEq("Injected existence failure preserves native code", nativeCode, injectedExists.status.nativeCode));

        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::CreateDirectories, nativeCode);
        static_cast<void>(context.expectEq(
            "Injected directory creation preserves native code",
            nativeCode,
            TestSupport::createDirectories(root / "injected_directory").nativeCode));

        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Remove, nativeCode);
        static_cast<void>(
            context.expectEq("Injected removal preserves native code", nativeCode, TestSupport::removeIfExists(root / "injected_remove").nativeCode));

        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::TemporaryDirectory, nativeCode);
        const TestSupport::ScopedTemporaryDirectory failedTemporaryDirectory("injected");
        static_cast<void>(context.expectEq(
            "Injected temporary-directory construction preserves native code",
            nativeCode,
            failedTemporaryDirectory.status().nativeCode));
        static_cast<void>(context.expectTrue("Failed temporary-directory guard is inert", failedTemporaryDirectory.path().empty()));

        TestSupport::TestHooks::forceNextFileFailure(FailurePoint::CurrentPath, nativeCode);
        const TestSupport::ScopedCurrentPath failedCurrentPath(root);
        static_cast<void>(
            context.expectEq("Injected current-path construction preserves native code", nativeCode, failedCurrentPath.status().nativeCode));
        static_cast<void>(context.expectTrue("Failed current-path guard is inert", failedCurrentPath.previousPath().empty()));
        static_cast<void>(
            context.expectEq("Failed current-path guard does not mutate process state", originalCurrentPath, std::filesystem::current_path()));

        TestSupport::TestHooks::reset();
    }
#endif
}

/// @brief Verifies strict UTF-8 reads/writes, valid-prefix preservation, and preflight rejection.
void testUtf8TextFileContracts(TestSupport::Context &context, const std::filesystem::path &root)
{
    const auto writeRaw = [](const std::filesystem::path &path, std::string_view bytes)
    {
        std::ofstream raw(path, std::ios::binary | std::ios::trunc);
        raw.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };

    const std::filesystem::path validPath = root / "utf8" / "valid.txt";
    const std::string validText = "Gr\xC3\xBC\xC3\x9F"
                                  "e \xCE\xBB";
    static_cast<void>(context.expectTrue("UTF-8 text write succeeds", TestSupport::writeTextFile(validPath, validText).ok()));
    const TestSupport::Types::TextResult validRead = TestSupport::readTextFile(validPath);
    static_cast<void>(context.expectTrue("UTF-8 text read succeeds", validRead.status.ok()));
    static_cast<void>(context.expectEq("UTF-8 text round trips unchanged", validText, validRead.text));

    const std::string malformed = std::string("prefix") + std::string("\xFF", 1);
    const std::filesystem::path malformedPath = root / "utf8" / "malformed.txt";
    writeRaw(malformedPath, malformed);
    const TestSupport::Types::TextResult malformedRead = TestSupport::readTextFile(malformedPath);
    static_cast<void>(context.expectEq(
        "malformed text read reports encoding failure",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        malformedRead.status.error));
    static_cast<void>(context.expectEq("malformed text read preserves valid prefix", std::string("prefix"), malformedRead.text));

    const std::string incomplete = std::string("prefix") + std::string("\xE2\x82", 2);
    const std::filesystem::path incompletePath = root / "utf8" / "incomplete.txt";
    writeRaw(incompletePath, incomplete);
    const TestSupport::Types::TextResult incompleteRead = TestSupport::readTextFile(incompletePath);
    static_cast<void>(context.expectEq(
        "incomplete text read reports encoding failure",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        incompleteRead.status.error));
    static_cast<void>(context.expectEq("incomplete text read preserves valid prefix", std::string("prefix"), incompleteRead.text));

    const std::filesystem::path preservedPath = root / "utf8" / "preserved.txt";
    static_cast<void>(context.expectTrue("preexisting UTF-8 fixture write succeeds", TestSupport::writeTextFile(preservedPath, "sentinel").ok()));

    const TestSupport::Types::InfrastructureStatus malformedWrite = TestSupport::writeTextFile(preservedPath, malformed);
    static_cast<void>(context.expectEq(
        "malformed text write reports encoding failure",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        malformedWrite.error));
    const TestSupport::Types::TextResult malformedPreservedRead = TestSupport::readTextFile(preservedPath);
    static_cast<void>(context.expectTrue("malformed text write leaves destination readable", malformedPreservedRead.status.ok()));
    static_cast<void>(context.expectEq("malformed text write preserves destination", std::string("sentinel"), malformedPreservedRead.text));

    const TestSupport::Types::InfrastructureStatus incompleteWrite = TestSupport::writeTextFile(preservedPath, incomplete);
    static_cast<void>(context.expectEq(
        "incomplete text write reports encoding failure",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        incompleteWrite.error));
    const TestSupport::Types::TextResult incompletePreservedRead = TestSupport::readTextFile(preservedPath);
    static_cast<void>(context.expectTrue("incomplete text write leaves destination readable", incompletePreservedRead.status.ok()));
    static_cast<void>(context.expectEq("incomplete text write preserves destination", std::string("sentinel"), incompletePreservedRead.text));

    const std::filesystem::path malformedParent = root / "utf8" / "malformed-not-created";
    const TestSupport::Types::InfrastructureStatus malformedNoSideEffect = TestSupport::writeTextFile(malformedParent / "bad.txt", malformed);
    static_cast<void>(context.expectEq(
        "malformed text write rejects before filesystem side effects",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        malformedNoSideEffect.error));
    const TestSupport::Types::BoolResult malformedParentExists = TestSupport::fileExists(malformedParent);
    static_cast<void>(context.expectTrue("malformed rejection parent inspection succeeds", malformedParentExists.status.ok()));
    static_cast<void>(context.expectFalse("malformed rejection does not create parent directory", malformedParentExists.value));

    const std::filesystem::path incompleteParent = root / "utf8" / "incomplete-not-created";
    const TestSupport::Types::InfrastructureStatus incompleteNoSideEffect = TestSupport::writeTextFile(incompleteParent / "bad.txt", incomplete);
    static_cast<void>(context.expectEq(
        "incomplete text write rejects before filesystem side effects",
        TestSupport::Types::InfrastructureError::EncodingFailed,
        incompleteNoSideEffect.error));
    const TestSupport::Types::BoolResult incompleteParentExists = TestSupport::fileExists(incompleteParent);
    static_cast<void>(context.expectTrue("incomplete rejection parent inspection succeeds", incompleteParentExists.status.ok()));
    static_cast<void>(context.expectFalse("incomplete rejection does not create parent directory", incompleteParentExists.value));
}
