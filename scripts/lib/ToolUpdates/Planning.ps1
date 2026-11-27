# ------------------------------------------------------------
# Update plan and display
# ------------------------------------------------------------

function Get-GameWipRepositoryRelativePath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPath = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $comparison = Get-GameWipPathComparison
    $prefix = $rootPath + [IO.Path]::DirectorySeparatorChar

    if ($fullPath.StartsWith($prefix, $comparison))
    {
        return $fullPath.Substring($prefix.Length).Replace('\', '/')
    }

    return $fullPath
}

function Get-GameWipToolUpdatePlan
{
    param([string[]]$ToolId)

    if ($null -eq $ToolId -or $ToolId.Count -eq 0 -or ($ToolId.Count -eq 1 -and [string]::IsNullOrWhiteSpace($ToolId[0])))
    {
        $ToolId = @('all')
    }

    # Resolve the complete selection before querying or mutating anything. A
    # tool selected twice still belongs to this one update plan only once.
    $selected = [System.Collections.Generic.List[object]]::new()
    $selectedIds = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($selector in $ToolId)
    {
        foreach ($tool in @(Get-GameWipProjectToolSelection -Selector $selector))
        {
            if ($selectedIds.Add([string]$tool.id))
            {
                $selected.Add($tool)
            }
        }
    }

    $plan = [System.Collections.Generic.List[object]]::new()
    Write-Host "Checking $($selected.Count) declared tool(s) for upstream versions..."

    # Resolve each selected tool and any pinned package dependencies.
    for ($toolIndex = 0; $toolIndex -lt $selected.Count; ++$toolIndex)
    {
        $toolInfo = $selected[$toolIndex]
        Assert-GameWipNotCancelled
        Write-GameWipStatusLine `
            -Status "$($toolIndex + 1)/$($selected.Count)" `
            -Text ([string]$toolInfo.id) `
            -Semantic Accent `
            -Indent 2
        $currentStatus = Get-GameWipToolStatus -Tool $toolInfo
        $detected = $currentStatus.Detected
        $query = Get-GameWipToolLatestQuery -Tool $toolInfo
        $dependencyQueries = @{}
        $latestDependencies = @{}

        if ($toolInfo.provider.kind -eq 'npm' -and $toolInfo.provider.Contains('dependencies'))
        {
            foreach ($dependency in @($toolInfo.provider.dependencies))
            {
                $dependencyQuery = Get-GameWipNpmPackageLatestQuery -Package ([string]$dependency.package)
                $dependencyQueries[[string]$dependency.package] = $dependencyQuery

                if ($dependencyQuery.State -eq 'resolved')
                {
                    $latestDependencies[[string]$dependency.package] = $dependencyQuery.Version
                }
            }
        }
        $plan.Add([pscustomobject]@{
                Tool = $toolInfo
                Detected = $detected
                Installed = $detected.Version
                Query = $query
                Latest = $query.Version
                LatestDependencies = $latestDependencies
                DependencyQueries = $dependencyQueries
                ReleaseMetadata = $query.Metadata
            }) | Out-Null
    }

    return @($plan)
}

function Show-GameWipToolUpdatePlan
{
    param([array]$Plan)
    Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-20} {4}' -f 'Tool', 'Requirement', 'Installed', 'Latest', 'Provider')
    Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-20} {4}' -f ('-' * 20), ('-' * 13), ('-' * 13), ('-' * 20), ('-' * 18))
    $details = [System.Collections.Generic.List[string]]::new()

    foreach ($item in $Plan)
    {
        $required = if ($item.Tool.Contains('requiredVersion'))
        {
            $item.Tool.requiredVersion
        }
        else
        {
            '-'
        }
        $latestDisplay = if ($item.Query.State -eq 'resolved')
        {
            $item.Latest
        }
        else
        {
            $item.Query.State
        }
        $required = [string]$required
        $latestDisplay = [string]$latestDisplay
        $installedDisplay = if ($item.Installed)
        {
            [string]$item.Installed
        }
        else
        {
            'missing'
        }

        if ($required.Length -gt 13)
        {
            $required = $required.Substring(0, 10) + '...'
        }

        if ($installedDisplay.Length -gt 13)
        {
            $installedDisplay = $installedDisplay.Substring(0, 10) + '...'
        }

        if ($latestDisplay.Length -gt 20)
        {
            $latestDisplay = $latestDisplay.Substring(0, 17) + '...'
        }
        $latestSemantic = if ($item.Query.State -eq 'resolved')
        {
            $updatesPin = $item.Tool.versionPolicy -eq 'exact' -or
                ($item.Tool.capabilities.Contains('updatePin') -and [bool]$item.Tool.capabilities.updatePin)
            $hasActionableDifference = if ($updatesPin -and $item.Tool.Contains('requiredVersion'))
            {
                [string]$item.Latest -ne [string]$item.Tool.requiredVersion
            }
            else
            {
                -not [string]::IsNullOrWhiteSpace([string]$item.Installed) -and [string]$item.Latest -ne [string]$item.Installed
            }

            if ($hasActionableDifference)
            {
                'Accent'
            }
            else
            {
                'Success'
            }
        }
        elseif ($item.Query.State -eq 'unsupported')
        {
            'Muted'
        }
        else
        {
            'Warning'
        }

        Write-Host ('  {0,-20} {1,-13} ' -f $item.Tool.id, $required) -NoNewline

        if ($installedDisplay -eq 'missing')
        {
            Write-GameWipSemanticText -Object ('{0,-13}' -f $installedDisplay) -Semantic Failure -NoNewline
        }
        else
        {
            Write-Host ('{0,-13}' -f $installedDisplay) -NoNewline
        }
        Write-Host ' ' -NoNewline
        Write-GameWipSemanticText -Object ('{0,-20}' -f $latestDisplay) -Semantic $latestSemantic -NoNewline
        Write-Host (' {0}' -f $item.Tool.provider.kind)

        if (-not [string]::IsNullOrWhiteSpace([string]$item.Query.Reason))
        {
            $details.Add("$($item.Tool.id): $($item.Query.Reason)") | Out-Null
        }
        $dependencies = if ($item.Tool.provider.Contains('dependencies'))
        {
            @($item.Tool.provider.dependencies)
        }
        else
        {
            @()
        }

        foreach ($dependency in $dependencies)
        {
            if (-not $dependency.Contains('version'))
            {
                continue
            }
            $dependencyQuery = $item.DependencyQueries[[string]$dependency.package]

            $latestDependency = if ($dependencyQuery.State -eq 'resolved')
            {
                $dependencyQuery.Version
            }
            else
            {
                $dependencyQuery.State
            }
            $details.Add(
                "$($item.Tool.id) dependency $($dependency.package): required=$($dependency.version), latest=$latestDependency"
            ) | Out-Null
        }
    }

    if ($details.Count -ne 0)
    {
        Write-Host ''
        Write-Host '  Query details:'

        foreach ($detail in $details)
        {
            Write-Host "    - $detail"
        }
    }
}
