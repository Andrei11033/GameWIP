# GameWIP Bundles helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Invoke-GameWipBundle
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [switch]$NoBuild,
        [switch]$Fresh,
        [System.Collections.Generic.HashSet[string]]$FreshenedPresets
    )

    $bundleInfo = Get-GameWipProjectBundle -Id $Id
    $freshByDefault = $bundleInfo.ContainsKey('FreshBuildTrees') -and [bool]$bundleInfo.FreshBuildTrees
    $effectiveFresh = [bool]$Fresh -or $freshByDefault
    if ($NoBuild -and $effectiveFresh)
    {
        throw "Bundle '$Id' cannot combine clean build-tree recreation with -NoBuild."
    }
    if ($null -eq $FreshenedPresets)
    {
        $FreshenedPresets = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    }

    if ($effectiveFresh)
    {
        foreach ($step in $bundleInfo.Steps)
        {
            $preset = switch ($step.Kind)
            {
                { $_ -in @('Configure', 'Build', 'BuildTarget', 'CTest') }
                {
                    [string]$step.Preset; break
                }
                'ProjectCommand'
                {
                    $buildIfMissing = -not $step.ContainsKey('BuildIfMissing') -or [bool]$step.BuildIfMissing
                    if ($buildIfMissing)
                    {
                        [string](Get-GameWipProjectCommand -Id $step.Command).BuildPreset
                    }
                    break
                }
                'Benchmark'
                {
                    [string](Get-GameWipProjectCommand -Id 'benchmark-dry-run').BuildPreset; break
                }
                default
                {
                    ''
                }
            }
            if (-not [string]::IsNullOrWhiteSpace($preset) -and $FreshenedPresets.Add($preset))
            {
                Reset-GameWipPresetBuildTree -Name $preset
            }
        }
    }

    Write-GameWipSection $bundleInfo.Name
    foreach ($step in $bundleInfo.Steps)
    {
        switch ($step.Kind)
        {
            'Configure'
            {
                Invoke-GameWipConfigurePreset -Name $step.Preset
            }
            'Build'
            {
                Invoke-GameWipBuildPreset -Name $step.Preset
            }
            'BuildTarget'
            {
                Invoke-GameWipBuildTarget -Name $step.Preset -Target $step.Target
            }
            'CTest'
            {
                Invoke-GameWipTestPreset `
                    -Name $step.Preset `
                    -UseWorkspaceTemp:([bool]$step.UseWorkspaceTemp) `
                    -NoBuild:$NoBuild
            }
            'ProjectCommand'
            {
                $buildIfMissing = if ($step.ContainsKey('BuildIfMissing'))
                {
                    [bool]$step.BuildIfMissing
                }
                else
                {
                    $true
                }

                $projectCommandParameters = @{
                    Id = $step.Command
                    NoBuild = ($NoBuild -or -not $buildIfMissing)
                }
                if ($step.ContainsKey('Arguments'))
                {
                    $projectCommandParameters.Arguments = @($step.Arguments)
                }
                Invoke-GameWipProjectCommand @projectCommandParameters
            }
            'Benchmark'
            {
                $stepProfile = if ($step.ContainsKey('Profile'))
                {
                    [string]$step.Profile
                }
                else
                {
                    'standard'
                }
                $stepFilter = if ($step.ContainsKey('Filter'))
                {
                    [string]$step.Filter
                }
                else
                {
                    ''
                }
                Invoke-GameWipBenchmark `
                    -Mode 'run' `
                    -ProfileId $stepProfile `
                    -NameFilter $stepFilter `
                    -RepeatCount 0 `
                    -MinimumTime '' `
                    -RequestedOutput '' `
                    -Format 'json' `
                    -SkipBuild:$NoBuild
            }
            'Bundle'
            {
                Invoke-GameWipBundle -Id $step.Bundle -NoBuild:$NoBuild -Fresh:$effectiveFresh -FreshenedPresets $FreshenedPresets
            }
            default
            {
                throw "Unknown bundle step kind '$($step.Kind)' in bundle '$Id'."
            }
        }
    }
}
