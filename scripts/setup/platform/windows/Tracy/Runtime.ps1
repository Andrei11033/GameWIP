# Tracy runtime dependency staging and compiler compatibility.

# ------------------------------------------------------------
# Runtime dependency closure
# ------------------------------------------------------------

function Copy-GameWipTracyRuntimeDependency
{
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$UcrtBin,
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [AllowNull()][System.Collections.Generic.HashSet[string]]$Visited = $null
    )

    $objdump = Join-Path $UcrtBin 'objdump.exe'
    $pending = [System.Collections.Generic.Queue[string]]::new()

    if ($null -eq $Visited)
    {
        $Visited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    }
    $inspectionClock = [Diagnostics.Stopwatch]::StartNew()
    $inspected = 0
    Write-Host "  Inspecting runtime dependency closure: $([IO.Path]::GetFileName($Executable))"
    $pending.Enqueue($Executable)

    while ($pending.Count -ne 0)
    {
        $binary = $pending.Dequeue()
        $inspection = Invoke-GameWipProcess -FilePath $objdump -Arguments @('-p', $binary) -OutputMode LogOnly -TimeoutSeconds 30
        ++$inspected

        if ($inspectionClock.Elapsed.TotalSeconds -ge 15)
        {
            Write-GameWipHost "  Runtime dependency inspection is still running ($inspected inspected, $($pending.Count) queued)..." -ForegroundColor DarkGray
            $inspectionClock.Restart()
        }

        if ($inspection.ExitCode -ne 0)
        {
            throw "Could not inspect runtime dependencies for $binary (exit $($inspection.ExitCode))."
        }

        foreach ($line in @($inspection.Stdout))
        {
            if ($line -notmatch '^\s*DLL Name:\s*(.+?)\s*$')
            {
                continue
            }

            $dllName = $Matches[1]

            if (-not $Visited.Add($dllName))
            {
                continue
            }

            $ucrtDll = Join-Path $UcrtBin $dllName

            if (-not (Test-Path -LiteralPath $ucrtDll))
            {
                continue
            }

            $stagedDll = Join-Path $StageRoot $dllName

            if (-not (Test-Path -LiteralPath $stagedDll))
            {
                Copy-Item -LiteralPath $ucrtDll -Destination $stagedDll
                Write-Host "  Staged runtime dependency: $dllName"
            }
            $pending.Enqueue($ucrtDll)
        }
    }
}

# ------------------------------------------------------------
# Compiler compatibility and reproducible build
# ------------------------------------------------------------

function Write-GameWipTracyCompilerCompatibilityHeader
{
    param([Parameter(Mandatory = $true)][string]$SetupRoot)

    $header = Join-Path $SetupRoot 'compat\TracyCompilerCompatibility.hpp'
    New-Item -ItemType Directory -Path (Split-Path -Parent $header) -Force | Out-Null
    $content = @'
#pragma once

#if defined(_WIN32) && !defined(_MSC_VER)
#include <cstddef>
#include <cstring>

inline void* memmem(const void* haystack, std::size_t haystackSize, const char* needle, std::size_t needleSize)
{
    auto remaining = std::ptrdiff_t(haystackSize) - std::ptrdiff_t(needleSize);

    while (remaining >= 0)
    {
        if (std::memcmp(haystack, needle, needleSize) == 0)
        {
            return const_cast<void*>(haystack);
        }

        haystack = static_cast<const char*>(haystack) + 1;
        --remaining;
    }

    return nullptr;
}
#endif
'@
    [IO.File]::WriteAllText($header, $content, [Text.UTF8Encoding]::new($false))
    return $header
}
