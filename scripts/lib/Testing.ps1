# GameWIP correctness-test, focused-module, stress, and validation-command behavior.

Set-StrictMode -Version Latest

function Initialize-GameWipTestPresetBuild
{
    param([Parameter(Mandatory = $true)][string]$Name, [switch]$NoBuild)
    $testFile = Join-Path $RepositoryRoot "build\$Name\CTestTestfile.cmake"
    if (Test-Path -LiteralPath $testFile)
    {
        return
    }
    if ($NoBuild)
    {
        throw (New-GameWipDiagnosticException -Code 'prerequisite-build-disabled' -Summary "CTest preset '$Name' has not been built." -SuggestedActions @("Run '.\gamewip.bat build $Name'.", 'Rerun without -NoBuild.'))
    }
    Write-GameWipOperationEvent -Phase plan -Step "test-$Name" -Severity info -Message "CTest preset '$Name' is missing; GameWIP will configure and build it first."
    Invoke-GameWipConfigurePreset -Name $Name
    Invoke-GameWipBuildPreset -Name $Name
}

function Invoke-GameWipTestPreset
{
    param([Parameter(Mandatory = $true)][string]$Name, [switch]$UseWorkspaceTemp, [switch]$NoBuild)
    Assert-GameWipValidPreset -Kind 'test' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    Initialize-GameWipTestPresetBuild -Name $Name -NoBuild:$NoBuild
    Invoke-GameWipNative -Name "ctest-$Name" -FilePath 'ctest' -Arguments @('--preset', $Name, '--output-on-failure') -UseWorkspaceTemp:$UseWorkspaceTemp -PathPrefix (Get-GameWipToolchainPathPrefix $Name)
}

function Invoke-GameWipValidationModule
{
    param([Parameter(Mandatory = $true)][string]$Name, [string[]]$Arguments = @(), [switch]$NoBuild)
    Assert-GameWipValidModule -Name $Name
    $command = Get-GameWipProjectCommand -Id 'test-all'
    Initialize-GameWipProjectCommandBuild -Command $command -NoBuild:$NoBuild
    $executable = Resolve-GameWipProjectExecutable -Command $command
    $testArguments = [System.Collections.Generic.List[string]]::new()
    if ($Name -ne 'all')
    {
        $testArguments.Add("--test-module=$Name") | Out-Null
    }
    $testArguments.Add('--no-test-report') | Out-Null
    foreach ($argument in $Arguments)
    {
        $testArguments.Add($argument) | Out-Null
    }
    Invoke-GameWipNative -Name "module-$Name" -FilePath $executable -Arguments $testArguments.ToArray() -UseWorkspaceTemp
}

function Invoke-GameWipStressModule
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$RunCount,
        [Parameter(Mandatory = $true)][int]$MaxParallel,
        [string[]]$Arguments = @(),
        [switch]$NoBuild,
        [switch]$StopOnFailure
    )
    Assert-GameWipValidModule -Name $Name
    Initialize-GameWipRunLog
    $command = Get-GameWipProjectCommand -Id 'test-all'
    Initialize-GameWipProjectCommandBuild -Command $command -NoBuild:$NoBuild
    $executable = Resolve-GameWipProjectExecutable -Command $command
    $baseArguments = [System.Collections.Generic.List[string]]::new()
    if ($Name -ne 'all')
    {
        $baseArguments.Add("--test-module=$Name") | Out-Null
    }
    $baseArguments.Add('--no-test-report') | Out-Null
    foreach ($argument in $Arguments)
    {
        $baseArguments.Add($argument) | Out-Null
    }

    Write-GameWipSection "Stress $Name"
    Write-Host "Runs: $RunCount"
    Write-Host "Parallel workers: $MaxParallel"
    Write-Host "Stop on failure: $([bool]$StopOnFailure)"
    Write-Host "Executable: $executable"
    Write-Host "Worker logs: $($Script:OperationContext.Run.Logs)\stress-####.log and stress-####.err.log"

    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $active = [System.Collections.Generic.List[object]]::new()
    $nextRun = 1
    $completed = 0
    $failed = 0
    $stopLaunching = $false
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $lastProgress = -2000.0
    try
    {
        if (-not $NoWorkspaceTemp)
        {
            $env:TEMP = $Script:OperationContext.Temp
            $env:TMP = $Script:OperationContext.Temp
        }
        while ((-not $stopLaunching -and $nextRun -le $RunCount) -or $active.Count -gt 0)
        {
            Assert-GameWipNotCancelled
            while (-not $stopLaunching -and $nextRun -le $RunCount -and $active.Count -lt $MaxParallel)
            {
                $runName = 'stress-{0:D4}' -f $nextRun
                $commandLine = ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $baseArguments.ToArray()
                $step = Initialize-GameWipToolRunStep -Run $Script:OperationContext.Run -Name $runName -CommandLine $commandLine
                $stdout = $step.LogPath
                $stderr = Join-Path $Script:OperationContext.Run.Logs "$runName.err.log"
                $process = Start-GameWipOwnedProcess -FilePath $executable -Arguments $baseArguments.ToArray() -WorkingDirectory $RepositoryRoot -RedirectStandardOutput $stdout -RedirectStandardError $stderr
                $entry = [pscustomobject]@{ Run = $nextRun; Process = $process; Stdout = $stdout; Stderr = $stderr; Step = $step }
                $active.Add($entry) | Out-Null
                ++$nextRun
            }

            if (($clock.Elapsed.TotalMilliseconds - $lastProgress) -ge 2000.0)
            {
                Write-Host ("Progress: completed={0}/{1} active={2} launched={3} failed={4}" -f $completed, $RunCount, $active.Count, ($nextRun - 1), $failed)
                $lastProgress = $clock.Elapsed.TotalMilliseconds
            }
            for ($index = $active.Count - 1; $index -ge 0; --$index)
            {
                $entry = $active[$index]
                if (-not $entry.Process.HasExited)
                {
                    continue
                }
                $code = [int]$entry.Process.ExitCode
                ++$completed
                $status = if ($code -eq 0)
                {
                    'passed'
                }
                else
                {
                    'failed'
                }
                Complete-GameWipToolRunStep -Run $Script:OperationContext.Run -Step $entry.Step -ExitCode $code -Status $status
                if ($code -eq 0)
                {
                    Write-Verbose ("Run {0}: PASS ({1}/{2} complete)" -f $entry.Run, $completed, $RunCount)
                }
                else
                {
                    ++$failed
                    Write-GameWipHost ("Run {0}: FAIL exit={1} stdout={2} stderr={3}" -f $entry.Run, $code, $entry.Stdout, $entry.Stderr) -ForegroundColor Red
                    if ($StopOnFailure)
                    {
                        $stopLaunching = $true
                    }
                }
                Remove-GameWipOwnedProcessRegistration -Process $entry.Process
                $active.RemoveAt($index)
            }
            if ($active.Count -gt 0)
            {
                Start-Sleep -Milliseconds 150
            }
        }
    }
    finally
    {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        foreach ($entry in @($active))
        {
            Stop-GameWipOwnedProcess -Process $entry.Process
        }
    }
    if ($failed -ne 0)
    {
        $launched = $nextRun - 1
        throw "$failed of $launched launched stress runs failed. Logs are in $Script:OperationContext.Run.Root"
    }
    Write-GameWipHost "Stress complete: $completed runs passed." -ForegroundColor Green
}

function Split-GameWipExtraArgument
{
    param([AllowEmptyString()][string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text))
    {
        return @()
    }
    return @($Text -split ' ' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Invoke-GameWipValidationCommandWizard
{
    Write-GameWipSection 'Validation command builder'
    $moduleResult = Read-GameWipMenuChoiceResult -Prompt 'Module selection' -Choices (@('all') + @($CommandConfig.Modules)) -Default 'all'
    if ($moduleResult.Status -eq 'Cancelled')
    {
        return
    }
    $selectedModule = [string]$moduleResult.Value

    $skippedModules = @()
    if ($selectedModule -eq 'all')
    {
        $skipResult = Read-GameWipMultiChoiceResult -Prompt 'Modules to skip' -Choices @($CommandConfig.Modules)
        if ($skipResult.Status -eq 'Cancelled')
        {
            return
        }
        $skippedModules = @($skipResult.Value)
    }
    $verboseTests = Read-GameWipYesNo -Prompt 'Mirror full test output to stdout?' -Default $false
    $manualTests = Read-GameWipYesNo -Prompt 'Enable manual tests?' -Default $false
    $childProcesses = Read-GameWipYesNo -Prompt 'Enable TestSupport child-process checks?' -Default $true
    $writeReport = Read-GameWipYesNo -Prompt 'Write retained test report?' -Default $false
    $reportPath = if ($writeReport)
    {
        Read-GameWipTextValue -Prompt 'Report path' -Default 'logs/tests/latest_test_report.txt'
    }
    else
    {
        ''
    }
    $extraText = Read-GameWipTextValue -Prompt 'Extra validation args, space-separated' -Default ''

    $arguments = [System.Collections.Generic.List[string]]::new()
    if ($selectedModule -ne 'all')
    {
        $arguments.Add("--test-module=$selectedModule") | Out-Null
    }
    foreach ($item in $skippedModules)
    {
        $arguments.Add("--skip-test-module=$item") | Out-Null
    }
    if ($verboseTests)
    {
        $arguments.Add('--verbose-tests') | Out-Null
    }
    if ($manualTests)
    {
        $arguments.Add('--manual-tests') | Out-Null
    }
    if (-not $childProcesses)
    {
        $arguments.Add('--no-test-support-child-process') | Out-Null
    }
    if ($writeReport)
    {
        $arguments.Add("--test-report=$reportPath") | Out-Null
    }
    else
    {
        $arguments.Add('--no-test-report') | Out-Null
    }
    foreach ($argument in (Split-GameWipExtraArgument -Text $extraText))
    {
        $arguments.Add($argument) | Out-Null
    }

    $command = Get-GameWipProjectCommand -Id 'test-all'
    Initialize-GameWipProjectCommandBuild -Command $command
    $executable = Resolve-GameWipProjectExecutable -Command $command
    Write-GameWipSection 'Built command'
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $arguments.ToArray())
    if (Read-GameWipYesNo -Prompt 'Run this command now?' -Default $true)
    {
        Invoke-GameWipNative -Name 'validation-wizard' -FilePath $executable -Arguments $arguments.ToArray() -UseWorkspaceTemp
    }
}
