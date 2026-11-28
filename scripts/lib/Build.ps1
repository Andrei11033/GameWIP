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

    if (-not [string]::Equals($presetParent, $targetRoot, (Get-GameWipPathComparison)))
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
        Write-GameWipOperationEvent `
            -Phase plan `
            -Step "fresh-$Name" `
            -Severity info `
            -Message "Build tree '$relativePresetRoot' does not exist; configuration will create it."
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

function Test-GameWipPresetConfigured
{
    param([Parameter(Mandatory = $true)][string]$BuildDirectory)

    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'

    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf))
    {
        return $false
    }

    $cacheValues = @{}

    foreach ($line in Get-Content -LiteralPath $cachePath -ErrorAction SilentlyContinue)
    {
        if ($line -match '^(?<key>[^:#]+):[^=]*=(?<value>.*)$')
        {
            $cacheValues[$Matches.key] = $Matches.value
        }
    }

    if ($cacheValues['CMAKE_PROJECT_NAME'] -ne 'GameWIP' -or $cacheValues['CMAKE_GENERATOR'] -notlike 'Ninja*')
    {
        return $false
    }

    $configuredSource = [string]$cacheValues['CMAKE_HOME_DIRECTORY']

    if ([string]::IsNullOrWhiteSpace($configuredSource))
    {
        return $false
    }

    try
    {
        $configuredSource = [IO.Path]::GetFullPath($configuredSource)
        $expectedSource = [IO.Path]::GetFullPath($RepositoryRoot)
    }
    catch
    {
        return $false
    }

    $pathComparison = Get-GameWipPathComparison
    if (-not [string]::Equals($configuredSource, $expectedSource, $pathComparison))
    {
        return $false
    }

    return Test-Path -LiteralPath (Join-Path $BuildDirectory 'build.ninja') -PathType Leaf
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

    if ($target.platform -in @('windows', 'linux'))
    {
        $arguments += "-DGAMEWIP_TARGET_PLATFORM=$($target.platform)"
        $arguments += "-DGAMEWIP_TARGET_ARCHITECTURE=$($target.architecture)"
        $toolchainVariable = ([string]$target.toolchain).ToUpperInvariant() -replace '-', '_'
        $toolchainRootVariable = "GAMEWIP_${toolchainVariable}_ROOT"
        $toolchainRoot = Get-GameWipBuildTargetToolchainRoot -Target $target
        $arguments += "-D$toolchainRootVariable=$toolchainRoot"
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

    # Replace partial or foreign caches before configuring the preset.
    if ((Test-Path -LiteralPath $cache) -and -not (Test-GameWipPresetConfigured -BuildDirectory $buildDirectory))
    {
        Write-GameWipOperationEvent `
            -Phase plan `
            -Step "configure-$Name" `
            -Severity info `
            -Message "Preset '$Name' has no usable CMake configuration; CMake will regenerate it."
        $arguments += '--fresh'
    }

    # Recreate a cache when it belongs to another target compiler.
    if ((Test-Path -LiteralPath $cache) -and $target.platform -in @('windows', 'linux'))
    {
        $compilerEntry = Get-Content -LiteralPath $cache |
            Where-Object { $_ -match '^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=' } |
            Select-Object -First 1
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

        if (
            $target.platform -in @('windows', 'linux') -and
            -not [string]::IsNullOrWhiteSpace($configuredCompiler) -and
            [IO.Path]::IsPathRooted($configuredCompiler)
        )
        {
            $configuredCompiler = [IO.Path]::GetFullPath($configuredCompiler)
            $expectedCompiler = [IO.Path]::GetFullPath((Get-GameWipBuildTargetCompilerPath -Target $target))
            $pathComparison = Get-GameWipPathComparison

            if (-not [string]::Equals($configuredCompiler, $expectedCompiler, $pathComparison))
            {
                Write-GameWipOperationEvent `
                    -Phase plan `
                    -Step "configure-$Name" `
                    -Severity info `
                    -Message "The cached compiler belongs to a different toolchain; CMake will recreate this preset cache."

                if ($arguments -notcontains '--fresh')
                {
                    $arguments += '--fresh'
                }
            }
        }
    }

    # Configure the isolated target build tree.
    Invoke-GameWipNative `
        -Name "configure-$Name" `
        -FilePath 'cmake' `
        -Arguments $arguments `
        -PathPrefix $pathPrefix
}

function Get-GameWipBuildPresetTargets
{
    param([Parameter(Mandatory = $true)][string]$Name)

    $buildPreset = @($PresetData.buildPresets | Where-Object { $_.name -eq $Name } | Select-Object -First 1)

    if ($buildPreset.Count -eq 0)
    {
        throw "Build preset '$Name' is missing from CMakePresets.json."
    }

    if ($buildPreset[0].PSObject.Properties.Name -notcontains 'targets')
    {
        return @()
    }

    return @($buildPreset[0].targets | ForEach-Object { [string]$_ })
}

function Invoke-GameWipBuildPreset
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [switch]$Fresh,
        [switch]$Offline
    )

    # Ensure a usable configure tree before invoking the build preset.
    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    $buildDirectory = Get-GameWipBuildDirectory -PresetName $Name
    $cache = Join-Path $buildDirectory 'CMakeCache.txt'

    if ($Fresh)
    {
        Reset-GameWipPresetBuildTree -Name $Name
        Write-GameWipOperationEvent `
            -Phase plan `
            -Step "build-$Name" `
            -Severity info `
            -Message "Fresh build requested; configuration is a prerequisite."
        Invoke-GameWipConfigurePreset -Name $Name -Offline:$Offline
    }
    elseif (-not (Test-GameWipPresetConfigured -BuildDirectory $buildDirectory))
    {
        $message = if (Test-Path -LiteralPath $cache)
        {
            "Build preset '$Name' has no usable CMake configuration; configuration is a prerequisite."
        }
        else
        {
            "Build preset '$Name' is not configured; configuration is a prerequisite."
        }
        Write-GameWipOperationEvent `
            -Phase plan `
            -Step "build-$Name" `
            -Severity info `
            -Message $message
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

    # Build only the targets declared by the preset.
    $arguments = @('--build', $buildDirectory)
    $targets = @(Get-GameWipBuildPresetTargets -Name $Name)

    if ($targets.Count -gt 0)
    {
        $arguments += '--target'
        $arguments += $targets
    }

    $arguments += '--parallel'

    Invoke-GameWipNative `
        -Name "build-$Name" `
        -FilePath 'cmake' `
        -Arguments $arguments `
        -PathPrefix (Get-GameWipToolchainPathPrefix)
}

function Invoke-GameWipBuildTarget
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Target
    )

    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    $buildDirectory = Get-GameWipBuildDirectory -PresetName $Name
    Invoke-GameWipNative `
        -Name "build-$Name-$Target" `
        -FilePath 'cmake' `
        -Arguments @('--build', $buildDirectory, '--target', $Target) `
        -PathPrefix (Get-GameWipToolchainPathPrefix)
}

# ------------------------------------------------------------
# Project executable and runtime operations
# ------------------------------------------------------------

function Resolve-GameWipProjectExecutable
{
    param([Parameter(Mandatory = $true)]$Command)

    $buildDirectory = Get-GameWipBuildDirectory -PresetName ([string]$Command.BuildPreset)
    $target = Get-GameWipSelectedBuildTarget
    $targetSuffix = switch ([string]$target.platform)
    {
        'windows'
        {
            '.exe'
        }
        'linux'
        {
            ''
        }
        default
        {
            [IO.Path]::GetExtension([string]$Command.Executable)
        }
    }
    $presetPrefix = [IO.Path]::GetFullPath(
        (Join-Path $RepositoryRoot (Join-Path 'build' ([string]$Command.BuildPreset)))
    )

    $resolvePath = {
        param([string]$RelativePath)

        $separator = [IO.Path]::DirectorySeparatorChar
        $RelativePath = $RelativePath.Replace('\', $separator).Replace('/', $separator)
        $extension = [IO.Path]::GetExtension($RelativePath)

        if ($extension -ine $targetSuffix)
        {
            if ($targetSuffix -eq '')
            {
                $RelativePath = $RelativePath.Substring(0, $RelativePath.Length - $extension.Length)
            }
            else
            {
                $RelativePath = [IO.Path]::ChangeExtension($RelativePath, $targetSuffix)
            }
        }

        $fullPath = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $RelativePath))
        $pathComparison = Get-GameWipPathComparison

        $presetDirectory = $presetPrefix.TrimEnd([IO.Path]::DirectorySeparatorChar)
        $presetPrefixWithSeparator = $presetDirectory + [IO.Path]::DirectorySeparatorChar

        if ($fullPath.StartsWith($presetPrefixWithSeparator, $pathComparison))
        {
            $suffix = $fullPath.Substring($presetDirectory.Length).TrimStart([IO.Path]::DirectorySeparatorChar)
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

function Start-GameWipTracyProfiler
{
    $backend = Get-GameWipTracyProfilerBackend
    $platform = if (Test-GameWipWindowsHost)
    {
        'windows'
    }
    else
    {
        'linux'
    }
    $executable = switch ($backend)
    {
        'windows'
        {
            'tracy-profiler.exe'
        }
        'wayland'
        {
            'tracy-profiler-wayland'
        }
        'x11'
        {
            'tracy-profiler-x11'
        }
        default
        {
            ''
        }
    }

    if ([string]::IsNullOrWhiteSpace($executable))
    {
        $suggestedActions = @('Run the profiling command from a Wayland or X11 desktop session.')
        $failure = New-GameWipDiagnosticException `
            -Code 'profiler-session-missing' `
            -Summary 'No Linux graphical session is available for Tracy.' `
            -SuggestedActions $suggestedActions
        throw $failure
    }

    $profilerPath = Join-Path (Get-GameWipManagedToolRoot) "tools/tracy/$platform/$executable"
    $processName = [IO.Path]::GetFileNameWithoutExtension($executable)

    if (Get-Process -Name $processName -ErrorAction SilentlyContinue | Select-Object -First 1)
    {
        return
    }

    if (-not (Test-Path -LiteralPath $profilerPath -PathType Leaf))
    {
        $suggestedActions = @("Run '$(Get-GameWipSetupHelperCommand) profiler' to install the matching Tracy profiler.")
        $failure = New-GameWipDiagnosticException `
            -Code 'profiler-missing' `
            -Summary "Tracy profiler is missing: $profilerPath" `
            -SuggestedActions $suggestedActions
        throw $failure
    }

    try
    {
        Start-Process -FilePath $profilerPath -WorkingDirectory (Split-Path -Parent $profilerPath) | Out-Null
    }

    catch
    {
        $suggestedActions = @(
            'Check that the managed Tracy tools are usable.',
            "Run '$(Get-GameWipSetupHelperCommand) profiler' to repair them."
        )
        $failure = New-GameWipDiagnosticException `
            -Code 'profiler-start-failed' `
            -Summary "Could not start the Tracy profiler: $profilerPath" `
            -Details $_.Exception.Message `
            -SuggestedActions $suggestedActions
        throw $failure
    }
}

function Initialize-GameWipProjectCommandBuild
{
    param([Parameter(Mandatory = $true)]$Command, [switch]$NoBuild = $Script:NoBuild)

    # Reuse an existing executable or build its declared preset.
    $executable = Resolve-GameWipProjectExecutable -Command $Command

    if (Test-Path -LiteralPath $executable)
    {
        return
    }

    if ($NoBuild)
    {
        $suggestedActions = @(
            "Build preset '$($Command.BuildPreset)' first.",
            'Rerun without -SkipBuild to let GameWIP ensure prerequisites.'
        )
        $failure = New-GameWipDiagnosticException `
            -Code 'prerequisite-build-disabled' `
            -Summary "Required executable is missing: $executable" `
            -Details 'Automatic prerequisite builds were disabled with -SkipBuild.' `
            -SuggestedActions $suggestedActions
        throw $failure
    }

    Write-GameWipOperationEvent `
        -Phase plan `
        -Step 'prerequisite-build' `
        -Severity info `
        -Message "Executable is missing; GameWIP will configure/build preset '$($Command.BuildPreset)' first."
    Invoke-GameWipConfigurePreset -Name $Command.BuildPreset
    Invoke-GameWipBuildPreset -Name $Command.BuildPreset
}

function New-GameWipRuntimeArguments
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Command,
        [ValidateSet('default', 'trace', 'debug', 'info', 'warn', 'error', 'fatal')][string]$LogLevel = 'default',
        [switch]$InputDebug,
        [switch]$InputDevices,
        [switch]$WindowEvents,
        [string]$InputBackends = ''
    )

    # Reject runtime options for commands that cannot forward them.
    $hasRuntimeOptions = $LogLevel -ne 'default'

    if (-not $hasRuntimeOptions)
    {
        $hasRuntimeOptions = (
            $InputDebug -or
            $InputDevices -or
            $WindowEvents -or
            -not [string]::IsNullOrWhiteSpace($InputBackends)
        )
    }

    if (
        $hasRuntimeOptions -and
        (-not $Command.ContainsKey('RuntimeOptions') -or -not [bool]$Command.RuntimeOptions)
    )
    {
        throw "Project command '$($Command.Id)' does not accept game runtime options."
    }

    # Add general runtime logging and diagnostic switches.
    $arguments = [System.Collections.Generic.List[string]]::new()

    if ($LogLevel -ne 'default')
    {
        $arguments.Add("--logLevel=$LogLevel") | Out-Null
    }

    if ($InputDebug)
    {
        $arguments.Add('--inputDebug') | Out-Null
    }

    if ($InputDevices)
    {
        $arguments.Add('--inputDevices') | Out-Null
    }

    if ($WindowEvents)
    {
        $arguments.Add('--windowEvents') | Out-Null
    }

    # Validate backend selection before adding the Windows-only option.
    if (-not [string]::IsNullOrWhiteSpace($InputBackends))
    {
        $target = Get-GameWipSelectedBuildTarget

        if ([string]$target.platform -ne 'windows')
        {
            throw 'The -InputBackends option is available only for Windows build targets.'
        }

        $selectedBackends = @($InputBackends.Split(',') | ForEach-Object { $_.Trim() })
        $validBackends = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)

        foreach ($backend in @('rawInput', 'hid', 'xinput', 'gameInput', 'directInput', 'windowMessages'))
        {
            $validBackends.Add($backend) | Out-Null
        }

        $hasUnsupportedBackend = @(
            $selectedBackends |
                Where-Object { -not $validBackends.Contains($_) -and $_ -cne 'none' }
        ).Count -ne 0

        if ($selectedBackends.Count -eq 0 -or $hasUnsupportedBackend)
        {
            throw "Invalid -InputBackends list '$InputBackends'. Use rawInput, hid, xinput, gameInput, directInput, windowMessages, or none."
        }

        if (($selectedBackends -contains 'none' -and $selectedBackends.Count -ne 1) -or
            @($selectedBackends | Select-Object -Unique).Count -ne $selectedBackends.Count)
        {
            throw "Invalid -InputBackends list '$InputBackends': 'none' cannot be combined and backends cannot repeat."
        }

        $arguments.Add("--inputBackends=$($selectedBackends -join ',')") | Out-Null
    }

    return $arguments.ToArray()
}

function Invoke-GameWipProjectCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string[]]$Arguments = @(),
        [ValidateSet('default', 'trace', 'debug', 'info', 'warn', 'error', 'fatal')][string]$LogLevel = 'default',
        [switch]$InputDebug,
        [switch]$InputDevices,
        [switch]$WindowEvents,
        [string]$InputBackends = '',
        [switch]$NoBuild,
        [switch]$ForceBuild
    )

    # Resolve the command and its runtime options.
    $command = Get-GameWipProjectCommand -Id $Id
    $runtimeArguments = @(New-GameWipRuntimeArguments `
            -Command $command `
            -LogLevel $LogLevel `
            -InputDebug:$InputDebug `
            -InputDevices:$InputDevices `
            -WindowEvents:$WindowEvents `
            -InputBackends $InputBackends)

    # Validate forwarded arguments and confirm the target can run here.
    $hasArguments = $null -ne $Arguments -and $Arguments.Length -ne 0

    if ($hasArguments -and -not [bool]$command.AcceptsExtraArgs)
    {
        throw "Project command '$Id' does not accept extra arguments."
    }

    Assert-GameWipSelectedTargetRunnable -Action "Project command '$Id'"

    # Build when requested or ensure the executable is available.
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
    $projectArguments = @($command.Arguments) + $runtimeArguments + @($Arguments)
    $inheritConsole = $projectArguments -contains '--manual-tests'
    $workingDirectory = $RepositoryRoot

    if ($command.ContainsKey('WorkingDirectory') -and $command.WorkingDirectory -eq 'build')
    {
        $workingDirectory = Get-GameWipBuildDirectory -PresetName ([string]$command.BuildPreset)
    }

    if ($command.ContainsKey('StartTracyProfiler') -and [bool]$command.StartTracyProfiler)
    {
        Start-GameWipTracyProfiler
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

    # Start the executable with the selected working directory and runtime path.
    Invoke-GameWipNative `
        -Name "project-$Id" `
        -FilePath $executable `
        -Arguments $projectArguments `
        -WorkingDirectory $workingDirectory `
        -PathPrefix $pathPrefix `
        -UseWorkspaceTemp:([bool]$command.UseWorkspaceTemp) `
        -InheritConsole:$inheritConsole
}
