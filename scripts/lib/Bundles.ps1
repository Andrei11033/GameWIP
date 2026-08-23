# GameWIP Bundles helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Invoke-GameWipBundle
{
    param([Parameter(Mandatory = $true)][string]$Id)

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
                Invoke-GameWipTestPreset -Name $step.Preset -UseWorkspaceTemp:([bool]$step.UseWorkspaceTemp)
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
                Invoke-GameWipProjectCommand -Id $step.Command -Arguments $stepArguments -ForceBuild:([bool]$step.BuildIfMissing)
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
                Invoke-GameWipBenchmark -Mode 'run' -ProfileId $stepProfile -NameFilter $stepFilter -RepeatCount 0 -MinimumTime '' -RequestedOutput '' -Format 'json'
            }
            'Bundle'
            {
                Invoke-GameWipBundle -Id $step.Bundle
            }
            default
            {
                throw "Unknown bundle step kind '$($step.Kind)' in bundle '$Id'."
            }
        }
    }
}
