# Tracy tool-set installation and rollback

# ------------------------------------------------------------
# Verify and atomically install the staged tool set
# ------------------------------------------------------------

function Install-GameWipTracyToolSet
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Version
    )

    foreach ($executable in Get-GameWipTracyExecutableSet)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $executable)))
        {
            throw "The staged Tracy rebuild is incomplete; missing $executable. Existing managed tools were not replaced."
        }
    }

    $destinationParent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    $incoming = Join-Path $destinationParent ('.tracy.{0}.incoming' -f [guid]::NewGuid().ToString('N'))
    $backup = $null

    try
    {
        New-Item -ItemType Directory -Path $incoming | Out-Null

        foreach ($file in Get-ChildItem -LiteralPath $stageRoot -File)
        {
            Copy-Item -LiteralPath $file.FullName -Destination $incoming -Force
        }
        Write-GameWipTextAtomic -Path (Join-Path $incoming 'version.txt') -Content "source-$version`n"

        if (-not (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot -ToolRoot $incoming))
        {
            throw 'The staged Tracy tool set failed final verification. Existing managed tools were not replaced.'
        }

        if (Test-Path -LiteralPath $destination)
        {
            $backup = Join-Path $destinationParent ('.tracy.{0}.backup' -f [guid]::NewGuid().ToString('N'))
            Set-GameWipMutationStarted
            Move-Item -LiteralPath $destination -Destination $backup
        }
        else
        {
            Set-GameWipMutationStarted
        }

        try
        {
            Move-Item -LiteralPath $incoming -Destination $destination
            $incoming = $null
        }

        catch
        {
            if ($null -ne $backup -and (Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $destination))
            {
                Move-Item -LiteralPath $backup -Destination $destination
                $backup = $null
            }
            throw
        }

        Add-GameWipOperationChange -Message "Installed verified Tracy tools for locked client $version."
    }

    finally
    {
        if ($null -ne $incoming -and (Test-Path -LiteralPath $incoming))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $incoming -OwnedRoot $destinationParent -SuppressMutationTracking
        }
    }

    if (-not (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot))
    {
        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            if (Test-Path -LiteralPath $destination)
            {
                Invoke-GameWipOwnedTreeRemoval `
                    -Path $destination `
                    -OwnedRoot $destinationParent
            }

            Move-Item -LiteralPath $backup -Destination $destination
            $backup = $null

            throw 'The installed Tracy tool set failed post-swap verification; the previous verified tool set was restored.'
        }

        throw 'The installed Tracy tool set failed post-swap verification.'
    }

    if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
    {
        Invoke-GameWipOwnedTreeRemoval -Path $backup -OwnedRoot $destinationParent
        $backup = $null
    }

    foreach ($file in Get-ChildItem -LiteralPath $destination -File)
    {
        Write-Host "  Installed: $($file.FullName)"
    }
    Write-Host "  Ready: rebuilt Tracy Windows tools with UCRT64 from locked client $version"
}