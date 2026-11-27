# Start the shared helpers with the declared PowerShell runtime.

# ------------------------------------------------------------
# Repository and runtime discovery
# ------------------------------------------------------------

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$forwardedArguments = @($args)

if ($forwardedArguments.Count -eq 0)
{
    Write-Error 'A helper entry point is required.'
    exit 2
}
$entryScript = [IO.Path]::GetFullPath([string]$forwardedArguments[0])
$helperArguments = @($forwardedArguments | Select-Object -Skip 1)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$projectConfig = Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts/config/project.json') -Raw | ConvertFrom-Json
$projectTools = Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts/config/project-tools.json') -Raw | ConvertFrom-Json
$powerShellTool = @($projectTools.tools | Where-Object { $_.id -eq 'powershell' })

if ($powerShellTool.Count -ne 1)
{
    Write-Error 'The project tool registry must declare PowerShell exactly once.'
    exit 1
}
$powerShellTool = $powerShellTool[0]
$minimumVersion = [Version]$powerShellTool.requiredVersion
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build')).TrimEnd('\', '/')
$configuredStorageRoot = if ([string]::IsNullOrWhiteSpace($env:GAMEWIP_STORAGE_ROOT))
{
    [string]$projectConfig.storage.root
}
else
{
    [string]$env:GAMEWIP_STORAGE_ROOT
}
$storageRoot = if ([IO.Path]::IsPathRooted($configuredStorageRoot))
{
    [IO.Path]::GetFullPath($configuredStorageRoot)
}
else
{
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $configuredStorageRoot))
}

if ($storageRoot.Equals($buildRoot, [StringComparison]::OrdinalIgnoreCase) -or
    -not $storageRoot.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))
{
    Write-Error "GAMEWIP_STORAGE_ROOT must be a child of '$buildRoot'."
    exit 1
}
$configuredToolsRoot = if ([string]::IsNullOrWhiteSpace($env:GAMEWIP_MANAGED_TOOLS_ROOT))
{
    [string]$projectConfig.managedEnvironment.gameWipToolsRoot
}
else
{
    [string]$env:GAMEWIP_MANAGED_TOOLS_ROOT
}
$managedToolsRoot = if ([IO.Path]::IsPathRooted($configuredToolsRoot))
{
    [IO.Path]::GetFullPath($configuredToolsRoot)
}
else
{
    [IO.Path]::GetFullPath((Join-Path $storageRoot $configuredToolsRoot))
}
$toolsRootIsLocal = $managedToolsRoot.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)

if (-not $toolsRootIsLocal -and -not (Test-Path -LiteralPath $managedToolsRoot -PathType Container))
{
    Write-Error "GAMEWIP_MANAGED_TOOLS_ROOT must select a directory under build/ or an existing tools directory."
    exit 1
}

# Resolve the host archive and managed executable.
$nativeArchitecture = if (-not [string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITEW6432))
{
    [string]$env:PROCESSOR_ARCHITEW6432
}
else
{
    [string]$env:PROCESSOR_ARCHITECTURE
}
$assetKey = switch -Regex ($nativeArchitecture)
{
    '^(ARM64)$'
    {
        'windows-arm64'
        break
    }
    '^(AMD64|x64)$'
    {
        'windows-amd64'
        break
    }
    default
    {
        $null
    }
}

if ([string]::IsNullOrWhiteSpace($assetKey))
{
    Write-Error "PowerShell bootstrap does not support Windows architecture '$nativeArchitecture'."
    exit 1
}
$assetProperty = $powerShellTool.provider.assets.PSObject.Properties[$assetKey]

if ($null -eq $assetProperty)
{
    Write-Error "The project tool registry has no PowerShell asset for '$assetKey'."
    exit 1
}
$asset = $assetProperty.Value
$managedRuntime = Join-Path $managedToolsRoot (Join-Path ([string]$powerShellTool.provider.managedDirectory) `
        (Join-Path ([string]$powerShellTool.requiredVersion) ([string]$powerShellTool.detection.command + '.exe')))
$managedMarker = Join-Path (Split-Path -Parent $managedRuntime) '.gamewip-tool.json'

function Get-GameWipPowerShellVersion
{
    param([Parameter(Mandatory = $true)][string]$Executable)
    $versionArguments = @($powerShellTool.detection.versionArguments)
    $output = & $Executable @versionArguments 2>$null

    if ($LASTEXITCODE -ne 0)
    {
        return $null
    }
    $versionText = ($output | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) } | Select-Object -Last 1)
    $match = [regex]::Match([string]$versionText, [string]$powerShellTool.detection.versionPattern)

    if (-not $match.Success)
    {
        return $null
    }
    $version = $null

    if ([Version]::TryParse($match.Groups[1].Value, [ref]$version))
    {
        return $version
    }

    return $null
}

function Test-GameWipPowerShellMarker
{
    if (-not (Test-Path -LiteralPath $managedMarker -PathType Leaf))
    {
        return $false
    }

    try
    {
        $marker = Get-Content -LiteralPath $managedMarker -Raw | ConvertFrom-Json
        return (
            [int]$marker.schemaVersion -eq 1 -and
            [string]$marker.toolId -eq 'powershell' -and
            [string]$marker.version -eq [string]$powerShellTool.requiredVersion -and
            [string]$marker.archive -eq [string]$asset.archive -and
            [string]$marker.sha256 -eq [string]$asset.sha256
        )
    }

    catch
    {
        return $false
    }
}

# Reuse a compatible system or managed runtime.
$externalCommand = Get-Command -Name ([string]$powerShellTool.detection.command) -ErrorAction SilentlyContinue | Select-Object -First 1
$externalRuntime = if ($null -ne $externalCommand -and -not [string]::IsNullOrWhiteSpace([string]$externalCommand.Source))
{
    [string]$externalCommand.Source
}
else
{
    $null
}
$selectedRuntime = $null
$externalVersion = $null

if ($null -ne $externalRuntime)
{
    $externalVersion = Get-GameWipPowerShellVersion -Executable $externalRuntime

    if ($null -ne $externalVersion -and $externalVersion -ge $minimumVersion)
    {
        $selectedRuntime = $externalRuntime
    }
}

if ($null -eq $selectedRuntime -and (Test-GameWipPowerShellMarker))
{
    $managedVersion = Get-GameWipPowerShellVersion -Executable $managedRuntime

    if ($null -ne $managedVersion -and $managedVersion -ge $minimumVersion)
    {
        $selectedRuntime = $managedRuntime
    }
}

# Ask before installing the pinned runtime.
if ($null -eq $selectedRuntime)
{
    if ($helperArguments -contains '-Preview')
    {
        Write-Error "PowerShell $minimumVersion or newer is required; preview does not install the managed runtime."
        exit 1
    }

    $approved = $helperArguments -contains '-Yes'

    if ($approved)
    {
        Write-Output "Installing PowerShell $($powerShellTool.requiredVersion) because -Yes was supplied."
    }
    elseif ($helperArguments -contains '-NonInteractive' -or [Console]::IsInputRedirected)
    {
        Write-Error "PowerShell $minimumVersion or newer is required. Rerun interactively or use -Yes to install it under build/."
        exit 1
    }
    else
    {
        $declaredVersion = if ($null -ne $externalVersion)
        {
            $externalVersion
        }
        else
        {
            'not found'
        }
        Write-Output "PowerShell $minimumVersion or newer is required; detected $declaredVersion."
        $answer = Read-Host "Install the verified PowerShell $($powerShellTool.requiredVersion) runtime under '$managedToolsRoot'? [y/N]"
        $approved = $answer -match '^(?i:y|yes)$'
    }

    if (-not $approved)
    {
        Write-Output 'PowerShell installation declined.'
        exit 2
    }

    # Use the declared provider and updater rather than a second installer.
    $windowsPowerShell = Join-Path $PSHOME 'powershell.exe'
    $helperEntry = Join-Path $repositoryRoot 'scripts/GameWIP.ps1'
    & $windowsPowerShell -NoLogo -NoProfile -ExecutionPolicy Bypass `
        -File $helperEntry tool ensure powershell -Yes -NoColor -OutputMode Summary
    $installExitCode = $LASTEXITCODE

    if ($installExitCode -ne 0)
    {
        exit $installExitCode
    }

    if (-not (Test-GameWipPowerShellMarker))
    {
        Write-Error "The managed PowerShell installation did not match the declared release."
        exit 1
    }
    $managedVersion = Get-GameWipPowerShellVersion -Executable $managedRuntime

    if ($null -eq $managedVersion -or $managedVersion -lt $minimumVersion)
    {
        Write-Error "The managed PowerShell runtime does not meet the required version $minimumVersion."
        exit 1
    }
    $selectedRuntime = $managedRuntime
}

# Run the selected helper and preserve its exit code.
& $selectedRuntime -NoLogo -NoProfile -ExecutionPolicy Bypass -File $entryScript @helperArguments
exit $LASTEXITCODE
