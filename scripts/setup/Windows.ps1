# GameWIP Windows setup orchestration: action ownership, consent, lifecycle, and verification.

[CmdletBinding()]
param(
    [string]$Action = 'menu',
    [string]$Branch,
    [switch]$NonInteractive,
    [switch]$SkipDocs,
    [switch]$Preview
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Focused setup libraries consume these public script parameters after they are
# dot-sourced below.
$null = $Branch, $SkipDocs

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot '..\lib\Config.ps1')
. (Join-Path $PSScriptRoot '..\lib\Storage.ps1')
. (Join-Path $PSScriptRoot '..\lib\ToolRuns.ps1')
. (Join-Path $PSScriptRoot '..\lib\Tools.ps1')
$ProjectConfig = Read-GameWipJsonConfig -Path (Join-Path $PSScriptRoot '..\config\project.json') -Name 'project' -SchemaPath (Join-Path $PSScriptRoot '..\schemas\project.schema.json')
$ProjectTools = Read-GameWipJsonConfig -Path (Join-Path $PSScriptRoot '..\config\project-tools.json') -Name 'project tools' -SchemaPath (Join-Path $PSScriptRoot '..\schemas\project-tools.schema.json')
$SetupActionConfig = Read-GameWipJsonConfig -Path (Join-Path $PSScriptRoot 'config\setup.json') -Name 'setup' -SchemaPath (Join-Path $PSScriptRoot '..\schemas\setup.schema.json')
$EditorConfig = Read-GameWipJsonConfig -Path (Join-Path $PSScriptRoot 'config\editors.json') -Name 'editors' -SchemaPath (Join-Path $PSScriptRoot '..\schemas\editors.schema.json')
$script:SetupStatePath = Join-Path $RepositoryRoot (Join-Path $ProjectConfig.storage.state 'setup.json')
$script:SetupRun = $null
$Script:OperationTemp = $null
$ToolConfig = @{ MsysRoot = $ProjectConfig.managedEnvironment.msys2Root }
Initialize-GameWipStorage
Assert-GameWipProjectToolConfig

Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'lib') -Filter '*.ps1' | Sort-Object Name | ForEach-Object { . $_.FullName }

function Initialize-GameWipSetupRun
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    $script:SetupRun = Initialize-GameWipToolRun `
        -RepositoryRoot $RepositoryRoot `
        -RunLogRoot $ProjectConfig.storage.runs `
        -Tool 'setup' `
        -Action "setup-$SelectedAction"
    Write-Host "Tool run: $($script:SetupRun.Root)"
    $Script:OperationTemp = Initialize-GameWipOperationTemp
}

function Complete-GameWipSetupRun
{
    param([Parameter(Mandatory = $true)][ValidateSet('passed', 'failed', 'cancelled')][string]$Status)

    $finalStatus = $Status
    $cleanupError = $null
    try
    {
        Complete-GameWipOperationTemp
    }
    catch
    {
        $finalStatus = 'failed'
        $cleanupError = $_
    }

    $summaryError = $null
    if ($null -ne $script:SetupRun)
    {
        try
        {
            $summary = Save-GameWipToolRun -Run $script:SetupRun -Status $finalStatus
            Write-Host "Summary: $summary"
        }
        catch
        {
            $summaryError = $_
        }
        finally
        {
            $script:SetupRun = $null
        }
    }

    if ($null -ne $cleanupError)
    {
        throw $cleanupError
    }
    if ($null -ne $summaryError)
    {
        throw $summaryError
    }
}

function Show-GameWipSetupMenu
{
    Write-Host ''
    Write-Host 'GameWIP Development Environment'
    Write-Host '==============================='
    foreach ($actionInfo in @($SetupActionConfig.Actions | Where-Object { $_.ContainsKey('Key') }))
    {
        Write-Host "$($actionInfo.Key). $($actionInfo.Name)"
    }
    Write-Host 'Esc. Exit'

    $mapping = @{}
    foreach ($actionInfo in @($SetupActionConfig.Actions | Where-Object { $_.ContainsKey('Key') }))
    {
        $mapping[$actionInfo.Key] = $actionInfo.Id
    }

    while ($true)
    {
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape -or [int]$key.KeyChar -eq 27)
        {
            Write-Host 'Esc'
            return 'exit'
        }

        $selection = $key.KeyChar.ToString()
        Write-Host $selection
        if ($mapping.ContainsKey($selection))
        {
            return $mapping[$selection]
        }

        Write-Host 'Press one of the listed number keys, or Esc to exit.' -ForegroundColor Yellow
    }
}

function Show-GameWipManualInstallInstruction
{
    Write-GameWipSetupSection 'Manual installation'
    Write-Host 'Install the following WinGet packages, then rerun setup.bat check:'
    $gitTool = Get-GameWipProjectTool -Id 'git'
    $msys2Tool = Get-GameWipProjectTool -Id 'msys2'
    Write-Host "  winget install --id $($gitTool.provider.package) --exact"
    Write-Host "  winget install --id $($msys2Tool.provider.package) --exact --override `"install --confirm-command --root $($ProjectConfig.managedEnvironment.msys2Root)`""
    $selectedEditors = @(Get-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    foreach ($id in $selectedEditors)
    {
        $editor = $EditorConfig.Options | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        if ($editor.Handler -eq 'visual-studio')
        {
            Write-Host "  winget install --id $($editor.Package) --exact --override `"--passive --wait --config $RepositoryRoot\.vsconfig --includeRecommended`""
        }
        else
        {
            Write-Host "  winget install --id $($editor.Package) --exact"
        }
    }
    Write-Host ''
    Write-Host 'In MSYS2, perform a complete pacman -Syu update and install the provider packages declared in:'
    Write-Host "  $PSScriptRoot\..\config\project-tools.json"
    Write-Host 'Then run setup.bat profiler to install the matching official Tracy Windows tools.'
    Write-Host ''
    Write-Host 'The setup script can finish repository, editor, and documentation preparation after those tools are present.'
}

function Confirm-GameWipSetupMachineChange
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    if ($SelectedAction -in @('full', 'repair', 'update'))
    {
        Show-GameWipSetupSizeEstimate
    }

    if ($NonInteractive)
    {
        return $true
    }

    Write-Host ''
    if ($SelectedAction -eq 'uninstall')
    {
        Write-Host 'Uninstall removes repository-owned integrations and only software recorded as installed by GameWIP.'
        Write-Host 'The repository, pre-existing software, and user-created files are preserved.'
    }
    else
    {
        Write-Host "The '$SelectedAction' action may install or update software, download packages, and request administrator approval."
        Write-Host 'It can manage the selected editors/IDEs, Git, MSYS2, UCRT64, CLANG64, and editor extensions.'
    }
    while ($true)
    {
        Write-Host 'Choose [A]utomatic installation, [M]anual instructions, or [C]ancel: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Escape -or [int]$key.KeyChar -eq 27)
        {
            Write-Host 'Esc'
            return $false
        }

        $choice = $key.KeyChar.ToString().ToUpperInvariant()
        Write-Host $choice
        switch ($choice)
        {
            'A'
            {
                return $true
            }
            'M'
            {
                Show-GameWipManualInstallInstruction; return $false
            }
            'C'
            {
                return $false
            }
            default
            {
                Write-Host 'Press A, M, C, or Esc.' -ForegroundColor Yellow
            }
        }
    }
}

function Show-GameWipSetupSizeEstimate
{
    Write-GameWipSetupSection 'Estimated resource use'
    Write-Host '  Download: approximately 1-4 GB (depends on selected editor and existing packages)'
    Write-Host '  Installed disk space: approximately 4-15 GB'
    Write-Host '  Temporary build space: up to approximately 6 GB (mainly Tracy and documentation)'
    Write-Host '  Runtime memory: setup can peak near 2-4 GB while compiling Tracy'
    Write-Host '  These are conservative estimates; WinGet and pacman determine exact dependency sizes.'
}

function Invoke-GameWipSetupAction
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    switch ($SelectedAction)
    {
        'full'
        {
            Invoke-GameWipCompleteSetup -RefreshMsys2
        }
        'repair'
        {
            Invoke-GameWipCompleteSetup
        }
        'update'
        {
            Invoke-GameWipCompleteSetup -Update -RefreshMsys2
        }
        'uninstall'
        {
            Invoke-GameWipUninstall -RepositoryRoot $RepositoryRoot -ProjectTools $ProjectTools -Preview:$Preview
        }
        'check'
        {
            Invoke-GameWipEnvironmentCheck
        }
        'tools'
        {
            Invoke-GameWipToolStep
        }
        'visual-studio'
        {
            Invoke-GameWipVisualStudioStep
        }
        'msys2'
        {
            Invoke-GameWipMsys2Step
        }
        'repository'
        {
            Invoke-GameWipRepositoryStep
        }
        'profiler'
        {
            Invoke-GameWipTracyStep
        }
        'editor'
        {
            Invoke-GameWipEditorStep -Choose:(-not $NonInteractive)
        }
        'docs'
        {
            Invoke-GameWipDocumentationStep -Open:(-not $NonInteractive)
        }
        'list'
        {
            Show-GameWipSetupActionCatalog
        }
        'help'
        {
            Show-GameWipSetupHelp
        }
        default
        {
            throw "Setup action '$SelectedAction' is registered but has no implementation."
        }
    }
}

function Show-GameWipSetupActionCatalog
{
    Write-GameWipSetupSection 'Setup actions'
    foreach ($actionInfo in @($SetupActionConfig.Actions))
    {
        Write-Host ("  {0,-16} {1}" -f $actionInfo.Id, $actionInfo.Description)
    }
}

function Show-GameWipSetupHelp
{
    Write-Host 'Usage:'
    Write-Host '  setup.bat [action] [-Branch <name>] [-NonInteractive] [-SkipDocs] [-Preview]'
    Write-Host '  setup.bat help | --help | -h | -?'
    Write-Host ''
    Show-GameWipSetupActionCatalog
    Write-Host ''
    Write-Host 'Options:'
    Write-Host '  -Branch <name>    Select a fetched branch for repository preparation.'
    Write-Host '  -NonInteractive   Approve automatic installation and use saved/default choices.'
    Write-Host '  -SkipDocs         Skip documentation during full, update, or repair.'
    Write-Host '  -Preview          Discover and report uninstall actions without mutation.'
}

function Assert-GameWipSetupActionCatalog
{
    $actions = @($SetupActionConfig.Actions)
    $duplicateIds = @($actions | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object { $_.Count -gt 1 })
    if ($duplicateIds.Count -ne 0)
    {
        throw "Duplicate setup action IDs: $($duplicateIds.Name -join ', ')."
    }
    $menuActions = @($actions | Where-Object { $_.ContainsKey('Key') })
    $duplicateKeys = @($menuActions | ForEach-Object { [string]$_.Key } | Group-Object | Where-Object { $_.Count -gt 1 })
    if ($duplicateKeys.Count -ne 0)
    {
        throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')."
    }
    foreach ($toolId in @($SetupActionConfig.bootstrapToolIds))
    {
        if (@($ProjectTools.tools.id) -notcontains $toolId)
        {
            throw "Setup bootstrap tool '$toolId' is not registered in project-tools.json."
        }
    }
    if (@($actions.Id) -notcontains $Action)
    {
        throw "Unknown setup action '$Action'. Run 'setup.bat list' to see supported actions."
    }
}

function Initialize-GameWipSetupManagedToolRoot
{
    if (-not (Test-GameWipWindowsHost))
    {
        return
    }

    $msysRoot = [string]$ProjectConfig.managedEnvironment.msys2Root
    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    if (-not (Test-Path -LiteralPath $root) -and -not (Test-Path -LiteralPath $msysRoot))
    {
        throw "MSYS2 must be configured before persistent GameWIPTools can be created. Run setup.bat msys2 or setup.bat full first."
    }

    if (Test-Path -LiteralPath $root)
    {
        $entries = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue)
        if (-not (Test-GameWipManagedToolRootOwnership -Root $root) -and $entries.Count -ne 0)
        {
            if ($NonInteractive)
            {
                throw "GameWIPTools exists without valid ownership proof and cannot be adopted noninteractively: '$root'."
            }

            Write-GameWipSetupSection 'Existing GameWIPTools directory'
            Write-Host "GameWIP found a non-empty managed-tool root without valid persistent ownership proof:"
            Write-Host "  $root"
            Write-Host ''
            Write-Host 'GameWIP cannot determine whether these files came from an older GameWIP setup or another source.'
            Write-Host 'If adopted, this root becomes GameWIP-managed and a later GameWIP uninstall may remove it.'
            Write-Host ''
            Write-Host 'Top-level contents:'
            foreach ($entry in @($entries | Select-Object -First 12))
            {
                Write-Host "  - $($entry.Name)"
            }
            if ($entries.Count -gt 12)
            {
                Write-Host "  - ... and $($entries.Count - 12) more"
            }

            $answer = Read-Host 'Adopt this existing GameWIPTools directory? [y/N]'
            if ($answer -notmatch '^(?i:y|yes)$')
            {
                throw "GameWIPTools was left unchanged. Resolve ownership, then rerun setup: '$root'."
            }
            Initialize-GameWipManagedToolRoot -AdoptExisting
            Write-Host '  Recorded explicit user-confirmed GameWIPTools adoption.'
            return
        }
    }

    Initialize-GameWipManagedToolRoot
}

function Invoke-GameWipToolStep
{
    param([switch]$Update)
    Write-GameWipSetupSection 'Project tools'
    Initialize-GameWipSetupManagedToolRoot
    foreach ($toolInfo in @($ProjectTools.tools | Where-Object { $_.capabilities.update -and $_.provider.kind -notin @('msys2', 'gitSubmodule', 'external') }))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $compatibility = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected
        $fromDeclaredProvider = Test-GameWipDetectedToolFromDeclaredProvider -Tool $toolInfo -Detected $detected
        if (-not $Update -and $compatibility -eq 'compatible' -and $fromDeclaredProvider) { Write-Host "  Ready: $($toolInfo.name)"; continue }
        if ($compatibility -eq 'compatible') { Write-Host "  Replacing non-canonical copy: $($toolInfo.name) ($($detected.Location))" }
        $wasInstalled = [bool]$detected.Installed
        $version = if ($toolInfo.Contains('requiredVersion')) { [string]$toolInfo.requiredVersion } else { $null }
        $functionName = Get-GameWipProviderFunction -Tool $toolInfo -Operation Install
        & $functionName -Tool $toolInfo -Version $version
        if (-not $wasInstalled -and $toolInfo.provider.kind -eq 'winget')
        {
            Add-GameWipOwnedWingetPackage -Id ([string]$toolInfo.provider.package)
        }

        $verified = Get-GameWipDetectedTool -Tool $toolInfo
        $verifiedCompatibility = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $verified
        $verifiedProvider = Test-GameWipDetectedToolFromDeclaredProvider -Tool $toolInfo -Detected $verified
        if ($verifiedCompatibility -ne 'compatible' -or -not $verifiedProvider)
        {
            $detectedVersion = if ([string]::IsNullOrWhiteSpace([string]$verified.Version)) { '<unknown>' } else { [string]$verified.Version }
            $detectedLocation = if ([string]::IsNullOrWhiteSpace([string]$verified.Location)) { '<none>' } else { [string]$verified.Location }
            throw "Project tool '$($toolInfo.id)' is not compatible after installation " +
                  "(state: $verifiedCompatibility, detected: $detectedVersion, location: $detectedLocation)."
        }
        Write-Host "  Ready: $($toolInfo.name)"
    }
}

function Invoke-GameWipVisualStudioStep
{
    param([switch]$Update)
    Write-GameWipSetupSection 'Visual Studio'
    $visualStudio = $EditorConfig.Options | Where-Object { $_.Handler -eq 'visual-studio' } | Select-Object -First 1
    Install-GameWipVisualStudio -PackageId $visualStudio.Package -VsConfigPath (Join-Path $RepositoryRoot '.vsconfig') -Update:$Update
}

function Invoke-GameWipMsys2Step
{
    param([switch]$Update)
    Write-GameWipSetupSection 'MSYS2 UCRT64 and CLANG64'
    $msys2Tool = Get-GameWipProjectTool -Id 'msys2'
    $bash = Join-Path $ToolConfig.MsysRoot 'usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash))
    {
        Install-GameWipWingetTool -Tool $msys2Tool -Version $null
        Add-GameWipOwnedWingetPackage -Id ([string]$msys2Tool.provider.package)
        $state = Get-GameWipSetupState
        $state.msys2InstalledBySetup = $true
        Save-GameWipSetupState -State $state
        [ordered]@{ schemaVersion = 1; owner = 'GameWIP'; resource = 'msys2'; installedBySetup = $true } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ToolConfig.MsysRoot '.gamewip-managed.json') -Encoding UTF8
    }
    $packageConfig = Get-GameWipMsys2PackageConfig -ProjectTools $ProjectTools
    Install-GameWipMsys2PackageSet -MsysRoot $ToolConfig.MsysRoot -PackageConfig $packageConfig -Update:$Update
    Test-GameWipMsys2Toolchain -ProjectTools $ProjectTools
}

function Invoke-GameWipRepositoryStep
{
    param([switch]$Update)
    Write-GameWipSetupSection 'Repository'
    $wasZip = -not (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
    $alreadyFetched = $false
    if ($wasZip)
    {
        # Populate submodules before an update checks for tracked changes; GitHub
        # ZIP archives do not contain gitlink working trees.
        Initialize-GameWipRepository -RepositoryRoot $RepositoryRoot -Branch $Branch -ChooseBranch:(-not $NonInteractive)
        $alreadyFetched = $true
    }
    else
    {
        Switch-GameWipRepositoryBranch -RepositoryRoot $RepositoryRoot -Branch $Branch -ChooseBranch:(-not $NonInteractive)
        $alreadyFetched = -not $NonInteractive -or -not [string]::IsNullOrWhiteSpace($Branch)
    }
    if ($Update)
    {
        Invoke-GameWipRepositoryUpdate -RepositoryRoot $RepositoryRoot -SkipFetch:$alreadyFetched
    }
    if (-not $wasZip -or $Update)
    {
        Initialize-GameWipRepository -RepositoryRoot $RepositoryRoot
    }
    Test-GameWipRepositoryState -RepositoryRoot $RepositoryRoot
    Write-Host '  Ready: submodules and development configuration'
}

function Invoke-GameWipEditorStep
{
    param(
        [switch]$Choose,
        [switch]$Update
    )

    Write-GameWipSetupSection 'Editor integration'
    $preferencePath = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    if ($Choose -or (-not (Test-Path -LiteralPath $preferencePath) -and -not $NonInteractive))
    {
        if (-not (Select-GameWipEditor -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig))
        {
            Write-Host '  Editor selection was not changed.'
            return
        }
    }
    elseif (-not (Test-Path -LiteralPath $preferencePath))
    {
        Save-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -Editors @($EditorConfig.Default)
    }

    $selectedEditors = @(Get-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    Install-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $selectedEditors -Update:$Update
    $selectedNames = @(
        $EditorConfig.Options |
            Where-Object { $selectedEditors -contains $_.Id } |
            ForEach-Object { $_.Name }
    )
    Write-Host "  Ready: $($selectedNames -join ', ')"
}

function Invoke-GameWipTracyStep
{
    Write-GameWipSetupSection 'Tracy profiler tools'
    Initialize-GameWipSetupManagedToolRoot
    Invoke-GameWipTracyToolBuild -RepositoryRoot $RepositoryRoot -MsysRoot $ToolConfig.MsysRoot
}

function Invoke-GameWipDocumentationStep
{
    param([switch]$Open)
    Write-GameWipSetupSection 'Documentation'
    Invoke-GameWipDocumentationBuild -RepositoryRoot $RepositoryRoot -Open:$Open
}

function Invoke-GameWipEnvironmentCheck
{
    Write-GameWipSetupSection 'Environment check'
    $failures = [System.Collections.Generic.List[string]]::new()
    $packageConfig = Get-GameWipMsys2PackageConfig -ProjectTools $ProjectTools
    $allMsysPackages = @($packageConfig.Common) + @($packageConfig.Ucrt64) + @($packageConfig.Clang64)
    foreach ($missingPackage in @(Get-GameWipMissingMsys2Package -MsysRoot $ToolConfig.MsysRoot -Packages $allMsysPackages))
    {
        $failures.Add("Missing required MSYS2 package: $missingPackage")
    }

    $managedToolsRoot = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    if (Test-Path -LiteralPath $managedToolsRoot)
    {
        $managedEntries = @(Get-ChildItem -LiteralPath $managedToolsRoot -Force -ErrorAction SilentlyContinue)
        if ($managedEntries.Count -ne 0 -and -not (Test-GameWipManagedToolRootOwnership -Root $managedToolsRoot))
        {
            $failures.Add(
                "GameWIPTools is non-empty but has no valid persistent ownership proof: $managedToolsRoot. " +
                'Run setup.bat repair or setup.bat tools interactively to review/adopt it.'
            )
        }
    }

    foreach ($toolInfo in @($ProjectTools.tools | Where-Object { $_.capabilities.detectInstalled -and $_.versionPolicy -ne 'informational' }))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $compatibility = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected
        if ($compatibility -ne 'compatible')
        {
            $failures.Add("Project tool '$($toolInfo.id)' is $compatibility.")
        }
    }
    if (-not (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot))
    {
        $failures.Add('Matching Tracy Windows profiler tools are not installed.')
    }
    try
    {
        Test-GameWipRepositoryState -RepositoryRoot $RepositoryRoot
    }
    catch
    {
        $failures.Add($_.Exception.Message)
    }

    $selectedEditors = @(Get-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    $editorFailures = @(Get-GameWipEditorFailure -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $selectedEditors)
    foreach ($editorFailure in $editorFailures)
    {
        $failures.Add($editorFailure)
    }

    if ($failures.Count -ne 0)
    {
        Write-Host 'Environment problems:' -ForegroundColor Yellow
        foreach ($failure in $failures)
        {
            Write-Host "  - $failure" -ForegroundColor Yellow
        }
        throw "$($failures.Count) environment check(s) failed. Choose 4 in the menu, or run setup.bat repair, to install or reapply requirements."
    }
    $selectedNames = @(
        $EditorConfig.Options |
            Where-Object { $selectedEditors -contains $_.Id } |
            ForEach-Object { $_.Name }
    )
    Write-Host "  Ready: selected editors/IDEs: $($selectedNames -join ', ')"
    Write-Host '  Ready: all required environment checks passed'
}

function Invoke-GameWipCompleteSetup
{
    param(
        [switch]$Update,
        [switch]$RefreshMsys2
    )

    $preferencePath = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    if (-not (Test-Path -LiteralPath $preferencePath))
    {
        if ($NonInteractive)
        {
            Save-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -Editors @($EditorConfig.Default)
        }
        elseif (-not (Select-GameWipEditor -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig))
        {
            throw 'Complete setup was cancelled before an editor or IDE was selected.'
        }
    }
    $selectedEditors = @(Get-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    $selectedNames = @(
        $EditorConfig.Options |
            Where-Object { $selectedEditors -contains $_.Id } |
            ForEach-Object { $_.Name }
    )

    Write-GameWipSetupSection 'Execution plan'
    Write-Host "  Mode: $(if ($Update) { 'update' } else { 'install/repair' })"
    Write-Host "  Editors/IDEs: $($selectedNames -join ', ')"
    if ($RefreshMsys2)
    {
        Write-Host '  1. Update the complete MSYS2 system with pacman -Syu, then verify declared packages/toolchains'
    }
    else
    {
        Write-Host '  1. Install or verify declared MSYS2 packages and toolchains'
    }
    Write-Host '  2. Install or verify non-MSYS2 project tools'
    Write-Host '  3. Connect an extracted ZIP to Git if needed, initialize pinned submodules, and configure dev'
    Write-Host '  4. Install or update the selected editor integrations'
    Write-Host '  5. Build and verify the pinned Tracy tool set'
    if (-not $SkipDocs)
    {
        Write-Host '  6. Build and verify the generated manual'
    }
    Write-Host '  Final. Verify the complete selected environment'

    # Resolve an existing tool root before environment mutation so an
    # interactive refusal cannot leave a partially updated setup run. On a
    # fresh machine, defer creation until after MSYS2 owns C:\MSYS2.
    $managedToolsRoot = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    if (Test-Path -LiteralPath $managedToolsRoot)
    {
        Initialize-GameWipSetupManagedToolRoot
    }

    Invoke-GameWipMsys2Step -Update:$RefreshMsys2
    Invoke-GameWipToolStep -Update:$Update
    Invoke-GameWipRepositoryStep -Update:$Update
    Invoke-GameWipEditorStep -Update:$Update
    Invoke-GameWipTracyStep
    if (-not $SkipDocs)
    {
        Invoke-GameWipDocumentationStep
    }
    Invoke-GameWipEnvironmentCheck
}

Assert-GameWipSetupWindows
Assert-GameWipSetupRepository -RepositoryRoot $RepositoryRoot
Assert-GameWipSetupActionCatalog

if ($Action -eq 'menu' -and $NonInteractive)
{
    $Action = 'full'
}

$machineChangeActions = @($SetupActionConfig.Actions | Where-Object { [bool]$_.MachineChanges } | ForEach-Object { $_.Id })

if ($Action -eq 'menu')
{
    while ($true)
    {
        $selectedAction = Show-GameWipSetupMenu
        if ($selectedAction -eq 'exit')
        {
            exit 0
        }

        if ($machineChangeActions -contains $selectedAction -and -not ($selectedAction -eq 'uninstall' -and $Preview))
        {
            if (-not (Confirm-GameWipSetupMachineChange -SelectedAction $selectedAction))
            {
                Write-Host 'No automatic installation changes were made.'
                continue
            }
        }

        try
        {
            Initialize-GameWipSetupRun -SelectedAction $selectedAction
            Invoke-GameWipSetupAction -SelectedAction $selectedAction
            Complete-GameWipSetupRun -Status 'passed'
            Write-Host ''
            Write-Host "GameWIP '$selectedAction' completed successfully." -ForegroundColor Green
        }
        catch
        {
            $actionError = $_
            try
            {
                Complete-GameWipSetupRun -Status 'failed'
            }
            catch
            {
                Write-Warning "Setup finalization also failed: $($_.Exception.Message)"
            }
            Write-Host ''
            Write-Host "GameWIP '$selectedAction' failed: $($actionError.Exception.Message)" -ForegroundColor Red
            Write-Host 'Returning to the main menu.' -ForegroundColor Yellow
        }
    }
}

if ($machineChangeActions -contains $Action -and -not ($Action -eq 'uninstall' -and $Preview))
{
    if (-not (Confirm-GameWipSetupMachineChange -SelectedAction $Action))
    {
        Write-Host 'No automatic installation changes were made.'
        exit 0
    }
}

try
{
    if ($Action -notin @('help', 'list'))
    {
        Initialize-GameWipSetupRun -SelectedAction $Action
    }
    Invoke-GameWipSetupAction -SelectedAction $Action
    Complete-GameWipSetupRun -Status 'passed'

    Write-Host ''
    Write-Host "GameWIP '$Action' completed successfully." -ForegroundColor Green
}
catch
{
    $actionError = $_
    try
    {
        Complete-GameWipSetupRun -Status 'failed'
    }
    catch
    {
        Write-Warning "Setup finalization also failed: $($_.Exception.Message)"
    }
    Write-Host ''
    Write-Error "GameWIP '$Action' failed: $($actionError.Exception.Message)"
    exit 1
}
