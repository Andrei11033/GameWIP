# ------------------------------------------------------------
# Tracked version mutation plans
# ------------------------------------------------------------

function Get-GameWipReferenceTexts
{
    param(
        [hashtable]$Reference,
        [hashtable]$Tool,
        [string]$OldVersion,
        [string]$NewVersion,
        $ProviderMetadata
    )

    if ($Reference.kind -eq 'path')
    {
        return $null
    }

    if ($Reference.kind -eq 'cmakeMinimum')
    {
        return [pscustomobject]@{
            Old = "cmake_minimum_required(VERSION $OldVersion)"
            New = "cmake_minimum_required(VERSION $NewVersion)"
        }
    }

    if ($Reference.kind -eq 'text')
    {
        $pattern = [string]$Reference.pattern
        return [pscustomobject]@{
            Old = $pattern.Replace('{version}', $OldVersion)
            New = $pattern.Replace('{version}', $NewVersion)
        }
    }

    if ($Reference.kind -eq 'releaseAsset')
    {
        $assetKey = [string]$Reference.assetKey
        $assetField = [string]$Reference.assetField
        $oldValue = [string]$Tool.provider.assets[$assetKey][$assetField]
        $newValue = [string]$ProviderMetadata.Assets[$assetKey][$assetField]

        if ([string]::IsNullOrWhiteSpace($newValue))
        {
            throw "Latest release metadata for '$($Tool.id)' is missing '$assetKey.$assetField'."
        }
        $pattern = [string]$Reference.pattern

        return [pscustomobject]@{
            Old = $pattern.Replace('{value}', $oldValue)
            New = $pattern.Replace('{value}', $newValue)
        }
    }

    throw "Unknown live version reference kind '$($Reference.kind)'."
}

function Get-GameWipTrackedToolMutationPlan
{
    param([Parameter(Mandatory = $true)][array]$Plan)

    $registry = Read-GameWipJsonConfig -Path $ProjectToolsPath -Name 'project tools'
    $staged = @{}
    $descriptions = [System.Collections.Generic.List[string]]::new()
    $registryMutations = [System.Collections.Generic.List[object]]::new()
    $updatedToolIds = @{}

    # Plan registry and live-reference changes without writing files.
    foreach ($item in $Plan)
    {
        $tool = $item.Tool

        if (-not $tool.capabilities.update)
        {
            continue
        }

        if ($tool.capabilities.checkLatest -and $item.Query.State -notin @('resolved', 'unsupported'))
        {
            throw (New-GameWipDiagnosticException `
                    -Code 'tool-query-incomplete' `
                    -Summary "Unable to resolve the complete update plan for '$($tool.id)'." `
                    -Details "$($item.Query.State): $($item.Query.Reason)" `
                    -SuggestedActions @(
                    'Retry after restoring provider/network availability.',
                    'Use tools check-updates to inspect provider state without mutation.'
                ))
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

        $updatePin = $tool.versionPolicy -eq 'exact' -or
        ($tool.capabilities.Contains('updatePin') -and [bool]$tool.capabilities.updatePin)

        if ($updatePin -and $item.Latest -and $item.Latest -ne $tool.requiredVersion)
        {
            # Stage every declared live reference before updating the pin.
            $oldVersion = [string]$tool.requiredVersion
            $newVersion = [string]$item.Latest

            foreach ($reference in @($tool.references))
            {
                if ($reference.kind -eq 'path')
                {
                    continue
                }
                $fullPath = Resolve-GameWipRepositoryPath -Path ([string]$reference.path)
                $current = if ($staged.ContainsKey($fullPath))
                {
                    [string]$staged[$fullPath]
                }
                else
                {
                    Read-GameWipUtf8Text -Path $fullPath
                }
                $texts = Get-GameWipReferenceTexts `
                    -Reference $reference `
                    -Tool $tool `
                    -OldVersion $oldVersion `
                    -NewVersion $newVersion `
                    -ProviderMetadata $item.ProviderMetadata
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
                $changeDescription = if ($reference.kind -eq 'releaseAsset')
                {
                    "$($tool.id) asset $($reference.assetKey).$($reference.assetField)"
                }
                else
                {
                    "$($tool.id) $oldVersion -> $newVersion"
                }
                $descriptions.Add("$($reference.path): $changeDescription") | Out-Null
            }
            $registryMutations.Add(@{
                    toolId = [string]$tool.id
                    path = @('requiredVersion')
                    expected = $oldVersion
                    value = $newVersion
                }) | Out-Null
            $descriptions.Add(
                "scripts/config/project-tools.json: $($tool.id).requiredVersion $oldVersion -> $newVersion"
            ) | Out-Null
            $registryTool.requiredVersion = $newVersion
            $updatedToolIds[[string]$tool.id] = $true

            if ($tool.provider.kind -in @('githubRelease', 'archive'))
            {
                if ($item.Query.State -ne 'resolved' -or $null -eq $item.ProviderMetadata)
                {
                    throw "Complete verified asset metadata is required for '$($tool.id)' $newVersion."
                }

                if ($tool.provider.kind -eq 'githubRelease')
                {
                    $oldReleaseTag = [string]$registryTool.provider.releaseTag
                    $newReleaseTag = [string]$item.ProviderMetadata.Tag

                    if ($oldReleaseTag -ne $newReleaseTag)
                    {
                        $registryMutations.Add(@{
                                toolId = [string]$tool.id
                                path = @('provider', 'releaseTag')
                                expected = $oldReleaseTag
                                value = $newReleaseTag
                            }) | Out-Null

                        $descriptions.Add(
                            "scripts/config/project-tools.json: $($tool.id).provider.releaseTag $oldReleaseTag -> $newReleaseTag"
                        ) | Out-Null
                    }
                    $registryTool.provider.releaseTag = $item.ProviderMetadata.Tag
                }

                # Keep each registered archive and digest in sync with its provider.
                foreach ($key in @($tool.provider.assets.Keys))
                {
                    $plannedAsset = $item.ProviderMetadata.Assets[$key]

                    if ($null -eq $plannedAsset)
                    {
                        throw "Planned asset metadata for '$($tool.id)' is missing '$key'."
                    }
                    foreach ($field in @('archive', 'sha256'))
                    {
                        $oldAssetValue = [string]$registryTool.provider.assets[$key][$field]
                        $newAssetValue = [string]$plannedAsset[$field]

                        if ($oldAssetValue -ne $newAssetValue)
                        {
                            $registryMutations.Add(@{
                                    toolId = [string]$tool.id
                                    path = @('provider', 'assets', [string]$key, $field)
                                    expected = $oldAssetValue
                                    value = $newAssetValue
                                }) | Out-Null
                            $descriptions.Add(
                                "scripts/config/project-tools.json: $($tool.id).provider.assets.$key.$field $oldAssetValue -> $newAssetValue"
                            ) | Out-Null
                        }
                    }

                    $registryTool.provider.assets[$key].archive = $plannedAsset.archive
                    $registryTool.provider.assets[$key].sha256 = $plannedAsset.sha256
                }
            }
        }

        # Stage resolved npm dependency pins.
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
                    $oldDependencyVersion = [string]$dependency.version
                    $registryMutations.Add(@{
                            toolId = [string]$tool.id
                            path = @('provider', 'dependencies', @{ match = @{ package = $package } }, 'version')
                            expected = $oldDependencyVersion
                            value = $newDependencyVersion
                        }) | Out-Null

                    $descriptions.Add(
                        "scripts/config/project-tools.json: $($tool.id).provider.dependencies[$package].version " +
                        "$oldDependencyVersion -> $newDependencyVersion"
                    ) | Out-Null
                    $dependency.version = $newDependencyVersion
                    $updatedToolIds[[string]$tool.id] = $true
                }
            }
        }
    }

    if ($registryMutations.Count -ne 0)
    {
        # Let the JSON mutator preserve comments, spacing, and key order.
        $node = Resolve-GameWipToolCommand -Command 'node'

        if ([string]::IsNullOrWhiteSpace([string]$node))
        {
            throw 'Node.js is required to stage project-tools.json mutations, but project-tool discovery could not resolve it.'
        }

        # Write a temporary specification and validate the staged registry.
        $temporaryRoot = Get-GameWipTemporaryRoot
        $mutationSpecification = Join-Path $temporaryRoot ('project-tools.mutations.' + [guid]::NewGuid().ToString('N') + '.json')
        $temporaryRegistry = Join-Path $temporaryRoot ('project-tools.staged.' + [guid]::NewGuid().ToString('N') + '.json')
        $mutationData = [ordered]@{ schemaVersion = 1; mutations = @($registryMutations) }
        [IO.File]::WriteAllText($mutationSpecification, (($mutationData | ConvertTo-Json -Depth 30) + "`n"), [Text.UTF8Encoding]::new($false))
        $mutator = Join-Path $ScriptsRoot 'lib\project-tools-json-mutator.js'
        $mutationResult = Invoke-GameWipProcess `
            -FilePath $node `
            -Arguments @($mutator, $ProjectToolsPath, $mutationSpecification, $temporaryRegistry) `
            -OutputMode LogOnly `
            -TimeoutSeconds 60

        if ($mutationResult.ExitCode -ne 0)
        {
            throw "The source-preserving project-tools.json mutation failed. See $($mutationResult.LogPath)"
        }

        $stagedRegistryText = Read-GameWipUtf8Text -Path $temporaryRegistry
        $stagedRegistry = Read-GameWipJsonConfig `
            -Path $temporaryRegistry `
            -Name 'staged project tools' `
            -SchemaPath (Join-Path $ScriptsRoot 'schemas\project-tools.schema.json')
        $previousProjectTools = $Script:ProjectTools

        try
        {
            $Script:ProjectTools = $stagedRegistry
            Assert-GameWipProjectToolConfig
        }

        finally
        {
            $Script:ProjectTools = $previousProjectTools
        }
        $staged[$ProjectToolsPath] = $stagedRegistryText
    }

    return [pscustomobject]@{
        Files = $staged
        Registry = $registry
        RegistryChanged = $registryMutations.Count -ne 0
        RegistryMutations = @($registryMutations)
        UpdatedToolIds = $updatedToolIds
        Descriptions = @($descriptions)
    }
}

function Invoke-GameWipTrackedToolMutation
{
    param([Parameter(Mandatory = $true)]$TrackedPlan)

    $originalTrackedContent = @{}
    $writtenTrackedPaths = [System.Collections.Generic.List[string]]::new()

    # Capture originals before writing any planned file.
    foreach ($entry in $TrackedPlan.Files.GetEnumerator())
    {
        $originalTrackedContent[[string]$entry.Key] = Read-GameWipUtf8Text -Path ([string]$entry.Key)
    }

    try
    {
        # Apply the complete plan, tracking each file for rollback.
        foreach ($entry in $TrackedPlan.Files.GetEnumerator())
        {
            Assert-GameWipNotCancelled
            Write-GameWipTextAtomic -Path ([string]$entry.Key) -Content ([string]$entry.Value)
            $writtenTrackedPaths.Add([string]$entry.Key) | Out-Null
            Add-GameWipOperationChange -Message "Updated tracked file $(Get-GameWipRepositoryRelativePath -Path ([string]$entry.Key))"
        }
    }

    catch
    {
        # Restore in reverse order so a partial plan leaves the tracked tree as it was.
        for ($index = $writtenTrackedPaths.Count - 1; $index -ge 0; --$index)
        {
            $writtenPath = $writtenTrackedPaths[$index]
            Write-GameWipTextAtomic -Path $writtenPath -Content ([string]$originalTrackedContent[$writtenPath])
        }
        throw
    }
}
