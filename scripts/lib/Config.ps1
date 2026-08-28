# Shared GameWIP configuration and repository-path helpers.
# Bootstrap validation is intentionally narrower than JSON Schema validation.

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Configuration loading
# ------------------------------------------------------------

function Resolve-GameWipRepositoryPath
{
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path))
    {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $Path))
}

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
        $parsed = Read-GameWipUtf8Text -Path $Path | ConvertFrom-Json
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
        throw "Unsupported $Name configuration schemaVersion '$($config.schemaVersion)'; expected 1."
    }

    # Bootstrap validation deliberately does not implement JSON Schema. The
    # conforming Draft 2020-12 validator is .github/scripts/validate_config_schemas.py.
    if (-not [string]::IsNullOrWhiteSpace($SchemaPath))
    {
        if (-not (Test-Path -LiteralPath $SchemaPath -PathType Leaf))
        {
            throw "Required $Name schema is missing: $SchemaPath"
        }
        try
        {
            $schemaHeader = Read-GameWipUtf8Text -Path $SchemaPath | ConvertFrom-Json
        }
        catch
        {
            throw "Malformed $Name schema '$SchemaPath': $($_.Exception.Message)"
        }
        if ([string]$schemaHeader.'$schema' -ne 'https://json-schema.org/draft/2020-12/schema')
        {
            throw "$Name schema '$SchemaPath' must declare JSON Schema draft 2020-12."
        }
    }
    return $config
}

# ------------------------------------------------------------
# Configuration validation
# ------------------------------------------------------------

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
    if ([string]$ProjectConfig.managedEnvironment.msys2Root -ne 'C:\MSYS2' -or [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot -ne 'C:\MSYS2\GameWIPTools')
    {
        throw 'Project managed-environment roots do not match the repository contract.'
    }
}

function Resolve-GameWipCommandConfigValue
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $value = $CommandConfig
    foreach ($segment in ($Path -split '\.'))
    {
        if ($value -isnot [System.Collections.IDictionary] -or -not $value.Contains($segment))
        {
            throw "Command configuration value '$Path' does not exist."
        }
        $value = $value[$segment]
    }
    return $value
}

function Assert-GameWipProjectToolConfig
{
    Assert-GameWipUniqueId -Items @($ProjectTools.tools) -Label 'project tool'
    $providerKinds = @('msys2', 'npm', 'python', 'powershellGallery', 'githubRelease', 'winget', 'gitSubmodule', 'external')
    $liveReferences = @{}
    $rootPath = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $pathComparison = if (Test-GameWipWindowsHost)
    {
        [StringComparison]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparison]::Ordinal
    }
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        if ($providerKinds -notcontains $toolInfo.provider.kind)
        {
            throw "Tool '$($toolInfo.id)' uses unsupported provider '$($toolInfo.provider.kind)'."
        }
        if ($toolInfo.versionPolicy -in @('exact', 'minimum') -and -not $toolInfo.Contains('requiredVersion'))
        {
            throw "Tool '$($toolInfo.id)' requires a declared version."
        }
        if ($toolInfo.capabilities.update -and -not $toolInfo.capabilities.detectInstalled)
        {
            throw "Updatable tool '$($toolInfo.id)' must support installed-version detection."
        }
        if ($toolInfo.detection.Contains('commandConfigVersionPath'))
        {
            $configVersion = [string](Resolve-GameWipCommandConfigValue -Path ([string]$toolInfo.detection.commandConfigVersionPath))
            if ([string]::IsNullOrWhiteSpace($configVersion))
            {
                throw "Tool '$($toolInfo.id)' references an empty command configuration version."
            }
        }

        $providerDependencies = if ($toolInfo.provider.Contains('dependencies'))
        {
            @($toolInfo.provider.dependencies)
        }
        else
        {
            @()
        }
        switch ([string]$toolInfo.provider.kind)
        {
            'msys2'
            {
                if (-not $toolInfo.provider.Contains('environment') -or [string]$toolInfo.provider.environment -notin @('common', 'ucrt64', 'clang64'))
                {
                    throw "MSYS2 tool '$($toolInfo.id)' must declare its managed environment."
                }
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "MSYS2 tool '$($toolInfo.id)' must declare its pacman package."
                }
                foreach ($dependency in $providerDependencies)
                {
                    if (-not $dependency.Contains('environment'))
                    {
                        throw "MSYS2 dependency '$($dependency.package)' for '$($toolInfo.id)' must declare its environment."
                    }
                }
            }
            'npm'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "npm tool '$($toolInfo.id)' must declare its package."
                }
                foreach ($dependency in $providerDependencies)
                {
                    if (-not $dependency.Contains('version'))
                    {
                        throw "npm dependency '$($dependency.package)' for '$($toolInfo.id)' must be versioned."
                    }
                }
            }
            'python'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "Python tool '$($toolInfo.id)' must declare its package."
                }
            }
            'powershellGallery'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "PowerShell Gallery tool '$($toolInfo.id)' must declare its package."
                }
            }
            'githubRelease'
            {
                foreach ($field in @('repository', 'releaseTag', 'assets'))
                {
                    if (-not $toolInfo.provider.Contains($field))
                    {
                        throw "GitHub release tool '$($toolInfo.id)' must declare '$field'."
                    }
                }
            }
            'winget'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "WinGet tool '$($toolInfo.id)' must declare its package."
                }
            }
        }

        foreach ($reference in @($toolInfo.references))
        {
            if ($reference -isnot [hashtable] -or -not $reference.Contains('path') -or -not $reference.Contains('kind'))
            {
                throw "Tool '$($toolInfo.id)' has malformed reference metadata."
            }
            $referencePath = Resolve-GameWipRepositoryPath -Path ([string]$reference.path)
            if (-not $referencePath.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, $pathComparison))
            {
                throw "Tool '$($toolInfo.id)' reference path '$($reference.path)' resolves outside the repository."
            }
            if (-not (Test-Path -LiteralPath $referencePath -PathType Leaf))
            {
                throw "Tool '$($toolInfo.id)' references missing path '$($reference.path)'."
            }

            $expectedCount = if ($reference.Contains('expectedCount'))
            {
                [int]$reference.expectedCount
            }
            else
            {
                1
            }
            switch ([string]$reference.kind)
            {
                'text'
                {
                    if (-not $reference.Contains('pattern'))
                    {
                        throw "Tool '$($toolInfo.id)' text reference '$($reference.path)' must declare a literal pattern."
                    }
                    $tokenCount = [regex]::Matches([string]$reference.pattern, [regex]::Escape('{version}')).Count
                    if ($tokenCount -ne 1)
                    {
                        throw "Tool '$($toolInfo.id)' text reference '$($reference.path)' pattern must contain exactly one literal {version} token."
                    }
                }
                'cmakeMinimum'
                {
                    if ($reference.Contains('pattern'))
                    {
                        throw "Tool '$($toolInfo.id)' CMake minimum reference '$($reference.path)' must not declare pattern."
                    }
                }
                'path'
                {
                    if ($reference.Contains('pattern') -or $reference.Contains('expectedCount'))
                    {
                        throw "Tool '$($toolInfo.id)' informational path reference '$($reference.path)' must not declare pattern or expectedCount."
                    }
                    continue
                }
                default
                {
                    throw "Tool '$($toolInfo.id)' uses unknown reference kind '$($reference.kind)'."
                }
            }
            if ($expectedCount -lt 1)
            {
                throw "Tool '$($toolInfo.id)' live reference '$($reference.path)' expectedCount must be at least 1."
            }
            $referenceKey = @(
                [string]$reference.kind,
                $referencePath,
                $(if ($reference.Contains('pattern'))
                    {
                        [string]$reference.pattern
                    }
                    else
                    {
                        ''
                    }),
                [string]$expectedCount
            ) -join "`n"
            if ($liveReferences.ContainsKey($referenceKey))
            {
                throw "Tool '$($toolInfo.id)' duplicates live reference '$($reference.path)'."
            }
            $liveReferences[$referenceKey] = $true
        }
    }
}

# ------------------------------------------------------------
# Commands and bundles
# ------------------------------------------------------------

function Get-GameWipVisiblePresetName
{
    param([Parameter(Mandatory = $true)][string]$Kind)
    $property = switch ($Kind)
    {
        'configure'
        {
            'configurePresets'
        } 'build'
        {
            'buildPresets'
        } 'test'
        {
            'testPresets'
        } default
        {
            throw "Unknown preset kind '$Kind'."
        }
    }
    return @($PresetData.$property | Where-Object { -not ($_.PSObject.Properties.Name -contains 'hidden' -and $_.hidden) } | ForEach-Object { $_.name })
}

function Assert-GameWipValidPreset
{
    param([Parameter(Mandatory = $true)][string]$Kind, [Parameter(Mandatory = $true)][string]$Name)
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
    return $command[0]
}

function Get-GameWipProjectBundle
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $bundleInfo = @($CommandConfig.Bundles | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($bundleInfo.Count -eq 0)
    {
        throw "Unknown bundle '$Id'. Run 'gamewip list' to see available bundles."
    }
    return $bundleInfo[0]
}

function Assert-GameWipUniqueId
{
    param([Parameter(Mandatory = $true)][string]$Label, [Parameter(Mandatory = $true)][object[]]$Items)
    $duplicates = @($Items | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
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
    $actions = @($CommandConfig.Actions)
    $menus = @($CommandConfig.Menus)
    $commands = @($CommandConfig.ProjectCommands)
    $bundles = @($CommandConfig.Bundles)
    $workflows = @($CommandConfig.ManualWorkflows)
    $benchmarkProfiles = @($CommandConfig.BenchmarkProfiles)

    Assert-GameWipUniqueId -Label 'action' -Items $actions
    Assert-GameWipUniqueId -Label 'interactive menu' -Items $menus
    Assert-GameWipUniqueId -Label 'project command' -Items $commands
    $requiredActions = @('menu', 'doctor', 'git', 'workflow', 'unicode', 'format', 'quality', 'tools', 'links', 'configure', 'build', 'test', 'module', 'wizard', 'stress', 'run', 'bundle', 'docs', 'analyze', 'coverage', 'asan', 'benchmark', 'runs', 'list', 'help')
    $actionIds = @($actions | ForEach-Object { [string]$_.Id })
    if ((($requiredActions | Sort-Object) -join "`n") -cne (($actionIds | Sort-Object) -join "`n"))
    {
        throw "Project action catalog drift. Required: $($requiredActions -join ', '); configured: $($actionIds -join ', ')."
    }
    if ($actionIds -contains 'analysis')
    {
        throw "Retired action alias 'analysis' must not be registered."
    }
    $requiredMenus = @('root', 'development', 'validation', 'quality', 'tools', 'repository', 'maintenance')
    $menuIds = @($menus | ForEach-Object { [string]$_.Id })
    if ((($requiredMenus | Sort-Object) -join "`n") -cne (($menuIds | Sort-Object) -join "`n"))
    {
        throw "Interactive menu catalog drift. Required: $($requiredMenus -join ', '); configured: $($menuIds -join ', ')."
    }
    $supportedMenuHandlers = @(
        'menu-development', 'menu-validation', 'menu-quality', 'menu-tools', 'menu-repository', 'menu-maintenance',
        'doctor', 'help', 'configure', 'build', 'run', 'docs', 'test', 'module', 'stress', 'wizard', 'benchmark',
        'coverage', 'asan', 'quality-check', 'quality-fix', 'format-check', 'format-apply', 'analyze', 'tools-status',
        'tools-check-updates', 'tools-preview', 'tools-update', 'setup-guidance', 'git-status', 'git-fetch',
        'git-switch', 'git-update', 'git-cleanup', 'git-log', 'workflow-list', 'workflow-run', 'unicode-status',
        'unicode-verify', 'unicode-regenerate', 'bundle', 'links'
    )
    foreach ($menu in $menus)
    {
        $duplicateMenuKeys = @($menu.Items | ForEach-Object { ([string]$_.Key).ToUpperInvariant() } | Group-Object | Where-Object Count -gt 1)
        if ($duplicateMenuKeys.Count -ne 0)
        {
            throw "Interactive menu '$($menu.Id)' has duplicate keys: $($duplicateMenuKeys.Name -join ', ')."
        }
        foreach ($item in $menu.Items)
        {
            if ($supportedMenuHandlers -notcontains [string]$item.Handler)
            {
                throw "Interactive menu '$($menu.Id)' references unsupported handler '$($item.Handler)'."
            }
        }
    }
    $configuredMenuHandlers = @($menus.Items | ForEach-Object { [string]$_.Handler } | Sort-Object -Unique)
    if ((($supportedMenuHandlers | Sort-Object) -join "`n") -cne (($configuredMenuHandlers | Sort-Object) -join "`n"))
    {
        throw "Interactive menu handler catalog drift. Supported: $($supportedMenuHandlers -join ', '); configured: $($configuredMenuHandlers -join ', ')."
    }
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
    $discoveredModules = @(Get-ChildItem -LiteralPath $moduleRoot -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'CMakeLists.txt') } | ForEach-Object { $_.Name } | Sort-Object)
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
