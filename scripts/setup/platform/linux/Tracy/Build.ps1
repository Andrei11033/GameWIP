# Linux Tracy tool build orchestration.

# ------------------------------------------------------------
# Build and install the locked tool set
# ------------------------------------------------------------

function Invoke-GameWipLinuxTracyToolBuild
{
    # Reuse tools that match the locked Tracy source.
    $version = Get-GameWipTracyVersion

    if (Test-GameWipLinuxTracyToolSet)
    {
        Write-Host "  Ready: Tracy tools for locked client $version"
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
        throw 'A compatible CMake installation is required to build Tracy tools. Run setup repair first.'
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
            throw "$($tool.Name) is required to build Tracy tools. Run setup system first."
        }
    }

    # Keep the source, build cache, and installed tools in their managed roots.
    $sourceRoot = Get-GameWipTracySourcePath
    $cacheRoot = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'tracy/linux'
    $cpmCache = Join-Path $cacheRoot 'cpm-cache'
    $stageRoot = Join-Path $Script:OperationContext.Temp 'tracy-linux-stage'
    $destination = Get-GameWipLinuxTracyToolRoot
    $previousCpmCache = $env:CPM_SOURCE_CACHE

    Write-Host "  Source: $sourceRoot"
    Write-Host "  Build: $cacheRoot/<tool>/build"
    Write-Host "  Staging: $stageRoot"
    Write-Host "  Verified destination: $destination"

    # Prepare isolated build and temporary staging directories.
    New-Item -ItemType Directory -Path $cacheRoot, $cpmCache -Force | Out-Null

    if (Test-Path -LiteralPath $stageRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $stageRoot -OwnedRoot $Script:OperationContext.Temp -SuppressMutationTracking
    }

    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

    try
    {
        $env:CPM_SOURCE_CACHE = $cpmCache

        # Build the shared command-line tools and both native GUI backends.
        $projects = @(
            # Shared command-line tools.
            @{
                Name = 'capture'
                Source = 'capture'
                Outputs = @(@{ Target = 'tracy-capture'; File = 'tracy-capture' })
                CMakeArguments = @()
            }
            @{
                Name = 'csvexport'
                Source = 'csvexport'
                Outputs = @(@{ Target = 'tracy-csvexport'; File = 'tracy-csvexport' })
                CMakeArguments = @()
            }
            @{
                Name = 'import'
                Source = 'import'
                Outputs = @(
                    @{ Target = 'tracy-import-chrome'; File = 'tracy-import-chrome' }
                    @{ Target = 'tracy-import-fuchsia'; File = 'tracy-import-fuchsia' }
                )
                CMakeArguments = @()
            }
            @{
                Name = 'update'
                Source = 'update'
                Outputs = @(@{ Target = 'tracy-update'; File = 'tracy-update' })
                CMakeArguments = @()
            }

            # Native profiler variants.
            @{
                Name = 'profiler-wayland'
                Source = 'profiler'
                Outputs = @(@{ Target = 'tracy-profiler'; File = 'tracy-profiler-wayland' })
                CMakeArguments = @('-DLEGACY=OFF')
            }
            @{
                Name = 'profiler-x11'
                Source = 'profiler'
                Outputs = @(@{ Target = 'tracy-profiler'; File = 'tracy-profiler-x11' })
                CMakeArguments = @(
                    '-DLEGACY=ON',
                    '-DDOWNLOAD_GLFW=ON',
                    '-DGLFW_BUILD_WAYLAND=OFF',
                    '-DGLFW_BUILD_X11=ON'
                )
            }
        )

        foreach ($project in $projects)
        {
            Write-Host "Building Tracy $($project.Name) $version from the locked dependency source..."
            $projectSource = Join-Path $sourceRoot $project.Source
            $projectRoot = Join-Path $cacheRoot $project.Name
            $buildRoot = Join-Path $projectRoot 'build'

            if (Test-Path -LiteralPath $buildRoot)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $buildRoot -OwnedRoot $projectRoot
            }

            $configureArguments = @(
                '-S', $projectSource,
                '-B', $buildRoot,
                '-G', 'Ninja',
                '-DCMAKE_BUILD_TYPE=Release',
                '-DNO_ISA_EXTENSIONS=ON',
                '-DNO_LTO=ON',
                '-DNO_MOLD_LINKER=ON',
                '-DNO_CCACHE=ON'
            ) + @($project.CMakeArguments)

            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList $configureArguments | Out-Null

            $targets = @($project.Outputs | ForEach-Object { [string]$_.Target })
            $buildArguments = @('--build', $buildRoot, '--target') + $targets + @('--parallel')
            Invoke-GameWipSetupNative -FilePath $cmake -ArgumentList $buildArguments | Out-Null

            foreach ($output in $project.Outputs)
            {
                $builtOutput = Join-Path $buildRoot ([string]$output.Target)
                $stagedOutput = Join-Path $stageRoot ([string]$output.File)

                if (-not (Test-Path -LiteralPath $builtOutput -PathType Leaf))
                {
                    throw "Tracy $($project.Name) build did not produce '$builtOutput'."
                }

                Copy-Item -LiteralPath $builtOutput -Destination $stagedOutput -Force
                Invoke-GameWipSetupNative -FilePath 'chmod' -ArgumentList @('u+x', $stagedOutput) | Out-Null
            }
        }

        Write-GameWipTextAtomic -Path (Join-Path $stageRoot 'version.txt') -Content "source-$version`n"

        Install-GameWipLinuxTracyToolSet `
            -StageRoot $stageRoot `
            -Destination $destination `
            -Version $version
    }

    finally
    {
        $env:CPM_SOURCE_CACHE = $previousCpmCache
    }
}
