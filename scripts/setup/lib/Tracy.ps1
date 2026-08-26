# GameWIP Tracy version matching, reproducible build cache, staging, and persistent tool installation.

Set-StrictMode -Version Latest

function Get-GameWipTracyVersion
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    $header = Join-Path $RepositoryRoot 'external\tracy\public\common\TracyVersion.hpp'
    if (-not (Test-Path -LiteralPath $header))
    {
        throw 'The Tracy submodule is not initialized; prepare the repository first.'
    }

    $text = Get-Content -LiteralPath $header -Raw
    $parts = foreach ($name in @('Major', 'Minor', 'Patch'))
    {
        $match = [regex]::Match($text, "(?m)^\s*constexpr\s+int\s+$name\s*=\s*(\d+)\s*;")
        if (-not $match.Success)
        {
            throw "Could not read Tracy $name version from $header."
        }
        $match.Groups[1].Value
    }
    return $parts -join '.'
}

function Get-GameWipTracyExecutableSet
{
    return @(
        'tracy-profiler.exe'
        'tracy-capture.exe'
        'tracy-csvexport.exe'
        'tracy-import-chrome.exe'
        'tracy-import-fuchsia.exe'
        'tracy-update.exe'
    )
}

function Get-GameWipTracyToolRoot
{
    return (Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) 'tools\tracy')
}

function Test-GameWipTracyToolSet
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$ToolRoot = (Get-GameWipTracyToolRoot)
    )

    foreach ($executable in Get-GameWipTracyExecutableSet)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $ToolRoot $executable)))
        {
            return $false
        }
    }

    $versionFile = Join-Path $ToolRoot 'version.txt'
    if (-not (Test-Path -LiteralPath $versionFile))
    {
        # Executable presence alone cannot prove that the tools match the
        # pinned client. Rebuild once to establish a trustworthy marker.
        return $false
    }
    $expected = "source-$(Get-GameWipTracyVersion -RepositoryRoot $RepositoryRoot)"
    return (Get-Content -LiteralPath $versionFile -Raw).Trim() -eq $expected
}

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

function Invoke-GameWipTracyToolBuild
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$MsysRoot
    )

    $version = Get-GameWipTracyVersion -RepositoryRoot $RepositoryRoot
    if (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot)
    {
        Write-Host "  Ready: complete Tracy tool set for pinned client $version"
        return
    }

    $ucrtBin = Join-Path $MsysRoot 'ucrt64\bin'
    $cmake = Join-Path $ucrtBin 'cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake))
    {
        throw "UCRT64 CMake is required to build Tracy tools: $cmake"
    }
    $compilers = @(
        (Join-Path $ucrtBin 'gcc.exe'),
        (Join-Path $ucrtBin 'g++.exe'),
        (Join-Path $ucrtBin 'windres.exe'),
        (Join-Path $ucrtBin 'ninja.exe'),
        (Join-Path $ucrtBin 'objdump.exe')
    )
    foreach ($compiler in $compilers)
    {
        if (-not (Test-Path -LiteralPath $compiler))
        {
            throw "The UCRT64 Tracy build toolchain is incomplete; missing $compiler"
        }
    }
    $tracyRoot = Join-Path $RepositoryRoot 'external\tracy'
    $setupRoot = Join-Path $RepositoryRoot (Join-Path $ProjectConfig.storage.cache 'tracy')
    $buildRoot = Join-Path $setupRoot 'ucrt64'
    $stageRoot = Join-Path $Script:OperationContext.Temp 'tracy-stage'
    $cacheRoot = Join-Path $setupRoot 'cpm-cache'
    $destination = Get-GameWipTracyToolRoot
    Write-Host "  Source: $tracyRoot"
    Write-Host "  Build trees: $buildRoot"
    Write-Host "  Staging: $stageRoot"
    Write-Host "  Verified destination: $destination"
    if (Test-Path -LiteralPath $stageRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $stageRoot -OwnedRoot $Script:OperationContext.Temp -SuppressMutationTracking
    }
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
    $compilerCompatibilityHeader = Write-GameWipTracyCompilerCompatibilityHeader -SetupRoot $setupRoot
    $runtimeDependenciesVisited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    $projects = @(
        @{ Name = 'profiler'; Source = 'profiler'; Outputs = @('tracy-profiler.exe') }
        @{ Name = 'capture'; Source = 'capture'; Outputs = @('tracy-capture.exe') }
        @{ Name = 'csvexport'; Source = 'csvexport'; Outputs = @('tracy-csvexport.exe') }
        @{ Name = 'import'; Source = 'import'; Outputs = @('tracy-import-chrome.exe', 'tracy-import-fuchsia.exe') }
        @{ Name = 'update'; Source = 'update'; Outputs = @('tracy-update.exe') }
    )

    $previousCache = $env:CPM_SOURCE_CACHE
    $previousPath = $env:Path
    $previousGitConfigCount = $env:GIT_CONFIG_COUNT
    $previousGitConfigKey = $env:GIT_CONFIG_KEY_0
    $previousGitConfigValue = $env:GIT_CONFIG_VALUE_0
    try
    {
        $env:CPM_SOURCE_CACHE = $cacheRoot
        $env:Path = @($ucrtBin, $previousPath) -join [IO.Path]::PathSeparator
        $env:GIT_CONFIG_COUNT = '1'
        $env:GIT_CONFIG_KEY_0 = 'safe.directory'
        $env:GIT_CONFIG_VALUE_0 = $tracyRoot.Replace('\', '/')
        $cmakeUcrtBin = $ucrtBin.Replace('\', '/')
        $cmakeCompilerCompatibilityHeader = $compilerCompatibilityHeader.Replace('\', '/')
        foreach ($project in $projects)
        {
            Write-Host "Building Tracy $($project.Name) $version from the pinned submodule..."
            $source = Join-Path $tracyRoot $project.Source
            $build = Join-Path $buildRoot $project.Name
            if (Test-Path -LiteralPath $build)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $build -OwnedRoot $buildRoot
            }
            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList @(
                '-S', $source, '-B', $build,
                '-G', 'Ninja',
                '-DCMAKE_BUILD_TYPE=Release',
                "-DCMAKE_C_COMPILER=$cmakeUcrtBin/gcc.exe",
                "-DCMAKE_CXX_COMPILER=$cmakeUcrtBin/g++.exe",
                "-DCMAKE_RC_COMPILER=$cmakeUcrtBin/windres.exe",
                "-DCMAKE_CXX_FLAGS=-march=x86-64-v3 -include cstdint -include `"$cmakeCompilerCompatibilityHeader`"",
                '-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++',
                '-DCMAKE_CXX_STANDARD_LIBRARIES=-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lws2_32 -ldbghelp -lsecur32',
                '-DNO_ISA_EXTENSIONS=ON'
            ) | Out-Null

            # Tracy forces IPO/LTO for Release builds, which can produce
            # incompatible COFF LTO objects across its dependency graph under
            # UCRT64 GCC. Strip only those generated flags while keeping the
            # pinned submodule pristine.
            foreach ($ninjaFile in Get-ChildItem -LiteralPath $build -Recurse -Filter '*.ninja')
            {
                $ninjaText = Get-Content -LiteralPath $ninjaFile.FullName -Raw
                $updatedNinjaText = $ninjaText.Replace('-flto=auto', '')
                $updatedNinjaText = $updatedNinjaText.Replace('-fno-fat-lto-objects', '')
                if ($updatedNinjaText -ne $ninjaText)
                {
                    [IO.File]::WriteAllText(
                        $ninjaFile.FullName,
                        $updatedNinjaText,
                        [Text.UTF8Encoding]::new($false)
                    )
                }
            }
            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList @(
                '--build', $build, '--parallel', '--clean-first'
            ) | Out-Null

            foreach ($output in $project.Outputs)
            {
                $built = Get-ChildItem -LiteralPath $build -Recurse -Filter $output |
                    Select-Object -First 1
                if (-not $built)
                {
                    throw "Tracy $($project.Name) build did not produce $output."
                }
                Copy-Item -LiteralPath $built.FullName -Destination (Join-Path $stageRoot $output) -Force
                Copy-GameWipTracyRuntimeDependency `
                    -Executable $built.FullName `
                    -UcrtBin $ucrtBin `
                    -StageRoot $stageRoot `
                    -Visited $runtimeDependenciesVisited
            }
        }
    }
    finally
    {
        $env:CPM_SOURCE_CACHE = $previousCache
        $env:Path = $previousPath
        $env:GIT_CONFIG_COUNT = $previousGitConfigCount
        $env:GIT_CONFIG_KEY_0 = $previousGitConfigKey
        $env:GIT_CONFIG_VALUE_0 = $previousGitConfigValue
    }

    foreach ($executable in Get-GameWipTracyExecutableSet)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $executable)))
        {
            throw "The staged Tracy rebuild is incomplete; missing $executable. Existing managed tools were not replaced."
        }
    }

    $destinationParent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    $incoming = Join-Path $destinationParent ('.tracy.{0}.incoming' -f [guid]::NewGuid().ToString('N'))
    $backup = $null
    try
    {
        New-Item -ItemType Directory -Path $incoming | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $stageRoot -File)
        {
            Copy-Item -LiteralPath $file.FullName -Destination $incoming -Force
        }
        Write-GameWipTextAtomic -Path (Join-Path $incoming 'version.txt') -Content "source-$version`n"
        if (-not (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot -ToolRoot $incoming))
        {
            throw 'The staged Tracy tool set failed final verification. Existing managed tools were not replaced.'
        }
        if (Test-Path -LiteralPath $destination)
        {
            $backup = Join-Path $destinationParent ('.tracy.{0}.backup' -f [guid]::NewGuid().ToString('N'))
            Set-GameWipMutationStarted
            Move-Item -LiteralPath $destination -Destination $backup
        }
        else
        {
            Set-GameWipMutationStarted
        }
        try
        {
            Move-Item -LiteralPath $incoming -Destination $destination
            $incoming = $null
        }
        catch
        {
            if ($null -ne $backup -and (Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $destination))
            {
                Move-Item -LiteralPath $backup -Destination $destination
                $backup = $null
            }
            throw
        }
        Add-GameWipOperationChange -Message "Installed verified Tracy tools for pinned client $version."
    }
    finally
    {
        if ($null -ne $incoming -and (Test-Path -LiteralPath $incoming))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $incoming -OwnedRoot $destinationParent -SuppressMutationTracking
        }
    }

    if (-not (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot))
    {
        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            if (Test-Path -LiteralPath $destination)
            {
                Invoke-GameWipOwnedTreeRemoval `
                    -Path $destination `
                    -OwnedRoot $destinationParent
            }

            Move-Item -LiteralPath $backup -Destination $destination
            $backup = $null

            throw 'The installed Tracy tool set failed post-swap verification; the previous verified tool set was restored.'
        }

        throw 'The installed Tracy tool set failed post-swap verification.'
    }

    if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
    {
        Invoke-GameWipOwnedTreeRemoval -Path $backup -OwnedRoot $destinationParent
        $backup = $null
    }
    foreach ($file in Get-ChildItem -LiteralPath $destination -File)
    {
        Write-Host "  Installed: $($file.FullName)"
    }
    Write-Host "  Ready: rebuilt Tracy Windows tools with UCRT64 from pinned client $version"
}
