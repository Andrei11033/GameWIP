# Non-executable GameWIP project-helper bootstrap. Safe to dot-source from tests.

param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$ScriptsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ProjectConfigPath = Join-Path $ScriptsRoot 'config\project.json'
$CommandConfigPath = Join-Path $ScriptsRoot 'config\commands.json'
$ProjectToolsPath = Join-Path $ScriptsRoot 'config\project-tools.json'
$PresetsPath = Join-Path $RepositoryRoot 'CMakePresets.json'

# Long-lived terminals do not inherit PATH changes made by WinGet installers.
# Refresh this helper process while preserving its existing path precedence.
. (Join-Path $ScriptsRoot 'lib\Storage.ps1')
Update-GameWipProcessPath

foreach ($defaultVariable in @{
        PythonPath = ''
        PythonProviderHostPath = ''
        ClangFormatPath = ''
        UnicodeDataRoot = ''
        RefreshUnicodeData = $false
        WorkflowKind = 'all'
        WorkflowNumber = 0
        ReleaseCommit = ''
        NoWorkspaceTemp = $false
        NoBuild = $false
        OperationContext = $null
    }.GetEnumerator())
{
    if ($null -eq (Get-Variable -Name $defaultVariable.Key -ErrorAction SilentlyContinue))
    {
        Set-Variable -Name $defaultVariable.Key -Value $defaultVariable.Value -Scope Script
    }
}

$libraryFiles = @(
    'Config.ps1',
    'Persistence.ps1',
    'Operation.ps1',
    'Runs.ps1',
    'Console.ps1',
    'Network.ps1',
    'Process.ps1',
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
    'Testing.ps1',
    'Benchmarks.ps1',
    'Documentation.ps1',
    'Bundles.ps1',
    'Quality.ps1',
    'Help.ps1',
    'Interactive.ps1'
)
foreach ($libraryFile in $libraryFiles)
{
    . (Join-Path $ScriptsRoot (Join-Path 'lib' $libraryFile))
}

$ProjectConfig = Read-GameWipJsonConfig -Path $ProjectConfigPath -Name 'project' -SchemaPath (Join-Path $ScriptsRoot 'schemas\project.schema.json')
$CommandConfig = Read-GameWipJsonConfig -Path $CommandConfigPath -Name 'commands' -SchemaPath (Join-Path $ScriptsRoot 'schemas\commands.schema.json')
$ProjectTools = Read-GameWipJsonConfig -Path $ProjectToolsPath -Name 'project tools' -SchemaPath (Join-Path $ScriptsRoot 'schemas\project-tools.schema.json')
$PresetData = Read-GameWipUtf8Text -Path $PresetsPath | ConvertFrom-Json

Assert-GameWipProjectConfig
Assert-GameWipCommandConfig
Assert-GameWipProjectToolConfig
