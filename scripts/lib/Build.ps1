# Project configure, build, and command execution helpers.

# ------------------------------------------------------------
# Preset configuration and build operations
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Reset-GameWipPresetBuildTree
{
    param([Parameter(Mandatory = $true)][string]$Name)

    # Resolve the selected preset's only allowed removal path.
    Assert-GameWipValidPreset -Kind 'configure' -Name $Name
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))
    $target = Get-GameWipSelectedBuildTarget
    $presetRoot = [IO.Path]::GetFullPath((Get-GameWipBuildDirectory -PresetName $Name -Target $target))
    $relativePresetRoot = Get-GameWipRepositoryRelativePath -Path $presetRoot
    $targetRoot = if ($target.platform -eq 'native')
    {
        $buildRoot
    }
    else
    {
        [IO.Path]::GetFullPath((Join-Path $buildRoot ([string]$target.id)))
    }
    $presetParent = [IO.Path]::GetDirectoryName($presetRoot)

    if (-not [string]::Equals($presetParent, $targetRoot, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing to recreate preset '$Name' outside target '$($target.id)'."
    }

    # Reject reparse points before recursive removal.
    if (Test-Path -LiteralPath $buildRoot)
    {
        $buildRootItem = Get-Item -LiteralPath $buildRoot -Force

        if (($buildRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing to recreate preset '$Name' through reparse-point build root '$buildRoot'."
        }
    }

    if (Test-Path -LiteralPath $targetRoot)
    {
        $targetItem = Get-Item -LiteralPath $targetRoot -Force

        if (($targetItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing to recreate preset '$Name' through reparse-point target tree '$targetRoot'."
        }
    }

    # Remove only the selected target's preset tree.
    if (-not (Test-Path -LiteralPath $presetRoot))
    {
        Write-GameWipOperationEvent -Phase plan -Step "fresh-$Name" -Severity info -Message "Build tree '$relativePresetRoot' does not exist; configuration will create it."
        return
    }

    $presetItem = Get-Item -LiteralPath $presetRoot -Force

    if (($presetItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Refusing to recursively remove reparse-point preset tree '$presetRoot'."
    }

    Write-GameWipOperationEvent -Phase execute -Step "fresh-$Name" -Severity info -Message "Removing build tree '$relativePresetRoot'."
    Remove-Item -LiteralPath $presetRoot -Recurse -Force
}

function Invoke-GameWipConfigurePreset
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [switch]$Fresh,
        [switch]$Offline
    )

    # Build target-specific CMake arguments and output paths.
    Assert-GameWipValidPreset -Kind 'configure' -Name $Name
    $target = Get-GameWipSelectedBuildTarget
    $buildDirectory = Get-GameWipBuildDirectory -PresetName $Name -Target $target

    if ($Fresh)
    {
        Reset-GameWipPresetBuildTree -Name $Name
    }
    Confirm-GameWipToolchain -PresetName $Name
    $pathPrefix = Get-GameWipToolchainPathPrefix
    $arguments = @('--preset', $Name)
    $arguments += @('-B', $buildDirectory)

    if ($target.platform -eq 'windows')
    {
        $arguments += "-DGAMEWIP_TARGET_PLATFORM=$($target.platform)"
        $arguments += "-DGAMEWIP_TARGET_ARCHITECTURE=$($target.architecture)"
        $arguments += "-DGAMEWIP_LLVM_MINGW_ROOT=$(Get-GameWipBuildTargetToolchainRoot -Target $target)"
    }
    $offlineValue = if ($Offline)
    {
        'ON'
    }
    else
    {
        'OFF'
    }
    $arguments += "-DGAMEWIP_DEPENDENCIES_OFFLINE=$offlineValue"
    $cache = Join-Path $buildDirectory 'CMakeCache.txt'

    # Recreate a cache when it belongs to another target compiler.
    if ((Test-GameWipWindowsHost) -and (Test-Path -LiteralPath $cache))
    {
        $compilerEntry = Get-Content -LiteralPath $cache | Where-Object { $_ -match '^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=' } | Select-Object -First 1
        $configuredCompiler = if ($null -ne $compilerEntry)
        {
            ($compilerEntry -split '=', 2)[1]
        }
        else
        {
            ''
        }

        # Presets assign CMAKE_CXX_COMPILER as an untyped value, so the cache can retain only
        # "clang++" while CMake records the resolved executable in its generated compiler state.
        # Read that state before deciding whether --fresh is required; otherwise a toolchain
        # switch makes CMake reset the cache mid-configure and silently drops the preset options.
        if ([string]::IsNullOrWhiteSpace($configuredCompiler) -or -not [IO.Path]::IsPathRooted($configuredCompiler))
        {
            $compilerState = Get-ChildItem -LiteralPath (Join-Path $buildDirectory 'CMakeFiles') `
                -Filter 'CMakeCXXCompiler.cmake' -File -Recurse -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 1

            if ($null -ne $compilerState)
            {
                $resolvedEntry = Get-Content -LiteralPath $compilerState.FullName |
                    Where-Object { $_ -match '^set\(CMAKE_CXX_COMPILER "(?<path>[^"]+)"\)' } |
                    Select-Object -First 1

                if ($null -ne $resolvedEntry -and $resolvedEntry -match '^set\(CMAKE_CXX_COMPILER "(?<path>[^"]+)"\)')
                {
                    $configuredCompiler = $Matches.path
                }
            }
        }

        if ($target.platform -eq 'windows' -and -not [string]::IsNullOrWhiteSpace($configuredCompiler) -and [IO.Path]::IsPathRooted($configuredCompiler))
        {
            $configuredCompiler = $configuredCompiler.Replace('/', '\')
            $expectedCompiler = [IO.Path]::GetFullPath((Get-GameWipBuildTargetCompilerPath -Target $target)).Replace('/', '\')

            if (-not [string]::Equals($configuredCompiler, $expectedCompiler, [StringComparison]::OrdinalIgnoreCase))
            {
                Write-GameWipOperationEvent -Phase plan -Step "configure-$Name" -Severity info -Message "The cached compiler belongs to a different toolchain; CMake will recreate this preset cache."
                $arguments += '--fresh'
            }
        }
    }
    # Configure the isolated target build tree.
    Invoke-GameWipNative -Name "configure-$Name" -FilePath 'cmake' -Arguments $arguments -PathPrefix $pathPrefix
}

function Invoke-GameWipBuildPreset
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [switch]$Fresh,
        [switch]$Offline
    )
    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    $buildDirectory = Get-GameWipBuildDirectory -PresetName $Name
    $cache = Join-Path $buildDirectory 'CMakeCache.txt'

    if ($Fresh)
    {
        Reset-GameWipPresetBuildTree -Name $Name
        Write-GameWipOperationEvent -Phase plan -Step "build-$Name" -Severity info -Message "Fresh build requested; configuration is a prerequisite."
        Invoke-GameWipConfigurePreset -Name $Name -Offline:$Offline
    }
    elseif (-not (Test-Path -LiteralPath $cache))
    {
        Write-GameWipOperationEvent -Phase plan -Step "build-$Name" -Severity info -Message "Build preset '$Name' is not configured; configuration is a prerequisite."
        Invoke-GameWipConfigurePreset -Name $Name -Offline:$Offline
    }
    elseif ($Offline)
    {
        Write-GameWipOperationEvent `
            -Phase plan `
            -Step "configure-$Name-offline" `
            -Severity info `
            -Message "Offline dependency mode requested; refreshing preset '$Name' with downloads disabled."
        Invoke-GameWipConfigurePreset -Name $Name -Offline
    }
    Invoke-GameWipNative -Name "build-$Name" -FilePath 'cmake' -Arguments @('--build', $buildDirectory, '--parallel') -PathPrefix (Get-GameWipToolchainPathPrefix)
}

function Invoke-GameWipBuildTarget
{
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][string]$Target)
    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    $buildDirectory = Get-GameWipBuildDirectory -PresetName $Name
    Invoke-GameWipNative -Name "build-$Name-$Target" -FilePath 'cmake' -Arguments @('--build', $buildDirectory, '--target', $Target) -PathPrefix (Get-GameWipToolchainPathPrefix)
}

function Resolve-GameWipProjectExecutable
{
    param([Parameter(Mandatory = $true)]$Command)
    $buildDirectory = Get-GameWipBuildDirectory -PresetName ([string]$Command.BuildPreset)
    $presetPrefix = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot (Join-Path 'build' ([string]$Command.BuildPreset))))

    $resolvePath = {
        param([string]$RelativePath)
        $fullPath = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $RelativePath))

        if ($fullPath.StartsWith($presetPrefix.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))
        {
            $suffix = $fullPath.Substring($presetPrefix.TrimEnd([IO.Path]::DirectorySeparatorChar).Length).TrimStart([IO.Path]::DirectorySeparatorChar)
            return Join-Path $buildDirectory $suffix
        }

        return $fullPath
    }

    $primary = & $resolvePath ([string]$Command.Executable)

    if (Test-Path -LiteralPath $primary)
    {
        return $primary
    }

    if ($Command.ContainsKey('AlternateExecutable'))
    {
        $alternate = & $resolvePath ([string]$Command.AlternateExecutable)

        if (Test-Path -LiteralPath $alternate)
        {
            return $alternate
        }
    }

    return $primary
}

function Ensure-GameWipTracyProfiler
{
    $profilerPath = Join-Path (Get-GameWipManagedToolRoot) 'tools/tracy/tracy-profiler.exe'

    if (Get-Process -Name 'tracy-profiler' -ErrorAction SilentlyContinue | Select-Object -First 1)
    {
        return
    }

    if (-not (Test-Path -LiteralPath $profilerPath -PathType Leaf))
    {
        throw (New-GameWipDiagnosticException `
                -Code 'profiler-missing' `
                -Summary "Tracy profiler is missing: $profilerPath" `
                -SuggestedActions @("Run '.\\setup.bat profiler' to install the matching profiler tools."))
    }

    try
    {
        Start-Process -FilePath $profilerPath -WorkingDirectory (Split-Path -Parent $profilerPath) | Out-Null
    }

    catch
    {
        throw (New-GameWipDiagnosticException `
                -Code 'profiler-start-failed' `
                -Summary "Could not start the Tracy profiler: $profilerPath" `
                -Details $_.Exception.Message `
                -SuggestedActions @('Check that the managed Tracy tools are usable.', 'Run .\\setup.bat profiler to repair them.'))
    }
}

function Initialize-GameWipProjectCommandBuild
{
    param([Parameter(Mandatory = $true)]$Command, [switch]$NoBuild = $Script:NoBuild)
    $executable = Resolve-GameWipProjectExecutable -Command $Command

    if (Test-Path -LiteralPath $executable)
    {
        return
    }

    if ($NoBuild)
    {
        throw (New-GameWipDiagnosticException `
                -Code 'prerequisite-build-disabled' `
                -Summary "Required executable is missing: $executable" `
                -Details "Automatic prerequisite builds were disabled with -SkipBuild." `
                -SuggestedActions @("Build preset '$($Command.BuildPreset)' first.", 'Rerun without -SkipBuild to let GameWIP ensure prerequisites.'))
    }
    Write-GameWipOperationEvent -Phase plan -Step 'prerequisite-build' -Severity info -Message "Executable is missing; GameWIP will configure/build preset '$($Command.BuildPreset)' first."
    Invoke-GameWipConfigurePreset -Name $Command.BuildPreset
    Invoke-GameWipBuildPreset -Name $Command.BuildPreset
}

function Invoke-GameWipProjectCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string[]]$Arguments = @(),
        [switch]$NoBuild,
        [switch]$ForceBuild
    )
    $command = Get-GameWipProjectCommand -Id $Id
    $hasArguments = $null -ne $Arguments -and $Arguments.Length -ne 0

    if ($hasArguments -and -not [bool]$command.AcceptsExtraArgs)
    {
        throw "Project command '$Id' does not accept extra arguments."
    }

    if ($ForceBuild)
    {
        Invoke-GameWipConfigurePreset -Name $command.BuildPreset
        Invoke-GameWipBuildPreset -Name $command.BuildPreset
    }
    else
    {
        Initialize-GameWipProjectCommandBuild -Command $command -NoBuild:$NoBuild
    }
    $executable = Resolve-GameWipProjectExecutable -Command $command
    $projectArguments = @($command.Arguments) + @($Arguments)
    $inheritConsole = $projectArguments -contains '--manual-tests'
    $workingDirectory = $RepositoryRoot

    if ($command.ContainsKey('WorkingDirectory') -and $command.WorkingDirectory -eq 'build')
    {
        $workingDirectory = Get-GameWipBuildDirectory -PresetName ([string]$command.BuildPreset)
    }

    if ($command.ContainsKey('StartTracyProfiler') -and [bool]$command.StartTracyProfiler)
    {
        Ensure-GameWipTracyProfiler
    }

    if ($command.ContainsKey('InheritConsole') -and [bool]$command.InheritConsole)
    {
        $inheritConsole = $true
    }

    $pathPrefix = if ($command.ContainsKey('TargetRuntimePath') -and [bool]$command.TargetRuntimePath)
    {
        Get-GameWipToolchainPathPrefix
    }
    else
    {
        $null
    }

    Invoke-GameWipNative `
        -Name "project-$Id" `
        -FilePath $executable `
        -Arguments $projectArguments `
        -WorkingDirectory $workingDirectory `
        -PathPrefix $pathPrefix `
        -UseWorkspaceTemp:([bool]$command.UseWorkspaceTemp) `
        -InheritConsole:$inheritConsole
}
