# GameWIP Bundles helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Invoke-GameWipBundle
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [switch]$NoBuild
    )

    $bundleInfo = Get-GameWipProjectBundle -Id $Id
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
                $stepArguments = if ($step.ContainsKey('Arguments'))
                {
                    @($step.Arguments)
                }
                else
                {
                    @()
                }

                $buildIfMissing = if ($step.ContainsKey('BuildIfMissing'))
                {
                    [bool]$step.BuildIfMissing
                }
                else
                {
                    $true
                }

                Invoke-GameWipProjectCommand `
                    -Id $step.Command `
                    -Arguments $stepArguments `
                    -NoBuild:($NoBuild -or -not $buildIfMissing)
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
                Invoke-GameWipBundle -Id $step.Bundle -NoBuild:$NoBuild
            }
            default
            {
                throw "Unknown bundle step kind '$($step.Kind)' in bundle '$Id'."
            }
        }
    }
}
