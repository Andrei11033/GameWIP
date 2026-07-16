Set-StrictMode -Version Latest

function Initialize-GameWipZipCheckout
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)
    if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git')) { return }

    Write-Host '  This is an extracted ZIP. Connecting it to the official Git repository...'
    Push-Location $RepositoryRoot
    try
    {
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('init') | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @(
            'remote', 'add', 'origin', 'https://github.com/Andrei11033/GameWIP.git'
        ) | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('fetch', 'origin', '--prune') | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('remote', 'set-head', 'origin', '--auto') | Out-Null
        $defaultBranch = (& git symbolic-ref --short refs/remotes/origin/HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $defaultBranch)
        {
            throw 'Could not determine the official repository default branch.'
        }

        # Compare the ZIP working tree with each fetched branch by changing only
        # the temporary index. This supports ZIPs downloaded from release branches
        # as well as the default branch, without replacing working files.
        $candidates = @(& git for-each-ref '--format=%(refname:short)' refs/remotes/origin) |
            Where-Object { $_ -ne 'origin/HEAD' }
        $selectedBranch = $defaultBranch
        $fewestChanges = [int]::MaxValue
        foreach ($candidate in @($defaultBranch) + @($candidates | Where-Object { $_ -ne $defaultBranch }))
        {
            & git reset --mixed $candidate 2>$null | Out-Null
            if ($LASTEXITCODE -ne 0) { continue }
            $changeCount = @(& git status --porcelain --untracked-files=no).Count
            if ($changeCount -lt $fewestChanges)
            {
                $selectedBranch = $candidate
                $fewestChanges = $changeCount
            }
            if ($changeCount -eq 0) { break }
        }

        $localBranch = $selectedBranch.Substring('origin/'.Length)
        $commit = (& git rev-parse $selectedBranch).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $commit) { throw "Could not resolve $selectedBranch." }
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('update-ref', "refs/heads/$localBranch", $commit) | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('symbolic-ref', 'HEAD', "refs/heads/$localBranch") | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('reset', '--mixed', $selectedBranch) | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('branch', '--set-upstream-to', $selectedBranch, $localBranch) | Out-Null
        $changes = @(& git status --porcelain --untracked-files=no)
        if ($changes.Count -ne 0)
        {
            Write-Warning 'The ZIP differs from the current default branch. Your files were preserved as local changes.'
        }
        Write-Host "  Ready: ZIP matched and connected to $selectedBranch without replacing local files"
    }
    finally { Pop-Location }
}

function Initialize-GameWipRepository
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    Initialize-GameWipZipCheckout -RepositoryRoot $RepositoryRoot
    Push-Location $RepositoryRoot
    try
    {
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('submodule', 'sync', '--recursive') | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('submodule', 'update', '--init', '--recursive') | Out-Null

        $oldPath = $env:Path
        try
        {
            $env:Path = "C:\MSYS2\ucrt64\bin;$oldPath"
            Invoke-SetupNative -FilePath 'cmake' -ArgumentList @('--preset', 'dev') | Out-Null
        }
        finally
        {
            $env:Path = $oldPath
        }
    }
    finally
    {
        Pop-Location
    }
}

function Update-GameWipRepository
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)
    Push-Location $RepositoryRoot
    try
    {
        $changes = @(& git status --porcelain --untracked-files=no)
        if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the Git working tree.' }
        if ($changes.Count -ne 0)
        {
            throw 'The repository has tracked local changes. Commit or stash them before setup.bat update.'
        }
        $upstream = (& git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null)
        if ($LASTEXITCODE -ne 0 -or -not $upstream)
        {
            throw 'The current branch has no upstream branch; configure one before setup.bat update.'
        }
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('fetch', '--prune') | Out-Null
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('merge', '--ff-only', $upstream.Trim()) | Out-Null
        Write-Host "  Ready: repository fast-forwarded from $($upstream.Trim())"
    }
    finally { Pop-Location }
}

function Test-GameWipRepositoryState
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    Push-Location $RepositoryRoot
    try
    {
        $status = & git submodule status --recursive
        if ($LASTEXITCODE -ne 0 -or ($status | Where-Object { $_ -match '^[-+U]' }))
        {
            throw 'Git submodules are missing, conflicted, or not at the committed revisions.'
        }
    }
    finally
    {
        Pop-Location
    }
}
