# Setup orchestration. The entry point loads this library and dispatches actions.

Set-StrictMode -Version Latest

$SetupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$SetupActionConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config/setup.json') -Name 'setup' -SchemaPath (Join-Path $ScriptsRoot 'schemas/setup.schema.json')
$EditorConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config/editors.json') -Name 'editors' -SchemaPath (Join-Path $ScriptsRoot 'schemas/editors.schema.json')
$script:SetupStatePath = Join-Path (Get-GameWipStorageAreaPath -Area state) 'setup.json'
$ToolConfig = @{ MsysRoot = Get-GameWipMsys2Root }

foreach ($file in @('Common.ps1', 'Repository.ps1', 'Editor.ps1', 'Documentation.ps1', 'Uninstall.ps1'))
{
    . (Join-Path $PSScriptRoot $file)
}

if (Test-GameWipWindowsHost)
{
    $windowsPlatformRoot = Join-Path $SetupRoot 'platform/windows'

    foreach ($file in @('Winget.ps1', 'Msys2.ps1', 'VisualStudio.ps1', 'Tracy.ps1', 'Uninstall.ps1'))
    {
        . (Join-Path $windowsPlatformRoot $file)
    }
}
elseif ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
{
    . (Join-Path $SetupRoot 'platform/linux/Linux.ps1')
    . (Join-Path $SetupRoot 'platform/linux/Uninstall.ps1')
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
    Assert-GameWipUniqueId -Items $options -Label 'setup option'
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
    Assert-GameWipUniqueId -Items $actions -Label 'setup action'
    $requiredActionIds = @('menu', 'full', 'check', 'update', 'repair', 'deps', 'uninstall', 'tool', 'vs', 'msys2', 'system', 'repo', 'profiler', 'editor', 'doc', 'list', 'help')
    $actionIds = @($actions | ForEach-Object { [string]$_.Id })

    if ((($requiredActionIds | Sort-Object) -join "`n") -cne (($actionIds | Sort-Object) -join "`n"))
    {
        $missingActions = @($requiredActionIds | Where-Object { $actionIds -cnotcontains $_ })
        $unexpectedActions = @($actionIds | Where-Object { $requiredActionIds -cnotcontains $_ })
        $differences = @()

        if ($missingActions.Count -gt 0)
        {
            $differences += "missing required actions: $($missingActions -join ', ')"
        }

        if ($unexpectedActions.Count -gt 0)
        {
            $differences += "unexpected configured actions: $($unexpectedActions -join ', ')"
        }

        throw "Setup action catalog drift: $($differences -join '; ')."
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

    foreach ($action in $actions)
    {
        if (-not $action.ContainsKey('Platforms'))
        {
            continue
        }

        foreach ($platform in @($action.Platforms))
        {
            if ([string]$platform -notin @('windows', 'linux'))
            {
                throw "Setup action '$($action.Id)' has an unsupported host platform '$platform'."
            }
        }
    }
}

function Get-GameWipAvailableSetupActions
{
    $hostPlatform = if (Test-GameWipWindowsHost)
    {
        'windows'
    }
    else
    {
        'linux'
    }

    return @($SetupActionConfig.Actions | Where-Object {
            (-not $_.ContainsKey('Platforms')) -or @($_.Platforms) -contains $hostPlatform
        })
}

function Resolve-GameWipSetupActionName
{
    param([Parameter(Mandatory = $true)][string]$Name)

    foreach ($action in @(Get-GameWipAvailableSetupActions))
    {
        if ([string]$action.Id -ieq $Name)
        {
            return [string]$action.Id
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
        $listCommand = "$(Get-GameWipSetupHelperCommand) list"
        throw "Unknown setup action '$Id'. Run '$listCommand'."
    }

    return $actionMatches[0]
}

function Show-GameWipSetupActionCatalog
{
    Write-GameWipSection 'Setup actions'

    foreach ($actionInfo in @(Get-GameWipAvailableSetupActions))
    {
        Write-Host ('  {0,-16} [{1,-11}] {2}' -f $actionInfo.Id, $actionInfo.Risk, $actionInfo.Description)
    }
}

function Show-GameWipSetupHelp
{
    Write-Host 'Usage:'

    if (Test-GameWipWindowsHost)
    {
        Write-Host '  setup.bat <action> [options]'
    }
    else
    {
        Write-Host '  bash ./setup.sh <action> [options]'
    }
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

function Select-GameWipExistingMsys2
{
    # Respect explicit roots, previews, and noninteractive runs.
    if (-not (Test-GameWipWindowsHost) -or
        -not [string]::IsNullOrWhiteSpace([string]$env:GAMEWIP_MSYS2_ROOT) -or
        $Preview -or $NonInteractive)
    {
        return
    }

    $localRoot = Get-GameWipMsys2Root

    if (Test-Path -LiteralPath (Join-Path $localRoot 'usr/bin/bash.exe') -PathType Leaf)
    {
        return
    }
    $existingRoots = @(Get-GameWipExistingMsys2Roots)

    if ($existingRoots.Count -eq 0)
    {
        return
    }
    Write-GameWipSection 'Existing MSYS2 installation'
    Write-Host 'Choose whether to reuse an existing installation or create a separate copy under the GameWIP build folder.'

    # Ask before reusing a discovered installation.
    if ($existingRoots.Count -eq 1)
    {
        $selectedRoot = $existingRoots[0]

        if (-not (Read-GameWipYesNo -Prompt "Use the existing MSYS2 installation at '$selectedRoot'? Setup may add the listed GameWIP packages." -Default $false))
        {
            Write-Host "  A separate MSYS2 copy will be installed under '$localRoot'."
            return
        }
    }
    else
    {
        for ($index = 0; $index -lt $existingRoots.Count; ++$index)
        {
            Write-Host ('  {0}. {1}' -f ($index + 1), $existingRoots[$index])
        }
        Write-Host '  Press Enter to install a separate local copy.'
        $selection = Read-Host 'Select an existing MSYS2 installation to reuse'

        if ([string]::IsNullOrWhiteSpace($selection))
        {
            Write-Host "  A separate MSYS2 copy will be installed under '$localRoot'."
            return
        }
        $selectedIndex = 0

        if (-not [int]::TryParse($selection, [ref]$selectedIndex) -or $selectedIndex -lt 1 -or $selectedIndex -gt $existingRoots.Count)
        {
            throw "Invalid MSYS2 selection '$selection'."
        }
        $selectedRoot = $existingRoots[$selectedIndex - 1]
    }

    # Share the selected root with the setup and tool providers.
    $env:GAMEWIP_MSYS2_ROOT = $selectedRoot
    $ToolConfig.MsysRoot = Get-GameWipMsys2Root
    Write-Host "  Selected existing MSYS2: $($ToolConfig.MsysRoot)"
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
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))

    if (Test-Path -LiteralPath $root)
    {
        $entries = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | Where-Object Name -ne '.gamewip-managed.json')

        if (-not (Test-GameWipPathWithinRoot -Path $root -Root $buildRoot) -and
            -not (Test-GameWipManagedToolRootOwnership -Root $root) -and
            $entries.Count -eq 0)
        {
            throw "An external managed-tools root must contain an existing installation; new GameWIP-managed roots must stay under '$buildRoot'."
        }

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

function Invoke-GameWipSetupMsys2Step
{
    param([switch]$Update)
    Write-GameWipSection 'MSYS2 UCRT64 and CLANG64'
    $bash = Join-Path $ToolConfig.MsysRoot 'usr\bin\bash.exe'

    if (-not (Test-Path -LiteralPath $bash))
    {
        Install-GameWipMsys2BaseArchive -MsysRoot $ToolConfig.MsysRoot
    }
    $packages = Get-GameWipMsys2PackageConfig -ProjectTools $ProjectTools
    Install-GameWipMsys2PackageSet -MsysRoot $ToolConfig.MsysRoot -PackageConfig $packages -Update:$Update
    Test-GameWipMsys2Toolchain -ProjectTools $ProjectTools
}

function Get-GameWipMsys2SetupPlan
{
    param([switch]$Update)

    # List the installation choice and any missing package sets.
    $root = Get-GameWipMsys2Root
    $bash = Join-Path $root 'usr/bin/bash.exe'
    $hasExistingInstall = Test-Path -LiteralPath $bash -PathType Leaf
    $plan = [System.Collections.Generic.List[string]]::new()

    if ($hasExistingInstall)
    {
        $plan.Add("Use existing MSYS2 at '$root'.") | Out-Null
    }
    else
    {
        $plan.Add("Install the MSYS2 base archive under '$root'.") | Out-Null
    }
    $packages = Get-GameWipMsys2PackageConfig -ProjectTools $ProjectTools
    $installed = if ($hasExistingInstall)
    {
        Get-GameWipMsys2InstalledPackageNames -MsysRoot $root
    }
    else
    {
        [pscustomobject]@{ State = 'missing'; Packages = @() }
    }
    $installedNames = [System.Collections.Generic.HashSet[string]]::new([string[]]$installed.Packages)

    # Compare each environment with the selected installation.
    foreach ($environment in @('Common', 'Ucrt64', 'Clang64'))
    {
        $missing = if ($installed.State -eq 'ready')
        {
            @($packages[$environment] | Where-Object { -not $installedNames.Contains([string]$_) })
        }
        else
        {
            @($packages[$environment])
        }

        if ($missing.Count -ne 0)
        {
            $plan.Add("Install $environment MSYS2 packages: $($missing -join ', ').") | Out-Null
        }
    }

    if ($Update)
    {
        $plan.Add('Refresh MSYS2 packages with pacman -Syu.') | Out-Null
    }

    return $plan.ToArray()
}

function Invoke-GameWipSetupToolStep
{
    Write-GameWipSection 'Project tools'

    if ((Test-GameWipWindowsHost) -and
        -not (Test-Path -LiteralPath (Join-Path (Get-GameWipMsys2Root) 'usr/bin/bash.exe') -PathType Leaf))
    {
        Invoke-GameWipSetupMsys2Step
    }
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

    if (-not (Test-GameWipWindowsHost) -and -not (Test-GameWipSetupCommand -Name 'code'))
    {
        Write-Host '  VS Code is not installed; editor integration is skipped.'
        return
    }

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
    $target = Get-GameWipSelectedBuildTarget
    $toolIds = @(Get-GameWipProjectReadinessToolIds -Target $target)
    $toolchain = Test-GameWipToolchain -ToolIds $toolIds -DisplayStatus -Indent 4

    foreach ($failure in @($toolchain.Failures))
    {
        $failures.Add("Project toolchain: $failure") | Out-Null
    }
    if (Test-GameWipWindowsHost)
    {
        Write-Host '  Checking matching Tracy tools...'

        if (-not (Test-GameWipTracyToolSet -RepositoryRoot $RepositoryRoot))
        {
            $failures.Add('Matching Tracy profiler tools are not installed.') | Out-Null
        }
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

    $codeInstalled = Test-GameWipSetupCommand -Name 'code'
    $editorsToCheck = [System.Collections.Generic.List[string]]::new()

    foreach ($id in $selected)
    {
        $option = $EditorConfig.Options | Where-Object { $_.Id -eq $id } | Select-Object -First 1

        if ($option.Handler -eq 'vscode')
        {
            if ($codeInstalled)
            {
                $editorsToCheck.Add($id) | Out-Null
            }
            else
            {
                Write-Host '  VS Code is not installed; its integration checks are skipped.'
            }

            continue
        }

        if ($option.Handler -eq 'visual-studio' -and (Test-GameWipWindowsHost))
        {
            if (Get-GameWipVisualStudioInstallation)
            {
                $editorsToCheck.Add($id) | Out-Null
            }
            else
            {
                Write-Host '  Visual Studio is not installed; its integration checks are skipped.'
            }

            continue
        }

        $editorsToCheck.Add($id) | Out-Null
    }

    foreach ($failure in @(Get-GameWipEditorFailure -RepositoryRoot $RepositoryRoot -EditorConfig $EditorConfig -SelectedEditors $editorsToCheck.ToArray()))
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
        $repairCommand = "$(Get-GameWipSetupHelperCommand) repair"
        throw "$($failures.Count) environment check(s) failed. Run '$repairCommand'."
    }

    Write-GameWipStatusLine -Status OK -Text 'Complete selected development environment is ready.' -Semantic Success -Indent 2
}

# ------------------------------------------------------------
# Planning and execution
# ------------------------------------------------------------

function Get-GameWipWindowsHostInstallPlan
{
    if (-not (Test-GameWipWindowsHost))
    {
        return @()
    }
    $packages = @(
        $ProjectTools.tools |
            Where-Object { $_.provider.kind -eq 'winget' } |
            ForEach-Object { "$($_.name) ($($_.provider.package))" }
    )

    if ($packages.Count -eq 0)
    {
        return @()
    }

    return @("Ask before installing missing WinGet host tools outside build/: $($packages -join ', ').")
}

function Get-GameWipEditorInstallPlan
{
    if (Test-GameWipWindowsHost)
    {
        return @(
            'Use existing VS Code; skip its integration if missing.',
            'Ask before installing Visual Studio when selected.'
        )
    }

    return @('Use installed editors and skip integrations for missing editors.')
}

function Get-GameWipSetupPlan
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    switch ($SelectedAction)
    {
        'full'
        {
            $hostPlan = if (Test-GameWipWindowsHost)
            {
                @(Get-GameWipMsys2SetupPlan -Update)
            }
            else
            {
                @(Get-GameWipSetupLinuxPlan)
            }
            $commonPlan = @('Install or repair declared project tools.', 'Prepare the repository and development configuration.', 'Prepare or reuse the locked dependency cache.')
            $commonPlan += @(Get-GameWipWindowsHostInstallPlan)
            $commonPlan += @(Get-GameWipEditorInstallPlan)

            if (Test-GameWipWindowsHost)
            {
                $commonPlan += 'Build matching Tracy tools.'
            }

            $commonPlan += @('Build docs unless skipped.', 'Verify the selected environment.')
            return @($hostPlan) + @($commonPlan)
        }

        'repair'
        {
            if (Test-GameWipWindowsHost)
            {
                return @(Get-GameWipMsys2SetupPlan) +
                    @('Repair declared project tools.') +
                    @(Get-GameWipWindowsHostInstallPlan) +
                    @('Prepare or reuse the locked dependency cache.') +
                    @(Get-GameWipEditorInstallPlan) +
                    @('Verify complete environment.')
            }

            return @(Get-GameWipSetupLinuxPlan) +
                @('Repair declared project tools.', 'Prepare or reuse the locked dependency cache.') +
                @(Get-GameWipEditorInstallPlan) +
                @('Verify the selected environment.')
        }

        'update'
        {
            if (Test-GameWipWindowsHost)
            {
                return @(Get-GameWipMsys2SetupPlan -Update) +
                    @('Install or update declared project tools.') +
                    @(Get-GameWipWindowsHostInstallPlan) +
                    @('Fast-forward repository.', 'Prepare or refresh dependencies for the updated lock file.') +
                    @(Get-GameWipEditorInstallPlan) +
                    @('Verify complete environment.')
            }

            return @(Get-GameWipSetupLinuxPlan) +
                @('Update declared project tools.', 'Fast-forward repository.', 'Prepare or refresh dependencies for the updated lock file.') +
                @(Get-GameWipEditorInstallPlan) +
                @('Verify the selected environment.')
        }

        'deps'
        {
            return @(Get-GameWipDependencyPreparationPlan)
        }

        'tool'
        {
            $toolPlan = @('Install, update, or repair tools at versions already declared by the checkout.', 'Do not modify project pins.')

            if (Test-GameWipWindowsHost)
            {
                return @(Get-GameWipMsys2SetupPlan) + $toolPlan + @(Get-GameWipWindowsHostInstallPlan)
            }

            return $toolPlan
        }

        'msys2'
        {
            return @(Get-GameWipMsys2SetupPlan)
        }

        'system'
        {
            return @(Get-GameWipSetupLinuxPlan)
        }

        'repo'
        {
            return @('Prepare the Git checkout and development configuration.')
        }

        'editor'
        {
            return @(Get-GameWipEditorInstallPlan)
        }

        'vs'
        {
            $visualStudio = $EditorConfig.Options | Where-Object Handler -eq 'visual-studio' | Select-Object -First 1
            return @(
                'Use an existing Visual Studio installation and apply .vsconfig.',
                "If Visual Studio is missing, ask before installing $($visualStudio.Package) outside build/."
            )
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
    if (Test-GameWipWindowsHost)
    {
        Invoke-GameWipSetupMsys2Step -Update:$RefreshMsys2
    }
    else
    {
        Invoke-GameWipSetupLinuxPrerequisites
    }
    Invoke-GameWipSetupToolStep
    Invoke-GameWipSetupRepositoryStep -Update:$Update
    Invoke-GameWipSetupDependencyStep
    Invoke-GameWipSetupEditorStep -Update:$Update
    if (Test-GameWipWindowsHost)
    {
        Invoke-GameWipSetupTracyStep
    }

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
        'system'
        {
            Invoke-GameWipSetupLinuxPrerequisites
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
    Assert-GameWipSetupHost
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
        if ($SelectedAction -in @('full', 'repair', 'update', 'msys2', 'tool'))
        {
            Select-GameWipExistingMsys2
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
    Assert-GameWipSetupHost
    Assert-GameWipSetupRepository -RepositoryRoot $RepositoryRoot
    Assert-GameWipSetupActionCatalog

    while ($true)
    {
        $menuActions = @(Get-GameWipAvailableSetupActions | Where-Object { $_.ContainsKey('Key') })
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
