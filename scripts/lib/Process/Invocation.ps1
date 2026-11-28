# Process invocation, output capture, and timeout handling.

# ------------------------------------------------------------
# Process invocation
# ------------------------------------------------------------

function Invoke-GameWipProcess
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string[]]$DisplayArguments,
        [string]$WorkingDirectory = $RepositoryRoot,
        [hashtable]$Environment = @{},
        [ValidateSet('Summary', 'Stream', 'LogOnly')][string]$OutputMode,
        [ValidateRange(0, 86400)][int]$TimeoutSeconds = 0,
        [int[]]$SuccessfulExitCodes = @(0),
        [string]$LogPath,
        [switch]$InheritConsole,
        [switch]$PreserveConsoleSession
    )

    Assert-GameWipNotCancelled

    if ($null -eq $DisplayArguments)
    {
        $DisplayArguments = @($Arguments)
    }

    if ([string]::IsNullOrWhiteSpace($OutputMode))
    {
        $OutputMode = if ($null -ne $Script:OperationContext)
        {
            [string]$Script:OperationContext.OutputMode
        }
        else
        {
            'Stream'
        }
    }

    # Allocate temporary streams and a retained log path for this invocation.
    $temporaryRoot = Get-GameWipTemporaryRoot
    $captureId = [guid]::NewGuid().ToString('N')
    $stdoutPath = Join-Path $temporaryRoot "$captureId.stdout.log"
    $stderrPath = Join-Path $temporaryRoot "$captureId.stderr.log"
    $ephemeralLog = $false

    if ([string]::IsNullOrWhiteSpace($LogPath))
    {
        if ($null -ne $Script:OperationContext -and $null -ne $Script:OperationContext.Run)
        {
            $LogPath = Join-Path $Script:OperationContext.Run.Logs "query-$captureId.log"
        }
        else
        {
            $LogPath = Join-Path $temporaryRoot "$captureId.log"
            $ephemeralLog = $true
        }
    }

    $displayCommandLine = ConvertTo-GameWipNativeCommandLine -FilePath $FilePath -Arguments $DisplayArguments

    # Preserve caller-session commands; otherwise own the launched process group.
    $launch = if ($InheritConsole -or $PreserveConsoleSession)
    {
        $sessionLaunch = Resolve-GameWipProcessLaunch -FilePath $FilePath -Arguments $Arguments
        [pscustomobject]@{
            FilePath = $sessionLaunch.FilePath
            ArgumentLine = $sessionLaunch.ArgumentLine
            OwnProcessGroup = $false
        }
    }
    else
    {
        Resolve-GameWipOwnedProcessLaunch -FilePath $FilePath -Arguments $Arguments
    }

    # Apply requested environment overrides temporarily in this helper process.
    $processEnvironment = @{}

    foreach ($entry in $Environment.GetEnumerator())
    {
        $processEnvironment[[string]$entry.Key] = [string]$entry.Value
    }

    if ([IO.Path]::GetFileNameWithoutExtension($FilePath) -match '^python(?:\d+(?:\.\d+)*)?$')
    {
        $processEnvironment.PYTHONDONTWRITEBYTECODE = '1'
    }

    $previousEnvironment = @{}

    foreach ($entry in $processEnvironment.GetEnumerator())
    {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable([string]$entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable([string]$entry.Key, [string]$entry.Value, 'Process')
    }

    # Track process completion and streamed output independently.
    $process = $null
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $cancelled = $false
    $timedOut = $false
    $exitCode = -1

    # Track visible output progress for the stream-mode heartbeat.
    $stdoutLineCount = 0
    $stderrLineCount = 0
    $lastVisibleActivitySeconds = 0.0
    $stdoutReader = $null
    $stderrReader = $null

    if ($OutputMode -eq 'Stream' -and -not $InheritConsole)
    {
        $stdoutReader = New-GameWipProcessOutputReader -Path $stdoutPath
        $stderrReader = New-GameWipProcessOutputReader -Path $stderrPath
    }

    try
    {
        # Inherited-console commands use the caller's streams directly. The child
        # owns console presentation, so its output is not captured into run logs.
        $start = @{
            FilePath = $launch.FilePath
            WorkingDirectory = $WorkingDirectory
            PassThru = $true
            NoNewWindow = $true
        }

        if (-not $InheritConsole)
        {
            $start.RedirectStandardOutput = $stdoutPath
            $start.RedirectStandardError = $stderrPath
        }

        if (-not [string]::IsNullOrWhiteSpace([string]$launch.ArgumentLine))
        {
            $start.ArgumentList = $launch.ArgumentLine
        }

        $process = Start-Process @start
        # Acquire the native handle before a short-lived child can exit. Without
        # this, Windows PowerShell can report ExitCode = 0 for a failed process.
        $null = $process.Handle
        $process | Add-Member -NotePropertyName GameWipOwnProcessGroup -NotePropertyValue ([bool]$launch.OwnProcessGroup)

        if ($null -ne $Script:OperationContext)
        {
            $Script:OperationContext.ActiveProcesses.Add([pscustomobject]@{
                    Process = $process
                    Command = $displayCommandLine
                }) | Out-Null
        }

        while (-not $process.HasExited)
        {
            Assert-GameWipNotCancelled

            if ($TimeoutSeconds -gt 0 -and $clock.Elapsed.TotalSeconds -ge $TimeoutSeconds)
            {
                $timedOut = $true
                Stop-GameWipOwnedProcess -Process $process
                break
            }

            if ($OutputMode -eq 'Stream' -and -not $InheritConsole)
            {
                $previousLineCount = $stdoutLineCount + $stderrLineCount
                $stdoutLineCount += Write-GameWipProcessNewOutput -ReaderState $stdoutReader
                $stderrLineCount += Write-GameWipProcessNewOutput -ReaderState $stderrReader -Prefix '[stderr] ' -PrefixColor Yellow

                if (($stdoutLineCount + $stderrLineCount) -gt $previousLineCount)
                {
                    $lastVisibleActivitySeconds = $clock.Elapsed.TotalSeconds
                }

                elseif (($clock.Elapsed.TotalSeconds - $lastVisibleActivitySeconds) -ge 15.0)
                {
                    if ($null -ne $Script:OperationContext -and $Script:OperationContext.NoColor)
                    {
                        Write-Host ('  Still running ({0:N0}s elapsed)...' -f $clock.Elapsed.TotalSeconds)
                    }
                    else
                    {
                        Write-GameWipHost ('  Still running ({0:N0}s elapsed)...' -f $clock.Elapsed.TotalSeconds) -ForegroundColor DarkGray
                    }
                    $lastVisibleActivitySeconds = $clock.Elapsed.TotalSeconds
                }
            }

            Start-Sleep -Milliseconds 100
        }

        try
        {
            $process.WaitForExit()
        }

        catch
        {
            $null = $_.Exception
        }

        try
        {
            $process.Refresh()
        }

        catch
        {
            $null = $_.Exception
        }

        if ($timedOut)
        {
            $exitCode = -1
        }
        else
        {
            $exitCode = [int]$process.ExitCode
        }
    }

    catch [System.Management.Automation.PipelineStoppedException]
    {
        Request-GameWipCancellation
        $cancelled = $true

        if ($null -ne $process)
        {
            Stop-GameWipOwnedProcess -Process $process
        }
    }

    catch [System.OperationCanceledException]
    {
        $cancelled = $true

        if ($null -ne $process)
        {
            Stop-GameWipOwnedProcess -Process $process
        }
    }

    finally
    {
        # Restore process-wide environment state before removing the process from
        # the operation registry, so cancellation cleanup sees a coherent state.
        $clock.Stop()

        foreach ($entry in $previousEnvironment.GetEnumerator())
        {
            [Environment]::SetEnvironmentVariable([string]$entry.Key, $entry.Value, 'Process')
        }

        if ($null -ne $Script:OperationContext -and $null -ne $process)
        {
            for ($index = $Script:OperationContext.ActiveProcesses.Count - 1; $index -ge 0; --$index)
            {
                if ($Script:OperationContext.ActiveProcesses[$index].Process.Id -eq $process.Id)
                {
                    $Script:OperationContext.ActiveProcesses.RemoveAt($index)
                }
            }
        }

        if ($null -ne $stdoutReader)
        {
            try
            {
                $stdoutLineCount += Write-GameWipProcessNewOutput -ReaderState $stdoutReader -Flush
                $stderrLineCount += Write-GameWipProcessNewOutput -ReaderState $stderrReader -Prefix '[stderr] ' -PrefixColor Yellow -Flush
            }

            finally
            {
                Close-GameWipProcessOutputReader -ReaderState $stdoutReader
                Close-GameWipProcessOutputReader -ReaderState $stderrReader
            }
        }
    }

    # Combine captured streams into the retained command log.
    $stdout = @(if (-not $InheritConsole -and (Test-Path -LiteralPath $stdoutPath))
        {
            Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
        })
    $stderr = @(if (-not $InheritConsole -and (Test-Path -LiteralPath $stderrPath))
        {
            Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue
        })
    $combined = [System.Collections.Generic.List[string]]::new()

    if ($InheritConsole)
    {
        $combined.Add('Process output was inherited by the caller console and was not captured.') | Out-Null
    }

    foreach ($line in $stdout)
    {
        $combined.Add([string]$line) | Out-Null
    }

    if ($stderr.Count -ne 0)
    {
        $combined.Add('') | Out-Null
        $combined.Add('--- stderr ---') | Out-Null

        foreach ($line in $stderr)
        {
            $combined.Add([string]$line) | Out-Null
        }
    }

    Write-GameWipTextAtomic `
        -Path $LogPath `
        -Content (($combined -join "`n") + $(if ($combined.Count -ne 0)
            {
                "`n"
            }
            else
            {
                ''
            })) `
        -SuppressMutationTracking

    if ($OutputMode -eq 'Summary' -and ($SuccessfulExitCodes -notcontains $exitCode -or $cancelled -or $timedOut))
    {
        foreach ($line in @($combined | Select-Object -Last 40))
        {
            Write-Host "  $line"
        }
    }

    # Temporary streams are no longer needed once the retained result is built.
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    $resultLogPath = $LogPath

    if ($ephemeralLog)
    {
        Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue
        $resultLogPath = $null
    }

    return [pscustomobject]@{
        # Command identity
        FilePath = $FilePath
        Arguments = @($DisplayArguments)
        CommandLine = $displayCommandLine

        # Execution result
        ExitCode = $exitCode
        DurationSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
        TimedOut = $timedOut
        Cancelled = $cancelled

        # Captured output and log
        Stdout = @($stdout)
        Stderr = @($stderr)
        LogPath = $resultLogPath
    }
}
