# Cleanup for processes owned by the active operation.

# ------------------------------------------------------------
# Owned process cleanup
# ------------------------------------------------------------

function Stop-GameWipOwnedProcess
{
    param([Parameter(Mandatory = $true)]$Process)

    $lastExitCodeVariable = Get-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
    $previousLastExitCode = if ($null -ne $lastExitCodeVariable)
    {
        $lastExitCodeVariable.Value
    }
    else
    {
        $null
    }

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
        if ($null -ne $lastExitCodeVariable)
        {
            $global:LASTEXITCODE = $previousLastExitCode
        }
        else
        {
            Remove-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
        }
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
