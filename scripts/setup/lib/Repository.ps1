# GameWIP setup repository initialization, branch selection, update, submodule, and dev configuration.

Set-StrictMode -Version Latest

function Invoke-GameWipSetupGitQuery
{
    param([Parameter(Mandatory = $true)][string[]]$Arguments, [int[]]$AllowedExitCodes = @(0))
    $result = Invoke-GameWipProcess -FilePath git -Arguments (@('-C', $RepositoryRoot) + $Arguments) -OutputMode LogOnly -TimeoutSeconds 60
    if ($AllowedExitCodes -notcontains $result.ExitCode)
    {
        throw "Git query failed: git $($Arguments -join ' ')"
    }
    return $result
}

function Select-GameWipZipBranch
{
    param([Parameter(Mandatory = $true)][string[]]$Candidates, [Parameter(Mandatory = $true)][string]$Recommended)
    $result = Read-GameWipMenuChoiceResult -Prompt 'Repository branch' -Choices $Candidates -Default $Recommended
    if ($result.Status -eq 'Cancelled')
    {
        throw 'Repository branch selection was cancelled.'
    }
    return [string]$result.Value
}

function Initialize-GameWipZipCheckout
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot, [string]$Branch, [switch]$ChooseBranch)
    if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
    {
        return
    }

    Write-Host '  Extracted ZIP detected; connecting files to the official Git repository without replacing them.'
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'init') | Out-Null
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'remote', 'add', 'origin', 'https://github.com/Andrei11033/GameWIP.git') | Out-Null
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'fetch', 'origin', '--prune') | Out-Null
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'remote', 'set-head', 'origin', '--auto') | Out-Null

    $defaultResult = Invoke-GameWipSetupGitQuery -Arguments @('symbolic-ref', '--short', 'refs/remotes/origin/HEAD')
    $defaultBranch = ($defaultResult.Stdout -join '').Trim()
    if (-not $defaultBranch)
    {
        throw 'Could not determine the repository default branch.'
    }
    $candidateResult = Invoke-GameWipSetupGitQuery -Arguments @('for-each-ref', '--format=%(refname:short)', 'refs/remotes/origin')
    $candidates = @($candidateResult.Stdout | Where-Object { $_ -like 'origin/*' -and $_ -ne 'origin/HEAD' })

    $recommended = $defaultBranch
    $fewestChanges = [int]::MaxValue
    foreach ($candidate in @($defaultBranch) + @($candidates | Where-Object { $_ -ne $defaultBranch }))
    {
        $reset = Invoke-GameWipProcess -FilePath git -Arguments @('-C', $RepositoryRoot, 'reset', '--mixed', $candidate) -OutputMode LogOnly -TimeoutSeconds 60
        if ($reset.ExitCode -ne 0)
        {
            continue
        }
        $status = Invoke-GameWipSetupGitQuery -Arguments @('status', '--porcelain', '--untracked-files=no')
        if ($status.Stdout.Count -lt $fewestChanges)
        {
            $fewestChanges = $status.Stdout.Count; $recommended = $candidate
        }
        if ($fewestChanges -eq 0)
        {
            break
        }
    }

    $names = @($candidates | ForEach-Object { $_.Substring('origin/'.Length) } | Sort-Object -Unique)
    $recommendedName = $recommended.Substring('origin/'.Length)
    $selectedName = if (-not [string]::IsNullOrWhiteSpace($Branch))
    {
        $Branch -replace '^origin/', ''
    }
    elseif ($ChooseBranch)
    {
        Select-GameWipZipBranch -Candidates $names -Recommended $recommendedName
    }
    else
    {
        $recommendedName
    }
    if ($names -notcontains $selectedName)
    {
        throw "Unknown remote branch '$selectedName'."
    }
    $selectedRemote = "origin/$selectedName"
    $commitResult = Invoke-GameWipSetupGitQuery -Arguments @('rev-parse', $selectedRemote)
    $commit = ($commitResult.Stdout -join '').Trim()
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'update-ref', "refs/heads/$selectedName", $commit) | Out-Null
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'symbolic-ref', 'HEAD', "refs/heads/$selectedName") | Out-Null
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'reset', '--mixed', $selectedRemote) | Out-Null
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'branch', '--set-upstream-to', $selectedRemote, $selectedName) | Out-Null
    Add-GameWipOperationChange -Message "Connected ZIP checkout to $selectedRemote without replacing local files."
}

function Switch-GameWipRepositoryBranch
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot, [string]$Branch, [switch]$ChooseBranch)
    if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git')))
    {
        return
    }
    if ([string]::IsNullOrWhiteSpace($Branch) -and -not $ChooseBranch)
    {
        return
    }
    $current = ((Invoke-GameWipSetupGitQuery -Arguments @('branch', '--show-current')).Stdout -join '').Trim()
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'fetch', 'origin', '--prune') | Out-Null
    $remote = @((Invoke-GameWipSetupGitQuery -Arguments @('for-each-ref', '--format=%(refname:short)', 'refs/remotes/origin')).Stdout | Where-Object { $_ -like 'origin/*' -and $_ -ne 'origin/HEAD' } | ForEach-Object { $_.Substring('origin/'.Length) } | Sort-Object -Unique)
    $local = @((Invoke-GameWipSetupGitQuery -Arguments @('for-each-ref', '--format=%(refname:short)', 'refs/heads')).Stdout)
    $branches = @($current) + $local + $remote | Sort-Object -Unique
    $selected = if (-not [string]::IsNullOrWhiteSpace($Branch))
    {
        $Branch -replace '^origin/', ''
    }
    else
    {
        Select-GameWipZipBranch -Candidates $branches -Recommended $current
    }
    if ($branches -notcontains $selected)
    {
        throw "Unknown repository branch '$selected'."
    }
    if ($selected -eq $current)
    {
        return
    }
    $changes = @((Invoke-GameWipSetupGitQuery -Arguments @('status', '--porcelain', '--untracked-files=no')).Stdout)
    if ($changes.Count -ne 0)
    {
        throw 'Tracked files have local changes. Commit or stash before switching setup branches.'
    }
    if ($local -contains $selected)
    {
        Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'switch', $selected) | Out-Null
    }
    else
    {
        Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'switch', '--track', '-c', $selected, "origin/$selected") | Out-Null
    }
    Add-GameWipOperationChange -Message "Switched repository branch to '$selected'."
}

function Initialize-GameWipRepository
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot, [string]$Branch, [switch]$ChooseBranch)
    Initialize-GameWipZipCheckout -RepositoryRoot $RepositoryRoot -Branch $Branch -ChooseBranch:$ChooseBranch
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'submodule', 'sync', '--recursive') | Out-Null
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'submodule', 'update', '--init', '--recursive') | Out-Null
    $ucrtBin = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin'
    Invoke-GameWipNative -Name 'setup-configure-dev' -FilePath cmake -Arguments @('--preset', 'dev') -PathPrefix $ucrtBin | Out-Null
}

function Invoke-GameWipRepositoryUpdate
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot, [switch]$SkipFetch)
    $changes = @((Invoke-GameWipSetupGitQuery -Arguments @('status', '--porcelain', '--untracked-files=no')).Stdout)
    if ($changes.Count -ne 0)
    {
        throw 'Tracked files have local changes. Commit or stash before setup update.'
    }
    $upstreamResult = Invoke-GameWipProcess -FilePath git -Arguments @('-C', $RepositoryRoot, 'rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{upstream}') -OutputMode LogOnly -TimeoutSeconds 30
    if ($upstreamResult.ExitCode -ne 0 -or $upstreamResult.Stdout.Count -eq 0)
    {
        throw 'Current branch has no upstream branch.'
    }
    $upstream = ($upstreamResult.Stdout -join '').Trim()
    if (-not $SkipFetch)
    {
        Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'fetch', '--prune') | Out-Null
    }
    Invoke-GameWipSetupNative -FilePath git -ArgumentList @('-C', $RepositoryRoot, 'merge', '--ff-only', $upstream) | Out-Null
    Add-GameWipOperationChange -Message "Fast-forwarded repository from $upstream."
}

function Test-GameWipRepositoryState
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)
    $result = Invoke-GameWipProcess -FilePath git -Arguments @('-C', $RepositoryRoot, 'submodule', 'status', '--recursive') -OutputMode LogOnly -TimeoutSeconds 60
    if ($result.ExitCode -ne 0 -or @($result.Stdout | Where-Object { $_ -match '^[-+U]' }).Count -ne 0)
    {
        throw 'Git submodules are missing, conflicted, or not at committed revisions.'
    }
}
