# GameWIP help, catalog, and structured diagnostic presentation.

Set-StrictMode -Version Latest

function Show-GameWipProjectCatalog
{
    Write-GameWipSection 'Project helper actions'
    foreach ($group in @('development', 'validation', 'quality', 'tools', 'repository', 'maintenance', 'navigation'))
    {
        $items = @($CommandConfig.Actions | Where-Object { $_.Visible -and $_.Group -eq $group })
        if ($items.Count -eq 0)
        {
            continue
        }
        Write-Host "  $((Get-Culture).TextInfo.ToTitleCase($group)):"
        foreach ($action in $items)
        {
            Write-Host ('    {0,-12} [{1,-11}] {2}' -f $action.Id, $action.Risk, $action.Description)
        }
    }

    foreach ($kind in @('configure', 'build', 'test'))
    {
        Write-GameWipSection ((Get-Culture).TextInfo.ToTitleCase($kind) + ' presets')
        Get-GameWipVisiblePresetName -Kind $kind | ForEach-Object { Write-Host "  $_" }
    }
    Write-GameWipSection 'Validation modules'
    Write-Host '  all'
    @($CommandConfig.Modules) | ForEach-Object { Write-Host "  $_" }
    Write-GameWipSection 'Project commands'
    foreach ($command in $CommandConfig.ProjectCommands)
    {
        Write-Host ("  {0,-24} {1}" -f $command.Id, $command.Name)
    }
    Write-GameWipSection 'Bundles'
    foreach ($bundleInfo in $CommandConfig.Bundles)
    {
        Write-Host ("  {0,-24} {1}" -f $bundleInfo.Id, $bundleInfo.Name)
    }
    Write-GameWipSection 'Benchmark profiles'
    foreach ($benchmarkProfile in $CommandConfig.BenchmarkProfiles)
    {
        Write-Host ("  {0,-12} {1}" -f $benchmarkProfile.Id, $benchmarkProfile.Name)
    }
    Show-GameWipWorkflowCatalog
}

function Show-GameWipActionFailure
{
    param([Parameter(Mandatory = $true)][System.Management.Automation.ErrorRecord]$ErrorRecord)

    $exception = $ErrorRecord.Exception
    $code = if ($exception.Data.Contains('GameWipCode'))
    {
        [string]$exception.Data['GameWipCode']
    }
    else
    {
        'operation-failed'
    }
    $details = if ($exception.Data.Contains('GameWipDetails'))
    {
        [string]$exception.Data['GameWipDetails']
    }
    else
    {
        ''
    }
    $suggestions = @(if ($exception.Data.Contains('GameWipSuggestedActions'))
        {
            $exception.Data['GameWipSuggestedActions']
        })
    $logPath = if ($exception.Data.Contains('GameWipLogPath'))
    {
        [string]$exception.Data['GameWipLogPath']
    }
    else
    {
        ''
    }

    Write-Host ''
    Write-GameWipHost "Action failed [$code]" -ForegroundColor Red
    Write-GameWipHost $exception.Message -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($details))
    {
        Write-Host $details
    }
    if (-not [string]::IsNullOrWhiteSpace($logPath))
    {
        Write-Host "Log: $logPath"
    }
    if ($suggestions.Count -eq 0)
    {
        $suggestions = @('Inspect the retained run/step log.', 'Rerun the smallest focused GameWIP command that reproduces the failure.')
    }
    Write-GameWipHost 'What to do next:' -ForegroundColor Cyan
    foreach ($suggestion in $suggestions)
    {
        Write-Host "  - $suggestion"
    }
}

function Show-GameWipCommonControlHelp
{
    param([string[]]$AdditionalOptions = @())

    Write-Host 'Common control options:'
    Write-Host '  -Preview          Do not apply the requested action; retain only diagnostic run evidence.'
    Write-Host '  -NonInteractive   Never prompt. This does not grant consent for any mutation risk.'
    Write-Host '  -Yes              Grant consent after the operation plan is known.'
    foreach ($option in $AdditionalOptions)
    {
        Write-Host $option
    }
    Write-Host '  -OutputMode <Summary|Stream|LogOnly>  Stream is the default; native output remains visible.'
    Write-Host '  -Quiet            Reduce normal command output; retained logs and receipt data remain available.'
    Write-Host '  -NoColor          Request plain terminal presentation.'
    Write-Host '  -Json             Emit the final operation result as JSON.'
    Write-Host '  -Verbose          Use the PowerShell common parameter for detailed progress.'
}

function Show-GameWipHelp
{
    Write-Host 'Usage:'
    Write-Host '  .\gamewip.bat <action> [command] [target] [options]'
    Write-Host '  .\gamewip.bat                         Open the interactive UI.'
    Write-Host ''
    Write-Host 'Available commands:'
    Write-Host '  doctor'
    Write-Host '  git <status|fetch|switch|update|cleanup|create|push|log> [branch]'
    Write-Host '  workflow <list|status|run> [workflow-id]'
    Write-Host '  unicode <status|verify|regenerate>'
    Write-Host '  format <check|apply>'
    Write-Host '  quality <check|fix|status> [-Changed] [-FailFast]'
    Write-Host '  tools <list|status|check-updates|ensure|update> [tool-id|category|all]'
    Write-Host '  configure [preset] [-Fresh]'
    Write-Host '  build [preset] [-Fresh]'
    Write-Host '  test [preset] [-NoBuild] [-Fresh]'
    Write-Host '  module [name] [-NoBuild] [-ExtraArgs <args>]'
    Write-Host '  wizard [-NoBuild]'
    Write-Host '  stress [name] [-Count N] [-Parallel N] [-StopOnFailure] [-NoBuild]'
    Write-Host '  run [project-command] [-NoBuild] [-ExtraArgs <args>]'
    Write-Host '  bundle [id] [-NoBuild] [-Fresh]'
    Write-Host '  docs | analyze | coverage | asan | links'
    Write-Host '  benchmark <run|dry-run|list|compare> [options]'
    Write-Host '  runs list [all] | runs show [latest|run-name] | runs clean [run-name|all]'
    Write-Host '  list | help'
    Write-Host ''
    Show-GameWipCommonControlHelp -AdditionalOptions @(
        '  -NoBuild          Do not build prerequisites automatically; require existing usable build state.'
        '  -Fresh            Recreate the selected known preset build tree before configuring or building.'
    )
    Write-Host ''
    Write-Host 'Benchmark options remain named because they describe measurement policy rather than command routing:'
    Write-Host '  -BenchmarkProfile <quick|standard|stable> -Filter <regex> -Repetitions N -MinTime <time>'
    Write-Host '  -AggregatesOnly -Output <path> -OutputFormat <json|csv> -Baseline <json> -Candidate <json>'
    Write-Host ''
    Write-Host 'Use .\gamewip.bat list to discover valid IDs.'
}
