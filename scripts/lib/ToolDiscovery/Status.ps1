# ------------------------------------------------------------
# Detected tool status and reports
# ------------------------------------------------------------

function New-GameWipDetectedToolResult
{
    param(
        [Parameter(Mandatory = $true)][bool]$Installed,
        [AllowNull()][object]$Version,
        [AllowNull()][object]$Location,
        [AllowNull()][object]$Source,
        [AllowNull()][object]$SelectionReason,
        [AllowEmptyCollection()][object[]]$Candidates = @()
    )

    return [pscustomobject]@{
        Installed = $Installed
        Version = $Version
        Location = $Location
        Source = $Source
        SelectionReason = $SelectionReason
        Candidates = @($Candidates)
    }
}

function Get-GameWipDetectedTool
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    # Gallery modules are located by package directory rather than command.
    if ($Tool.provider.kind -eq 'powershellGallery')
    {
        $candidates = @(Get-GameWipPowerShellGalleryCandidates -Tool $Tool)

        if ($candidates.Count -eq 0)
        {
            return New-GameWipDetectedToolResult -Installed $false
        }

        $selected = $candidates[0]
        return New-GameWipDetectedToolResult `
            -Installed $true `
            -Version $selected.Version `
            -Location $selected.Location `
            -Source $selected.Source `
            -SelectionReason $selected.SelectionReason `
            -Candidates $candidates
    }

    $candidates = @(Get-GameWipToolCandidates -Tool $Tool)

    if ($candidates.Count -eq 0)
    {
        return New-GameWipDetectedToolResult -Installed $false
    }

    $selected = $candidates[0]
    $version = Get-GameWipToolCandidateVersion -Tool $Tool -Path $selected.Path
    $usesPackageQueryHost = $Tool.provider.kind -in @('python', 'npm') -and
        [string]$Tool.detection.command -ne [string]$Tool.provider.package

    if ($usesPackageQueryHost -and $null -eq $version)
    {
        # A package-query host may exist more than once on PATH. Select the
        # first host that contains the package instead of treating an earlier
        # interpreter without it as authoritative.
        foreach ($candidate in @($candidates | Select-Object -Skip 1))
        {
            $candidateVersion = Get-GameWipToolCandidateVersion -Tool $Tool -Path $candidate.Path

            if ($null -ne $candidateVersion)
            {
                $selected = $candidate
                $version = $candidateVersion
                break
            }
        }
    }

    if ($usesPackageQueryHost -and $null -eq $version)
    {
        # The executables are only package-query hosts, not evidence that the
        # provider package exists.
        return New-GameWipDetectedToolResult -Installed $false -Candidates $candidates
    }

    return New-GameWipDetectedToolResult `
        -Installed $true `
        -Version $version `
        -Location $selected.Path `
        -Source $selected.Source `
        -SelectionReason $selected.SelectionReason `
        -Candidates $candidates
}

function Show-GameWipToolStatus
{
    Initialize-GameWipStorage
    Write-GameWipSection 'Project tool status (offline)'
    Write-Host '  Versions are read from this machine; no network requests are made.'
    Write-Host ''
    Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-11} {4}' -f 'Tool', 'Requirement', 'Installed', 'State', 'Provider')
    Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-11} {4}' -f ('-' * 20), ('-' * 13), ('-' * 13), ('-' * 11), ('-' * 18))
    $results = [System.Collections.Generic.List[object]]::new()

    # Print each installed version and any missing dependencies.
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $status = Get-GameWipToolStatus -Tool $toolInfo
        $detected = $status.Detected
        $required = if ($toolInfo.Contains('requiredVersion'))
        {
            [string]$toolInfo.requiredVersion
        }
        else
        {
            [string]$toolInfo.versionPolicy
        }
        $installed = if ($detected.Installed -and $detected.Version)
        {
            [string]$detected.Version
        }
        elseif ($detected.Installed)
        {
            'unknown'
        }
        else
        {
            '-'
        }

        if ($installed.Length -gt 13)
        {
            $installed = $installed.Substring(0, 10) + '...'
        }
        $state = $status.State
        $stateSemantic = Get-GameWipToolCompatibilitySemantic -Compatibility $state
        Write-Host ('  {0,-20} {1,-13} {2,-13} ' -f $toolInfo.id, $required, $installed) -NoNewline
        Write-GameWipSemanticText -Object ('{0,-11}' -f $state) -Semantic $stateSemantic -NoNewline
        Write-Host (' {0}' -f $toolInfo.provider.kind)
        $results.Add($status) | Out-Null

        foreach ($dependency in @($status.DependencyFailures))
        {
            $detail = if ($dependency.Reason)
            {
                " - $($dependency.Reason)"
            }
            else
            {
                ''
            }
            Write-Host "    Dependency $($dependency.Package): " -NoNewline
            Write-GameWipSemanticText -Object ("$($dependency.State)$detail") -Semantic Failure
        }
    }

    # Summarize the observed tool states.
    $groups = @($results | Group-Object State | Sort-Object Name)
    Write-Host ''
    Write-Host '  Summary: ' -NoNewline

    for ($groupIndex = 0; $groupIndex -lt $groups.Count; ++$groupIndex)
    {
        $group = $groups[$groupIndex]
        $semantic = switch ($group.Name)
        {
            'compatible'
            {
                'Success'
            }
            'missing'
            {
                'Failure'
            }
            'dependency-missing'
            {
                'Failure'
            }
            default
            {
                'Warning'
            }
        }
        Write-GameWipSemanticText -Object ("$($group.Count) $($group.Name)") -Semantic $semantic -NoNewline
        Write-Host $(if ($groupIndex -lt ($groups.Count - 1))
            {
                '; '
            }
            else
            {
                '.'
            }) -NoNewline:($groupIndex -lt ($groups.Count - 1))
    }

    # Explain selection when a tool is incompatible or has alternatives.
    $details = @($results | Where-Object {
            $_.Tool.versionPolicy -ne 'informational' -and
            $_.Detected.Installed -and
            (@($_.Detected.Candidates).Count -gt 1 -or $_.State -ne 'compatible')
        })

    if ($details.Count -eq 0)
    {
        return
    }

    Write-GameWipSection 'Selection details'
    Write-Host '  Declared provider locations take precedence over PATH fallbacks.'

    foreach ($result in $details)
    {
        $toolInfo = $result.Tool
        $detected = $result.Detected
        Write-Host ''
        Write-Host "  $($toolInfo.id)"
        Write-Host '    Using:  ' -NoNewline
        Write-GameWipSemanticText -Object ([string]$detected.Location) -Semantic Muted
        Write-Host '    Source: ' -NoNewline
        Write-GameWipSemanticText -Object "$($detected.Source) ($($detected.SelectionReason))" -Semantic Muted
        $alternatives = @($detected.Candidates | Select-Object -Skip 1)

        if ($alternatives.Count -ne 0)
        {
            Write-Host '    Also found:'

            foreach ($candidate in $alternatives)
            {
                $candidatePath = if ($candidate.PSObject.Properties['Path'])
                {
                    $candidate.Path
                }
                else
                {
                    $candidate.Location
                }
                $candidateVersion = if ($candidate.PSObject.Properties['Version'])
                {
                    $candidate.Version
                }
                else
                {
                    Get-GameWipToolCandidateVersion -Tool $toolInfo -Path $candidatePath
                }
                $version = if ($candidateVersion)
                {
                    [string]$candidateVersion
                }
                else
                {
                    'unknown version'
                }
                Write-Host '      - ' -NoNewline
                Write-GameWipSemanticText -Object "$candidatePath ($version; $($candidate.Source))" -Semantic Muted
            }
        }
    }
}
