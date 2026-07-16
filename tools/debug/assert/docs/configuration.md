@page assert_configuration Assert configuration

Assert behavior is controlled by the Assert CMake target. Consumers should configure the library before it is built and should not redefine `ASSERT_*` macros in application source after including `debug/assert/assert.h`.

## CMake options

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `ASSERT_ENABLED` | `AUTO`, `ON`, `OFF` | `AUTO` | Controls fatal assertion failure handling for `ASSERT`, `VERIFY`, interactive fatal variants, and `UNREACHABLE`. |
| `ASSERT_CHECKS_ENABLED` | `AUTO`, `ON`, `OFF` | `AUTO` | Controls recoverable reporting for `CHECK`, `CHECK_ONCE`, and `ENSURE`. |
| `ASSERT_DIAGNOSTICS` | `ON`, `OFF` | `ON` | Includes condition text, message text, file, line, and function data in failure reports. |
| `ASSERT_UNREACHABLE_ASSUME` | `ON`, `OFF` | `OFF` | Uses compiler unreachable assumptions instead of a trap when `UNREACHABLE()` is compiled without assertion handling. |
| `ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST` | `ON`, `OFF` | `ON` | On Windows, propagates the Common Controls v6 manifest resource needed by the preferred interactive dialog. |
| `ASSERT_ENABLE_TEST_HOOKS` | `ON`, `OFF` | `OFF` | Enables source-tree-only validation hooks. It is not consumer API. |

`AUTO` follows the active build configuration: assertions and checks are enabled outside release-style configurations and disabled for `Release`, `RelWithDebInfo`, and `MinSizeRel`.

## Runtime target form

Assert builds a shared runtime when at least one failure-reporting family can be enabled. If both `ASSERT_ENABLED=OFF` and `ASSERT_CHECKS_ENABLED=OFF`, Assert becomes an interface-only target. In that mode the installed header still provides the public macros, but no runtime handlers are linked.

Multi-config generators need a runtime target unless both families are forced off, because different configurations can enable different macro behavior from the same generated target.

## Public compile definitions

The Assert target propagates the compile definitions that the public header consumes:

| Definition | Owner | Purpose |
| --- | --- | --- |
| `INTERNAL_ASSERT_RUNTIME` | Assert CMake target | Indicates whether the runtime bridge symbols are available. Consumers should not set it manually. |
| `ASSERT_ENABLED` | `ASSERT_ENABLED` CMake option | Selects fatal assertion macro behavior. |
| `ASSERT_CHECKS_ENABLED` | `ASSERT_CHECKS_ENABLED` CMake option | Selects recoverable check macro behavior. |
| `ASSERT_DIAGNOSTICS` | `ASSERT_DIAGNOSTICS` CMake option | Selects diagnostic payload collection and message-expression evaluation. |
| `ASSERT_UNREACHABLE_ASSUME` | `ASSERT_UNREACHABLE_ASSUME` CMake option | Selects the disabled `UNREACHABLE()` backend. |
| `INTERNAL_ASSERT_TEST_HOOKS` | source-tree validation build | Exposes internal hook declarations only to approved test targets. |

`ASSERT_POPUP_ON_ASSERT` and `ASSERT_POPUP_ON_CHECK` are compile-time runtime controls declared by the public header. The current CMake target does not expose them as cache options. Because popup decisions are compiled into the Assert runtime, changing those definitions only on a consumer target does not reconfigure an already-built runtime library.

## Diagnostics

When `ASSERT_DIAGNOSTICS=ON`, `_MSG` arguments are evaluated only on enabled failure paths that need the message. When diagnostics are off, failure reports keep the failure category but omit condition, message, file, line, and function text.

See @ref assert_diagnostics and @ref assert_macro_behavior for the diagnostic and expression-evaluation contracts.

## Windows Common Controls manifest

Installed Windows consumers that link `GameWIP::Assert` receive the Common Controls v6 resource automatically when `ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST=ON`. This enables the preferred Task Dialog path for interactive assertions.

The installed package also provides:

```cmake
assert_enable_common_controls_v6(<target>)
```

Use this helper only for unusual targets that need the resource but do not link `GameWIP::Assert` directly. It is a CMake package helper, not a C++ API.

## Test hooks

`ASSERT_ENABLE_TEST_HOOKS=ON` is for repository validation builds. It exposes `debug/assert/internal/assert_test_hooks.h` to approved source-tree targets and should not be used by installed consumers.

See @ref assert_test_hooks for the hook contract.

## Related pages

- @ref assert_public_api
- @ref assert_macro_behavior
- @ref assert_abi
- @ref assert_troubleshooting
