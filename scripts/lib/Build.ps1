# GameWIP configure/build/project-command behavior.

Set-StrictMode -Version Latest

function Invoke-GameWipConfigurePreset
{
    param([Parameter(Mandatory = $true)][string]$Name)
    Assert-GameWipValidPreset -Kind 'configure' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    Invoke-GameWipNative -Name "configure-$Name" -FilePath 'cmake' -Arguments @('--preset', $Name) -PathPrefix (Get-GameWipToolchainPathPrefix $Name)
}

function Invoke-GameWipBuildPreset
{
    param([Parameter(Mandatory = $true)][string]$Name)
    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    $cache = Join-Path $RepositoryRoot "build\$Name\CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache))
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

function Write-GameWipNextStepHint
{
    param([Parameter(Mandatory = $true)][string]$Message)
    Add-GameWipOperationNextAction -Message $Message
    Write-GameWipHost "Next: $Message" -ForegroundColor Cyan
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
    if ($Arguments.Count -ne 0 -and -not [bool]$command.AcceptsExtraArgs)
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
