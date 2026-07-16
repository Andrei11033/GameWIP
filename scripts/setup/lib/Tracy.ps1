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
        $match = [regex]::Match($text, "enum\s*\{\s*$name\s*=\s*(\d+)\s*\}")
        if (-not $match.Success)
        {
            throw "Could not read Tracy $name version from $header."
        }
        $match.Groups[1].Value
    }
    return $parts -join '.'
}

function Get-GameWipTracyExecutables
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

function Test-GameWipTracyTools
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    foreach ($executable in Get-GameWipTracyExecutables)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot ".tracy\$executable")))
        {
            return $false
        }
    }

    $versionFile = Join-Path $RepositoryRoot '.tracy\version.txt'
    if (-not (Test-Path -LiteralPath $versionFile))
    {
        # Executable presence alone cannot prove that the tools match the
        # pinned client. Rebuild once to establish a trustworthy marker.
        return $false
    }
    $expected = "source-$(Get-GameWipTracyVersion -RepositoryRoot $RepositoryRoot)"
    return (Get-Content -LiteralPath $versionFile -Raw).Trim() -eq $expected
}

function Copy-GameWipTracyRuntimeDependencies
{
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$UcrtBin,
        [Parameter(Mandatory = $true)][string]$StageRoot
    )

    $objdump = Join-Path $UcrtBin 'objdump.exe'
    $pending = [System.Collections.Generic.Queue[string]]::new()
    $visited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $pending.Enqueue($Executable)
    while ($pending.Count -ne 0)
    {
        $binary = $pending.Dequeue()
        foreach ($line in & $objdump -p $binary)
        {
            if ($line -notmatch '^\s*DLL Name:\s*(.+?)\s*$')
            {
                continue
            }
            $dllName = $Matches[1]
            if (-not $visited.Add($dllName))
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
        if ($LASTEXITCODE -ne 0)
        {
            throw "Could not inspect runtime dependencies for $binary."
        }
    }
}

function Build-GameWipTracyTools
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$MsysRoot
    )

    $version = Get-GameWipTracyVersion -RepositoryRoot $RepositoryRoot
    if (Test-GameWipTracyTools -RepositoryRoot $RepositoryRoot)
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
    $setupRoot = Join-Path $RepositoryRoot 'build\setup\tracy'
    $buildRoot = Join-Path $setupRoot 'ucrt64'
    $stageRoot = Join-Path $setupRoot 'stage'
    $cacheRoot = Join-Path $setupRoot 'cpm-cache'
    $destination = Join-Path $RepositoryRoot '.tracy'
    Write-Host "  Source: $tracyRoot"
    Write-Host "  Build trees: $buildRoot"
    Write-Host "  Staging: $stageRoot"
    Write-Host "  Verified destination: $destination"
    if (Test-Path -LiteralPath $stageRoot)
    {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null

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
        $env:Path = "$ucrtBin;$previousPath"
        $env:GIT_CONFIG_COUNT = '1'
        $env:GIT_CONFIG_KEY_0 = 'safe.directory'
        $env:GIT_CONFIG_VALUE_0 = $tracyRoot.Replace('\', '/')
        $cmakeUcrtBin = $ucrtBin.Replace('\', '/')
        foreach ($project in $projects)
        {
            Write-Host "Building Tracy $($project.Name) $version from the pinned submodule..."
            $source = Join-Path $tracyRoot $project.Source
            $build = Join-Path $buildRoot $project.Name
            if (Test-Path -LiteralPath $build)
            {
                Remove-Item -LiteralPath $build -Recurse -Force
            }
            Invoke-SetupNative -FilePath $cmake -ArgumentList @(
                '-S', $source, '-B', $build,
                '-G', 'Ninja',
                '-DCMAKE_BUILD_TYPE=Release',
                "-DCMAKE_C_COMPILER=$cmakeUcrtBin/gcc.exe",
                "-DCMAKE_CXX_COMPILER=$cmakeUcrtBin/g++.exe",
                "-DCMAKE_RC_COMPILER=$cmakeUcrtBin/windres.exe",
                '-DCMAKE_CXX_FLAGS=-march=x86-64-v3 -include cstdint',
                '-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++',
                '-DCMAKE_CXX_STANDARD_LIBRARIES=-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lws2_32 -ldbghelp',
                '-DNO_ISA_EXTENSIONS=ON'
            ) | Out-Null

            # Tracy 0.13.1 adds MSVC's /MP switch for every Windows compiler
            # and forces IPO/LTO for Release builds. UCRT64 GCC already gets
            # parallel jobs from Ninja, treats /MP as an input file, and can
            # produce incompatible COFF LTO objects across Tracy's dependency
            # graph. Strip only those generated build flags while keeping the
            # pinned submodule pristine.
            foreach ($ninjaFile in Get-ChildItem -LiteralPath $build -Recurse -Filter '*.ninja')
            {
                $ninjaText = Get-Content -LiteralPath $ninjaFile.FullName -Raw
                $updatedNinjaText = $ninjaText.Replace('/MP', '')
                $updatedNinjaText = $updatedNinjaText.Replace('-flto=auto', '')
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
            Invoke-SetupNative -FilePath $cmake -ArgumentList @(
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
                Copy-GameWipTracyRuntimeDependencies `
                    -Executable $built.FullName `
                    -UcrtBin $ucrtBin `
                    -StageRoot $stageRoot
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

    foreach ($executable in Get-GameWipTracyExecutables)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $executable)))
        {
            throw "The staged Tracy rebuild is incomplete; missing $executable. Existing .tracy tools were not replaced."
        }
    }

    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $stageRoot -File)
    {
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        Write-Host "  Installed: $(Join-Path $destination $file.Name)"
    }
    Set-Content -LiteralPath (Join-Path $destination 'version.txt') -Value "source-$version" -Encoding Ascii

    if (-not (Test-GameWipTracyTools -RepositoryRoot $RepositoryRoot))
    {
        throw 'The rebuilt Tracy tool set failed final verification.'
    }
    Write-Host "  Ready: rebuilt Tracy Windows tools with UCRT64 from pinned client $version"
}
