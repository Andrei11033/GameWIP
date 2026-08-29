# GameWIP configure/build/project-command behavior.

Set-StrictMode -Version Latest

function Reset-GameWipPresetBuildTree
{
    param([Parameter(Mandatory = $true)][string]$Name)

    Assert-GameWipValidPreset -Kind 'configure' -Name $Name
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))
    $presetRoot = [IO.Path]::GetFullPath((Join-Path $buildRoot $Name))
    $presetParent = [IO.Path]::GetDirectoryName($presetRoot)
    if (-not [string]::Equals($presetParent, $buildRoot, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing to recreate preset '$Name' outside the repository build root."
    }

    if (Test-Path -LiteralPath $buildRoot)
    {
        $buildRootItem = Get-Item -LiteralPath $buildRoot -Force
        if (($buildRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing to recreate preset '$Name' through reparse-point build root '$buildRoot'."
        }
    }
    if (-not (Test-Path -LiteralPath $presetRoot))
    {
        Write-GameWipOperationEvent -Phase plan -Step "fresh-$Name" -Severity info -Message "Preset '$Name' already has no build tree; configuration will create it."
        return
    }

    $presetItem = Get-Item -LiteralPath $presetRoot -Force
    if (($presetItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Refusing to recursively remove reparse-point preset tree '$presetRoot'."
    }
    Write-GameWipOperationEvent -Phase execute -Step "fresh-$Name" -Severity info -Message "Removing the complete preset build tree '$presetRoot'."
    Remove-Item -LiteralPath $presetRoot -Recurse -Force
}

function Invoke-GameWipConfigurePreset
{
    param([Parameter(Mandatory = $true)][string]$Name, [switch]$Fresh)
    Assert-GameWipValidPreset -Kind 'configure' -Name $Name
    if ($Fresh)
    {
        Reset-GameWipPresetBuildTree -Name $Name
    }
    Confirm-GameWipToolchain -PresetName $Name
    $pathPrefix = Get-GameWipToolchainPathPrefix $Name
    $arguments = @('--preset', $Name)
    $cache = Join-Path $RepositoryRoot "build\$Name\CMakeCache.txt"
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
            $compilerState = Get-ChildItem -LiteralPath (Join-Path $RepositoryRoot "build\$Name\CMakeFiles") `
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

        if (-not [string]::IsNullOrWhiteSpace($configuredCompiler) -and [IO.Path]::IsPathRooted($configuredCompiler))
        {
            $configuredCompiler = $configuredCompiler.Replace('/', '\')
            $expectedRoot = ([IO.Path]::GetFullPath($pathPrefix)).TrimEnd('\') + '\'
            if (-not $configuredCompiler.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase))
            {
                Write-GameWipOperationEvent -Phase plan -Step "configure-$Name" -Severity info -Message "The cached compiler belongs to a different toolchain; CMake will recreate this preset cache."
                $arguments += '--fresh'
            }
        }
    }
    Invoke-GameWipNative -Name "configure-$Name" -FilePath 'cmake' -Arguments $arguments -PathPrefix $pathPrefix
}

function Invoke-GameWipBuildPreset
{
    param([Parameter(Mandatory = $true)][string]$Name, [switch]$Fresh)
    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    $cache = Join-Path $RepositoryRoot "build\$Name\CMakeCache.txt"
    if ($Fresh)
    {
        Reset-GameWipPresetBuildTree -Name $Name
        Write-GameWipOperationEvent -Phase plan -Step "build-$Name" -Severity info -Message "Fresh build requested; configuration is a prerequisite."
        Invoke-GameWipConfigurePreset -Name $Name
    }
    elseif (-not (Test-Path -LiteralPath $cache))
    {
        Write-GameWipOperationEvent -Phase plan -Step "build-$Name" -Severity info -Message "Build preset '$Name' is not configured; configuration is a prerequisite."
        Invoke-GameWipConfigurePreset -Name $Name
    }
    Invoke-GameWipNative -Name "build-$Name" -FilePath 'cmake' -Arguments @('--build', '--preset', $Name, '--parallel') -PathPrefix (Get-GameWipToolchainPathPrefix $Name)
}

function Invoke-GameWipBuildTarget
{
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][string]$Target)
    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    Invoke-GameWipNative -Name "build-$Name-$Target" -FilePath 'cmake' -Arguments @('--build', '--preset', $Name, '--target', $Target) -PathPrefix (Get-GameWipToolchainPathPrefix $Name)
}

function Resolve-GameWipProjectExecutable
{
    param([Parameter(Mandatory = $true)]$Command)
    $primary = Join-Path $RepositoryRoot $Command.Executable
    if (Test-Path -LiteralPath $primary)
    {
        return $primary
    }
    if ($Command.ContainsKey('AlternateExecutable'))
    {
        $alternate = Join-Path $RepositoryRoot $Command.AlternateExecutable
        if (Test-Path -LiteralPath $alternate)
        {
            return $alternate
        }
    }
    return $primary
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
                -Details "Automatic prerequisite builds were disabled with -NoBuild." `
                -SuggestedActions @("Build preset '$($Command.BuildPreset)' first.", 'Rerun without -NoBuild to let GameWIP ensure prerequisites.'))
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
    Invoke-GameWipNative -Name "project-$Id" -FilePath $executable -Arguments (@($command.Arguments) + @($Arguments)) -UseWorkspaceTemp:([bool]$command.UseWorkspaceTemp)
}
