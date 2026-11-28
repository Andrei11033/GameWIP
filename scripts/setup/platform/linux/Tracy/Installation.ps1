# Linux Tracy tool installation and rollback.

# ------------------------------------------------------------
# Verify and install the staged tool set
# ------------------------------------------------------------

function Install-GameWipLinuxTracyToolSet
{
    param(
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Version
    )

    if (-not (Test-GameWipLinuxTracyToolSet -ToolRoot $StageRoot))
    {
        throw 'The staged Tracy tool set failed verification. Existing tools were not replaced.'
    }

    # Stage beside the destination so the final directory swap stays atomic.
    $parent = Split-Path -Parent $Destination
    $incoming = Join-Path $parent ('.tracy-linux.{0}.incoming' -f [guid]::NewGuid().ToString('N'))
    $backup = $null
    $installed = $false
    Set-GameWipMutationStarted
    New-Item -ItemType Directory -Path $parent -Force | Out-Null

    try
    {
        New-Item -ItemType Directory -Path $incoming -Force | Out-Null

        foreach ($executable in Get-GameWipLinuxTracyExecutableSet)
        {
            $installedExecutable = Join-Path $incoming $executable
            Copy-Item -LiteralPath (Join-Path $StageRoot $executable) -Destination $installedExecutable
            Invoke-GameWipSetupNative -FilePath 'chmod' -ArgumentList @('u+x', $installedExecutable) | Out-Null
        }

        Copy-Item -LiteralPath (Join-Path $StageRoot 'version.txt') -Destination $incoming

        if (-not (Test-GameWipLinuxTracyToolSet -ToolRoot $incoming))
        {
            throw 'The copied Tracy tool set failed verification. Existing tools were not replaced.'
        }

        # Preserve the current installation until the candidate is verified.
        if (Test-Path -LiteralPath $Destination)
        {
            $backup = Join-Path $parent ('.tracy-linux.{0}.backup' -f [guid]::NewGuid().ToString('N'))
            Move-Item -LiteralPath $Destination -Destination $backup
        }

        try
        {
            Move-Item -LiteralPath $incoming -Destination $Destination
            $incoming = $null
            $installed = $true
        }

        catch
        {
            if ($null -ne $backup -and (Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $Destination))
            {
                Move-Item -LiteralPath $backup -Destination $Destination
                $backup = $null
            }

            throw
        }

        if (-not (Test-GameWipLinuxTracyToolSet -ToolRoot $Destination))
        {
            throw 'The installed Tracy tool set failed verification.'
        }

        Add-GameWipOperationChange -Message "Installed Tracy tools for locked client $Version."
    }

    catch
    {
        if ($installed -and (Test-Path -LiteralPath $Destination))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $Destination -OwnedRoot $parent
        }

        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            Move-Item -LiteralPath $backup -Destination $Destination
            $backup = $null
        }

        throw
    }

    finally
    {
        if ($null -ne $incoming -and (Test-Path -LiteralPath $incoming))
        {
            Invoke-GameWipOwnedTreeRemoval `
                -Path $incoming `
                -OwnedRoot $parent `
                -SuppressMutationTracking
        }
    }

    if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
    {
        Invoke-GameWipOwnedTreeRemoval -Path $backup -OwnedRoot $parent
    }

    Write-Host "  Ready: Tracy tools for locked client $Version"
}
