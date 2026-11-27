# ------------------------------------------------------------
# Managed tool paths and versions
# ------------------------------------------------------------

# Expose a managed path only when Windows ownership is verified.
function Test-GameWipManagedToolRootTrusted
{
    param([Parameter(Mandatory = $true)][string]$Root)

    return -not (
        (Test-Path -LiteralPath $Root) -and
        (Test-GameWipWindowsHost) -and
        -not (Test-GameWipManagedToolRootOwnership -Root $Root)
    )
}

# Search paths exposed to child processes

function Get-GameWipManagedToolPath
{
    $root = Get-GameWipManagedToolRoot

    if (-not (Test-GameWipManagedToolRootTrusted -Root $root))
    {
        return @()
    }

    $paths = @(
        (Join-Path $root 'bin'),
        (Join-Path $root 'cmake/bin'),
        (Join-Path $root 'npm'),
        (Join-Path $root 'npm/bin'),
        (Get-GameWipPythonEnvironmentExecutableDirectory -Root (Join-Path $root 'python')),
        (Join-Path $root 'powershell')
    )

    if (Test-GameWipWindowsHost)
    {
        $paths += @(Get-GameWipWindowsManagedToolPath)
    }

    return $paths
}

function Get-GameWipProviderManagedToolPath
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $root = Get-GameWipManagedToolRoot

    if (-not (Test-GameWipManagedToolRootTrusted -Root $root))
    {
        return @()
    }

    $providerKind = [string]$Tool.provider.kind

    if ($providerKind -in @('githubRelease', 'archive'))
    {
        if ($Tool.provider.Contains('installMode') -and $Tool.provider.installMode -eq 'directoryArchive')
        {
            $toolRoot = Join-Path $root (Join-Path ([string]$Tool.provider.managedDirectory) ([string]$Tool.requiredVersion))
            return @((Join-Path $toolRoot ([string]$Tool.provider.binaryDirectory)))
        }

        return @((Join-Path $root 'bin'))
    }

    switch ($providerKind)
    {
        'python'
        {
            return @((Get-GameWipPythonEnvironmentExecutableDirectory -Root (Join-Path $root 'python')))
        }

        'npm'
        {
            return @((Join-Path $root 'npm'), (Join-Path $root 'npm/bin'))
        }

        'powershellGallery'
        {
            return @((Join-Path $root 'powershell'))
        }
        default
        {
            return @()
        }
    }
}

# Verify installed tool versions

function Get-GameWipToolCandidateVersion
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool, [Parameter(Mandatory = $true)][string]$Path)

    # Validate the marker for tools installed from managed archives.
    $isManagedArchive = $Tool.provider.kind -in @('githubRelease', 'archive') -and
    $Tool.provider.Contains('installMode') -and
    $Tool.provider.installMode -eq 'directoryArchive'

    if ($isManagedArchive)
    {
        # Trust a managed directory tool only when its install marker matches the pin.
        $toolRoot = Join-Path (Get-GameWipManagedToolRoot) (Join-Path ([string]$Tool.provider.managedDirectory) ([string]$Tool.requiredVersion))
        $binaryDirectory = Join-Path $toolRoot ([string]$Tool.provider.binaryDirectory)
        $binaryRoot = [IO.Path]::GetFullPath($binaryDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar) +
        [IO.Path]::DirectorySeparatorChar
        $candidatePath = [IO.Path]::GetFullPath($Path)
        $pathComparison = Get-GameWipPathComparison

        if (-not $candidatePath.StartsWith($binaryRoot, $pathComparison))
        {
            if ($Tool.provider.kind -eq 'githubRelease')
            {
                return $null
            }
        }
        else
        {
            $manifestPath = Join-Path $toolRoot '.gamewip-tool.json'

            if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf))
            {
                return $null
            }

            try
            {
                $manifest = Read-GameWipUtf8Text -Path $manifestPath | ConvertFrom-Json
            }

            catch
            {
                return $null
            }

            $assetKey = Get-GameWipHostAssetKey
            $asset = if ($null -ne $assetKey -and $Tool.provider.assets.ContainsKey($assetKey))
            {
                $Tool.provider.assets[$assetKey]
            }
            else
            {
                return $null
            }

            if ($manifest.schemaVersion -eq 1 -and
                [string]$manifest.toolId -eq [string]$Tool.id -and
                [string]$manifest.version -eq [string]$Tool.requiredVersion -and
                [string]$manifest.archive -eq [string]$asset.archive -and
                [string]$manifest.sha256 -eq [string]$asset.sha256)
            {
                foreach ($relativePath in @(Get-GameWipToolValidationFiles -Tool $Tool -AssetKey $assetKey))
                {
                    $relativePath = [string]$relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)

                    if (-not (Test-Path -LiteralPath (Join-Path $toolRoot $relativePath) -PathType Leaf))
                    {
                        return $null
                    }
                }

                return [string]$manifest.version
            }

            return $null
        }
    }

    if ($Tool.detection.Contains('commandConfigVersionPath'))
    {
        return [string](Resolve-GameWipCommandConfigValue -Path ([string]$Tool.detection.commandConfigVersionPath))
    }

    $environment = @{}
    $managed = @(Get-GameWipManagedToolPath)

    if ($managed.Count -ne 0)
    {
        $environment.PATH = (@($managed) + @($env:PATH)) -join [IO.Path]::PathSeparator
    }

    # Read the version from the detected executable.
    $result = Invoke-GameWipProcess `
        -FilePath $Path `
        -Arguments @($Tool.detection.versionArguments) `
        -Environment $environment `
        -OutputMode LogOnly `
        -TimeoutSeconds 20

    if ($result.ExitCode -ne 0)
    {
        return $null
    }

    $output = (@($result.Stdout) + @($result.Stderr)) -join "`n"
    $match = [regex]::Match($output, [string]$Tool.detection.versionPattern)

    if ($match.Success)
    {
        return $match.Groups[1].Value
    }

    return $null
}
