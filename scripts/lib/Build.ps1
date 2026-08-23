# GameWIP Build helper behavior. Dot-sourced by scripts/GameWIP.ps1.

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
        Write-Host "Build preset '$Name' has not been configured; configuring it now." -ForegroundColor Cyan
        Invoke-GameWipConfigurePreset -Name $Name
    }
    Invoke-GameWipNative -Name "build-$Name" -FilePath 'cmake' -Arguments @('--build', '--preset', $Name, '--parallel') -PathPrefix (Get-GameWipToolchainPathPrefix $Name)
}

function Invoke-GameWipBuildTarget
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Target
    )

    Assert-GameWipValidPreset -Kind 'build' -Name $Name
    Invoke-GameWipNative -Name "build-$Name-$Target" -FilePath 'cmake' -Arguments @('--build', '--preset', $Name, '--target', $Target) -PathPrefix (Get-GameWipToolchainPathPrefix $Name)
}

function Write-GameWipNextStepHint
{
    param([Parameter(Mandatory = $true)][string]$Message)

    Write-Host "Next: $Message" -ForegroundColor Cyan
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
    param(
        [Parameter(Mandatory = $true)]$Command,
        [switch]$ForceBuild
    )

    $executable = Resolve-GameWipProjectExecutable -Command $Command
    if (Test-Path -LiteralPath $executable)
    {
        return
    }

    if (-not $ForceBuild)
    {
        throw "Missing executable '$executable'. Rerun with -BuildIfMissing or build preset '$($Command.BuildPreset)'."
    }

    Invoke-GameWipConfigurePreset -Name $Command.BuildPreset
    Invoke-GameWipBuildPreset -Name $Command.BuildPreset
}

function Invoke-GameWipProjectCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string[]]$Arguments = @(),
        [switch]$ForceBuild
    )

    $command = Get-GameWipProjectCommand -Id $Id
    if ($Arguments.Count -ne 0 -and -not [bool]$command.AcceptsExtraArgs)
    {
        throw "Project command '$Id' does not accept extra arguments."
    }
    Initialize-GameWipProjectCommandBuild -Command $command -ForceBuild:$ForceBuild
    $executable = Resolve-GameWipProjectExecutable -Command $command
    $commandArguments = @($command.Arguments) + @($Arguments)
    Invoke-GameWipNative -Name "project-$Id" -FilePath $executable -Arguments $commandArguments -UseWorkspaceTemp:([bool]$command.UseWorkspaceTemp)
}
