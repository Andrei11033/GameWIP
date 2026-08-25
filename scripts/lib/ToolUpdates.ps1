# GameWIP tool update/ensure planning, preflight, installation, and tracked commit behavior.

Set-StrictMode -Version Latest

function Get-GameWipRepositoryRelativePath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPath = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $comparison = if (Test-GameWipWindowsHost)
    {
        [StringComparison]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparison]::Ordinal
    }
    $prefix = $rootPath + [IO.Path]::DirectorySeparatorChar
    if ($fullPath.StartsWith($prefix, $comparison))
    {
        return $fullPath.Substring($prefix.Length).Replace('\', '/')
    }
    return $fullPath
}


function Get-GameWipToolUpdatePlan
{
    param([string]$ToolId)
    $selected = @(Get-GameWipProjectToolSelection -Selector $(if ([string]::IsNullOrWhiteSpace($ToolId))
            {
                'all'
            }
            else
            {
                $ToolId
            }))

    $plan = [System.Collections.Generic.List[object]]::new()
    Write-Host "Checking $($selected.Count) declared tool(s) for upstream versions..."
    for ($toolIndex = 0; $toolIndex -lt $selected.Count; ++$toolIndex)
    {
        $toolInfo = $selected[$toolIndex]
        Assert-GameWipNotCancelled
        Write-Host "  [$($toolIndex + 1)/$($selected.Count)] $($toolInfo.id)"
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
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
        Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-20} {4}' -f $item.Tool.id, $required, $installedDisplay, $latestDisplay, $item.Tool.provider.kind)
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
            $details.Add("$($item.Tool.id) dependency $($dependency.package): required=$($dependency.version), latest=$(if ($dependencyQuery.State -eq 'resolved') { $dependencyQuery.Version } else { $dependencyQuery.State })") | Out-Null
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

function Get-GameWipReferenceTexts
{
    param([hashtable]$Reference, [string]$OldVersion, [string]$NewVersion)
    if ($Reference.kind -eq 'path')
    {
        return $null
    }
    if ($Reference.kind -eq 'cmakeMinimum')
    {
        return [pscustomobject]@{ Old = "cmake_minimum_required(VERSION $OldVersion)"; New = "cmake_minimum_required(VERSION $NewVersion)" }
    }
    if ($Reference.kind -eq 'text')
    {
        $pattern = [string]$Reference.pattern
        return [pscustomobject]@{ Old = $pattern.Replace('{version}', $OldVersion); New = $pattern.Replace('{version}', $NewVersion) }
    }
    throw "Unknown live version reference kind '$($Reference.kind)'."
}

function Get-GameWipTrackedToolMutationPlan
{
    param([Parameter(Mandatory = $true)][array]$Plan)
    $registry = Read-GameWipJsonConfig -Path $ProjectToolsPath -Name 'project tools'
    $staged = @{}
    $descriptions = [System.Collections.Generic.List[string]]::new()
    $registryChanged = $false

    foreach ($item in $Plan)
    {
        $tool = $item.Tool
        if (-not $tool.capabilities.update)
        {
            continue
        }
        if ($tool.capabilities.checkLatest -and $item.Query.State -notin @('resolved', 'unsupported'))
        {
            throw (New-GameWipDiagnosticException -Code 'tool-query-incomplete' -Summary "Unable to resolve the complete update plan for '$($tool.id)'." -Details "$($item.Query.State): $($item.Query.Reason)" -SuggestedActions @('Retry after restoring provider/network availability.', 'Use tools check-updates to inspect provider state without mutation.'))
        }
        if ($tool.capabilities.checkLatest -and $item.Query.State -eq 'unsupported' -and $tool.versionPolicy -eq 'exact')
        {
            throw "Exact-version tool '$($tool.id)' cannot be updated without a resolved latest version."
        }

        $registryTool = @($registry.tools | Where-Object { $_.id -eq $tool.id } | Select-Object -First 1)
        if ($registryTool.Count -ne 1)
        {
            throw "Tool '$($tool.id)' disappeared from staged project-tools.json."
        }
        $registryTool = $registryTool[0]

        if ($tool.versionPolicy -eq 'exact' -and $item.Latest -and $item.Latest -ne $tool.requiredVersion)
        {
            $oldVersion = [string]$tool.requiredVersion
            $newVersion = [string]$item.Latest
            foreach ($reference in @($tool.references))
            {
                if ($reference.kind -eq 'path' -or [string]$reference.path -like 'docs/releases/*')
                {
                    continue
                }
                $fullPath = Join-Path $RepositoryRoot ([string]$reference.path)
                $current = if ($staged.ContainsKey($fullPath))
                {
                    [string]$staged[$fullPath]
                }
                else
                {
                    Get-Content -Raw -LiteralPath $fullPath
                }
                $texts = Get-GameWipReferenceTexts -Reference $reference -OldVersion $oldVersion -NewVersion $newVersion
                $expectedCount = if ($reference.Contains('expectedCount'))
                {
                    [int]$reference.expectedCount
                }
                else
                {
                    1
                }
                $count = [regex]::Matches($current, [regex]::Escape($texts.Old)).Count
                if ($count -ne $expectedCount)
                {
                    throw "Live version reference '$($reference.path)' expected $expectedCount exact match(es), found $count."
                }
                $staged[$fullPath] = $current.Replace($texts.Old, $texts.New)
                $descriptions.Add("$($reference.path): $oldVersion -> $newVersion") | Out-Null
            }
            $registryTool.requiredVersion = $newVersion
            $registryChanged = $true
            if ($tool.provider.kind -eq 'githubRelease')
            {
                if ($item.Query.State -ne 'resolved' -or $null -eq $item.ReleaseMetadata)
                {
                    throw "Complete verified release metadata is required for '$($tool.id)' $newVersion."
                }
                $registryTool.provider.releaseTag = $item.ReleaseMetadata.Tag
                foreach ($key in @($tool.provider.assets.Keys))
                {
                    $plannedAsset = $item.ReleaseMetadata.Assets[$key]
                    if ($null -eq $plannedAsset)
                    {
                        throw "Planned release metadata for '$($tool.id)' is missing '$key'."
                    }
                    $registryTool.provider.assets[$key].archive = $plannedAsset.archive
                    $registryTool.provider.assets[$key].sha256 = $plannedAsset.sha256
                }
            }
        }

        if ($item.LatestDependencies.Count -ne 0)
        {
            foreach ($dependency in @($registryTool.provider.dependencies))
            {
                if (-not $dependency.Contains('version'))
                {
                    continue
                }
                $package = [string]$dependency.package
                if (-not $item.LatestDependencies.ContainsKey($package))
                {
                    continue
                }
                $newDependencyVersion = [string]$item.LatestDependencies[$package]
                if ($newDependencyVersion -ne [string]$dependency.version)
                {
                    $descriptions.Add("project-tools.json dependency ${package}: $($dependency.version) -> $newDependencyVersion") | Out-Null
                    $dependency.version = $newDependencyVersion
                    $registryChanged = $true
                }
            }
        }
    }

    if ($registryChanged)
    {
        $prettierTool = Get-GameWipProjectTool -Id 'prettier'
        $prettier = Get-GameWipDetectedTool -Tool $prettierTool
        if ((Get-GameWipToolCompatibility -Tool $prettierTool -Detected $prettier) -ne 'compatible')
        {
            throw 'Prettier must be available before a project-tools.json update can be staged.'
        }
        $temporaryRegistry = Join-Path $Script:OperationContext.Temp 'project-tools.staged.json'
        [IO.File]::WriteAllText($temporaryRegistry, (($registry | ConvertTo-Json -Depth 30) + "`n"), [Text.UTF8Encoding]::new($false))
        $config = Join-Path $RepositoryRoot 'config\quality\prettier.json'
        $formatResult = Invoke-GameWipProcess -FilePath $prettier.Location -Arguments @('--config', $config, '--write', $temporaryRegistry) -OutputMode LogOnly -TimeoutSeconds 60
        if ($formatResult.ExitCode -ne 0)
        {
            throw "Prettier could not format the staged project-tools registry. See $($formatResult.LogPath)"
        }
        $null = Get-Content -Raw -LiteralPath $temporaryRegistry | ConvertFrom-Json
        $staged[$ProjectToolsPath] = Get-Content -Raw -LiteralPath $temporaryRegistry
        $descriptions.Add('scripts/config/project-tools.json: staged once after all tool/dependency changes') | Out-Null
    }

    return [pscustomobject]@{ Files = $staged; Registry = $registry; RegistryChanged = $registryChanged; Descriptions = @($descriptions) }
}

function Get-GameWipPlannedInstallTool
{
    param([Parameter(Mandatory = $true)]$PlanItem, [Parameter(Mandatory = $true)]$TrackedPlan)
    $tool = Copy-GameWipValue -Value $PlanItem.Tool
    $registryTool = @($TrackedPlan.Registry.tools | Where-Object { $_.id -eq $tool.id } | Select-Object -First 1)
    if ($registryTool.Count -eq 1)
    {
        return Copy-GameWipValue -Value $registryTool[0]
    }
    return $tool
}

function Invoke-GameWipToolUpdate
{
    param([string]$ToolId, [switch]$PreviewOnly)
    if ([string]::IsNullOrWhiteSpace($ToolId))
    {
        $ToolId = 'all'
    }

    Write-GameWipOperationEvent -Phase discover -Severity info -Message "Resolving project-tool state for '$ToolId'..."
    $plan = @(Get-GameWipToolUpdatePlan -ToolId $ToolId)
    $trackedPlan = Get-GameWipTrackedToolMutationPlan -Plan $plan

    Write-GameWipSection "Tool update plan$(if ($PreviewOnly -or ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)) { ' (preview)' } else { '' })"
    Show-GameWipToolUpdatePlan -Plan $plan
    if ($trackedPlan.Descriptions.Count -ne 0)
    {
        Write-Host '  Tracked mutations:'
        foreach ($description in $trackedPlan.Descriptions)
        {
            Write-Host "    - $description"
        }
    }
    else
    {
        Write-Host '  Tracked mutations: none'
    }

    if ($PreviewOnly -or ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview))
    {
        return
    }
    Assert-GameWipCleanTrackedTree
    $planLines = @($plan | Where-Object { $_.Tool.capabilities.update } | ForEach-Object { "$($_.Tool.id): install/verify $(if ($_.Latest) { $_.Latest } elseif ($_.Tool.Contains('requiredVersion')) { $_.Tool.requiredVersion } else { 'provider-managed version' })" }) + @($trackedPlan.Descriptions)
    if (-not (Confirm-GameWipMutation -Summary 'Apply the complete project-tool update plan?' -Risk machine -Plan $planLines))
    {
        Write-Host 'Project-tool update cancelled; no changes were made.'
        return
    }

    Initialize-GameWipManagedToolRoot
    Set-GameWipMutationState -State partial

    foreach ($item in $plan)
    {
        Assert-GameWipNotCancelled
        if (-not $item.Tool.capabilities.update)
        {
            Write-Host "  [skip] $($item.Tool.id): provider does not support updates"
            continue
        }
        $installTool = Get-GameWipPlannedInstallTool -PlanItem $item -TrackedPlan $trackedPlan
        $version = if ($installTool.Contains('requiredVersion'))
        {
            [string]$installTool.requiredVersion
        }
        else
        {
            $null
        }
        Write-GameWipOperationEvent -Phase execute -Step $item.Tool.id -Severity progress -Message "Installing/verifying $($item.Tool.name) $(if ($version) { $version } else { '' })..."
        $functionName = Get-GameWipProviderFunction -Tool $installTool -Operation Install
        & $functionName -Tool $installTool -Version $version
        $verified = Get-GameWipDetectedTool -Tool $installTool
        $compatibility = Get-GameWipToolCompatibility -Tool $installTool -Detected $verified
        if ($compatibility -ne 'compatible' -or -not (Test-GameWipDetectedToolFromDeclaredProvider -Tool $installTool -Detected $verified))
        {
            throw "Tool '$($installTool.id)' failed post-install verification (state=$compatibility, location=$($verified.Location))."
        }
        Add-GameWipOperationChange -Message "Installed/verified $($installTool.id) at $($verified.Location)"
    }

    foreach ($entry in $trackedPlan.Files.GetEnumerator())
    {
        Assert-GameWipNotCancelled
        Write-GameWipTextAtomic -Path ([string]$entry.Key) -Content ([string]$entry.Value)
        Add-GameWipOperationChange -Message "Updated tracked file $(Get-GameWipRepositoryRelativePath -Path ([string]$entry.Key))"
    }

    if ($trackedPlan.RegistryChanged)
    {
        $script:ProjectTools = Read-GameWipJsonConfig -Path $ProjectToolsPath -Name 'project tools' -SchemaPath (Join-Path $ScriptsRoot 'schemas\project-tools.schema.json')
        Assert-GameWipProjectToolConfig
    }

    Write-GameWipOperationEvent -Phase verify -Severity info -Message 'Running the complete quality gate after the update...'
    Invoke-GameWipQuality -Mode check
    Invoke-GameWipNative -Name 'git-status-after-tool-update' -FilePath 'git' -Arguments @('-C', $RepositoryRoot, 'status', '--short')
    Set-GameWipMutationState -State complete
}

function Assert-GameWipPinnedToolAvailable
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)
    if ($Tool.versionPolicy -ne 'exact' -or -not $Tool.Contains('requiredVersion'))
    {
        return
    }
    $version = [string]$Tool.requiredVersion
    switch ([string]$Tool.provider.kind)
    {
        'python'
        {
            $result = Invoke-GameWipHttpJson -Uri "https://pypi.org/pypi/$($Tool.provider.package)/$version/json" -MaxAttempts 3 -TimeoutSeconds 20
            if ($result.State -ne 'resolved')
            {
                throw "Pinned Python package '$($Tool.provider.package)==$version' is unavailable: $($result.Reason)"
            }
        }
        'npm'
        {
            $npm = Resolve-GameWipToolCommand -Command npm
            if ($null -eq $npm)
            {
                throw "npm is unavailable while preflighting '$($Tool.id)'."
            }
            $query = Invoke-GameWipToolQueryProcess -FilePath $npm -Arguments @('view', "$($Tool.provider.package)@$version", 'version') -Provider npm
            if ($query.State -ne 'process' -or $query.Output.Trim() -ne $version)
            {
                throw "Pinned npm package '$($Tool.provider.package)@$version' is unavailable."
            }
            foreach ($dependency in $(if ($Tool.provider.Contains('dependencies'))
                    {
                        @($Tool.provider.dependencies)
                    }
                    else
                    {
                        @()
                    }))
            {
                $depQuery = Invoke-GameWipToolQueryProcess -FilePath $npm -Arguments @('view', "$($dependency.package)@$($dependency.version)", 'version') -Provider npm
                if ($depQuery.State -ne 'process')
                {
                    throw "Pinned npm dependency '$($dependency.package)@$($dependency.version)' is unavailable."
                }
            }
        }
        'powershellGallery'
        {
            try
            {
                $null = Find-Module -Name ([string]$Tool.provider.package) -RequiredVersion $version -Repository PSGallery -ErrorAction Stop
            }
            catch
            {
                throw "Pinned PowerShell Gallery module '$($Tool.provider.package) $version' is unavailable: $($_.Exception.Message)"
            }
        }
        'githubRelease'
        {
            if (-not $Tool.provider.Contains('releaseTag') -or -not $Tool.provider.Contains('assets'))
            {
                throw "Pinned GitHub-release tool '$($Tool.id)' lacks verified release metadata."
            }
        }
    }
}

function Invoke-GameWipToolEnsure
{
    param([string]$Selector = 'all', [switch]$ConsentAlreadyGranted)
    $selected = @(Get-GameWipProjectToolSelection -Selector $Selector)
    if (-not (Test-GameWipWindowsHost))
    {
        $hostManaged = @($selected | Where-Object { $_.provider.kind -in @('msys2', 'winget') })
        foreach ($tool in $hostManaged)
        {
            Write-Verbose "Skipping Windows-managed provider '$($tool.id)' on this host; the runner/package image owns it."
        }
        $selected = @($selected | Where-Object { $_.provider.kind -notin @('msys2', 'winget') })
    }
    foreach ($tool in @($selected | Where-Object { -not $_.capabilities.detectInstalled -or -not $_.capabilities.update }))
    {
        Write-Verbose "Skipping non-installable tool '$($tool.id)' during ensure; its lifecycle is owned by setup, repository state, or an external source."
    }
    $selected = @($selected | Where-Object { $_.capabilities.detectInstalled -and $_.capabilities.update })
    $plan = [System.Collections.Generic.List[object]]::new()
    Write-Host "Checking $($selected.Count) declared tool(s)..."
    for ($toolIndex = 0; $toolIndex -lt $selected.Count; ++$toolIndex)
    {
        $tool = $selected[$toolIndex]
        Write-Host "  [$($toolIndex + 1)/$($selected.Count)] $($tool.id)"
        $detected = Get-GameWipDetectedTool -Tool $tool
        $compatibility = Get-GameWipToolCompatibility -Tool $tool -Detected $detected
        $providerOk = Test-GameWipDetectedToolFromDeclaredProvider -Tool $tool -Detected $detected
        $needsInstall = $compatibility -ne 'compatible' -or (-not $providerOk -and $tool.provider.kind -in @('python', 'npm', 'powershellGallery', 'githubRelease'))
        $plan.Add([pscustomobject]@{ Tool = $tool; Detected = $detected; Compatibility = $compatibility; ProviderOk = $providerOk; NeedsInstall = $needsInstall }) | Out-Null
        if ($needsInstall)
        {
            Assert-GameWipPinnedToolAvailable -Tool $tool
        }
    }

    Write-GameWipSection "Tool ensure plan: $Selector"
    foreach ($item in $plan)
    {
        Write-Host ('  {0,-20} {1,-11} {2}' -f $item.Tool.id, $(if ($item.NeedsInstall)
                {
                    'ensure'
                }
                else
                {
                    'ready'
                }), $(if ($item.Detected.Location)
                {
                    $item.Detected.Location
                }
                else
                {
                    '-'
                }))
    }
    $needed = @($plan | Where-Object { $_.NeedsInstall })
    if ($needed.Count -eq 0)
    {
        Write-GameWipHost 'All selected tools already satisfy the declared policy.' -ForegroundColor Green; return
    }
    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)
    {
        return
    }

    $risk = if (Test-GameWipWindowsHost)
    {
        'machine'
    }
    else
    {
        'local'
    }
    if (-not $ConsentAlreadyGranted -and -not (Confirm-GameWipMutation -Summary "Install/repair $($needed.Count) selected tool(s) without changing repository pins?" -Risk $risk -Plan @($needed | ForEach-Object { "$($_.Tool.id): declared $($_.Tool.versionPolicy) version" })))
    {
        return
    }
    Initialize-GameWipManagedToolRoot
    Set-GameWipMutationState -State partial
    foreach ($item in $needed)
    {
        Assert-GameWipNotCancelled
        $tool = $item.Tool
        if (-not (Test-GameWipWindowsHost) -and $tool.provider.kind -in @('msys2', 'winget'))
        {
            throw "Tool '$($tool.id)' uses Windows provider '$($tool.provider.kind)' and must be provisioned by the runner image/package manager."
        }
        $version = if ($tool.Contains('requiredVersion'))
        {
            [string]$tool.requiredVersion
        }
        else
        {
            $null
        }
        $functionName = Get-GameWipProviderFunction -Tool $tool -Operation Install
        & $functionName -Tool $tool -Version $version
        $verified = Get-GameWipDetectedTool -Tool $tool
        $state = Get-GameWipToolCompatibility -Tool $tool -Detected $verified
        if ($state -ne 'compatible')
        {
            throw "Tool '$($tool.id)' is '$state' after ensure."
        }
        Add-GameWipOperationChange -Message "Ensured $($tool.id) at $($verified.Location)"
    }
    Set-GameWipMutationState -State complete
}
