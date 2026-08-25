# GameWIP operation lifecycle, cancellation, consent, events, and result contracts.

Set-StrictMode -Version Latest

function Write-GameWipHost
{
    param(
        [AllowEmptyString()][string]$Object = '',
        [ConsoleColor]$ForegroundColor,
        [switch]$NoNewline
    )
    $operationContextVariable = Get-Variable -Name OperationContext -Scope Script -ErrorAction SilentlyContinue
    $noColorVariable = Get-Variable -Name NoColor -Scope Script -ErrorAction SilentlyContinue
    $plainPresentation = ($null -ne $operationContextVariable -and $null -ne $operationContextVariable.Value -and
        [bool]$operationContextVariable.Value.NoColor) -or ($null -ne $noColorVariable -and [bool]$noColorVariable.Value)
    if ($PSBoundParameters.ContainsKey('ForegroundColor') -and -not $plainPresentation)
    {
        Write-Host $Object -ForegroundColor $ForegroundColor -NoNewline:$NoNewline
    }
    else
    {
        Write-Host $Object -NoNewline:$NoNewline
    }
}

function New-GameWipOperationContext
{
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [switch]$NonInteractive,
        [switch]$Yes,
        [switch]$Preview,
        [ValidateSet('Summary', 'Stream', 'LogOnly')][string]$OutputMode = 'Stream',
        [switch]$NoColor
    )

    [pscustomobject]@{
        Id = [guid]::NewGuid().ToString('N')
        Label = $Label
        StartedAt = (Get-Date).ToUniversalTime().ToString('o')
        NonInteractive = [bool]$NonInteractive
        Yes = [bool]$Yes
        Preview = [bool]$Preview
        OutputMode = $OutputMode
        NoColor = [bool]$NoColor
        CancellationRequested = $false
        MutationState = 'none'
        MutationIntent = $false
        ActiveProcesses = [System.Collections.Generic.List[object]]::new()
        Events = [System.Collections.Generic.List[object]]::new()
        Changes = [System.Collections.Generic.List[string]]::new()
        Preserved = [System.Collections.Generic.List[string]]::new()
        Warnings = [System.Collections.Generic.List[string]]::new()
        NextActions = [System.Collections.Generic.List[string]]::new()
        Run = $null
        Temp = $null
        OperationLock = $null
    }
}

function Enter-GameWipOperationLock
{
    if ($null -eq $Script:OperationContext)
    {
        throw 'An operation context is required before acquiring the helper lock.'
    }

    $operationLock = [System.Threading.Mutex]::new($false, 'GameWIP.HelperOperation.v1')
    try
    {
        if (-not $operationLock.WaitOne(0))
        {
            throw (New-GameWipDiagnosticException `
                    -Code 'operation-in-progress' `
                    -Summary 'Another GameWIP helper operation is already running.' `
                    -Details 'Setup and project-helper operations are serialized so checks cannot observe partially updated tools or build state.' `
                    -SuggestedActions @('Wait for the other helper operation to finish, then rerun this command.', 'Inspect the other terminal or its retained run log for progress.'))
        }
    }
    catch [System.Threading.AbandonedMutexException]
    {
        # The previous owner exited without releasing the mutex. WaitOne still
        # transfers ownership, so this operation can safely continue.
        Write-Verbose 'Recovered the operation lock from a terminated helper process.'
    }
    catch
    {
        $operationLock.Dispose()
        throw
    }
    $Script:OperationContext.OperationLock = $operationLock
}

function Exit-GameWipOperationLock
{
    if ($null -eq $Script:OperationContext -or $null -eq $Script:OperationContext.OperationLock)
    {
        return
    }
    try
    {
        $Script:OperationContext.OperationLock.ReleaseMutex()
    }
    finally
    {
        $Script:OperationContext.OperationLock.Dispose()
        $Script:OperationContext.OperationLock = $null
    }
}

function Write-GameWipOperationEvent
{
    param(
        [ValidateSet('discover', 'plan', 'preflight', 'execute', 'verify', 'receipt')][string]$Phase,
        [string]$Step = '',
        [ValidateSet('info', 'warning', 'error', 'success', 'progress')][string]$Severity = 'info',
        [Parameter(Mandatory = $true)][string]$Message,
        [AllowNull()]$Data = $null
    )

    if ($null -ne $Script:OperationContext)
    {
        $Script:OperationContext.Events.Add([pscustomobject]@{
                at = (Get-Date).ToUniversalTime().ToString('o')
                phase = $Phase
                step = $Step
                severity = $Severity
                message = $Message
                data = $Data
            }) | Out-Null
    }

    if ($Severity -eq 'warning')
    {
        Write-Warning $Message
        return
    }
    if ($Severity -eq 'error')
    {
        Write-GameWipHost $Message -ForegroundColor Red
        return
    }
    if ($Severity -eq 'success')
    {
        Write-GameWipHost $Message -ForegroundColor Green
        return
    }
    Write-Host $Message
}

function Add-GameWipOperationChange
{
    param([Parameter(Mandatory = $true)][string]$Message)
    if ($null -eq $Script:OperationContext)
    {
        return
    }
    Set-GameWipMutationStarted
    $Script:OperationContext.Changes.Add($Message) | Out-Null
}

function Add-GameWipOperationPreserved
{
    param([Parameter(Mandatory = $true)][string]$Message)
    if ($null -eq $Script:OperationContext)
    {
        return
    }
    $Script:OperationContext.Preserved.Add($Message) | Out-Null
}

function Add-GameWipOperationWarning
{
    param([Parameter(Mandatory = $true)][string]$Message)
    if ($null -eq $Script:OperationContext)
    {
        return
    }
    $Script:OperationContext.Warnings.Add($Message) | Out-Null
}

function Add-GameWipOperationNextAction
{
    param([Parameter(Mandatory = $true)][string]$Message)
    if ($null -eq $Script:OperationContext)
    {
        return
    }
    $Script:OperationContext.NextActions.Add($Message) | Out-Null
}

function Set-GameWipMutationState
{
    param([ValidateSet('none', 'partial', 'complete')][string]$State)
    if ($null -eq $Script:OperationContext)
    {
        return
    }
    $Script:OperationContext.MutationState = $State
}

function Set-GameWipMutationStarted
{
    if ($null -eq $Script:OperationContext -or -not $Script:OperationContext.MutationIntent)
    {
        return
    }
    if ($Script:OperationContext.MutationState -eq 'none')
    {
        $Script:OperationContext.MutationState = 'partial'
    }
}

function Test-GameWipCancellationRequested
{
    return $null -ne $Script:OperationContext -and [bool]$Script:OperationContext.CancellationRequested
}

function Request-GameWipCancellation
{
    if ($null -ne $Script:OperationContext)
    {
        $Script:OperationContext.CancellationRequested = $true
    }
}

function Assert-GameWipNotCancelled
{
    if (Test-GameWipCancellationRequested)
    {
        throw [System.OperationCanceledException]::new('GameWIP operation was cancelled.')
    }
}

function New-GameWipDiagnosticException
{
    param(
        [Parameter(Mandatory = $true)][string]$Code,
        [Parameter(Mandatory = $true)][string]$Summary,
        [string]$Details = '',
        [string[]]$SuggestedActions = @(),
        [string]$LogPath = ''
    )

    $exception = [System.InvalidOperationException]::new($Summary)
    $exception.Data['GameWipCode'] = $Code
    $exception.Data['GameWipDetails'] = $Details
    $exception.Data['GameWipSuggestedActions'] = @($SuggestedActions)
    $exception.Data['GameWipLogPath'] = $LogPath
    return $exception
}

function Confirm-GameWipMutation
{
    param(
        [Parameter(Mandatory = $true)][string]$Summary,
        [ValidateSet('local', 'tracked', 'machine', 'destructive', 'remote')][string]$Risk = 'local',
        [string[]]$Plan = @(),
        [string]$TypedPhrase = ''
    )

    Write-GameWipOperationEvent -Phase plan -Severity info -Message $Summary
    foreach ($line in $Plan)
    {
        Write-Host "  - $line"
    }

    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)
    {
        Write-GameWipHost 'Preview: mutation is disabled.' -ForegroundColor Cyan
        return $false
    }

    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Yes)
    {
        return $true
    }

    if ($null -ne $Script:OperationContext -and $Script:OperationContext.NonInteractive)
    {
        throw (New-GameWipDiagnosticException `
                -Code 'consent-required' `
                -Summary 'This non-interactive operation requires explicit consent.' `
                -Details "Risk class: $Risk" `
                -SuggestedActions @('Rerun with -Yes after reviewing the printed plan.', 'Use -Preview to inspect the plan without mutation.'))
    }

    if ($Risk -eq 'local')
    {
        return $true
    }

    if (-not [string]::IsNullOrWhiteSpace($TypedPhrase))
    {
        $answer = Read-Host "Type '$TypedPhrase' to continue"
        return $answer -ceq $TypedPhrase
    }

    return Read-GameWipYesNo -Prompt 'Apply this plan?' -Default $false
}

function Stop-GameWipOwnedProcess
{
    param([Parameter(Mandatory = $true)]$Process)

    $previousLastExitCode = $global:LASTEXITCODE
    try
    {
        $ownsProcessGroup = $null -ne $Process.PSObject.Properties['GameWipOwnProcessGroup'] -and [bool]$Process.GameWipOwnProcessGroup
        try
        {
            if ($Process.HasExited -and -not $ownsProcessGroup)
            {
                return
            }
        }
        catch
        {
            return
        }

        try
        {
            if (Test-GameWipWindowsHost)
            {
                & taskkill.exe /PID $Process.Id /T /F *> $null
            }
            else
            {
                if ($ownsProcessGroup)
                {
                    $killCommand = Get-Command -Name kill -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
                    if ($null -ne $killCommand)
                    {
                        & $killCommand.Source -KILL -- ("-$($Process.Id)") 2>$null
                        if ($LASTEXITCODE -eq 0)
                        {
                            return
                        }
                    }
                    if ($Process.HasExited)
                    {
                        return
                    }
                }
                $descendantIds = @(Get-GameWipUnixDescendantProcessId -ParentProcessId $Process.Id)
                Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
                [array]::Reverse($descendantIds)
                foreach ($descendantId in $descendantIds)
                {
                    Stop-Process -Id $descendantId -Force -ErrorAction SilentlyContinue
                }
            }
        }
        catch
        {
            Write-Warning "Could not terminate owned process $($Process.Id): $($_.Exception.Message)"
        }
    }
    finally
    {
        $global:LASTEXITCODE = $previousLastExitCode
    }
}

function Get-GameWipUnixDescendantProcessId
{
    param([Parameter(Mandatory = $true)][int]$ParentProcessId)

    if (Test-GameWipWindowsHost)
    {
        return @()
    }
    $processStatusCommand = Get-Command -Name ps -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $childrenByParent = @{}
    foreach ($line in @(& $processStatusCommand.Source -eo pid=, ppid= 2>$null))
    {
        if ([string]$line -notmatch '^\s*(\d+)\s+(\d+)\s*$')
        {
            continue
        }
        $processId = [int]$Matches[1]
        $parentId = [int]$Matches[2]
        if (-not $childrenByParent.ContainsKey($parentId))
        {
            $childrenByParent[$parentId] = [System.Collections.Generic.List[int]]::new()
        }
        $childrenByParent[$parentId].Add($processId)
    }

    $descendants = [System.Collections.Generic.List[int]]::new()
    $pending = [System.Collections.Generic.Queue[int]]::new()
    $pending.Enqueue($ParentProcessId)
    while ($pending.Count -ne 0)
    {
        $parentId = $pending.Dequeue()
        if (-not $childrenByParent.ContainsKey($parentId))
        {
            continue
        }
        foreach ($childId in $childrenByParent[$parentId])
        {
            $descendants.Add($childId)
            $pending.Enqueue($childId)
        }
    }
    return $descendants.ToArray()
}

function Stop-GameWipOwnedProcesses
{
    if ($null -eq $Script:OperationContext)
    {
        return
    }
    foreach ($entry in @($Script:OperationContext.ActiveProcesses))
    {
        if ($null -ne $entry.Process)
        {
            Stop-GameWipOwnedProcess -Process $entry.Process
        }
    }
    $Script:OperationContext.ActiveProcesses.Clear()
}

function Show-GameWipOperationReceipt
{
    param([Parameter(Mandatory = $true)]$Result)

    Write-Host ''
    Write-Host 'Operation receipt'
    Write-Host '================='
    Write-Host "  Status:         $($Result.Status)"
    Write-Host "  Mutation state: $($Result.MutationState)"
    Write-Host ('  Duration:       {0:N3}s' -f $Result.DurationSeconds)
    if (-not [string]::IsNullOrWhiteSpace([string]$Result.RunRoot))
    {
        Write-Host "  Run:            $($Result.RunRoot)"
    }
    if ($Result.Changes.Count -ne 0)
    {
        Write-Host '  Changes:'
        foreach ($item in $Result.Changes)
        {
            Write-Host "    - $item"
        }
    }
    if ($Result.Preserved.Count -ne 0)
    {
        Write-Host '  Preserved:'
        foreach ($item in $Result.Preserved)
        {
            Write-Host "    - $item"
        }
    }
    if ($Result.Warnings.Count -ne 0)
    {
        Write-Host '  Warnings:'
        foreach ($item in $Result.Warnings)
        {
            Write-GameWipHost "    - $item" -ForegroundColor Yellow
        }
    }
    if ($Result.NextActions.Count -ne 0)
    {
        Write-Host '  Next:'
        foreach ($item in $Result.NextActions)
        {
            Write-Host "    - $item"
        }
    }
}

function Invoke-GameWipOperation
{
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][scriptblock]$ScriptBlock,
        [switch]$NonInteractive,
        [switch]$Yes,
        [switch]$Preview,
        [ValidateSet('Summary', 'Stream', 'LogOnly')][string]$OutputMode = 'Stream',
        [switch]$NoColor,
        [switch]$SuppressReceipt,
        [switch]$SuppressOutput
    )

    if ($null -ne $Script:OperationContext)
    {
        throw 'Nested GameWIP operations are not supported. Invoke the underlying operation function instead.'
    }

    $context = New-GameWipOperationContext `
        -Label $Label `
        -NonInteractive:$NonInteractive `
        -Yes:$Yes `
        -Preview:$Preview `
        -OutputMode $OutputMode `
        -NoColor:$NoColor
    $Script:OperationContext = $context
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $status = 'passed'
    $failure = $null
    $finalizationFailure = $null

    try
    {
        Enter-GameWipOperationLock
        Initialize-GameWipStorage
        $context.Run = Initialize-GameWipToolRun `
            -RepositoryRoot $RepositoryRoot `
            -RunLogRoot $ProjectConfig.storage.runs `
            -Tool 'project-tool' `
            -Action $Label
        $context.Temp = Initialize-GameWipOperationTemp
        # Operation bodies communicate through the shared context and terminal
        # presentation. Suppress incidental return values so callers always
        # receive exactly one operation-result object.
        if ($SuppressOutput)
        {
            $null = & $ScriptBlock 6>$null
        }
        else
        {
            $null = & $ScriptBlock
        }
        if ($context.MutationState -eq 'partial')
        {
            # A mutation body returned normally after recording a partial state.
            $context.MutationState = 'complete'
        }
    }
    catch [System.Management.Automation.PipelineStoppedException]
    {
        Request-GameWipCancellation
        $status = 'cancelled'
        $failure = $_
    }
    catch [System.OperationCanceledException]
    {
        Request-GameWipCancellation
        $status = 'cancelled'
        $failure = $_
    }
    catch
    {
        $status = 'failed'
        $failure = $_
    }
    finally
    {
        Stop-GameWipOwnedProcesses

        try
        {
            Complete-GameWipOperationTemp
        }
        catch
        {
            $status = 'failed'
            $finalizationFailure = $_
            Add-GameWipOperationWarning -Message "Operation-temp cleanup failed: $($_.Exception.Message)"
        }

        try
        {
            if ($null -ne $context.Run)
            {
                $context.Run.MutationState = $context.MutationState
                $context.Run.Changes = @($context.Changes)
                $context.Run.Preserved = @($context.Preserved)
                $context.Run.Warnings = @($context.Warnings)
                $context.Run.NextActions = @($context.NextActions)
                $context.Run.Events = @($context.Events)
                Save-GameWipToolRun -Run $context.Run -Status $status | Out-Null
            }
        }
        catch
        {
            $status = 'failed'
            $finalizationFailure = $_
        }

        Exit-GameWipOperationLock
    }

    $clock.Stop()
    $runRoot = if ($null -ne $context.Run)
    {
        [string]$context.Run.Root
    }
    else
    {
        ''
    }
    $result = [pscustomobject]@{
        Status = $status
        MutationState = $context.MutationState
        DurationSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
        RunRoot = $runRoot
        Failure = $failure
        FinalizationFailure = $finalizationFailure
        Changes = @($context.Changes)
        Preserved = @($context.Preserved)
        Warnings = @($context.Warnings)
        NextActions = @($context.NextActions)
    }

    if ($status -eq 'failed' -and $null -ne $failure)
    {
        Show-GameWipActionFailure -ErrorRecord $failure
    }
    elseif ($status -eq 'cancelled')
    {
        Write-GameWipHost 'Operation cancelled.' -ForegroundColor Yellow
    }
    if ($null -ne $finalizationFailure)
    {
        Write-Warning "Operation finalization failed: $($finalizationFailure.Exception.Message)"
    }
    if (-not $SuppressReceipt)
    {
        Show-GameWipOperationReceipt -Result $result
    }

    $Script:OperationContext = $null
    Remove-Variable -Name RunContext -Scope Script -ErrorAction SilentlyContinue
    Remove-Variable -Name RunLabel -Scope Script -ErrorAction SilentlyContinue
    return $result
}

function Invoke-GameWipMutation
{
    param(
        [Parameter(Mandatory = $true)][string]$Summary,
        [ValidateSet('local', 'tracked', 'machine', 'destructive', 'remote')][string]$Risk = 'local',
        [string[]]$Plan = @(),
        [string]$TypedPhrase = '',
        [Parameter(Mandatory = $true)][scriptblock]$Body
    )

    if (-not (Confirm-GameWipMutation -Summary $Summary -Risk $Risk -Plan $Plan -TypedPhrase $TypedPhrase))
    {
        Add-GameWipOperationPreserved -Message 'No mutation was applied.'
        return $false
    }

    Invoke-GameWipMutationBody -Body $Body
    return $true
}

function Invoke-GameWipMutationBody
{
    param([Parameter(Mandatory = $true)][scriptblock]$Body)

    if ($null -eq $Script:OperationContext)
    {
        & $Body
        return
    }

    $previousIntent = [bool]$Script:OperationContext.MutationIntent
    $Script:OperationContext.MutationIntent = $true
    try
    {
        & $Body
        if ($Script:OperationContext.MutationState -eq 'partial')
        {
            Set-GameWipMutationState -State complete
        }
    }
    finally
    {
        $Script:OperationContext.MutationIntent = $previousIntent
    }
}
