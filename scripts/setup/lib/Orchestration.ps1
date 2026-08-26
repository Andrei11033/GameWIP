# GameWIP setup orchestration. The executable entry point only loads this library and dispatches.

Set-StrictMode -Version Latest

$SetupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$SetupActionConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config\setup.json') -Name 'setup' -SchemaPath (Join-Path $ScriptsRoot 'schemas\setup.schema.json')
$EditorConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config\editors.json') -Name 'editors' -SchemaPath (Join-Path $ScriptsRoot 'schemas\editors.schema.json')
$script:SetupStatePath = Join-Path $RepositoryRoot (Join-Path $ProjectConfig.storage.state 'setup.json')
$ToolConfig = @{ MsysRoot = [string]$ProjectConfig.managedEnvironment.msys2Root }

foreach ($file in @('Common.ps1', 'Winget.ps1', 'Msys2.ps1', 'Repository.ps1', 'VisualStudio.ps1', 'Editor.ps1', 'Tracy.ps1', 'Documentation.ps1', 'Uninstall.ps1'))
{
    . (Join-Path $PSScriptRoot $file)
}

function Assert-GameWipSetupActionCatalog
{
    $actions = @($SetupActionConfig.Actions)
    $duplicateIds = @($actions | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object Count -gt 1)
    if ($duplicateIds.Count -ne 0)
    {
        throw "Duplicate setup action IDs: $($duplicateIds.Name -join ', ')."
    }
    $duplicateKeys = @($actions | Where-Object { $_.ContainsKey('Key') } | ForEach-Object { [string]$_.Key } | Group-Object | Where-Object Count -gt 1)
    if ($duplicateKeys.Count -ne 0)
    {
        throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')."
    }
    foreach ($toolId in @($SetupActionConfig.BootstrapToolIds))
    {
        if (@($ProjectTools.tools.id) -notcontains $toolId)
        {
            throw "Setup bootstrap tool '$toolId' is not registered."
        }
    }
}

function Get-GameWipSetupAction
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $actionMatches = @($SetupActionConfig.Actions | Where-Object { $_.Id -eq $Id })
    if ($actionMatches.Count -ne 1)
    {
        throw "Unknown setup action '$Id'. Run 'setup.bat list'."
    }
    return $actionMatches[0]
}

function Show-GameWipSetupActionCatalog
{
    Write-GameWipSection 'Setup actions'
    foreach ($actionInfo in @($SetupActionConfig.Actions))
    {
        Write-Host ('  {0,-16} [{1,-11}] {2}' -f $actionInfo.Id, $actionInfo.Risk, $actionInfo.Description)
    }
}

function Show-GameWipSetupHelp
{
    Write-Host 'Usage:'
    Write-Host '  setup.bat <action> [-Branch <name>] [-Preview] [-NonInteractive] [-Yes] [-SkipDocs]'
    Write-Host ''
    Write-Host 'Controls:'
    Write-Host '  -Preview          Do not apply the requested action; retain only diagnostic run evidence.'
    Write-Host '  -NonInteractive   Disable prompts; it never grants mutation consent.'
    Write-Host '  -Yes              Approve the printed mutation plan in non-interactive use.'
    Write-Host '  -SkipDocs         Skip generated documentation during full/update/repair.'
    Write-Host '  -OutputMode       Select Stream (default), Summary, or LogOnly process output.'
    Write-Host '  -Quiet            Suppress ordinary output while retaining logs and receipt data.'
    Write-Host '  -Json             Emit the final structured operation result.'
    Write-Host ''
    Show-GameWipSetupActionCatalog
}

function Show-GameWipSetupSizeEstimate
{
    Write-GameWipSection 'Estimated resource use'
    Write-Host '  Download: approximately 1-4 GB, depending on editor selection and existing packages.'
    Write-Host '  Installed disk: approximately 4-15 GB.'
    Write-Host '  Temporary build space: up to approximately 6 GB, primarily Tracy and documentation.'
}

function Initialize-GameWipSetupManagedToolRoot
{
    if (-not (Test-GameWipWindowsHost))
    {
        return
    }
    $root = Get-GameWipManagedToolRoot
    if (Test-Path -LiteralPath $root)
    {
        $entries = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | Where-Object Name -ne '.gamewip-managed.json')
        if (-not (Test-GameWipManagedToolRootOwnership -Root $root) -and $entries.Count -ne 0)
        {
            if ($NonInteractive)
            {
                throw "Managed tool root has unknown ownership and cannot be adopted non-interactively: '$root'."
            }
            Write-GameWipSection 'Existing managed tool directory'
            Write-Host $root
            foreach ($entry in @($entries | Select-Object -First 12))
            {
                Write-Host "  - $($entry.Name)"
            }
            if (-not (Read-GameWipYesNo -Prompt 'Adopt this existing directory as GameWIP-managed?' -Default $false))
            {
                throw 'Managed tool-root adoption was declined.'
            }
            Initialize-GameWipManagedToolRoot -AdoptExisting
            Add-GameWipOperationChange -Message "Adopted managed tool root: $root"
            return
        }
    }
    Initialize-GameWipManagedToolRoot
}

function Write-GameWipMsys2Ownership
{
    param([ValidateSet('created', 'claimedEmpty', 'adopted')][string]$Origin = 'created')
    $markerPath = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) '.gamewip-managed.json'
    $marker = New-GameWipOwnershipMarker -Resource 'msys2' -Origin $Origin -Payload ([ordered]@{ installedBySetup = $true })
    Write-GameWipJsonAtomic -Path $markerPath -Value $marker
}

function Invoke-GameWipSetupMsys2Step
{
    param([switch]$Update)
    Write-GameWipSection 'MSYS2 UCRT64 and CLANG64'
    $msys2Tool = Get-GameWipProjectTool -Id msys2
    $bash = Join-Path $ToolConfig.MsysRoot 'usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash))
    {
        $before = Get-GameWipDetectedTool -Tool $msys2Tool
        Install-GameWipWingetTool -Tool $msys2Tool -Version $null
        if (-not $before.Installed)
        {
            Add-GameWipOwnedWingetPackage -Id ([string]$msys2Tool.provider.package)
        }
        $state = Get-GameWipSetupState
        $state.msys2InstalledBySetup = $true
        Save-GameWipSetupState -State $state
        Write-GameWipMsys2Ownership -Origin created
        Add-GameWipOperationChange -Message "Installed MSYS2 at $($ToolConfig.MsysRoot)"
    }
    $packages = Get-GameWipMsys2PackageConfig -ProjectTools $ProjectTools
    Install-GameWipMsys2PackageSet -MsysRoot $ToolConfig.MsysRoot -PackageConfig $packages -Update:$Update
    Test-GameWipMsys2Toolchain -ProjectTools $ProjectTools
}

function Invoke-GameWipSetupToolStep
{
    Write-GameWipSection 'Project tools'
    Initialize-GameWipSetupManagedToolRoot
    Invoke-GameWipToolEnsure -Selector all -ConsentAlreadyGranted
}

function Invoke-GameWipSetupRepositoryStep
{
    param([switch]$Update)
    Write-GameWipSection 'Repository'
    $wasZip = -not (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
    $alreadyFetched = $false
    if ($wasZip)
    {
        Initialize-GameWipRepository -RepositoryRoot $RepositoryRoot -Branch $Branch -ChooseBranch:(-not $NonInteractive)
        $alreadyFetched = $true
    }
    else
    {
        $chooseBranch = -not $NonInteractive -and [string]::IsNullOrWhiteSpace($Branch)

        Switch-GameWipRepositoryBranch `
            -RepositoryRoot $RepositoryRoot `
            -Branch $Branch `
            -ChooseBranch:$chooseBranch

        $alreadyFetched = -not [string]::IsNullOrWhiteSpace($Branch) -or $chooseBranch
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
}

function Invoke-GameWipSetupEditorStep
{
    param([switch]$Choose, [switch]$Update)
    Write-GameWipSection 'Editor integration'
    $preference = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    if ($Choose)
    {
        if (-not (Select-GameWipEditor -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig))
        {
            throw 'Editor selection was cancelled.'
        }
    }
    elseif (-not (Test-Path -LiteralPath $preference))
    {
        Save-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -Editors @($EditorConfig.Default)
    }
    $selected = @(Get-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    Install-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $selected -Update:$Update
}

function Invoke-GameWipSetupVisualStudioStep
{
    param([switch]$Update)
    $visualStudio = $EditorConfig.Options | Where-Object Handler -eq 'visual-studio' | Select-Object -First 1
    Install-GameWipVisualStudio -PackageId $visualStudio.Package -VsConfigPath (Join-Path $RepositoryRoot '.vsconfig') -Update:$Update
}

function Invoke-GameWipSetupTracyStep
{
    Write-GameWipSection 'Tracy profiler tools'
    Initialize-GameWipSetupManagedToolRoot
    Invoke-GameWipTracyToolBuild -RepositoryRoot $RepositoryRoot -MsysRoot $ToolConfig.MsysRoot
}

function Invoke-GameWipSetupDocumentationStep
{
    param([switch]$Open)
    Write-GameWipSection 'Documentation'
    Invoke-GameWipDocumentationBuild -RepositoryRoot $RepositoryRoot -Open:$Open
}

function Invoke-GameWipSetupEnvironmentCheck
{
    Write-GameWipSection 'Environment check'
    $failures = [System.Collections.Generic.List[string]]::new()
    Write-Host '  Checking declared MSYS2 packages...'
    $packageConfig = Get-GameWipMsys2PackageConfig -ProjectTools $ProjectTools
    $packages = @($packageConfig.Common) + @($packageConfig.Ucrt64) + @($packageConfig.Clang64)
    foreach ($missing in @(Get-GameWipMissingMsys2Package -MsysRoot $ToolConfig.MsysRoot -Packages $packages))
    {
        $failures.Add("Missing MSYS2 package: $missing") | Out-Null
    }
    $checkedTools = @($ProjectTools.tools | Where-Object { $_.capabilities.detectInstalled -and $_.versionPolicy -ne 'informational' })
    Write-Host "  Checking $($checkedTools.Count) declared project tools..."
    for ($toolIndex = 0; $toolIndex -lt $checkedTools.Count; ++$toolIndex)
    {
        $tool = $checkedTools[$toolIndex]
        $detected = Get-GameWipDetectedTool -Tool $tool
        $compatibility = Get-GameWipToolCompatibility -Tool $tool -Detected $detected
        $semantic = switch ($compatibility)
        {
            'compatible'
            {
                'Success'
            }
            'missing'
            {
                'Failure'
            }
            default
            {
                'Warning'
            }
        }
        Write-GameWipStatusLine `
            -Status "$($toolIndex + 1)/$($checkedTools.Count)" `
            -Text ([string]$tool.id) `
            -Suffix "($compatibility)" `
            -Semantic $semantic `
            -SuffixSemantic Muted `
            -Indent 4
        if ($compatibility -ne 'compatible')
        {
            $failures.Add("Project tool '$($tool.id)' is not compatible.") | Out-Null
        }
    }
    Write-Host '  Checking matching Tracy tools...'
    if (-not (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot))
    {
        $failures.Add('Matching Tracy profiler tools are not installed.') | Out-Null
    }
    Write-Host '  Checking repository and editor integration...'
    try
    {
        Test-GameWipRepositoryState -RepositoryRoot $RepositoryRoot
    }
    catch
    {
        $failures.Add($_.Exception.Message) | Out-Null
    }
    $selected = @(Get-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig)
    foreach ($failure in @(Get-GameWipEditorFailure -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $selected))
    {
        $failures.Add($failure) | Out-Null
    }
    if ($failures.Count -ne 0)
    {
        foreach ($failure in $failures)
        {
            Write-GameWipStatusLine -Status FAIL -Text $failure -Semantic Failure -Indent 2
        }
        throw "$($failures.Count) environment check(s) failed. Run '.\setup.bat repair'."
    }
    Write-GameWipStatusLine -Status OK -Text 'Complete selected development environment is ready.' -Semantic Success -Indent 2
}

function Get-GameWipSetupPlan
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)
    switch ($SelectedAction)
    {
        'full'
        {
            return @('Install/verify MSYS2 toolchains.', 'Ensure exact declared project tools.', 'Prepare repository/submodules.', 'Apply selected editor integrations.', 'Build matching Tracy tools.', 'Build docs unless skipped.', 'Verify complete environment.')
        }
        'repair'
        {
            return @('Reapply declared environment state without advancing project pins.', 'Verify complete environment.')
        }
        'update'
        {
            return @('Update MSYS2/environment packages without advancing exact project pins.', 'Ensure declared project tools.', 'Fast-forward repository.', 'Refresh integrations.', 'Verify complete environment.')
        }
        'tools'
        {
            return @('Install/repair tools at versions already declared by the checkout.', 'Do not modify project pins.')
        }
        'msys2'
        {
            return @('Install/repair declared UCRT64 and CLANG64 packages.')
        }
        'repository'
        {
            return @('Prepare Git/submodules and development configuration.')
        }
        'editor'
        {
            return @('Apply selected editor/IDE integrations.')
        }
        'visual-studio'
        {
            return @('Install/repair Visual Studio using .vsconfig.')
        }
        'profiler'
        {
            return @('Build/install Tracy tools matching the pinned submodule.')
        }
        'docs'
        {
            return @(
                'Configure and build the documentation preset.',
                'Verify generated documentation.',
                'Open the manual only after a real interactive build.'
            )
        }
        default
        {
            return @()
        }
    }
}

function Invoke-GameWipCompleteSetup
{
    param([switch]$Update, [switch]$RefreshMsys2)
    $preference = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot
    if (-not (Test-Path -LiteralPath $preference))
    {
        if ($NonInteractive)
        {
            Save-GameWipEditorSelection -RepositoryRoot $RepositoryRoot -Editors @($EditorConfig.Default)
        }
        elseif (-not (Select-GameWipEditor -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig))
        {
            throw 'Complete setup was cancelled before editor selection.'
        }
    }
    if (Test-Path -LiteralPath (Get-GameWipManagedToolRoot))
    {
        Initialize-GameWipSetupManagedToolRoot
    }
    Invoke-GameWipSetupMsys2Step -Update:$RefreshMsys2
    Invoke-GameWipSetupToolStep
    Invoke-GameWipSetupRepositoryStep -Update:$Update
    Invoke-GameWipSetupEditorStep -Update:$Update
    Invoke-GameWipSetupTracyStep
    if (-not $SkipDocs)
    {
        Invoke-GameWipSetupDocumentationStep
    }
    Invoke-GameWipSetupEnvironmentCheck
}

function Invoke-GameWipSetupActionBody
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
        'check'
        {
            Invoke-GameWipSetupEnvironmentCheck
        }
        'tools'
        {
            Invoke-GameWipSetupToolStep
        }
        'visual-studio'
        {
            Invoke-GameWipSetupVisualStudioStep
        }
        'msys2'
        {
            Invoke-GameWipSetupMsys2Step
        }
        'repository'
        {
            Invoke-GameWipSetupRepositoryStep
        }
        'profiler'
        {
            Invoke-GameWipSetupTracyStep
        }
        'editor'
        {
            Invoke-GameWipSetupEditorStep -Choose:(-not $NonInteractive)
        }
        'docs'
        {
            Invoke-GameWipSetupDocumentationStep -Open:(-not $NonInteractive)
        }
        'uninstall'
        {
            Invoke-GameWipUninstall -RepositoryRoot $RepositoryRoot -Preview:$Preview
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
            throw "Setup action '$SelectedAction' has no implementation."
        }
    }
}

function Invoke-GameWipSetupOperation
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)
    Assert-GameWipSetupWindows
    Assert-GameWipSetupRepository -RepositoryRoot $RepositoryRoot
    Assert-GameWipSetupActionCatalog
    $actionInfo = Get-GameWipSetupAction -Id $SelectedAction

    return Invoke-GameWipOperation `
        -Label "setup-$SelectedAction" `
        -NonInteractive:$NonInteractive `
        -Yes:$Yes `
        -Preview:$Preview `
        -OutputMode $OutputMode `
        -NoColor:$NoColor `
        -SuppressReceipt:$Quiet `
        -SuppressOutput:$Quiet `
        -ScriptBlock {
        if ($SelectedAction -in @('help', 'list', 'check'))
        {
            Invoke-GameWipSetupActionBody -SelectedAction $SelectedAction
            return
        }
        if ($SelectedAction -eq 'uninstall')
        {
            # Uninstall owns its confirmation so inventory is always printed first.
            Invoke-GameWipSetupActionBody -SelectedAction uninstall
            return
        }
        if ($SelectedAction -in @('full', 'repair', 'update'))
        {
            Show-GameWipSetupSizeEstimate
        }
        $plan = @(Get-GameWipSetupPlan -SelectedAction $SelectedAction)
        Invoke-GameWipMutation -Summary $actionInfo.Description -Risk ([string]$actionInfo.Risk) -Plan $plan -Body { Invoke-GameWipSetupActionBody -SelectedAction $SelectedAction } | Out-Null
    }
}

function Show-GameWipSetupMenu
{
    Assert-GameWipSetupWindows
    Assert-GameWipSetupRepository -RepositoryRoot $RepositoryRoot
    Assert-GameWipSetupActionCatalog
    while ($true)
    {
        Write-GameWipSection 'GameWIP Development Environment'
        $menuActions = @($SetupActionConfig.Actions | Where-Object { $_.ContainsKey('Key') })
        foreach ($actionInfo in $menuActions)
        {
            Write-Host "$($actionInfo.Key). $($actionInfo.Name)"
        }
        Write-Host 'ESC. Exit'
        $keys = @($menuActions | ForEach-Object { [string]$_.Key })
        $choice = Read-GameWipActionKey -Prompt 'Choose an action:' -AllowedKeys $keys -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        $selected = $menuActions | Where-Object { [string]$_.Key -ieq [string]$choice.Value } | Select-Object -First 1
        if ($null -ne $selected)
        {
            Invoke-GameWipSetupOperation -SelectedAction ([string]$selected.Id) | Out-Null
        }
    }
}
