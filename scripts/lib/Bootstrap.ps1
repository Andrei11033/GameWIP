# Shared project-helper bootstrap. It is safe to load from tests.

# ------------------------------------------------------------
# Repository paths and process defaults
# ------------------------------------------------------------

param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$ScriptsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ProjectConfigPath = Join-Path $ScriptsRoot 'config/project.json'
$BuildTargetsPath = Join-Path $ScriptsRoot 'config/build-targets.json'
$CommandConfigPath = Join-Path $ScriptsRoot 'config/commands.json'
$ProjectToolsPath = Join-Path $ScriptsRoot 'config/project-tools.json'
$HygieneConfigPath = Join-Path $RepositoryRoot 'config/quality/hygiene.json'
$PresetsPath = Join-Path $RepositoryRoot 'CMakePresets.json'

# ------------------------------------------------------------
# Shared library loading and configuration validation
# ------------------------------------------------------------

# Long-lived terminals do not inherit PATH changes made by WinGet installers.
# Refresh this helper process while preserving its existing path precedence.
. (Join-Path $ScriptsRoot 'lib/Storage.ps1')

if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows))
{
    . (Join-Path $ScriptsRoot 'platform/windows/Environment.ps1')
    . (Join-Path $ScriptsRoot 'platform/windows/Tracy.ps1')
    Update-GameWipProcessPath
}
elseif ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
{
    . (Join-Path $ScriptsRoot 'platform/linux/Tracy.ps1')
}

foreach ($defaultVariable in @{
        PythonPath = ''
        ClangFormatPath = ''
        UnicodeDataRoot = ''
        RefreshUnicodeData = $false
        WorkflowKind = 'all'
        WorkflowNumber = 0
        ReleaseCommit = ''
        NoWorkspaceTemp = $false
        NoBuild = $false
        Fresh = $false
        OperationContext = $null
    }.GetEnumerator())
{
    if ($null -eq (Get-Variable -Name $defaultVariable.Key -ErrorAction SilentlyContinue))
    {
        Set-Variable -Name $defaultVariable.Key -Value $defaultVariable.Value -Scope Script
    }
}

$libraryFiles = @(
    # Load configuration functions before the helpers that consume them.
    'Config.ps1',
    'Config/CommandCatalog.ps1',
    'Config/ProjectTools.ps1',
    'Config/CommandValidation.ps1',

    # Operation state, consent, retained receipts, and history.
    'Persistence.ps1',
    'Operation.ps1',
    'Operation/Journal.ps1',
    'Operation/Consent.ps1',
    'Operation/Processes.ps1',
    'Operation/Results.ps1',
    'Operation/Execution.ps1',
    'Runs.ps1',
    'Runs/History.ps1',

    # Console input/output and shared command helpers.
    'Console.ps1',
    'CommandBuilder.ps1',
    'Dependencies.ps1',
    'Network.ps1',

    # Native process launch, capture, and command receipts.
    'Process.ps1',
    'Process/Owned.ps1',
    'Process/Invocation.ps1',
    'Process/Native.ps1',

    # Repository, tools, build, and quality workflows.
    'Git.ps1',
    'Workflows.ps1',
    'Unicode.ps1',
    'Formatting.ps1',
    'Tools.ps1',
    'ToolOwnership.ps1',
    'ToolDiscovery.ps1',
    'ToolQueries.ps1',
    'ToolUpdates.ps1',
    'Build.ps1',
    # Load test execution before its validation command builder.
    'Testing.ps1',
    'Testing/CommandBuilder.ps1',
    'Benchmarks.ps1',
    'BuildTargets.ps1',
    'Documentation.ps1',
    'Bundles.ps1',
    # Load scope, formatting, and check/fix helpers before the quality entry point.
    'Quality/Scope.ps1',
    'Quality/Formatting.ps1',
    'Quality/Checks.ps1',
    'Quality.ps1',
    'Hygiene.ps1',
    'Help.ps1',

    # Load shared menu helpers before the menu implementations and root dispatcher.
    'Interactive.ps1',
    'Interactive/Development.ps1',
    'Interactive/Validation.ps1',
    'Interactive/Hygiene.ps1',
    'Interactive/Quality.ps1',
    'Interactive/Tools.ps1',
    'Interactive/Repository.ps1',
    'Interactive/Advanced.ps1',
    'Interactive/RootMenu.ps1'
)

foreach ($libraryFile in $libraryFiles)
{
    . (Join-Path $ScriptsRoot (Join-Path 'lib' $libraryFile))
}

$ProjectConfig = Read-GameWipJsonConfig -Path $ProjectConfigPath -Name 'project' -SchemaPath (Join-Path $ScriptsRoot 'schemas/project.schema.json')
$BuildTargets = Read-GameWipJsonConfig -Path $BuildTargetsPath -Name 'build targets' -SchemaPath (Join-Path $ScriptsRoot 'schemas/build-targets.schema.json')
$CommandConfig = Read-GameWipJsonConfig -Path $CommandConfigPath -Name 'commands' -SchemaPath (Join-Path $ScriptsRoot 'schemas/commands.schema.json')
$ProjectTools = Read-GameWipJsonConfig -Path $ProjectToolsPath -Name 'project tools' -SchemaPath (Join-Path $ScriptsRoot 'schemas/project-tools.schema.json')
$HygieneConfig = Read-GameWipJsonConfig -Path $HygieneConfigPath -Name 'hygiene' -SchemaPath (Join-Path $ScriptsRoot 'schemas/hygiene.schema.json')
$PresetData = Read-GameWipUtf8Text -Path $PresetsPath | ConvertFrom-Json

Assert-GameWipProjectConfig
Assert-GameWipCommandConfig
Assert-GameWipProjectToolConfig
Assert-GameWipBuildTargetConfig
Assert-GameWipHygieneConfig
