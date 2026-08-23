# GameWIP Git helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Resolve-GameWipRepositoryPath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([IO.Path]::IsPathRooted($Path))
    {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $Path))
}

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

function Get-GameWipCurrentBranch
{
    Assert-GameWipGitRepository
    $branch = (& git branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $branch)
    {
        throw 'Could not determine the current Git branch.'
    }
    return $branch
}

function Assert-GameWipCleanTrackedTree
{
    $changes = @(& git status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Could not inspect the Git working tree.'
    }
    if ($changes.Count -ne 0)
    {
        throw 'Tracked files have local changes. Commit or stash them before switching or updating branches.'
    }
}

function Show-GameWipGitStatus
{
    Assert-GameWipGitRepository
    Write-GameWipSection 'Git workspace status'
    & git status --short --branch
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Could not read Git status.'
    }
}

function Invoke-GameWipGitFetch
{
    Assert-GameWipGitRepository
    Invoke-GameWipNative -Name 'git-fetch-prune' -FilePath 'git' -Arguments @('fetch', '--all', '--prune')
}

function Invoke-GameWipBranchSwitch
{
    param([string]$TargetBranch)
    Assert-GameWipCleanTrackedTree
    Invoke-GameWipGitFetch
    $localBranches = @(& git for-each-ref '--format=%(refname:short)' refs/heads)
    $remoteBranches = @(& git for-each-ref '--format=%(refname:short)' refs/remotes/origin) |
        Where-Object { $_ -like 'origin/*' -and $_ -ne 'origin/HEAD' } |
        ForEach-Object { $_.Substring('origin/'.Length) }
    $branches = @($localBranches + $remoteBranches | Sort-Object -Unique)
    $selected = if ([string]::IsNullOrWhiteSpace($TargetBranch))
    {
        Read-GameWipIndexedChoice -Prompt 'Switch to branch' -Choices $branches
    }
    else
    {
        $TargetBranch -replace '^origin/', ''
    }
    if ($null -eq $selected)
    {
        return
    }
    if ($branches -notcontains $selected)
    {
        throw "Unknown local or origin branch '$selected'."
    }
    if ($selected -eq (Get-GameWipCurrentBranch))
    {
        Write-Host "Already on '$selected'."
        return
    }
    if ($localBranches -contains $selected)
    {
        Invoke-GameWipNative -Name "git-switch-$selected" -FilePath 'git' -Arguments @('switch', $selected)
    }
    else
    {
        Invoke-GameWipNative -Name "git-switch-$selected" -FilePath 'git' -Arguments @('switch', '--track', '-c', $selected, "origin/$selected")
    }
    Show-GameWipGitStatus
}

function Invoke-GameWipCurrentBranchUpdate
{
    Assert-GameWipCleanTrackedTree
    $branch = Get-GameWipCurrentBranch
    $upstream = (& git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $upstream)
    {
        throw "Branch '$branch' has no upstream. Push it with -u or select a tracked remote branch first."
    }
    Invoke-GameWipGitFetch
    Invoke-GameWipNative -Name "git-update-$branch" -FilePath 'git' -Arguments @('merge', '--ff-only', $upstream.Trim())
}

function Invoke-GameWipBranchCreate
{
    param([string]$BranchName)
    Assert-GameWipCleanTrackedTree
    $name = if ([string]::IsNullOrWhiteSpace($BranchName))
    {
        Read-GameWipTextValue -Prompt 'New branch name'
    }
    else
    {
        $BranchName
    }
    if ([string]::IsNullOrWhiteSpace($name))
    {
        Write-Host 'Branch creation cancelled.'
        return
    }
    & git check-ref-format --branch $name 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0)
    {
        throw "'$name' is not a valid Git branch name."
    }
    Invoke-GameWipNative -Name "git-create-$name" -FilePath 'git' -Arguments @('switch', '-c', $name)
    Show-GameWipGitStatus
}

function Invoke-GameWipCurrentBranchPush
{
    $branch = Get-GameWipCurrentBranch
    $upstream = (& git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null)
    if ($LASTEXITCODE -eq 0 -and $upstream)
    {
        Invoke-GameWipNative -Name "git-push-$branch" -FilePath 'git' -Arguments @('push')
        return
    }
    if (-not (Read-GameWipYesNo -Prompt "Publish '$branch' to origin and set its upstream?" -Default $true))
    {
        Write-Host 'Push cancelled.'
        return
    }
    Invoke-GameWipNative -Name "git-publish-$branch" -FilePath 'git' -Arguments @('push', '--set-upstream', 'origin', $branch)
}

function Show-GameWipRecentCommit
{
    Assert-GameWipGitRepository
    Write-GameWipSection 'Recent commits'
    & git log -12 --oneline --decorate --graph
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Could not read Git history.'
    }
}

function Invoke-GameWipBranchCleanup
{
    Assert-GameWipCleanTrackedTree
    Invoke-GameWipGitFetch
    $current = Get-GameWipCurrentBranch
    $defaultRemote = (& git symbolic-ref --short refs/remotes/origin/HEAD 2>$null)
    $defaultName = if ($LASTEXITCODE -eq 0 -and $defaultRemote)
    {
        $defaultRemote.Trim().Substring('origin/'.Length)
    }
    else
    {
        $ProjectConfig.defaultBranch
    }
    $protected = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@($current, $defaultName, 'main', 'master', 'develop'),
        [StringComparer]::OrdinalIgnoreCase
    )
    $merged = @(& git for-each-ref "--merged=origin/$defaultName" '--format=%(refname:short)' refs/heads) |
        Where-Object { -not $protected.Contains($_) }
    $deleted = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($branch in $merged)
    {
        if (Read-GameWipYesNo -Prompt "Delete fully merged local branch '$branch'?" -Default $true)
        {
            Invoke-GameWipNative -Name "git-delete-$branch" -FilePath 'git' -Arguments @('branch', '-d', $branch)
            $deleted.Add($branch) | Out-Null
        }
    }

    $gone = @(& git for-each-ref '--format=%(refname:short)|%(upstream:track)' refs/heads) |
        Where-Object { $_ -match '\|\[gone\]$' } |
        ForEach-Object { ($_ -split '\|', 2)[0] } |
        Where-Object { -not $protected.Contains($_) -and -not $deleted.Contains($_) }
    foreach ($branch in $gone)
    {
        Write-Host "Branch '$branch' has a deleted upstream but is not ancestry-merged; this is common after squash merges." -ForegroundColor Yellow
        if (Read-GameWipYesNo -Prompt "Force-delete local branch '$branch' after confirming its work is preserved remotely?" -Default $false)
        {
            Invoke-GameWipNative -Name "git-force-delete-$branch" -FilePath 'git' -Arguments @('branch', '-D', $branch)
            $deleted.Add($branch) | Out-Null
        }
    }
    if ($deleted.Count -eq 0)
    {
        Write-Host 'No local branches were deleted.'
    }
    else
    {
        Write-Host "Deleted local branches: $($deleted -join ', ')" -ForegroundColor Green
    }
}

function Show-GameWipGitMenu
{
    while ($true)
    {
        Write-Host ''
        Write-Host "Git Workspace ($(Get-GameWipCurrentBranch))"
        Write-Host '============='
        Write-Host '1. Show status'
        Write-Host '2. Fetch and prune remote references'
        Write-Host '3. Switch branch'
        Write-Host '4. Update current branch (fast-forward only)'
        Write-Host '5. Clean merged or gone local branches'
        Write-Host '6. Create and switch to a new branch'
        Write-Host '7. Push/publish the current branch'
        Write-Host '8. Show recent commits'
        Write-Host 'ESC. Back'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::ESCape)
        {
            Write-Host 'ESC'; return
        }
        Write-Host $key.KeyChar
        switch ($key.KeyChar)
        {
            '1'
            {
                Show-GameWipGitStatus
            }
            '2'
            {
                Invoke-GameWipGitFetch
            }
            '3'
            {
                Invoke-GameWipBranchSwitch
            }
            '4'
            {
                Invoke-GameWipCurrentBranchUpdate
            }
            '5'
            {
                Invoke-GameWipBranchCleanup
            }
            '6'
            {
                Invoke-GameWipBranchCreate
            }
            '7'
            {
                Invoke-GameWipCurrentBranchPush
            }
            '8'
            {
                Show-GameWipRecentCommit
            }
            default
            {
                Write-Host 'Press 1-8 or ESC.' -ForegroundColor Yellow
            }
        }
    }
}

function Invoke-GameWipGitAction
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$BranchName
    )
    switch ($Name)
    {
        'menu'
        {
            Show-GameWipGitMenu
        }
        'status'
        {
            Show-GameWipGitStatus
        }
        'fetch'
        {
            Invoke-GameWipGitFetch
        }
        'switch'
        {
            Invoke-GameWipBranchSwitch -TargetBranch $BranchName
        }
        'update'
        {
            Invoke-GameWipCurrentBranchUpdate
        }
        'cleanup'
        {
            Invoke-GameWipBranchCleanup
        }
        'create'
        {
            Invoke-GameWipBranchCreate -BranchName $BranchName
        }
        'push'
        {
            Invoke-GameWipCurrentBranchPush
        }
        'log'
        {
            Show-GameWipRecentCommit
        }
    }
}

function Assert-GameWipGitHubCli
{
    param([Parameter(Mandatory = $true)][string]$WorkflowId)

    if ($null -eq (Get-Command gh -ErrorAction SilentlyContinue))
    {
        throw "GitHub CLI is unavailable. Run '.\setup.bat repair' to install it, then run 'gh auth login'."
    }
    $previousErrorActionPreference = $ErrorActionPreference
    try
    {
        $ErrorActionPreference = 'Continue'
        $authLines = @(& gh auth status --hostname github.com 2>&1)
        $authExitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($authExitCode -ne 0)
    {
        throw "GitHub CLI is not authenticated. Run 'gh auth login --hostname github.com --scopes repo,workflow,project'."
    }
    $authText = $authLines -join [Environment]::NewLine
    if ($authText -match 'Token scopes:' -and ($authText -notmatch "'repo'" -or $authText -notmatch "'workflow'"))
    {
        throw "GitHub authentication lacks repo or workflow scope. Run 'gh auth refresh --hostname github.com --scopes repo,workflow'."
    }
    if ($WorkflowId -like 'project-*' -and $authText -match 'Token scopes:' -and $authText -notmatch "'project'")
    {
        throw "Project automation requires project scope. Run 'gh auth refresh --hostname github.com --scopes project'."
    }
}
