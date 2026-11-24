# Named native commands and retained step results.

function Invoke-GameWipNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string[]]$DisplayArguments,
        [string]$WorkingDirectory = $RepositoryRoot,
        [switch]$UseWorkspaceTemp,
        [string]$PathPrefix,
        [hashtable]$Environment = @{},
        [ValidateRange(0, 86400)][int]$TimeoutSeconds = 0,
        [ValidateSet('Summary', 'Stream', 'LogOnly')][string]$OutputMode,
        [int[]]$AllowedExitCodes = @(0),
        [switch]$InheritConsole,
        [switch]$PreserveConsoleSession
    )

    # Record the command and its display form before starting the process.
    Initialize-GameWipRunLog

    if ($null -eq $DisplayArguments)
    {
        $DisplayArguments = @($Arguments)
    }
    $commandLine = ConvertTo-GameWipNativeCommandLine `
        -FilePath $FilePath `
        -Arguments $DisplayArguments
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

    # Apply operation-scoped temporary and PATH overrides.
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

    # Run the process and complete its retained step record.
    try
    {
        $result = Invoke-GameWipProcess `
            -FilePath $FilePath `
            -Arguments $Arguments `
            -DisplayArguments $DisplayArguments `
            -WorkingDirectory $WorkingDirectory `
            -Environment $effectiveEnvironment `
            -TimeoutSeconds $TimeoutSeconds `
            -OutputMode $effectiveOutputMode `
            -SuccessfulExitCodes $AllowedExitCodes `
            -LogPath $step.LogPath `
            -InheritConsole:$InheritConsole `
            -PreserveConsoleSession:$PreserveConsoleSession
    }

    catch [System.OperationCanceledException]
    {
        Complete-GameWipToolRunStep `
            -Run $Script:OperationContext.Run `
            -Step $step `
            -ExitCode -1 `
            -Status cancelled
        throw
    }

    catch
    {
        Complete-GameWipToolRunStep `
            -Run $Script:OperationContext.Run `
            -Step $step `
            -ExitCode -1 `
            -Status failed
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
    Complete-GameWipToolRunStep `
        -Run $Script:OperationContext.Run `
        -Step $step `
        -ExitCode $result.ExitCode `
        -Status $stepStatus

    if ($result.Cancelled)
    {
        throw [System.OperationCanceledException]::new("$Name was cancelled.")
    }

    if ($result.TimedOut)
    {
        throw (New-GameWipDiagnosticException `
                -Code 'native-timeout' `
                -Summary "$Name timed out after $TimeoutSeconds second(s)." `
                -SuggestedActions @(
                    'Inspect the retained step log.',
                    'Rerun the focused operation after resolving the underlying stall.'
                ) `
                -LogPath $step.LogPath)
    }

    if ($AllowedExitCodes -notcontains $result.ExitCode)
    {
        throw (New-GameWipDiagnosticException `
                -Code 'native-failed' `
                -Summary "$Name failed with exit code $($result.ExitCode)." `
                -SuggestedActions @(
                    'Inspect the retained step log.',
                    'Rerun the smallest focused GameWIP command that reproduces the failure.'
                ) `
                -LogPath $step.LogPath)
    }

    if ($effectiveOutputMode -ne 'LogOnly')
    {
        Write-GameWipHost "Finished: $Name" -ForegroundColor Green
    }

    return $result
}
