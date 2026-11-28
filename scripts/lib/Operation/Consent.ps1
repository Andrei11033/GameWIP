# Cancellation, diagnostics, and mutation consent.

# ------------------------------------------------------------
# Cancellation and confirmation
# ------------------------------------------------------------

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

    # Present the complete plan before applying consent policy.
    Write-GameWipOperationEvent -Phase plan -Severity info -Message $Summary

    foreach ($line in $Plan)
    {
        Write-Host "  - $line"
    }

    # Preview and explicit approval take precedence over interactive prompts.
    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)
    {
        Write-GameWipSemanticText -Object 'Preview: mutation is disabled.' -Semantic Accent
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

    # Local changes need no prompt; higher-risk changes require confirmation.
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

function Confirm-GameWipExternalInstall
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Provider,
        [Parameter(Mandatory = $true)][string]$Package
    )

    $message = "Install $Name through $Provider package '$Package' outside build/?"
    Write-GameWipOperationEvent -Phase plan -Step external-install -Severity info -Message $message

    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)
    {
        return $false
    }

    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Yes)
    {
        return $true
    }

    if ($null -ne $Script:OperationContext -and $Script:OperationContext.NonInteractive)
    {
        return $false
    }

    return Read-GameWipYesNo -Prompt $message -Default $false
}
