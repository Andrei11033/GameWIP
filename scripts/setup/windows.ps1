[CmdletBinding()]
param(
    [string]$Action = 'menu',
    [string]$Branch,
    [switch]$NonInteractive,
    [switch]$SkipDocs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot '..\common\ToolRuns.ps1')
$script:SetupStatePath = Join-Path $RepositoryRoot '.gamewip-install-state.json'
$script:SetupRun = $null
$ToolConfig = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'config\tools.psd1')
$MsysPackageConfig = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'config\msys2-packages.psd1')
$EditorConfig = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'config\editors.psd1')
$SetupActionConfig = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'config\actions.psd1')

Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'lib') -Filter '*.ps1' | Sort-Object Name | ForEach-Object { . $_.FullName }

function Initialize-SetupRun
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    $script:SetupRun = New-GameWipToolRun `
        -RepositoryRoot $RepositoryRoot `
        -RunLogRoot 'build\tool-runs' `
        -Tool 'setup' `
        -Action "setup-$SelectedAction"
    Write-Host "Tool run: $($script:SetupRun.Root)"
}

function Complete-SetupRun
{
    param([Parameter(Mandatory = $true)][ValidateSet('passed', 'failed', 'cancelled')][string]$Status)

    if ($null -eq $script:SetupRun) { return }
    $summary = Save-GameWipToolRun -Run $script:SetupRun -Status $Status
    Write-Host "Summary: $summary"
    $script:SetupRun = $null
}

function Show-SetupMenu
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
    foreach ($actionInfo in @($SetupActionConfig.Actions | Where-Object { $_.ContainsKey('Key') })) { $mapping[$actionInfo.Key] = $actionInfo.Id }

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

function Show-ManualInstallInstructions
{
    Write-SetupSection 'Manual installation'
    Write-Host 'Install the following WinGet packages, then rerun setup.bat check:'
    Write-Host '  winget install --id Git.Git --exact'
    Write-Host '  winget install --id MSYS2.MSYS2 --exact --override "install --confirm-command --root C:\MSYS2"'
    $selectedEditors = @(Get-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
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
    Write-Host 'In MSYS2, perform a complete pacman -Syu update and install the packages declared in:'
    Write-Host "  $PSScriptRoot\config\msys2-packages.psd1"
    Write-Host 'Then run setup.bat profiler to install the matching official Tracy Windows tools.'
    Write-Host ''
    Write-Host 'The setup script can finish repository, editor, and documentation preparation after those tools are present.'
}

function Confirm-SetupMachineChanges
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    if ($SelectedAction -in @('full', 'repair', 'update')) { Show-SetupSizeEstimate }

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
            'A' { return $true }
            'M' { Show-ManualInstallInstructions; return $false }
            'C' { return $false }
            default { Write-Host 'Press A, M, C, or Esc.' -ForegroundColor Yellow }
        }
    }
}

function Show-SetupSizeEstimate
{
    Write-SetupSection 'Estimated resource use'
    Write-Host '  Download: approximately 1-4 GB (depends on selected editor and existing packages)'
    Write-Host '  Installed disk space: approximately 4-15 GB'
    Write-Host '  Temporary build space: up to approximately 6 GB (mainly Tracy and documentation)'
    Write-Host '  Runtime memory: setup can peak near 2-4 GB while compiling Tracy'
    Write-Host '  These are conservative estimates; WinGet and pacman determine exact dependency sizes.'
}

function Invoke-SetupAction
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    switch ($SelectedAction)
    {
        'full' { Invoke-CompleteSetup -RefreshMsys2 }
        'repair' { Invoke-CompleteSetup }
        'update' { Invoke-CompleteSetup -Update -RefreshMsys2 }
        'uninstall' { Invoke-GameWipUninstall -RepositoryRoot $RepositoryRoot }
        'check' { Invoke-EnvironmentCheck }
        'tools' { Invoke-ToolStep }
        'visual-studio' { Invoke-VisualStudioStep }
        'msys2' { Invoke-Msys2Step }
        'repository' { Invoke-RepositoryStep }
        'profiler' { Invoke-TracyStep }
        'editor' { Invoke-EditorStep -Choose:(-not $NonInteractive) }
        'docs' { Invoke-DocumentationStep -Open:(-not $NonInteractive) }
        'list' { Show-SetupActionCatalog }
        'help' { Show-SetupHelp }
        default { throw "Setup action '$SelectedAction' is registered but has no implementation." }
    }
}

function Show-SetupActionCatalog
{
    Write-SetupSection 'Setup actions'
    foreach ($actionInfo in @($SetupActionConfig.Actions | Where-Object { $_.Id -ne 'menu' }))
    {
        Write-Host ("  {0,-16} {1}" -f $actionInfo.Id, $actionInfo.Description)
    }
}

function Show-SetupHelp
{
    Write-Host 'Usage:'
    Write-Host '  setup.bat [action] [-Branch <name>] [-NonInteractive] [-SkipDocs]'
    Write-Host ''
    Show-SetupActionCatalog
    Write-Host ''
    Write-Host 'Options:'
    Write-Host '  -Branch <name>    Select a fetched branch for repository preparation.'
    Write-Host '  -NonInteractive   Approve automatic installation and use saved/default choices.'
    Write-Host '  -SkipDocs         Skip documentation during full, update, or repair.'
}

function Assert-SetupActionCatalog
{
    $actions = @($SetupActionConfig.Actions)
    $duplicateIds = @($actions | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object { $_.Count -gt 1 })
    if ($duplicateIds.Count -ne 0) { throw "Duplicate setup action IDs: $($duplicateIds.Name -join ', ')." }
    $menuActions = @($actions | Where-Object { $_.ContainsKey('Key') })
    $duplicateKeys = @($menuActions | ForEach-Object { [string]$_.Key } | Group-Object | Where-Object { $_.Count -gt 1 })
    if ($duplicateKeys.Count -ne 0) { throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')." }
    if (@($actions.Id) -notcontains $Action) { throw "Unknown setup action '$Action'. Run 'setup.bat list' to see supported actions." }
}

function Invoke-ToolStep
{
    param([switch]$Update)
    Write-SetupSection 'Machine tools'
    Install-ConfiguredWingetTools -Packages $ToolConfig.WingetPackages -Update:$Update
}

function Invoke-VisualStudioStep
{
    param([switch]$Update)
    Write-SetupSection 'Visual Studio'
    $visualStudio = $EditorConfig.Options | Where-Object { $_.Handler -eq 'visual-studio' } | Select-Object -First 1
    Install-GameWipVisualStudio -PackageId $visualStudio.Package -VsConfigPath (Join-Path $RepositoryRoot '.vsconfig') -Update:$Update
}

function Invoke-Msys2Step
{
    param([switch]$Update)
    Write-SetupSection 'MSYS2 UCRT64 and CLANG64'
    if (-not (Test-Path -LiteralPath (Join-Path $ToolConfig.MsysRoot 'usr\bin\bash.exe')))
    {
        Install-WingetPackage -Id 'MSYS2.MSYS2' -Override "install --confirm-command --root $($ToolConfig.MsysRoot)"
        $state = Get-GameWipSetupState
        $state.msys2InstalledBySetup = $true
        Save-GameWipSetupState -State $state
    }
    Install-GameWipMsys2Packages -MsysRoot $ToolConfig.MsysRoot -PackageConfig $MsysPackageConfig -Update:$Update
    Test-GameWipMsys2Tools -MsysRoot $ToolConfig.MsysRoot -CMakeVersionPattern $ToolConfig.CMakeVersionPattern
}

function Invoke-RepositoryStep
{
    param([switch]$Update)
    Write-SetupSection 'Repository'
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
        Set-GameWipRepositoryBranch -RepositoryRoot $RepositoryRoot -Branch $Branch -ChooseBranch:(-not $NonInteractive)
        $alreadyFetched = -not $NonInteractive -or -not [string]::IsNullOrWhiteSpace($Branch)
    }
    if ($Update) { Update-GameWipRepository -RepositoryRoot $RepositoryRoot -SkipFetch:$alreadyFetched }
    if (-not $wasZip -or $Update)
    {
        Initialize-GameWipRepository -RepositoryRoot $RepositoryRoot
    }
    Test-GameWipRepositoryState -RepositoryRoot $RepositoryRoot
    Write-Host '  Ready: submodules and development configuration'
}

function Invoke-EditorStep
{
    param(
        [switch]$Choose,
        [switch]$Update
    )

    Write-SetupSection 'Editor integration'
    $preferencePath = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    if ($Choose -or (-not (Test-Path -LiteralPath $preferencePath) -and -not $NonInteractive))
    {
        if (-not (Select-GameWipEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig))
        {
            Write-Host '  Editor selection was not changed.'
            return
        }
    }
    elseif (-not (Test-Path -LiteralPath $preferencePath))
    {
        Set-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -Editors @($EditorConfig.Default)
    }

    $selectedEditors = @(Get-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    Install-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $selectedEditors -Update:$Update
    $selectedNames = @(
        $EditorConfig.Options |
            Where-Object { $selectedEditors -contains $_.Id } |
            ForEach-Object { $_.Name }
    )
    Write-Host "  Ready: $($selectedNames -join ', ')"
}

function Invoke-TracyStep
{
    Write-SetupSection 'Tracy profiler tools'
    Build-GameWipTracyTools -RepositoryRoot $RepositoryRoot -MsysRoot $ToolConfig.MsysRoot
}

function Invoke-DocumentationStep
{
    param([switch]$Open)
    Write-SetupSection 'Documentation'
    Build-GameWipDocumentation -RepositoryRoot $RepositoryRoot -Open:$Open
}

function Invoke-EnvironmentCheck
{
    Write-SetupSection 'Environment check'
    $failures = [System.Collections.Generic.List[string]]::new()
    foreach ($package in $ToolConfig.WingetPackages)
    {
        if ($package.ContainsKey('Command') -and -not (Test-SetupCommand -Name $package.Command))
        {
            $failures.Add("Missing required command: $($package.Command)")
        }
        if ($package.ContainsKey('Path') -and -not (Test-Path -LiteralPath $package.Path))
        {
            $failures.Add("Missing required path: $($package.Path)")
        }
    }
    $allMsysPackages = @($MsysPackageConfig.Common) + @($MsysPackageConfig.Ucrt64) + @($MsysPackageConfig.Clang64)
    $missingMsysPackages = @(Get-MissingMsys2Packages -MsysRoot $ToolConfig.MsysRoot -Packages $allMsysPackages)
    foreach ($missingPackage in $missingMsysPackages)
    {
        $failures.Add("Missing required MSYS2 package: $missingPackage")
    }
    try
    {
        Test-GameWipMsys2Tools -MsysRoot $ToolConfig.MsysRoot -CMakeVersionPattern $ToolConfig.CMakeVersionPattern
    }
    catch
    {
        $failures.Add($_.Exception.Message)
    }
    if (-not (Test-GameWipTracyTools -RepositoryRoot $RepositoryRoot))
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

    $selectedEditors = @(Get-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    $editorFailures = @(Get-GameWipEditorFailures -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $selectedEditors)
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

function Invoke-CompleteSetup
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
            Set-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -Editors @($EditorConfig.Default)
        }
        elseif (-not (Select-GameWipEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig))
        {
            throw 'Complete setup was cancelled before an editor or IDE was selected.'
        }
    }
    $selectedEditors = @(Get-GameWipSelectedEditors -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    $selectedNames = @(
        $EditorConfig.Options |
            Where-Object { $selectedEditors -contains $_.Id } |
            ForEach-Object { $_.Name }
    )

    Write-SetupSection 'Execution plan'
    Write-Host "  Mode: $(if ($Update) { 'update' } else { 'install/repair' })"
    Write-Host "  Editors/IDEs: $($selectedNames -join ', ')"
    Write-Host '  1. Install or verify common machine tools'
    Write-Host '  2. Install or verify MSYS2 packages and toolchains'
    Write-Host '  3. Connect an extracted ZIP to Git if needed, initialize pinned submodules, and configure dev'
    Write-Host '  4. Install or update the selected editor integrations'
    Write-Host '  5. Build and verify the pinned Tracy tool set'
    if (-not $SkipDocs)
    {
        Write-Host '  6. Build and verify the generated manual'
    }
    Write-Host '  Final. Verify the complete selected environment'

    Invoke-ToolStep -Update:$Update
    Invoke-Msys2Step -Update:$RefreshMsys2
    Invoke-RepositoryStep -Update:$Update
    Invoke-EditorStep -Update:$Update
    Invoke-TracyStep
    if (-not $SkipDocs)
    {
        Invoke-DocumentationStep
    }
    Invoke-EnvironmentCheck
}

Test-SetupWindows
Test-SetupRepository -RepositoryRoot $RepositoryRoot
Assert-SetupActionCatalog

if ($Action -eq 'menu' -and $NonInteractive)
{
    $Action = 'full'
}

$machineChangeActions = @($SetupActionConfig.Actions | Where-Object { [bool]$_.MachineChanges } | ForEach-Object { $_.Id })

if ($Action -eq 'menu')
{
    while ($true)
    {
        $selectedAction = Show-SetupMenu
        if ($selectedAction -eq 'exit')
        {
            exit 0
        }

        if ($machineChangeActions -contains $selectedAction)
        {
            if (-not (Confirm-SetupMachineChanges -SelectedAction $selectedAction))
            {
                Write-Host 'No automatic installation changes were made.'
                continue
            }
        }

        try
        {
            Initialize-SetupRun -SelectedAction $selectedAction
            Invoke-SetupAction -SelectedAction $selectedAction
            Write-Host ''
            Write-Host "GameWIP '$selectedAction' completed successfully." -ForegroundColor Green
            Complete-SetupRun -Status 'passed'
        }
        catch
        {
            Write-Host ''
            Write-Host "GameWIP '$selectedAction' failed: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host 'Returning to the main menu.' -ForegroundColor Yellow
            Complete-SetupRun -Status 'failed'
        }
    }
}

if ($machineChangeActions -contains $Action)
{
    if (-not (Confirm-SetupMachineChanges -SelectedAction $Action))
    {
        Write-Host 'No automatic installation changes were made.'
        exit 0
    }
}

try
{
    if ($Action -notin @('help', 'list')) { Initialize-SetupRun -SelectedAction $Action }
    Invoke-SetupAction -SelectedAction $Action

    Write-Host ''
    Write-Host "GameWIP '$Action' completed successfully." -ForegroundColor Green
    Complete-SetupRun -Status 'passed'
}
catch
{
    Write-Host ''
    Write-Error "GameWIP '$Action' failed: $($_.Exception.Message)"
    Complete-SetupRun -Status 'failed'
    exit 1
}
