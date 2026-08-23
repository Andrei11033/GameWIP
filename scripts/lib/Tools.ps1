# GameWIP Tools helper behavior. Dot-sourced by scripts/GameWIP.ps1.

foreach ($providerFile in @('Msys2.ps1', 'Npm.ps1', 'Python.ps1', 'PowerShellGallery.ps1', 'GitHubRelease.ps1', 'Winget.ps1', 'GitSubmodule.ps1', 'External.ps1'))
{
    . (Join-Path $PSScriptRoot (Join-Path 'Providers' $providerFile))
}

function Get-GameWipManagedToolPath
{
    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    return @(
        (Join-Path $root 'bin'), (Join-Path $root 'npm'), (Join-Path $root 'npm\bin'),
        (Join-Path $root 'python\Scripts'), (Join-Path $root 'python\bin'), (Join-Path $root 'powershell')
    )
}

function Initialize-GameWipManagedToolRoot
{
    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    foreach ($name in @('bin', 'tools', 'npm', 'python', 'powershell'))
    {
        New-Item -ItemType Directory -Force -Path (Join-Path $root $name) | Out-Null
    }
}

function Write-GameWipManagedToolShim
{
    param(
        [Parameter(Mandatory = $true)][string]$ToolId,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$ExecutableName
    )

    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    $binRoot = Join-Path $root 'bin'
    New-Item -ItemType Directory -Force -Path $binRoot | Out-Null
    $relativeExecutable = "..\tools\$ToolId\$Version\$ExecutableName"
    $shimPath = Join-Path $binRoot "$ToolId.cmd"
    $shim = "@echo off`r`n`"%~dp0$relativeExecutable`" %*`r`n"
    [IO.File]::WriteAllText($shimPath, $shim, [Text.UTF8Encoding]::new($false))
}

function Resolve-GameWipToolCommand
{
    param([Parameter(Mandatory = $true)][string]$Command)
    foreach ($name in @("$Command.exe", "$Command.cmd", "$Command.bat", "$Command.ps1", $Command))
    {
        $candidate = Join-Path $RepositoryRoot $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf)
        {
            return $candidate
        }
    }
    foreach ($root in @(Get-GameWipManagedToolPath))
    {
        foreach ($name in @("$Command.exe", "$Command.cmd", "$Command.bat", "$Command.ps1", $Command))
        {
            $candidate = Join-Path $root $name
            if (Test-Path -LiteralPath $candidate -PathType Leaf)
            {
                return $candidate
            }
        }
    }
    foreach ($environment in @('ucrt64', 'clang64', 'usr'))
    {
        $candidate = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) "$environment\bin\$Command.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf)
        {
            return $candidate
        }
    }
    $resolved = Get-Command $Command -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $resolved)
    {
        return $resolved.Source
    }
    return $null
}

function Get-GameWipDetectedTool
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)
    if ($Tool.provider.kind -eq 'powershellGallery')
    {
        $moduleRoot = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) "powershell\$($Tool.provider.package)"
        $versionDirectory = Get-ChildItem -LiteralPath $moduleRoot -Directory -ErrorAction SilentlyContinue | Sort-Object { try
            {
                [version]$_.Name
            }
            catch
            {
                [version]'0.0'
            } } -Descending | Select-Object -First 1
        return [pscustomobject]@{ Installed = $null -ne $versionDirectory; Version = if ($null -ne $versionDirectory)
            {
                $versionDirectory.Name
            }
            else
            {
                $null
            }; Location = if ($null -ne $versionDirectory)
            {
                $versionDirectory.FullName
            }
            else
            {
                $null
            }
        }
    }
    $commandPath = Resolve-GameWipToolCommand -Command ([string]$Tool.detection.command)
    if ($null -eq $commandPath)
    {
        return [pscustomobject]@{ Installed = $false; Version = $null; Location = $null }
    }
    $previousPath = $env:PATH
    $previousErrorActionPreference = $ErrorActionPreference
    try
    {
        $env:PATH = (@(Get-GameWipManagedToolPath) + @($env:PATH)) -join ';'
        $ErrorActionPreference = 'Continue'
        $output = (& $commandPath @($Tool.detection.versionArguments) 2>&1 | Out-String).Trim()
        $match = [regex]::Match($output, [string]$Tool.detection.versionPattern)
        if ($LASTEXITCODE -ne 0 -and -not $match.Success)
        {
            return [pscustomobject]@{ Installed = $true; Version = $null; Location = $commandPath }
        }
        return [pscustomobject]@{ Installed = $true; Version = if ($match.Success)
            {
                $match.Groups[1].Value
            }
            else
            {
                $null
            }; Location = $commandPath
        }
    }
    finally
    {
        $env:PATH = $previousPath; $ErrorActionPreference = $previousErrorActionPreference
    }
}

function Get-GameWipToolCompatibility
{
    param([hashtable]$Tool, $Detected)
    if (-not $Detected.Installed)
    {
        return 'missing'
    }
    if ($Tool.versionPolicy -in @('managed', 'informational'))
    {
        return 'compatible'
    }
    if ([string]::IsNullOrWhiteSpace([string]$Detected.Version))
    {
        return 'unknown'
    }
    if ($Tool.versionPolicy -eq 'exact')
    {
        return $(if ($Detected.Version -eq $Tool.requiredVersion)
            {
                'compatible'
            }
            else
            {
                'mismatch'
            })
    }
    try
    {
        return $(if ([version]$Detected.Version -ge [version]$Tool.requiredVersion)
            {
                'compatible'
            }
            else
            {
                'outdated'
            })
    }
    catch
    {
        return 'unknown'
    }
}

function Get-GameWipProviderFunction
{
    param([hashtable]$Tool, [ValidateSet('Latest', 'Install')][string]$Operation)
    $providerName = switch ([string]$Tool.provider.kind)
    {
        'msys2'
        {
            'Msys2'
        }; 'npm'
        {
            'Npm'
        }; 'python'
        {
            'Python'
        }; 'powershellGallery'
        {
            'PowerShellGallery'
        }
        'githubRelease'
        {
            'GitHubRelease'
        }; 'winget'
        {
            'Winget'
        }; 'gitSubmodule'
        {
            'GitSubmodule'
        }; 'external'
        {
            'External'
        }
        default
        {
            throw "Unsupported tool provider '$($Tool.provider.kind)'."
        }
    }
    if ($Operation -eq 'Latest')
    {
        return "Get-GameWip${providerName}ToolLatestVersion"
    }
    return "Install-GameWip${providerName}Tool"
}

function Show-GameWipToolList
{
    Write-GameWipSection 'Project tools'
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $capabilities = @('detectInstalled', 'checkLatest', 'update') | Where-Object { [bool]$toolInfo.capabilities[$_] }
        Write-Host ('  {0,-20} {1,-24} {2,-16} {3,-18} {4,-13} {5}' -f $toolInfo.id, $toolInfo.name, $toolInfo.category, $toolInfo.provider.kind, $toolInfo.versionPolicy, ($capabilities -join ','))
    }
}

function Show-GameWipToolStatus
{
    Initialize-GameWipStorage
    Write-GameWipSection 'Project tool status (offline)'
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $required = if ($toolInfo.Contains('requiredVersion'))
        {
            $toolInfo.requiredVersion
        }
        else
        {
            '-'
        }
        $installed = if ($detected.Installed -and $detected.Version)
        {
            $detected.Version
        }
        elseif ($detected.Installed)
        {
            'detected'
        }
        else
        {
            'missing'
        }
        Write-Host ('  {0,-20} required={1,-12} installed={2,-12} state={3,-10} provider={4,-18}' -f $toolInfo.id, $required, $installed, (Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected), $toolInfo.provider.kind)
        Write-Host ('    location={0}; detect={1}; latest={2}; update={3}' -f $(if ($detected.Location)
                {
                    $detected.Location
                }
                else
                {
                    '-'
                }), $toolInfo.capabilities.detectInstalled, $toolInfo.capabilities.checkLatest, $toolInfo.capabilities.update)
    }
}

function Get-GameWipToolUpdatePlan
{
    param([string]$ToolId)
    $selected = if ([string]::IsNullOrWhiteSpace($ToolId) -or $ToolId -eq 'all')
    {
        @($ProjectTools.tools)
    }
    else
    {
        @($ProjectTools.tools | Where-Object { $_.id -eq $ToolId })
    }
    if ($selected.Count -eq 0)
    {
        throw "Unknown project tool '$ToolId'."
    }
    $plan = @()
    foreach ($toolInfo in $selected)
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $latest = $null
        if ($toolInfo.capabilities.checkLatest)
        {
            $functionName = Get-GameWipProviderFunction -Tool $toolInfo -Operation Latest
            $latest = & $functionName -Tool $toolInfo
        }
        $plan += [pscustomobject]@{ Tool = $toolInfo; Installed = $detected.Version; Latest = $latest }
    }
    return $plan
}

function Show-GameWipToolUpdatePlan
{
    param([array]$Plan)
    foreach ($item in $Plan)
    {
        $required = if ($item.Tool.Contains('requiredVersion'))
        {
            $item.Tool.requiredVersion
        }
        else
        {
            '-'
        }
        Write-Host ('  {0,-20} required={1,-12} installed={2,-12} latest={3}' -f $item.Tool.id, $required, $(if ($item.Installed)
                {
                    $item.Installed
                }
                else
                {
                    'missing'
                }), $(if ($item.Latest)
                {
                    $item.Latest
                }
                else
                {
                    'not-supported/unknown'
                }))
    }
}

function Invoke-GameWipToolUpdate
{
    param([string]$ToolId, [switch]$PreviewOnly)
    if ([string]::IsNullOrWhiteSpace($ToolId))
    {
        throw "tools update requires -Tool <id|all>."
    }
    if (-not $PreviewOnly)
    {
        Assert-GameWipCleanTrackedTree
    }
    $plan = @(Get-GameWipToolUpdatePlan -ToolId $ToolId)
    Write-GameWipSection "Tool update plan$(if ($PreviewOnly) { ' (preview)' })"
    Show-GameWipToolUpdatePlan -Plan $plan
    if ($PreviewOnly)
    {
        return
    }
    Initialize-GameWipManagedToolRoot
    foreach ($item in $plan)
    {
        if (-not $item.Tool.capabilities.update)
        {
            Write-Host "  [skip] $($item.Tool.id): provider does not support updates"; continue
        }
        $version = if ($item.Tool.versionPolicy -eq 'exact' -and $item.Latest)
        {
            $item.Latest
        }
        elseif ($item.Tool.Contains('requiredVersion'))
        {
            $item.Tool.requiredVersion
        }
        else
        {
            $null
        }
        if ($item.Tool.versionPolicy -eq 'exact' -and $item.Latest -and $item.Latest -ne $item.Tool.requiredVersion)
        {
            Sync-GameWipToolVersionReference -Tool $item.Tool -NewVersion $item.Latest
        }
        $functionName = Get-GameWipProviderFunction -Tool $item.Tool -Operation Install
        & $functionName -Tool $item.Tool -Version $version
    }
    Invoke-GameWipQuality -Mode check
    & git -C $RepositoryRoot status --short
}

function Sync-GameWipToolVersionReference
{
    param([hashtable]$Tool, [Parameter(Mandatory = $true)][string]$NewVersion)
    $oldVersion = [string]$Tool.requiredVersion
    foreach ($reference in @($Tool.references | Where-Object { $_ -ne 'scripts/config/project-tools.json' }))
    {
        if ($reference.StartsWith('docs/releases/'))
        {
            continue
        }
        $path = Join-Path $RepositoryRoot $reference
        $content = Get-Content -Raw -LiteralPath $path
        if (-not $content.Contains($oldVersion))
        {
            throw "Ambiguous live version reference for '$($Tool.id)': '$reference' does not contain '$oldVersion'."
        }
        $updated = $content.Replace($oldVersion, $NewVersion)
        [IO.File]::WriteAllText($path, $updated, [Text.UTF8Encoding]::new($false))
    }

    $registry = Get-Content -Raw -LiteralPath $ProjectToolsPath | ConvertFrom-Json
    $registryTool = $registry.tools | Where-Object { $_.id -eq $Tool.id } | Select-Object -First 1
    $registryTool.requiredVersion = $NewVersion
    if ($Tool.provider.kind -eq 'githubRelease')
    {
        $metadata = Get-GameWipGitHubReleaseMetadata -Tool $Tool
        if ($metadata.Version -ne $NewVersion -or $metadata.Assets.Count -lt 2)
        {
            throw "Incomplete verified release metadata for '$($Tool.id)' $NewVersion."
        }
        foreach ($key in @('linux-amd64', 'windows-amd64'))
        {
            $registryTool.provider.assets.$key.archive = $metadata.Assets[$key].archive
            $registryTool.provider.assets.$key.sha256 = $metadata.Assets[$key].sha256
            $Tool.provider.assets[$key].archive = $metadata.Assets[$key].archive
            $Tool.provider.assets[$key].sha256 = $metadata.Assets[$key].sha256
        }
    }
    $registryJson = $registry | ConvertTo-Json -Depth 30
    [IO.File]::WriteAllText($ProjectToolsPath, "$registryJson`n", [Text.UTF8Encoding]::new($false))
    $Tool.requiredVersion = $NewVersion
}

function Invoke-GameWipToolAction
{
    param([Parameter(Mandatory = $true)][string]$Name, [string]$ToolId, [switch]$PreviewOnly)
    switch ($Name)
    {
        'list'
        {
            Show-GameWipToolList
        }
        'status'
        {
            Show-GameWipToolStatus
        }
        'check-updates'
        {
            Write-GameWipSection 'Project tool updates (online)'; Show-GameWipToolUpdatePlan -Plan @(Get-GameWipToolUpdatePlan -ToolId $ToolId)
        }
        'update'
        {
            Invoke-GameWipToolUpdate -ToolId $ToolId -PreviewOnly:$PreviewOnly
        }
    }
}

function Get-GameWipToolchainPathPrefix
{
    param([Parameter(Mandatory = $true)][string]$PresetName)

    if ($PresetName -eq 'asan')
    {
        return Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'clang64\bin'
    }
    return Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin'
}

function Test-GameWipProjectReadiness
{
    param([switch]$ThrowOnFailure)

    $msys2Root = [string]$ProjectConfig.managedEnvironment.msys2Root
    $requirements = @(
        @{ Name = 'UCRT64 CMake'; Path = Join-Path $msys2Root 'ucrt64\bin\cmake.exe' }
        @{ Name = 'UCRT64 Ninja'; Path = Join-Path $msys2Root 'ucrt64\bin\ninja.exe' }
        @{ Name = 'UCRT64 C++ compiler'; Path = Join-Path $msys2Root 'ucrt64\bin\g++.exe' }
        @{ Name = 'UCRT64 Python'; Path = Join-Path $msys2Root 'ucrt64\bin\python.exe' }
        @{ Name = 'UCRT64 clang-format'; Path = Join-Path $msys2Root 'ucrt64\bin\clang-format.exe' }
        @{ Name = 'CLANG64 C++ compiler'; Path = Join-Path $msys2Root 'clang64\bin\clang++.exe' }
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
    if ($drive.IsReady)
    {
        Write-Host ('  Free disk space: {0:N1} GB' -f ($drive.AvailableFreeSpace / 1GB))
    }

    if ($failures.Count -ne 0)
    {
        $message = "$($failures.Count) project requirement(s) are missing. Run .\setup.bat repair, then rerun gamewip."
        if ($ThrowOnFailure)
        {
            throw $message
        }
        Write-Host "  $message" -ForegroundColor Yellow
        return $false
    }
    Write-Host '  Ready: the project toolchain is available.' -ForegroundColor Green
    return $true
}

function Confirm-GameWipToolchain
{
    param([Parameter(Mandatory = $true)][string]$PresetName)
    $prefix = Get-GameWipToolchainPathPrefix $PresetName
    if (-not (Test-Path -LiteralPath (Join-Path $prefix 'cmake.exe')))
    {
        Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null
    }
}

function Resolve-GameWipPython
{
    $configuredPath = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin\python.exe'
    $candidate = $null
    $source = $null

    if (-not [string]::IsNullOrWhiteSpace($PythonPath))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $PythonPath
        $source = '-PythonPath override'
        if (-not (Test-Path -LiteralPath $candidate))
        {
            throw "Python override does not exist: $candidate"
        }
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_PYTHON))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $env:GAMEWIP_PYTHON
        $source = 'GAMEWIP_PYTHON override'
        if (-not (Test-Path -LiteralPath $candidate))
        {
            throw "GAMEWIP_PYTHON does not exist: $candidate"
        }
    }
    else
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $configuredPath
        $source = 'GameWIP UCRT64 toolchain'
        if (-not (Test-Path -LiteralPath $candidate))
        {
            $command = Get-Command python.exe -ErrorAction SilentlyContinue
            if ($null -ne $command)
            {
                $candidate = $command.Source
                $source = 'PATH fallback'
            }
            else
            {
                throw "UCRT64 Python is unavailable at '$candidate'. Run .\setup.bat repair to install the GameWIP toolchain."
            }
        }
    }

    $versionOutput = @(& $candidate --version 2>&1)
    $exitCode = if ($null -ne $LASTEXITCODE)
    {
        [int]$LASTEXITCODE
    }
    else
    {
        0
    }
    if ($exitCode -ne 0)
    {
        throw "Python failed to start from '$candidate' with exit code $exitCode."
    }

    [pscustomobject]@{
        Path = $candidate
        Source = $source
        Version = (($versionOutput | Out-String).Trim())
    }
}

function Resolve-GameWipClangFormat
{
    $configuredPath = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin\clang-format.exe'
    $candidate = $null
    $source = $null

    if (-not [string]::IsNullOrWhiteSpace($ClangFormatPath))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $ClangFormatPath
        $source = '-ClangFormatPath override'
        if (-not (Test-Path -LiteralPath $candidate))
        {
            throw "clang-format override does not exist: $candidate"
        }
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_CLANG_FORMAT))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $env:GAMEWIP_CLANG_FORMAT
        $source = 'GAMEWIP_CLANG_FORMAT override'
        if (-not (Test-Path -LiteralPath $candidate))
        {
            throw "GAMEWIP_CLANG_FORMAT does not exist: $candidate"
        }
    }
    else
    {
        $candidate = Resolve-GameWipRepositoryPath -Path $configuredPath
        $source = 'GameWIP UCRT64 toolchain'
        if (-not (Test-Path -LiteralPath $candidate))
        {
            $command = Get-Command clang-format.exe -ErrorAction SilentlyContinue
            if ($null -ne $command)
            {
                $candidate = $command.Source
                $source = 'PATH fallback'
            }
            else
            {
                throw "UCRT64 clang-format is unavailable at '$candidate'. Run .\setup.bat repair to install the GameWIP toolchain."
            }
        }
    }

    $versionOutput = @(& $candidate --version 2>&1)
    $exitCode = if ($null -ne $LASTEXITCODE)
    {
        [int]$LASTEXITCODE
    }
    else
    {
        0
    }
    if ($exitCode -ne 0)
    {
        throw "clang-format failed to start from '$candidate' with exit code $exitCode."
    }

    [pscustomobject]@{
        Path = $candidate
        Source = $source
        Version = (($versionOutput | Out-String).Trim())
    }
}
