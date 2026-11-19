# Regression tests for setup metadata, consent, ownership, and orchestration boundaries.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$setupScript = Join-Path $repositoryRoot 'scripts\setup\Setup.ps1'
$powerShellPath = (Get-Process -Id $PID).Path

# ------------------------------------------------------------
# Setup catalog and CLI contract
# ------------------------------------------------------------

$actionConfig = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'scripts\setup\config\setup.json') | ConvertFrom-Json
$actions = @($actionConfig.Actions)

$duplicates = @($actions.Id | Group-Object | Where-Object Count -gt 1)

if ($duplicates.Count -ne 0)
{
    throw "Duplicate setup action IDs: $($duplicates.Name -join ', ')."
}

$menuActions = @($actions | Where-Object { $null -ne $_.PSObject.Properties['key'] })
$duplicateKeys = @($menuActions.Key | Group-Object | Where-Object Count -gt 1)

if ($duplicateKeys.Count -ne 0)
{
    throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')."
}

foreach ($requiredAction in @('menu', 'full', 'check', 'update', 'repair', 'uninstall', 'tool', 'vs', 'msys2', 'repo', 'deps', 'profiler', 'editor', 'doc', 'list', 'help'))
{
    if (@($actions.Id) -notcontains $requiredAction)
    {
        throw "Missing setup action '$requiredAction'."
    }
}
$expectedMenuOrder = @('full', 'check', 'repair', 'update', 'repo', 'tool', 'deps', 'msys2', 'system', 'editor', 'doc', 'profiler', 'vs', 'uninstall')
$actualMenuOrder = @($menuActions | ForEach-Object { [string]$_.Id })

if (($actualMenuOrder -join "`n") -cne ($expectedMenuOrder -join "`n"))
{
    throw 'Setup menu actions are not in the approved workflow order.'
}

foreach ($nonMenuAction in @('list', 'help', 'menu'))
{
    $action = @($actions | Where-Object Id -eq $nonMenuAction)[0]

    if ($null -ne $action.PSObject.Properties['key'] -or ($action -is [hashtable] -and ($action.ContainsKey('key') -or $action.ContainsKey('Key'))))
    {
        throw "Setup direct action '$nonMenuAction' must not appear as an operational menu task."
    }
}

foreach ($action in $actions)
{
    if ($action.risk -notin @('read-only', 'local', 'tracked', 'machine', 'destructive', 'remote'))
    {
        throw "Setup action '$($action.id)' has invalid risk metadata."
    }

    if ($action.mutation -notin @('none', 'local', 'tracked', 'machine', 'destructive', 'remote', 'conditional'))
    {
        throw "Setup action '$($action.id)' has invalid mutation metadata."
    }
}

$helpOutput = (& $powerShellPath -NoProfile -ExecutionPolicy Bypass -File $setupScript help 2>&1 | Out-String)
$expectedSetupUsage = if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows))
{
    'setup.bat <action>'
}
else
{
    'bash ./setup.sh <action>'
}

foreach ($requiredHelpText in @($expectedSetupUsage, 'Setup actions', '-Preview', '-NonInteractive', '-Yes', '-OutputMode', '-Quiet', '-NoColor', '-Json'))
{
    if ($helpOutput -notmatch [regex]::Escape($requiredHelpText))
    {
        throw "Setup helper help omits '$requiredHelpText'."
    }
}

foreach ($requiredOption in @('Preview', 'NonInteractive', 'Yes', 'Json', 'Quiet', 'NoColor', 'OutputMode', 'Verbose', 'Branch', 'SkipDocs'))
{
    if (@($actionConfig.Options | Where-Object Id -eq $requiredOption).Count -ne 1)
    {
        throw "Setup option catalog is missing '$requiredOption'."
    }
}

# ------------------------------------------------------------
# Setup state and service contracts
# ------------------------------------------------------------

# Bootstrap project internals without invoking the setup executable entrypoint.
. (Join-Path $repositoryRoot 'scripts\lib\Bootstrap.ps1') -RepositoryRoot $repositoryRoot
$SetupRoot = Join-Path $repositoryRoot 'scripts\setup'
$SetupActionConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config\setup.json') -Name setup -SchemaPath (Join-Path $repositoryRoot 'scripts\schemas\setup.schema.json')
$EditorConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config\editors.json') -Name editors -SchemaPath (Join-Path $repositoryRoot 'scripts\schemas\editors.schema.json')
$script:SetupStatePath = Join-Path (Get-GameWipStorageAreaPath -Area state) 'setup.json'
$ToolConfig = @{ MsysRoot = Get-GameWipMsys2Root }

foreach ($file in @('Common.ps1', 'Editor.ps1'))
{
    . (Join-Path $SetupRoot (Join-Path 'lib' $file))
}

. (Join-Path $SetupRoot 'lib\Orchestration.ps1')

# Missing Windows host tools must be disclosed before the setup plan is applied.
$originalHostCheck = (Get-Command Test-GameWipWindowsHost).ScriptBlock

try
{
    Set-Item -Path function:Test-GameWipWindowsHost -Value { return $true }
    $hostInstallPlan = @(Get-GameWipWindowsHostInstallPlan)
    $hostInstallPlanText = $hostInstallPlan -join "`n"

    foreach ($expectedPackage in @('Git (Git.Git)', 'GitHub CLI (GitHub.cli)', 'outside build/'))
    {
        if ($hostInstallPlanText -notmatch [regex]::Escape($expectedPackage))
        {
            throw "Windows host install plan omits '$expectedPackage'."
        }
    }
}

finally
{
    Set-Item -Path function:Test-GameWipWindowsHost -Value $originalHostCheck
}

# Missing optional editors follow the same skip policy on both hosts.
$editorTestOverrides = @{}

foreach ($name in @('Test-GameWipWindowsHost', 'Test-GameWipSetupCommand', 'Select-GameWipEditor', 'Confirm-GameWipExternalInstall', 'Install-GameWipEditorIntegration'))
{
    $editorTestOverrides[$name] = (Get-Command -Name $name).ScriptBlock
}

try
{
    Set-Item -Path function:Test-GameWipWindowsHost -Value { return $false }
    Set-Item -Path function:Test-GameWipSetupCommand -Value { param([string]$Name) return $false }
    Set-Item -Path function:Select-GameWipEditor -Value { throw 'Editor selection ran without an installed Linux editor.' }
    $linuxEditorPlan = @(Get-GameWipEditorInstallPlan) -join "`n"

    if ($linuxEditorPlan -notmatch 'skip integrations for missing editors')
    {
        throw 'Linux editor plan does not disclose that missing integrations are skipped.'
    }

    Invoke-GameWipSetupEditorStep -Choose

    Set-Item -Path function:Test-GameWipWindowsHost -Value { return $true }
    $windowsEditorPlan = @(Get-GameWipEditorInstallPlan) -join "`n"

    if ($windowsEditorPlan -notmatch 'Use existing VS Code' -or $windowsEditorPlan -notmatch 'Ask before installing Visual Studio')
    {
        throw 'Windows editor plan does not disclose the VS Code and Visual Studio install policies.'
    }

    Set-Item -Path function:Confirm-GameWipExternalInstall -Value { throw 'Missing VS Code unexpectedly triggered an install prompt.' }
    Set-Item -Path function:Install-GameWipEditorIntegration -Value { throw 'Editor integration ran after its install was declined.' }
    Install-GameWipEditorSelection -RepositoryRoot $repositoryRoot -EditorConfig $EditorConfig -SelectedEditors @('vscode')
}

finally
{
    foreach ($name in $editorTestOverrides.Keys)
    {
        Set-Item -Path "function:$name" -Value $editorTestOverrides[$name]
    }

    Remove-Variable -Name TestSetupHostPlatform -Scope Script -ErrorAction SilentlyContinue
}

if (-not (Test-GameWipWindowsHost))
{
    foreach ($functionName in @('Invoke-GameWipMsys2PacmanWithRetry', 'Get-GameWipMsys2Ownership'))
    {
        if (Get-Command -Name $functionName -ErrorAction SilentlyContinue)
        {
            throw "Windows-only setup function '$functionName' loaded on Linux."
        }
    }

    # Load the Windows package provider only for its retry unit test.
    . (Join-Path $SetupRoot 'platform/windows/Msys2.ps1')

    if (-not (Get-Command Invoke-GameWipUninstall -ErrorAction SilentlyContinue))
    {
        throw 'Linux setup did not load its uninstall implementation.'
    }
}

Assert-GameWipSetupActionCatalog

$originalWindowsHostCheck = (Get-Command Test-GameWipWindowsHost).ScriptBlock

try
{
    foreach ($hostPlatform in @('windows', 'linux'))
    {
        Set-Item -Path function:Test-GameWipWindowsHost -Value { return $script:TestSetupHostPlatform -eq 'windows' }
        $script:TestSetupHostPlatform = $hostPlatform
        $availableActionIds = @(Get-GameWipAvailableSetupActions | ForEach-Object { [string]$_.Id })

        foreach ($action in $SetupActionConfig.Actions)
        {
            $platforms = @(
                if ($action.ContainsKey('Platforms'))
                {
                    $action.Platforms | ForEach-Object { [string]$_ }
                }
            )
            $shouldBeAvailable = $platforms.Count -eq 0 -or $platforms -contains $hostPlatform
            $resolvedAction = Resolve-GameWipSetupActionName -Name ([string]$action.Id).ToUpperInvariant()

            if ($shouldBeAvailable -and ($resolvedAction -cne $action.Id -or $availableActionIds -cnotcontains $action.Id))
            {
                throw "Setup action '$($action.Id)' is missing from the $hostPlatform action catalog."
            }

            if (-not $shouldBeAvailable -and ($null -ne $resolvedAction -or $availableActionIds -ccontains $action.Id))
            {
                throw "Setup action '$($action.Id)' should be hidden on $hostPlatform."
            }
        }
    }
}

finally
{
    Set-Item -Path function:Test-GameWipWindowsHost -Value $originalWindowsHostCheck
    Remove-Variable -Name TestSetupHostPlatform -Scope Script -ErrorAction SilentlyContinue
}

$originalSetupStatePath = $script:SetupStatePath
$setupStateTestRoot = Join-Path $repositoryRoot ('build\gamewip\temp\setup-state-test-' + [guid]::NewGuid().ToString('N'))

try
{
    New-Item -ItemType Directory -Force -Path $setupStateTestRoot | Out-Null
    $script:SetupStatePath = Join-Path $setupStateTestRoot 'setup.json'

    $missingState = Get-GameWipSetupState

    if ($script:SetupStateReadStatus -ne 'missing' -or $missingState.schemaVersion -ne 1)
    {
        throw 'A missing setup state file must continue to produce the first-install default.'
    }

    [ordered]@{
        schemaVersion = 1
        wingetPackages = @('package.id')
        vscodeExtensions = @('publisher.extension')
        systemPackages = @([ordered]@{ manager = 'apt'; package = 'git' })
        msys2InstalledBySetup = $true
    } | ConvertTo-Json | Set-Content -LiteralPath $script:SetupStatePath
    $validState = Get-GameWipSetupState

    if ($script:SetupStateReadStatus -ne 'valid' -or @($validState.wingetPackages) -notcontains 'package.id' -or
        @($validState.vscodeExtensions) -notcontains 'publisher.extension' -or
        @($validState.systemPackages | Where-Object { $_.manager -eq 'apt' -and $_.package -eq 'git' }).Count -ne 1 -or
        -not $validState.msys2InstalledBySetup)
    {
        throw 'A complete setup state must preserve all ownership fields.'
    }

    [ordered]@{
        schemaVersion = 1
        wingetPackages = @('package.id')
        vscodeExtensions = @('publisher.extension')
        msys2InstalledBySetup = $true
    } | ConvertTo-Json | Set-Content -LiteralPath $script:SetupStatePath
    $legacyState = Get-GameWipSetupState

    if ($script:SetupStateReadStatus -ne 'valid' -or @($legacyState.systemPackages).Count -ne 0)
    {
        throw 'Setup state created before system package tracking must remain valid.'
    }

    foreach ($missingField in @('wingetPackages', 'vscodeExtensions', 'msys2InstalledBySetup'))
    {
        $state = [ordered]@{
            schemaVersion = 1
            wingetPackages = @()
            vscodeExtensions = @()
            msys2InstalledBySetup = $false
        }
        $state.Remove($missingField)
        $state | ConvertTo-Json | Set-Content -LiteralPath $script:SetupStatePath

        $invalidState = Get-GameWipSetupState

        if ($script:SetupStateReadStatus -ne 'corrupt' -or $invalidState.schemaVersion -ne 1)
        {
            throw "Setup state missing required ownership field '$missingField' was not rejected."
        }
    }
}

finally
{
    $script:SetupStatePath = $originalSetupStatePath
    Remove-Item -LiteralPath $setupStateTestRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# Linux package ownership records only packages newly added by setup.
$linuxPackageTestRoot = Join-Path $repositoryRoot ('build\gamewip\temp\linux-package-test-' + [guid]::NewGuid().ToString('N'))
$originalSetupStatePath = $script:SetupStatePath
$linuxFunctionNames = @(
    'Get-GameWipLinuxDistribution',
    'Get-GameWipLinuxInstalledPackages',
    'Invoke-GameWipLinuxPrivilegedCommand',
    'Install-GameWipLinuxCMake'
)
$originalLinuxFunctions = @{}

foreach ($name in $linuxFunctionNames)
{
    $command = Get-Command -Name $name -ErrorAction SilentlyContinue
    $originalLinuxFunctions[$name] = if ($null -ne $command) { $command.ScriptBlock } else { $null }
}

. (Join-Path $SetupRoot 'platform/linux/Linux.ps1')
$linuxImplementationFunctions = @{}

foreach ($name in $linuxFunctionNames)
{
    $linuxImplementationFunctions[$name] = (Get-Command -Name $name).ScriptBlock
}

try
{
    New-Item -ItemType Directory -Force -Path $linuxPackageTestRoot | Out-Null
    $script:SetupStatePath = Join-Path $linuxPackageTestRoot 'setup.json'
    $script:LinuxPackageQueryCount = 0
    $script:LinuxPackageCommands = [System.Collections.Generic.List[object]]::new()

    Set-Item -Path function:Get-GameWipLinuxDistribution -Value {
        return [pscustomobject]@{ Id = 'ubuntu'; Name = 'Ubuntu'; PackageManager = 'apt' }
    }
    Set-Item -Path function:Get-GameWipLinuxInstalledPackages -Value {
        param([string]$Manager, [string[]]$Packages)
        ++$script:LinuxPackageQueryCount

        if ($script:LinuxPackageQueryCount -eq 1)
        {
            return @('git')
        }

        return @('git', 'clang')
    }
    Set-Item -Path function:Invoke-GameWipLinuxPrivilegedCommand -Value {
        param([string]$FilePath, [string[]]$Arguments)
        $script:LinuxPackageCommands.Add([pscustomobject]@{ FilePath = $FilePath; Arguments = @($Arguments) }) | Out-Null
    }
    Set-Item -Path function:Install-GameWipLinuxCMake -Value { }

    Invoke-GameWipSetupLinuxPrerequisites
    $recordedPackages = @(Get-GameWipSetupState).systemPackages

    if ($script:SetupStateReadStatus -ne 'valid' -or
        @($recordedPackages | Where-Object { $_.manager -eq 'apt' -and $_.package -eq 'clang' }).Count -ne 1 -or
        @($recordedPackages | Where-Object { $_.package -eq 'git' }).Count -ne 0 -or
        $script:LinuxPackageCommands.Count -ne 2)
    {
        throw 'Linux package ownership did not record only packages newly installed by setup.'
    }
}

finally
{
    $script:SetupStatePath = $originalSetupStatePath

    foreach ($name in $linuxFunctionNames)
    {
        if ($null -ne $originalLinuxFunctions[$name])
        {
            Set-Item -Path "function:$name" -Value $originalLinuxFunctions[$name]
        }
        else
        {
            Remove-Item -Path "function:$name" -ErrorAction SilentlyContinue
        }
    }

    Remove-Item -LiteralPath $linuxPackageTestRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Variable -Name LinuxPackageQueryCount, LinuxPackageCommands -Scope Script -ErrorAction SilentlyContinue
}

$originalSetupStateRoot = [string]$ProjectConfig.storage.state
$editorStateRoot = 'temp/editor-state-test-' + [guid]::NewGuid().ToString('N')

try
{
    $ProjectConfig.storage.state = $editorStateRoot
    Save-GameWipEditorSelection -RepositoryRoot $repositoryRoot -Editors @('vscode')
    Save-GameWipEditorSelection -RepositoryRoot $repositoryRoot -Editors @('vscode', 'visual-studio')
    $savedPreference = Get-Content -Raw -LiteralPath (Get-GameWipEditorPreferencePath -RepositoryRoot $repositoryRoot) | ConvertFrom-Json

    if ($savedPreference.schemaVersion -ne 1 -or @($savedPreference.editors).Count -ne 2)
    {
        throw 'Atomic editor preference persistence lost the supported state document.'
    }
}

finally
{
    $ProjectConfig.storage.state = $originalSetupStateRoot
    Remove-Item -LiteralPath (Resolve-GameWipStoragePath -RelativePath $editorStateRoot) -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------
# Mutation policy and repository setup
# ------------------------------------------------------------

# Setup keeps prompting policy separate from execution mode.
$windowsSource = Get-Content -Raw -LiteralPath $setupScript

if ($windowsSource -notmatch '\[switch\]\$Yes')
{
    throw 'Setup entrypoint does not expose explicit -Yes consent.'
}

if ($windowsSource -match '(?ms)if\s*\(\$NonInteractive\)\s*\{\s*return\s+\$true')
{
    throw 'Setup still treats -NonInteractive as mutation approval.'
}

if ($windowsSource -notmatch '\$OutputMode\s*=\s*''Stream''')
{
    throw 'Setup does not stream native output by default.'
}

$orchestrationSource = Get-Content -Raw -LiteralPath (Join-Path $SetupRoot 'lib\Orchestration.ps1')

if ($orchestrationSource -notmatch 'Invoke-GameWipOperation')
{
    throw 'Setup does not use the shared operation lifecycle.'
}

if ($orchestrationSource -notmatch 'Invoke-GameWipToolEnsure')
{
    throw 'Setup does not use the shared exact tool ensure path.'
}

if ($orchestrationSource -notmatch 'Read-GameWipActionMenuItem')
{
    throw 'Setup does not use the shared declarative menu renderer.'
}

if ($orchestrationSource -notmatch 'Uninstall owns its confirmation so inventory is always printed first')
{
    throw 'Uninstall inventory/consent ordering is not explicit.'
}

$originalNative = (Get-Command Invoke-GameWipNative).ScriptBlock
$script:setupNativeForcedOutputMode = $false
function Invoke-GameWipNative
{
    param([string]$Name, [string]$FilePath, [string[]]$Arguments = @(), [int[]]$AllowedExitCodes = @(0), [string]$OutputMode)
    $null = $Name, $FilePath, $Arguments, $AllowedExitCodes
    $script:setupNativeForcedOutputMode = $PSBoundParameters.ContainsKey('OutputMode')
    return [pscustomobject]@{ ExitCode = 0 }
}

try
{
    Invoke-GameWipSetupNative -FilePath 'sample.exe' | Out-Null

    if ($script:setupNativeForcedOutputMode)
    {
        throw 'Setup native execution forced an output mode instead of inheriting the operation policy.'
    }
}

finally
{
    Set-Item -Path function:Invoke-GameWipNative -Value $originalNative
    Remove-Variable -Name setupNativeForcedOutputMode -Scope Script -ErrorAction SilentlyContinue
}

$originalSwitchRepositoryBranch = (Get-Command Switch-GameWipRepositoryBranch).ScriptBlock
$originalInitializeRepository = (Get-Command Initialize-GameWipRepository).ScriptBlock
$originalTestRepositoryState = (Get-Command Test-GameWipRepositoryState).ScriptBlock
$script:setupChooseBranch = $null
function Switch-GameWipRepositoryBranch
{
    param([string]$RepositoryRoot, [string]$Branch, [switch]$ChooseBranch)
    $null = $RepositoryRoot, $Branch
    $script:setupChooseBranch = [bool]$ChooseBranch
}
function Initialize-GameWipRepository
{
    param([string]$RepositoryRoot)
    $null = $RepositoryRoot
}
function Test-GameWipRepositoryState
{
    param([string]$RepositoryRoot)
    $null = $RepositoryRoot
}
$NonInteractive = $false
$Branch = ''

try
{
    Invoke-GameWipSetupRepositoryStep

    if ($script:setupChooseBranch -ne $true)
    {
        throw 'Existing interactive setup did not request repository branch selection when -Branch was omitted.'
    }
}

finally
{
    Set-Item -Path function:Switch-GameWipRepositoryBranch -Value $originalSwitchRepositoryBranch
    Set-Item -Path function:Initialize-GameWipRepository -Value $originalInitializeRepository
    Set-Item -Path function:Test-GameWipRepositoryState -Value $originalTestRepositoryState
    Remove-Variable -Name setupChooseBranch -Scope Script -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------
# Preview and provider behavior
# ------------------------------------------------------------

$originalSetupRunRoot = [string]$ProjectConfig.storage.runs
$setupPreviewRoot = 'temp/setup-preview-test-' + [guid]::NewGuid().ToString('N')

$originalSetupActionBody = (Get-Command Invoke-GameWipSetupActionBody).ScriptBlock
$originalSetupWindowsAssertion = (Get-Command Assert-GameWipSetupWindows).ScriptBlock
$script:setupDocsBodyRan = $false
function Assert-GameWipSetupWindows
{
    # The preview contract is platform-neutral even though the setup entrypoint
    # itself is intentionally Windows-only.
}
function Invoke-GameWipSetupActionBody
{
    param([string]$SelectedAction)

    if ($SelectedAction -eq 'doc')
    {
        $script:setupDocsBodyRan = $true
    }
}

$Preview = $true
$NonInteractive = $false
$Yes = $false
$OutputMode = 'LogOnly'
$NoColor = $true
$Quiet = $true

try
{
    $ProjectConfig.storage.runs = "$setupPreviewRoot/runs"

    $docsPreview = Invoke-GameWipSetupOperation -SelectedAction doc

    if ($docsPreview.Status -ne 'passed' -or $script:setupDocsBodyRan)
    {
        throw 'setup docs -Preview executed the mutating documentation body.'
    }
}

finally
{
    $ProjectConfig.storage.runs = $originalSetupRunRoot

    $setupPreviewPath = Resolve-GameWipStoragePath -RelativePath $setupPreviewRoot

    if (Test-Path -LiteralPath $setupPreviewPath)
    {
        Remove-Item -LiteralPath $setupPreviewPath -Recurse -Force
    }

    Set-Item -Path function:Invoke-GameWipSetupActionBody -Value $originalSetupActionBody
    Set-Item -Path function:Assert-GameWipSetupWindows -Value $originalSetupWindowsAssertion
    Remove-Variable -Name setupDocsBodyRan -Scope Script -ErrorAction SilentlyContinue
    $Preview = $false
}

# ------------------------------------------------------------
# Ownership, retries, and consent
# ------------------------------------------------------------

# Common ownership markers use the shared envelope and fail conservatively.
$marker = New-GameWipOwnershipMarker -Resource 'test-resource' -Origin created -Payload ([ordered]@{ sample = $true })

if ($marker.schemaVersion -ne 1 -or $marker.owner -ne 'GameWIP' -or $marker.resource -ne 'test-resource' -or $marker.origin -ne 'created')
{
    throw 'Ownership-marker envelope is incomplete.'
}

# Pacman retry remains bounded and reports each attempt.
$script:simulatedAttempts = 0
$script:simulatedFailures = 1
function Invoke-GameWipMsys2
{
    param([string]$MsysRoot, [string]$Command)
    $null = $MsysRoot, $Command
    ++$script:simulatedAttempts

    if ($script:simulatedFailures -gt 0)
    {
        --$script:simulatedFailures
        throw 'simulated transient failure'
    }
}
$output = @(& { Invoke-GameWipMsys2PacmanWithRetry -MsysRoot 'C:\TestMsys2' -Command 'pacman -Syu --noconfirm' -MaxAttempts 3 -RetryDelaySeconds 0 } *>&1)

if ($script:simulatedAttempts -ne 2)
{
    throw "Pacman retry made $script:simulatedAttempts attempts instead of 2."
}

if (($output -join "`n") -notmatch 'attempt 2 of 3')
{
    throw 'Pacman retry feedback omitted the successful retry.'
}

if (Test-GameWipWindowsHost)
{
    # Incomplete uninstall entries must not break MSYS2 discovery.
    function Get-ItemProperty
    {
        param([string]$Path, [string]$ErrorAction)
        $null = $Path, $ErrorAction
        return @(
            [pscustomobject]@{ InstallLocation = 'C:\GameWIP-Test\MSYS2' }
            [pscustomobject]@{ DisplayName = 'MSYS2' }
        )
    }

    try
    {
        $null = @(Get-GameWipExistingMsys2Roots)
    }

    finally
    {
        Remove-Item function:Get-ItemProperty -ErrorAction SilentlyContinue
    }

    # Prepare a deterministic installed MSYS2 fixture.
    $msys2PlanFunctionNames = @(
        'Get-GameWipMsys2Root',
        'Get-GameWipMsys2PackageConfig',
        'Get-GameWipMsys2InstalledPackageNames'
    )
    $originalMsys2PlanFunctions = @{}

    foreach ($name in $msys2PlanFunctionNames)
    {
        $originalMsys2PlanFunctions[$name] = (Get-Command -Name $name).ScriptBlock
    }

    $msys2PlanRoot = Join-Path (Get-GameWipStorageAreaPath -Area temp) ('msys2-plan-' + [guid]::NewGuid().ToString('N'))
    $msys2PlanBinaryDirectory = Join-Path (Join-Path $msys2PlanRoot 'usr') 'bin'
    $Script:Msys2PlanRoot = $msys2PlanRoot
    $Script:Msys2PlanInstalledPackages = @('common-test', 'ucrt-test', 'clang-test')

    try
    {
        New-Item -ItemType Directory -Path $msys2PlanBinaryDirectory -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $msys2PlanBinaryDirectory 'bash.exe'), '')

        Set-Item -Path function:Get-GameWipMsys2Root -Value { return $Script:Msys2PlanRoot }
        Set-Item -Path function:Get-GameWipMsys2PackageConfig -Value {
            param([hashtable]$ProjectTools)
            $null = $ProjectTools
            return @{ Common = @('common-test'); Ucrt64 = @('ucrt-test'); Clang64 = @('clang-test') }
        }
        Set-Item -Path function:Get-GameWipMsys2InstalledPackageNames -Value {
            param([string]$MsysRoot)
            $null = $MsysRoot
            return [pscustomobject]@{ State = 'ready'; Packages = @($Script:Msys2PlanInstalledPackages); Reason = $null }
        }

        # Exercise empty and single-package missing sets.
        $completePlan = @(Get-GameWipMsys2SetupPlan)

        if (@($completePlan | Where-Object { $_ -like 'Install * MSYS2 packages:*' }).Count -ne 0)
        {
            throw 'MSYS2 setup planned package installation when every package was present.'
        }

        $Script:Msys2PlanInstalledPackages = @('common-test', 'clang-test')
        $partialPlan = @(Get-GameWipMsys2SetupPlan)

        if ($partialPlan -notcontains 'Install Ucrt64 MSYS2 packages: ucrt-test.')
        {
            throw 'MSYS2 setup omitted a single missing package.'
        }
    }

    finally
    {
        # Restore discovery functions and remove the fixture.
        foreach ($name in $msys2PlanFunctionNames)
        {
            Set-Item -Path "function:$name" -Value $originalMsys2PlanFunctions[$name]
        }

        if (Test-Path -LiteralPath $msys2PlanRoot)
        {
            Remove-Item -LiteralPath $msys2PlanRoot -Recurse -Force
        }

        Remove-Variable -Name Msys2PlanRoot, Msys2PlanInstalledPackages -Scope Script -ErrorAction SilentlyContinue
    }
}

# Destructive setup actions must never be consented by non-interactive mode alone.
$Script:OperationContext = New-GameWipOperationContext -Label 'setup-consent' -NonInteractive
$rejected = $false

try
{
    Confirm-GameWipMutation -Summary 'setup mutation' -Risk destructive -Plan @('remove test resource') | Out-Null
}

catch
{
    $rejected = $_.Exception.Data['GameWipCode'] -eq 'consent-required'
}

if (-not $rejected)
{
    throw 'Non-interactive setup implicitly authorized a destructive action.'
}

$Script:OperationContext = $null

# Missing host applications default to No; an explicit -Yes accepts the plan.
$originalYesNoPrompt = (Get-Command Read-GameWipYesNo).ScriptBlock
$originalOperationContext = $Script:OperationContext

try
{
    Set-Item -Path function:Read-GameWipYesNo -Value { param([string]$Prompt, [bool]$Default) return $false }
    $Script:OperationContext = New-GameWipOperationContext -Label 'external-install-consent'

    if (Confirm-GameWipExternalInstall -Name 'GitHub CLI' -Provider WinGet -Package 'GitHub.cli')
    {
        throw 'A declined external host install was approved.'
    }

    $Script:OperationContext = New-GameWipOperationContext -Label 'external-install-yes' -Yes

    if (-not (Confirm-GameWipExternalInstall -Name 'GitHub CLI' -Provider WinGet -Package 'GitHub.cli'))
    {
        throw 'An explicitly approved external host install was declined.'
    }
}

finally
{
    Set-Item -Path function:Read-GameWipYesNo -Value $originalYesNoPrompt
    $Script:OperationContext = $originalOperationContext
}

if (-not (Test-GameWipWindowsHost))
{
    # Linux uninstall removes only marked tools and never follows nested links.
    $testRoot = Resolve-GameWipStoragePath -RelativePath ('temp/linux-uninstall-test-' + [guid]::NewGuid().ToString('N'))
    $managedRoot = Join-Path $testRoot 'managed'
    $originalSetupStatePath = $script:SetupStatePath
    $originalLaunchToken = [string]$env:GAMEWIP_SETUP_LAUNCH_TOKEN
    $overrideNames = @(
        'Get-GameWipManagedToolRoot',
        'Get-GameWipSetupState',
        'Get-GameWipEditorPreferencePath',
        'Invoke-GameWipEditorIntegrationRemoval',
        'Test-GameWipSetupCommand',
        'Confirm-GameWipMutation',
        'Test-GameWipLinuxCurrentPowerShell',
        'Get-GameWipLinuxInstalledPackages',
        'Remove-GameWipLinuxSystemPackages'
    )
    $originalOverrides = @{}

    foreach ($name in $overrideNames)
    {
        $originalOverrides[$name] = (Get-Command -Name $name).ScriptBlock
    }

    try
    {
        New-Item -ItemType Directory -Force -Path $managedRoot | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $testRoot 'cache') | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $managedRoot 'tools/llvm-mingw/20261104') | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $managedRoot 'tools/powershell/7.7.0') | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $managedRoot 'tools/custom') | Out-Null

        foreach ($name in @('bin', 'npm', 'python', 'powershell', 'cmake'))
        {
            New-Item -ItemType Directory -Force -Path (Join-Path $managedRoot $name) | Out-Null
            Set-Content -LiteralPath (Join-Path $managedRoot "$name/payload.txt") -Value $name
        }

        $rootMarker = New-GameWipOwnershipMarker -Resource project-tools -Origin created -Payload ([ordered]@{
                installedBySetup = $true
                adoptedByUser = $false
            })
        Write-GameWipJsonAtomic -Path (Join-Path $managedRoot '.gamewip-managed.json') -Value $rootMarker

        $llvmMingwTool = Get-GameWipProjectTool -Id llvm-mingw
        $llvmMingwAsset = $llvmMingwTool.provider.assets['linux-amd64']

        foreach ($tool in @(
                @{ Path = (Join-Path $managedRoot 'cmake'); Id = 'cmake'; Version = '4.4.4'; Archive = 'cmake-4.4.4-linux-x86_64.tar.gz'; Sha256 = ('a' * 64); Marker = '.gamewip-tool.json' },
                @{ Path = (Join-Path $managedRoot 'tools/llvm-mingw/20261104'); Id = 'llvm-mingw'; Version = [string]$llvmMingwTool.requiredVersion; Archive = [string]$llvmMingwAsset.archive; Sha256 = [string]$llvmMingwAsset.sha256; Marker = '.gamewip-tool.json' }
            ))
        {
            Write-GameWipJsonAtomic -Path (Join-Path $tool.Path $tool.Marker) -Value ([ordered]@{
                    schemaVersion = 1
                    toolId = $tool.Id
                    version = $tool.Version
                    archive = $tool.Archive
                    sha256 = $tool.Sha256
                })
        }

        $powerShellRoot = Join-Path $managedRoot 'tools/powershell/7.7.0'
        Set-Content -LiteralPath (Join-Path $powerShellRoot 'pwsh') -Value 'runtime'
        Set-Content -LiteralPath (Join-Path $powerShellRoot '.gamewip-tool') -Value @(
            'version=7.7.0',
            'archive=powershell-7.7.0-linux-x64.tar.gz',
            ('sha256=' + ('b' * 64))
        )
        Set-Content -LiteralPath (Join-Path $managedRoot 'tools/custom/user.txt') -Value 'preserve'
        Set-Content -LiteralPath (Join-Path $testRoot 'cache/keep.txt') -Value 'preserve'
        Set-Content -LiteralPath (Join-Path $testRoot 'build-target.json') -Value 'preserve'
        Set-Content -LiteralPath (Join-Path $testRoot 'run-receipt.txt') -Value 'preserve'
        $sentinelPath = Join-Path $testRoot 'outside-sentinel.txt'
        Set-Content -LiteralPath $sentinelPath -Value 'preserve'
        [void][IO.File]::CreateSymbolicLink((Join-Path $managedRoot 'python/outside-link'), $sentinelPath)
        $script:SetupStatePath = Join-Path $testRoot 'setup.json'
        $editorPreferencePath = Join-Path $testRoot 'editor-selection.json'
        Set-Content -LiteralPath $script:SetupStatePath -Value '{}'
        Set-Content -LiteralPath $editorPreferencePath -Value '{}'

        Set-Item -Path function:Get-GameWipManagedToolRoot -Value { return $script:LinuxUninstallTestManagedRoot }
        $script:LinuxUninstallTestSystemPackages = @(
            [ordered]@{
                manager = (Get-GameWipLinuxDistribution).PackageManager
                package = 'gamewip-test-package'
            }
        )
        $script:LinuxUninstallRemovedPackages = @()
        Set-Item -Path function:Get-GameWipSetupState -Value { return [ordered]@{ vscodeExtensions = @(); systemPackages = $script:LinuxUninstallTestSystemPackages } }
        Set-Item -Path function:Get-GameWipEditorPreferencePath -Value { return $script:LinuxUninstallTestEditorPreferencePath }
        Set-Item -Path function:Invoke-GameWipEditorIntegrationRemoval -Value { }
        Set-Item -Path function:Test-GameWipSetupCommand -Value { return $false }
        Set-Item -Path function:Confirm-GameWipMutation -Value { return $true }
        Set-Item -Path function:Get-GameWipLinuxInstalledPackages -Value { param([string]$Manager, [string[]]$Packages) return $Packages }
        Set-Item -Path function:Remove-GameWipLinuxSystemPackages -Value {
            param([string]$Manager, [string[]]$Packages)
            $script:LinuxUninstallRemovedPackages = @($Packages)
        }
        Set-Item -Path function:Test-GameWipLinuxCurrentPowerShell -Value {
            param([string]$Path)
            return $Path -eq $script:LinuxUninstallTestCurrentPowerShell
        }
        $script:LinuxUninstallTestManagedRoot = $managedRoot
        $script:LinuxUninstallTestEditorPreferencePath = $editorPreferencePath
        $script:LinuxUninstallTestCurrentPowerShell = $powerShellRoot
        Remove-Item Env:GAMEWIP_SETUP_LAUNCH_TOKEN -ErrorAction SilentlyContinue

        $inventory = Get-GameWipLinuxUninstallInventory -RepositoryRoot $repositoryRoot

        if ($inventory.RemovePaths -notcontains (Join-Path $managedRoot 'cmake') -or
            $inventory.RemovePaths -notcontains (Join-Path $managedRoot 'tools/llvm-mingw/20261104') -or
            $inventory.Preserved -notcontains "$powerShellRoot (currently running helper; use system PowerShell to remove it)")
        {
            throw "Linux uninstall inventory mismatch. Planned: $($inventory.RemovePaths -join ', '). Preserved: $($inventory.Preserved -join ', ')."
        }

        $launchToken = '123-456-789'
        $env:GAMEWIP_SETUP_LAUNCH_TOKEN = $launchToken
        $inventory = Get-GameWipLinuxUninstallInventory -RepositoryRoot $repositoryRoot

        if ($inventory.DeferredPowerShellPaths -notcontains $powerShellRoot)
        {
            throw 'Linux uninstall did not schedule its active managed PowerShell runtime for cleanup.'
        }

        Invoke-GameWipUninstall -RepositoryRoot $repositoryRoot

        if ($script:LinuxUninstallRemovedPackages -notcontains 'gamewip-test-package')
        {
            throw 'Linux uninstall did not remove the recorded GameWIP-added package.'
        }

        foreach ($name in @('bin', 'npm', 'python', 'powershell', 'cmake', 'tools/llvm-mingw/20261104'))
        {
            if (Test-Path -LiteralPath (Join-Path $managedRoot $name))
            {
                throw "Linux uninstall left managed tool data at '$name'."
            }
        }

        foreach ($path in @(
                (Join-Path $managedRoot 'tools/custom/user.txt'),
                (Join-Path $powerShellRoot '.gamewip-tool'),
                (Join-Path $testRoot 'cache/keep.txt'),
                (Join-Path $testRoot 'build-target.json'),
                (Join-Path $testRoot 'run-receipt.txt'),
                $sentinelPath
            ))
        {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf))
            {
                throw "Linux uninstall removed preserved data at '$path'."
            }
        }

        $cleanupToken = Get-Content -Raw -LiteralPath (Join-Path $powerShellRoot '.gamewip-uninstall-pending')

        if ($cleanupToken -cne $launchToken)
        {
            throw 'Linux uninstall wrote an invalid deferred PowerShell cleanup token.'
        }

        if ((Test-Path -LiteralPath $script:SetupStatePath) -or (Test-Path -LiteralPath $editorPreferencePath))
        {
            throw 'Linux uninstall left disposable setup state behind.'
        }
    }

    finally
    {
        $script:SetupStatePath = $originalSetupStatePath

        if ([string]::IsNullOrEmpty($originalLaunchToken))
        {
            Remove-Item Env:GAMEWIP_SETUP_LAUNCH_TOKEN -ErrorAction SilentlyContinue
        }
        else
        {
            $env:GAMEWIP_SETUP_LAUNCH_TOKEN = $originalLaunchToken
        }

        foreach ($name in $overrideNames)
        {
            Set-Item -Path "function:$name" -Value $originalOverrides[$name]
        }

        Remove-Variable -Name LinuxUninstallTestManagedRoot, LinuxUninstallTestEditorPreferencePath, LinuxUninstallTestCurrentPowerShell -Scope Script -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host 'Setup helper regression tests passed.'
