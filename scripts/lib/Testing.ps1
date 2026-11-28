# Correctness tests, focused modules, stress runs, and validation commands.

# ------------------------------------------------------------
# Test and validation execution
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Initialize-GameWipTestPresetBuild
{
    param([Parameter(Mandatory = $true)][string]$Name, [switch]$NoBuild, [switch]$Fresh)

    if ($NoBuild -and $Fresh)
    {
        throw '-CleanBuild cannot be combined with -SkipBuild because a recreated preset must be configured and built.'
    }

    $testFile = Join-Path (Get-GameWipBuildDirectory -PresetName $Name) 'CTestTestfile.cmake'

    if ($NoBuild)
    {
        if (-not (Test-Path -LiteralPath $testFile))
        {
            $buildCommand = "$(Get-GameWipProjectHelperCommand) build $Name"
            throw (New-GameWipDiagnosticException -Code 'prerequisite-build-disabled' -Summary "CTest preset '$Name' has no configured test tree." -SuggestedActions @("Run '$buildCommand'.", 'Rerun without -SkipBuild.'))
        }

        return
    }

    Invoke-GameWipBuildPreset -Name $Name -Fresh:$Fresh
}

function Invoke-GameWipTestPreset
{
    param([Parameter(Mandatory = $true)][string]$Name, [switch]$UseWorkspaceTemp, [switch]$NoBuild, [switch]$Fresh)
    Assert-GameWipValidPreset -Kind 'test' -Name $Name
    Assert-GameWipSelectedTargetRunnable -Action "CTest preset '$Name'"
    Confirm-GameWipToolchain -PresetName $Name
    Initialize-GameWipTestPresetBuild -Name $Name -NoBuild:$NoBuild -Fresh:$Fresh
    $buildDirectory = Get-GameWipBuildDirectory -PresetName $Name
    Invoke-GameWipNative `
        -Name "ctest-$Name" `
        -FilePath 'ctest' `
        -Arguments @('--preset', $Name, '--test-dir', $buildDirectory, '--output-on-failure') `
        -UseWorkspaceTemp:$UseWorkspaceTemp `
        -PathPrefix (Get-GameWipToolchainPathPrefix)
}

function Invoke-GameWipValidationModule
{
    param([Parameter(Mandatory = $true)][string]$Name, [string[]]$Arguments = @(), [switch]$NoBuild)
    Assert-GameWipValidModule -Name $Name
    Assert-GameWipSelectedTargetRunnable -Action "Validation module '$Name'"
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
    $inheritConsole = @($testArguments) -contains '--manual-tests'
    Invoke-GameWipNative `
        -Name "module-$Name" `
        -FilePath $executable `
        -Arguments $testArguments.ToArray() `
        -UseWorkspaceTemp `
        -InheritConsole:$inheritConsole
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

    if (@($Arguments) -contains '--manual-tests')
    {
        throw "Manual tests cannot run through 'stress' because stress launches concurrent validation processes. Use 'wizard' or 'module' instead."
    }

    Assert-GameWipSelectedTargetRunnable -Action "Stress module '$Name'"

    # Resolve the executable and shared arguments before launching workers.
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
        # Fill available worker slots and reap completed runs until all work ends.
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
                Write-GameWipSemanticText -Object 'Progress:' -Semantic Accent -NoNewline
                Write-Host (" completed={0}/{1} active={2} launched={3} failed={4}" -f $completed, $RunCount, $active.Count, ($nextRun - 1), $failed)
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
                    Write-Host ("Run {0}: " -f $entry.Run) -NoNewline
                    Write-GameWipSemanticText -Object 'FAIL' -Semantic Failure -NoNewline
                    Write-Host (" exit={0} stdout=" -f $code) -NoNewline
                    Write-GameWipSemanticText -Object ([string]$entry.Stdout) -Semantic Muted -NoNewline
                    Write-Host ' stderr=' -NoNewline
                    Write-GameWipSemanticText -Object ([string]$entry.Stderr) -Semantic Muted

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
        # Restore the caller's temporary paths and stop remaining child processes.
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

    # Report aggregate failure only after worker cleanup completes.
    if ($failed -ne 0)
    {
        $launched = $nextRun - 1
        throw "$failed of $launched launched stress runs failed. Logs are in $Script:OperationContext.Run.Root"
    }

    Write-GameWipSemanticText -Object 'Stress complete:' -Semantic Success -NoNewline
    Write-Host " $completed runs passed."
}
