# Retained-run lookup, display, and cleanup.

# ------------------------------------------------------------
# Run history
# ------------------------------------------------------------

function Get-GameWipRunRoot
{
    return Get-GameWipStorageAreaPath -Area runs
}

function Get-GameWipRetainedRun
{
    $root = Get-GameWipRunRoot

    if (-not (Test-Path -LiteralPath $root))
    {
        return @()
    }

    $activeRunRoot = if ($null -ne $Script:OperationContext -and $null -ne $Script:OperationContext.Run)
    {
        [IO.Path]::GetFullPath([string]$Script:OperationContext.Run.Root)
    }
    else
    {
        ''
    }

    return @(Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { [IO.Path]::GetFullPath($_.FullName) -ne $activeRunRoot } |
            Sort-Object Name -Descending)
}

function Show-GameWipRunList
{
    param([switch]$All)
    Write-GameWipSection 'Retained runs'
    $items = @(Get-GameWipRetainedRun)

    if ($items.Count -eq 0)
    {
        Write-Host '  (none)'
        return
    }

    $displayItems = if ($All)
    {
        $items
    }
    else
    {
        @($items | Select-Object -First 25)
    }

    if (-not $All -and $displayItems.Count -lt $items.Count)
    {
        Write-Host "  Showing the newest $($displayItems.Count) of $($items.Count) runs. Use 'gamewip history list all' for complete history."
        Write-Host ''
    }
    $runWidth = [Math]::Min(60, [Math]::Max(38, [int](@($displayItems | ForEach-Object { $_.Name.Length }) | Measure-Object -Maximum).Maximum))
    $rowFormat = "  {0,-$runWidth} {1,-10} {2}"
    Write-Host ($rowFormat -f 'Run', 'Status', 'Action')
    Write-Host ($rowFormat -f ('-' * $runWidth), ('-' * 10), ('-' * 24))

    foreach ($item in $displayItems)
    {
        $manifest = Join-Path $item.FullName 'manifest.json'
        $status = 'incomplete'
        $action = $item.Name

        if (Test-Path -LiteralPath $manifest -PathType Leaf)
        {
            try
            {
                $doc = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
                $status = [string]$doc.status
                $action = [string]$doc.action
            }

            catch
            {
                $null = $_.Exception
            }
        }
        $displayName = if ($item.Name.Length -le $runWidth)
        {
            $item.Name
        }
        else
        {
            $item.Name.Substring(0, $runWidth - 3) + '...'
        }
        $statusSemantic = switch ($status)
        {
            'passed'
            {
                'Success'
            }
            { $_ -in @('failed', 'timed-out') }
            {
                'Failure'
            }
            { $_ -in @('cancelled', 'incomplete') }
            {
                'Warning'
            }
            'running'
            {
                'Accent'
            }
            default
            {
                'Muted'
            }
        }
        Write-Host ("  {0,-$runWidth} " -f $displayName) -NoNewline
        Write-GameWipSemanticText -Object ('{0,-10}' -f $status) -Semantic $statusSemantic -NoNewline
        Write-Host " $action"
    }
}

function Resolve-GameWipRunSelection
{
    param([Parameter(Mandatory = $true)][string]$Selector)
    $items = @(Get-GameWipRetainedRun)

    if ($items.Count -eq 0)
    {
        throw 'No retained GameWIP runs exist.'
    }

    if ($Selector -eq 'latest')
    {
        return $items[0]
    }

    $match = @($items | Where-Object { $_.Name -eq $Selector })

    if ($match.Count -ne 1)
    {
        throw "Unknown retained run '$Selector'."
    }

    return $match[0]
}

function Show-GameWipRun
{
    param([string]$Selector = 'latest')
    $run = Resolve-GameWipRunSelection -Selector $Selector
    $summary = Join-Path $run.FullName 'summary.txt'

    if (Test-Path -LiteralPath $summary)
    {
        Get-Content -LiteralPath $summary | ForEach-Object { Write-Host $_ }
    }
    else
    {
        Write-GameWipSemanticText -Object 'Retained run has no finalized receipt.' -Semantic Warning
        Write-Host '  Run:  ' -NoNewline
        Write-GameWipSemanticText -Object ([string]$run.FullName) -Semantic Muted
        Write-Host '  Logs: ' -NoNewline
        Write-GameWipSemanticText -Object (Join-Path $run.FullName 'logs') -Semantic Muted
    }
}

function Invoke-GameWipRunCleanup
{
    param([string]$Selector = 'all')
    $root = Get-GameWipRunRoot
    $targets = @(if ($Selector -eq 'all')
        {
            Get-GameWipRetainedRun
        }
        else
        {
            Resolve-GameWipRunSelection -Selector $Selector
        })

    if ($targets.Count -eq 0)
    {
        Write-Host 'No retained runs to clean.'
        return
    }

    $plan = @($targets | ForEach-Object { "Remove retained run $($_.Name)" })

    if (-not (Confirm-GameWipMutation -Summary "Remove $($targets.Count) retained run(s)?" -Risk destructive -Plan $plan))
    {
        return
    }

    Set-GameWipMutationState partial

    foreach ($target in $targets)
    {
        Assert-GameWipSafeChildPath -Path $target.FullName -OwnedRoot $root | Out-Null
        Remove-Item -LiteralPath $target.FullName -Recurse -Force
        Add-GameWipOperationChange -Message "Removed retained run $($target.Name)"
    }

    Set-GameWipMutationState complete
}
