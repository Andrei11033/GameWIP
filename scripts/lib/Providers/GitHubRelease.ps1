# GameWIP verified GitHub-release tool provider.

function ConvertFrom-GameWipGitHubReleaseTag
{
    param([Parameter(Mandatory = $true)][string]$Tag)
    return $Tag -replace '^v(?=[0-9])', ''
}

function Get-GameWipGitHubReleaseToolLatestVersion
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }
    return $null
}

function Get-GameWipGitHubReleaseMetadata
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -ne 'resolved')
    {
        throw "GitHub release metadata for '$($Tool.id)' is $($query.State): $($query.Reason)"
    }
    return $query.Metadata
}

function Install-GameWipGitHubReleaseTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    if (-not $Version)
    {
        throw "GitHub release tool '$($Tool.id)' requires a version."
    }
    if (-not $Tool.provider.Contains('releaseTag'))
    {
        throw "GitHub release tool '$($Tool.id)' lacks an upstream release tag."
    }
    $platformKey = if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
    {
        'linux-amd64'
    }
    else
    {
        'windows-amd64'
    }
    $asset = $Tool.provider.assets[$platformKey]
    if ($null -eq $asset)
    {
        throw "No verified '$platformKey' asset is declared for '$($Tool.id)'."
    }
    $download = Join-Path $Script:OperationContext.Temp $asset.archive
    $uri = "https://github.com/$($Tool.provider.repository)/releases/download/$($Tool.provider.releaseTag)/$($asset.archive)"
    $downloadResult = Invoke-GameWipDownload -Uri $uri -OutFile $download -Label "$($Tool.id) $Version"
    if ($downloadResult.State -ne 'resolved')
    {
        throw "Download failed for '$($Tool.id)': $($downloadResult.Reason)"
    }
    $actualHash = Get-GameWipFileSha256 -Path $download
    if ($actualHash -ne $asset.sha256)
    {
        throw "SHA256 mismatch for '$($asset.archive)'."
    }

    $candidateRoot = Join-Path $Script:OperationContext.Temp "candidate-$($Tool.id)"
    New-Item -ItemType Directory -Path $candidateRoot | Out-Null
    $executableName = $null
    if ($asset.Contains('format') -and $asset.format -eq 'executable')
    {
        $executableName = if ($platformKey -eq 'windows-amd64')
        {
            "$($Tool.id).exe"
        }
        else
        {
            [string]$Tool.id
        }
        Copy-Item -LiteralPath $download -Destination (Join-Path $candidateRoot $executableName)
    }
    else
    {
        $extractRoot = Join-Path $Script:OperationContext.Temp "extract-$($Tool.id)"
        New-Item -ItemType Directory -Path $extractRoot | Out-Null
        if ($asset.archive.EndsWith('.zip'))
        {
            Expand-Archive -LiteralPath $download -DestinationPath $extractRoot
        }
        else
        {
            Invoke-GameWipProviderNative -Name "extract-$($Tool.id)" -FilePath tar -Arguments @('-xzf', $download, '-C', $extractRoot) | Out-Null
        }
        $binary = Get-ChildItem -LiteralPath $extractRoot -Recurse -File | Where-Object { $_.Name -in @($Tool.id, "$($Tool.id).exe") } | Select-Object -First 1
        if ($null -eq $binary)
        {
            throw "Release archive did not contain '$($Tool.id)'."
        }
        $executableName = $binary.Name
        Copy-Item -LiteralPath $binary.FullName -Destination (Join-Path $candidateRoot $executableName)
    }

    $candidate = Join-Path $candidateRoot $executableName
    if (-not (Test-GameWipWindowsHost))
    {
        Invoke-GameWipProcess -FilePath chmod -Arguments @('+x', $candidate) -OutputMode LogOnly -TimeoutSeconds 10 | Out-Null
    }
    $candidateVersion = Get-GameWipToolCandidateVersion -Tool $Tool -Path $candidate
    if ([string]$candidateVersion -ne $Version)
    {
        throw "Staged '$($Tool.id)' reported version '$candidateVersion'; expected '$Version'."
    }

    Initialize-GameWipManagedToolRoot
    $managedRoot = Get-GameWipManagedToolRoot
    $toolsRoot = Join-Path $managedRoot 'tools'
    $familyRoot = Join-Path $toolsRoot $Tool.id
    $toolRoot = Join-Path $familyRoot $Version
    $incoming = Join-Path $toolsRoot ('.{0}.{1}.{2}.incoming' -f $Tool.id, $Version, [guid]::NewGuid().ToString('N'))
    $shimPath = Join-Path (Join-Path $managedRoot 'bin') $(if (Test-GameWipWindowsHost)
        {
            "$($Tool.id).cmd"
        }
        else
        {
            [string]$Tool.id
        })
    $shimBackup = $null
    $backup = $null
    $familyCreated = $false
    $shimReplacementAttempted = $false
    try
    {
        New-Item -ItemType Directory -Path $incoming | Out-Null
        Copy-Item -LiteralPath $candidate -Destination (Join-Path $incoming $executableName)
        if (-not (Test-Path -LiteralPath $familyRoot -PathType Container))
        {
            Set-GameWipMutationStarted
            New-Item -ItemType Directory -Path $familyRoot | Out-Null
            $familyCreated = $true
        }
        if (Test-Path -LiteralPath $toolRoot)
        {
            $backup = Join-Path $familyRoot ('.{0}.{1}.backup' -f $Version, [guid]::NewGuid().ToString('N'))
            Set-GameWipMutationStarted
            Move-Item -LiteralPath $toolRoot -Destination $backup
        }
        else
        {
            Set-GameWipMutationStarted
        }
        try
        {
            Move-Item -LiteralPath $incoming -Destination $toolRoot
            $incoming = $null
            if (Test-Path -LiteralPath $shimPath -PathType Leaf)
            {
                $shimBackup = Join-Path (Split-Path -Parent $shimPath) ('.{0}.{1}.backup' -f $Tool.id, [guid]::NewGuid().ToString('N'))
                Copy-Item -LiteralPath $shimPath -Destination $shimBackup
            }
            $shimReplacementAttempted = $true
            Write-GameWipManagedToolShim -ToolId $Tool.id -Version $Version -ExecutableName $executableName
            $installedVersion = Get-GameWipToolCandidateVersion -Tool $Tool -Path $shimPath
            if ([string]$installedVersion -ne $Version)
            {
                throw "Installed '$($Tool.id)' reported version '$installedVersion'; expected '$Version'."
            }
        }
        catch
        {
            if ($shimReplacementAttempted -and $null -ne $shimBackup -and (Test-Path -LiteralPath $shimBackup))
            {
                [IO.File]::Move($shimBackup, $shimPath, $true)
                $shimBackup = $null
            }
            elseif ($shimReplacementAttempted -and (Test-Path -LiteralPath $shimPath))
            {
                Remove-Item -LiteralPath $shimPath -Force
            }
            if (Test-Path -LiteralPath $toolRoot)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $toolRoot -OwnedRoot $familyRoot -SuppressMutationTracking
            }
            if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
            {
                Move-Item -LiteralPath $backup -Destination $toolRoot
                $backup = $null
            }
            if ($familyCreated -and (Test-Path -LiteralPath $familyRoot) -and @(Get-ChildItem -LiteralPath $familyRoot -Force).Count -eq 0)
            {
                Remove-Item -LiteralPath $familyRoot -Force
            }
            throw
        }
        if ($null -ne $shimBackup -and (Test-Path -LiteralPath $shimBackup))
        {
            Remove-Item -LiteralPath $shimBackup -Force
            $shimBackup = $null
        }
        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $backup -OwnedRoot $familyRoot -SuppressMutationTracking
            $backup = $null
        }
    }
    finally
    {
        if ($null -ne $incoming -and (Test-Path -LiteralPath $incoming))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $incoming -OwnedRoot $toolsRoot -SuppressMutationTracking
        }
        if ($null -ne $shimBackup -and (Test-Path -LiteralPath $shimBackup))
        {
            Remove-Item -LiteralPath $shimBackup -Force -ErrorAction SilentlyContinue
        }
    }
}
