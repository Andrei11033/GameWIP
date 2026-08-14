/// @file environment_test.inl
/// @brief Private TestSupport correctness cases grouped by behavioral responsibility.

    /// @brief Verifies scoped environment set/unset and exact restoration behavior.
    void testEnvironmentHelpers(TestSupport::Context &context)
    {
        const TestSupport::ScopedEnvironmentVariable emptyName("", "value");
        static_cast<void>(context.expectEq(
            "ScopedEnvironmentVariable rejects an empty name",
            TestSupport::Types::InfrastructureError::InvalidArgument,
            emptyName.status().error));

        const TestSupport::ScopedUnsetEnvironmentVariable equalsName("INVALID=NAME");
        static_cast<void>(context.expectEq(
            "ScopedUnsetEnvironmentVariable rejects '=' in a name",
            TestSupport::Types::InfrastructureError::InvalidArgument,
            equalsName.status().error));

        const TestSupport::ScopedEnvironmentVariable invalidUtf8("\xFF", "value");
        static_cast<void>(context.expectEq(
            "ScopedEnvironmentVariable rejects invalid UTF-8",
            TestSupport::Types::InfrastructureError::InvalidArgument,
            invalidUtf8.status().error));

        {
            TestSupport::ScopedUnsetEnvironmentVariable clean(kScopedVariable);
            static_cast<void>(context.expectTrue("Initial scoped unset succeeds", clean.status().ok()));
            static_cast<void>(
                context.expectTrue("Scoped unset clears missing variable", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));

            {
                TestSupport::ScopedEnvironmentVariable scoped(kScopedVariable, "temporary");
                static_cast<void>(context.expectTrue("Scoped environment set succeeds", scoped.status().ok()));
                const char *value = std::getenv(std::string(kScopedVariable).c_str());
                static_cast<void>(
                    context.expectTrue("ScopedEnvironmentVariable sets value", value != nullptr && std::string_view(value) == "temporary"));
            }

            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable restores missing state", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
        }

        {
            TestSupport::ScopedEnvironmentVariable existing(kScopedVariable, "old");
            static_cast<void>(context.expectTrue("Existing environment setup succeeds", existing.status().ok()));
            {
                TestSupport::ScopedEnvironmentVariable nested(kScopedVariable, "new");
                static_cast<void>(context.expectTrue("Nested environment override succeeds", nested.status().ok()));
                const char *value = std::getenv(std::string(kScopedVariable).c_str());
                static_cast<void>(
                    context.expectTrue("ScopedEnvironmentVariable overrides existing value", value != nullptr && std::string_view(value) == "new"));
            }

            const char *restored = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable restores old value", restored != nullptr && std::string_view(restored) == "old"));

            {
                TestSupport::ScopedUnsetEnvironmentVariable unset(kScopedVariable);
                static_cast<void>(context.expectTrue("Nested scoped unset succeeds", unset.status().ok()));
                static_cast<void>(context.expectTrue(
                    "ScopedUnsetEnvironmentVariable clears existing value",
                    std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
            }

            const char *afterUnset = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(context.expectTrue(
                "ScopedUnsetEnvironmentVariable restores old value",
                afterUnset != nullptr && std::string_view(afterUnset) == "old"));
        }

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        {
            using FailurePoint = TestSupport::TestHooks::EnvironmentFailurePoint;
            constexpr std::uint64_t nativeCode = 0x3001u;
            TestSupport::TestHooks::reset();

            TestSupport::TestHooks::forceNextEnvironmentFailure(FailurePoint::Read, nativeCode);
            const TestSupport::ScopedEnvironmentVariable readFailure("INTERNAL_TEST_SUPPORT_INJECTED_READ", "value");
            static_cast<void>(context.expectEq("Injected environment read preserves native code", nativeCode, readFailure.status().nativeCode));

            TestSupport::TestHooks::forceNextEnvironmentFailure(FailurePoint::Set, nativeCode);
            const TestSupport::ScopedEnvironmentVariable setFailure("INTERNAL_TEST_SUPPORT_INJECTED_SET", "value");
            static_cast<void>(context.expectEq("Injected environment set preserves native code", nativeCode, setFailure.status().nativeCode));

            TestSupport::TestHooks::forceNextEnvironmentFailure(FailurePoint::Unset, nativeCode);
            const TestSupport::ScopedUnsetEnvironmentVariable unsetFailure("INTERNAL_TEST_SUPPORT_INJECTED_UNSET");
            static_cast<void>(context.expectEq("Injected environment unset preserves native code", nativeCode, unsetFailure.status().nativeCode));

            TestSupport::TestHooks::reset();
        }
#endif
    }
