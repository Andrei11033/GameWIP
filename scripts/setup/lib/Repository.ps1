Set-StrictMode -Version Latest

function Select-GameWipZipBranch
{
    param(
        [Parameter(Mandatory = $true)][string[]]$Candidates,
        [Parameter(Mandatory = $true)][string]$Recommended
    )

    Write-Host ''
    Write-Host "The extracted files most closely match '$Recommended'."
    Write-Host 'Choose the repository branch this checkout should track:'
    for ($index = 0; $index -lt $Candidates.Count; ++$index)
    {
        $suffix = if ($Candidates[$index] -eq $Recommended) { ' (detected, recommended)' } else { '' }
        Write-Host ("  [{0}] {1}{2}" -f ($index + 1), $Candidates[$index], $suffix)
    }
    while ($true)
    {
        $choice = Read-Host "Selection [Enter = $Recommended, Q = cancel]"
        if ([string]::IsNullOrWhiteSpace($choice)) { return $Recommended }
        if ($choice -eq 'q' -or $choice -eq 'Q') { throw 'Repository branch selection was cancelled.' }
        $number = 0
        if ([int]::TryParse($choice, [ref]$number) -and $number -ge 1 -and $number -le $Candidates.Count)
        {
            return $Candidates[$number - 1]
        }
        Write-Host 'Enter one of the listed numbers, Enter, or Q.' -ForegroundColor Yellow
    }
}

function Initialize-GameWipZipCheckout
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$Branch,
        [switch]$ChooseBranch
    )
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
            Where-Object { $_ -like 'origin/*' -and $_ -ne 'origin/HEAD' }
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

        $candidateNames = @($candidates | ForEach-Object { $_.Substring('origin/'.Length) } | Sort-Object -Unique)
        $recommendedName = $selectedBranch.Substring('origin/'.Length)
        if (-not [string]::IsNullOrWhiteSpace($Branch))
        {
            $requestedName = $Branch -replace '^origin/', ''
            if ($candidateNames -notcontains $requestedName)
            {
                throw "Unknown remote branch '$Branch'. Available branches: $($candidateNames -join ', ')"
            }
            $selectedBranch = "origin/$requestedName"
        }
        elseif ($ChooseBranch)
        {
            $selectedName = Select-GameWipZipBranch -Candidates $candidateNames -Recommended $recommendedName
            $selectedBranch = "origin/$selectedName"
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

function Set-GameWipRepositoryBranch
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$Branch,
        [switch]$ChooseBranch
    )
    if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))) { return }
    if ([string]::IsNullOrWhiteSpace($Branch) -and -not $ChooseBranch) { return }

    Push-Location $RepositoryRoot
    try
    {
        $current = (& git branch --show-current).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $current) { throw 'Could not determine the current repository branch.' }
        Invoke-SetupNative -FilePath 'git' -ArgumentList @('fetch', 'origin', '--prune') | Out-Null
        $remoteBranches = @(& git for-each-ref '--format=%(refname:short)' refs/remotes/origin) |
            Where-Object { $_ -like 'origin/*' -and $_ -ne 'origin/HEAD' } |
            ForEach-Object { $_.Substring('origin/'.Length) } |
            Sort-Object -Unique
        if ($remoteBranches.Count -eq 0) { throw 'The origin remote has no branches.' }
        $localBranches = @(& git for-each-ref '--format=%(refname:short)' refs/heads)
        $branches = @($current) + @($localBranches) + @($remoteBranches) | Sort-Object -Unique

        $selected = if (-not [string]::IsNullOrWhiteSpace($Branch)) {
            $Branch -replace '^origin/', ''
        } else {
            Select-GameWipZipBranch -Candidates $branches -Recommended $current
        }
        if ($branches -notcontains $selected)
        {
            throw "Unknown remote branch '$selected'. Available branches: $($branches -join ', ')"
        }
        if ($selected -eq $current)
        {
            Write-Host "  Ready: keeping current branch '$current'"
            return
        }
        $changes = @(& git status --porcelain --untracked-files=no)
        if ($changes.Count -ne 0)
        {
            throw 'Tracked files have local changes. Commit or stash them before selecting another setup branch.'
        }
        if ($localBranches -contains $selected)
        {
            Invoke-SetupNative -FilePath 'git' -ArgumentList @('switch', $selected) | Out-Null
        }
        else
        {
            if ($remoteBranches -notcontains $selected) { throw "Branch '$selected' is not available locally or on origin." }
            Invoke-SetupNative -FilePath 'git' -ArgumentList @('switch', '--track', '-c', $selected, "origin/$selected") | Out-Null
        }
        Write-Host "  Ready: switched repository to '$selected'"
    }
    finally { Pop-Location }
}

function Initialize-GameWipRepository
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$Branch,
        [switch]$ChooseBranch
    )

    Initialize-GameWipZipCheckout -RepositoryRoot $RepositoryRoot -Branch $Branch -ChooseBranch:$ChooseBranch
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
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [switch]$SkipFetch
    )
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
        if (-not $SkipFetch)
        {
            Invoke-SetupNative -FilePath 'git' -ArgumentList @('fetch', '--prune') | Out-Null
        }
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
