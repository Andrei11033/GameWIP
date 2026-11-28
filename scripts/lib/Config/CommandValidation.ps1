# Validate the project command, menu, bundle, and workflow catalogs.

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Command catalog validation
# ------------------------------------------------------------

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

    # Validate action identifiers and the required action catalog.
    Assert-GameWipUniqueId -Label 'action' -Items $actions
    Assert-GameWipUniqueId -Label 'interactive menu' -Items $menus
    Assert-GameWipUniqueId -Label 'project command' -Items $commands
    Assert-GameWipOptionCatalog
    $requiredActions = @(
        'menu'
        'ready'
        'git'
        'workflow'
        'unicode'
        'format'
        'quality'
        'tool'
        'links'
        'deps'
        'target'
        'config'
        'build'
        'test'
        'module'
        'wizard'
        'stress'
        'run'
        'runtime'
        'bundle'
        'doc'
        'analyze'
        'cov'
        'asan'
        'ubsan'
        'bench'
        'history'
        'list'
        'help'
    )
    $actionIds = @($actions | ForEach-Object { [string]$_.Id })

    if ((($requiredActions | Sort-Object) -join "`n") -cne (($actionIds | Sort-Object) -join "`n"))
    {
        $missingActions = @($requiredActions | Where-Object { $actionIds -cnotcontains $_ })
        $unexpectedActions = @($actionIds | Where-Object { $requiredActions -cnotcontains $_ })
        $differences = @()

        if ($missingActions.Count -gt 0)
        {
            $differences += "missing required actions: $($missingActions -join ', ')"
        }

        if ($unexpectedActions.Count -gt 0)
        {
            $differences += "unexpected configured actions: $($unexpectedActions -join ', ')"
        }

        throw "Project action catalog drift: $($differences -join '; ')."
    }

    $actionNames = @{}

    foreach ($action in $actions)
    {
        $canonical = [string]$action.Id
        $canonicalKey = $canonical.ToLowerInvariant()

        if ($actionNames.ContainsKey($canonicalKey))
        {
            throw "Project action name '$canonical' is duplicated."
        }

        $actionNames[$canonicalKey] = $canonical
    }

    # Validate menu identifiers, keys, and supported handlers.
    $requiredMenus = @(
        'root', 'development', 'validation', 'quality', 'hygiene', 'tools', 'tool-updates',
        'git-workspace', 'github-workflows', 'advanced', 'advanced-validation', 'dependencies',
        'unicode-data', 'run-history'
    )
    $menuIds = @($menus | ForEach-Object { [string]$_.Id })

    if ((($requiredMenus | Sort-Object) -join "`n") -cne (($menuIds | Sort-Object) -join "`n"))
    {
        $missingMenus = @($requiredMenus | Where-Object { $menuIds -cnotcontains $_ })
        $unexpectedMenus = @($menuIds | Where-Object { $requiredMenus -cnotcontains $_ })
        $differences = @()

        if ($missingMenus.Count -gt 0)
        {
            $differences += "missing required menus: $($missingMenus -join ', ')"
        }

        if ($unexpectedMenus.Count -gt 0)
        {
            $differences += "unexpected configured menus: $($unexpectedMenus -join ', ')"
        }

        throw "Interactive menu catalog drift: $($differences -join '; ')."
    }

    $supportedMenuHandlers = @(
        'menu-development', 'menu-validation', 'menu-quality', 'menu-hygiene', 'menu-tools', 'menu-tool-updates',
        'menu-git-workspace', 'menu-github-workflows', 'menu-advanced', 'menu-advanced-validation',
        'menu-dependencies', 'menu-unicode-data', 'menu-run-history', 'deps-check', 'deps-prepare',
        'ready', 'help', 'target', 'config', 'build', 'run', 'doc', 'test', 'module', 'stress', 'wizard', 'bench',
        'cov', 'asan', 'ubsan', 'bundle', 'quality-check', 'quality-fix', 'quality-status', 'format-check', 'format-apply',
        'analyze', 'links', 'hygiene-standard', 'hygiene-deep', 'hygiene-check', 'hygiene-list', 'hygiene-status', 'tool-status',
        'tool-plan', 'tool-preview', 'tool-update', 'setup-guidance', 'git-status', 'git-fetch',
        'git-switch', 'git-create', 'git-update', 'git-push', 'git-cleanup', 'git-log', 'workflow-list',
        'workflow-status', 'workflow-run', 'unicode-status', 'unicode-verify', 'unicode-regenerate',
        'history-list', 'history-show', 'history-clean'
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
        $missingHandlers = @($supportedMenuHandlers | Where-Object { $configuredMenuHandlers -cnotcontains $_ })
        $unsupportedHandlers = @($configuredMenuHandlers | Where-Object { $supportedMenuHandlers -cnotcontains $_ })
        $differences = @()

        if ($missingHandlers.Count -gt 0)
        {
            $differences += "missing supported handlers: $($missingHandlers -join ', ')"
        }

        if ($unsupportedHandlers.Count -gt 0)
        {
            $differences += "unsupported configured handlers: $($unsupportedHandlers -join ', ')"
        }

        throw "Interactive menu handler catalog drift: $($differences -join '; ')."
    }

    # Validate remaining catalog identifiers.
    Assert-GameWipUniqueId -Label 'bundle' -Items $bundles
    Assert-GameWipUniqueId -Label 'manual workflow' -Items $workflows
    Assert-GameWipUniqueId -Label 'benchmark profile' -Items $benchmarkProfiles

    # Validate default presets and discovered validation modules.
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

    $discoveredModules = @(Get-GameWipRegisteredValidationModuleName)
    $discoveredModuleEntries = @($discoveredModules | ForEach-Object { [pscustomobject]@{ Id = $_ } })
    Assert-GameWipUniqueId -Label 'registered validation module' -Items $discoveredModuleEntries
    Assert-GameWipUniqueId -Label 'validation module' -Items @($CommandConfig.Modules)
    $configuredModules = @(Get-GameWipValidationModuleName | Sort-Object)

    if (($discoveredModules -join "`n") -ne ($configuredModules -join "`n"))
    {
        $missingModules = @($discoveredModules | Where-Object { $configuredModules -cnotcontains $_ })
        $staleModules = @($configuredModules | Where-Object { $discoveredModules -cnotcontains $_ })
        $differences = @()

        if ($missingModules.Count -gt 0)
        {
            $differences += "missing from configuration: $($missingModules -join ', ')"
        }

        if ($staleModules.Count -gt 0)
        {
            $differences += "no longer discovered: $($staleModules -join ', ')"
        }

        throw "Validation module catalog drift: $($differences -join '; ')."
    }

    if ($configuredModules -notcontains $CommandConfig.DefaultModule -and $CommandConfig.DefaultModule -ne 'all')
    {
        throw "Unknown default validation module '$($CommandConfig.DefaultModule)'."
    }

    $validCommonCapabilities = @('manual-tests', 'verbose-tests', 'test-support-child-process', 'report')

    foreach ($module in @($CommandConfig.Modules))
    {
        $options = @(if ($module -is [hashtable] -and $module.ContainsKey('builderOptions'))
            {
                @($module['builderOptions'])
            }
            else
            {
                @()
            })

        if ($options.Count -eq 0)
        {
            continue
        }

        Assert-GameWipCommandBuilderOptionSet `
            -Label "validation '$($module.Id)'" `
            -Options $options `
            -ValidCommonCapabilities $validCommonCapabilities
    }

    Assert-GameWipCommandBuilderOptionSet `
        -Label 'runtime' `
        -Options @($CommandConfig.RuntimeBuilderOptions) `
        -ValidPlatforms @('windows', 'linux')

    # Validate project commands and bundle steps.
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

        if ($command.ContainsKey('WorkingDirectory') -and $command.WorkingDirectory -notin @('repository', 'build'))
        {
            throw "Project command '$($command.Id)' has an unsupported working directory '$($command.WorkingDirectory)'."
        }

        foreach ($field in @('InheritConsole', 'TargetRuntimePath', 'StartTracyProfiler', 'RuntimeOptions'))
        {
            if ($command.ContainsKey($field) -and $command[$field] -isnot [bool])
            {
                throw "Project command '$($command.Id)' must set '$field' to a Boolean value."
            }
        }
    }

    $runtimeCommands = @($commands | Where-Object { $_.ContainsKey('RuntimeOptions') -and [bool]$_.RuntimeOptions })

    if ($runtimeCommands.Count -eq 0)
    {
        throw 'The runtime command builder requires at least one project command with RuntimeOptions enabled.'
    }

    if (@($runtimeCommands | Where-Object { -not [bool]$_.AcceptsExtraArgs }).Count -gt 0)
    {
        throw 'Every project command with RuntimeOptions enabled must accept extra arguments.'
    }

    if (@($runtimeCommands | ForEach-Object { [string]$_.Id }) -notcontains [string]$CommandConfig.DefaultRuntimeCommand)
    {
        throw "Default runtime command '$($CommandConfig.DefaultRuntimeCommand)' is not enabled for runtime options."
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

    # Reject recursive bundle definitions.
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

    # Validate benchmark profiles.
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

    # Validate workflow paths.
    foreach ($workflow in $workflows)
    {
        $workflowPath = Join-Path $RepositoryRoot (Join-Path '.github\workflows' $workflow.File)

        if (-not (Test-Path -LiteralPath $workflowPath))
        {
            throw "Manual workflow '$($workflow.Id)' references missing file '$($workflow.File)'."
        }
    }
}
