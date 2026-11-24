# ------------------------------------------------------------
# Host and tool discovery
# ------------------------------------------------------------

# Host identification

function Test-GameWipWindowsHost
{
    return [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)
}

function Get-GameWipHostAssetKey
{
    $platform = if (Test-GameWipWindowsHost)
    {
        'windows'
    }
    elseif ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
    {
        'linux'
    }
    else
    {
        return $null
    }

    $architecture = switch ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant())
    {
        'x64'
        {
            'amd64'
        }
        'arm64'
        {
            'arm64'
        }
        default
        {
            return $null
        }
    }

    return "$platform-$architecture"
}

# Project tool registry

function Test-GameWipToolHostSupport
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $assetKey = Get-GameWipHostAssetKey

    if ($null -eq $assetKey)
    {
        return $false
    }

    if ($Tool.provider.Contains('assets') -and -not $Tool.provider.assets.ContainsKey($assetKey))
    {
        return $false
    }

    if (-not $Tool.provider.Contains('hostPlatform'))
    {
        return $true
    }

    return [string]$Tool.provider.hostPlatform -eq ($assetKey -split '-', 2)[0]
}

function Get-GameWipToolValidationFiles
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Tool,
        [string]$AssetKey = (Get-GameWipHostAssetKey)
    )

    if ($null -ne $AssetKey -and
        $Tool.provider.Contains('assets') -and
        $Tool.provider.assets.ContainsKey($AssetKey))
    {
        $asset = $Tool.provider.assets[$AssetKey]

        if ($asset.Contains('validationFiles'))
        {
            return @($asset.validationFiles)
        }
    }

    if ($Tool.provider.Contains('validationFiles'))
    {
        return @($Tool.provider.validationFiles)
    }

    return @()
}

function Get-GameWipProjectTool
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $toolMatches = @($ProjectTools.tools | Where-Object { $_.id -eq $Id })

    if ($toolMatches.Count -ne 1)
    {
        throw "Project tool '$Id' is not registered exactly once."
    }

    return $toolMatches[0]
}

function Get-GameWipProjectToolSelection
{
    param([Parameter(Mandatory = $true)][string]$Selector)

    if ([string]::IsNullOrWhiteSpace($Selector) -or $Selector -eq 'all')
    {
        return @($ProjectTools.tools)
    }

    $byId = @($ProjectTools.tools | Where-Object { $_.id -eq $Selector })

    if ($byId.Count -eq 1)
    {
        return $byId
    }

    $byCategory = @($ProjectTools.tools | Where-Object { $_.category -eq $Selector })

    if ($byCategory.Count -ne 0)
    {
        return $byCategory
    }

    throw "Unknown project tool or category '$Selector'."
}
