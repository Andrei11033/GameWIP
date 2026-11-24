# Shared interactive context, menu primitives, and operation state.

# ------------------------------------------------------------
# Interactive context and menu dispatch
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Invoke-GameWipInteractiveOperation
{
    param([Parameter(Mandatory = $true)][string]$Label, [Parameter(Mandatory = $true)][scriptblock]$Body)
    $result = Invoke-GameWipOperation `
        -Label $Label `
        -ScriptBlock $Body `
        -NonInteractive:$NonInteractive `
        -Yes:$Yes `
        -Preview:$Preview `
        -OutputMode $OutputMode `
        -NoColor:$NoColor
    return $result
}

function Read-GameWipNamedChoice
{
    param([string]$Prompt, [string[]]$Choices, [string]$Default, [switch]$AllowMultiple)
    $result = if ($AllowMultiple)
    {
        Read-GameWipIndexedChoiceResult -Prompt $Prompt -Choices $Choices -Default $Default -AllowMultiple
    }
    else
    {
        Read-GameWipMenuChoiceResult -Prompt $Prompt -Choices $Choices -Default $Default
    }

    if ($result.Status -eq 'Cancelled')
    {
        return $null
    }

    return [string]$result.Value
}

function Get-GameWipInteractiveContext
{
    $branch = '<none>'
    $workspace = 'unknown'

    try
    {
        if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
        {
            $branch = Get-GameWipCurrentBranch
            $changes = @(Invoke-GameWipGitQuery -Arguments @('status', '--porcelain', '--untracked-files=no'))
            $workspace = if ($changes.Count -eq 0)
            {
                'clean'
            }
            else
            {
                'modified'
            }
        }
    }

    catch
    {
        $workspace = 'degraded'
    }

    $environment = 'degraded'

    try
    {
        $cmake = Get-GameWipProjectTool -Id 'cmake'

        if ($null -ne (Resolve-GameWipToolCommand -Tool $cmake))
        {
            $environment = 'ready'
        }
    }

    catch
    {
        $null = $_.Exception
    }

    return [pscustomobject]@{ Branch = $branch; Workspace = $workspace; Environment = $environment }
}

function Write-GameWipInteractiveHeader
{
    $context = Get-GameWipInteractiveContext
    $workspaceSemantic = switch ([string]$context.Workspace)
    {
        'clean'
        {
            'Success'
        }
        'modified'
        {
            'Warning'
        }
        'degraded'
        {
            'Failure'
        }
        default
        {
            'Muted'
        }
    }
    $environmentSemantic = switch ([string]$context.Environment)
    {
        'ready'
        {
            'Success'
        }
        'degraded'
        {
            'Failure'
        }
        default
        {
            'Muted'
        }
    }

    Write-Host 'Branch: ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$context.Branch) -Semantic Accent -NoNewline
    Write-Host '  Workspace: ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$context.Workspace) -Semantic $workspaceSemantic -NoNewline
    Write-Host '  Environment: ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$context.Environment) -Semantic $environmentSemantic
}

function Read-GameWipConfiguredMenuItem
{
    param([Parameter(Mandatory = $true)][string]$MenuId, [scriptblock]$Header)

    $menuMatches = @($CommandConfig.Menus | Where-Object { [string]$_.Id -eq $MenuId })

    if ($menuMatches.Count -ne 1)
    {
        throw "Interactive menu '$MenuId' is not registered exactly once."
    }

    $menu = $menuMatches[0]
    return Read-GameWipActionMenuItem `
        -Title ([string]$menu.Title) `
        -Prompt ([string]$menu.Prompt) `
        -Items @($menu.Items) `
        -ExitLabel ([string]$menu.ExitLabel) `
        -Header $Header
}
