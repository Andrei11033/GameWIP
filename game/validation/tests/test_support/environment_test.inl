/// @file environment_test.inl
/// @brief Private TestSupport correctness cases grouped by behavioral responsibility.

/// @brief Verifies scoped environment set/unset and exact restoration behavior.
void testEnvironmentHelpers(TestSupport::Context &context)
{
    TestSupport::ScopedProcessState processState;
    static_cast<void>(context.expectTrue("Process-state scope construction succeeds", processState.status().ok()));

    const TestSupport::ScopedEnvironmentVariable emptyName(processState, "", "value");
    static_cast<void>(context.expectEq(
        "ScopedEnvironmentVariable rejects an empty name",
        TestSupport::Types::InfrastructureError::InvalidArgument,
        emptyName.status().error));

    const TestSupport::ScopedUnsetEnvironmentVariable equalsName(processState, "INVALID=NAME");
    static_cast<void>(context.expectEq(
        "ScopedUnsetEnvironmentVariable rejects '=' in a name",
        TestSupport::Types::InfrastructureError::InvalidArgument,
        equalsName.status().error));

    const TestSupport::ScopedEnvironmentVariable invalidUtf8(processState, "\xFF", "value");
    static_cast<void>(context.expectEq(
        "ScopedEnvironmentVariable rejects invalid UTF-8",
        TestSupport::Types::InfrastructureError::InvalidArgument,
        invalidUtf8.status().error));

    {
        TestSupport::ScopedUnsetEnvironmentVariable clean(processState, kScopedVariable);
        static_cast<void>(context.expectTrue("Initial scoped unset succeeds", clean.status().ok()));
        static_cast<void>(context.expectTrue("Scoped unset clears missing variable", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));

        {
            TestSupport::ScopedEnvironmentVariable scoped(processState, kScopedVariable, "temporary");
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
        const TestSupport::Types::InfrastructureStatus restoreStatus = explicitRestore.restore();
        static_cast<void>(context.expectTrue("Explicit environment restoration succeeds", restoreStatus.ok()));
        static_cast<void>(
            context.expectTrue("Explicit environment restoration clears the value", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
        static_cast<void>(context.expectTrue("Environment restoration is idempotent", explicitRestore.restore().ok()));
    }

    {
        TestSupport::ScopedEnvironmentVariable conflicted(processState, kScopedVariable, "guarded");
        static_cast<void>(context.expectTrue("Environment conflict setup succeeds", conflicted.status().ok()));
#if defined(_WIN32)
        static_cast<void>(_putenv_s(std::string(kScopedVariable).c_str(), "external"));
#else
        static_cast<void>(setenv(std::string(kScopedVariable).c_str(), "external", 1));
#endif
        static_cast<void>(context.expectEq(
            "Environment restore reports unrelated mutation",
            TestSupport::Types::InfrastructureError::ProcessStateConflict,
            conflicted.restore().error));
        const char *external = std::getenv(std::string(kScopedVariable).c_str());
        static_cast<void>(
            context.expectTrue("Environment conflict preserves the newer value", external != nullptr && std::string_view(external) == "external"));
#if defined(_WIN32)
        static_cast<void>(_putenv_s(std::string(kScopedVariable).c_str(), ""));
#else
        static_cast<void>(unsetenv(std::string(kScopedVariable).c_str()));
#endif
        static_cast<void>(context.expectEq(
            "Process-state restoration status is sticky after a successful retry",
            TestSupport::Types::InfrastructureError::ProcessStateConflict,
            processState.restorationStatus().error));
    }

    {
        TestSupport::ScopedProcessState destructorProcessState;
        {
            TestSupport::ScopedEnvironmentVariable destructorOnly(destructorProcessState, kScopedVariable, "destructor-guarded");
            static_cast<void>(context.expectTrue("Destructor-only conflict setup succeeds", destructorOnly.status().ok()));
#if defined(_WIN32)
            static_cast<void>(_putenv_s(std::string(kScopedVariable).c_str(), "destructor-external"));
#else
            static_cast<void>(setenv(std::string(kScopedVariable).c_str(), "destructor-external", 1));
#endif
        }

        static_cast<void>(context.expectEq(
            "Destructor-only restoration conflict is recorded",
            TestSupport::Types::InfrastructureError::ProcessStateConflict,
            destructorProcessState.restorationStatus().error));
#if defined(_WIN32)
        static_cast<void>(_putenv_s(std::string(kScopedVariable).c_str(), ""));
#else
        static_cast<void>(unsetenv(std::string(kScopedVariable).c_str()));
#endif
    }

    {
        TestSupport::ScopedEnvironmentVariable existing(processState, kScopedVariable, "old");
        static_cast<void>(context.expectTrue("Existing environment setup succeeds", existing.status().ok()));
        {
            TestSupport::ScopedEnvironmentVariable nested(processState, kScopedVariable, "new");
            static_cast<void>(context.expectTrue("Nested environment override succeeds", nested.status().ok()));
            const char *value = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable overrides existing value", value != nullptr && std::string_view(value) == "new"));
        }

        const char *restored = std::getenv(std::string(kScopedVariable).c_str());
        static_cast<void>(
            context.expectTrue("ScopedEnvironmentVariable restores old value", restored != nullptr && std::string_view(restored) == "old"));

        {
            TestSupport::ScopedUnsetEnvironmentVariable unset(processState, kScopedVariable);
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
