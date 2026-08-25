# GameWIP correctness-test, focused-module, stress, and validation-command behavior.

Set-StrictMode -Version Latest

function Initialize-GameWipTestPresetBuild
{
    param([Parameter(Mandatory = $true)][string]$Name, [switch]$NoBuild)
    $testFile = Join-Path $RepositoryRoot "build\$Name\CTestTestfile.cmake"
    if ($NoBuild)
    {
        if (-not (Test-Path -LiteralPath $testFile))
        {
            throw (New-GameWipDiagnosticException -Code 'prerequisite-build-disabled' -Summary "CTest preset '$Name' has no configured test tree." -SuggestedActions @("Run '.\gamewip.bat build $Name'.", 'Rerun without -NoBuild.'))
        }
        return
    }

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
                try
                {
                    $process = Start-GameWipOwnedProcess -FilePath $executable -Arguments $baseArguments.ToArray() -WorkingDirectory $RepositoryRoot -RedirectStandardOutput $stdout -RedirectStandardError $stderr
                }
                catch
                {
                    Complete-GameWipToolRunStep -Run $Script:OperationContext.Run -Step $step -ExitCode -1 -Status failed
                    throw
                }
                Set-GameWipMutationStarted
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
            $status = if (Test-GameWipCancellationRequested)
            {
                'cancelled'
            }
            else
            {
                'failed'
            }
            Complete-GameWipToolRunStep -Run $Script:OperationContext.Run -Step $entry.Step -ExitCode -1 -Status $status
            Remove-GameWipOwnedProcessRegistration -Process $entry.Process
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
    param([switch]$NoBuild)
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
    $executable = Resolve-GameWipProjectExecutable -Command $command
    Write-GameWipSection 'Built command'
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $arguments.ToArray())
    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)
    {
        Write-GameWipHost 'Preview: validation command will not be built or executed.' -ForegroundColor Cyan
        return
    }
    if (Read-GameWipYesNo -Prompt 'Run this command now?' -Default $true)
    {
        Invoke-GameWipMutation -Summary 'Run the composed validation command.' -Risk local -Plan @('Ensure the validation executable unless -NoBuild is used.', 'Execute the composed correctness command.') -Body {
            Initialize-GameWipProjectCommandBuild -Command $command -NoBuild:$NoBuild
            Invoke-GameWipNative -Name 'validation-wizard' -FilePath $executable -Arguments $arguments.ToArray() -UseWorkspaceTemp
        } | Out-Null
    }
}
