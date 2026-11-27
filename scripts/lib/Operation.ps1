# Operation context, output, and cross-operation locking.

# ------------------------------------------------------------
# Operation lifecycle and result presentation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Operation context and locking
# ------------------------------------------------------------

function Write-GameWipHost
{
    param(
        [AllowEmptyString()][string]$Object = '',
        [ConsoleColor]$ForegroundColor,
        [switch]$NoNewline
    )
    $operationContextVariable = Get-Variable -Name OperationContext -Scope Script -ErrorAction SilentlyContinue
    $noColorVariable = Get-Variable -Name NoColor -Scope Script -ErrorAction SilentlyContinue
    $plainPresentation = (
        ($null -ne $operationContextVariable -and
        $null -ne $operationContextVariable.Value -and
        [bool]$operationContextVariable.Value.NoColor) -or
        ($null -ne $noColorVariable -and [bool]$noColorVariable.Value)
    )

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
        # Operation identity
        Id = [guid]::NewGuid().ToString('N')
        Label = $Label
        StartedAt = (Get-Date).ToUniversalTime().ToString('o')

        # User-selected behavior
        NonInteractive = [bool]$NonInteractive
        Yes = [bool]$Yes
        Preview = [bool]$Preview
        OutputMode = $OutputMode
        NoColor = [bool]$NoColor

        # Cancellation and mutation state
        CancellationRequested = $false
        MutationState = 'none'
        MutationIntent = $false

        # Owned work and retained results
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
                    -SuggestedActions @(
                    'Wait for the other helper operation to finish, then rerun this command.',
                    'Inspect the other terminal or its retained run log for progress.'
                ))
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
