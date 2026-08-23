# GameWIP Config helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function ConvertTo-GameWipHashtable
{
    param([AllowNull()]$Value)

    if ($null -eq $Value -or $Value -is [string] -or $Value.GetType().IsValueType)
    {
        return $Value
    }
    if ($Value -is [System.Collections.IDictionary])
    {
        $result = @{}
        foreach ($key in $Value.Keys)
        {
            $result[[string]$key] = ConvertTo-GameWipHashtable -Value $Value[$key]
        }
        return $result
    }
    if ($Value -is [System.Collections.IEnumerable])
    {
        $items = @($Value | ForEach-Object { ConvertTo-GameWipHashtable -Value $_ })
        return , $items
    }

    $result = @{}
    foreach ($property in $Value.PSObject.Properties)
    {
        $result[$property.Name] = ConvertTo-GameWipHashtable -Value $property.Value
    }
    return $result
}

function Read-GameWipJsonConfig
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$SchemaPath
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        throw "Required $Name configuration is missing: $Path"
    }

    try
    {
        $parsed = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    }
    catch
    {
        throw "Malformed $Name configuration '$Path': $($_.Exception.Message)"
    }

    $config = ConvertTo-GameWipHashtable -Value $parsed
    if (-not $config.Contains('schemaVersion'))
    {
        throw "$Name configuration '$Path' does not declare schemaVersion."
    }
    if ([int]$config.schemaVersion -ne 1)
    {
        throw "Unsupported $name configuration schemaVersion '$($config.schemaVersion)'; expected 1."
    }
    if (-not [string]::IsNullOrWhiteSpace($SchemaPath))
    {
        if (-not (Test-Path -LiteralPath $SchemaPath -PathType Leaf))
        {
            throw "Required $Name schema is missing: $SchemaPath"
        }
        $schema = ConvertTo-GameWipHashtable -Value (Get-Content -Raw -LiteralPath $SchemaPath | ConvertFrom-Json)
        Assert-GameWipJsonSchemaValue -Value $config -Schema $schema -RootSchema $schema -JsonPath '$'
    }
    return $config
}

function Resolve-GameWipJsonSchemaReference
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$RootSchema,
        [Parameter(Mandatory = $true)][string]$Reference
    )

    if (-not $Reference.StartsWith('#/'))
    {
        throw "Unsupported JSON Schema reference '$Reference'."
    }
    $value = $RootSchema
    foreach ($segment in $Reference.Substring(2).Split('/'))
    {
        $key = $segment.Replace('~1', '/').Replace('~0', '~')
        if (-not $value.Contains($key))
        {
            throw "Unresolved JSON Schema reference '$Reference'."
        }
        $value = $value[$key]
    }
    return $value
}

function Test-GameWipJsonSchemaCondition
{
    param($Value, [hashtable]$Schema, [hashtable]$RootSchema)
    try
    {
        Assert-GameWipJsonSchemaValue -Value $Value -Schema $Schema -RootSchema $RootSchema -JsonPath '$condition'
        return $true
    }
    catch
    {
        return $false
    }
}

function Assert-GameWipJsonSchemaValue
{
    param(
        [AllowNull()]$Value,
        [Parameter(Mandatory = $true)][hashtable]$Schema,
        [Parameter(Mandatory = $true)][hashtable]$RootSchema,
        [Parameter(Mandatory = $true)][string]$JsonPath
    )

    if ($Schema.Contains('$ref'))
    {
        $resolved = Resolve-GameWipJsonSchemaReference -RootSchema $RootSchema -Reference ([string]$Schema['$ref'])
        Assert-GameWipJsonSchemaValue -Value $Value -Schema $resolved -RootSchema $RootSchema -JsonPath $JsonPath
        return
    }
    if ($Schema.Contains('allOf'))
    {
        foreach ($part in @($Schema.allOf))
        {
            if ($part.Contains('if'))
            {
                if (Test-GameWipJsonSchemaCondition -Value $Value -Schema $part.if -RootSchema $RootSchema)
                {
                    Assert-GameWipJsonSchemaValue -Value $Value -Schema $part.then -RootSchema $RootSchema -JsonPath $JsonPath
                }
            }
            else
            {
                Assert-GameWipJsonSchemaValue -Value $Value -Schema $part -RootSchema $RootSchema -JsonPath $JsonPath
            }
        }
    }
    if ($Schema.Contains('const') -and $Value -ne $Schema.const)
    {
        throw "$JsonPath must equal '$($Schema.const)'."
    }
    if ($Schema.Contains('enum') -and @($Schema.enum) -notcontains $Value)
    {
        throw "$JsonPath has unsupported value '$Value'."
    }
    if ($Schema.Contains('type'))
    {
        $schemaMatches = switch ([string]$Schema.type)
        {
            'object'
            {
                $Value -is [System.Collections.IDictionary]
            }
            'array'
            {
                $Value -is [array]
            }
            'string'
            {
                $Value -is [string]
            }
            'boolean'
            {
                $Value -is [bool]
            }
            'integer'
            {
                $Value -is [int] -or $Value -is [long]
            }
            default
            {
                $true
            }
        }
        if (-not $schemaMatches)
        {
            throw "$JsonPath must be of JSON type '$($Schema.type)'."
        }
    }
    if ($Value -is [System.Collections.IDictionary])
    {
        $requiredProperties = if ($Schema.Contains('required'))
        {
            @($Schema.required)
        }
        else
        {
            @()
        }
        foreach ($required in $requiredProperties)
        {
            if (-not $Value.Contains($required))
            {
                throw "$JsonPath is missing required property '$required'."
            }
        }
        if ($Schema.Contains('additionalProperties') -and $Schema.additionalProperties -eq $false)
        {
            foreach ($key in $Value.Keys)
            {
                if (-not $Schema.properties.Contains($key))
                {
                    throw "$JsonPath contains unknown property '$key'."
                }
            }
        }
        if ($Schema.Contains('properties'))
        {
            foreach ($key in $Value.Keys)
            {
                if ($Schema.properties.Contains($key))
                {
                    Assert-GameWipJsonSchemaValue -Value $Value[$key] -Schema $Schema.properties[$key] -RootSchema $RootSchema -JsonPath "$JsonPath.$key"
                }
            }
        }
    }
    if ($Value -is [array])
    {
        if ($Schema.Contains('minItems') -and $Value.Count -lt [int]$Schema.minItems)
        {
            throw "$JsonPath has too few items."
        }
        if ($Schema.Contains('uniqueItems') -and $Schema.uniqueItems -eq $true -and @($Value | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 20 } | Select-Object -Unique).Count -ne $Value.Count)
        {
            throw "$JsonPath contains duplicate items."
        }
        if ($Schema.Contains('items'))
        {
            for ($index = 0; $index -lt $Value.Count; ++$index)
            {
                Assert-GameWipJsonSchemaValue -Value $Value[$index] -Schema $Schema.items -RootSchema $RootSchema -JsonPath "$JsonPath[$index]"
            }
        }
    }
    if ($Value -is [string])
    {
        if ($Schema.Contains('minLength') -and $Value.Length -lt [int]$Schema.minLength)
        {
            throw "$JsonPath is too short."
        }
        if ($Schema.Contains('maxLength') -and $Value.Length -gt [int]$Schema.maxLength)
        {
            throw "$JsonPath is too long."
        }
        if ($Schema.Contains('pattern') -and $Value -notmatch [string]$Schema.pattern)
        {
            throw "$JsonPath does not match its required pattern."
        }
    }
    if ($Schema.Contains('minimum') -and $Value -lt $Schema.minimum)
    {
        throw "$JsonPath is below its minimum."
    }
}

function Assert-GameWipProjectConfig
{
    foreach ($field in @('repository', 'defaultBranch', 'storage', 'managedEnvironment'))
    {
        if (-not $ProjectConfig.Contains($field))
        {
            throw "Project configuration is missing '$field'."
        }
    }
    $expectedStorage = [ordered]@{
        root = 'build/gamewip'
        cache = 'build/gamewip/cache'
        state = 'build/gamewip/state'
        temp = 'build/gamewip/temp'
        runs = 'build/gamewip/runs'
    }
    foreach ($entry in $expectedStorage.GetEnumerator())
    {
        if ([string]$ProjectConfig.storage[$entry.Key] -ne $entry.Value)
        {
            throw "Project storage '$($entry.Key)' must be '$($entry.Value)'."
        }
    }
    if ([string]$ProjectConfig.managedEnvironment.msys2Root -ne 'C:\MSYS2' -or
        [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot -ne 'C:\MSYS2\GameWIPTools')
    {
        throw 'Project managed-environment roots do not match the repository contract.'
    }
}

function Assert-GameWipProjectToolConfig
{
    Assert-GameWipUniqueId -Items @($ProjectTools.tools) -Label 'project tool'
    $providerKinds = @('msys2', 'npm', 'python', 'powershellGallery', 'githubRelease', 'winget', 'gitSubmodule', 'external')
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        if ($providerKinds -notcontains $toolInfo.provider.kind) { throw "Tool '$($toolInfo.id)' uses unsupported provider '$($toolInfo.provider.kind)'." }
        if ($toolInfo.versionPolicy -in @('exact', 'minimum') -and -not $toolInfo.Contains('requiredVersion')) { throw "Tool '$($toolInfo.id)' requires a declared version." }
        if ($toolInfo.capabilities.update -and -not $toolInfo.capabilities.detectInstalled) { throw "Updatable tool '$($toolInfo.id)' must support installed-version detection." }

        $providerDependencies = if ($toolInfo.provider.Contains('dependencies')) { @($toolInfo.provider.dependencies) } else { @() }
        if ($toolInfo.provider.kind -eq 'msys2')
        {
            if (-not $toolInfo.provider.Contains('environment') -or [string]$toolInfo.provider.environment -notin @('common', 'ucrt64', 'clang64')) { throw "MSYS2 tool '$($toolInfo.id)' must declare its managed environment." }
            if (-not $toolInfo.provider.Contains('package')) { throw "MSYS2 tool '$($toolInfo.id)' must declare its pacman package." }
            foreach ($dependency in $providerDependencies)
            {
                if (-not $dependency.Contains('environment')) { throw "MSYS2 dependency '$($dependency.package)' for '$($toolInfo.id)' must declare its environment." }
            }
        }
        elseif ($toolInfo.provider.kind -eq 'npm')
        {
            foreach ($dependency in $providerDependencies)
            {
                if (-not $dependency.Contains('version')) { throw "npm dependency '$($dependency.package)' for '$($toolInfo.id)' must be versioned." }
            }
        }
        elseif ($toolInfo.provider.kind -eq 'githubRelease' -and -not $toolInfo.provider.Contains('releaseTag'))
        {
            throw "GitHub release tool '$($toolInfo.id)' must retain its actual upstream release tag."
        }

        foreach ($reference in @($toolInfo.references))
        {
            if ($reference -isnot [hashtable] -or -not $reference.Contains('path') -or -not $reference.Contains('kind')) { throw "Tool '$($toolInfo.id)' has malformed live-reference metadata." }
            if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot ([string]$reference.path)))) { throw "Tool '$($toolInfo.id)' references missing live path '$($reference.path)'." }
            if ($reference.kind -eq 'text' -and (-not $reference.Contains('pattern') -or -not ([string]$reference.pattern).Contains('{version}'))) { throw "Tool '$($toolInfo.id)' text reference '$($reference.path)' must contain {version}." }
        }
    }
}

function Get-GameWipVisiblePresetName
{
    param([Parameter(Mandatory = $true)][string]$Kind)

    $property = switch ($Kind)
    {
        'configure'
        {
            'configurePresets'
        }
        'build'
        {
            'buildPresets'
        }
        'test'
        {
            'testPresets'
        }
    }

    @($PresetData.$property | Where-Object { -not ($_.PSObject.Properties.Name -contains 'hidden' -and $_.hidden) } | ForEach-Object { $_.name })
}

function Assert-GameWipValidPreset
{
    param(
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ((Get-GameWipVisiblePresetName -Kind $Kind) -notcontains $Name)
    {
        throw "Unknown $Kind preset '$Name'. Run 'gamewip list' to see available presets."
    }
}

function Assert-GameWipValidModule
{
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($Name -ne 'all' -and @($CommandConfig.Modules) -notcontains $Name)
    {
        throw "Unknown validation module '$Name'. Run 'gamewip list' to see available modules."
    }
}

function Get-GameWipProjectCommand
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $command = @($CommandConfig.ProjectCommands | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($command.Count -eq 0)
    {
        throw "Unknown project command '$Id'. Run 'gamewip list' to see available commands."
    }
    $command[0]
}

function Get-GameWipProjectBundle
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $bundleInfo = @($CommandConfig.Bundles | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($bundleInfo.Count -eq 0)
    {
        throw "Unknown bundle '$Id'. Run 'gamewip list' to see available bundles."
    }
    $bundleInfo[0]
}

function Assert-GameWipUniqueId
{
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][object[]]$Items
    )

    $duplicates = @(
        $Items |
            ForEach-Object { [string]$_.Id } |
            Group-Object |
            Where-Object { $_.Count -gt 1 } |
            ForEach-Object { $_.Name }
    )
    if ($duplicates.Count -ne 0)
    {
        throw "Duplicate $Label IDs: $($duplicates -join ', ')."
    }
}

function Assert-GameWipBundleAcyclic
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)]$Lookup,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.HashSet[string]]$Visiting,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.HashSet[string]]$Visited
    )

    if ($Visited.Contains($Id))
    {
        return
    }
    if (-not $Visiting.Add($Id))
    {
        throw "Bundle cycle detected at '$Id'."
    }
    foreach ($step in @($Lookup[$Id].Steps | Where-Object { $_.Kind -eq 'Bundle' }))
    {
        Assert-GameWipBundleAcyclic -Id ([string]$step.Bundle) -Lookup $Lookup -Visiting $Visiting -Visited $Visited
    }
    $Visiting.Remove($Id) | Out-Null
    $Visited.Add($Id) | Out-Null
}

function Assert-GameWipCommandConfig
{
    $configurePresets = @(Get-GameWipVisiblePresetName -Kind 'configure')
    $buildPresets = @(Get-GameWipVisiblePresetName -Kind 'build')
    $testPresets = @(Get-GameWipVisiblePresetName -Kind 'test')
    $commands = @($CommandConfig.ProjectCommands)
    $bundles = @($CommandConfig.Bundles)
    $workflows = @($CommandConfig.ManualWorkflows)
    $benchmarkProfiles = @($CommandConfig.BenchmarkProfiles)

    Assert-GameWipUniqueId -Label 'project command' -Items $commands
    Assert-GameWipUniqueId -Label 'bundle' -Items $bundles
    Assert-GameWipUniqueId -Label 'manual workflow' -Items $workflows
    Assert-GameWipUniqueId -Label 'benchmark profile' -Items $benchmarkProfiles

    foreach ($default in @(
            @{ Label = 'configure'; Value = $CommandConfig.DefaultConfigurePreset; Values = $configurePresets },
            @{ Label = 'build'; Value = $CommandConfig.DefaultBuildPreset; Values = $buildPresets },
            @{ Label = 'test'; Value = $CommandConfig.DefaultTestPreset; Values = $testPresets }
        ))
    {
        if ($default.Values -notcontains $default.Value)
        {
            throw "Unknown default $($default.Label) preset '$($default.Value)' in the project command catalog."
        }
    }

    $moduleRoot = Join-Path $RepositoryRoot 'game\validation\tests'
    $discoveredModules = @(
        Get-ChildItem -LiteralPath $moduleRoot -Directory |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'CMakeLists.txt') } |
            ForEach-Object { $_.Name } |
            Sort-Object
    )
    $configuredModules = @($CommandConfig.Modules | Sort-Object)
    if (($discoveredModules -join "`n") -ne ($configuredModules -join "`n"))
    {
        throw "Validation module catalog drift. Configured: $($configuredModules -join ', '); discovered: $($discoveredModules -join ', ')."
    }
    if ($configuredModules -notcontains $CommandConfig.DefaultModule -and $CommandConfig.DefaultModule -ne 'all')
    {
        throw "Unknown default validation module '$($CommandConfig.DefaultModule)'."
    }

    foreach ($command in $commands)
    {
        foreach ($field in @('Id', 'Name', 'BuildPreset', 'Executable', 'Arguments', 'UseWorkspaceTemp', 'AcceptsExtraArgs'))
        {
            if (-not $command.ContainsKey($field))
            {
                throw "Project command '$($command.Id)' is missing '$field'."
            }
        }
        if ($buildPresets -notcontains $command.BuildPreset)
        {
            throw "Project command '$($command.Id)' references unknown build preset '$($command.BuildPreset)'."
        }
    }

    $commandIds = @($commands | ForEach-Object { $_.Id })
    $bundleIds = @($bundles | ForEach-Object { $_.Id })
    $validBundleKinds = @('Configure', 'Build', 'BuildTarget', 'CTest', 'ProjectCommand', 'Benchmark', 'Bundle')
    foreach ($bundle in $bundles)
    {
        if (-not $bundle.ContainsKey('Steps') -or @($bundle.Steps).Count -eq 0)
        {
            throw "Bundle '$($bundle.Id)' must contain at least one step."
        }
        foreach ($step in $bundle.Steps)
        {
            if ($validBundleKinds -notcontains $step.Kind)
            {
                throw "Unknown bundle step kind '$($step.Kind)' in bundle '$($bundle.Id)'."
            }
            if ($step.Kind -eq 'Configure' -and $configurePresets -notcontains $step.Preset)
            {
                throw "Bundle '$($bundle.Id)' references unknown configure preset '$($step.Preset)'."
            }
            if ($step.Kind -in @('Build', 'BuildTarget') -and $buildPresets -notcontains $step.Preset)
            {
                throw "Bundle '$($bundle.Id)' references unknown build preset '$($step.Preset)'."
            }
            if ($step.Kind -eq 'CTest' -and $testPresets -notcontains $step.Preset)
            {
                throw "Bundle '$($bundle.Id)' references unknown test preset '$($step.Preset)'."
            }
            if ($step.Kind -eq 'ProjectCommand' -and $commandIds -notcontains $step.Command)
            {
                throw "Bundle '$($bundle.Id)' references unknown project command '$($step.Command)'."
            }
            if ($step.Kind -eq 'Benchmark' -and $step.ContainsKey('Profile') -and @($benchmarkProfiles | ForEach-Object { $_.Id }) -notcontains $step.Profile)
            {
                throw "Bundle '$($bundle.Id)' references unknown benchmark profile '$($step.Profile)'."
            }
            if ($step.Kind -eq 'Bundle' -and $bundleIds -notcontains $step.Bundle)
            {
                throw "Bundle '$($bundle.Id)' references unknown bundle '$($step.Bundle)'."
            }
        }
    }
    $bundleLookup = @{}
    foreach ($bundle in $bundles)
    {
        $bundleLookup[$bundle.Id] = $bundle
    }
    $visiting = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $visited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($bundleId in $bundleIds)
    {
        Assert-GameWipBundleAcyclic -Id $bundleId -Lookup $bundleLookup -Visiting $visiting -Visited $visited
    }

    foreach ($benchmarkProfile in $benchmarkProfiles)
    {
        foreach ($field in @('Id', 'Name', 'Repetitions', 'MinTime', 'AggregatesOnly'))
        {
            if (-not $benchmarkProfile.ContainsKey($field))
            {
                throw "Benchmark profile '$($benchmarkProfile.Id)' is missing '$field'."
            }
        }
        if ([int]$benchmarkProfile.Repetitions -lt 1)
        {
            throw "Benchmark profile '$($benchmarkProfile.Id)' must use at least one repetition."
        }
        if ([string]$benchmarkProfile.MinTime -notmatch '^(?:[0-9]+x|[0-9]+(?:\.[0-9]+)?s)$')
        {
            throw "Benchmark profile '$($benchmarkProfile.Id)' has invalid MinTime '$($benchmarkProfile.MinTime)'."
        }
    }

    foreach ($workflow in $workflows)
    {
        $workflowPath = Join-Path $RepositoryRoot (Join-Path '.github\workflows' $workflow.File)
        if (-not (Test-Path -LiteralPath $workflowPath))
        {
            throw "Manual workflow '$($workflow.Id)' references missing file '$($workflow.File)'."
        }
    }
}
