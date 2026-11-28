# Operation execution and mutation handling.

# ------------------------------------------------------------
# Operation execution
# ------------------------------------------------------------

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

    # Create the shared context and start operation timing.
    $context = New-GameWipOperationContext `
        -Label $Label `
        -NonInteractive:$NonInteractive `
        -Yes:$Yes `
        -Preview:$Preview `
        -OutputMode $OutputMode `
        -NoColor:$NoColor
    $Script:OperationContext = $context
    $clock = [System.Diagnostics.Stopwatch]::StartNew()

    # Keep body and finalization failures distinct.
    $status = 'passed'
    $failure = $null
    $finalizationFailure = $null

    try
    {
        # Acquire operation-wide resources before invoking the body.
        Enter-GameWipOperationLock
        Initialize-GameWipStorage
        $context.Run = Initialize-GameWipToolRun `
            -RepositoryRoot $RepositoryRoot `
            -RunLogRoot (Get-GameWipStorageAreaPath -Area runs) `
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
        # Stop owned processes, remove temporary state, and persist the receipt
        # even when the body fails or cancellation interrupts the pipeline.
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

                if ($null -ne $failure)
                {
                    $context.Run.Failure = ConvertTo-GameWipRunFailure -ErrorRecord $failure
                }

                if ($null -ne $finalizationFailure)
                {
                    $context.Run.FinalizationFailure = ConvertTo-GameWipRunFailure -ErrorRecord $finalizationFailure
                }

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

    # Build and present one stable result after all finalization work is complete.
    $clock.Stop()
    $result = New-GameWipOperationResult `
        -Context $context `
        -Status $status `
        -Clock $clock `
        -Failure $failure `
        -FinalizationFailure $finalizationFailure
    Show-GameWipOperationResult -Result $result -SuppressReceipt:$SuppressReceipt

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
