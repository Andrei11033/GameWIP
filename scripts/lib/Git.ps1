# GameWIP Git operations. Query, planning, consent, and mutation are kept separate.

Set-StrictMode -Version Latest

function Assert-GameWipGitRepository
{
    if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git')))
    {
        throw 'Git metadata is missing. Run .\setup.bat repair to prepare this checkout.'
    }
    if ($null -eq (Get-Command git -ErrorAction SilentlyContinue))
    {
        throw 'Git is unavailable. Run .\setup.bat repair to install it.'
    }
}

function Invoke-GameWipGitQuery
{
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    Assert-GameWipGitRepository
    $result = Invoke-GameWipProcess -FilePath 'git' -Arguments (@('-C', $RepositoryRoot) + $Arguments) -OutputMode LogOnly -TimeoutSeconds 30
    if ($result.ExitCode -ne 0)
    {
        throw "Git query failed: git $($Arguments -join ' ')"
    }
    return @($result.Stdout)
}

function Get-GameWipCurrentBranch
{
    $branch = ((Invoke-GameWipGitQuery -Arguments @('branch', '--show-current')) -join '').Trim()
    if ([string]::IsNullOrWhiteSpace($branch))
    {
        throw 'Could not determine the current Git branch.'
    }
    return $branch
}

function Assert-GameWipCleanTrackedTree
{
    $changes = @(Invoke-GameWipGitQuery -Arguments @('status', '--porcelain', '--untracked-files=no'))
    if ($changes.Count -ne 0)
    {
        throw 'Tracked files have local changes. Commit or stash them before switching or updating branches.'
    }
}

function Show-GameWipGitStatus
{
    Write-GameWipSection 'Git workspace status'
    foreach ($line in @(Invoke-GameWipGitQuery -Arguments @('status', '--short', '--branch')))
    {
        Write-Host $line
    }
}

function Invoke-GameWipGitFetch
{
    Invoke-GameWipMutation -Summary 'Fetch and prune Git remote references.' -Risk local -Plan @('git fetch --all --prune') -Body {
        Invoke-GameWipNative -Name 'git-fetch-prune' -FilePath 'git' -Arguments @('-C', $RepositoryRoot, 'fetch', '--all', '--prune')
    } | Out-Null
}

function Get-GameWipBranchNames
{
    $local = @(Invoke-GameWipGitQuery -Arguments @('for-each-ref', '--format=%(refname:short)', 'refs/heads'))
    $remote = @(Invoke-GameWipGitQuery -Arguments @('for-each-ref', '--format=%(refname:short)', 'refs/remotes/origin')) |
        Where-Object { $_ -like 'origin/*' -and $_ -ne 'origin/HEAD' } | ForEach-Object { $_.Substring('origin/'.Length) }
    return [pscustomobject]@{ Local = @($local); All = @($local + $remote | Sort-Object -Unique) }
}

function Invoke-GameWipBranchSwitch
{
    param([string]$TargetBranch)
    Assert-GameWipCleanTrackedTree
    $branches = Get-GameWipBranchNames
    $selected = $TargetBranch -replace '^origin/', ''
    if ([string]::IsNullOrWhiteSpace($selected))
    {
        if ($null -ne $Script:OperationContext -and $Script:OperationContext.NonInteractive)
        {
            throw 'git switch requires a branch in non-interactive mode.'
        }
        $selected = Read-GameWipIndexedChoice -Prompt 'Switch to branch' -Choices $branches.All
    }
    if ([string]::IsNullOrWhiteSpace([string]$selected))
    {
        return
    }
    if ($branches.All -notcontains $selected)
    {
        throw "Unknown local or origin branch '$selected'. Run 'gamewip git fetch' first if needed."
    }
    if ($selected -eq (Get-GameWipCurrentBranch))
    {
        Write-Host "Already on '$selected'."; return
    }
    $gitArguments = if ($branches.Local -contains $selected)
    {
        @('-C', $RepositoryRoot, 'switch', $selected)
    }
    else
    {
        @('-C', $RepositoryRoot, 'switch', '--track', '-c', $selected, "origin/$selected")
    }
    Invoke-GameWipMutation -Summary "Switch Git branch to '$selected'." -Risk local -Plan @((ConvertTo-GameWipNativeCommandLine -FilePath git -Arguments $gitArguments)) -Body {
        Invoke-GameWipNative -Name "git-switch-$selected" -FilePath git -Arguments $gitArguments
    } | Out-Null
    Show-GameWipGitStatus
}

function Invoke-GameWipCurrentBranchUpdate
{
    Assert-GameWipCleanTrackedTree
    $branch = Get-GameWipCurrentBranch
    $upstream = ((Invoke-GameWipGitQuery -Arguments @('rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{upstream}')) -join '').Trim()
    if ([string]::IsNullOrWhiteSpace($upstream))
    {
        throw "Branch '$branch' has no upstream."
    }
    Invoke-GameWipMutation -Summary "Fast-forward '$branch' from '$upstream'." -Risk local -Plan @('Fetch/prune remotes.', "git merge --ff-only $upstream") -Body {
        Invoke-GameWipNative -Name 'git-fetch-prune' -FilePath git -Arguments @('-C', $RepositoryRoot, 'fetch', '--all', '--prune')
        Invoke-GameWipNative -Name "git-update-$branch" -FilePath git -Arguments @('-C', $RepositoryRoot, 'merge', '--ff-only', $upstream)
    } | Out-Null
}

function Invoke-GameWipBranchCreate
{
    param([string]$BranchName)
    Assert-GameWipCleanTrackedTree
    $name = $BranchName
    if ([string]::IsNullOrWhiteSpace($name))
    {
        if ($null -ne $Script:OperationContext -and $Script:OperationContext.NonInteractive)
        {
            throw 'git create requires a branch name in non-interactive mode.'
        }
        $name = Read-GameWipTextValue -Prompt 'New branch name'
    }
    if ([string]::IsNullOrWhiteSpace($name))
    {
        return
    }
    $check = Invoke-GameWipProcess -FilePath git -Arguments @('check-ref-format', '--branch', $name) -OutputMode LogOnly -TimeoutSeconds 10
    if ($check.ExitCode -ne 0)
    {
        throw "'$name' is not a valid Git branch name."
    }
    Invoke-GameWipMutation -Summary "Create and switch to branch '$name'." -Risk local -Plan @("git switch -c $name") -Body {
        Invoke-GameWipNative -Name "git-create-$name" -FilePath git -Arguments @('-C', $RepositoryRoot, 'switch', '-c', $name)
    } | Out-Null
}

function Invoke-GameWipCurrentBranchPush
{
    $branch = Get-GameWipCurrentBranch
    $upstreamResult = Invoke-GameWipProcess -FilePath git -Arguments @('-C', $RepositoryRoot, 'rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{upstream}') -OutputMode LogOnly -TimeoutSeconds 10
    $hasUpstream = $upstreamResult.ExitCode -eq 0 -and $upstreamResult.Stdout.Count -ne 0
    $gitArguments = if ($hasUpstream)
    {
        @('-C', $RepositoryRoot, 'push')
    }
    else
    {
        @('-C', $RepositoryRoot, 'push', '--set-upstream', 'origin', $branch)
    }
    Invoke-GameWipMutation -Summary "Push branch '$branch' to origin." -Risk remote -Plan @((ConvertTo-GameWipNativeCommandLine -FilePath git -Arguments $gitArguments)) -TypedPhrase "push $branch" -Body {
        Invoke-GameWipNative -Name "git-push-$branch" -FilePath git -Arguments $gitArguments
    } | Out-Null
}

function Show-GameWipRecentCommit
{
    Write-GameWipSection 'Recent commits'
    foreach ($line in @(Invoke-GameWipGitQuery -Arguments @('log', '-12', '--oneline', '--decorate', '--graph')))
    {
        Write-Host $line
    }
}

function Invoke-GameWipBranchCleanup
{
    Assert-GameWipCleanTrackedTree
    $current = Get-GameWipCurrentBranch
    $defaultName = [string]$ProjectConfig.defaultBranch
    $protected = [Collections.Generic.HashSet[string]]::new([string[]]@($current, $defaultName, 'main', 'master', 'develop'), [StringComparer]::OrdinalIgnoreCase)
    $merged = @(Invoke-GameWipGitQuery -Arguments @('for-each-ref', "--merged=origin/$defaultName", '--format=%(refname:short)', 'refs/heads')) | Where-Object { -not $protected.Contains($_) }
    $gone = @(Invoke-GameWipGitQuery -Arguments @('for-each-ref', '--format=%(refname:short)|%(upstream:track)', 'refs/heads')) |
        Where-Object { $_ -match '\|\[gone\]$' } | ForEach-Object { ($_ -split '\|', 2)[0] } | Where-Object { -not $protected.Contains($_) -and $merged -notcontains $_ }
    if ($merged.Count -eq 0 -and $gone.Count -eq 0)
    {
        Write-Host 'No cleanup candidates.'; return
    }
    $plan = @($merged | ForEach-Object { "Delete merged local branch: $_" }) + @($gone | ForEach-Object { "Force-delete local branch with gone upstream: $_" })
    Invoke-GameWipMutation -Summary 'Delete reviewed local branch cleanup candidates.' -Risk destructive -Plan $plan -Body {
        foreach ($branch in $merged)
        {
            Invoke-GameWipNative -Name "git-delete-$branch" -FilePath git -Arguments @('-C', $RepositoryRoot, 'branch', '-d', $branch)
        }
        foreach ($branch in $gone)
        {
            Invoke-GameWipNative -Name "git-force-delete-$branch" -FilePath git -Arguments @('-C', $RepositoryRoot, 'branch', '-D', $branch)
        }
    } | Out-Null
}

function Invoke-GameWipGitAction
{
    param([Parameter(Mandatory = $true)][ValidateSet('status', 'fetch', 'switch', 'update', 'cleanup', 'create', 'push', 'log')][string]$Name, [string]$BranchName)
    switch ($Name)
    {
        status
        {
            Show-GameWipGitStatus
        }
        fetch
        {
            Invoke-GameWipGitFetch
        }
        switch
        {
            Invoke-GameWipBranchSwitch -TargetBranch $BranchName
        }
        update
        {
            Invoke-GameWipCurrentBranchUpdate
        }
        cleanup
        {
            Invoke-GameWipBranchCleanup
        }
        create
        {
            Invoke-GameWipBranchCreate -BranchName $BranchName
        }
        push
        {
            Invoke-GameWipCurrentBranchPush
        }
        log
        {
            Show-GameWipRecentCommit
        }
    }
}

function Get-GameWipChangedFile
{
    Assert-GameWipGitRepository
    $paths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($gitArguments in @(
            @('diff', '--name-only', '--diff-filter=ACMR', 'HEAD'),
            @('diff', '--cached', '--name-only', '--diff-filter=ACMR'),
            @('ls-files', '--others', '--exclude-standard')
        ))
    {
        $result = Invoke-GameWipGitQuery -Arguments $gitArguments
        foreach ($line in @($result))
        {
            if (-not [string]::IsNullOrWhiteSpace([string]$line))
            {
                [void]$paths.Add(([string]$line).Trim())
            }
        }
    }
    return @($paths | Sort-Object)
}

function Get-GameWipWorkspaceContext
{
    Assert-GameWipGitRepository
    $branch = Get-GameWipCurrentBranch
    $status = Invoke-GameWipGitQuery -Arguments @('status', '--porcelain')
    return [pscustomobject]@{
        Branch = $branch
        Workspace = $(if (@($status).Count -eq 0)
            {
                'clean'
            }
            else
            {
                'modified'
            })
    }
}
