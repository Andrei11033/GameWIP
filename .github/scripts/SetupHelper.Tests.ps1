# Regression tests for Windows setup catalogs, providers, ownership, and lifecycle contracts.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$setupScript = Join-Path $repositoryRoot 'scripts\setup\Windows.ps1'
$powerShellPath = (Get-Process -Id $PID).Path
$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$actionConfig = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'scripts\setup\config\setup.json') | ConvertFrom-Json
$actions = @($actionConfig.Actions)

$duplicates = @($actions | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object { $_.Count -gt 1 })
if ($duplicates.Count -ne 0)
{
    throw "Duplicate setup action IDs: $($duplicates.Name -join ', ')."
}

$menuActions = @($actions | Where-Object { $null -ne $_.PSObject.Properties['key'] })
$duplicateKeys = @($menuActions | ForEach-Object { [string]$_.Key } | Group-Object | Where-Object { $_.Count -gt 1 })
if ($duplicateKeys.Count -ne 0)
{
    throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')."
}

foreach ($requiredAction in @('menu', 'full', 'check', 'update', 'repair', 'uninstall', 'tools', 'visual-studio', 'msys2', 'repository', 'profiler', 'editor', 'docs', 'list', 'help'))
{
    if (@($actions | ForEach-Object { $_.Id }) -notcontains $requiredAction)
    {
        throw "Missing setup action '$requiredAction'."
    }
}

if ($isWindowsHost)
{
    $output = @(& $powerShellPath -NoProfile -ExecutionPolicy Bypass -File $setupScript list 2>&1)
    if ($LASTEXITCODE -ne 0)
    {
        throw "setup list failed: $($output -join [Environment]::NewLine)"
    }
    foreach ($action in $actions)
    {
        if (($output -join "`n") -notmatch [regex]::Escape([string]$action.Id))
        {
            throw "setup list omitted '$($action.Id)'."
        }
    }

    $setupLauncher = Join-Path $repositoryRoot 'setup.bat'
    foreach ($helpAlias in @('help', '--help', '-h', '-?'))
    {
        $helpOutput = @(& $setupLauncher $helpAlias 2>&1)
        if ($LASTEXITCODE -ne 0)
        {
            throw "setup '$helpAlias' failed: $($helpOutput -join [Environment]::NewLine)"
        }
        if (($helpOutput -join "`n") -notmatch [regex]::Escape('setup.bat [action]'))
        {
            throw "setup '$helpAlias' did not print command-line help."
        }
    }
}

. (Join-Path $repositoryRoot 'scripts\setup\lib\Msys2.ps1')

$script:simulatedMsys2Attempts = 0
$script:simulatedMsys2FailuresRemaining = 0
function Invoke-GameWipMsys2
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][string]$Command
    )

    $null = $MsysRoot, $Command
    ++$script:simulatedMsys2Attempts
    if ($script:simulatedMsys2FailuresRemaining -gt 0)
    {
        --$script:simulatedMsys2FailuresRemaining
        throw "Simulated pacman failure on attempt $script:simulatedMsys2Attempts."
    }
}

$script:simulatedMsys2FailuresRemaining = 1
$retryOutput = @(& {
        Invoke-GameWipMsys2PacmanWithRetry -MsysRoot 'C:\TestMsys2' -Command 'pacman -Syu --noconfirm' -RetryDelaySeconds 0
    } *>&1)
if ($script:simulatedMsys2Attempts -ne 2)
{
    throw "Expected fail-once pacman retry to make 2 attempts; observed $script:simulatedMsys2Attempts."
}
$retryText = $retryOutput -join "`n"
if ($retryText -notmatch 'pacman attempt 1 of 3')
{
    throw 'Pacman retry output omitted the first attempt.'
}
if ($retryText -notmatch 'pacman attempt 2 of 3')
{
    throw 'Pacman retry output omitted the successful retry.'
}
if ($retryText -notmatch 'Retrying pacman in 0 second\(s\)')
{
    throw 'Pacman retry output omitted the retry delay.'
}

$script:simulatedMsys2Attempts = 0
$script:simulatedMsys2FailuresRemaining = 4
$script:simulatedMsys2FinalError = $null
$finalFailureOutput = @(& {
        try
        {
            Invoke-GameWipMsys2PacmanWithRetry -MsysRoot 'C:\TestMsys2' -Command 'pacman --needed --noconfirm -S test-package' -RetryDelaySeconds 0
        }
        catch
        {
            $script:simulatedMsys2FinalError = $_
        }
    } *>&1)
if ($script:simulatedMsys2Attempts -ne 3)
{
    throw "Expected final pacman failure after 3 attempts; observed $script:simulatedMsys2Attempts."
}
if ($null -eq $script:simulatedMsys2FinalError)
{
    throw 'Expected the final pacman error to propagate.'
}
if ($script:simulatedMsys2FinalError.Exception.Message -ne 'Simulated pacman failure on attempt 3.')
{
    throw "Pacman retry propagated the wrong error: $($script:simulatedMsys2FinalError.Exception.Message)"
}
$finalFailureText = $finalFailureOutput -join "`n"
if ($finalFailureText -notmatch 'pacman attempt 3 of 3')
{
    throw 'Pacman retry output omitted the final attempt.'
}
if ($finalFailureText -notmatch 'pacman failed after 3 attempts')
{
    throw 'Pacman retry output omitted the final failure summary.'
}

$script:capturedPacmanCommands = @()
function Invoke-GameWipMsys2PacmanWithRetry
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][string]$Command,
        [int]$MaxAttempts = 3,
        [int]$RetryDelaySeconds = 2
    )

    $null = $MsysRoot, $MaxAttempts, $RetryDelaySeconds
    $script:capturedPacmanCommands += $Command
}
function Test-GameWipMsys2PackageSet
{
    return $true
}

$testPackageConfig = @{
    Common = @('common-package')
    Ucrt64 = @('ucrt-package')
    Clang64 = @('clang-package')
}
Install-GameWipMsys2PackageSet -MsysRoot 'C:\TestMsys2' -PackageConfig $testPackageConfig -Update
$expectedPacmanCommands = @(
    'pacman -Syu --noconfirm',
    'pacman -Syu --noconfirm',
    'pacman --needed --noconfirm -S clang-package common-package ucrt-package'
)
if (@(Compare-Object -ReferenceObject $expectedPacmanCommands -DifferenceObject $script:capturedPacmanCommands -SyncWindow 0).Count -ne 0)
{
    throw "MSYS2 package installation did not route every pacman mutation through the retry wrapper: $($script:capturedPacmanCommands -join '; ')"
}

foreach ($forbiddenProperty in @('wingetPackages', 'msys2Packages', 'cmakeVersionPattern'))
{
    if ($null -ne $actionConfig.PSObject.Properties[$forbiddenProperty])
    {
        throw "Setup registry still duplicates project-tool authority '$forbiddenProperty'."
    }
}

$windowsSource = Get-Content -Raw -LiteralPath $setupScript
$completeSetupMatch = [regex]::Match($windowsSource, '(?ms)function Invoke-GameWipCompleteSetup.*?^}')
if (-not $completeSetupMatch.Success)
{
    throw 'Could not inspect complete setup stage order.'
}
$completeSetup = $completeSetupMatch.Value
if ($completeSetup.IndexOf('Invoke-GameWipMsys2Step') -gt $completeSetup.IndexOf('Invoke-GameWipToolStep'))
{
    throw 'Complete setup must bootstrap/configure MSYS2 before project-tool installation.'
}

$fakeTools = @{
    tools = @(
        @{
            id = 'one'
            provider = @{
                kind = 'msys2'
                environment = 'ucrt64'
                package = 'ucrt-one'
                dependencies = @(@{ environment = 'clang64'; package = 'clang-one' })
            }
        },
        @{
            id = 'two'
            provider = @{
                kind = 'msys2'
                environment = 'common'
                package = 'common-two'
                dependencies = @()
            }
        }
    )
}
$derivedPackages = Get-GameWipMsys2PackageConfig -ProjectTools $fakeTools
if (@($derivedPackages.Common) -notcontains 'common-two' -or
    @($derivedPackages.Ucrt64) -notcontains 'ucrt-one' -or
    @($derivedPackages.Clang64) -notcontains 'clang-one')
{
    throw 'MSYS2 setup package derivation did not consume canonical provider metadata.'
}

$projectConfigJson = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'scripts\config\project.json') | ConvertFrom-Json
$ProjectConfig = @{
    managedEnvironment = @{
        msys2Root = [string]$projectConfigJson.managedEnvironment.msys2Root
        gameWipToolsRoot = [string]$projectConfigJson.managedEnvironment.gameWipToolsRoot
    }
    storage = @{
        state = [string]$projectConfigJson.storage.state
        temp = [string]$projectConfigJson.storage.temp
    }
}
. (Join-Path $repositoryRoot 'scripts\lib\Tools.ps1')
$venvInterpreter = Get-GameWipPythonEnvironmentInterpreterPath -Root 'X:\GameWIPTools\python'
if ($isWindowsHost -and $venvInterpreter -notmatch '[\\/]Scripts[\\/]python\.exe$')
{
    throw "Windows Python provider resolved the wrong venv interpreter: $venvInterpreter"
}
if (-not $isWindowsHost -and $venvInterpreter -notmatch '[\\/]bin[\\/]python$')
{
    throw "Non-Windows Python provider resolved the wrong venv interpreter: $venvInterpreter"
}

if (-not (Test-GameWipWingetNoUpdateExitCode -ExitCode -1978335189) -or
    (Test-GameWipWingetNoUpdateExitCode -ExitCode 1))
{
    throw 'WinGet no-applicable-update classification is not narrow enough.'
}

if ($isWindowsHost)
{
    $providerTestRoot = Join-Path $repositoryRoot (
        'build\gamewip\temp\setup-helper-python-' + [guid]::NewGuid().ToString('N')
    )
    $originalProjectConfig = $ProjectConfig
    try
    {
        $ProjectConfig = @{
            managedEnvironment = @{
                msys2Root = 'C:\TestMsys2'
                gameWipToolsRoot = Join-Path $providerTestRoot 'GameWIPTools'
            }
        }
        function Resolve-GameWipPython
        {
            return [pscustomobject]@{ Path = 'mock-system-python.exe' }
        }
        function New-GameWipPythonToolEnvironment
        {
            param([string]$SystemPython, [string]$Root)
            $null = $SystemPython
            $scripts = Join-Path $Root 'Scripts'
            New-Item -ItemType Directory -Force -Path $scripts | Out-Null
            New-Item -ItemType File -Force -Path (Join-Path $scripts 'python.exe') | Out-Null
        }
        function Install-GameWipPythonPackageSpecification
        {
            param([string]$Python, [string]$Specification)
            $null = $Python
            if ($Specification -ne 'ruff==0.16.4')
            {
                throw "Unexpected mocked Python package specification '$Specification'."
            }
            $scripts = Split-Path -Parent $Python
            @'
@echo off
echo ruff 0.16.4
'@ | Set-Content -LiteralPath (Join-Path $scripts 'ruff.cmd') -Encoding Ascii
        }
        function Resolve-GameWipPythonProviderHost
        {
            return [pscustomobject]@{ Path = 'mock-system-python.exe'; Source = 'test mock' }
        }
        function Test-GameWipPythonEnvironmentIsMsys
        {
            param([string]$PythonPath)
            return $false
        }

        $ruffTool = @{
            id = 'ruff'
            versionPolicy = 'exact'
            requiredVersion = '0.16.4'
            capabilities = @{ detectInstalled = $true }
            detection = @{
                command = 'ruff'
                versionArguments = @('--version')
                versionPattern = 'ruff\s+([0-9]+\.[0-9]+\.[0-9]+)'
            }
            provider = @{ kind = 'python'; package = 'ruff' }
        }
        Install-GameWipPythonTool -Tool $ruffTool -Version '0.16.4'
        $detectedRuff = Get-GameWipDetectedTool -Tool $ruffTool
        if (-not $detectedRuff.Installed -or $detectedRuff.Version -ne '0.16.4')
        {
            throw 'Mocked Python provider create/install/detect lifecycle failed.'
        }
    }
    finally
    {
        $ProjectConfig = $originalProjectConfig
        if (Test-Path -LiteralPath $providerTestRoot)
        {
            Remove-Item -LiteralPath $providerTestRoot -Recurse -Force
        }
    }
}

# Advisory editor selection must recover from corrupt disposable state.
$editorTestRoot = Join-Path $repositoryRoot (
    'build\gamewip\temp\setup-helper-editor-' + [guid]::NewGuid().ToString('N')
)
$originalProjectConfigForEditor = $ProjectConfig
try
{
    $ProjectConfig = @{ storage = @{ state = 'build/gamewip/state' } }
    . (Join-Path $repositoryRoot 'scripts\setup\lib\Editor.ps1')
    $editorConfig = @{
        Default = @('vscode')
        Options = @(@{ Id = 'vscode' })
    }
    $statePath = Get-GameWipEditorPreferencePath -RepositoryRoot $editorTestRoot
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $statePath) | Out-Null
    'not-json' | Set-Content -LiteralPath $statePath -Encoding UTF8
    $selection = @(Get-GameWipEditorSelection -RepositoryRoot $editorTestRoot -EditorConfig $editorConfig)
    if ($selection.Count -ne 1 -or $selection[0] -ne 'vscode')
    {
        throw 'Corrupt advisory editor state did not fall back to the configured default.'
    }
}
finally
{
    $ProjectConfig = $originalProjectConfigForEditor
    if (Test-Path -LiteralPath $editorTestRoot)
    {
        Remove-Item -LiteralPath $editorTestRoot -Recurse -Force
    }
}

# Uninstall preview must classify/report without mutating ownership-unknown roots.
$uninstallTestRoot = Join-Path $repositoryRoot (
    'build\gamewip\temp\setup-helper-uninstall-' + [guid]::NewGuid().ToString('N')
)
$originalProjectConfigForUninstall = $ProjectConfig
try
{
    $testMsysRoot = Join-Path $uninstallTestRoot 'MSYS2'
    $testToolsRoot = Join-Path $testMsysRoot 'GameWIPTools'
    New-Item -ItemType Directory -Force -Path $testToolsRoot | Out-Null
    'unknown' | Set-Content -LiteralPath (Join-Path $testToolsRoot 'existing.txt') -Encoding UTF8
    $ProjectConfig = @{
        managedEnvironment = @{
            msys2Root = $testMsysRoot
            gameWipToolsRoot = $testToolsRoot
        }
        storage = @{
            cache = 'build/gamewip/cache'
            state = 'build/gamewip/state'
        }
    }
    $ProjectTools = @{ tools = @() }
    $script:SetupStatePath = Join-Path $uninstallTestRoot 'state\setup.json'
    $script:SetupRun = $null
    . (Join-Path $repositoryRoot 'scripts\setup\lib\Common.ps1')
    . (Join-Path $repositoryRoot 'scripts\setup\lib\Uninstall.ps1')
    $previewOutput = @(
        Invoke-GameWipUninstall -RepositoryRoot $repositoryRoot -ProjectTools $ProjectTools -Preview *>&1
    )
    if (($previewOutput -join "`n") -notmatch 'Preview complete; no resources were changed')
    {
        throw 'Uninstall preview did not report its non-mutating completion contract.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $testToolsRoot 'existing.txt')))
    {
        throw 'Uninstall preview mutated an ownership-unknown GameWIPTools root.'
    }
}
finally
{
    $ProjectConfig = $originalProjectConfigForUninstall
    if (Test-Path -LiteralPath $uninstallTestRoot)
    {
        Remove-Item -LiteralPath $uninstallTestRoot -Recurse -Force
    }
}

$editorSource = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'scripts\setup\lib\Editor.ps1')
if ($editorSource -match 'build[\\/]setup')
{
    throw 'Editor integration still stages mutable helper data under retired build/setup.'
}
$uninstallSource = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'scripts\setup\lib\Uninstall.ps1')
if ($uninstallSource -notmatch '--uninstall-extension')
{
    throw 'Uninstall does not remove the repository-owned VS Code workflow extension through the VS Code CLI.'
}
if ($windowsSource -notmatch 'Initialize-GameWipSetupManagedToolRoot' -or
    $windowsSource -notmatch 'Adopt this existing GameWIPTools directory')
{
    throw 'Interactive setup does not expose explicit GameWIPTools ownership adoption.'
}
if ($windowsSource -notmatch 'cannot be adopted noninteractively')
{
    throw 'Noninteractive setup no longer fails closed for an unowned non-empty GameWIPTools root.'
}
if ($windowsSource -notmatch 'MSYS2 must be configured before persistent GameWIPTools can be created')
{
    throw 'Focused setup can create GameWIPTools before the MSYS2 provider root exists.'
}
$environmentCheckMatch = [regex]::Match(
    $windowsSource,
    '(?ms)function Invoke-GameWipEnvironmentCheck.*?(?=^function |\z)'
)
if (-not $environmentCheckMatch.Success -or
    $environmentCheckMatch.Value -notmatch 'no valid persistent ownership proof')
{
    throw 'Read-only environment check does not diagnose unknown GameWIPTools ownership.'
}
$tracyStepMatch = [regex]::Match(
    $windowsSource,
    '(?ms)function Invoke-GameWipTracyStep.*?(?=^function |\z)'
)
if (-not $tracyStepMatch.Success -or
    $tracyStepMatch.Value -notmatch 'Initialize-GameWipSetupManagedToolRoot')
{
    throw 'Focused Tracy setup does not establish or resolve GameWIPTools ownership first.'
}
if ($windowsSource -notmatch "(?s)Complete-GameWipSetupRun -Status 'passed'.*completed successfully")
{
    throw 'Setup reports success before run cleanup/finalization completes.'
}
foreach ($pathOwner in @(
    'scripts\setup\lib\Repository.ps1',
    'scripts\setup\lib\Documentation.ps1'
))
{
    $source = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot $pathOwner)
    if ($source -match [regex]::Escape('C:\MSYS2\ucrt64\bin'))
    {
        throw "$pathOwner still duplicates the configured MSYS2 root."
    }
}

Write-Host 'Setup helper regression tests passed.'
