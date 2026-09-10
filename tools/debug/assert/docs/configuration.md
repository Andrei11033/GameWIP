@page assert_configuration Configuration

Assert behavior is controlled by the Assert CMake target. Consumers must configure the library before it is built and must not redefine `ASSERT_*`
macros in application source after including `debug/assert/assert.h`.

## CMake options

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `ASSERT_ENABLED` | `AUTO`, `ON`, `OFF` | `AUTO` | Controls fatal assertion failure handling for `ASSERT`, `VERIFY`, interactive fatal variants, and `UNREACHABLE`. |
| `ASSERT_CHECKS_ENABLED` | `AUTO`, `ON`, `OFF` | `AUTO` | Controls recoverable reporting for `CHECK`, `CHECK_ONCE`, and `ENSURE`. |
| `ASSERT_DIAGNOSTICS` | `ON`, `OFF` | `ON` | Includes condition text, message text, file, line, and function data in failure reports. |
| `ASSERT_UNREACHABLE_ASSUME` | `ON`, `OFF` | `OFF` | Uses compiler unreachable assumptions instead of a trap when `UNREACHABLE()` is compiled without assertion handling. |
| `ASSERT_ENABLE_TEST_HOOKS` | `ON`, `OFF` | `OFF` | Enables source-tree-only validation hooks. It is not consumer API. |

`AUTO` follows the active build configuration: assertions and checks are enabled outside release-style configurations and disabled for `Release`,
`RelWithDebInfo`, and `MinSizeRel`.

## Runtime target form

Assert builds a shared runtime when at least one failure-reporting family can be enabled. If both `ASSERT_ENABLED=OFF` and
`ASSERT_CHECKS_ENABLED=OFF`, Assert becomes an interface-only target. In that mode the installed header still provides the public macros, but no
runtime handlers are linked.

Multi-config generators need a runtime target unless both families are forced off, because different configurations can enable different macro
behavior from the same generated target.

## Public compile definitions

The Assert target propagates the compile definitions that the public header consumes:

| Definition | Owner | Purpose |
| --- | --- | --- |
| `ASSERT_INTERNAL_RUNTIME` | Assert CMake target | Indicates whether the runtime bridge symbols are available. Consumers must not set it manually. |
| `ASSERT_ENABLED` | `ASSERT_ENABLED` CMake option | Selects fatal assertion macro behavior. |
| `ASSERT_CHECKS_ENABLED` | `ASSERT_CHECKS_ENABLED` CMake option | Selects recoverable check macro behavior. |
| `ASSERT_DIAGNOSTICS` | `ASSERT_DIAGNOSTICS` CMake option | Selects diagnostic payload collection and message-expression evaluation. |
| `ASSERT_UNREACHABLE_ASSUME` | `ASSERT_UNREACHABLE_ASSUME` CMake option | Selects the disabled `UNREACHABLE()` backend. |
| `ASSERT_INTERNAL_TEST_HOOKS` | source-tree validation build | Exposes internal hook declarations only to approved test targets. |

`ASSERT_POPUP_ON_ASSERT` and `ASSERT_POPUP_ON_CHECK` are compile-time runtime
controls declared by the public header. The Assert CMake target does not expose
them as cache options. Because popup decisions are compiled into the Assert
runtime, changing those definitions only on a consumer target does not
reconfigure an already-built runtime library.

## Diagnostics

When `ASSERT_DIAGNOSTICS=ON`, `_MSG` arguments are evaluated only on enabled failure paths that need the message. When diagnostics are off, failure
reports keep the failure category but omit condition, message, file, line, and function text.

See @ref assert_diagnostics and @ref assert_macro_behavior for the diagnostic and expression-evaluation contracts.

## Application manifests

Assert does not own an executable manifest. Process-wide resources belong to the application target because they affect the whole process
and must be attached exactly once.

Applications that use Assert's preferred Task Dialog path and no Desktop features may request Common Controls v6 through the shared CMake helper:

```cmake
# In the consumer's top-level directory, after project() and before subdirectories.
if(WIN32)
    enable_language(RC)
endif()

gamewip_attach_application_manifest(
    TARGET MyApplication
    COMMON_CONTROLS_V6
)
```

Applications that use Desktop must select both requirements in one application manifest:

```cmake
gamewip_attach_application_manifest(
    TARGET MyApplication
    COMMON_CONTROLS_V6
    PER_MONITOR_V2
)
```

The helper is available from the source tree and installed `GameWIPApplication` package. It is a no-op on non-Windows platforms, and repeated calls
merge requirements without attaching duplicate resources. Linking `GameWIP::Assert` alone never changes the application manifest.
The repository already enables RC. See @ref project_cmake_infrastructure for the shared helper's target and directory contracts.

## Test hooks

`ASSERT_ENABLE_TEST_HOOKS=ON` is for repository validation builds. It exposes `debug/assert/internal/assert_test_hooks.h` to approved source-tree
targets and must not be used by installed consumers.

See @ref assert_test_hooks for the hook contract.

## Related pages

- @ref assert_public_api
- @ref assert_macro_behavior
- @ref assert_abi
- @ref assert_troubleshooting
