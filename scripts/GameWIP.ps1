[CmdletBinding()]
param(
    [ValidateSet('menu', 'doctor', 'git', 'workflow', 'configure', 'build', 'test', 'module', 'wizard', 'stress', 'run', 'bundle', 'docs', 'analysis', 'coverage', 'asan', 'benchmark', 'list', 'help')]
    [string]$Action = 'menu',
    [string]$Preset,
    [string]$Module,
    [string]$ProjectCommand,
    [string]$Bundle,
    [ValidateSet('menu', 'status', 'fetch', 'switch', 'update', 'cleanup', 'create', 'push', 'log')]
    [string]$GitAction = 'menu',
    [string]$GitBranch,
    [ValidateSet('menu', 'list', 'status', 'run')]
    [string]$WorkflowAction = 'menu',
    [string]$Workflow,
    [ValidateSet('all', 'issue', 'pull_request')]
    [string]$WorkflowKind = 'all',
    [int]$WorkflowNumber = 0,
    [string]$ReleaseCommit,
    [ValidateRange(1, 100000)]
    [int]$Count = 0,
    [ValidateRange(1, 256)]
    [int]$Parallel = 0,
    [string[]]$ExtraArgs = @(),
    [switch]$BuildIfMissing,
    [switch]$NoWorkspaceTemp,
    [switch]$Preview
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$CommandConfigPath = Join-Path $PSScriptRoot 'config\gamewip-commands.psd1'
$CommandConfig = Import-PowerShellDataFile $CommandConfigPath
$PresetsPath = Join-Path $RepositoryRoot 'CMakePresets.json'
$PresetData = Get-Content -Raw -LiteralPath $PresetsPath | ConvertFrom-Json

$Script:RunRoot = $null
$Script:StepIndex = 0
$Script:StepResults = New-Object System.Collections.Generic.List[object]

function Write-GameWipSection
{
    param([Parameter(Mandatory = $true)][string]$Title)

    Write-Host ''
    Write-Host $Title
    Write-Host ('=' * $Title.Length)
}

function Get-VisiblePresetNames
{
    param([Parameter(Mandatory = $true)][string]$Kind)

    $property = switch ($Kind)
    {
        'configure' { 'configurePresets' }
        'build' { 'buildPresets' }
        'test' { 'testPresets' }
    }

    @($PresetData.$property | Where-Object { -not ($_.PSObject.Properties.Name -contains 'hidden' -and $_.hidden) } | ForEach-Object { $_.name })
}

function Assert-ValidPreset
{
    param(
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ((Get-VisiblePresetNames -Kind $Kind) -notcontains $Name)
    {
        throw "Unknown $Kind preset '$Name'. Run 'gamewip list' to see available presets."
    }
}

function Assert-ValidModule
{
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($Name -ne 'all' -and @($CommandConfig.Modules) -notcontains $Name)
    {
        throw "Unknown validation module '$Name'. Run 'gamewip list' to see available modules."
    }
}

function Get-ProjectCommand
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $command = @($CommandConfig.ProjectCommands | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($command.Count -eq 0)
    {
        throw "Unknown project command '$Id'. Run 'gamewip list' to see available commands."
    }
    $command[0]
}

function Get-ProjectBundle
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $bundleInfo = @($CommandConfig.Bundles | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($bundleInfo.Count -eq 0)
    {
        throw "Unknown bundle '$Id'. Run 'gamewip list' to see available bundles."
    }
    $bundleInfo[0]
}

function ConvertTo-SafeName
{
    param([Parameter(Mandatory = $true)][string]$Text)

    $safe = $Text -replace '[^A-Za-z0-9_.-]+', '_'
    if ([string]::IsNullOrWhiteSpace($safe))
    {
        return 'step'
    }
    $safe.Trim('_')
}

function ConvertTo-NativeArgument
{
    param([AllowEmptyString()][string]$Argument)

    if ($null -eq $Argument)
    {
        return '""'
    }
    if ($Argument.Length -eq 0)
    {
        return '""'
    }
    if ($Argument -notmatch '[\s"&|<>^()%!]')
    {
        return $Argument
    }

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray())
    {
        if ($character -eq '\')
        {
            ++$backslashes
            continue
        }
        if ($character -eq '"')
        {
            [void]$builder.Append('\' * (($backslashes * 2) + 1))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }

        if ($backslashes -gt 0)
        {
            [void]$builder.Append('\' * $backslashes)
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashes -gt 0)
    {
        [void]$builder.Append('\' * ($backslashes * 2))
    }
    [void]$builder.Append('"')
    $builder.ToString()
}

function ConvertTo-NativeCommandLine
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    (@(ConvertTo-NativeArgument $FilePath) + @($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ })) -join ' '
}

function Initialize-RunLog
{
    if ($null -ne $Script:RunRoot)
    {
        return
    }

    $timestamp = Get-Date -Format 'yyyy-MM-dd_HHmmss_fff'
    $Script:RunRoot = Join-Path $RepositoryRoot (Join-Path $CommandConfig.RunLogRoot $timestamp)
    New-Item -ItemType Directory -Force -Path $Script:RunRoot | Out-Null
    Write-Host "Run logs: $Script:RunRoot"
}

function Add-StepResult
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $Script:StepResults.Add([pscustomobject]@{
        Name = $Name
        ExitCode = $ExitCode
        LogPath = $LogPath
    }) | Out-Null
}

function Save-RunSummary
{
    if ($null -eq $Script:RunRoot)
    {
        return
    }

    $summaryPath = Join-Path $Script:RunRoot 'summary.txt'
    $jsonPath = Join-Path $Script:RunRoot 'summary.json'
    $failed = @($Script:StepResults | Where-Object { $_.ExitCode -ne 0 })

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("GameWIP project tool summary") | Out-Null
    $lines.Add("Generated: $(Get-Date -Format o)") | Out-Null
    $lines.Add("Repository: $RepositoryRoot") | Out-Null
    $lines.Add("Failed steps: $($failed.Count)") | Out-Null
    $lines.Add('') | Out-Null
    foreach ($result in $Script:StepResults)
    {
        $status = if ($result.ExitCode -eq 0) { 'PASS' } else { 'FAIL' }
        $lines.Add(('{0} {1} exit={2} log={3}' -f $status, $result.Name, $result.ExitCode, $result.LogPath)) | Out-Null
    }

    $lines | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    $Script:StepResults | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
    Write-Host "Summary: $summaryPath"
}

function Invoke-GameWipNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [switch]$UseWorkspaceTemp,
        [string]$PathPrefix
    )

    Initialize-RunLog
    ++$Script:StepIndex
    $safeName = ConvertTo-SafeName $Name
    $logPath = Join-Path $Script:RunRoot ('step_{0:D3}_{1}.log' -f $Script:StepIndex, $safeName)
    $commandLine = ConvertTo-NativeCommandLine -FilePath $FilePath -Arguments $Arguments

    Write-Host ''
    Write-Host "Starting: $Name" -ForegroundColor Cyan
    Write-Host "> $commandLine"
    Write-Host "  log: $logPath"

    $previousLocation = Get-Location
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $previousPath = $env:PATH
    $exitCode = 0
    $previousErrorActionPreference = $ErrorActionPreference

    try
    {
        Set-Location $RepositoryRoot
        $ErrorActionPreference = 'Continue'
        if ($UseWorkspaceTemp -and -not $NoWorkspaceTemp)
        {
            $tempRoot = Join-Path $RepositoryRoot $CommandConfig.WorkspaceTemp
            New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
            $env:TEMP = $tempRoot
            $env:TMP = $tempRoot
            Write-Host "  workspace temp: $tempRoot"
        }
        if (-not [string]::IsNullOrWhiteSpace($PathPrefix))
        {
            $env:PATH = "$PathPrefix;$env:PATH"
            Write-Host "  PATH prefix: $PathPrefix"
        }

        & $FilePath @Arguments 2>&1 | Tee-Object -FilePath $logPath
        $exitCode = if ($null -ne $LASTEXITCODE) { [int]$LASTEXITCODE } else { 0 }
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        $env:PATH = $previousPath
        Set-Location $previousLocation
    }

    Add-StepResult -Name $Name -ExitCode $exitCode -LogPath $logPath
    if ($exitCode -ne 0)
    {
        throw "$Name failed with exit code $exitCode. See $logPath"
    }
    Write-Host "Finished: $Name" -ForegroundColor Green
}

function Get-ToolchainPathPrefix
{
    param([Parameter(Mandatory = $true)][string]$PresetName)

    if ($PresetName -eq 'asan')
    {
        return 'C:\MSYS2\clang64\bin'
    }
    return 'C:\MSYS2\ucrt64\bin'
}

function Test-GameWipProjectReadiness
{
    param([switch]$ThrowOnFailure)

    $requirements = @(
        @{ Name = 'UCRT64 CMake'; Path = 'C:\MSYS2\ucrt64\bin\cmake.exe' }
        @{ Name = 'UCRT64 Ninja'; Path = 'C:\MSYS2\ucrt64\bin\ninja.exe' }
        @{ Name = 'UCRT64 C++ compiler'; Path = 'C:\MSYS2\ucrt64\bin\g++.exe' }
        @{ Name = 'CLANG64 C++ compiler'; Path = 'C:\MSYS2\clang64\bin\clang++.exe' }
    )
    $failures = New-Object System.Collections.Generic.List[string]
    Write-GameWipSection 'Project readiness'
    foreach ($requirement in $requirements)
    {
        if (Test-Path -LiteralPath $requirement.Path)
        {
            Write-Host "  [ready] $($requirement.Name)"
        }
        else
        {
            Write-Host "  [missing] $($requirement.Name): $($requirement.Path)" -ForegroundColor Yellow
            $failures.Add($requirement.Name) | Out-Null
        }
    }
    if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
    {
        Write-Host '  [ready] Git repository metadata'
    }
    else
    {
        Write-Host '  [missing] Git repository metadata' -ForegroundColor Yellow
        $failures.Add('Git repository metadata') | Out-Null
    }
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($RepositoryRoot))
    if ($drive.IsReady) { Write-Host ('  Free disk space: {0:N1} GB' -f ($drive.AvailableFreeSpace / 1GB)) }

    if ($failures.Count -ne 0)
    {
        $message = "$($failures.Count) project requirement(s) are missing. Run .\setup.bat repair, then rerun gamewip."
        if ($ThrowOnFailure) { throw $message }
        Write-Host "  $message" -ForegroundColor Yellow
        return $false
    }
    Write-Host '  Ready: the project toolchain is available.' -ForegroundColor Green
    return $true
}

function Confirm-GameWipToolchain
{
    param([Parameter(Mandatory = $true)][string]$PresetName)
    $prefix = Get-ToolchainPathPrefix $PresetName
    if (-not (Test-Path -LiteralPath (Join-Path $prefix 'cmake.exe')))
    {
        Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null
    }
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
    if ($LASTEXITCODE -ne 0 -or -not $branch) { throw 'Could not determine the current Git branch.' }
    return $branch
}

function Assert-GameWipCleanTrackedTree
{
    $changes = @(& git status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the Git working tree.' }
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
    if ($LASTEXITCODE -ne 0) { throw 'Could not read Git status.' }
}

function Read-GameWipIndexedChoice
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices
    )
    if ($Choices.Count -eq 0) { return $null }
    Write-Host ''
    Write-Host $Prompt
    for ($index = 0; $index -lt $Choices.Count; ++$index)
    {
        Write-Host ("  [{0}] {1}" -f ($index + 1), $Choices[$index])
    }
    while ($true)
    {
        $answer = Read-Host 'Selection [Q = cancel]'
        if ($answer -eq 'q' -or $answer -eq 'Q' -or [string]::IsNullOrWhiteSpace($answer)) { return $null }
        $number = 0
        if ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and $number -le $Choices.Count)
        {
            return $Choices[$number - 1]
        }
        Write-Host 'Enter one of the listed numbers or Q.' -ForegroundColor Yellow
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
    $selected = if ([string]::IsNullOrWhiteSpace($TargetBranch)) {
        Read-GameWipIndexedChoice -Prompt 'Switch to branch' -Choices $branches
    } else {
        $TargetBranch -replace '^origin/', ''
    }
    if ($null -eq $selected) { return }
    if ($branches -notcontains $selected) { throw "Unknown local or origin branch '$selected'." }
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
    $name = if ([string]::IsNullOrWhiteSpace($BranchName)) { Read-TextValue -Prompt 'New branch name' } else { $BranchName }
    if ([string]::IsNullOrWhiteSpace($name))
    {
        Write-Host 'Branch creation cancelled.'
        return
    }
    & git check-ref-format --branch $name 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "'$name' is not a valid Git branch name." }
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
    if (-not (Read-YesNo -Prompt "Publish '$branch' to origin and set its upstream?" -Default $true))
    {
        Write-Host 'Push cancelled.'
        return
    }
    Invoke-GameWipNative -Name "git-publish-$branch" -FilePath 'git' -Arguments @('push', '--set-upstream', 'origin', $branch)
}

function Show-GameWipRecentCommits
{
    Assert-GameWipGitRepository
    Write-GameWipSection 'Recent commits'
    & git log -12 --oneline --decorate --graph
    if ($LASTEXITCODE -ne 0) { throw 'Could not read Git history.' }
}

function Invoke-GameWipBranchCleanup
{
    Assert-GameWipCleanTrackedTree
    Invoke-GameWipGitFetch
    $current = Get-GameWipCurrentBranch
    $defaultRemote = (& git symbolic-ref --short refs/remotes/origin/HEAD 2>$null)
    $defaultName = if ($LASTEXITCODE -eq 0 -and $defaultRemote) {
        $defaultRemote.Trim().Substring('origin/'.Length)
    } else {
        $CommandConfig.GitHubDefaultBranch
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
        if (Read-YesNo -Prompt "Delete fully merged local branch '$branch'?" -Default $true)
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
        if (Read-YesNo -Prompt "Force-delete local branch '$branch' after confirming its work is preserved remotely?" -Default $false)
        {
            Invoke-GameWipNative -Name "git-force-delete-$branch" -FilePath 'git' -Arguments @('branch', '-D', $branch)
            $deleted.Add($branch) | Out-Null
        }
    }
    if ($deleted.Count -eq 0) { Write-Host 'No local branches were deleted.' }
    else { Write-Host "Deleted local branches: $($deleted -join ', ')" -ForegroundColor Green }
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
        Write-Host 'Esc. Back'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape) { Write-Host 'Esc'; return }
        Write-Host $key.KeyChar
        switch ($key.KeyChar)
        {
            '1' { Show-GameWipGitStatus }
            '2' { Invoke-GameWipGitFetch }
            '3' { Invoke-GameWipBranchSwitch }
            '4' { Invoke-GameWipCurrentBranchUpdate }
            '5' { Invoke-GameWipBranchCleanup }
            '6' { Invoke-GameWipBranchCreate }
            '7' { Invoke-GameWipCurrentBranchPush }
            '8' { Show-GameWipRecentCommits }
            default { Write-Host 'Press 1-8 or Esc.' -ForegroundColor Yellow }
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
        'menu' { Show-GameWipGitMenu }
        'status' { Show-GameWipGitStatus }
        'fetch' { Invoke-GameWipGitFetch }
        'switch' { Invoke-GameWipBranchSwitch -TargetBranch $BranchName }
        'update' { Invoke-GameWipCurrentBranchUpdate }
        'cleanup' { Invoke-GameWipBranchCleanup }
        'create' { Invoke-GameWipBranchCreate -BranchName $BranchName }
        'push' { Invoke-GameWipCurrentBranchPush }
        'log' { Show-GameWipRecentCommits }
    }
}

function Get-GameWipWorkflow
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $workflowInfo = @($CommandConfig.ManualWorkflows | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($workflowInfo.Count -eq 0)
    {
        throw "Unknown manual workflow '$Id'. Run '.\gamewip.bat workflow -WorkflowAction list' to see supported workflows."
    }
    $workflowInfo[0]
}

function Show-GameWipWorkflowCatalog
{
    Write-GameWipSection 'Manual GitHub workflows'
    Write-Host "Repository: $($CommandConfig.GitHubRepository)"
    Write-Host "Dispatch ref: $($CommandConfig.GitHubDefaultBranch) (fixed)"
    Write-Host ''
    foreach ($workflowInfo in $CommandConfig.ManualWorkflows)
    {
        Write-Host ('  {0,-27} [{1,-8}] {2}' -f $workflowInfo.Id, $workflowInfo.Safety, $workflowInfo.Name)
    }
    Write-Host ''
    Write-Host 'check/dry-run operations do not mutate repository state.'
    Write-Host 'write/deploy/finalize operations require typed confirmation and protected-environment approval.'
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

function Invoke-GameWipGhJson
{
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $previousErrorActionPreference = $ErrorActionPreference
    try
    {
        $ErrorActionPreference = 'Continue'
        $output = @(& gh @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0)
    {
        throw "GitHub CLI command failed: gh $($Arguments -join ' ')$([Environment]::NewLine)$($output -join [Environment]::NewLine)"
    }
    $json = ($output -join [Environment]::NewLine).Trim()
    if ([string]::IsNullOrWhiteSpace($json)) { return @() }
    @($json | ConvertFrom-Json)
}

function Resolve-GameWipWorkflowArguments
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowId,
        [Parameter(Mandatory = $true)][string]$ItemKind,
        [int]$ItemNumber,
        [string]$Commit
    )

    $workflowInfo = Get-GameWipWorkflow -Id $WorkflowId
    $arguments = New-Object System.Collections.Generic.List[string]
    @('workflow', 'run', $workflowInfo.File, '--repo', $CommandConfig.GitHubRepository, '--ref', $CommandConfig.GitHubDefaultBranch) |
        ForEach-Object { $arguments.Add($_) | Out-Null }

    if ($WorkflowId -like 'project-*')
    {
        if ($ItemKind -ne 'all' -and $ItemNumber -le 0)
        {
            $numberText = Read-TextValue -Prompt "$ItemKind number"
            if (-not [int]::TryParse($numberText, [ref]$ItemNumber))
            {
                $ItemNumber = 0
            }
        }
        if ($ItemKind -ne 'all' -and $ItemNumber -le 0)
        {
            throw "Project kind '$ItemKind' requires -WorkflowNumber with a positive issue or pull-request number."
        }
        @('-f', "kind=$ItemKind") | ForEach-Object { $arguments.Add($_) | Out-Null }
        if ($ItemKind -ne 'all')
        {
            @('-f', "number=$ItemNumber") | ForEach-Object { $arguments.Add($_) | Out-Null }
        }
        $dryRunValue = if ($WorkflowId -eq 'project-dry-run') { 'true' } else { 'false' }
        @('-f', "dry_run=$dryRunValue") | ForEach-Object { $arguments.Add($_) | Out-Null }
    }

    if ($WorkflowId -like 'release-*')
    {
        $command = switch ($WorkflowId)
        {
            'release-check' { 'check' }
            'release-prepare' { 'prepare' }
            default { 'finalize' }
        }
        $dryRunValue = if ($WorkflowId -eq 'release-prepare' -or $WorkflowId -eq 'release-finalize') { 'false' } else { 'true' }
        @('-f', "command=$command", '-f', "dry_run=$dryRunValue") |
            ForEach-Object { $arguments.Add($_) | Out-Null }

        if ($WorkflowId -like 'release-finalize*')
        {
            if ([string]::IsNullOrWhiteSpace($Commit))
            {
                $Commit = Read-TextValue -Prompt 'Exact master commit SHA to finalize'
            }
            if ($Commit -notmatch '^[0-9a-fA-F]{40}$')
            {
                throw 'Release finalization requires the complete 40-character master commit SHA.'
            }
            @('-f', "release_commit=$Commit") | ForEach-Object { $arguments.Add($_) | Out-Null }
        }
    }

    [pscustomobject]@{
        Definition = $workflowInfo
        Arguments = $arguments.ToArray()
        ReleaseCommit = $Commit
    }
}

function Confirm-GameWipTypedPhrase
{
    param([Parameter(Mandatory = $true)][string]$Phrase)

    Write-Host ''
    Write-Host 'This operation can change shared GitHub state.' -ForegroundColor Yellow
    Write-Host "Type exactly: $Phrase"
    $answer = Read-Host 'Confirmation'
    if ($answer -cne $Phrase)
    {
        Write-Host 'Confirmation did not match; workflow dispatch cancelled.' -ForegroundColor Yellow
        return $false
    }
    return $true
}

function Get-GameWipWorkflowRunIds
{
    param([Parameter(Mandatory = $true)][string]$WorkflowFile)

    $runs = Invoke-GameWipGhJson -Arguments @(
        'run', 'list',
        '--repo', $CommandConfig.GitHubRepository,
        '--workflow', $WorkflowFile,
        '--event', 'workflow_dispatch',
        '--branch', $CommandConfig.GitHubDefaultBranch,
        '--limit', '10',
        '--json', 'databaseId'
    )
    @($runs | ForEach-Object { [string]$_.databaseId })
}

function Wait-GameWipWorkflowRun
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowFile,
        [Parameter(Mandatory = $true)][string[]]$PreviousIds
    )

    for ($attempt = 0; $attempt -lt 15; ++$attempt)
    {
        Start-Sleep -Seconds 2
        $runs = Invoke-GameWipGhJson -Arguments @(
            'run', 'list',
            '--repo', $CommandConfig.GitHubRepository,
            '--workflow', $WorkflowFile,
            '--event', 'workflow_dispatch',
            '--branch', $CommandConfig.GitHubDefaultBranch,
            '--limit', '10',
            '--json', 'databaseId,url,status,createdAt'
        )
        $run = @($runs | Where-Object { $PreviousIds -notcontains [string]$_.databaseId } | Select-Object -First 1)
        if ($run.Count -ne 0) { return $run[0] }
    }
    return $null
}

function Show-GameWipWorkflowVerification
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowId,
        [Parameter(Mandatory = $true)][string]$RunId
    )

    Write-GameWipSection 'Verification commands'
    Write-Host (ConvertTo-NativeCommandLine -FilePath 'gh' -Arguments @('run', 'view', $RunId, '--repo', $CommandConfig.GitHubRepository))
    Write-Host (ConvertTo-NativeCommandLine -FilePath 'gh' -Arguments @('run', 'view', $RunId, '--repo', $CommandConfig.GitHubRepository, '--log-failed'))
    if ($WorkflowId -eq 'release-finalize')
    {
        Write-Host (ConvertTo-NativeCommandLine -FilePath 'gh' -Arguments @('release', 'list', '--repo', $CommandConfig.GitHubRepository, '--limit', '5'))
        Write-Host (ConvertTo-NativeCommandLine -FilePath 'git' -Arguments @('ls-remote', '--tags', 'origin'))
    }
    elseif ($WorkflowId -eq 'release-prepare')
    {
        Write-Host (ConvertTo-NativeCommandLine -FilePath 'gh' -Arguments @('pr', 'list', '--repo', $CommandConfig.GitHubRepository, '--state', 'open'))
    }
    elseif ($WorkflowId -eq 'docs-deploy')
    {
        Write-Host (ConvertTo-NativeCommandLine -FilePath 'gh' -Arguments @('api', "repos/$($CommandConfig.GitHubRepository)/pages"))
    }
}

function Invoke-GameWipManualWorkflow
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowId,
        [Parameter(Mandatory = $true)][string]$ItemKind,
        [int]$ItemNumber,
        [string]$Commit
    )

    $resolved = Resolve-GameWipWorkflowArguments -WorkflowId $WorkflowId -ItemKind $ItemKind -ItemNumber $ItemNumber -Commit $Commit
    $workflowInfo = $resolved.Definition
    $dispatchCommand = ConvertTo-NativeCommandLine -FilePath 'gh' -Arguments $resolved.Arguments

    Write-GameWipSection 'Workflow dispatch preview'
    Write-Host "Workflow: $($workflowInfo.Name)"
    Write-Host "Safety:   $($workflowInfo.Safety)"
    Write-Host "Ref:      $($CommandConfig.GitHubDefaultBranch) (fixed)"
    Write-Host ''
    Write-Host $dispatchCommand
    Write-Host 'gh run watch <run-id> --exit-status'
    Write-Host 'gh run view <run-id> --log-failed'

    if ($Preview)
    {
        Write-Host ''
        Write-Host 'Preview only; nothing was dispatched.' -ForegroundColor Green
        return
    }

    Assert-GameWipGitHubCli -WorkflowId $WorkflowId
    if (@('write', 'deploy', 'finalize') -contains $workflowInfo.Safety)
    {
        $phrase = "$WorkflowId $($CommandConfig.GitHubDefaultBranch)"
        if ($WorkflowId -eq 'release-finalize') { $phrase = "$phrase $($resolved.ReleaseCommit)" }
        if (-not (Confirm-GameWipTypedPhrase -Phrase $phrase)) { return }
    }
    elseif (-not (Read-YesNo -Prompt 'Dispatch this non-mutating workflow now?' -Default $true))
    {
        Write-Host 'Workflow dispatch cancelled.'
        return
    }

    $previousIds = @(Get-GameWipWorkflowRunIds -WorkflowFile $workflowInfo.File)
    Invoke-GameWipNative -Name "workflow-dispatch-$WorkflowId" -FilePath 'gh' -Arguments $resolved.Arguments
    $run = Wait-GameWipWorkflowRun -WorkflowFile $workflowInfo.File -PreviousIds $previousIds
    if ($null -eq $run)
    {
        Write-Host 'Dispatch succeeded, but the new run was not visible within 30 seconds.' -ForegroundColor Yellow
        Write-Host "Check it with: .\gamewip.bat workflow -WorkflowAction status"
        return
    }

    $runId = [string]$run.databaseId
    Write-Host ''
    Write-Host "Queued run: $($run.url)" -ForegroundColor Green
    $watchArguments = @('run', 'watch', $runId, '--repo', $CommandConfig.GitHubRepository, '--exit-status')
    Write-Host (ConvertTo-NativeCommandLine -FilePath 'gh' -Arguments $watchArguments)
    if (Read-YesNo -Prompt 'Watch this run until it finishes?' -Default $true)
    {
        Invoke-GameWipNative -Name "workflow-watch-$runId" -FilePath 'gh' -Arguments $watchArguments
    }
    Show-GameWipWorkflowVerification -WorkflowId $WorkflowId -RunId $runId
}

function Show-GameWipWorkflowStatus
{
    Assert-GameWipGitHubCli -WorkflowId 'validation'
    Invoke-GameWipNative -Name 'workflow-recent-runs' -FilePath 'gh' -Arguments @(
        'run', 'list',
        '--repo', $CommandConfig.GitHubRepository,
        '--event', 'workflow_dispatch',
        '--limit', '12'
    )
}

function Show-GameWipWorkflowMenu
{
    while ($true)
    {
        Write-Host ''
        Write-Host 'GitHub Workflows'
        Write-Host '================'
        Write-Host '1. List supported workflows'
        Write-Host '2. Dispatch a supported workflow'
        Write-Host '3. Show recent manual runs'
        Write-Host 'Esc. Back'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape) { Write-Host 'Esc'; return }
        Write-Host $key.KeyChar
        switch ($key.KeyChar)
        {
            '1' { Show-GameWipWorkflowCatalog }
            '2' {
                Show-GameWipWorkflowCatalog
                $choice = Read-GameWipIndexedChoice -Prompt 'Workflow to dispatch' -Choices @($CommandConfig.ManualWorkflows | ForEach-Object { $_.Id })
                if ($null -ne $choice)
                {
                    Invoke-GameWipManualWorkflow -WorkflowId $choice -ItemKind $WorkflowKind -ItemNumber $WorkflowNumber -Commit $ReleaseCommit
                }
            }
            '3' { Show-GameWipWorkflowStatus }
            default { Write-Host 'Press 1-3 or Esc.' -ForegroundColor Yellow }
        }
    }
}

function Invoke-GameWipWorkflowAction
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$WorkflowId
    )
    switch ($Name)
    {
        'menu' { Show-GameWipWorkflowMenu }
        'list' { Show-GameWipWorkflowCatalog }
        'status' { Show-GameWipWorkflowStatus }
        'run' {
            if ([string]::IsNullOrWhiteSpace($WorkflowId))
            {
                throw "WorkflowAction 'run' requires -Workflow <id>. Run '.\gamewip.bat workflow -WorkflowAction list' first."
            }
            Invoke-GameWipManualWorkflow -WorkflowId $WorkflowId -ItemKind $WorkflowKind -ItemNumber $WorkflowNumber -Commit $ReleaseCommit
        }
    }
}

function Invoke-ConfigurePreset
{
    param([Parameter(Mandatory = $true)][string]$Name)

    Assert-ValidPreset -Kind 'configure' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    Invoke-GameWipNative -Name "configure-$Name" -FilePath 'cmake' -Arguments @('--preset', $Name) -PathPrefix (Get-ToolchainPathPrefix $Name)
}

function Invoke-BuildPreset
{
    param([Parameter(Mandatory = $true)][string]$Name)

    Assert-ValidPreset -Kind 'build' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    $cache = Join-Path $RepositoryRoot "build\$Name\CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache))
    {
        Write-Host "Build preset '$Name' has not been configured; configuring it now." -ForegroundColor Cyan
        Invoke-ConfigurePreset -Name $Name
    }
    Invoke-GameWipNative -Name "build-$Name" -FilePath 'cmake' -Arguments @('--build', '--preset', $Name, '--parallel') -PathPrefix (Get-ToolchainPathPrefix $Name)
}

function Invoke-BuildTarget
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Target
    )

    Assert-ValidPreset -Kind 'build' -Name $Name
    Invoke-GameWipNative -Name "build-$Name-$Target" -FilePath 'cmake' -Arguments @('--build', '--preset', $Name, '--target', $Target) -PathPrefix (Get-ToolchainPathPrefix $Name)
}

function Invoke-TestPreset
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [switch]$UseWorkspaceTemp
    )

    Assert-ValidPreset -Kind 'test' -Name $Name
    Confirm-GameWipToolchain -PresetName $Name
    $testFile = Join-Path $RepositoryRoot "build\$Name\CTestTestfile.cmake"
    if (-not (Test-Path -LiteralPath $testFile))
    {
        Write-Host "Test preset '$Name' is not built; configuring and building it now." -ForegroundColor Cyan
        Invoke-ConfigurePreset -Name $Name
        Invoke-BuildPreset -Name $Name
    }
    Invoke-GameWipNative -Name "ctest-$Name" -FilePath 'ctest' -Arguments @('--preset', $Name, '--output-on-failure') -UseWorkspaceTemp:$UseWorkspaceTemp -PathPrefix (Get-ToolchainPathPrefix $Name)
}

function Write-NextStepHint
{
    param([Parameter(Mandatory = $true)][string]$Message)

    Write-Host "Next: $Message" -ForegroundColor Cyan
}

function Resolve-ProjectExecutable
{
    param([Parameter(Mandatory = $true)]$Command)

    $primary = Join-Path $RepositoryRoot $Command.Executable
    if (Test-Path -LiteralPath $primary)
    {
        return $primary
    }

    if ($Command.ContainsKey('AlternateExecutable'))
    {
        $alternate = Join-Path $RepositoryRoot $Command.AlternateExecutable
        if (Test-Path -LiteralPath $alternate)
        {
            return $alternate
        }
    }

    return $primary
}

function Ensure-ProjectCommandBuilt
{
    param(
        [Parameter(Mandatory = $true)]$Command,
        [switch]$ForceBuild
    )

    $executable = Resolve-ProjectExecutable -Command $Command
    if (Test-Path -LiteralPath $executable)
    {
        return
    }

    if (-not $ForceBuild)
    {
        throw "Missing executable '$executable'. Rerun with -BuildIfMissing or build preset '$($Command.BuildPreset)'."
    }

    Invoke-ConfigurePreset -Name $Command.BuildPreset
    Invoke-BuildPreset -Name $Command.BuildPreset
}

function Invoke-ProjectCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [switch]$ForceBuild
    )

    $command = Get-ProjectCommand -Id $Id
    Ensure-ProjectCommandBuilt -Command $command -ForceBuild:$ForceBuild
    $executable = Resolve-ProjectExecutable -Command $command
    $arguments = @($command.Arguments)
    Invoke-GameWipNative -Name "project-$Id" -FilePath $executable -Arguments $arguments -UseWorkspaceTemp:([bool]$command.UseWorkspaceTemp)
}

function Invoke-ValidationModule
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$Arguments = @(),
        [switch]$ForceBuild
    )

    Assert-ValidModule -Name $Name
    $command = Get-ProjectCommand -Id 'test-all'
    Ensure-ProjectCommandBuilt -Command $command -ForceBuild:$ForceBuild
    $executable = Resolve-ProjectExecutable -Command $command

    $testArguments = New-Object System.Collections.Generic.List[string]
    if ($Name -ne 'all')
    {
        $testArguments.Add("--test-module=$Name") | Out-Null
    }
    $testArguments.Add('--no-test-report') | Out-Null
    foreach ($argument in $Arguments)
    {
        $testArguments.Add($argument) | Out-Null
    }

    Invoke-GameWipNative -Name "module-$Name" -FilePath $executable -Arguments $testArguments.ToArray() -UseWorkspaceTemp
}

function Invoke-StressModule
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][int]$RunCount,
        [Parameter(Mandatory = $true)][int]$MaxParallel,
        [string[]]$Arguments = @(),
        [switch]$ForceBuild
    )

    Assert-ValidModule -Name $Name
    Initialize-RunLog

    $command = Get-ProjectCommand -Id 'test-all'
    Ensure-ProjectCommandBuilt -Command $command -ForceBuild:$ForceBuild
    $executable = Resolve-ProjectExecutable -Command $command
    $tempRoot = Join-Path $RepositoryRoot $CommandConfig.WorkspaceTemp
    if (-not $NoWorkspaceTemp)
    {
        New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    }

    $baseArguments = New-Object System.Collections.Generic.List[string]
    if ($Name -ne 'all')
    {
        $baseArguments.Add("--test-module=$Name") | Out-Null
    }
    $baseArguments.Add('--no-test-report') | Out-Null
    foreach ($argument in $Arguments)
    {
        $baseArguments.Add($argument) | Out-Null
    }

    Write-GameWipSection "Stress $Name"
    Write-Host "Runs: $RunCount"
    Write-Host "Parallel workers: $MaxParallel"
    Write-Host "Executable: $executable"
    Write-Host ("> {0}" -f (ConvertTo-NativeCommandLine -FilePath $executable -Arguments $baseArguments.ToArray()))
    Write-Host "Worker logs: $Script:RunRoot\stress_####.out.log and stress_####.err.log"

    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $active = New-Object System.Collections.Generic.List[object]
    $nextRun = 1
    $completed = 0
    $failed = 0
    $progressClock = [System.Diagnostics.Stopwatch]::StartNew()
    $lastProgressMilliseconds = -2000.0

    try
    {
        if (-not $NoWorkspaceTemp)
        {
            $env:TEMP = $tempRoot
            $env:TMP = $tempRoot
            Write-Host "Workspace temp: $tempRoot"
        }

        while ($nextRun -le $RunCount -or $active.Count -gt 0)
        {
            while ($nextRun -le $RunCount -and $active.Count -lt $MaxParallel)
            {
                $runName = 'stress_{0:D4}' -f $nextRun
                $stdout = Join-Path $Script:RunRoot "$runName.out.log"
                $stderr = Join-Path $Script:RunRoot "$runName.err.log"
                $exitPath = Join-Path $Script:RunRoot "$runName.exit"

                $process = Start-Process -FilePath $executable `
                    -ArgumentList $baseArguments.ToArray() `
                    -WorkingDirectory $RepositoryRoot `
                    -NoNewWindow `
                    -PassThru `
                    -RedirectStandardOutput $stdout `
                    -RedirectStandardError $stderr

                $active.Add([pscustomobject]@{
                    Run = $nextRun
                    Process = $process
                    Stdout = $stdout
                    Stderr = $stderr
                    ExitPath = $exitPath
                }) | Out-Null
                ++$nextRun
            }

            $elapsedMilliseconds = $progressClock.Elapsed.TotalMilliseconds
            if (($elapsedMilliseconds - $lastProgressMilliseconds) -ge 2000.0)
            {
                Write-Host ("Progress: completed={0}/{1} active={2} launched={3} failed={4}" -f $completed, $RunCount, $active.Count, ($nextRun - 1), $failed)
                $lastProgressMilliseconds = $elapsedMilliseconds
            }

            for ($index = $active.Count - 1; $index -ge 0; --$index)
            {
                $entry = $active[$index]
                if (-not $entry.Process.HasExited)
                {
                    continue
                }

                $code = [int]$entry.Process.ExitCode
                Set-Content -LiteralPath $entry.ExitPath -Value $code -Encoding ASCII
                ++$completed
                if ($code -eq 0)
                {
                    Write-Host ("Run {0}: PASS ({1}/{2} complete)" -f $entry.Run, $completed, $RunCount)
                }
                else
                {
                    ++$failed
                    Write-Host ("Run {0}: FAIL exit={1} ({2}/{3} complete) stdout={4} stderr={5}" -f $entry.Run, $code, $completed, $RunCount, $entry.Stdout, $entry.Stderr) -ForegroundColor Red
                }
                Add-StepResult -Name ("stress-{0:D4}" -f $entry.Run) -ExitCode $code -LogPath $entry.Stdout
                $active.RemoveAt($index)
            }

            if ($active.Count -gt 0)
            {
                Start-Sleep -Milliseconds 200
            }
        }
    }
    finally
    {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
    }

    if ($failed -ne 0)
    {
        throw "$failed of $RunCount stress runs failed. Logs are in $Script:RunRoot"
    }
    Write-Host "Stress complete: $RunCount runs passed." -ForegroundColor Green
}

function Invoke-Bundle
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $bundleInfo = Get-ProjectBundle -Id $Id
    Write-GameWipSection $bundleInfo.Name
    foreach ($step in $bundleInfo.Steps)
    {
        switch ($step.Kind)
        {
            'Configure' { Invoke-ConfigurePreset -Name $step.Preset }
            'Build' { Invoke-BuildPreset -Name $step.Preset }
            'BuildTarget' { Invoke-BuildTarget -Name $step.Preset -Target $step.Target }
            'CTest' { Invoke-TestPreset -Name $step.Preset -UseWorkspaceTemp:([bool]$step.UseWorkspaceTemp) }
            'ProjectCommand' { Invoke-ProjectCommand -Id $step.Command -ForceBuild:([bool]$step.BuildIfMissing) }
            default { throw "Unknown bundle step kind '$($step.Kind)' in bundle '$Id'." }
        }
    }
}

function Show-ProjectCatalog
{
    Write-GameWipSection 'Configure presets'
    Get-VisiblePresetNames -Kind 'configure' | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Build presets'
    Get-VisiblePresetNames -Kind 'build' | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Test presets'
    Get-VisiblePresetNames -Kind 'test' | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Validation modules'
    @($CommandConfig.Modules) | ForEach-Object { Write-Host "  $_" }

    Write-GameWipSection 'Project commands'
    foreach ($command in $CommandConfig.ProjectCommands)
    {
        Write-Host ("  {0} - {1}" -f $command.Id, $command.Name)
    }

    Write-GameWipSection 'Bundles'
    foreach ($bundleInfo in $CommandConfig.Bundles)
    {
        Write-Host ("  {0} - {1}" -f $bundleInfo.Id, $bundleInfo.Name)
    }

    Show-GameWipWorkflowCatalog
}

function Read-MenuChoice
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices,
        [string]$Default
    )

    $keys = @('1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'a', 'b', 'c', 'd', 'e', 'f')
    if ($Choices.Count -gt $keys.Count)
    {
        throw "Too many choices for single-key selection: $($Choices.Count)."
    }

    while ($true)
    {
        Write-Host ''
        Write-Host $Prompt
        for ($index = 0; $index -lt $Choices.Count; ++$index)
        {
            Write-Host ("  [{0}] {1}" -f $keys[$index], $Choices[$index])
        }
        if (-not [string]::IsNullOrWhiteSpace($Default))
        {
            Write-Host "  [Enter] $Default"
        }
        Write-Host 'Choose one key, or Esc/q to cancel: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q')
        {
            Write-Host 'cancel'
            return $null
        }
        if ($key.Key -eq [ConsoleKey]::Enter -and -not [string]::IsNullOrWhiteSpace($Default))
        {
            Write-Host 'Enter'
            return $Default
        }

        $selectionKey = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $selectionKey
        $index = [array]::IndexOf($keys, $selectionKey)
        if ($index -ge 0 -and $index -lt $Choices.Count)
        {
            return $Choices[$index]
        }
        Write-Host 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Read-TextValue
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [string]$Default = ''
    )

    if ([string]::IsNullOrWhiteSpace($Default))
    {
        $value = Read-Host $Prompt
    }
    else
    {
        $value = Read-Host "$Prompt [$Default]"
    }
    if ([string]::IsNullOrWhiteSpace($value))
    {
        return $Default
    }
    $value
}

function Read-IntegerValue
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][int]$Default
    )

    $values = @(1, 2, 4, 8, 16, 32, 64, 100)
    $keys = @('1', '2', '3', '4', '5', '6', '7', '8')
    while ($true)
    {
        Write-Host ''
        Write-Host $Prompt
        for ($index = 0; $index -lt $values.Count; ++$index)
        {
            Write-Host ("  [{0}] {1}" -f $keys[$index], $values[$index])
        }
        Write-Host "  [Enter] $Default"
        Write-Host '  [c] custom'
        Write-Host 'Choose one key: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            return $Default
        }
        $selectionKey = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $selectionKey
        if ($selectionKey -eq 'c')
        {
            $value = Read-TextValue -Prompt 'Custom positive integer' -Default ([string]$Default)
            $parsed = 0
            if ([int]::TryParse($value, [ref]$parsed) -and $parsed -gt 0)
            {
                return $parsed
            }
            Write-Host 'Enter a positive integer.' -ForegroundColor Yellow
            continue
        }

        $index = [array]::IndexOf($keys, $selectionKey)
        if ($index -ge 0)
        {
            return $values[$index]
        }
        Write-Host 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Read-YesNo
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][bool]$Default
    )

    $suffix = if ($Default) { '[Y/n]' } else { '[y/N]' }
    while ($true)
    {
        Write-Host "$Prompt $suffix " -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            return $Default
        }
        $value = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $value
        switch ($value)
        {
            'y' { return $true }
            'n' { return $false }
            default { Write-Host 'Enter y or n.' -ForegroundColor Yellow }
        }
    }
}

function Read-MultiChoice
{
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [Parameter(Mandatory = $true)][string[]]$Choices
    )

    $keys = @('1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'a', 'b', 'c', 'd', 'e', 'f')
    if ($Choices.Count -gt $keys.Count)
    {
        throw "Too many choices for single-key selection: $($Choices.Count)."
    }

    $selected = New-Object System.Collections.Generic.HashSet[string]
    while ($true)
    {
        Write-Host ''
        Write-Host $Prompt
        for ($index = 0; $index -lt $Choices.Count; ++$index)
        {
            $marker = if ($selected.Contains($Choices[$index])) { 'x' } else { ' ' }
            Write-Host ("  [{0}] [{1}] {2}" -f $keys[$index], $marker, $Choices[$index])
        }
        Write-Host 'Toggle one key, Enter to accept, or Esc/q to cancel: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q')
        {
            Write-Host 'cancel'
            return @()
        }
        if ($key.Key -eq [ConsoleKey]::Enter)
        {
            Write-Host 'Enter'
            $result = New-Object System.Collections.Generic.List[string]
            foreach ($choice in $selected)
            {
                $result.Add($choice) | Out-Null
            }
            return $result.ToArray()
        }

        $selectionKey = $key.KeyChar.ToString().ToLowerInvariant()
        Write-Host $selectionKey
        $index = [array]::IndexOf($keys, $selectionKey)
        if ($index -ge 0 -and $index -lt $Choices.Count)
        {
            $choice = $Choices[$index]
            if ($selected.Contains($choice))
            {
                [void]$selected.Remove($choice)
            }
            else
            {
                [void]$selected.Add($choice)
            }
            continue
        }

        Write-Host 'Invalid selection.' -ForegroundColor Yellow
    }
}

function Show-ActionFailure
{
    param([Parameter(Mandatory = $true)][System.Management.Automation.ErrorRecord]$ErrorRecord)

    $message = $ErrorRecord.Exception.Message
    Write-Host ''
    Write-Host 'Action failed' -ForegroundColor Red
    Write-Host $message -ForegroundColor Red

    $suggestions = New-Object System.Collections.Generic.List[string]
    if ($message -match "Unknown .+ Run 'gamewip list'")
    {
        $suggestions.Add('Run .\gamewip.bat list to see the valid presets, modules, commands, and bundles.') | Out-Null
    }
    if ($message -match "Missing executable '([^']+)'.+build preset '([^']+)'")
    {
        $suggestions.Add(("Build the missing executable with .\gamewip.bat build -Preset {0}." -f $Matches[2])) | Out-Null
        $suggestions.Add('For project commands and modules, add -BuildIfMissing when you want the helper to configure/build first.') | Out-Null
    }
    if ($message -match 'failed with exit code')
    {
        $suggestions.Add('Open the step log path printed above; it has the complete native command output.') | Out-Null
        $suggestions.Add('Rerun the smallest focused command: .\gamewip.bat wizard or .\gamewip.bat module -Module <name>.') | Out-Null
    }
    if ($message -match 'GitHub CLI|GitHub authentication|workflow scope|project scope')
    {
        $suggestions.Add("Check authentication with 'gh auth status', then refresh the scopes named above.") | Out-Null
    }
    if ($message -match 'workflow|Workflow')
    {
        $suggestions.Add("Preview a safe command with '.\gamewip.bat workflow -WorkflowAction run -Workflow <id> -Preview'.") | Out-Null
        $suggestions.Add("List supported workflows with '.\gamewip.bat workflow -WorkflowAction list'.") | Out-Null
    }
    if ($message -match 'stress runs failed')
    {
        $suggestions.Add('Inspect the stress_####.out.log and stress_####.err.log files in the printed run-log folder.') | Out-Null
    }
    if ($message -match 'Logger|logger')
    {
        $suggestions.Add('For rare Logger failures, rerun .\gamewip.bat stress -Module logger -Count 100 -Parallel 16 -BuildIfMissing to measure repeatability.') | Out-Null
    }
    if ($suggestions.Count -eq 0)
    {
        $suggestions.Add('Rerun the same command after reviewing the printed native command and run-log folder.') | Out-Null
        $suggestions.Add('Use .\gamewip.bat list if the failure was caused by an unknown command, module, or preset.') | Out-Null
    }

    Write-Host ''
    Write-Host 'What to do next:' -ForegroundColor Cyan
    foreach ($suggestion in ($suggestions | Select-Object -Unique))
    {
        Write-Host "  - $suggestion"
    }
}

function Invoke-InteractivePostBuildFlow
{
    param([Parameter(Mandatory = $true)][string]$Name)

    if ((Get-VisiblePresetNames -Kind 'test') -contains $Name)
    {
        if (Read-YesNo -Prompt "Run CTest preset '$Name' now?" -Default $true)
        {
            Invoke-TestPreset -Name $Name -UseWorkspaceTemp
            if ($Name -eq 'coverage' -and (Read-YesNo -Prompt "Generate the coverage report target now?" -Default $true))
            {
                Invoke-BuildTarget -Name 'coverage' -Target 'coverage'
            }
        }
        else
        {
            Write-NextStepHint "run tests with: .\gamewip.bat test -Preset $Name"
        }
        return
    }

    switch ($Name)
    {
        'benchmark' {
            if (Read-YesNo -Prompt 'Run the benchmark dry-run registration check now?' -Default $true)
            {
                Invoke-ProjectCommand -Id 'benchmark-dry-run' -ForceBuild
            }
            else
            {
                Write-NextStepHint 'run benchmark registration with: .\gamewip.bat run -ProjectCommand benchmark-dry-run'
            }
        }
        'dev' {
            if (Read-YesNo -Prompt 'Print the development executable version now?' -Default $true)
            {
                Invoke-ProjectCommand -Id 'dev-version' -ForceBuild
            }
            else
            {
                Write-NextStepHint 'print it later with: .\gamewip.bat run -ProjectCommand dev-version'
            }
        }
        'release' {
            if (Read-YesNo -Prompt 'Print the release executable version now?' -Default $true)
            {
                Invoke-ProjectCommand -Id 'release-version' -ForceBuild
            }
            else
            {
                Write-NextStepHint 'print it later with: .\gamewip.bat run -ProjectCommand release-version'
            }
        }
        'docs' {
            Write-NextStepHint 'open build\docs\docs\doxygen\html\index.html to inspect the generated manual.'
        }
        'analyze' {
            Write-NextStepHint 'static analysis is complete when the build exits successfully.'
        }
        default {
            Write-NextStepHint 'use .\gamewip.bat list to see follow-up project commands and bundles.'
        }
    }
}

function Invoke-InteractiveConfigureFlow
{
    param([Parameter(Mandatory = $true)][string]$Name)

    Invoke-ConfigurePreset -Name $Name
    if ((Get-VisiblePresetNames -Kind 'build') -contains $Name)
    {
        if (Read-YesNo -Prompt "Build preset '$Name' now?" -Default $true)
        {
            Invoke-BuildPreset -Name $Name
            Invoke-InteractivePostBuildFlow -Name $Name
        }
        else
        {
            Write-NextStepHint "build it with: .\gamewip.bat build -Preset $Name"
        }
    }
}

function Invoke-InteractiveBuildFlow
{
    param([Parameter(Mandatory = $true)][string]$Name)

    Invoke-BuildPreset -Name $Name
    Invoke-InteractivePostBuildFlow -Name $Name
}

function Split-ExtraArguments
{
    param([AllowEmptyString()][string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text))
    {
        return @()
    }
    @($Text -split ' ' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Invoke-ValidationCommandWizard
{
    Write-GameWipSection 'Validation command builder'
    Write-Host 'Builds a GameWIPTests.exe command from supported validation-runner arguments.'

    $moduleChoices = @('all') + @($CommandConfig.Modules)
    $selectedModule = Read-MenuChoice -Prompt 'Module selection' -Choices $moduleChoices -Default 'all'
    if ($null -eq $selectedModule)
    {
        return
    }

    $skippedModules = @()
    if ($selectedModule -eq 'all')
    {
        $skippedModules = @(Read-MultiChoice -Prompt 'Modules to skip' -Choices @($CommandConfig.Modules))
    }

    $verbose = Read-YesNo -Prompt 'Mirror full test output to stdout?' -Default $false
    $manualUi = Read-YesNo -Prompt 'Enable manual UI checks?' -Default $false
    $loggerPopup = Read-YesNo -Prompt 'Enable Logger fatal popup check?' -Default $false
    $childProcesses = Read-YesNo -Prompt 'Enable TestSupport child-process checks?' -Default $true
    $writeReport = Read-YesNo -Prompt 'Write retained test report?' -Default $false

    $reportPath = ''
    if ($writeReport)
    {
        $reportPath = Read-TextValue -Prompt 'Report path' -Default 'logs/tests/latest_test_report.txt'
    }

    $extraText = Read-TextValue -Prompt 'Extra validation args, space-separated' -Default ''
    $buildIfMissingChoice = Read-YesNo -Prompt 'Build test executable if missing?' -Default $true

    $arguments = New-Object System.Collections.Generic.List[string]
    if ($selectedModule -ne 'all')
    {
        $arguments.Add("--test-module=$selectedModule") | Out-Null
    }
    foreach ($skippedModule in $skippedModules)
    {
        $arguments.Add("--skip-test-module=$skippedModule") | Out-Null
    }
    if ($verbose)
    {
        $arguments.Add('--verbose-tests') | Out-Null
    }
    if ($manualUi)
    {
        $arguments.Add('--manual-ui') | Out-Null
    }
    if ($loggerPopup)
    {
        $arguments.Add('--logger-popup') | Out-Null
    }
    if ($childProcesses -eq $false)
    {
        $arguments.Add('--no-test-support-child-process') | Out-Null
    }
    if ($writeReport)
    {
        $arguments.Add("--test-report=$reportPath") | Out-Null
    }
    else
    {
        $arguments.Add('--no-test-report') | Out-Null
    }
    foreach ($argument in (Split-ExtraArguments -Text $extraText))
    {
        $arguments.Add($argument) | Out-Null
    }

    $command = Get-ProjectCommand -Id 'test-all'
    if ($buildIfMissingChoice)
    {
        Ensure-ProjectCommandBuilt -Command $command -ForceBuild
    }
    $executable = Resolve-ProjectExecutable -Command $command
    $commandLine = ConvertTo-NativeCommandLine -FilePath $executable -Arguments $arguments.ToArray()

    Write-GameWipSection 'Built command'
    Write-Host $commandLine

    if (Read-YesNo -Prompt 'Run this command now?' -Default $true)
    {
        Invoke-GameWipNative -Name 'validation-wizard' -FilePath $executable -Arguments $arguments.ToArray() -UseWorkspaceTemp
        Write-NextStepHint 'Use the printed command directly next time, or rerun gamewip wizard to adjust flags.'
    }
    else
    {
        Write-NextStepHint 'Copy the printed command into PowerShell when you want to run it.'
    }
}

function Show-GameWipMenu
{
    while ($true)
    {
        Write-Host ''
        Write-Host 'GameWIP Project Tool'
        Write-Host '===================='
        Write-Host '1. Configure preset'
        Write-Host '2. Build preset'
        Write-Host '3. Run CTest preset'
        Write-Host '4. Build/run a validation command'
        Write-Host '5. Run one validation module'
        Write-Host '6. Stress validation module'
        Write-Host '7. Run known project command'
        Write-Host '8. Run command bundle'
        Write-Host '9. List commands and presets'
        Write-Host '0. Check project readiness'
        Write-Host 'G. Git branches and workspace cleanup'
        Write-Host 'W. Guarded GitHub workflows'
        Write-Host 'Esc. Exit'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape -or [int]$key.KeyChar -eq 27)
        {
            Write-Host 'Esc'
            return
        }
        Write-Host $key.KeyChar

        try
        {
            switch ($key.KeyChar)
            {
                '1' {
                    $choice = Read-MenuChoice -Prompt 'Configure preset' -Choices (Get-VisiblePresetNames -Kind 'configure') -Default $CommandConfig.DefaultConfigurePreset
                    if ($null -ne $choice)
                    {
                        Invoke-InteractiveConfigureFlow -Name $choice
                    }
                }
                '2' {
                    $choice = Read-MenuChoice -Prompt 'Build preset' -Choices (Get-VisiblePresetNames -Kind 'build') -Default $CommandConfig.DefaultBuildPreset
                    if ($null -ne $choice)
                    {
                        Invoke-InteractiveBuildFlow -Name $choice
                    }
                }
                '3' {
                    $choice = Read-MenuChoice -Prompt 'CTest preset' -Choices (Get-VisiblePresetNames -Kind 'test') -Default $CommandConfig.DefaultTestPreset
                    if ($null -ne $choice)
                    {
                        Invoke-TestPreset -Name $choice -UseWorkspaceTemp
                        Write-NextStepHint 'use option 4 to build a focused GameWIPTests.exe command, or option 6 to stress a module.'
                    }
                }
                '4' { Invoke-ValidationCommandWizard }
                '5' {
                    $choices = @('all') + @($CommandConfig.Modules)
                    $choice = Read-MenuChoice -Prompt 'Validation module' -Choices $choices -Default $CommandConfig.DefaultModule
                    if ($null -ne $choice)
                    {
                        Invoke-ValidationModule -Name $choice -ForceBuild
                        Write-NextStepHint "stress this module with: .\gamewip.bat stress -Module $choice -Count 100 -Parallel 16"
                    }
                }
                '6' {
                    $choices = @('all') + @($CommandConfig.Modules)
                    $choice = Read-MenuChoice -Prompt 'Stress validation module' -Choices $choices -Default $CommandConfig.DefaultModule
                    if ($null -ne $choice)
                    {
                        $runCount = Read-IntegerValue -Prompt 'Run count' -Default ([int]$CommandConfig.DefaultStressCount)
                        $parallelCount = Read-IntegerValue -Prompt 'Parallel workers' -Default ([int]$CommandConfig.DefaultStressParallel)
                        Invoke-StressModule -Name $choice -RunCount $runCount -MaxParallel $parallelCount -ForceBuild
                    }
                }
                '7' {
                    $choices = @($CommandConfig.ProjectCommands | ForEach-Object { $_.Id })
                    $choice = Read-MenuChoice -Prompt 'Project command' -Choices $choices -Default 'benchmark-dry-run'
                    if ($null -ne $choice) { Invoke-ProjectCommand -Id $choice -ForceBuild }
                }
                '8' {
                    $choices = @($CommandConfig.Bundles | ForEach-Object { $_.Id })
                    $choice = Read-MenuChoice -Prompt 'Command bundle' -Choices $choices -Default 'quick'
                    if ($null -ne $choice) { Invoke-Bundle -Id $choice }
                }
                '9' { Show-ProjectCatalog }
                '0' { Test-GameWipProjectReadiness | Out-Null }
                { $_ -eq 'g' -or $_ -eq 'G' } { Show-GameWipGitMenu }
                { $_ -eq 'w' -or $_ -eq 'W' } { Show-GameWipWorkflowMenu }
                default { Write-Host 'Press one of the listed number keys, or Esc to exit.' -ForegroundColor Yellow }
            }
        }
        catch
        {
            Show-ActionFailure -ErrorRecord $_
        }
        finally
        {
            Save-RunSummary
        }
    }
}

function Show-Help
{
    Write-Host 'Usage:'
    Write-Host '  gamewip'
    Write-Host '  gamewip doctor'
    Write-Host '  gamewip git'
    Write-Host '  gamewip git -GitAction status'
    Write-Host '  gamewip git -GitAction switch -GitBranch <name>'
    Write-Host '  gamewip workflow'
    Write-Host '  gamewip workflow -WorkflowAction list'
    Write-Host '  gamewip workflow -WorkflowAction run -Workflow release-check -Preview'
    Write-Host '  gamewip workflow -WorkflowAction run -Workflow project-dry-run -WorkflowKind issue -WorkflowNumber <number>'
    Write-Host '  gamewip workflow -WorkflowAction run -Workflow release-finalize-dry-run -ReleaseCommit <sha>'
    Write-Host '  gamewip list'
    Write-Host '  gamewip configure -Preset test'
    Write-Host '  gamewip build -Preset test'
    Write-Host '  gamewip test -Preset test'
    Write-Host '  gamewip wizard'
    Write-Host '  gamewip module -Module logger -BuildIfMissing'
    Write-Host '  gamewip stress -Module logger -Count 100 -Parallel 16 -BuildIfMissing'
    Write-Host '  gamewip run -ProjectCommand benchmark-dry-run -BuildIfMissing'
    Write-Host '  gamewip bundle -Bundle quick'
    Write-Host ''
    Write-Host 'Known commands, modules, presets, and bundles are listed by:'
    Write-Host '  gamewip list'
}

try
{
    switch ($Action)
    {
        'menu' { Show-GameWipMenu }
        'doctor' { Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null }
        'git' { Invoke-GameWipGitAction -Name $GitAction -BranchName $GitBranch }
        'workflow' { Invoke-GameWipWorkflowAction -Name $WorkflowAction -WorkflowId $Workflow }
        'configure' {
            if ([string]::IsNullOrWhiteSpace($Preset)) { $Preset = $CommandConfig.DefaultConfigurePreset }
            Invoke-ConfigurePreset -Name $Preset
            Write-NextStepHint "build it with: .\gamewip.bat build -Preset $Preset"
        }
        'build' {
            if ([string]::IsNullOrWhiteSpace($Preset)) { $Preset = $CommandConfig.DefaultBuildPreset }
            Invoke-BuildPreset -Name $Preset
            if ((Get-VisiblePresetNames -Kind 'test') -contains $Preset)
            {
                Write-NextStepHint "run tests with: .\gamewip.bat test -Preset $Preset"
            }
            elseif ($Preset -eq 'benchmark')
            {
                Write-NextStepHint 'run benchmark registration with: .\gamewip.bat run -ProjectCommand benchmark-dry-run'
            }
        }
        'test' {
            if ([string]::IsNullOrWhiteSpace($Preset)) { $Preset = $CommandConfig.DefaultTestPreset }
            Invoke-TestPreset -Name $Preset -UseWorkspaceTemp
            Write-NextStepHint 'run a focused command with: .\gamewip.bat wizard'
        }
        'wizard' {
            Invoke-ValidationCommandWizard
        }
        'module' {
            if ([string]::IsNullOrWhiteSpace($Module)) { $Module = $CommandConfig.DefaultModule }
            Invoke-ValidationModule -Name $Module -Arguments $ExtraArgs -ForceBuild:$BuildIfMissing
            Write-NextStepHint "stress this module with: .\gamewip.bat stress -Module $Module -Count 100 -Parallel 16"
        }
        'stress' {
            if ([string]::IsNullOrWhiteSpace($Module)) { $Module = $CommandConfig.DefaultModule }
            if ($Count -le 0) { $Count = [int]$CommandConfig.DefaultStressCount }
            if ($Parallel -le 0) { $Parallel = [int]$CommandConfig.DefaultStressParallel }
            Invoke-StressModule -Name $Module -RunCount $Count -MaxParallel $Parallel -Arguments $ExtraArgs -ForceBuild:$BuildIfMissing
        }
        'run' {
            if ([string]::IsNullOrWhiteSpace($ProjectCommand)) { $ProjectCommand = 'benchmark-dry-run' }
            Invoke-ProjectCommand -Id $ProjectCommand -ForceBuild:$BuildIfMissing
        }
        'bundle' {
            if ([string]::IsNullOrWhiteSpace($Bundle)) { $Bundle = 'quick' }
            Invoke-Bundle -Id $Bundle
        }
        'docs' {
            Invoke-ConfigurePreset -Name 'docs'
            Invoke-BuildPreset -Name 'docs'
        }
        'analysis' {
            Invoke-ConfigurePreset -Name 'analyze'
            Invoke-BuildPreset -Name 'analyze'
        }
        'coverage' {
            Invoke-ConfigurePreset -Name 'coverage'
            Invoke-BuildPreset -Name 'coverage'
            Invoke-TestPreset -Name 'coverage' -UseWorkspaceTemp
            Invoke-BuildTarget -Name 'coverage' -Target 'coverage'
        }
        'asan' {
            Invoke-ConfigurePreset -Name 'asan'
            Invoke-BuildPreset -Name 'asan'
            Invoke-TestPreset -Name 'asan' -UseWorkspaceTemp
        }
        'benchmark' {
            Invoke-ConfigurePreset -Name 'benchmark'
            Invoke-BuildPreset -Name 'benchmark'
            Invoke-ProjectCommand -Id 'benchmark-dry-run' -ForceBuild
        }
        'list' { Show-ProjectCatalog }
        'help' { Show-Help }
    }
}
catch
{
    Show-ActionFailure -ErrorRecord $_
    exit 1
}
finally
{
    Save-RunSummary
}
