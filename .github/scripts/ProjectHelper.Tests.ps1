# Regression tests for the GameWIP project-helper command and reusable bootstrap boundary.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'
$powerShellPath = (Get-Process -Id $PID).Path

# Tests consume the supported non-executable bootstrap boundary. The executable
# entrypoint is exercised only as a child process.
. (Join-Path $repositoryRoot 'scripts\lib\Bootstrap.ps1') -RepositoryRoot $repositoryRoot

# Verify repository-owned hashes before environment-dependent checks.
$hashRoot = Join-Path $repositoryRoot ('build\gamewip\temp\hash-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $hashRoot | Out-Null

try
{
    $hashPath = Join-Path $hashRoot 'sample.txt'
    [IO.File]::WriteAllText($hashPath, 'first', [Text.UTF8Encoding]::new($false))
    $firstHash = Get-GameWipFileSha256 -Path $hashPath
    $firstSha512 = Get-GameWipFileSha512 -Path $hashPath

    if ($firstHash -notmatch '^[0-9a-f]{64}$' -or $firstHash -ne (Get-GameWipFileSha256 -Path $hashPath))
    {
        throw 'Repository-owned SHA-256 hashing is not stable.'
    }

    if ($firstSha512 -notmatch '^[0-9a-f]{128}$' -or $firstSha512 -ne (Get-GameWipFileSha512 -Path $hashPath))
    {
        throw 'Repository-owned SHA-512 hashing is not stable.'
    }

    [IO.File]::WriteAllText($hashPath, 'second', [Text.UTF8Encoding]::new($false))

    if ($firstHash -eq (Get-GameWipFileSha256 -Path $hashPath) -or
        $firstSha512 -eq (Get-GameWipFileSha512 -Path $hashPath))
    {
        throw 'Repository-owned file hashing did not detect a content change.'
    }
}

finally
{
    Remove-Item -LiteralPath $hashRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------
# Command catalog and menu contracts
# ------------------------------------------------------------

$helpOutput = (& $powerShellPath -NoProfile -ExecutionPolicy Bypass -File $helperPath help 2>&1 | Out-String)
$requiredHelperUsage = "$(Get-GameWipProjectHelperCommand) <action> [command] [target]"

foreach ($requiredHelpText in @($requiredHelperUsage, 'Available commands', 'ready', 'quality', 'quality hygiene', '-FailOnFindings', 'tool', 'history', 'config [preset]', '-Preview', '-NonInteractive', '-Yes', '-SkipBuild', '-CleanBuild', '-NoColor', '-Json', '-OutputPath', '-PassThroughArgs', '-BuildBeforeRun', '-LogLevel'))
{
    if ($helpOutput -notmatch [regex]::Escape($requiredHelpText))
    {
        throw "Project helper help omits '$requiredHelpText'."
    }
}

Assert-GameWipCommandConfig
Assert-GameWipProjectToolConfig
Assert-GameWipHygieneConfig
Assert-GameWipBuildTargetConfig

$workspaceSettings = Get-Content (Join-Path $repositoryRoot '.vscode/settings.json') -Raw | ConvertFrom-Json
$compileCommandPaths = @($workspaceSettings.'C_Cpp.default.compileCommands')
$expectedCompileCommandPaths = @($BuildTargets.targets | ForEach-Object { "`${workspaceFolder}/build/$($_.id)/dev/compile_commands.json" })

if (($compileCommandPaths -join '|') -cne ($expectedCompileCommandPaths -join '|'))
{
    throw 'VS Code IntelliSense must list every target dev compilation database in target order.'
}

$qualityScopeManifest = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('quality-scope-' + [guid]::NewGuid().ToString('N') + '.json')

try
{
    # Deleted maintained paths expand checks; excluded paths remain out of scope.
    $manifest = @{ changed_files = @('docs/removed-page.md', 'external/vendor.cpp') } | ConvertTo-Json -Depth 3
    [IO.File]::WriteAllText($qualityScopeManifest, $manifest, [Text.UTF8Encoding]::new($false))
    $qualityScope = Get-GameWipQualityScope -ChangedFilesPath $qualityScopeManifest

    if (-not $qualityScope.Expanded -or $qualityScope.UseChanged -or
        $qualityScope.Files -notcontains 'docs/removed-page.md' -or $qualityScope.Files -contains 'external/vendor.cpp')
    {
        throw 'Quality scope must expand for deleted maintained paths and exclude third-party paths.'
    }

    $manifest = @{ changed_files = @('external/vendor.cpp') } | ConvertTo-Json -Depth 3
    [IO.File]::WriteAllText($qualityScopeManifest, $manifest, [Text.UTF8Encoding]::new($false))
    $qualityScope = Get-GameWipQualityScope -ChangedFilesPath $qualityScopeManifest

    if ($qualityScope.Expanded -or $qualityScope.Files.Count -ne 0)
    {
        throw 'Third-party-only changes must not expand or enter the maintained quality scope.'
    }
}

finally
{
    Remove-Item -LiteralPath $qualityScopeManifest -Force -ErrorAction SilentlyContinue
}

$linuxTargets = @($BuildTargets.targets | Where-Object platform -eq 'linux')
$linuxTargetIds = @($linuxTargets | ForEach-Object { [string]$_.id })

if (($linuxTargetIds -join '|') -cne 'linux-amd64|linux-arm64' -or
    @($linuxTargets | Where-Object { $_.backend -ne 'linux' -or $_.toolchain -ne 'zig' }).Count -ne 0)
{
    throw 'Linux AMD64 and ARM64 targets must use the Linux backend and Zig toolchain.'
}

$linuxReadinessToolIds = @(Get-GameWipProjectReadinessToolIds -Target $linuxTargets[0])

if (($linuxReadinessToolIds -join '|') -cne 'cmake|ninja|zig')
{
    throw "Linux target readiness selected unrelated tools: '$($linuxReadinessToolIds -join '|')'."
}

$zigProcessImplementation = (Get-Command Invoke-GameWipProcess).ScriptBlock

try
{
    Set-Item -Path function:Invoke-GameWipProcess -Value {
        param([string]$FilePath, [string[]]$Arguments, [hashtable]$Environment, [string]$OutputMode, [int]$TimeoutSeconds)
        $null = $Arguments, $Environment, $OutputMode, $TimeoutSeconds
        $stdout = if ([IO.Path]::GetFileNameWithoutExtension($FilePath) -eq 'git')
        {
            @('git version 2.55.0.windows.5')
        }
        else
        {
            @('0.17.1')
        }
        return [pscustomobject]@{ ExitCode = 0; Stdout = $stdout; Stderr = @() }
    }

    $zigTool = Get-GameWipProjectTool -Id zig
    $externalZigPath = Join-Path ([IO.Path]::GetTempPath()) $(if (Test-GameWipWindowsHost)
        {
            'external-zig/zig.exe'
        }
        else
        {
            'external-zig/zig'
        })
    $externalZigVersion = Get-GameWipToolCandidateVersion -Tool $zigTool -Path $externalZigPath

    if ($externalZigVersion -ne [string]$zigTool.requiredVersion)
    {
        throw "An exact system Zig install was not accepted for reuse: '$externalZigVersion'."
    }

    $gitTool = Get-GameWipProjectTool -Id git
    $externalGitPath = Join-Path ([IO.Path]::GetTempPath()) $(if (Test-GameWipWindowsHost)
        {
            'external-git/git.exe'
        }
        else
        {
            'external-git/git'
        })
    $externalGitVersion = Get-GameWipToolCandidateVersion -Tool $gitTool -Path $externalGitPath

    if ($externalGitVersion -ne '2.55.0')
    {
        throw "Git version parsing did not remove the Windows build suffix: '$externalGitVersion'."
    }
}

finally
{
    Set-Item -Path function:Invoke-GameWipProcess -Value $zigProcessImplementation
}

$zigPreflightRoot = Join-Path $repositoryRoot ('build\gamewip\temp\zig-preflight-test-' + [guid]::NewGuid().ToString('N'))
$zigPreflightCompilerPath = Join-Path $zigPreflightRoot $(if (Test-GameWipWindowsHost)
    {
        'zig.exe'
    }
    else
    {
        'zig'
    })
$preflightFunctions = @{}

foreach ($functionName in @(
        'Get-GameWipSelectedBuildTarget',
        'Test-GameWipWindowsHost',
        'Get-GameWipProjectTool',
        'Get-GameWipToolStatus',
        'Get-GameWipBuildTargetToolchainRoot',
        'Get-GameWipBuildTargetCompilerPath',
        'Get-GameWipToolValidationFiles'
    ))
{
    $preflightFunctions[$functionName] = (Get-Command $functionName).ScriptBlock
}

try
{
    New-Item -ItemType Directory -Force -Path $zigPreflightRoot | Out-Null
    New-Item -ItemType File -Path $zigPreflightCompilerPath | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $zigPreflightRoot 'lib/std') | Out-Null

    if (-not (Test-Path -LiteralPath (Join-Path $zigPreflightRoot 'zig')))
    {
        New-Item -ItemType File -Path (Join-Path $zigPreflightRoot 'zig') | Out-Null
    }

    New-Item -ItemType File -Path (Join-Path $zigPreflightRoot 'lib/std/std.zig') | Out-Null
    Set-Item -Path function:Get-GameWipSelectedBuildTarget -Value { return @{ platform = 'linux'; architecture = 'amd64'; toolchain = 'zig' } }
    Set-Item -Path function:Test-GameWipWindowsHost -Value { return $false }
    Set-Item -Path function:Get-GameWipProjectTool -Value {
        param([string]$Id)
        $provider = @{ kind = 'system' }
        if ($Id -eq 'zig')
        {
            $provider = @{ kind = 'archive'; compiler = 'zig' }
        }
        return @{ id = $Id; provider = $provider }
    }
    Set-Item -Path function:Get-GameWipToolStatus -Value {
        param([hashtable]$Tool)
        return @{ Ready = $true; State = 'compatible' }
    }
    Set-Item -Path function:Get-GameWipBuildTargetToolchainRoot -Value { return $zigPreflightRoot }
    Set-Item -Path function:Get-GameWipBuildTargetCompilerPath -Value { return $zigPreflightCompilerPath }
    Set-Item -Path function:Get-GameWipToolValidationFiles -Value { return @('zig', 'lib/std/std.zig') }
    Confirm-GameWipToolchain -PresetName dev
}

finally
{
    foreach ($functionName in $preflightFunctions.Keys)
    {
        Set-Item -Path "function:$functionName" -Value $preflightFunctions[$functionName]
    }

    Remove-Item -LiteralPath $zigPreflightRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$savedRuntimeTarget = (Get-Command Get-GameWipSelectedBuildTarget).ScriptBlock

try
{
    Set-Item -Path function:Get-GameWipSelectedBuildTarget -Value { return @{ platform = 'windows'; architecture = 'amd64' } }
    $gameCommand = Get-GameWipProjectCommand -Id game-dev
    $runtimeArguments = @(New-GameWipRuntimeArguments `
            -Command $gameCommand `
            -LogLevel trace `
            -InputDebug `
            -InputDevices `
            -WindowEvents `
            -InputBackends 'hid,rawInput')

    if (($runtimeArguments -join '|') -cne '--logLevel=trace|--inputDebug|--inputDevices|--windowEvents|--inputBackends=hid,rawInput')
    {
        throw "Game runtime argument builder returned '$($runtimeArguments -join '|')'."
    }

    if (@(New-GameWipRuntimeArguments -Command $gameCommand).Count -ne 0)
    {
        throw 'Default game runtime settings unexpectedly added command-line arguments.'
    }

    $runtimeBuilderOptions = @($CommandConfig.RuntimeBuilderOptions)
    $linuxBuilderOptions = @(Get-GameWipCommandBuilderOptions -Options $runtimeBuilderOptions -Platform linux)
    $windowsBuilderOptions = @(Get-GameWipCommandBuilderOptions -Options $runtimeBuilderOptions -Platform windows)
    $logLevelOption = @($runtimeBuilderOptions | Where-Object Id -eq 'log-level')[0]
    $inputDebugOption = @($runtimeBuilderOptions | Where-Object Id -eq 'input-debug')[0]
    $inputBackendsOption = @($windowsBuilderOptions | Where-Object Id -eq 'input-backends')[0]

    if (@($linuxBuilderOptions | Where-Object Id -eq 'input-backends').Count -ne 0 -or $null -eq $inputBackendsOption)
    {
        throw 'Runtime builder did not filter the Windows-only Input backend option by target platform.'
    }

    if (@(ConvertTo-GameWipCommandBuilderArguments -Option $logLevelOption -Value default).Count -ne 0 -or
        (@(ConvertTo-GameWipCommandBuilderArguments -Option $inputDebugOption -Value $true) -join '|') -cne '--inputDebug' -or
        (@(ConvertTo-GameWipCommandBuilderArguments -Option $inputBackendsOption -Value @('hid', 'rawInput')) -join '|') -cne '--inputBackends=hid,rawInput' -or
        @(ConvertTo-GameWipCommandBuilderArguments -Option $inputBackendsOption -Value @()).Count -ne 0)
    {
        throw 'Runtime builder option arguments do not match the executable command line.'
    }

    $exclusiveRuntimeBackendRejected = $false

    try
    {
        ConvertTo-GameWipCommandBuilderArguments -Option $inputBackendsOption -Value @('none', 'hid') | Out-Null
    }

    catch
    {
        $exclusiveRuntimeBackendRejected = $_.Exception.Message -like '*exclusive choice*'
    }

    if (-not $exclusiveRuntimeBackendRejected)
    {
        throw 'Runtime builder allowed the exclusive none backend to be combined with another backend.'
    }

    $invalidBackendRejected = $false

    try
    {
        New-GameWipRuntimeArguments -Command $gameCommand -InputBackends 'hid,unknown' | Out-Null
    }

    catch
    {
        $invalidBackendRejected = $_.Exception.Message -like '*Invalid -InputBackends list*'
    }

    if (-not $invalidBackendRejected)
    {
        throw 'Game runtime accepted an unknown Input backend.'
    }

    $nonGameRuntimeRejected = $false

    try
    {
        New-GameWipRuntimeArguments -Command (Get-GameWipProjectCommand -Id dev-version) -LogLevel trace | Out-Null
    }

    catch
    {
        $nonGameRuntimeRejected = $_.Exception.Message -like '*does not accept game runtime options*'
    }

    if (-not $nonGameRuntimeRejected)
    {
        throw 'A non-game project command accepted game runtime options.'
    }
}

finally
{
    Set-Item -Path function:Get-GameWipSelectedBuildTarget -Value $savedRuntimeTarget
}

# ------------------------------------------------------------
# Target executable path resolution
# ------------------------------------------------------------

$savedExecutableHostCheck = (Get-Command Test-GameWipWindowsHost).ScriptBlock
$savedBuildDirectoryResolver = (Get-Command Get-GameWipBuildDirectory).ScriptBlock
$savedBuildTargetResolver = (Get-Command Get-GameWipSelectedBuildTarget).ScriptBlock
$savedPathComparison = (Get-Command Get-GameWipPathComparison).ScriptBlock

try
{
    Set-Item -Path function:Test-GameWipWindowsHost -Value { return $false }
    Set-Item -Path function:Get-GameWipBuildDirectory -Value { param([string]$PresetName) return 'C:\gamewip-target-build' }
    Set-Item -Path function:Get-GameWipSelectedBuildTarget -Value { return @{ platform = 'linux'; architecture = 'amd64'; toolchain = 'zig' } }
    Set-Item -Path function:Get-GameWipPathComparison -Value { return [StringComparison]::Ordinal }
    $caseMismatchCommand = @{ BuildPreset = 'dev'; Executable = 'build/DEV/GameWIP.exe' }
    $resolvedCaseMismatch = Resolve-GameWipProjectExecutable -Command $caseMismatchCommand
    $expectedCaseMismatch = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build/DEV/GameWIP'))

    if ($resolvedCaseMismatch -cne $expectedCaseMismatch)
    {
        throw "Linux executable path resolution returned '$resolvedCaseMismatch'; expected '$expectedCaseMismatch'."
    }
}

finally
{
    Set-Item -Path function:Test-GameWipWindowsHost -Value $savedExecutableHostCheck
    Set-Item -Path function:Get-GameWipBuildDirectory -Value $savedBuildDirectoryResolver
    Set-Item -Path function:Get-GameWipSelectedBuildTarget -Value $savedBuildTargetResolver
    Set-Item -Path function:Get-GameWipPathComparison -Value $savedPathComparison
}

# The module catalog follows registrations, not support-folder CMake files.
$moduleDiscoveryRoot = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('gamewip-module-discovery-' + [guid]::NewGuid().ToString('N'))

try
{
    foreach ($module in @('alpha', 'internal', 'platform', 'zeta'))
    {
        $moduleDirectory = Join-Path $moduleDiscoveryRoot $module
        New-Item -ItemType Directory -Path $moduleDirectory -Force | Out-Null
    }

    [IO.File]::WriteAllText(
        (Join-Path (Join-Path $moduleDiscoveryRoot 'alpha') 'CMakeLists.txt'),
        "gamewip_add_test_module(`n    NAME alpha`n    SOURCES alpha.cpp`n)`n"
    )
    [IO.File]::WriteAllText(
        (Join-Path (Join-Path $moduleDiscoveryRoot 'internal') 'CMakeLists.txt'),
        "add_library(SharedTestSupport OBJECT support.cpp)`n"
    )
    [IO.File]::WriteAllText(
        (Join-Path (Join-Path $moduleDiscoveryRoot 'platform') 'CMakeLists.txt'),
        "# gamewip_add_test_module(NAME ignored SOURCES ignored.cpp)`n" + "gamewip_add_platform_backend(TARGET Tests ROOT .)`n"
    )
    [IO.File]::WriteAllText(
        (Join-Path (Join-Path $moduleDiscoveryRoot 'zeta') 'CMakeLists.txt'),
        "gamewip_add_test_module(NAME zeta SOURCES zeta.cpp)`n"
    )

    $discoveredModuleNames = @(Get-GameWipRegisteredValidationModuleName -Root $moduleDiscoveryRoot)

    if (($discoveredModuleNames -join ',') -cne 'alpha,zeta')
    {
        throw "Validation module discovery returned '$($discoveredModuleNames -join ',')' instead of registered module names."
    }
}

finally
{
    Remove-Item -LiteralPath $moduleDiscoveryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($CommandConfig.ActionOptions.unicode -notcontains 'ClangFormatPath')
{
    throw 'Unicode operations must accept the clang-format override.'
}

foreach ($requiredOption in @('SkipBuild', 'CleanBuild', 'NameFilter', 'MinimumTime', 'OutputPath', 'BaselinePath', 'CandidatePath', 'RunCount', 'WorkerCount', 'PassThroughArgs', 'BuildBeforeRun', 'LogLevel', 'InputDebug', 'InputDevices', 'WindowEvents', 'InputBackends', 'ChangedOnly', 'ChangedFilesPath', 'FailOnFindings', 'UseCallerTemp', 'UnicodeDataPath', 'ItemNumber'))
{
    if (@($CommandConfig.Options | Where-Object Id -eq $requiredOption).Count -ne 1)
    {
        throw "Project option catalog is missing '$requiredOption'."
    }
}

foreach ($action in $CommandConfig.Actions)
{
    if ((Resolve-GameWipActionName -Name ([string]$action.Id).ToUpperInvariant()) -cne $action.Id)
    {
        throw "Canonical project action '$($action.Id)' did not resolve."
    }
}

if ($null -ne (Resolve-GameWipActionName -Name 'unknown-action'))
{
    throw 'An unknown project action must not resolve.'
}

$validationMenu = @($CommandConfig.Menus | Where-Object Id -eq 'validation')[0]
$validationHandlers = @($validationMenu.Items | ForEach-Object Handler)

if (@($validationHandlers | Where-Object { $_ -eq 'bench' }).Count -ne 1)
{
    throw 'Validation menu must contain Benchmarks exactly once.'
}

if (@($validationHandlers | Where-Object { $_ -eq 'menu-advanced-validation' }).Count -ne 1)
{
    throw 'Validation menu must contain Advanced validation exactly once.'
}

if (@($validationMenu.Items | ForEach-Object Key | Group-Object | Where-Object Count -gt 1).Count -ne 0)
{
    throw 'Validation menu keys must be unique.'
}

foreach ($handler in @('test', 'module', 'stress', 'bench', 'menu-advanced-validation'))
{
    if ($validationHandlers -notcontains $handler)
    {
        throw "Validation menu is missing '$handler'."
    }
}

$expectedMultiChoiceIndexes = @{
    '1' = @(1)
    '1,4,17' = @(1, 4, 17)
    '1, 4, 17' = @(1, 4, 17)
    '1,1' = @(1)
}

foreach ($inputText in $expectedMultiChoiceIndexes.Keys)
{
    $actual = @(ConvertTo-GameWipMultiChoiceIndexes -InputText $inputText -ChoiceCount 17)

    if (($actual -join ',') -ne ($expectedMultiChoiceIndexes[$inputText] -join ','))
    {
        throw "Unexpected multi-choice indexes for '$inputText'."
    }
}

foreach ($inputText in @('0', '18', 'abc', '1,abc', '1,999', '1,,3'))
{
    $rejected = $false

    try
    {
        ConvertTo-GameWipMultiChoiceIndexes -InputText $inputText -ChoiceCount 17 | Out-Null
    }

    catch
    {
        $rejected = $true
    }

    if (-not $rejected)
    {
        throw "Invalid multi-choice input '$inputText' was accepted."
    }
}

$selection = [System.Collections.Generic.HashSet[int]]::new()

foreach ($number in @(ConvertTo-GameWipMultiChoiceIndexes -InputText '17,2,9' -ChoiceCount 17))
{
    [void]$selection.Add($number)
}
$ordered = @(1..17 | Where-Object { $selection.Contains($_) })

if (($ordered -join ',') -ne '2,9,17')
{
    throw 'Multi-choice selections did not preserve configured order.'
}

# ------------------------------------------------------------
# Tool selection and hygiene flows
# ------------------------------------------------------------

# An empty installation phase is valid when a caller only needs the separate
# tracked-file phase. It must return before binding or provider work fails.
Invoke-GameWipToolInstallPlan -InstallPlan @() -TrackedPlan ([pscustomobject]@{})

$savedDetectedTool = (Get-Command Get-GameWipDetectedTool).ScriptBlock
$savedLatestQuery = (Get-Command Get-GameWipToolLatestQuery).ScriptBlock

try
{
    Set-Item -Path function:Get-GameWipDetectedTool -Value { param($Tool) [pscustomobject]@{ Installed = $true; Version = '1.0.0'; Location = 'test'; Source = 'test'; SelectionReason = 'test'; Candidates = @() } }
    Set-Item -Path function:Get-GameWipToolLatestQuery -Value {
        param($Tool)
        [pscustomobject]@{ State = 'resolved'; Version = '1.0.0'; Metadata = $null }
    }
    $multipleToolPlan = @(Get-GameWipToolUpdatePlan -ToolId @('clang-tidy', 'clang-format', 'clang-tidy'))

    if (($multipleToolPlan.Tool.id -join ',') -ne 'clang-tidy,clang-format')
    {
        throw 'Multi-tool planning lost selected tools, changed their order, or queried duplicates.'
    }
}

finally
{
    Set-Item -Path function:Get-GameWipDetectedTool -Value $savedDetectedTool
    Set-Item -Path function:Get-GameWipToolLatestQuery -Value $savedLatestQuery
}

# Each multi-selection enters one operation. Separate operations would reject
# the second tool after the first update makes tracked declarations dirty.
$updateMenuFunctions = @{}

foreach ($functionName in @('Read-GameWipConfiguredMenuItem', 'Read-GameWipNamedChoice', 'Invoke-GameWipInteractiveOperation', 'Invoke-GameWipToolUpdate'))
{
    $updateMenuFunctions[$functionName] = (Get-Command $functionName).ScriptBlock
}

try
{
    Set-Item -Path function:Read-GameWipConfiguredMenuItem -Value {
        param([string]$MenuId)
        $Script:UpdateMenuReadCount++

        if ($Script:UpdateMenuReadCount -eq 1)
        {
            return New-GameWipChoiceResult -Status Selected -Value $Script:UpdateMenuAction
        }

        return New-GameWipChoiceResult -Status Cancelled -Value $null
    }

    Set-Item -Path function:Read-GameWipNamedChoice -Value {
        param([string]$Prompt, [string[]]$Choices, [string]$Default, [switch]$AllowMultiple)
        return @('clang-tidy', 'clang-format')
    }

    Set-Item -Path function:Invoke-GameWipInteractiveOperation -Value {
        param([string]$Label, [scriptblock]$Body)
        $Script:UpdateOperationCount++
        & $Body
    }
    Set-Item -Path function:Invoke-GameWipToolUpdate -Value {
        param([string[]]$ToolId, [switch]$PreviewOnly)

        if (($ToolId -join ',') -ne 'clang-tidy,clang-format' -or [bool]$PreviewOnly -ne ($Script:UpdateMenuAction -eq 'tool-preview'))
        {
            throw 'The tool menu did not forward the complete selection and preview policy.'
        }
    }

    foreach ($action in @('tool-preview', 'tool-update'))
    {
        $Script:UpdateMenuAction = $action
        $Script:UpdateMenuReadCount = 0
        $Script:UpdateOperationCount = 0
        Show-GameWipToolUpdatesMenu

        if ($Script:UpdateOperationCount -ne 1)
        {
            throw 'The tool menu split one selection into separate update operations.'
        }
    }
}

finally
{
    foreach ($functionName in $updateMenuFunctions.Keys)
    {
        Set-Item -Path "function:$functionName" -Value $updateMenuFunctions[$functionName]
    }
    Remove-Variable -Name UpdateMenuAction, UpdateMenuReadCount, UpdateOperationCount -Scope Script -ErrorAction SilentlyContinue
}

# Both advisory providers need setup even when selected alone. Stub execution,
# but require the audit to create its database before dispatching either one.
$hygieneFixtureRoot = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('gamewip-hygiene-' + [guid]::NewGuid().ToString('N'))
$savedHygieneRoot = $Script:RepositoryRoot
$hygieneFunctions = @{}

foreach ($functionName in @('Invoke-GameWipConfigurePreset', 'Invoke-GameWipHygieneClangTidy', 'Invoke-GameWipHygieneCompilerWarnings', 'Write-GameWipHygieneReport'))
{
    $hygieneFunctions[$functionName] = (Get-Command $functionName).ScriptBlock
}

try
{
    $Script:RepositoryRoot = $hygieneFixtureRoot
    Set-Item -Path function:Invoke-GameWipConfigurePreset -Value {
        param([string]$Name)
        $Script:HygieneConfigureCount++
        $directory = Get-GameWipBuildDirectory -PresetName $Name
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $directory 'compile_commands.json'), '[]')
    }
    $providerStub = {
        param([object[]]$Checks)

        if ($Script:HygieneConfigureCount -ne 1)
        {
            throw 'The hygiene provider ran before shared compilation-database preparation.'
        }

        $Script:HygieneProviderCount++
    }
    Set-Item -Path function:Invoke-GameWipHygieneClangTidy -Value $providerStub
    Set-Item -Path function:Invoke-GameWipHygieneCompilerWarnings -Value $providerStub
    Set-Item -Path function:Write-GameWipHygieneReport -Value { param($Report) }

    foreach ($selector in @('unreachable-code', 'standard'))
    {
        $Script:HygieneConfigureCount = 0
        $Script:HygieneProviderCount = 0
        Invoke-GameWipHygieneAudit -Selector $selector
        $expectedProviders = if ($selector -eq 'standard')
        {
            2
        }
        else
        {
            1
        }

        if ($Script:HygieneConfigureCount -ne 1 -or $Script:HygieneProviderCount -ne $expectedProviders)
        {
            throw "Hygiene selection '$selector' skipped setup or a selected provider."
        }
    }
}

finally
{
    $Script:RepositoryRoot = $savedHygieneRoot

    foreach ($functionName in $hygieneFunctions.Keys)
    {
        Set-Item -Path "function:$functionName" -Value $hygieneFunctions[$functionName]
    }
    Remove-Variable -Name HygieneConfigureCount, HygieneProviderCount -Scope Script -ErrorAction SilentlyContinue

    if (Test-Path -LiteralPath $hygieneFixtureRoot)
    {
        Remove-Item -LiteralPath $hygieneFixtureRoot -Recurse -Force
    }
}

# ------------------------------------------------------------
# Bundle execution and status presentation
# ------------------------------------------------------------

foreach ($freshBundleId in @('local-release-check', 'sanitizer'))
{
    $freshBundle = Get-GameWipProjectBundle -Id $freshBundleId

    if (-not $freshBundle.ContainsKey('FreshBuildTrees') -or -not [bool]$freshBundle.FreshBuildTrees)
    {
        throw "Authoritative bundle '$freshBundleId' does not require clean preset trees."
    }
}

if ((Get-GameWipProjectBundle -Id quick).ContainsKey('FreshBuildTrees'))
{
    throw 'The quick development bundle unexpectedly requires clean preset trees.'
}

$releaseBundle = Get-GameWipProjectBundle -Id local-release-check
$savedReleaseSteps = $releaseBundle.Steps
$savedFreshPolicy = $releaseBundle.FreshBuildTrees
$savedProjectCommandInvoker = (Get-Command Invoke-GameWipProjectCommand).ScriptBlock
$savedTargetRunnableCheck = (Get-Command Assert-GameWipSelectedTargetRunnable).ScriptBlock
$Script:BundleBoundArguments = $null

try
{
    $releaseBundle.FreshBuildTrees = $false
    $releaseBundle.Steps = @(@{ Kind = 'ProjectCommand'; Command = 'benchmark-dry-run'; BuildIfMissing = $true })
    Set-Item -Path function:Invoke-GameWipProjectCommand -Value {
        param([string]$Id, [string[]]$Arguments, [switch]$NoBuild, [switch]$ForceBuild)
        $Script:BundleBoundArguments = $PSBoundParameters.ContainsKey('Arguments')
    }
    Set-Item -Path function:Assert-GameWipSelectedTargetRunnable -Value { param([string]$Action) $null = $Action }
    Invoke-GameWipBundle -Id local-release-check

    if ($Script:BundleBoundArguments)
    {
        throw 'A bundle step without arguments bound a synthetic empty project-command argument.'
    }
}

finally
{
    $releaseBundle.Steps = $savedReleaseSteps
    $releaseBundle.FreshBuildTrees = $savedFreshPolicy
    Set-Item -Path function:Invoke-GameWipProjectCommand -Value $savedProjectCommandInvoker
    Set-Item -Path function:Assert-GameWipSelectedTargetRunnable -Value $savedTargetRunnableCheck
    Remove-Variable -Name BundleBoundArguments -Scope Script -ErrorAction SilentlyContinue
}

if ((New-GameWipOperationContext -Label output-default).OutputMode -ne 'Stream')
{
    throw 'Project helper operations do not stream native output by default.'
}

# ------------------------------------------------------------
# Console output conventions
# ------------------------------------------------------------

$expectedSemanticColors = [ordered]@{
    Success = [ConsoleColor]::Green
    Failure = [ConsoleColor]::Red
    Warning = [ConsoleColor]::Yellow
    Accent = [ConsoleColor]::Cyan
    Muted = [ConsoleColor]::DarkGray
}

foreach ($semanticRole in $expectedSemanticColors.Keys)
{
    $actualColor = Get-GameWipSemanticColor -Semantic $semanticRole

    if ($actualColor -ne $expectedSemanticColors[$semanticRole])
    {
        throw "Semantic console role '$semanticRole' mapped to '$actualColor' instead of '$($expectedSemanticColors[$semanticRole])'."
    }
}
$expectedToolSemantics = [ordered]@{
    compatible = 'Success'
    missing = 'Failure'
    mismatch = 'Warning'
    outdated = 'Warning'
    unknown = 'Warning'
}

foreach ($compatibility in $expectedToolSemantics.Keys)
{
    $actualSemantic = Get-GameWipToolCompatibilitySemantic -Compatibility $compatibility

    if ($actualSemantic -ne $expectedToolSemantics[$compatibility])
    {
        throw "Tool compatibility '$compatibility' mapped to '$actualSemantic' instead of '$($expectedToolSemantics[$compatibility])'."
    }
}

# ------------------------------------------------------------
# Interactive menu topology
# ------------------------------------------------------------

$expectedMenuIds = @(
    'root', 'development', 'validation', 'quality', 'hygiene', 'tools', 'tool-updates',
    'git-workspace', 'github-workflows', 'advanced', 'advanced-validation', 'dependencies',
    'unicode-data', 'run-history'
)
$actualMenuIds = @($CommandConfig.Menus | ForEach-Object { [string]$_.Id })

if ((($expectedMenuIds | Sort-Object) -join "`n") -cne (($actualMenuIds | Sort-Object) -join "`n"))
{
    throw 'Project helper interactive menu topology is not fully declarative.'
}

$rootHandlers = @($CommandConfig.Menus | Where-Object Id -eq 'root' | ForEach-Object { $_.Items } | ForEach-Object Handler)
$expectedRootHandlers = @('menu-development', 'menu-validation', 'menu-quality', 'menu-tools', 'menu-git-workspace', 'menu-advanced', 'help')

if (($rootHandlers -join "`n") -cne ($expectedRootHandlers -join "`n"))
{
    throw 'Project helper root menu does not expose the approved six-area topology.'
}

$advancedHandlers = @($CommandConfig.Menus | Where-Object Id -eq 'advanced' | ForEach-Object { $_.Items } | ForEach-Object Handler)

foreach ($requiredAdvancedHandler in @('menu-advanced-validation', 'menu-hygiene', 'menu-github-workflows', 'menu-dependencies', 'menu-unicode-data', 'menu-run-history'))
{
    if ($advancedHandlers -notcontains $requiredAdvancedHandler)
    {
        throw "Advanced menu is missing '$requiredAdvancedHandler'."
    }
}

# ------------------------------------------------------------
# Hygiene profiles and diagnostics
# ------------------------------------------------------------

$standardHygiene = Get-GameWipHygieneSelection -Selector standard

if (@($standardHygiene.Checks).Count -ne 4 -or @($standardHygiene.Checks | Where-Object Availability -ne available).Count -ne 0)
{
    throw 'The standard hygiene profile must contain exactly the four executable compiler-backed checks.'
}

$deepHygiene = Get-GameWipHygieneSelection -Selector deep

if (@($deepHygiene.Checks | Where-Object Availability -eq planned).Count -eq 0)
{
    throw 'The deep hygiene profile does not disclose its planned providers.'
}

$publicHeaderExplanation = Get-GameWipHygieneExplanation -CheckId unused-includes -Path 'game/validation/public_headers/example.cpp'

if ($null -eq $publicHeaderExplanation -or $publicHeaderExplanation.Id -ne 'public-header-isolation')
{
    throw 'Public-header isolation findings are not centrally explained.'
}

$compilerWarningCheck = Get-GameWipHygieneCheck -Id unreachable-code
$compilerWarningRule = @{ '-Wunreachable-code' = $compilerWarningCheck }
$compilerWarningFinding = @(ConvertFrom-GameWipClangTidyFinding -Lines @(
        'D:/GameWIP/game/main.cpp:12:5: warning: code will never be executed [-Wunreachable-code]'
    ) -RuleLookup $compilerWarningRule)

if ($compilerWarningFinding.Count -ne 1 -or $compilerWarningFinding[0].evidence -ne "compiler warning '-Wunreachable-code'")
{
    throw 'Compiler-warning hygiene diagnostics were not normalized correctly.'
}

$savedHygieneConfig = $Script:HygieneConfig

try
{
    $unknownReferenceConfig = ConvertTo-GameWipHashtable ($savedHygieneConfig | ConvertTo-Json -Depth 12 | ConvertFrom-Json)
    $unknownReferenceConfig.Profiles[0].Checks += 'unknown-check'
    $Script:HygieneConfig = $unknownReferenceConfig
    $unknownReferenceRejected = $false

    try
    {
        Assert-GameWipHygieneConfig
    }

    catch
    {
        $unknownReferenceRejected = $_.Exception.Message -like '*references unknown check*'
    }

    if (-not $unknownReferenceRejected)
    {
        throw 'Hygiene configuration accepted an unknown profile check reference.'
    }

    $duplicateCheckConfig = ConvertTo-GameWipHashtable ($savedHygieneConfig | ConvertTo-Json -Depth 12 | ConvertFrom-Json)
    $duplicateCheckConfig.Checks += $duplicateCheckConfig.Checks[0]
    $Script:HygieneConfig = $duplicateCheckConfig
    $duplicateCheckRejected = $false

    try
    {
        Assert-GameWipHygieneConfig
    }

    catch
    {
        $duplicateCheckRejected = $_.Exception.Message -like '*Duplicate hygiene check IDs*'
    }

    if (-not $duplicateCheckRejected)
    {
        throw 'Hygiene configuration accepted a duplicate check ID.'
    }

    $invalidPatternConfig = ConvertTo-GameWipHashtable ($savedHygieneConfig | ConvertTo-Json -Depth 12 | ConvertFrom-Json)
    $invalidPatternConfig.Explanations[0].PathPattern = '('
    $Script:HygieneConfig = $invalidPatternConfig
    $invalidPatternRejected = $false

    try
    {
        Assert-GameWipHygieneConfig
    }

    catch
    {
        $invalidPatternRejected = $_.Exception.Message -like '*invalid path pattern*'
    }

    if (-not $invalidPatternRejected)
    {
        throw 'Hygiene configuration accepted an invalid explanation path pattern.'
    }
}

finally
{
    $Script:HygieneConfig = $savedHygieneConfig
}

$syntheticRuleLookup = @{ 'misc-include-cleaner' = Get-GameWipHygieneCheck -Id unused-includes }
$syntheticFindings = @(ConvertFrom-GameWipClangTidyFinding -Lines @(
        'D:\GameWIP\game\validation\public_headers\example.cpp:1:1: warning: unused include [misc-include-cleaner]',
        'D:\GameWIP\game\source.cpp:2:3: warning: unused include [misc-include-cleaner]'
    ) -RuleLookup $syntheticRuleLookup)

if ($syntheticFindings.Count -ne 2 -or $syntheticFindings[0].confidence -notin @('EXPLAINED', 'LIKELY') -or @($syntheticFindings | Where-Object confidence -eq EXPLAINED).Count -ne 1)
{
    throw 'clang-tidy hygiene findings were not normalized or centrally explained correctly.'
}

$runnerFixtureRoot = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('gamewip-clang-tidy-' + [guid]::NewGuid().ToString('N'))
$runnerFixtureBin = Join-Path $runnerFixtureRoot 'bin'
New-Item -ItemType Directory -Force -Path $runnerFixtureBin | Out-Null
$runnerFixturePath = Join-Path $runnerFixtureBin 'run-clang-tidy'
[IO.File]::WriteAllText($runnerFixturePath, '')

try
{
    $resolvedRunner = Resolve-GameWipHygieneRunner -ClangTidy (Join-Path $runnerFixtureBin 'clang-tidy.exe')

    if ($resolvedRunner -cne [IO.Path]::GetFullPath($runnerFixturePath))
    {
        throw 'Hygiene runner discovery did not find the extensionless companion script beside clang-tidy.'
    }
}

finally
{
    Remove-Item -LiteralPath $runnerFixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------
# Menu input and status rendering
# ------------------------------------------------------------

$originalActionKeyReader = (Get-Command Read-GameWipActionKey).ScriptBlock
function Read-GameWipActionKey
{
    return New-GameWipChoiceResult -Status Selected -Value X
}

try
{
    $menuResult = Read-GameWipActionMenuItem `
        -Title 'Test menu' `
        -Prompt 'Choose:' `
        -Items @([pscustomobject]@{ Key = 'X'; Label = 'Test item'; Handler = 'test-handler' }) `
        -ExitLabel Back `
        6>$null

    if ($menuResult.Status -ne 'Selected' -or $menuResult.Value -ne 'test-handler')
    {
        throw 'The shared declarative menu renderer did not return the configured handler.'
    }
}

finally
{
    Set-Item -Path function:Read-GameWipActionKey -Value $originalActionKeyReader
}

$originalPresentationContext = $Script:OperationContext

try
{
    $Script:OperationContext = New-GameWipOperationContext -Label 'semantic-presentation-test'
    $coloredStatusOutput = (& {
            Write-GameWipStatusLine `
                -Status ready `
                -Text 'tool available' `
                -Suffix '(compatible)' `
                -Semantic Success `
                -SuffixSemantic Muted `
                -Indent 2 `
                -MarkerWidth 10
        } 6>&1 | ForEach-Object { [string]$_ }) -join ''

    $Script:OperationContext = New-GameWipOperationContext -Label 'plain-presentation-test' -NoColor
    $plainStatusOutput = (& {
            Write-GameWipStatusLine `
                -Status ready `
                -Text 'tool available' `
                -Suffix '(compatible)' `
                -Semantic Success `
                -SuffixSemantic Muted `
                -Indent 2 `
                -MarkerWidth 10
        } 6>&1 | ForEach-Object { [string]$_ }) -join ''

    $expectedStatusOutput = '  [ready]    tool available (compatible)'

    if ($coloredStatusOutput -ne $expectedStatusOutput)
    {
        throw "Semantic status rendering changed its text contract: '$coloredStatusOutput'."
    }

    if ($plainStatusOutput -ne $expectedStatusOutput)
    {
        throw "No-color status rendering changed its text contract: '$plainStatusOutput'."
    }
}

finally
{
    $Script:OperationContext = $originalPresentationContext
}

# ------------------------------------------------------------
# Preset and environment selection
# ------------------------------------------------------------

$schemaTestRoot = Join-Path $repositoryRoot ('build\gamewip\temp\schema-test-' + [guid]::NewGuid().ToString('N'))

try
{
    $schemaTestRunRoot = Join-Path $schemaTestRoot 'runs'

    if ((Initialize-GameWipToolRun -RepositoryRoot $schemaTestRoot -RunLogRoot $schemaTestRunRoot -Tool test -Action schema-check).SchemaVersion -ne 1)
    {
        throw 'Retained run receipts must remain at schema version 1.'
    }
}

finally
{
    if (Test-Path -LiteralPath $schemaTestRoot)
    {
        Remove-Item -LiteralPath $schemaTestRoot -Recurse -Force
    }
}

$freshTestRoot = Join-Path $repositoryRoot ('build\gamewip\temp\fresh-preset-test-' + [guid]::NewGuid().ToString('N'))
$savedRepositoryRoot = $Script:RepositoryRoot

try
{
    $Script:RepositoryRoot = $freshTestRoot
    $testTree = Get-GameWipBuildDirectory -PresetName test
    $neighborTree = Get-GameWipBuildDirectory -PresetName coverage
    [IO.Directory]::CreateDirectory($testTree) | Out-Null
    [IO.Directory]::CreateDirectory($neighborTree) | Out-Null
    [IO.File]::WriteAllText((Join-Path $testTree 'stale.gcno'), 'stale')
    [IO.File]::WriteAllText((Join-Path $neighborTree 'preserved.txt'), 'preserved')
    Reset-GameWipPresetBuildTree -Name test

    if (Test-Path -LiteralPath $testTree)
    {
        throw 'Fresh preset recreation retained files from the selected preset tree.'
    }

    if (-not (Test-Path -LiteralPath (Join-Path $neighborTree 'preserved.txt')))
    {
        throw 'Fresh preset recreation modified a neighboring preset tree.'
    }

    $unknownPresetRejected = $false

    try
    {
        Reset-GameWipPresetBuildTree -Name '..'
    }

    catch
    {
        $unknownPresetRejected = $true
    }

    if (-not $unknownPresetRejected)
    {
        throw 'Fresh preset recreation accepted an unknown path-like preset name.'
    }
}

finally
{
    $Script:RepositoryRoot = $savedRepositoryRoot
    Remove-Item -LiteralPath $freshTestRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# Configure arguments must use CMake's cache-definition prefix for every target toolchain.
# Preserve the functions replaced by this test.
$configureArgumentFunctions = @{}
$configureArgumentFunctionNames = @(
    'Assert-GameWipValidPreset',
    'Get-GameWipSelectedBuildTarget',
    'Get-GameWipBuildDirectory',
    'Confirm-GameWipToolchain',
    'Get-GameWipToolchainPathPrefix',
    'Get-GameWipBuildTargetToolchainRoot',
    'Invoke-GameWipNative'
)

foreach ($name in $configureArgumentFunctionNames)
{
    $configureArgumentFunctions[$name] = (Get-Command $name).ScriptBlock
}

$configureArgumentPath = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('configure-arguments-' + [guid]::NewGuid().ToString('N'))
$Script:ConfigureArgumentRoot = $configureArgumentPath
$Script:ConfigureArgumentTarget = $null
$Script:ConfigureArgumentCapture = @()

try
{
    # Capture configure arguments without invoking CMake.
    Set-Item -Path function:Assert-GameWipValidPreset -Value { param([string]$Kind, [string]$Name) }
    Set-Item -Path function:Get-GameWipSelectedBuildTarget -Value { return $Script:ConfigureArgumentTarget }
    Set-Item -Path function:Get-GameWipBuildDirectory -Value { param([string]$PresetName, $Target) return Join-Path $Script:ConfigureArgumentRoot $Target.id }
    Set-Item -Path function:Confirm-GameWipToolchain -Value { param([string]$PresetName) }
    Set-Item -Path function:Get-GameWipToolchainPathPrefix -Value { return @() }
    Set-Item -Path function:Get-GameWipBuildTargetToolchainRoot -Value { param($Target) return $Script:ConfigureArgumentRoot }
    Set-Item -Path function:Invoke-GameWipNative -Value {
        param([string]$Name, [string]$FilePath, [string[]]$Arguments, [string[]]$PathPrefix)
        $Script:ConfigureArgumentCapture = @($Arguments)
    }

    # Verify both target toolchain definitions.
    foreach ($target in @(
            [pscustomobject]@{ id = 'windows-amd64'; platform = 'windows'; architecture = 'amd64'; toolchain = 'llvm-mingw' }
            [pscustomobject]@{ id = 'linux-amd64'; platform = 'linux'; architecture = 'amd64'; toolchain = 'zig' }
        ))
    {
        $Script:ConfigureArgumentTarget = $target
        Invoke-GameWipConfigurePreset -Name dev
        $toolchainVariable = "GAMEWIP_$($target.toolchain.ToUpperInvariant().Replace('-', '_'))_ROOT"
        $expectedRootArgument = "-D$toolchainVariable=$configureArgumentPath"

        if ($Script:ConfigureArgumentCapture -notcontains $expectedRootArgument)
        {
            throw "Configure omitted the CMake toolchain-root definition for '$($target.id)'."
        }
    }
}

finally
{
    # Restore project functions and remove test state.
    foreach ($name in $configureArgumentFunctionNames)
    {
        Set-Item -Path "function:$name" -Value $configureArgumentFunctions[$name]
    }
    Remove-Variable -Name ConfigureArgumentRoot, ConfigureArgumentTarget, ConfigureArgumentCapture -Scope Script -ErrorAction SilentlyContinue
}

if (Test-Path -LiteralPath $configureArgumentPath)
{
    Remove-Item -LiteralPath $configureArgumentPath -Recurse -Force -ErrorAction SilentlyContinue
}

# MSYS2 bootstrap must choose a dated stable release, not the nightly channel.
if (Test-GameWipWindowsHost)
{
    $msys2ReleaseFunctionNames = @('Invoke-GameWipHttpJson', 'Invoke-GameWipHttpRead')
    $originalMsys2ReleaseFunctions = @{}

    foreach ($name in $msys2ReleaseFunctionNames)
    {
        $originalMsys2ReleaseFunctions[$name] = (Get-Command -Name $name).ScriptBlock
    }

    $Script:Msys2ReleaseFixture = @(
        [pscustomobject]@{ tag_name = '2026-06-11'; draft = $false; prerelease = $false; assets = @(
                [pscustomobject]@{ name = 'msys2-base-x86_64-20260611.sfx.exe'; browser_download_url = 'https://example.test/20260611.exe' }
                [pscustomobject]@{ name = 'msys2-base-x86_64-20260611.sfx.exe.sha256'; browser_download_url = 'https://example.test/20260611.sha256' }
            )
        }
        [pscustomobject]@{ tag_name = 'nightly-x86_64'; draft = $false; prerelease = $false; assets = @() }
        [pscustomobject]@{ tag_name = '2026-10-01'; draft = $false; prerelease = $true; assets = @() }
        [pscustomobject]@{ tag_name = '2026-10-15'; draft = $true; prerelease = $false; assets = @() }
        [pscustomobject]@{ tag_name = '2026-09-27'; draft = $false; prerelease = $false; assets = @(
                [pscustomobject]@{ name = 'msys2-base-x86_64-20260927.sfx.exe'; browser_download_url = 'https://example.test/20260927.exe' }
                [pscustomobject]@{ name = 'msys2-base-x86_64-20260927.sfx.exe.sha256'; browser_download_url = 'https://example.test/20260927.sha256' }
            )
        }
    )
    $Script:Msys2ReleaseChecksumUri = $null
    $Script:Msys2ReleaseHash = 'a' * 64

    try
    {
        # Mock network responses and verify the selected archive and checksum.
        Set-Item -Path function:Invoke-GameWipHttpJson -Value {
            param([string]$Uri, [hashtable]$Headers)
            $null = $Headers

            if ($Uri -ne 'https://api.github.com/repos/msys2/msys2-installer/releases?per_page=100')
            {
                throw "Unexpected MSYS2 release query '$Uri'."
            }

            return [pscustomobject]@{ State = 'resolved'; Value = [object[]]$Script:Msys2ReleaseFixture; Reason = '' }
        }
        Set-Item -Path function:Invoke-GameWipHttpRead -Value {
            param([string]$Uri, [int]$MaxAttempts, [int]$TimeoutSeconds)
            $null = $MaxAttempts, $TimeoutSeconds
            $Script:Msys2ReleaseChecksumUri = $Uri
            return [pscustomobject]@{
                State = 'resolved'
                Value = [pscustomobject]@{
                    Content = [Text.Encoding]::UTF8.GetBytes("$($Script:Msys2ReleaseHash)  msys2-base.sfx.exe.sha256`n")
                }
                Reason = ''
            }
        }

        $msys2Archive = Get-GameWipMsys2BaseArchive

        if ($msys2Archive.Name -ne 'msys2-base-x86_64-20260927.sfx.exe' -or
            $msys2Archive.Uri -ne 'https://example.test/20260927.exe' -or
            $msys2Archive.Sha256 -ne $Script:Msys2ReleaseHash -or
            $Script:Msys2ReleaseChecksumUri -ne 'https://example.test/20260927.sha256')
        {
            throw 'MSYS2 bootstrap did not select the latest stable archive and its checksum.'
        }
    }

    finally
    {
        # Restore the network functions and remove test state.
        foreach ($name in $msys2ReleaseFunctionNames)
        {
            Set-Item -Path "function:$name" -Value $originalMsys2ReleaseFunctions[$name]
        }

        Remove-Variable -Name Msys2ReleaseFixture, Msys2ReleaseChecksumUri, Msys2ReleaseHash -Scope Script -ErrorAction SilentlyContinue
    }
}

# Latest-version queries use verified provider data and keep rolling updates informational.
$latestQueryFunctionNames = @(
    'Invoke-GameWipHttpJson',
    'Invoke-GameWipHttpRead',
    'Test-GameWipWindowsHost',
    'Get-GameWipMsys2Root',
    'Invoke-GameWipProcess'
)
$originalLatestQueryFunctions = @{}
$msysRoot = $null
$script:ZigIndexFixtureEmpty = $false

foreach ($name in $latestQueryFunctionNames)
{
    $originalLatestQueryFunctions[$name] = (Get-Command -Name $name).ScriptBlock
}

try
{
    # Zig checks the newest stable version and all declared archive checksums.
    $zigTool = Get-GameWipProjectTool -Id 'zig'
    $zigIndexFixture = [ordered]@{ master = [pscustomobject]@{ version = '0.18.0-dev.10+fixture' } }

    foreach ($version in @('0.17.1', '0.17.2'))
    {
        $release = [ordered]@{ version = $version }

        foreach ($assetKey in @($zigTool.provider.assets.Keys))
        {
            $indexKey = [string]$zigTool.provider.latestIndex.assetKeys[$assetKey]
            $archive = ([string]$zigTool.provider.assets[$assetKey].archive).Replace([string]$zigTool.requiredVersion, $version)
            $release[$indexKey] = [pscustomobject]@{
                tarball = "https://ziglang.org/download/$version/$archive"
                shasum = ('a' * 64)
            }
        }
        $zigIndexFixture[$version] = [pscustomobject]$release
    }
    $script:ZigIndexFixture = [pscustomobject]$zigIndexFixture

    Set-Item -Path function:Invoke-GameWipHttpJson -Value {
        param([string]$Uri, [hashtable]$Headers)
        $null = $Headers

        if ($Uri -ne 'https://ziglang.org/download/index.json')
        {
            throw "Unexpected Zig latest-version URL '$Uri'."
        }

        if ($script:ZigIndexFixtureEmpty)
        {
            return [pscustomobject]@{ State = 'resolved'; Value = $null; Attempts = 1; Reason = '' }
        }

        return [pscustomobject]@{ State = 'resolved'; Value = $script:ZigIndexFixture; Attempts = 1; Reason = '' }
    }

    $zigQuery = Get-GameWipToolLatestQuery -Tool $zigTool

    if ($zigQuery.State -ne 'resolved' -or
        $zigQuery.Version -ne '0.17.2' -or
        $zigQuery.Metadata.Assets.Count -ne $zigTool.provider.assets.Count -or
        $zigQuery.Metadata.Assets['linux-arm64'].sha256 -ne ('a' * 64))
    {
        throw 'Zig latest-version query did not select the newest stable release and all verified assets.'
    }

    $script:ZigIndexFixtureEmpty = $true
    $emptyZigQuery = Get-GameWipToolLatestQuery -Tool $zigTool
    $script:ZigIndexFixtureEmpty = $false

    if ($emptyZigQuery.State -ne 'invalidResponse')
    {
        throw 'Zig latest-version query did not reject an empty index response.'
    }

    $script:ZigIndexFixture.'0.17.2'.'x86_64-windows'.tarball = 'https://example.test/zig.zip'
    $untrustedZigQuery = Get-GameWipToolLatestQuery -Tool $zigTool

    if ($untrustedZigQuery.State -ne 'integrityUnavailable')
    {
        throw 'Zig latest-version query accepted an archive outside its declared download host.'
    }

    # Unicode version detection reads the official UCD release marker.
    Set-Item -Path function:Invoke-GameWipHttpRead -Value {
        param([string]$Uri, [hashtable]$Headers, [int]$MaxAttempts, [int]$TimeoutSeconds)
        $null = $Headers, $MaxAttempts, $TimeoutSeconds

        if ($Uri -ne 'https://www.unicode.org/Public/UCD/latest/ReadMe.txt')
        {
            throw "Unexpected Unicode latest-version URL '$Uri'."
        }

        return [pscustomobject]@{
            State = 'resolved'
            Value = [pscustomobject]@{ Content = 'This directory contains final data files and charts for version 18.0.0 of the Unicode Character Database.' }
            Attempts = 1
            Reason = ''
        }
    }

    $unicodeQuery = Get-GameWipToolLatestQuery -Tool (Get-GameWipProjectTool -Id 'unicode')

    if ($unicodeQuery.State -ne 'resolved' -or $unicodeQuery.Version -ne '18.0.0')
    {
        throw 'Unicode latest-version query did not read the official data-version marker.'
    }

    # MSYS2 update checks report cached package upgrades without changing packages.
    $msysRoot = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('msys2-query-' + [guid]::NewGuid().ToString('N'))
    $bashPath = Join-Path $msysRoot 'usr\bin\bash.exe'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $bashPath) | Out-Null
    [IO.File]::WriteAllText($bashPath, 'fixture')
    $script:Msys2QueryRoot = $msysRoot
    $script:Msys2QueryProcessResult = [pscustomobject]@{
        ExitCode = 0
        Stdout = @('mingw-w64-ucrt-x86_64-cmake 4.5.0-1 -> 4.5.1-1')
        Stderr = @()
        TimedOut = $false
        Cancelled = $false
    }

    Set-Item -Path function:Test-GameWipWindowsHost -Value { return $true }
    Set-Item -Path function:Get-GameWipMsys2Root -Value { return $script:Msys2QueryRoot }
    Set-Item -Path function:Invoke-GameWipProcess -Value {
        param([string]$FilePath, [string[]]$Arguments, [string]$OutputMode, [int]$TimeoutSeconds)
        $null = $FilePath, $Arguments, $OutputMode, $TimeoutSeconds
        return $script:Msys2QueryProcessResult
    }

    $msys2Query = Get-GameWipToolLatestQuery -Tool (Get-GameWipProjectTool -Id 'msys2')

    if ($msys2Query.State -ne 'resolved' -or
        $msys2Query.Metadata.Count -ne 1 -or
        $msys2Query.Metadata.Packages[0].Available -ne '4.5.1-1')
    {
        throw 'MSYS2 update query did not parse cached package upgrades.'
    }

    $script:Msys2QueryProcessResult = [pscustomobject]@{
        ExitCode = 1
        Stdout = @()
        Stderr = @()
        TimedOut = $false
        Cancelled = $false
    }
    $noMsys2Updates = Get-GameWipToolLatestQuery -Tool (Get-GameWipProjectTool -Id 'msys2')

    if ($noMsys2Updates.State -ne 'resolved' -or $noMsys2Updates.Metadata.Count -ne 0)
    {
        throw 'MSYS2 update query did not treat an empty pacman result as up to date.'
    }

    # Windows package providers are unavailable to Linux query plans.
    Set-Item -Path function:Test-GameWipWindowsHost -Value { return $false }
    $unsupportedMsys2Query = Get-GameWipToolLatestQuery -Tool (Get-GameWipProjectTool -Id 'cmake')
    $unsupportedWingetQuery = Get-GameWipToolLatestQuery -Tool (Get-GameWipProjectTool -Id 'git')

    if ($unsupportedMsys2Query.State -ne 'unsupported' -or $unsupportedWingetQuery.State -ne 'unsupported')
    {
        throw 'Windows package queries were not reported as unsupported on Linux.'
    }
}

finally
{
    # Restore provider functions and remove temporary fixtures.
    foreach ($name in $latestQueryFunctionNames)
    {
        Set-Item -Path "function:$name" -Value $originalLatestQueryFunctions[$name]
    }

    if ($msysRoot -and (Test-Path -LiteralPath $msysRoot))
    {
        Remove-Item -LiteralPath $msysRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Variable -Name ZigIndexFixture, ZigIndexFixtureEmpty, Msys2QueryRoot, Msys2QueryProcessResult -Scope Script -ErrorAction SilentlyContinue
}

$conflictingBuildPolicyRejected = $false

try
{
    Initialize-GameWipTestPresetBuild -Name test -NoBuild -Fresh
}

catch
{
    $conflictingBuildPolicyRejected = $_.Exception.Message -like '*cannot be combined*'
}

if (-not $conflictingBuildPolicyRejected)
{
    throw '-CleanBuild and -SkipBuild were not rejected as contradictory test policies.'
}

if ((Test-GameWipWindowsHost) -and (Get-GameWipExecutableNames -Command sample) -contains 'sample')
{
    throw 'Windows tool discovery included an extensionless shell entry point.'
}

$syntheticPythonRoot = if (Test-GameWipWindowsHost)
{
    'C:\gamewip-python-path-test'
}
else
{
    '/tmp/gamewip-python-path-test'
}
$expectedPythonPath = if (Test-GameWipWindowsHost)
{
    'C:\gamewip-python-path-test\Scripts\python.exe'
}
else
{
    '/tmp/gamewip-python-path-test/bin/python'
}

if ((Get-GameWipPythonEnvironmentInterpreterPath -Root $syntheticPythonRoot) -ne $expectedPythonPath)
{
    throw 'Python provider interpreter-path resolution is not platform-correct.'
}

if (Test-GameWipWindowsHost)
{
    $pythonLayoutTestRoot = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('python-layout-test-' + [guid]::NewGuid().ToString('N'))
    $standardPythonRoot = Join-Path $pythonLayoutTestRoot 'standard'
    $msysPythonRoot = Join-Path $pythonLayoutTestRoot 'msys2'

    try
    {
        New-Item -ItemType Directory -Force -Path (Join-Path $standardPythonRoot 'Scripts'), (Join-Path $msysPythonRoot 'bin') | Out-Null
        [IO.File]::WriteAllText((Join-Path $standardPythonRoot 'Scripts/python.exe'), 'fixture')
        [IO.File]::WriteAllText((Join-Path $msysPythonRoot 'bin/python.exe'), 'fixture')

        if ((Get-GameWipPythonEnvironmentInterpreterPath -Root $standardPythonRoot) -cne
            (Join-Path $standardPythonRoot 'Scripts/python.exe') -or
            (Get-GameWipPythonEnvironmentInterpreterPath -Root $msysPythonRoot) -cne
            (Join-Path $msysPythonRoot 'bin/python.exe'))
        {
            throw 'Windows Python provider did not recognize both standard and MSYS2 virtual-environment layouts.'
        }
    }

    finally
    {
        Remove-Item -LiteralPath $pythonLayoutTestRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$jsonSchemaTool = Get-GameWipProjectTool -Id 'jsonschema'

if (Test-GameWipWindowsHost)
{
    $pythonNativeTestRoot = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('python-native-test-' + [guid]::NewGuid().ToString('N'))
    $pythonNativeTestInterpreter = Join-Path $pythonNativeTestRoot 'python.exe'
    New-Item -ItemType Directory -Force -Path $pythonNativeTestRoot | Out-Null
    [IO.File]::WriteAllText($pythonNativeTestInterpreter, 'fixture')
    $pythonNativeFunctionNames = @(
        'Test-GameWipWindowsHost',
        'Get-GameWipToolLatestQuery',
        'Initialize-GameWipManagedToolRoot',
        'Get-GameWipManagedToolRoot',
        'Get-GameWipPythonEnvironmentInterpreterPath',
        'Install-GameWipMsys2Tool',
        'Install-GameWipPythonPackageSpecification',
        'Get-GameWipMsys2ToolDependencyStatus'
    )
    $originalPythonNativeFunctions = @{}

    foreach ($name in $pythonNativeFunctionNames)
    {
        $originalPythonNativeFunctions[$name] = (Get-Command -Name $name).ScriptBlock
    }

    try
    {
        Set-Item -Path function:Test-GameWipWindowsHost -Value { return $true }
        Set-Item -Path function:Get-GameWipToolLatestQuery -Value {
            param([hashtable]$Tool)
            $script:PythonNativeQueryTool = $Tool
            return [pscustomobject]@{ State = 'resolved'; Version = '4.26.1' }
        }
        Set-Item -Path function:Initialize-GameWipManagedToolRoot -Value {}
        Set-Item -Path function:Get-GameWipManagedToolRoot -Value { return $script:PythonNativeTestRoot }
        Set-Item -Path function:Get-GameWipPythonEnvironmentInterpreterPath -Value { return $script:PythonNativeTestInterpreter }
        Set-Item -Path function:Install-GameWipMsys2Tool -Value {
            param([hashtable]$Tool, [AllowNull()][string]$Version)
            $script:PythonNativeInstallTool = $Tool
            $script:PythonNativeInstallVersion = $Version
        }
        Set-Item -Path function:Install-GameWipPythonPackageSpecification -Value {
            $script:PythonPipInstallCalled = $true
        }
        Set-Item -Path function:Get-GameWipMsys2ToolDependencyStatus -Value {
            param([hashtable]$Tool)
            $script:PythonNativeDependencyTool = $Tool
            return @([pscustomobject]@{ Package = 'fixture'; State = 'compatible' })
        }
        $script:PythonNativeTestRoot = $pythonNativeTestRoot
        $script:PythonNativeTestInterpreter = $pythonNativeTestInterpreter
        $script:PythonPipInstallCalled = $false

        if ((Get-GameWipPythonToolLatestVersion -Tool $jsonSchemaTool) -ne '4.26.1' -or
            [string]$script:PythonNativeQueryTool.provider.package -ne [string]$jsonSchemaTool.provider.windowsMsys2.package)
        {
            throw 'Windows Python latest-version checks did not use the declared MSYS2 package.'
        }

        Install-GameWipPythonTool -Tool $jsonSchemaTool -Version '4.26.0'

        if ([string]$script:PythonNativeInstallTool.provider.package -ne [string]$jsonSchemaTool.provider.windowsMsys2.package -or
            $script:PythonNativeInstallVersion -ne '4.26.0' -or $script:PythonPipInstallCalled)
        {
            throw 'Windows Python installation did not use the declared MSYS2 package exclusively.'
        }

        $nativeDependencies = @(Get-GameWipToolProviderDependencyStatus -Tool $jsonSchemaTool)

        if ($nativeDependencies.Count -ne 1 -or
            [string]$script:PythonNativeDependencyTool.provider.dependencies[0].package -ne [string]$jsonSchemaTool.provider.windowsMsys2.package)
        {
            throw 'Windows Python package status did not verify the declared MSYS2 package.'
        }
    }

    finally
    {
        foreach ($name in $pythonNativeFunctionNames)
        {
            Set-Item -Path "function:$name" -Value $originalPythonNativeFunctions[$name]
        }

        Remove-Item -LiteralPath $pythonNativeTestRoot -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Variable -Name PythonNativeTestRoot, PythonNativeTestInterpreter, PythonNativeQueryTool, PythonNativeInstallTool, PythonNativeInstallVersion, PythonPipInstallCalled, PythonNativeDependencyTool -Scope Script -ErrorAction SilentlyContinue
    }
}

# Owned-tree cleanup unlinks nested links and refuses linked deletion roots.
# Create a marked tree and an external target.
$storageTempRoot = Get-GameWipStorageAreaPath -Area temp
$ownedTreeTestRoot = Join-Path $storageTempRoot ('owned-tree-test-' + [guid]::NewGuid().ToString('N'))
$ownedTreeTargetRoot = Join-Path $storageTempRoot ('owned-tree-target-' + [guid]::NewGuid().ToString('N'))
$ownedTreeLinkedRoot = Join-Path $storageTempRoot ('owned-tree-link-' + [guid]::NewGuid().ToString('N'))
$ownedTreeInternalLink = Join-Path $ownedTreeTestRoot 'linked-target'
$ownedTreeRootLinkRejected = $false
$ownedTreeLinksCreated = $false

try
{
    New-Item -ItemType Directory -Path $ownedTreeTestRoot, $ownedTreeTargetRoot -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $ownedTreeTestRoot '.gamewip-owned.json'), '{}')
    $outsideMarker = Join-Path $ownedTreeTargetRoot 'preserved.txt'
    [IO.File]::WriteAllText($outsideMarker, 'preserved')

    try
    {
        New-Item -ItemType SymbolicLink -Path $ownedTreeInternalLink -Target $ownedTreeTargetRoot -ErrorAction Stop | Out-Null
        New-Item -ItemType SymbolicLink -Path $ownedTreeLinkedRoot -Target $ownedTreeTargetRoot -ErrorAction Stop | Out-Null
        $ownedTreeLinksCreated = $true
    }

    catch
    {
        if ($env:GAMEWIP_REQUIRE_SYMLINK_TESTS -eq '1')
        {
            throw
        }
    }

    # Confirm cleanup removes links without touching their targets.
    if ($ownedTreeLinksCreated)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $ownedTreeTestRoot -OwnedRoot $storageTempRoot -RequireMarker

        if ((Test-Path -LiteralPath $ownedTreeTestRoot) -or -not (Test-Path -LiteralPath $outsideMarker))
        {
            throw 'Owned-tree cleanup followed or retained a nested symbolic link.'
        }

        try
        {
            Invoke-GameWipOwnedTreeRemoval -Path $ownedTreeLinkedRoot -OwnedRoot $storageTempRoot
        }

        catch
        {
            $ownedTreeRootLinkRejected = $_.Exception.Message -like '*linked*'
        }

        if (-not $ownedTreeRootLinkRejected -or -not (Test-Path -LiteralPath $outsideMarker))
        {
            throw 'Owned-tree cleanup did not reject a linked root while preserving its target.'
        }
    }
    elseif ($env:GAMEWIP_REQUIRE_SYMLINK_TESTS -eq '1')
    {
        throw 'The required owned-tree symbolic-link tests could not run.'
    }
}

finally
{
    # Remove test links before cleaning up both directories.
    foreach ($linkPath in @($ownedTreeInternalLink, $ownedTreeLinkedRoot))
    {
        $attributes = Get-GameWipFileSystemAttributes -Path $linkPath

        if ($null -ne $attributes -and ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            if (($attributes -band [IO.FileAttributes]::Directory) -ne 0)
            {
                [IO.Directory]::Delete($linkPath)
            }
            else
            {
                [IO.File]::Delete($linkPath)
            }
        }
    }

    foreach ($treePath in @($ownedTreeTestRoot, $ownedTreeTargetRoot))
    {
        if (Test-Path -LiteralPath $treePath)
        {
            Invoke-GameWipOwnedTreeRemoval -Path $treePath -OwnedRoot $storageTempRoot
        }
    }
}

if (Test-GameWipWindowsHost)
{
    $detectedJsonSchema = Get-GameWipDetectedTool -Tool $jsonSchemaTool

    if ((Get-GameWipToolCompatibility -Tool $jsonSchemaTool -Detected $detectedJsonSchema) -ne 'compatible')
    {
        throw 'jsonschema discovery did not select an interpreter containing the package.'
    }
}

$actionIds = @($CommandConfig.Actions | ForEach-Object { [string]$_.Id })

foreach ($requiredAction in @('menu', 'ready', 'quality', 'tool', 'history', 'analyze', 'runtime', 'help'))
{
    if ($actionIds -notcontains $requiredAction)
    {
        throw "Missing action metadata '$requiredAction'."
    }
}

$duplicateActions = @($actionIds | Group-Object | Where-Object Count -gt 1)

if ($duplicateActions.Count -ne 0)
{
    throw "Duplicate action IDs: $($duplicateActions.Name -join ', ')."
}

# ------------------------------------------------------------
# Mutation consent and operation results
# ------------------------------------------------------------

# Non-interactive mode suppresses prompts; it never grants mutation consent.
$Script:OperationContext = New-GameWipOperationContext -Label 'consent-test' -NonInteractive
$consentRejected = $false

try
{
    Confirm-GameWipMutation -Summary 'test mutation' -Risk machine -Plan @('test') | Out-Null
}

catch
{
    $consentRejected = $_.Exception.Data['GameWipCode'] -eq 'consent-required'
}

if (-not $consentRejected)
{
    throw '-NonInteractive implicitly authorized a machine mutation.'
}

$Script:OperationContext = New-GameWipOperationContext -Label 'preview-test' -Preview

if (Confirm-GameWipMutation -Summary 'preview mutation' -Risk tracked -Plan @('test'))
{
    throw '-Preview authorized a tracked mutation.'
}

$Script:OperationContext = New-GameWipOperationContext -Label 'yes-test' -NonInteractive -Yes

if (-not (Confirm-GameWipMutation -Summary 'approved mutation' -Risk machine -Plan @('test')))
{
    throw '-Yes did not provide explicit non-interactive consent.'
}

$Script:OperationContext = $null

# Internal console assertions must never trigger PowerShell parameter binding
# prompts when a caller omits descriptive metadata.
$interactiveConsoleImplementation = (Get-Command Test-GameWipInteractiveConsole).ScriptBlock
function Test-GameWipInteractiveConsole
{
    return $false
}

$interactiveRejected = $false

try
{
    try
    {
        Assert-GameWipInteractiveConsole
    }

    catch
    {
        $interactiveRejected = $_.Exception.Data['GameWipCode'] -eq 'interactive-console-required'
    }
}

finally
{
    Set-Item -Path function:Test-GameWipInteractiveConsole -Value $interactiveConsoleImplementation
}

if (-not $interactiveRejected)
{
    throw 'A non-interactive console assertion did not fail with the supported diagnostic.'
}

# Incidental operation-body output must not turn the final result into an
# array or hide its Status property.
$originalRunRoot = [string]$ProjectConfig.storage.runs
$resultShapeRoot = 'temp/result-shape-test-' + [guid]::NewGuid().ToString('N')

try
{
    $ProjectConfig.storage.runs = "$resultShapeRoot/runs"
    $operationResult = Invoke-GameWipOperation -Label 'result-shape-test' -SuppressReceipt -ScriptBlock {
        [pscustomobject]@{ Incidental = $true }
        throw 'expected result-shape failure'
    }
}

finally
{
    $ProjectConfig.storage.runs = $originalRunRoot
    $resultShapePath = Resolve-GameWipStoragePath -RelativePath $resultShapeRoot

    if (Test-Path -LiteralPath $resultShapePath)
    {
        Remove-Item -LiteralPath $resultShapePath -Recurse -Force
    }
}

if ($operationResult -is [array] -or $operationResult.Status -ne 'failed')
{
    throw 'Operation failure did not return exactly one structured result.'
}

# Catalogs larger than the single-key alphabet fall back to numbered input.
$interactiveConsoleImplementation = (Get-Command Test-GameWipInteractiveConsole).ScriptBlock
function Test-GameWipInteractiveConsole
{
    return $true
}

function Read-Host
{
    return '25'
}

try
{
    $largeChoices = @(1..25 | ForEach-Object { "choice-$_" })
    $largeChoice = Read-GameWipMenuChoiceResult -Prompt 'Large catalog' -Choices $largeChoices -Default 'choice-1'

    if ($largeChoice.Status -ne 'Selected' -or $largeChoice.Value -ne 'choice-25')
    {
        throw 'A large menu catalog did not accept numbered input.'
    }
}

finally
{
    Remove-Item -Path function:Read-Host
    Set-Item -Path function:Test-GameWipInteractiveConsole -Value $interactiveConsoleImplementation
}

# ------------------------------------------------------------
# Persistent state and process execution
# ------------------------------------------------------------

# Blank lines are valid process output.
Write-GameWipProcessOutputLine -Line ''

# Repository text persistence is strict UTF-8 without BOM and can atomically
# replace an existing file without losing non-ASCII content.
$atomicRoot = Join-Path $repositoryRoot ('build\gamewip\temp\atomic-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $atomicRoot | Out-Null

try
{
    $atomicPath = Join-Path $atomicRoot 'state.txt'
    $unicodeText = (
        'GameWIP ' +
        [char]0x2014 +
        ' UTF-8: ' +
        [char]0x0103 +
        [char]0x00EE +
        [char]0x0219 +
        [char]0x021B +
        ' ' +
        [char]0x20AC
    )

    Write-GameWipTextAtomic -Path $atomicPath -Content 'first'
    Write-GameWipTextAtomic -Path $atomicPath -Content $unicodeText

    $readBack = Read-GameWipUtf8Text -Path $atomicPath

    if ($readBack -cne $unicodeText)
    {
        throw 'UTF-8 persistence did not preserve repository text exactly.'
    }

    $bytes = [IO.File]::ReadAllBytes($atomicPath)

    if (
        $bytes.Length -ge 3 -and
        $bytes[0] -eq 0xEF -and
        $bytes[1] -eq 0xBB -and
        $bytes[2] -eq 0xBF
    )
    {
        throw 'Repository text persistence introduced a UTF-8 BOM.'
    }

    $invalidUtf8Path = Join-Path $atomicRoot 'invalid-utf8.txt'
    [IO.File]::WriteAllBytes(
        $invalidUtf8Path,
        [byte[]](0xC3, 0x28)
    )

    $invalidUtf8Rejected = $false

    try
    {
        $null = Read-GameWipUtf8Text -Path $invalidUtf8Path
    }

    catch [System.Text.DecoderFallbackException]
    {
        $invalidUtf8Rejected = $true
    }

    if (-not $invalidUtf8Rejected)
    {
        throw 'Repository UTF-8 reads silently accepted malformed UTF-8.'
    }
}

finally
{
    if (Test-Path -LiteralPath $atomicRoot)
    {
        Remove-Item -LiteralPath $atomicRoot -Recurse -Force
    }
}

# A single suggested action remains a collection under strict mode.
$diagnostic = New-GameWipDiagnosticException -Code test-diagnostic -Summary 'expected failure' -SuggestedActions @('single recovery action')
$failureOutput = @(& { Show-GameWipActionFailure -ErrorRecord ([System.Management.Automation.ErrorRecord]::new($diagnostic, 'test', 'NotSpecified', $null)) } *>&1) -join "`n"

if ($failureOutput -notmatch 'single recovery action')
{
    throw 'Failure rendering lost a scalar suggested action.'
}

# Operation identity must be fresh for each action selection.
$first = New-GameWipOperationContext -Label 'first'
$second = New-GameWipOperationContext -Label 'second'

if ($first.Id -eq $second.Id)
{
    throw 'Operation contexts reused an identity.'
}

if ($first.MutationState -ne 'none' -or $second.MutationState -ne 'none')
{
    throw 'A fresh operation inherited mutation state.'
}

# Candidate ranking is explicit rather than dependent on filesystem enumeration.
$candidates = [System.Collections.Generic.List[object]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$candidateRoot = Join-Path $repositoryRoot ('build\gamewip\temp\candidate-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $candidateRoot | Out-Null

try
{
    $preferred = Join-Path $candidateRoot 'preferred.exe'
    $fallback = Join-Path $candidateRoot 'fallback.exe'
    New-Item -ItemType File -Path $preferred, $fallback | Out-Null
    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path $fallback -Source PATH -Priority 100 -SelectionReason 'PATH fallback'
    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path $preferred -Source managed -Priority 20 -SelectionReason 'declared provider location'
    $ordered = @($candidates | Sort-Object Priority, Path)

    if ($ordered[0].Path -ne [IO.Path]::GetFullPath($preferred) -or $ordered[0].SelectionReason -ne 'declared provider location')
    {
        throw 'Tool-candidate ranking is not deterministic.'
    }
}

finally
{
    if (Test-Path -LiteralPath $candidateRoot)
    {
        Remove-Item -LiteralPath $candidateRoot -Recurse -Force
    }
}

# Windows command shims are valid managed-tool entry points and must be launched
# through the command interpreter rather than passed directly to Start-Process.
if (Test-GameWipWindowsHost)
{
    $shimRoot = Join-Path $repositoryRoot ('build\gamewip\temp\process-shim-test-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $shimRoot | Out-Null

    try
    {
        $shim = Join-Path $shimRoot 'sample.cmd'
        [IO.File]::WriteAllText($shim, "@echo off`r`necho shim-ok`r`n")
        $shimResult = Invoke-GameWipProcess -FilePath $shim -OutputMode LogOnly -TimeoutSeconds 10

        if ($shimResult.ExitCode -ne 0 -or (@($shimResult.Stdout) -join "`n") -notmatch 'shim-ok')
        {
            throw 'Windows command shim execution failed.'
        }

        $failedShim = Join-Path $shimRoot 'failure.cmd'
        [IO.File]::WriteAllText($failedShim, "@echo off`r`nexit /b 7`r`n")
        $failedShimResult = Invoke-GameWipProcess -FilePath $failedShim -OutputMode LogOnly -TimeoutSeconds 10

        if ($failedShimResult.ExitCode -ne 7)
        {
            throw "Windows command shim exit code was reported as $($failedShimResult.ExitCode), not 7."
        }
    }

    finally
    {
        if (Test-Path -LiteralPath $shimRoot)
        {
            Remove-Item -LiteralPath $shimRoot -Recurse -Force
        }
    }
}

# The retained run document is a structured operation receipt.
$runRoot = Join-Path $repositoryRoot ('build\gamewip\temp\run-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

try
{
    $run = Initialize-GameWipToolRun -RepositoryRoot $runRoot -RunLogRoot (Join-Path $runRoot 'runs') -Tool 'test' -Action 'sample'
    $firstStep = Initialize-GameWipToolRunStep -Run $run -Name first -CommandLine 'first command'
    $secondStep = Initialize-GameWipToolRunStep -Run $run -Name second -CommandLine 'second command'

    if ($firstStep.Index -ne 1 -or $secondStep.Index -ne 2 -or $firstStep.LogPath -eq $secondStep.LogPath)
    {
        throw 'Overlapping retained steps did not reserve unique indices and log paths.'
    }

    Complete-GameWipToolRunStep -Run $run -Step $secondStep -ExitCode 0
    Complete-GameWipToolRunStep -Run $run -Step $firstStep -ExitCode 0
    $run.MutationState = 'complete'
    $run.Changes = @('changed sample')
    $summary = Save-GameWipToolRun -Run $run -Status passed
    $document = Get-Content -Raw -LiteralPath (Join-Path $run.Root 'summary.json') | ConvertFrom-Json

    if ($document.status -ne 'passed' -or $document.mutationState -ne 'complete' -or @($document.changes).Count -ne 1)
    {
        throw 'Structured run receipt lost operation result fields.'
    }

    if (-not (Test-Path -LiteralPath $summary))
    {
        throw 'Human-readable run summary was not created.'
    }
}

finally
{
    if (Test-Path -LiteralPath $runRoot)
    {
        Remove-Item -LiteralPath $runRoot -Recurse -Force
    }
}

# ------------------------------------------------------------
# Owned process cleanup and mutation tracking
# ------------------------------------------------------------

if (-not (Test-GameWipWindowsHost))
{
    $ownedProcess = $null
    $descendantIds = @()
    $unixCancellationRoot = Resolve-GameWipStoragePath -RelativePath ('temp/unix-cancellation-test-' + [guid]::NewGuid().ToString('N'))

    try
    {
        New-Item -ItemType Directory -Path $unixCancellationRoot -Force | Out-Null
        $parentScript = Join-Path $unixCancellationRoot 'parent.sh'
        $childProcessIdPath = Join-Path $unixCancellationRoot 'child.pid'
        $parentScriptContent = @('#!/bin/sh', 'child_pid_file=$1', 'sleep 60 &', 'echo $! > "$child_pid_file"', 'wait', '') -join "`n"
        [IO.File]::WriteAllText($parentScript, $parentScriptContent, [Text.UTF8Encoding]::new($false))
        $ownedProcess = Start-GameWipOwnedProcess -FilePath '/bin/sh' -Arguments @($parentScript, $childProcessIdPath)

        if (-not [bool]$ownedProcess.GameWipOwnProcessGroup)
        {
            throw 'Unix owned-process launch did not establish an isolated process group.'
        }

        $discoveryDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do
        {
            Start-Sleep -Milliseconds 100

            if (Test-Path -LiteralPath $childProcessIdPath -PathType Leaf)
            {
                $childProcessId = 0
                $childProcessIdText = Get-Content -Raw -LiteralPath $childProcessIdPath

                if ([int]::TryParse($childProcessIdText.Trim(), [ref]$childProcessId) -and $childProcessId -gt 0)
                {
                    $descendantIds = @($childProcessId)
                }
            }
        }

        while ($descendantIds.Count -eq 0 -and -not $ownedProcess.HasExited -and [DateTime]::UtcNow -lt $discoveryDeadline)

        if ($descendantIds.Count -eq 0)
        {
            throw 'Unix cancellation regression could not observe the owned child process.'
        }

        Stop-GameWipOwnedProcess -Process $ownedProcess
        $null = $ownedProcess.WaitForExit(5000)

        foreach ($descendantId in $descendantIds)
        {
            $terminationDeadline = [DateTime]::UtcNow.AddSeconds(5)

            while ($null -ne (Get-Process -Id $descendantId -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $terminationDeadline)
            {
                Start-Sleep -Milliseconds 100
            }

            if ($null -ne (Get-Process -Id $descendantId -ErrorAction SilentlyContinue))
            {
                throw "Unix cancellation left descendant process $descendantId running."
            }
        }
        $previousLastExitCode = $global:LASTEXITCODE

        try
        {
            $global:LASTEXITCODE = 73
            Stop-GameWipOwnedProcess -Process $ownedProcess

            if ($global:LASTEXITCODE -ne 73)
            {
                throw 'Owned-process cleanup leaked a native process exit code.'
            }
        }

        finally
        {
            $global:LASTEXITCODE = $previousLastExitCode
        }
    }

    finally
    {
        if ($null -ne $ownedProcess)
        {
            Stop-GameWipOwnedProcess -Process $ownedProcess
        }

        foreach ($descendantId in $descendantIds)
        {
            Stop-Process -Id $descendantId -Force -ErrorAction SilentlyContinue
        }

        Remove-Item -LiteralPath $unixCancellationRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$Script:OperationContext = New-GameWipOperationContext -Label 'mutation-state-before-write'

try
{
    try
    {
        Invoke-GameWipMutation -Summary 'fail before mutation' -Risk local -Plan @('fail first') -Body { throw 'expected pre-mutation failure' } | Out-Null
    }

    catch
    {
        $null = $_.Exception
    }

    if ($Script:OperationContext.MutationState -ne 'none')
    {
        throw 'A failure before the first mutating step was incorrectly reported as partial mutation.'
    }
}

finally
{
    $Script:OperationContext = $null
}

# ------------------------------------------------------------
# Managed tool rollback
# ------------------------------------------------------------

$managedRootImplementation = (Get-Command Get-GameWipManagedToolRoot).ScriptBlock
$managedRootTest = Join-Path $repositoryRoot ('build\gamewip\temp\managed-root-test-' + [guid]::NewGuid().ToString('N'))
function Get-GameWipManagedToolRoot
{
    return $managedRootTest
}

$Script:OperationContext = New-GameWipOperationContext -Label 'managed-root-idempotence'
$Script:OperationContext.MutationIntent = $true

try
{
    Initialize-GameWipManagedToolRoot

    if ($Script:OperationContext.MutationState -ne 'partial')
    {
        throw 'Creating the managed tool root did not mark action mutation.'
    }

    $markerPath = Join-Path $managedRootTest '.gamewip-managed.json'
    $markerBefore = Get-Content -Raw -LiteralPath $markerPath
    $Script:OperationContext.MutationState = 'none'
    Initialize-GameWipManagedToolRoot

    if ($Script:OperationContext.MutationState -ne 'none' -or (Get-Content -Raw -LiteralPath $markerPath) -ne $markerBefore)
    {
        throw 'Reinitializing a complete managed tool root rewrote persistent state.'
    }
}

finally
{
    $Script:OperationContext = $null
    Set-Item -Path function:Get-GameWipManagedToolRoot -Value $managedRootImplementation
    Remove-Item -LiteralPath $managedRootTest -Recurse -Force -ErrorAction SilentlyContinue
}

$managedRootImplementation = (Get-Command Get-GameWipManagedToolRoot).ScriptBlock
$downloadImplementation = (Get-Command Invoke-GameWipDownload).ScriptBlock
$hashImplementation = (Get-Command Get-GameWipFileSha256).ScriptBlock
$candidateVersionImplementation = (Get-Command Get-GameWipToolCandidateVersion).ScriptBlock
$shimImplementation = (Get-Command Write-GameWipManagedToolShim).ScriptBlock
$releaseRollbackRoot = Join-Path $repositoryRoot ('build\gamewip\temp\release-rollback-test-' + [guid]::NewGuid().ToString('N'))
$releaseManagedRoot = Join-Path $releaseRollbackRoot 'managed'
$releaseOperationTemp = Join-Path $releaseRollbackRoot 'operation'
function Get-GameWipManagedToolRoot
{
    return $releaseManagedRoot
}

function Invoke-GameWipDownload
{
    param([string]$Uri, [string]$OutFile, [string]$Label)
    $null = $Uri, $Label
    [IO.File]::WriteAllText($OutFile, 'candidate', [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ State = 'resolved'; Reason = '' }
}

function Get-GameWipFileSha256
{
    param([string]$Path)
    $null = $Path
    return 'expected-hash'
}

function Get-GameWipToolCandidateVersion
{
    param([hashtable]$Tool, [string]$Path)
    $null = $Tool, $Path
    return '1.0.0'
}

function Write-GameWipManagedToolShim
{
    param([string]$ToolId, [string]$Version, [string]$ExecutableName)
    $null = $Version, $ExecutableName
    $shimName = if (Test-GameWipWindowsHost)
    {
        "$ToolId.cmd"
    }
    else
    {
        $ToolId
    }
    Write-GameWipTextAtomic -Path (Join-Path (Join-Path $releaseManagedRoot 'bin') $shimName) -Content 'new shim'
    throw 'expected shim replacement failure'
}

$Script:OperationContext = New-GameWipOperationContext -Label 'release-rollback'
$Script:OperationContext.MutationIntent = $true
$Script:OperationContext.Temp = $releaseOperationTemp

try
{
    New-Item -ItemType Directory -Path $releaseOperationTemp -Force | Out-Null
    Initialize-GameWipManagedToolRoot
    $oldToolRoot = Join-Path $releaseManagedRoot 'tools/release-test/1.0.0'
    New-Item -ItemType Directory -Path $oldToolRoot -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $oldToolRoot 'old.txt'), 'old tool', [Text.UTF8Encoding]::new($false))
    $shimName = if (Test-GameWipWindowsHost)
    {
        'release-test.cmd'
    }
    else
    {
        'release-test'
    }
    $oldShim = Join-Path (Join-Path $releaseManagedRoot 'bin') $shimName
    [IO.File]::WriteAllText($oldShim, 'old shim', [Text.UTF8Encoding]::new($false))
    $releaseTool = @{
        id = 'release-test'
        detection = @{ versionArguments = @('--version'); versionPattern = '([0-9.]+)' }
        provider = @{
            repository = 'example/example'
            releaseTag = 'v1.0.0'
            assets = @{
                'windows-amd64' = @{ archive = 'release-test.exe'; format = 'executable'; sha256 = 'expected-hash' }
                'linux-amd64' = @{ archive = 'release-test'; format = 'executable'; sha256 = 'expected-hash' }
            }
        }
    }
    $replacementFailed = $false

    try
    {
        Install-GameWipGitHubReleaseTool -Tool $releaseTool -Version '1.0.0'
    }

    catch
    {
        $replacementFailed = $_.Exception.Message -eq 'expected shim replacement failure'
    }

    if (-not $replacementFailed -or
        (Get-Content -Raw -LiteralPath (Join-Path $oldToolRoot 'old.txt')) -ne 'old tool' -or
        (Get-Content -Raw -LiteralPath $oldShim) -ne 'old shim' -or
        @(Get-ChildItem -LiteralPath (Join-Path $releaseManagedRoot 'tools') -Force -Recurse | Where-Object Name -match '\.(?:incoming|backup)$').Count -ne 0)
    {
        throw 'A failed GitHub-release replacement did not restore the prior tool and shim exactly.'
    }
}

finally
{
    $Script:OperationContext = $null
    Set-Item -Path function:Get-GameWipManagedToolRoot -Value $managedRootImplementation
    Set-Item -Path function:Invoke-GameWipDownload -Value $downloadImplementation
    Set-Item -Path function:Get-GameWipFileSha256 -Value $hashImplementation
    Set-Item -Path function:Get-GameWipToolCandidateVersion -Value $candidateVersionImplementation
    Set-Item -Path function:Write-GameWipManagedToolShim -Value $shimImplementation
    Remove-Item -LiteralPath $releaseRollbackRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$processLogRoot = Join-Path $repositoryRoot ('build\gamewip\temp\process-log-mutation-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $processLogRoot | Out-Null

$Script:OperationContext = New-GameWipOperationContext -Label 'process-log-does-not-count-as-mutation'
$Script:OperationContext.MutationIntent = $true
$Script:OperationContext.Temp = $processLogRoot

try
{
    $processResult = Invoke-GameWipProcess `
        -FilePath 'git' `
        -Arguments @('--version') `
        -OutputMode LogOnly `
        -LogPath (Join-Path $processLogRoot 'git-version.log')

    if ($processResult.ExitCode -ne 0)
    {
        throw 'Read-only native query failed during mutation-state regression coverage.'
    }

    if ($Script:OperationContext.MutationState -ne 'none')
    {
        throw 'Retained native-process logging was incorrectly counted as action mutation.'
    }
}

finally
{
    $Script:OperationContext = $null
    Remove-Item -LiteralPath $processLogRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$savedLastExitCode = Get-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
$exitedProcess = Start-Process -FilePath $powerShellPath -ArgumentList @('-NoProfile', '-Command', 'exit 0') -PassThru
$exitedProcess.WaitForExit()
$exitedProcess | Add-Member -NotePropertyName GameWipOwnProcessGroup -NotePropertyValue $false

try
{
    Remove-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
    Stop-GameWipOwnedProcess -Process $exitedProcess
}

finally
{
    if ($null -ne $savedLastExitCode)
    {
        $global:LASTEXITCODE = $savedLastExitCode.Value
    }
}

$atomicRoot = Join-Path $repositoryRoot (
    'build\gamewip\temp\atomic-parent-mutation-test-' +
    [guid]::NewGuid().ToString('N')
)

$Script:OperationContext = New-GameWipOperationContext -Label 'atomic-parent-counts-as-mutation'
$Script:OperationContext.MutationIntent = $true

try
{
    Write-GameWipTextAtomic `
        -Path (Join-Path $atomicRoot 'nested\result.txt') `
        -Content 'test'

    if ($Script:OperationContext.MutationState -ne 'partial')
    {
        throw 'Creating persistent atomic-write state did not mark action mutation.'
    }
}

finally
{
    $Script:OperationContext = $null
    Remove-Item -LiteralPath $atomicRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------
# Tracked tool update planning
# ------------------------------------------------------------

# Tracked tool planning is declarative, composes same-file references, and
# stages source-preserving compare-and-set mutations without provider/network calls.
$trackedPlanRoot = Join-Path $repositoryRoot ('build\gamewip\temp\tracked-plan-test-' + [guid]::NewGuid().ToString('N'))
$savedRepositoryRoot = $Script:RepositoryRoot
$savedProjectToolsPath = $Script:ProjectToolsPath
$savedProjectTools = $Script:ProjectTools
$savedOperationContext = $Script:OperationContext
$savedMsys2RootResolver = (Get-Command Get-GameWipMsys2Root).ScriptBlock
$trackedPlanMsys2Root = Get-GameWipMsys2Root

try
{
    New-Item -ItemType Directory -Path $trackedPlanRoot -Force | Out-Null
    $sharedReferencePath = Join-Path $trackedPlanRoot 'shared.md'
    $historicalReferencePath = Join-Path $trackedPlanRoot 'historical.md'
    $emDash = [char]0x2014
    $romanianText = [string]([char]0x0103) + [char]0x00EE + [char]0x0219 + [char]0x021B
    $sharedSource = "alpha=1.0.0`nbeta=2.0.0`nunrelated=$emDash $romanianText`n"
    [IO.File]::WriteAllText($sharedReferencePath, $sharedSource, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($historicalReferencePath, 'historical=3.0.0', [Text.UTF8Encoding]::new($false))

    $commonDetection = @{ command = 'test'; versionArguments = @('--version'); versionPattern = '([0-9.]+)' }
    $commonCapabilities = @{ detectInstalled = $true; checkLatest = $true; update = $true }
    $testRegistry = @{
        schemaVersion = 1
        tools = @(
            @{
                id = 'alpha'; name = 'Alpha'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '1.0.0'
                provider = @{ kind = 'npm'; package = 'alpha'; dependencies = @(@{ package = 'alpha-dependency'; version = '4.0.0' }) }
                detection = ConvertTo-GameWipHashtable -Value $commonDetection
                capabilities = ConvertTo-GameWipHashtable -Value $commonCapabilities
                references = @(@{ path = 'shared.md'; kind = 'text'; pattern = 'alpha={version}'; expectedCount = 1 })
            },
            @{
                id = 'beta'; name = 'Beta'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '2.0.0'
                provider = @{ kind = 'npm'; package = 'beta' }
                detection = ConvertTo-GameWipHashtable -Value $commonDetection
                capabilities = ConvertTo-GameWipHashtable -Value $commonCapabilities
                references = @(@{ path = 'shared.md'; kind = 'text'; pattern = 'beta={version}'; expectedCount = 1 })
            },
            @{
                id = 'historical'; name = 'Historical'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '3.0.0'
                provider = @{ kind = 'npm'; package = 'historical' }
                detection = ConvertTo-GameWipHashtable -Value $commonDetection
                capabilities = ConvertTo-GameWipHashtable -Value $commonCapabilities
                references = @(@{ path = 'historical.md'; kind = 'path' })
            },
            @{
                id = 'release'; name = 'Release'; category = 'quality'; versionPolicy = 'exact'; requiredVersion = '5.0.0'
                provider = @{
                    kind = 'githubRelease'; repository = 'example/release'; releaseTag = 'v5.0.0'
                    assets = @{
                        'windows-amd64' = @{ archive = 'release_5.0.0_windows.zip'; sha256 = ('a' * 64) }
                        'linux-amd64' = @{ archive = 'release_5.0.0_linux.tar.gz'; sha256 = ('b' * 64) }
                    }
                }
                detection = ConvertTo-GameWipHashtable -Value $commonDetection
                capabilities = ConvertTo-GameWipHashtable -Value $commonCapabilities
                references = @()
            },
            @{
                id = 'archive'; name = 'Archive'; category = 'compiler'; versionPolicy = 'exact'; requiredVersion = '1.0.0'
                provider = @{
                    kind = 'archive'; baseUrl = 'https://example.test/download'; installMode = 'directoryArchive'
                    managedDirectory = 'tools/archive'; binaryDirectory = '.'; compiler = 'archive'; cmakeToolchain = 'Archive.cmake'
                    assets = @{
                        'windows-amd64' = @{
                            archive = 'archive-1.0.0-windows.zip'; sha256 = ('e' * 64); validationFiles = @('archive.exe')
                        }
                    }
                }
                detection = ConvertTo-GameWipHashtable -Value $commonDetection
                capabilities = ConvertTo-GameWipHashtable -Value $commonCapabilities
                references = @()
            }
        )
    }
    $testRegistryPath = Join-Path $trackedPlanRoot 'project-tools.json'
    [IO.File]::WriteAllText($testRegistryPath, (($testRegistry | ConvertTo-Json -Depth 20) + "`n"), [Text.UTF8Encoding]::new($false))
    $Script:RepositoryRoot = $trackedPlanRoot
    $Script:ProjectToolsPath = $testRegistryPath
    $Script:ProjectTools = $testRegistry
    $Script:OperationContext = New-GameWipOperationContext -Label 'tracked-plan-test'
    $Script:OperationContext.Temp = $trackedPlanRoot
    $Script:TrackedPlanMsys2Root = $trackedPlanMsys2Root
    Set-Item -Path function:Get-GameWipMsys2Root -Value { return $Script:TrackedPlanMsys2Root }

    $plan = @(
        [pscustomobject]@{
            Tool = $testRegistry.tools[0]; Latest = '1.1.0'; LatestDependencies = @{ 'alpha-dependency' = '4.1.0' }
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ProviderMetadata = $null
        },
        [pscustomobject]@{
            Tool = $testRegistry.tools[1]; Latest = '2.1.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ProviderMetadata = $null
        },
        [pscustomobject]@{
            Tool = $testRegistry.tools[2]; Latest = '3.1.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ProviderMetadata = $null
        },
        [pscustomobject]@{
            Tool = $testRegistry.tools[3]; Latest = '5.1.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }
            ProviderMetadata = @{
                Tag = 'v5.1.0'
                Assets = @{
                    'windows-amd64' = @{ archive = 'release_5.1.0_windows.zip'; sha256 = ('c' * 64) }
                    'linux-amd64' = @{ archive = 'release_5.1.0_linux.tar.gz'; sha256 = ('d' * 64) }
                }
            }
        },
        [pscustomobject]@{
            Tool = $testRegistry.tools[4]; Latest = '2.0.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }
            ProviderMetadata = @{
                Assets = @{
                    'windows-amd64' = @{ archive = 'archive-2.0.0-windows.zip'; sha256 = ('f' * 64) }
                }
            }
        }
    )
    $trackedPlan = Get-GameWipTrackedToolMutationPlan -Plan $plan
    $sharedStaged = [string]$trackedPlan.Files[$sharedReferencePath]

    if ($sharedStaged -cne "alpha=1.1.0`nbeta=2.1.0`nunrelated=$emDash $romanianText`n")
    {
        throw 'Multiple live reference mutations did not compose while preserving unrelated UTF-8 text.'
    }

    if ($trackedPlan.Files.ContainsKey($historicalReferencePath))
    {
        throw 'An informational path reference was automatically mutated.'
    }

    $mutationTargets = @($trackedPlan.RegistryMutations | ForEach-Object {
            [string]$_.toolId + ':' + (@($_.path | ForEach-Object {
                        if ($_ -is [hashtable])
                        {
                            '[alpha-dependency]'
                        }
                        else
                        {
                            [string]$_
                        }
                    }) -join '.')
        })

    foreach ($expectedTarget in @(
            'alpha:requiredVersion',
            'alpha:provider.dependencies.[alpha-dependency].version',
            'release:requiredVersion',
            'release:provider.releaseTag',
            'release:provider.assets.windows-amd64.archive',
            'release:provider.assets.windows-amd64.sha256',
            'archive:requiredVersion',
            'archive:provider.assets.windows-amd64.archive',
            'archive:provider.assets.windows-amd64.sha256'
        ))
    {
        if ($mutationTargets -notcontains $expectedTarget)
        {
            throw "Tracked mutation planning omitted '$expectedTarget'."
        }
    }

    $stagedRegistrySource = [string]$trackedPlan.Files[$testRegistryPath]

    if ($stagedRegistrySource -notmatch '"requiredVersion":\s+"1\.1\.0"' -or
        $stagedRegistrySource -notmatch '"version":\s+"4\.1\.0"' -or
        $stagedRegistrySource -notmatch '"requiredVersion":\s+"2\.0\.0"' -or
        $stagedRegistrySource -notmatch '"archive":\s+"archive-2\.0\.0-windows\.zip"')
    {
        throw 'Structured registry mutations did not stage planned exact/dependency versions.'
    }

    $badReferenceTool = ConvertTo-GameWipHashtable -Value $testRegistry.tools[0]
    $badReferenceTool.references[0].expectedCount = 2
    $countRejected = $false

    try
    {
        Get-GameWipTrackedToolMutationPlan -Plan @([pscustomobject]@{
                Tool = $badReferenceTool; Latest = '1.1.0'; LatestDependencies = @{}
                Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ProviderMetadata = $null
            }) | Out-Null
    }

    catch
    {
        $countRejected = $_.Exception.Message -like '*expected 2 exact match(es), found 1*'
    }

    if (-not $countRejected)
    {
        throw 'Live reference expectedCount mismatch was not rejected.'
    }

    $noOpPlan = Get-GameWipTrackedToolMutationPlan -Plan @([pscustomobject]@{
            Tool = $testRegistry.tools[1]; Latest = '2.0.0'; LatestDependencies = @{}
            Query = [pscustomobject]@{ State = 'resolved'; Reason = '' }; ProviderMetadata = $null
        })

    if ($noOpPlan.RegistryMutations.Count -ne 0 -or $noOpPlan.Files.Count -ne 0)
    {
        throw 'No-op tracked planning staged repository changes.'
    }
}

finally
{
    $Script:RepositoryRoot = $savedRepositoryRoot
    $Script:ProjectToolsPath = $savedProjectToolsPath
    $Script:ProjectTools = $savedProjectTools
    $Script:OperationContext = $savedOperationContext
    Set-Item -Path function:Get-GameWipMsys2Root -Value $savedMsys2RootResolver
    Remove-Variable -Name TrackedPlanMsys2Root -Scope Script -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $trackedPlanRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------
# Quality policy and maintained-file discovery
# ------------------------------------------------------------

if (-not (Test-GameWipQualityPolicyChange -Files @('.clang-format')) -or
    -not (Test-GameWipQualityPolicyChange -Files @('config/quality/ruff.toml')) -or
    (Test-GameWipQualityPolicyChange -Files @('game/main.cpp')))
{
    throw 'Changed-quality policy invalidation does not distinguish policy changes from ordinary source changes.'
}

$untrackedRelative = 'scripts/gamewip_quality_untracked_' + [guid]::NewGuid().ToString('N') + '.py'
$untrackedPath = Join-Path $repositoryRoot $untrackedRelative

try
{
    [IO.File]::WriteAllText($untrackedPath, 'print("quality scope")', [Text.UTF8Encoding]::new($false))
    $allMaintainedFiles = @(Get-GameWipMaintainedWorktreeFile)

    if ($allMaintainedFiles -notcontains 'README.md' -or $allMaintainedFiles -notcontains $untrackedRelative.Replace('\', '/'))
    {
        throw 'Unfiltered quality discovery must include tracked and untracked maintained files.'
    }

    if (@(Get-GameWipMaintainedWorktreeFile -Extensions @('.py')) -notcontains $untrackedRelative.Replace('\', '/'))
    {
        throw 'Full quality discovery skipped a first-party untracked Python file.'
    }

    if (@(Get-GameWipMaintainedWorktreeFile -Extensions @('.py')) -contains 'README.md')
    {
        throw 'Extension-filtered quality discovery included an unrelated Markdown file.'
    }
}

finally
{
    Remove-Item -LiteralPath $untrackedPath -Force -ErrorAction SilentlyContinue
}

$informational = Get-GameWipProjectTool -Id unicode

if ([bool]$informational.capabilities.update)
{
    throw 'Informational Unicode resource unexpectedly became installable.'
}

$standardizationScript = Join-Path $repositoryRoot '.github\scripts\check_helper_standardization.py'
$qualityOwnershipScript = Join-Path $repositoryRoot '.github\scripts\check_quality_ownership.py'

if (-not (Test-Path -LiteralPath $standardizationScript) -or -not (Test-Path -LiteralPath $qualityOwnershipScript))
{
    throw 'Repository standardization self-checkers are missing.'
}

Write-Host 'Project helper regression tests passed.'
