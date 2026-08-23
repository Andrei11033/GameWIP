# GameWIP Testing helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Invoke-GameWipTestPreset
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [switch]$UseWorkspaceTemp
    )

    Assert-GameWipValidPreset -Kind 'test' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    $testFile = Join-Path $RepositoryRoot "build\$Name\CTestTestfile.cmake"
    if (-not (Test-Path -LiteralPath $testFile))
    {
        Write-Host "Test preset '$Name' is not built; configuring and building it now." -ForegroundColor Cyan
        Invoke-GameWipConfigurePreset -Name $Name
        Invoke-GameWipBuildPreset -Name $Name
    }
    Invoke-GameWipNative -Name "ctest-$Name" -FilePath 'ctest' -Arguments @('--preset', $Name, '--output-on-failure') -UseWorkspaceTemp:$UseWorkspaceTemp -PathPrefix (Get-GameWipToolchainPathPrefix $Name)
}

function Invoke-GameWipValidationModule
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$Arguments = @(),
        [switch]$ForceBuild
    )

    Assert-GameWipValidModule -Name $Name
    $command = Get-GameWipProjectCommand -Id 'test-all'
    Initialize-GameWipProjectCommandBuild -Command $command -ForceBuild:$ForceBuild
    $executable = Resolve-GameWipProjectExecutable -Command $command

    $testArguments = New-Object System.Collections.Generic.List[string]
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
        [switch]$ForceBuild
    )

    Assert-GameWipValidModule -Name $Name
    Initialize-GameWipRunLog

    $command = Get-GameWipProjectCommand -Id 'test-all'
    Initialize-GameWipProjectCommandBuild -Command $command -ForceBuild:$ForceBuild
    $executable = Resolve-GameWipProjectExecutable -Command $command
    $tempRoot = $Script:OperationTemp

    $baseArguments = New-Object System.Collections.Generic.List[string]
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
    Write-Host "Executable: $executable"
    Write-Host ("> {0}" -f (ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $baseArguments.ToArray()))
    Write-Host "Worker logs: $($Script:RunContext.Logs)\stress-####.log and stress-####.err.log"

    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $active = New-Object System.Collections.Generic.List[object]
    $nextRun = 1
    $completed = 0
    $failed = 0
    $progressClock = [System.Diagnostics.Stopwatch]::StartNew()
    $lastProgressMilliseconds = -2000.0

    try
    {
        if (-not $NoWorkspaceTemp)
        {
            $env:TEMP = $tempRoot
            $env:TMP = $tempRoot
            Write-Host "Workspace temp: $tempRoot"
        }

        while ($nextRun -le $RunCount -or $active.Count -gt 0)
        {
            while ($nextRun -le $RunCount -and $active.Count -lt $MaxParallel)
            {
                $runName = 'stress-{0:D4}' -f $nextRun
                $commandLine = ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $baseArguments.ToArray()
                $step = Initialize-GameWipToolRunStep -Run $Script:RunContext -Name $runName -CommandLine $commandLine
                $stdout = $step.LogPath
                $stderr = Join-Path $Script:RunContext.Logs "$runName.err.log"
                $exitPath = Join-Path $Script:RunContext.Logs "$runName.exit"

                $process = Start-Process -FilePath $executable `
                    -ArgumentList $baseArguments.ToArray() `
                    -WorkingDirectory $RepositoryRoot `
                    -NoNewWindow `
                    -PassThru `
                    -RedirectStandardOutput $stdout `
                    -RedirectStandardError $stderr

                $active.Add([pscustomobject]@{
                        Run = $nextRun
                        Process = $process
                        Stdout = $stdout
                        Stderr = $stderr
                        ExitPath = $exitPath
                        Step = $step
                    }) | Out-Null
                ++$nextRun
            }

            $elapsedMilliseconds = $progressClock.Elapsed.TotalMilliseconds
            if (($elapsedMilliseconds - $lastProgressMilliseconds) -ge 2000.0)
            {
                Write-Host ("Progress: completed={0}/{1} active={2} launched={3} failed={4}" -f $completed, $RunCount, $active.Count, ($nextRun - 1), $failed)
                $lastProgressMilliseconds = $elapsedMilliseconds
            }

            for ($index = $active.Count - 1; $index -ge 0; --$index)
            {
                $entry = $active[$index]
                if (-not $entry.Process.HasExited)
                {
                    continue
                }

                $code = [int]$entry.Process.ExitCode
                Set-Content -LiteralPath $entry.ExitPath -Value $code -Encoding ASCII
                ++$completed
                if ($code -eq 0)
                {
                    Write-Host ("Run {0}: PASS ({1}/{2} complete)" -f $entry.Run, $completed, $RunCount)
                }
                else
                {
                    ++$failed
                    Write-Host ("Run {0}: FAIL exit={1} ({2}/{3} complete) stdout={4} stderr={5}" -f $entry.Run, $code, $completed, $RunCount, $entry.Stdout, $entry.Stderr) -ForegroundColor Red
                }
                Complete-GameWipToolRunStep -Run $Script:RunContext -Step $entry.Step -ExitCode $code
                $active.RemoveAt($index)
            }

            if ($active.Count -gt 0)
            {
                Start-Sleep -Milliseconds 200
            }
        }
    }
    finally
    {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
    }

    if ($failed -ne 0)
    {
        throw "$failed of $RunCount stress runs failed. Logs are in $Script:RunRoot"
    }
    Write-Host "Stress complete: $RunCount runs passed." -ForegroundColor Green
}

function Split-GameWipExtraArgument
{
    param([AllowEmptyString()][string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text))
    {
        return @()
    }
    @($Text -split ' ' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Invoke-GameWipValidationCommandWizard
{
    Write-GameWipSection 'Validation command builder'
    Write-Host 'Builds a GameWIPTests.exe command from supported validation-runner arguments.'

    $moduleChoices = @('all') + @($CommandConfig.Modules)
    $selectedModule = Read-GameWipMenuChoice -Prompt 'Module selection' -Choices $moduleChoices -Default 'all'
    if ($null -eq $selectedModule)
    {
        return
    }

    $skippedModules = @()
    if ($selectedModule -eq 'all')
    {
        $skippedModules = @(Read-GameWipMultiChoice -Prompt 'Modules to skip' -Choices @($CommandConfig.Modules))
    }

    $verbose = Read-GameWipYesNo -Prompt 'Mirror full test output to stdout?' -Default $false
    $manualTests = Read-GameWipYesNo -Prompt 'Enable manual tests?' -Default $false
    $childProcesses = Read-GameWipYesNo -Prompt 'Enable TestSupport child-process checks?' -Default $true
    $writeReport = Read-GameWipYesNo -Prompt 'Write retained test report?' -Default $false

    $reportPath = ''
    if ($writeReport)
    {
        $reportPath = Read-GameWipTextValue -Prompt 'Report path' -Default 'logs/tests/latest_test_report.txt'
    }

    $extraText = Read-GameWipTextValue -Prompt 'Extra validation args, space-separated' -Default ''
    $buildIfMissingChoice = Read-GameWipYesNo -Prompt 'Build test executable if missing?' -Default $true

    $arguments = New-Object System.Collections.Generic.List[string]
    if ($selectedModule -ne 'all')
    {
        $arguments.Add("--test-module=$selectedModule") | Out-Null
    }
    foreach ($skippedModule in $skippedModules)
    {
        $arguments.Add("--skip-test-module=$skippedModule") | Out-Null
    }
    if ($verbose)
    {
        $arguments.Add('--verbose-tests') | Out-Null
    }
    if ($manualTests)
    {
        $arguments.Add('--manual-tests') | Out-Null
    }
    if ($childProcesses -eq $false)
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
    if ($buildIfMissingChoice)
    {
        Initialize-GameWipProjectCommandBuild -Command $command -ForceBuild
    }
    $executable = Resolve-GameWipProjectExecutable -Command $command
    $commandLine = ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $arguments.ToArray()

    Write-GameWipSection 'Built command'
    Write-Host $commandLine

    if (Read-GameWipYesNo -Prompt 'Run this command now?' -Default $true)
    {
        Invoke-GameWipNative -Name 'validation-wizard' -FilePath $executable -Arguments $arguments.ToArray() -UseWorkspaceTemp
        Write-GameWipNextStepHint 'Use the printed command directly next time, or rerun gamewip wizard to adjust flags.'
    }
    else
    {
        Write-GameWipNextStepHint 'Copy the printed command into PowerShell when you want to run it.'
    }
}
