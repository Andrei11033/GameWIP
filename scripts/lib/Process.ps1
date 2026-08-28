# GameWIP native-process execution. Process ownership and logs are centralized here.

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Native command construction
# ------------------------------------------------------------

function ConvertTo-GameWipNativeArgument
{
    param([AllowEmptyString()][string]$Argument)
    if ($null -eq $Argument -or $Argument.Length -eq 0)
    {
        return '""'
    }
    if ($Argument -notmatch '[\s"&|<>^()%!]')
    {
        return $Argument
    }

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray())
    {
        if ($character -eq '\')
        {
            ++$backslashes; continue
        }
        if ($character -eq '"')
        {
            [void]$builder.Append('\' * (($backslashes * 2) + 1))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0)
        {
            [void]$builder.Append('\' * $backslashes); $backslashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashes -gt 0)
    {
        [void]$builder.Append('\' * ($backslashes * 2))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-GameWipNativeCommandLine
{
    param([Parameter(Mandatory = $true)][string]$FilePath, [string[]]$Arguments = @())
    return (@(ConvertTo-GameWipNativeArgument $FilePath) + @($Arguments | ForEach-Object { ConvertTo-GameWipNativeArgument $_ })) -join ' '
}

function Resolve-GameWipProcessLaunch
{
    param([Parameter(Mandatory = $true)][string]$FilePath, [string[]]$Arguments = @())

    $extension = [IO.Path]::GetExtension($FilePath).ToLowerInvariant()
    if ((Test-GameWipWindowsHost) -and $extension -in @('.cmd', '.bat'))
    {
        $commandInterpreter = if ([string]::IsNullOrWhiteSpace([string]$env:ComSpec))
        {
            'cmd.exe'
        }
        else
        {
            [string]$env:ComSpec
        }
        $commandLine = ConvertTo-GameWipNativeCommandLine -FilePath $FilePath -Arguments $Arguments
        return [pscustomobject]@{ FilePath = $commandInterpreter; ArgumentLine = "/d /s /c `"$commandLine`"" }
    }
    if ((Test-GameWipWindowsHost) -and $extension -eq '.ps1')
    {
        $powerShell = (Get-Process -Id $PID).Path
        $launchArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $FilePath) + @($Arguments)
        return [pscustomobject]@{ FilePath = $powerShell; ArgumentLine = (@($launchArguments | ForEach-Object { ConvertTo-GameWipNativeArgument $_ }) -join ' ') }
    }
    return [pscustomobject]@{
        FilePath = $FilePath
        ArgumentLine = (@($Arguments | ForEach-Object { ConvertTo-GameWipNativeArgument $_ }) -join ' ')
    }
}

function Resolve-GameWipOwnedProcessLaunch
{
    param([Parameter(Mandatory = $true)][string]$FilePath, [string[]]$Arguments = @())

    $launch = Resolve-GameWipProcessLaunch -FilePath $FilePath -Arguments $Arguments
    if (Test-GameWipWindowsHost)
    {
        return [pscustomobject]@{ FilePath = $launch.FilePath; ArgumentLine = $launch.ArgumentLine; OwnProcessGroup = $false }
    }
    $setsid = Get-Command -Name setsid -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $setsid)
    {
        return [pscustomobject]@{ FilePath = $launch.FilePath; ArgumentLine = $launch.ArgumentLine; OwnProcessGroup = $false }
    }
    $argumentLine = ConvertTo-GameWipNativeArgument -Argument $launch.FilePath
    if (-not [string]::IsNullOrWhiteSpace([string]$launch.ArgumentLine))
    {
        $argumentLine += " $($launch.ArgumentLine)"
    }
    return [pscustomobject]@{ FilePath = $setsid.Source; ArgumentLine = $argumentLine; OwnProcessGroup = $true }
}

# ------------------------------------------------------------
# Owned process execution
# ------------------------------------------------------------

function Write-GameWipProcessNewOutput
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ref]$LineCount,
        [string]$Prefix = '',
        [string]$PrefixColor
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return
    }
    $lines = @(Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue)
    if ($lines.Count -le $LineCount.Value)
    {
        return
    }
    foreach ($line in @($lines | Select-Object -Skip $LineCount.Value))
    {
        $colorEnabled = -not [string]::IsNullOrWhiteSpace($PrefixColor) -and
        ($null -eq $Script:OperationContext -or -not $Script:OperationContext.NoColor)
        if ($colorEnabled -and -not [string]::IsNullOrEmpty($Prefix))
        {
            Write-GameWipHost $Prefix -NoNewline -ForegroundColor $PrefixColor
            Write-Host $line
        }
        else
        {
            Write-Host "$Prefix$line"
        }
    }
    $LineCount.Value = $lines.Count
}



function Start-GameWipOwnedProcess
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $RepositoryRoot,
        [string]$RedirectStandardOutput,
        [string]$RedirectStandardError
    )
    Assert-GameWipNotCancelled
    $launch = Resolve-GameWipOwnedProcessLaunch -FilePath $FilePath -Arguments $Arguments
    $start = @{
        FilePath = $launch.FilePath
        WorkingDirectory = $WorkingDirectory
        PassThru = $true
        NoNewWindow = $true
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$launch.ArgumentLine))
    {
        $start.ArgumentList = $launch.ArgumentLine
    }
    if ($RedirectStandardOutput)
    {
        $start.RedirectStandardOutput = $RedirectStandardOutput
    }
    if ($RedirectStandardError)
    {
        $start.RedirectStandardError = $RedirectStandardError
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
                Command = ConvertTo-GameWipNativeCommandLine -FilePath $FilePath -Arguments $Arguments
            }) | Out-Null
    }
    return $process
}

function Remove-GameWipOwnedProcessRegistration
{
    param([Parameter(Mandatory = $true)]$Process)
    if ($null -eq $Script:OperationContext)
    {
        return
    }
    for ($index = $Script:OperationContext.ActiveProcesses.Count - 1; $index -ge 0; --$index)
    {
        if ($Script:OperationContext.ActiveProcesses[$index].Process.Id -eq $Process.Id)
        {
            $Script:OperationContext.ActiveProcesses.RemoveAt($index)
        }
    }
}

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
        [string]$LogPath
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

    $temporaryRoot = if ($null -ne $Script:OperationContext -and -not [string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        $Script:OperationContext.Temp
    }
    else
    {
        [IO.Path]::GetTempPath()
    }
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
    $launch = Resolve-GameWipOwnedProcessLaunch -FilePath $FilePath -Arguments $Arguments
    $previousEnvironment = @{}
    foreach ($entry in $Environment.GetEnumerator())
    {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable([string]$entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable([string]$entry.Key, [string]$entry.Value, 'Process')
    }

    $process = $null
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $cancelled = $false
    $timedOut = $false
    $exitCode = -1
    $stdoutLineCount = 0
    $stderrLineCount = 0
    $lastVisibleActivitySeconds = 0.0
    try
    {
        $start = @{
            FilePath = $launch.FilePath
            WorkingDirectory = $WorkingDirectory
            PassThru = $true
            NoNewWindow = $true
            RedirectStandardOutput = $stdoutPath
            RedirectStandardError = $stderrPath
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
            $Script:OperationContext.ActiveProcesses.Add([pscustomobject]@{ Process = $process; Command = $displayCommandLine }) | Out-Null
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
            if ($OutputMode -eq 'Stream')
            {
                $previousLineCount = $stdoutLineCount + $stderrLineCount
                Write-GameWipProcessNewOutput -Path $stdoutPath -LineCount ([ref]$stdoutLineCount)
                Write-GameWipProcessNewOutput -Path $stderrPath -LineCount ([ref]$stderrLineCount) -Prefix '[stderr] ' -PrefixColor Yellow
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
    }

    if ($OutputMode -eq 'Stream')
    {
        Write-GameWipProcessNewOutput -Path $stdoutPath -LineCount ([ref]$stdoutLineCount)
        Write-GameWipProcessNewOutput -Path $stderrPath -LineCount ([ref]$stderrLineCount) -Prefix '[stderr] ' -PrefixColor Yellow
    }

    $stdout = @(if (Test-Path -LiteralPath $stdoutPath)
        {
            Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
        })
    $stderr = @(if (Test-Path -LiteralPath $stderrPath)
        {
            Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue
        })
    $combined = [System.Collections.Generic.List[string]]::new()
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

    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    $resultLogPath = $LogPath
    if ($ephemeralLog)
    {
        Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue
        $resultLogPath = $null
    }
    return [pscustomobject]@{
        FilePath = $FilePath
        Arguments = @($DisplayArguments)
        CommandLine = $displayCommandLine
        ExitCode = $exitCode
        DurationSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
        TimedOut = $timedOut
        Cancelled = $cancelled
        Stdout = @($stdout)
        Stderr = @($stderr)
        LogPath = $resultLogPath
    }
}

function Invoke-GameWipNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string[]]$DisplayArguments,
        [switch]$UseWorkspaceTemp,
        [string]$PathPrefix,
        [hashtable]$Environment = @{},
        [ValidateRange(0, 86400)][int]$TimeoutSeconds = 0,
        [ValidateSet('Summary', 'Stream', 'LogOnly')][string]$OutputMode,
        [int[]]$AllowedExitCodes = @(0)
    )

    Initialize-GameWipRunLog
    if ($null -eq $DisplayArguments)
    {
        $DisplayArguments = @($Arguments)
    }
    $commandLine = ConvertTo-GameWipNativeCommandLine -FilePath $FilePath -Arguments $DisplayArguments
    $step = Initialize-GameWipToolRunStep -Run $Script:OperationContext.Run -Name $Name -CommandLine $commandLine
    $effectiveOutputMode = if ([string]::IsNullOrWhiteSpace([string]$OutputMode))
    {
        if ($null -ne $Script:OperationContext)
        {
            [string]$Script:OperationContext.OutputMode
        }
        else
        {
            'Stream'
        }
    }
    else
    {
        $OutputMode
    }

    if ($effectiveOutputMode -ne 'LogOnly')
    {
        Write-Host ''
        Write-GameWipHost "Starting: $Name" -ForegroundColor Cyan
        Write-Host "> $commandLine"
        Write-Host "  log: $($step.LogPath)"
    }

    $effectiveEnvironment = @{}
    foreach ($entry in $Environment.GetEnumerator())
    {
        $effectiveEnvironment[[string]$entry.Key] = [string]$entry.Value
    }
    if ($UseWorkspaceTemp -and -not $NoWorkspaceTemp -and -not [string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        $effectiveEnvironment.TEMP = $Script:OperationContext.Temp
        $effectiveEnvironment.TMP = $Script:OperationContext.Temp
        Write-Verbose "workspace temp: $Script:OperationContext.Temp"
    }
    if (-not [string]::IsNullOrWhiteSpace($PathPrefix))
    {
        $effectiveEnvironment.PATH = @($PathPrefix, $env:PATH) -join [IO.Path]::PathSeparator
        Write-Verbose "PATH prefix: $PathPrefix"
    }

    try
    {
        $result = Invoke-GameWipProcess `
            -FilePath $FilePath `
            -Arguments $Arguments `
            -DisplayArguments $DisplayArguments `
            -Environment $effectiveEnvironment `
            -TimeoutSeconds $TimeoutSeconds `
            -OutputMode $effectiveOutputMode `
            -SuccessfulExitCodes $AllowedExitCodes `
            -LogPath $step.LogPath
    }
    catch [System.OperationCanceledException]
    {
        Complete-GameWipToolRunStep -Run $Script:OperationContext.Run -Step $step -ExitCode -1 -Status cancelled
        throw
    }
    catch
    {
        Complete-GameWipToolRunStep -Run $Script:OperationContext.Run -Step $step -ExitCode -1 -Status failed
        throw
    }

    Set-GameWipMutationStarted
    $stepStatus = if ($result.Cancelled)
    {
        'cancelled'
    }
    elseif ($result.TimedOut)
    {
        'timed-out'
    }
    elseif ($AllowedExitCodes -contains $result.ExitCode)
    {
        'passed'
    }
    else
    {
        'failed'
    }
    Complete-GameWipToolRunStep -Run $Script:OperationContext.Run -Step $step -ExitCode $result.ExitCode -Status $stepStatus

    if ($result.Cancelled)
    {
        throw [System.OperationCanceledException]::new("$Name was cancelled.")
    }
    if ($result.TimedOut)
    {
        throw (New-GameWipDiagnosticException `
                -Code 'native-timeout' `
                -Summary "$Name timed out after $TimeoutSeconds second(s)." `
                -SuggestedActions @('Inspect the retained step log.', 'Rerun the focused operation after resolving the underlying stall.') `
                -LogPath $step.LogPath)
    }
    if ($AllowedExitCodes -notcontains $result.ExitCode)
    {
        throw (New-GameWipDiagnosticException `
                -Code 'native-failed' `
                -Summary "$Name failed with exit code $($result.ExitCode)." `
                -SuggestedActions @('Inspect the retained step log.', 'Rerun the smallest focused GameWIP command that reproduces the failure.') `
                -LogPath $step.LogPath)
    }
    if ($effectiveOutputMode -ne 'LogOnly')
    {
        Write-GameWipHost "Finished: $Name" -ForegroundColor Green
    }
    return $result
}
