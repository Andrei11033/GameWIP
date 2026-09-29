# Setup orchestration. The entry point loads this library and dispatches actions.

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

# ------------------------------------------------------------
# Action catalog and presentation
# ------------------------------------------------------------

function Get-GameWipSetupActionOptionIds
{
    param([Parameter(Mandatory = $true)][string]$Action)

    $ids = @($SetupActionConfig.Options | Where-Object { $_.ContainsKey('Global') -and [bool]$_.Global } | ForEach-Object { [string]$_.Id })
    if ($SetupActionConfig.ActionOptions.ContainsKey($Action))
    {
        $ids += @($SetupActionConfig.ActionOptions[$Action])
    }
    return @($ids | Select-Object -Unique)
}

function Assert-GameWipSetupOptionCatalog
{
    $options = @($SetupActionConfig.Options)
    $duplicateOptions = @($options | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object Count -gt 1)
    if ($duplicateOptions.Count -ne 0)
    {
        throw "Duplicate setup option IDs: $($duplicateOptions.Name -join ', ')."
    }
    $optionIds = @{}
    foreach ($option in $options)
    {
        $id = [string]$option.Id
        if ($id -notmatch '^[A-Za-z][A-Za-z0-9]*$')
        {
            throw "Setup option '$id' has an invalid identifier."
        }
        $optionIds[$id.ToLowerInvariant()] = $id
        if ($option.Kind -in @('string', 'integer', 'choice', 'string-list') -and [string]::IsNullOrWhiteSpace([string]$option.ValueName))
        {
            throw "Setup option '$id' must declare valueName."
        }
        if ($option.Kind -eq 'choice' -and @($option.Choices).Count -eq 0)
        {
            throw "Setup option '$id' must declare choices."
        }
    }
    $actionIds = @($SetupActionConfig.Actions | ForEach-Object { [string]$_.Id })
    foreach ($actionName in $SetupActionConfig.ActionOptions.Keys)
    {
        if ($actionIds -notcontains [string]$actionName)
        {
            throw "Setup option mapping references unknown action '$actionName'."
        }
        foreach ($optionId in @($SetupActionConfig.ActionOptions[$actionName]))
        {
            if (-not $optionIds.ContainsKey(([string]$optionId).ToLowerInvariant()))
            {
                throw "Setup action '$actionName' references unknown option '$optionId'."
            }
        }
    }
    foreach ($actionId in $actionIds)
    {
        if (-not $SetupActionConfig.ActionOptions.ContainsKey($actionId))
        {
            throw "Setup action '$actionId' is missing an option mapping."
        }
    }
}

function Assert-GameWipSetupActionOptions
{
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][hashtable]$BoundParameters
    )

    $allowed = @{}
    foreach ($optionId in @(Get-GameWipSetupActionOptionIds -Action $Action))
    {
        $allowed[([string]$optionId).ToLowerInvariant()] = $true
    }
    foreach ($boundName in $BoundParameters.Keys)
    {
        $boundOption = @($SetupActionConfig.Options | Where-Object { [string]$_.Id -ieq [string]$boundName })
        if ($boundOption.Count -eq 0)
        {
            continue
        }
        if (-not $allowed.ContainsKey(([string]$boundName).ToLowerInvariant()))
        {
            throw "Option '-$boundName' is not supported for setup action '$Action'."
        }
    }
}

function Assert-GameWipSetupActionCatalog
{
    $actions = @($SetupActionConfig.Actions)
    Assert-GameWipSetupOptionCatalog
    $requiredActionIds = @('menu', 'full', 'check', 'update', 'repair', 'deps', 'uninstall', 'tool', 'vs', 'msys2', 'repo', 'profiler', 'editor', 'doc', 'list', 'help')
    $actionIds = @($actions | ForEach-Object { [string]$_.Id })
    if ((($requiredActionIds | Sort-Object) -join "`n") -cne (($actionIds | Sort-Object) -join "`n"))
    {
        throw "Setup action catalog drift. Required: $($requiredActionIds -join ', '); configured: $($actionIds -join ', ')."
    }
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
    $actionNames = @{}
    foreach ($action in $actions)
    {
        $canonical = [string]$action.Id
        $canonicalKey = $canonical.ToLowerInvariant()
        if ($actionNames.ContainsKey($canonicalKey))
        {
            throw "Setup action name '$canonical' is duplicated or collides with an alias."
        }
        $actionNames[$canonicalKey] = $canonical
        $hasAliases = ($action -is [hashtable] -and ($action.ContainsKey('aliases') -or $action.ContainsKey('Aliases'))) -or ($action.PSObject.Properties.Name -contains 'aliases')
        if (-not $hasAliases -or $null -eq $action.Aliases)
        {
            $aliases = @()
        }
        else
        {
            $aliases = @($action.Aliases)
        }
        foreach ($alias in $aliases)
        {
            $aliasText = [string]$alias
            if ($aliasText -notmatch '^[a-z][a-z0-9-]*$')
            {
                throw "Setup action '$canonical' has invalid alias '$aliasText'."
            }
            $aliasKey = $aliasText.ToLowerInvariant()
            if ($actionNames.ContainsKey($aliasKey))
            {
                throw "Setup action alias '$aliasText' collides with '$($actionNames[$aliasKey])'."
            }
            $actionNames[$aliasKey] = $canonical
        }
    }
    foreach ($toolId in @($SetupActionConfig.BootstrapToolIds))
    {
        if (@($ProjectTools.tools.id) -notcontains $toolId)
        {
            throw "Setup bootstrap tool '$toolId' is not registered."
        }
    }
}

function Resolve-GameWipSetupActionName
{
    param([Parameter(Mandatory = $true)][string]$Name)

    foreach ($action in @($SetupActionConfig.Actions))
    {
        if ([string]$action.Id -ieq $Name)
        {
            return [string]$action.Id
        }
        $hasAliases = ($action -is [hashtable] -and ($action.ContainsKey('aliases') -or $action.ContainsKey('Aliases'))) -or ($action.PSObject.Properties.Name -contains 'aliases')
        if (-not $hasAliases -or $null -eq $action.Aliases)
        {
            $aliases = @()
        }
        else
        {
            $aliases = @($action.Aliases)
        }
        foreach ($alias in $aliases)
        {
            if ([string]$alias -ieq $Name)
            {
                return [string]$action.Id
            }
        }
    }
    return $null
}

function Get-GameWipSetupAction
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $canonicalId = Resolve-GameWipSetupActionName -Name $Id
    $actionMatches = @($SetupActionConfig.Actions | Where-Object { $_.Id -eq $canonicalId })
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
        $hasAliases = ($actionInfo -is [hashtable] -and ($actionInfo.ContainsKey('aliases') -or $actionInfo.ContainsKey('Aliases'))) -or ($actionInfo.PSObject.Properties.Name -contains 'aliases')
        if ($hasAliases -and $null -ne $actionInfo.Aliases -and @($actionInfo.Aliases).Count -gt 0)
        {
            $aliases = " (aliases: $(@($actionInfo.Aliases) -join ', '))"
        }
        else
        {
            $aliases = ''
        }
        Write-Host ('  {0,-16} [{1,-11}] {2}{3}' -f $actionInfo.Id, $actionInfo.Risk, $actionInfo.Description, $aliases)
    }
}

function Show-GameWipSetupHelp
{
    Write-Host 'Usage:'
    Write-Host '  setup.bat <action> [options]'
    Write-Host ''
    Show-GameWipCommonControlHelp -OptionDefinitions $SetupActionConfig.Options
    Write-Host ''
    Write-Host 'Action-specific options:'
    Show-GameWipOptionDefinitions -OptionDefinitions $SetupActionConfig.Options -OptionIds @('Branch', 'SkipDocs')
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

# ------------------------------------------------------------
# Setup steps
# ------------------------------------------------------------

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
        $before = (Get-GameWipToolStatus -Tool $msys2Tool).Detected
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

function Invoke-GameWipSetupDependencyStep
{
    Write-GameWipSection 'GameWIP dependency cache'
    Invoke-GameWipDependencyPreparation
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
    try
    {
        Test-GameWipDependencyCache -ThrowOnFailure | Out-Null
    }
    catch
    {
        Write-Host '  Tracy source cache is not ready; preparing the shared dependency cache.'
        Invoke-GameWipDependencyPreparation
    }
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
    $toolchain = Test-GameWipToolchain -DisplayStatus -Indent 4
    foreach ($failure in @($toolchain.Failures))
    {
        $failures.Add("Project toolchain: $failure") | Out-Null
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
    Write-Host '  Checking GameWIP dependency cache...'
    try
    {
        Test-GameWipDependencyCache -ThrowOnFailure | Out-Null
    }
    catch
    {
        $failures.Add($_.Exception.Message) | Out-Null
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

# ------------------------------------------------------------
# Planning and execution
# ------------------------------------------------------------

function Get-GameWipSetupPlan
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)
    switch ($SelectedAction)
    {
        'full'
        {
            return @('Install/verify MSYS2 toolchains.', 'Install or repair exact declared project tools.', 'Prepare the repository and development configuration.', 'Prepare or reuse the locked dependency cache.', 'Apply selected editor integrations.', 'Build matching Tracy tools.', 'Build docs unless skipped.', 'Verify complete environment.')
        }
        'repair'
        {
            return @('Reapply declared environment state without advancing project pins.', 'Prepare or reuse the locked dependency cache.', 'Verify complete environment.')
        }
        'update'
        {
            return @('Update MSYS2/environment packages without advancing exact project pins.', 'Install or update declared project tools.', 'Fast-forward repository.', 'Prepare or refresh dependencies for the updated lock file.', 'Refresh integrations.', 'Verify complete environment.')
        }
        'deps'
        {
            return @(Get-GameWipDependencyPreparationPlan)
        }
        'tool'
        {
            return @('Install, update, or repair tools at versions already declared by the checkout.', 'Do not modify project pins.')
        }
        'msys2'
        {
            return @('Install/repair declared UCRT64 and CLANG64 packages.')
        }
        'repo'
        {
            return @('Prepare the Git checkout and development configuration.')
        }
        'editor'
        {
            return @('Apply selected editor/IDE integrations.')
        }
        'vs'
        {
            return @('Install/repair Visual Studio using .vsconfig.')
        }
        'profiler'
        {
            return @('Prepare or reuse the locked Tracy source, then build/install matching profiler tools.')
        }
        'doc'
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
    Invoke-GameWipSetupDependencyStep
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
        'deps'
        {
            Invoke-GameWipSetupDependencyStep
        }
        'check'
        {
            Invoke-GameWipSetupEnvironmentCheck
        }
        'tool'
        {
            Invoke-GameWipSetupToolStep
        }
        'vs'
        {
            Invoke-GameWipSetupVisualStudioStep
        }
        'msys2'
        {
            Invoke-GameWipSetupMsys2Step
        }
        'repo'
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
        'doc'
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

# ------------------------------------------------------------
# Interactive menu
# ------------------------------------------------------------

function Show-GameWipSetupMenu
{
    Assert-GameWipSetupWindows
    Assert-GameWipSetupRepository -RepositoryRoot $RepositoryRoot
    Assert-GameWipSetupActionCatalog
    while ($true)
    {
        $menuActions = @($SetupActionConfig.Actions | Where-Object { $_.ContainsKey('Key') })
        $menuItems = @($menuActions | ForEach-Object {
                [pscustomobject]@{
                    Key = [string]$_.Key
                    Label = [string]$_.Name
                    Handler = [string]$_.Id
                }
            })
        $choice = Read-GameWipActionMenuItem `
            -Title 'GameWIP Development Environment' `
            -Prompt 'Choose an action:' `
            -Items $menuItems `
            -ExitLabel Exit
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        Invoke-GameWipSetupOperation -SelectedAction ([string]$choice.Value) | Out-Null
    }
}
