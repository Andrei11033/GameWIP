/// @file environment_test.inl
/// @brief Private TestSupport environment and process-state restoration tests.

/// @brief Verifies scoped environment set/unset and exact restoration behavior.
void testEnvironmentHelpers(TestSupport::Context &context)
{
    TestSupport::ScopedProcessState processState;
    static_cast<void>(context.expectTrue("Process-state scope construction succeeds", processState.status().ok()));

    // Reject malformed names and text before changing process state.
    const TestSupport::ScopedEnvironmentVariable emptyName(processState, "", "value");
    static_cast<void>(context.expectEq(
        "ScopedEnvironmentVariable rejects an empty name",
        GameWIP::Status::Types::Code::InvalidArgument,
        emptyName.status().code()));

    const TestSupport::ScopedUnsetEnvironmentVariable equalsName(processState, "INVALID=NAME");
    static_cast<void>(context.expectEq(
        "ScopedUnsetEnvironmentVariable rejects '=' in a name",
        GameWIP::Status::Types::Code::InvalidArgument,
        equalsName.status().code()));

    const TestSupport::ScopedEnvironmentVariable invalidUtf8(processState, "\xFF", "value");
    static_cast<void>(context.expectEq(
        "ScopedEnvironmentVariable rejects invalid UTF-8",
        GameWIP::Status::Types::Code::InvalidArgument,
        invalidUtf8.status().code()));

    // Restore an initially absent variable across nested scopes.
    {
        const TestSupport::ScopedUnsetEnvironmentVariable clean(processState, kScopedVariable);
        static_cast<void>(context.expectTrue("Initial scoped unset succeeds", clean.status().ok()));
        static_cast<void>(context.expectTrue("Scoped unset clears missing variable", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));

        {
            const TestSupport::ScopedEnvironmentVariable scoped(processState, kScopedVariable, "temporary");
            static_cast<void>(context.expectTrue("Scoped environment set succeeds", scoped.status().ok()));
            const char *value = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(context.expectTrue("ScopedEnvironmentVariable sets value", value != nullptr && std::string_view(value) == "temporary"));
        }

        static_cast<void>(
            context.expectTrue("ScopedEnvironmentVariable restores missing state", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
    }

    {
        TestSupport::ScopedEnvironmentVariable explicitRestore(processState, kScopedVariable, "explicit");
        static_cast<void>(context.expectTrue("Explicit environment restoration setup succeeds", explicitRestore.status().ok()));
        const TestSupport::Types::Result<void> restoreStatus = explicitRestore.restore();
        static_cast<void>(context.expectTrue("Explicit environment restoration succeeds", restoreStatus.ok()));
        static_cast<void>(
            context.expectTrue("Explicit environment restoration clears the value", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
        static_cast<void>(context.expectTrue("Environment restoration is idempotent", explicitRestore.restore().ok()));
    }

    // Preserve unrelated external mutations and retain restoration failures.
    {
        TestSupport::ScopedEnvironmentVariable conflicted(processState, kScopedVariable, "guarded");
        static_cast<void>(context.expectTrue("Environment conflict setup succeeds", conflicted.status().ok()));
        static_cast<void>(GameWIP::Validation::PlatformTests::setEnvironmentVariable(kScopedVariable, "external"));
        static_cast<void>(context.expectEq(
            "Environment restore reports unrelated mutation",
            GameWIP::Status::Types::Code::ProcessStateConflict,
            conflicted.restore().status().code()));
        const char *external = std::getenv(std::string(kScopedVariable).c_str());
        static_cast<void>(
            context.expectTrue("Environment conflict preserves the newer value", external != nullptr && std::string_view(external) == "external"));
        static_cast<void>(GameWIP::Validation::PlatformTests::setEnvironmentVariable(kScopedVariable, nullptr));
        static_cast<void>(context.expectEq(
            "Process-state restoration status is sticky after a successful retry",
            GameWIP::Status::Types::Code::ProcessStateConflict,
            processState.restorationStatus().code()));
    }

    // Restore previous values after nested set and unset guards.
    {
        TestSupport::ScopedProcessState destructorProcessState;
        {
            const TestSupport::ScopedEnvironmentVariable destructorOnly(destructorProcessState, kScopedVariable, "destructor-guarded");
            static_cast<void>(context.expectTrue("Destructor-only conflict setup succeeds", destructorOnly.status().ok()));
            static_cast<void>(GameWIP::Validation::PlatformTests::setEnvironmentVariable(kScopedVariable, "destructor-external"));
        }

        static_cast<void>(context.expectEq(
            "Destructor-only restoration conflict is recorded",
            GameWIP::Status::Types::Code::ProcessStateConflict,
            destructorProcessState.restorationStatus().code()));
        static_cast<void>(GameWIP::Validation::PlatformTests::setEnvironmentVariable(kScopedVariable, nullptr));
    }

    {
        const TestSupport::ScopedEnvironmentVariable existing(processState, kScopedVariable, "old");
        static_cast<void>(context.expectTrue("Existing environment setup succeeds", existing.status().ok()));
        {
            const TestSupport::ScopedEnvironmentVariable nested(processState, kScopedVariable, "new");
            static_cast<void>(context.expectTrue("Nested environment override succeeds", nested.status().ok()));
            const char *value = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable overrides existing value", value != nullptr && std::string_view(value) == "new"));
        }

        const char *restored = std::getenv(std::string(kScopedVariable).c_str());
        static_cast<void>(
            context.expectTrue("ScopedEnvironmentVariable restores old value", restored != nullptr && std::string_view(restored) == "old"));

        {
            const TestSupport::ScopedUnsetEnvironmentVariable unset(processState, kScopedVariable);
            static_cast<void>(context.expectTrue("Nested scoped unset succeeds", unset.status().ok()));
            static_cast<void>(context.expectTrue(
                "ScopedUnsetEnvironmentVariable clears existing value",
                std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
        }

        const char *afterUnset = std::getenv(std::string(kScopedVariable).c_str());
        static_cast<void>(
            context.expectTrue("ScopedUnsetEnvironmentVariable restores old value", afterUnset != nullptr && std::string_view(afterUnset) == "old"));
    }
}
