# Linux setup support for Ubuntu, Debian, and CachyOS.

Set-StrictMode -Version Latest

$LinuxSetupConfig = Read-GameWipJsonConfig `
    -Path (Join-Path $PSScriptRoot 'host.json') `
    -Name 'Linux setup' `
    -SchemaPath (Join-Path $ScriptsRoot 'schemas/linux-host.schema.json')

# ------------------------------------------------------------
# Distribution detection and setup plan
# ------------------------------------------------------------

function Get-GameWipLinuxDistribution
{
    $releasePath = '/etc/os-release'

    if (-not (Test-Path -LiteralPath $releasePath -PathType Leaf))
    {
        throw 'Linux setup requires /etc/os-release to identify the distribution.'
    }

    $release = @{}

    foreach ($line in Get-Content -LiteralPath $releasePath)
    {
        if ($line -match '^([A-Z0-9_]+)=(.*)$')
        {
            $release[$Matches[1]] = $Matches[2].Trim().Trim('"').Trim("'")
        }
    }

    $id = [string]$release['ID']

    if (-not $LinuxSetupConfig.distributions.ContainsKey($id))
    {
        throw "Linux setup does not support distribution '$id'. Supported distributions: Ubuntu, Debian, and CachyOS."
    }

    return [pscustomobject]@{
        Id = $id
        Name = [string]$release['NAME']
        PackageManager = [string]$LinuxSetupConfig.distributions[$id]
    }
}

function Get-GameWipSetupLinuxPlan
{
    $distribution = Get-GameWipLinuxDistribution
    $manager = $LinuxSetupConfig.packageManagers[$distribution.PackageManager]
    $packages = @($manager.packages) -join ', '
    $cmakeVersion = (Get-GameWipProjectTool -Id cmake).requiredVersion

    $packageStep = if ($distribution.PackageManager -eq 'pacman')
    {
        "Run pacman -Syu to install host packages for $($distribution.Name). This includes a full system upgrade."
    }
    else
    {
        "Refresh apt indexes and install host packages for $($distribution.Name): $packages."
    }

    return @(
        $packageStep,
        "Install verified CMake $cmakeVersion if the system version is too old.",
        'Install the selected LLVM-MinGW host archive when project tools are ensured.'
    )
}

# ------------------------------------------------------------
# Privileged package commands
# ------------------------------------------------------------

function Invoke-GameWipLinuxPrivilegedCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $isRoot = [Environment]::UserName -eq 'root'

    if ($isRoot)
    {
        Invoke-GameWipSetupNative -FilePath $FilePath -ArgumentList $Arguments | Out-Null
        return
    }

    $sudo = Resolve-GameWipSetupCommand -Name 'sudo'

    if ($null -eq $sudo)
    {
        throw "'$FilePath' requires administrator privileges. Run setup with sudo or install sudo first."
    }

    if ($NonInteractive)
    {
        $check = Invoke-GameWipProcess -FilePath $sudo -Arguments @('-n', 'true') -OutputMode LogOnly -TimeoutSeconds 10

        if ($check.ExitCode -ne 0)
        {
            throw 'Noninteractive setup needs root or cached sudo credentials. Run sudo -v first, then retry.'
        }
        $arguments = @('-n', $FilePath) + $Arguments
    }
    else
    {
        Invoke-GameWipSetupNative -FilePath $sudo -ArgumentList @('-v') -InheritConsole | Out-Null
        $arguments = @('-n', $FilePath) + $Arguments
    }

    Invoke-GameWipSetupNative -FilePath $sudo -ArgumentList $arguments | Out-Null
}

# ------------------------------------------------------------
# Managed CMake installation
# ------------------------------------------------------------

function Install-GameWipLinuxCMake
{
    $tool = Get-GameWipProjectTool -Id cmake
    $status = Get-GameWipToolStatus -Tool $tool

    if ($status.Ready)
    {
        Write-GameWipStatusLine -Status ready -Text "CMake $($status.Detected.Version)" -Semantic Success -Indent 2
        return
    }

    $assetKey = Get-GameWipHostAssetKey
    $release = $LinuxSetupConfig.cmake.releases[[string]$tool.requiredVersion]

    if ([string]::IsNullOrWhiteSpace($assetKey) -or $null -eq $release -or -not $release.ContainsKey($assetKey))
    {
        throw (
            "No verified Linux CMake archive is configured for version '{0}' and host '{1}'." -f
            $tool.requiredVersion,
            $assetKey
        )
    }

    $asset = $release[$assetKey]
    $download = Join-Path $Script:OperationContext.Temp $asset.archive
    $uri = 'https://github.com/{0}/releases/download/v{1}/{2}' -f `
        $LinuxSetupConfig.cmake.repository, $tool.requiredVersion, $asset.archive
    $downloadResult = Invoke-GameWipDownload -Uri $uri -OutFile $download -Label "CMake $($tool.requiredVersion)"

    if ($downloadResult.State -ne 'resolved')
    {
        throw "CMake download failed: $($downloadResult.Reason)"
    }

    if ((Get-GameWipFileSha256 -Path $download) -ne [string]$asset.sha256)
    {
        throw "SHA256 mismatch for '$($asset.archive)'."
    }

    # Extract and validate the archive before replacing the managed version.
    $extractRoot = Join-Path $Script:OperationContext.Temp 'extract-cmake'
    New-Item -ItemType Directory -Path $extractRoot | Out-Null
    Invoke-GameWipProviderNative `
        -Name 'extract-cmake' `
        -FilePath tar `
        -Arguments @('-xzf', $download, '-C', $extractRoot) | Out-Null
    $payloadDirectories = @(Get-ChildItem -LiteralPath $extractRoot -Directory -Force)

    if ($payloadDirectories.Count -ne 1 -or @(Get-ChildItem -LiteralPath $extractRoot -File -Force).Count -ne 0)
    {
        throw "CMake archive '$($asset.archive)' must contain one top-level directory."
    }

    $payload = $payloadDirectories[0].FullName

    foreach ($relativePath in @($asset.validationFiles))
    {
        $candidate = Join-Path $payload ([string]$relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))

        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf))
        {
            throw "CMake archive is missing required file '$relativePath'."
        }
    }

    $managedRoot = Get-GameWipManagedToolRoot
    $toolRoot = Join-Path $managedRoot 'cmake'
    $incomingName = '.cmake.{0}.{1}.incoming' -f $tool.requiredVersion, [guid]::NewGuid().ToString('N')
    $incoming = Join-Path $managedRoot $incomingName
    $backup = $null
    New-Item -ItemType Directory -Force -Path $managedRoot | Out-Null

    try
    {
        Set-GameWipMutationStarted
        Move-Item -LiteralPath $payload -Destination $incoming
        $marker = [ordered]@{
            schemaVersion = 1
            toolId = 'cmake'
            version = [string]$tool.requiredVersion
            archive = [string]$asset.archive
            sha256 = [string]$asset.sha256
        }
        Write-GameWipJsonAtomic -Path (Join-Path $incoming '.gamewip-tool.json') -Value $marker

        if (Test-Path -LiteralPath $toolRoot)
        {
            $markerPath = Join-Path $toolRoot '.gamewip-tool.json'

            if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf))
            {
                throw "Refusing to replace an unowned CMake directory: '$toolRoot'."
            }

            try
            {
                $existingMarker = Read-GameWipUtf8Text -Path $markerPath | ConvertFrom-Json

                if ($existingMarker.schemaVersion -ne 1 -or $existingMarker.toolId -ne 'cmake')
                {
                    throw 'Invalid managed CMake marker.'
                }
            }

            catch
            {
                throw "Refusing to replace CMake directory with invalid ownership metadata: '$toolRoot'."
            }
            $backupName = '.{0}.{1}.backup' -f $tool.requiredVersion, [guid]::NewGuid().ToString('N')
            $backup = Join-Path $managedRoot $backupName
            Move-Item -LiteralPath $toolRoot -Destination $backup
        }

        try
        {
            Move-Item -LiteralPath $incoming -Destination $toolRoot
            $installedFiles = @($asset.validationFiles | Where-Object {
                    $relativePath = [string]$_.Replace('/', [IO.Path]::DirectorySeparatorChar)
                    -not (Test-Path -LiteralPath (Join-Path $toolRoot $relativePath) -PathType Leaf)
                })

            if ($installedFiles.Count -ne 0)
            {
                throw "Installed CMake is incomplete: $($installedFiles -join ', ')."
            }

            $cmakeExecutable = Join-Path $toolRoot 'bin/cmake'
            $versionOutput = Invoke-GameWipProcess `
                -FilePath $cmakeExecutable `
                -Arguments @('--version') `
                -OutputMode LogOnly `
                -TimeoutSeconds 20
            $installedVersion = Get-GameWipToolCandidateVersion -Tool $tool -Path $cmakeExecutable

            if ($versionOutput.ExitCode -ne 0 -or
                $null -eq $installedVersion -or
                [version]$installedVersion -lt [version]$tool.requiredVersion)
            {
                throw (
                    "Installed CMake '{0}' does not satisfy the minimum version '{1}'." -f
                    $installedVersion,
                    $tool.requiredVersion
                )
            }
        }

        catch
        {
            if (Test-Path -LiteralPath $toolRoot)
            {
                Remove-Item -LiteralPath $toolRoot -Recurse -Force
            }

            if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
            {
                Move-Item -LiteralPath $backup -Destination $toolRoot
                $backup = $null
            }
            throw
        }

        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            Remove-Item -LiteralPath $backup -Recurse -Force
        }
        Add-GameWipOperationChange -Message "Installed CMake $($tool.requiredVersion)"
    }

    finally
    {
        if (Test-Path -LiteralPath $incoming)
        {
            Remove-Item -LiteralPath $incoming -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# ------------------------------------------------------------
# Linux prerequisite installation
# ------------------------------------------------------------

function Invoke-GameWipSetupLinuxPrerequisites
{
    $distribution = Get-GameWipLinuxDistribution
    $manager = $LinuxSetupConfig.packageManagers[$distribution.PackageManager]
    $command = if ($distribution.PackageManager -eq 'apt')
    {
        'apt-get'
    }
    else
    {
        'pacman'
    }

    Write-GameWipSection "Linux prerequisites: $($distribution.Name)"
    Write-Host "  Package manager: $command"
    Write-Host "  Packages: $(@($manager.packages) -join ', ')"

    if ($manager.updateArguments.Count -ne 0)
    {
        Invoke-GameWipLinuxPrivilegedCommand -FilePath $command -Arguments @($manager.updateArguments)
    }

    Invoke-GameWipLinuxPrivilegedCommand `
        -FilePath $command `
        -Arguments (@($manager.installArguments) + @($manager.packages))
    Install-GameWipLinuxCMake
}
