# Linux Tracy profiler build orchestration.

# ------------------------------------------------------------
# Build and install the locked profiler
# ------------------------------------------------------------

function Invoke-GameWipLinuxTracyToolBuild
{
    # Reuse the profiler when it matches the locked Tracy source.
    $version = Get-GameWipTracyVersion

    if (Test-GameWipLinuxTracyProfiler)
    {
        Write-Host "  Ready: Tracy Wayland and X11 profilers for locked client $version"
        return
    }

    # Resolve the native build tools before creating build state.
    $cmakeTool = Get-GameWipProjectTool -Id cmake
    $cmakeStatus = Get-GameWipToolStatus -Tool $cmakeTool
    $cmake = [string]$cmakeStatus.Detected.Location
    $ninja = Resolve-GameWipSetupCommand -Name 'ninja'
    $pkgConfig = Resolve-GameWipSetupCommand -Name 'pkg-config'
    $waylandScanner = Resolve-GameWipSetupCommand -Name 'wayland-scanner'

    if (-not $cmakeStatus.Ready -or [string]::IsNullOrWhiteSpace($cmake))
    {
        throw 'A compatible CMake installation is required to build the Tracy profiler. Run setup repair first.'
    }

    $requiredTools = @(
        @{ Name = 'Ninja'; Path = $ninja }
        @{ Name = 'pkg-config'; Path = $pkgConfig }
        @{ Name = 'wayland-scanner'; Path = $waylandScanner }
    )

    foreach ($tool in $requiredTools)
    {
        if ([string]::IsNullOrWhiteSpace([string]$tool.Path))
        {
            throw "$($tool.Name) is required to build the Tracy profiler. Run setup system first."
        }
    }

    # Keep the source, build cache, and installed profiler in their managed roots.
    $sourceRoot = Get-GameWipTracySourcePath
    $source = Join-Path $sourceRoot 'profiler'
    $cacheRoot = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'tracy/linux'
    $cpmCache = Join-Path $cacheRoot 'cpm-cache'
    $stageRoot = Join-Path $Script:OperationContext.Temp 'tracy-linux-stage'
    $destination = Get-GameWipLinuxTracyToolRoot
    $previousCpmCache = $env:CPM_SOURCE_CACHE
    $previousBuildRoot = Join-Path $cacheRoot 'build'

    Write-Host "  Source: $sourceRoot"
    Write-Host "  Build: $cacheRoot/<backend>/build"
    Write-Host "  Staging: $stageRoot"
    Write-Host "  Verified destination: $destination"

    # Prepare clean build and temporary staging directories.
    New-Item -ItemType Directory -Path $cacheRoot, $cpmCache -Force | Out-Null

    if (Test-Path -LiteralPath $previousBuildRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $previousBuildRoot -OwnedRoot $cacheRoot
    }

    if (Test-Path -LiteralPath $stageRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $stageRoot -OwnedRoot $Script:OperationContext.Temp -SuppressMutationTracking
    }

    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

    try
    {
        $env:CPM_SOURCE_CACHE = $cpmCache

        # Build one native GUI backend per isolated CMake tree.
        $variants = @(
            @{
                Name = 'wayland'
                Legacy = 'OFF'
                CMakeArguments = @()
            }
            @{
                Name = 'x11'
                Legacy = 'ON'
                CMakeArguments = @(
                    '-DDOWNLOAD_GLFW=ON',
                    '-DGLFW_BUILD_WAYLAND=OFF',
                    '-DGLFW_BUILD_X11=ON'
                )
            }
        )

        foreach ($variant in $variants)
        {
            $variantRoot = Join-Path $cacheRoot $variant.Name
            $buildRoot = Join-Path $variantRoot 'build'

            if (Test-Path -LiteralPath $buildRoot)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $buildRoot -OwnedRoot $variantRoot
            }

            $configureArguments = @(
                '-S', $source,
                '-B', $buildRoot,
                '-G', 'Ninja',
                '-DCMAKE_BUILD_TYPE=Release',
                '-DNO_ISA_EXTENSIONS=ON',
                '-DNO_LTO=ON',
                '-DNO_MOLD_LINKER=ON',
                '-DNO_CCACHE=ON',
                "-DLEGACY=$($variant.Legacy)"
            ) + @($variant.CMakeArguments)

            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList $configureArguments | Out-Null

            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList @(
                '--build', $buildRoot,
                '--target', 'tracy-profiler',
                '--parallel'
            ) | Out-Null

            $builtProfiler = Join-Path $buildRoot 'tracy-profiler'
            $stagedProfiler = Join-Path $stageRoot "tracy-profiler-$($variant.Name)"

            if (-not (Test-Path -LiteralPath $builtProfiler -PathType Leaf))
            {
                throw "Tracy $($variant.Name) build did not produce '$builtProfiler'."
            }

            Copy-Item -LiteralPath $builtProfiler -Destination $stagedProfiler -Force
            Invoke-GameWipSetupNative -FilePath 'chmod' -ArgumentList @('u+x', $stagedProfiler) | Out-Null
        }

        Write-GameWipTextAtomic -Path (Join-Path $stageRoot 'version.txt') -Content "source-$version`n"

        Install-GameWipLinuxTracyProfiler `
            -StageRoot $stageRoot `
            -Destination $destination `
            -Version $version
    }

    finally
    {
        $env:CPM_SOURCE_CACHE = $previousCpmCache
    }
}
