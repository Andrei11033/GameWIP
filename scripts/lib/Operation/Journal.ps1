# Operation events and recorded state.

# ------------------------------------------------------------
# Operation journal
# ------------------------------------------------------------

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
                # Event timing and classification
                at = (Get-Date).ToUniversalTime().ToString('o')
                phase = $Phase
                step = $Step
                severity = $Severity

                # Event content
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
        Write-GameWipSemanticText -Object $Message -Semantic Failure
        return
    }

    if ($Severity -eq 'success')
    {
        Write-GameWipSemanticText -Object $Message -Semantic Success
        return
    }

    if ($Severity -eq 'progress')
    {
        $progressMatch = [regex]::Match(
            $Message,
            '^\[(?<status>[^\]]+)\](?:\s+(?<text>.*))?$'
        )

        if ($progressMatch.Success)
        {
            Write-GameWipStatusLine `
                -Status $progressMatch.Groups['status'].Value `
                -Text $progressMatch.Groups['text'].Value `
                -Semantic Accent
            return
        }

        Write-GameWipSemanticText -Object $Message -Semantic Accent
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
