# Operation receipts and guarded execution.

# ------------------------------------------------------------
# Results and receipts
# ------------------------------------------------------------

function Show-GameWipOperationReceipt
{
    param([Parameter(Mandatory = $true)]$Result)

    Write-GameWipSection 'Operation receipt'

    Write-Host '  Status:         ' -NoNewline

    switch ([string]$Result.Status)
    {
        'passed'
        {
            Write-GameWipSemanticText -Object ([string]$Result.Status) -Semantic Success
        }
        'failed'
        {
            Write-GameWipSemanticText -Object ([string]$Result.Status) -Semantic Failure
        }
        'cancelled'
        {
            Write-GameWipSemanticText -Object ([string]$Result.Status) -Semantic Warning
        }
        default
        {
            Write-Host ([string]$Result.Status)
        }
    }

    Write-Host '  Mutation state: ' -NoNewline

    switch ([string]$Result.MutationState)
    {
        'complete'
        {
            Write-GameWipSemanticText -Object ([string]$Result.MutationState) -Semantic Success
        }
        'partial'
        {
            Write-GameWipSemanticText -Object ([string]$Result.MutationState) -Semantic Warning
        }
        'none'
        {
            Write-GameWipSemanticText -Object ([string]$Result.MutationState) -Semantic Muted
        }
        default
        {
            Write-Host ([string]$Result.MutationState)
        }
    }

    Write-Host ('  Duration:       {0:N3}s' -f $Result.DurationSeconds)

    if (-not [string]::IsNullOrWhiteSpace([string]$Result.RunRoot))
    {
        Write-Host '  Run:            ' -NoNewline
        Write-GameWipSemanticText -Object ([string]$Result.RunRoot) -Semantic Muted
    }

    if ($Result.Changes.Count -ne 0)
    {
        Write-GameWipSemanticText -Object '  Changes:' -Semantic Accent

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
        Write-GameWipSemanticText -Object '  Warnings:' -Semantic Warning

        foreach ($item in $Result.Warnings)
        {
            Write-GameWipSemanticText -Object "    - $item" -Semantic Warning
        }
    }

    if ($Result.NextActions.Count -ne 0)
    {
        Write-GameWipSemanticText -Object '  Next:' -Semantic Accent

        foreach ($item in $Result.NextActions)
        {
            Write-Host "    - $item"
        }
    }
}

function New-GameWipOperationResult
{
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)][string]$Status,
        [Parameter(Mandatory = $true)][System.Diagnostics.Stopwatch]$Clock,
        $Failure,
        $FinalizationFailure
    )

    $runRoot = if ($null -ne $Context.Run)
    {
        [string]$Context.Run.Root
    }
    else
    {
        ''
    }

    return [pscustomobject]@{
        # Final status and timing
        Status = $Status
        MutationState = $Context.MutationState
        DurationSeconds = [Math]::Round($Clock.Elapsed.TotalSeconds, 3)

        # Associated run and failures
        RunRoot = $runRoot
        Failure = $Failure
        FinalizationFailure = $FinalizationFailure

        # Recorded outcomes
        Changes = @($Context.Changes)
        Preserved = @($Context.Preserved)
        Warnings = @($Context.Warnings)
        NextActions = @($Context.NextActions)
    }
}

function Show-GameWipOperationResult
{
    param(
        [Parameter(Mandatory = $true)]$Result,
        [switch]$SuppressReceipt
    )

    if ($Result.Status -eq 'failed' -and $null -ne $Result.Failure)
    {
        Show-GameWipActionFailure -ErrorRecord $Result.Failure
    }
    elseif ($Result.Status -eq 'cancelled')
    {
        Write-GameWipSemanticText -Object 'Operation cancelled.' -Semantic Warning
    }

    if ($null -ne $Result.FinalizationFailure)
    {
        Write-Warning "Operation finalization failed: $($Result.FinalizationFailure.Exception.Message)"
    }

    if (-not $SuppressReceipt)
    {
        Show-GameWipOperationReceipt -Result $Result
    }
}
