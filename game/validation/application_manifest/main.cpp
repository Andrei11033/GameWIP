/// @file main.cpp
/// @brief Checks the embedded application manifest and effective Windows DPI policy.

#if defined(_WIN32)
#include <windows.h>

#include <string_view>

#ifndef EXPECT_COMMON_CONTROLS
#define EXPECT_COMMON_CONTROLS 1
#endif
namespace
{
    // Count resource names as well as checking ID 1 so extra manifests cannot
    // hide behind a valid application resource.
    BOOL CALLBACK countManifest(HMODULE, LPCWSTR, LPWSTR, LONG_PTR parameter)
    {
        ++*reinterpret_cast<unsigned int *>(parameter);
        return TRUE;
    }
} // namespace
#endif

int main()
{
#if defined(_WIN32)
    const HMODULE module = GetModuleHandleW(nullptr);
    unsigned int manifestCount = 0;
    if (!EnumResourceNamesW(module, MAKEINTRESOURCEW(24), countManifest, reinterpret_cast<LONG_PTR>(&manifestCount)) || manifestCount != 1)
    {
        return 1;
    }

    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
    if (resource == nullptr)
    {
        return 2;
    }

    const HGLOBAL loaded = LoadResource(module, resource);
    const auto *data = static_cast<const char *>(LockResource(loaded));
    if (data == nullptr)
    {
        return 3;
    }

    const std::string_view manifest(data, SizeofResource(module, resource));
    const bool commonControls = manifest.contains("Microsoft.Windows.Common-Controls");
    if (commonControls != (EXPECT_COMMON_CONTROLS != 0) || !manifest.contains("PerMonitorV2"))
    {
        return 4;
    }

    // Inspect the context established before main; the helper must not depend
    // on a library changing process policy after startup.
    if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        return 5;
    }
#endif
    return 0;
}
