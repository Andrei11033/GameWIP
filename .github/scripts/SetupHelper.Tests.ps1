[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$setupScript = Join-Path $repositoryRoot 'scripts\setup\windows.ps1'
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
    'pacman --needed --noconfirm -S common-package ucrt-package clang-package'
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

. (Join-Path $repositoryRoot 'scripts\lib\Providers\Python.ps1')
$venvInterpreter = Get-GameWipPythonEnvironmentInterpreterPath -Root 'X:\GameWIPTools\python'
if ($isWindowsHost -and $venvInterpreter -notmatch '[\\/]Scripts[\\/]python\.exe$')
{
    throw "Windows Python provider resolved the wrong venv interpreter: $venvInterpreter"
}
if (-not $isWindowsHost -and $venvInterpreter -notmatch '[\\/]bin[\\/]python$')
{
    throw "Non-Windows Python provider resolved the wrong venv interpreter: $venvInterpreter"
}

Write-Host 'Setup helper regression tests passed.'
